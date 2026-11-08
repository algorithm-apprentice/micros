"""MEM04's exact authority plan and actual execution journal."""

import ast
from contextlib import contextmanager
from dataclasses import dataclass
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import stat
import subprocess
import uuid

from tests.host.evidence_authority_source import literal_value, single_assignment
from tests.host.evidence_authority_types import (
    AuthorityClass, AuthorityKind, Environment, EventKind, Oracle, Plan, Provenance,
    TaskAuthority, TransitionClaim, TransitionRegistry, TransitionState, digest, fact_set_digest,
)
from tests.host.selected_address_space_source import (
    AUTHORITY_PATH, FAMILY, RECIPE_ADMISSION, SOURCE_PATH, SOURCE_PROJECTION, SOURCE_ROLES,
    candidate_paths, read_declarations,
)
from tools.stack_artifact_files import read_regular


TRANSITION = "MEM04-selected-address-space"
COMPLETED_CLAIM = ("MEM04-selected-address-space", "MEM04", ("B02-F005",))
DESIGN_PINS = {
    "design_commit": "60e8b8e160b1c063e448cd32b0418535dd63bbb7",
    "design_tree": "28a01d02f4c12a148627f23d31cb0de512c657f4",
    "implementation_base_commit": "60e8b8e160b1c063e448cd32b0418535dd63bbb7",
    "implementation_base_tree": "28a01d02f4c12a148627f23d31cb0de512c657f4",
    "runtime_reference": "f28377decc0d20533e05330f2f2211db3f230910",
    "runtime_tree": "4b56076479ac53092c4c83d9c9b52829b66139ba",
    "minix_reference": "4db99f4012570a577414fe2a43697b2f239b699e",
    "minix_tree": "80db89de58d5ecaf31efe48b4d1d98c7e285e81f",
    "design_document": "docs/architecture/selected-user-address-space.md",
    "adr_document": "docs/adr/0107-selected-user-address-space-validation.md",
    "research_document": "docs/research/minix-selected-user-address-space.md",
    "reporting_adr_document": "docs/adr/0108-qemu-accepted-transcript-reporting.md",
    "reporting_contract_document": "docs/architecture/qemu-runner-source-organization.md",
    "handoff_adr_document": "docs/adr/0109-handoff-preflight-error-ownership.md",
    "stack_adr_document": "docs/adr/0110-kernel-visitor-extension-admission.md",
    "stack_contract_document": "docs/architecture/stack-bound-evidence.md",
    "fatal_adr_document": "docs/adr/0111-fatal-tty-header-extension.md",
}
ORACLE_RULES = (
    (
        "mem04-selected-path",
        "tests.host.selected_address_space_evidence.SelectedAddressSpaceOracles.test_selected_path",
        "actual-selected-path-work;owner-before-dereference;exception-scoping;exact-allocation-compensation",
        "ADR-0107", "Independent evidence and phase sequence",
        (2, 3, 5, 6, 7, 8, 10, 11, 12, 13, 15, 16, 17, 18, 19, 20, 21, 22, 23, 25, 26, 27, 28, 29, 30, 32),
    ),
    (
        "mem04-root-activation",
        "tests.host.selected_address_space_evidence.SelectedAddressSpaceOracles.test_root_activation",
        "actual-root-preparation-and-scheduler-work;hardware-admission-and-satp;retained-diagnostic-rejection",
        "ADR-0107", "Independent evidence and phase sequence",
        (1, 4, 9, 14, 24, 31),
    ),
    (
        "mem04-scoped-diagnostic",
        "tests.host.selected_address_space_evidence.SelectedAddressSpaceOracles.test_scoped_diagnostic",
        "actual-handed-off-missing-translation;separate-complete-diagnosis;unchanged-output-and-state",
        "ADR-0107", "Independent evidence and phase sequence",
        (0,),
    ),
)

RECERTIFICATION_UNITS = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
EXTERNAL_INPUTS = (
    "@mem04/native-compiler", "@mem04/native-nm", "@mem04/closed-tools", "@mem04/native-cache",
    "@mem04/qemu-firmware", "@mem04/accepted-design", "@mem04/fixed-adapters",
)
PHASE_KINDS = {
    "red": EventKind.RED_OBSERVED,
    "green": EventKind.GREEN_OBSERVED,
    "recertify": EventKind.AUTHOR_RECERTIFIED,
}
JOURNAL_PATH = "build/evidence-transitions/MEM04"
_provisional = None


@dataclass(frozen=True)
class AuthorContext:
    root: Path
    phase: str
    candidate: object
    authority: TaskAuthority
    authorities: dict
    plan: Plan
    registry: TransitionRegistry
    inventory: object
    environment: Environment
    files: dict
    native_compiler: dict
    native_nm: dict


@dataclass(frozen=True)
class Journal:
    directory: Path
    attempt: str
    records: tuple
    sha256: str
    selector_sha256: str


def git(root, *arguments):
    completed = subprocess.run(
        ("git", *arguments), cwd=root, stdin=subprocess.DEVNULL,
        capture_output=True, timeout=30,
    )
    if completed.returncode:
        raise ValueError("MEM04 fixed Git authority is unavailable: " + completed.stderr.decode())
    return completed.stdout


def design_blob(root, path):
    if path not in tuple(DESIGN_PINS[key] for key in (
        "design_document", "adr_document", "research_document",
        "reporting_adr_document", "reporting_contract_document", "handoff_adr_document",
        "stack_adr_document", "stack_contract_document", "fatal_adr_document",
    )):
        raise ValueError("MEM04 design input leaves its fixed namespace")
    for commit, tree in (
        (DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"]),
        (DESIGN_PINS["runtime_reference"], DESIGN_PINS["runtime_tree"]),
        (DESIGN_PINS["implementation_base_commit"], DESIGN_PINS["implementation_base_tree"]),
    ):
        if git(root, "rev-parse", commit + "^{tree}").decode().strip() != tree:
            raise ValueError("MEM04 fixed commit/tree identity changed")
    return git(root, "show", DESIGN_PINS["design_commit"] + ":" + path)


def base_membership(root):
    result = {}
    entries = git(root, "ls-tree", "-r", "-z", DESIGN_PINS["implementation_base_commit"])
    for row in filter(None, entries.split(b"\0")):
        metadata, separator, raw_path = row.partition(b"\t")
        fields = metadata.split()
        path = raw_path.decode()
        if (
            not separator or len(fields) != 3 or fields[0] not in (b"100644", b"100755")
            or fields[1] != b"blob" or path in result or Path(path).is_absolute()
            or ".." in Path(path).parts or "\\" in path
        ):
            raise ValueError("MEM04 implementation base has an unadmitted source member")
        result[path] = (fields[0].decode(), fields[2].decode())
    if digest(result) != SOURCE_ROLES["base_membership_sha256"]:
        raise ValueError("MEM04 complete implementation-base mode/blob membership changed")
    return {path: (int(mode, 8) & 0o777, oid) for path, (mode, oid) in result.items()}


def historical_predecessor_records(root, inventory):
    from tests.host.evidence_authority_validation import validate_inventory
    from tests.host.selected_address_space_source import fixed_source

    result = validate_inventory(inventory)
    if not result.ok:
        raise ValueError("MEM04 current authority inventory is invalid: " + str(result.error))
    tree = ast.parse(fixed_source(str(root), "tests/host/evidence_authority_inventory.py").decode())
    expected = literal_value(single_assignment(tree, "DECLARED_FAMILY_FACT_DIGESTS").value)
    if type(expected) is not dict or len(expected) != 19 or FAMILY in expected:
        raise ValueError("MEM04 historical family register changed")
    current = dict(inventory.declared_family_fact_digests)
    if any(current.get(name) != fingerprint for name, fingerprint in expected.items()):
        raise ValueError("MEM04 changed a historical family fingerprint")
    records = tuple(fact for fact in inventory.facts if fact.family in expected)
    if len(records) != 4774:
        raise ValueError("MEM04 historical predecessor membership changed")
    return records


def declared_scope(root):
    from tests.host.evidence_authority_inventory import current_inventory

    for key in (
        "adr_document", "reporting_adr_document", "handoff_adr_document",
        "stack_adr_document", "fatal_adr_document",
    ):
        if b"\n- Status: Accepted\n" not in design_blob(root, DESIGN_PINS[key]):
            raise ValueError("MEM04 requires its separately merged Accepted designs")
    contract = design_blob(root, DESIGN_PINS["design_document"]).decode()
    blocks = re.findall(r"^```text\n(.*?)\n```$", contract, re.S | re.M)

    def block(prefix):
        matches = [tuple(value.splitlines()) for value in blocks if value.startswith(prefix)]
        if len(matches) != 1:
            raise ValueError("MEM04 design lost an exact source/identity boundary")
        return matches[0]

    if block("tests/host/selected_address_space_test.c\n") != SOURCE_ROLES["immutable_additions"]:
        raise ValueError("MEM04 evidence owners differ from the Accepted design")
    if block("tools/stack_bound_bindings.py\n") != SOURCE_ROLES["stack_admission_inputs"]:
        raise ValueError("MEM04 stack admission inputs differ from the Accepted design")
    keys = [
        "tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/" + name + "/2"
        for name in block("user_address_space_internal.h\n")
    ]
    for family, owner in (
        ("MEM02", "tests/host/memory_native_source_bindings.py"),
        ("MEM03", "tests/host/selected_ownership_native_source_bindings.py"),
    ):
        keys.extend(owner + ":native-source-admission:" + family + "/native-input/" + path
                    for path in block("include/micros/user_address_space.h\n"))
    prefix = "tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/"
    keys.extend(prefix + "function/kernel/memory/user_address_space_vm_scratch.c/" + name
                for name in block("prepare_vm_scratch_mapping_leaf\n"))
    keys.extend((
        prefix + "native-input/kernel/vm/vm_handoff_core.c",
        prefix + "definitions/kernel/memory/user_address_space_validation.c",
        prefix + "definitions/kernel/vm/vm_handoff_runtime.c",
        "tests/host/test_kernel_vm_handoff_source_layout.py:data-entry:STABLE_HEADER_HASHES/kernel/vm_handoff_core.h",
        "tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/kernel/tty_handoff_runtime.h",
        "tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler.c/validate_selected_thread",
    ))
    if tuple(sorted(keys)) != SOURCE_PROJECTION["retirement_keys"]:
        raise ValueError("MEM04 retirement differs from its fixed 33-clause contract")
    inventory = current_inventory(root)
    prior = historical_predecessor_records(root, inventory)
    if fact_set_digest(prior) != SOURCE_PROJECTION["predecessor_facts_sha256"]:
        raise ValueError("MEM04 changed a complete predecessor authority record")
    affected = tuple(fact for fact in inventory.facts if fact.key in keys)
    if fact_set_digest(affected) != SOURCE_PROJECTION["affected_facts_sha256"]:
        raise ValueError("MEM04 changed its independently frozen affected records")
    closure = {path for fact in affected for path in (fact.owner_path, *fact.payload_paths)}
    dependencies = set(SOURCE_ROLES["dependent_production_paths"])
    if (
        block("kernel/memory/user_address_space_handoff.c") != SOURCE_ROLES["dependent_production_paths"]
        or len(closure) != 28 or closure & dependencies
        or closure | dependencies != set(candidate_paths())
    ):
        raise ValueError("MEM04 candidate paths differ from its exact closure and handoff dependency")
    production = {path for path in closure if path.startswith(("kernel/", "include/")) and not path.startswith("kernel/tests/")}
    if production | dependencies != set(SOURCE_ROLES["production_paths"]):
        raise ValueError("MEM04 production/frozen-adapter roles are inconsistent")
    return contract


def declared_commands(contract, executable):
    from tools.python_host_runtime import python_command

    phases = re.findall(
        r'^"\$\{MICROS_PYTHON\}" -E -S -B tools/check_selected_address_space.py --(red|green|recertify)$',
        contract, re.M,
    )
    if tuple(phases) != tuple(PHASE_KINDS):
        raise ValueError("MEM04 design lacks its three exact recording commands")
    commands = {phase: python_command(executable, RECIPE_ADMISSION["driver"], "--" + phase) for phase in phases}
    rows = re.findall(r"^\| (B[0-9]{2}) \| (.*?) \|$", contract, re.M)
    if tuple(unit for unit, _ in rows) != RECERTIFICATION_UNITS:
        raise ValueError("MEM04 design lacks its exact affected-unit inventory")
    units = []
    for unit, spelling in rows:
        if unit == "B14":
            if spelling != (
                "The actual `--recertify` invocation above, including the complete current native/target "
                "oracle replay, retained MEM01/MEM02/MEM03 evidence, granular authority, source/recipe and Python-owner checks"
            ):
                raise ValueError("MEM04 B14 is not the actual author transaction")
            command = commands["recertify"]
        else:
            if not spelling.startswith("`") or not spelling.endswith("`"):
                raise ValueError("MEM04 unit command has unbound syntax")
            command = tuple(shlex.split(spelling[1:-1]))
            if len(command) != 4 or command[:3] != ("cmake", "--workflow", "--preset"):
                raise ValueError("MEM04 unit command has an undeclared form")
        units.append((unit, command))
    return commands, tuple(units)


def task_definition(root):
    from tests.host import selected_ownership_authority as previous
    from tools.python_host_runtime import require_current_runtime, selected_executable

    require_current_runtime()
    contract = declared_scope(root)
    phases, commands = declared_commands(contract, selected_executable(dict(os.environ)))
    candidates = candidate_paths()
    members = tuple(sorted(set(base_membership(root)) | set(SOURCE_ROLES["immutable_additions"])))
    immutable = tuple(path for path in members if path not in candidates)
    authority = TaskAuthority(
        "MEM04", "B02", ("B02-F005",), DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"],
        DESIGN_PINS["design_document"], RECERTIFICATION_UNITS,
        phases["red"], phases["green"], phases["recertify"], commands,
        immutable, tuple((unit, candidates) for unit in RECERTIFICATION_UNITS),
        DESIGN_PINS["runtime_reference"], DESIGN_PINS["runtime_tree"],
    )
    inputs = (
        DESIGN_PINS["design_document"], DESIGN_PINS["adr_document"],
        DESIGN_PINS["research_document"], DESIGN_PINS["reporting_adr_document"],
        DESIGN_PINS["reporting_contract_document"], DESIGN_PINS["handoff_adr_document"],
        DESIGN_PINS["stack_adr_document"], DESIGN_PINS["stack_contract_document"],
        DESIGN_PINS["fatal_adr_document"],
        *SOURCE_ROLES["immutable_additions"], *SOURCE_ROLES["stack_admission_inputs"],
    )
    keys = SOURCE_PROJECTION["retirement_keys"]
    if any("\n## " + row[4] + "\n" not in contract for row in ORACLE_RULES):
        raise ValueError("MEM04 oracle provenance lacks its Accepted section")
    oracles = tuple(Oracle(
        identifier, test, property_name, tuple(keys[index] for index in positions),
        AuthorityClass.BASELINE_ORACLE,
        Provenance(AuthorityKind.ACCEPTED_ADAPTATION, path=DESIGN_PINS["design_document"], adr=adr, section=section),
        inputs,
    ) for identifier, test, property_name, adr, section, positions in ORACLE_RULES)
    plan = Plan(
        TRANSITION, "MEM04", "B02", ("B02-F005",),
        DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"], DESIGN_PINS["design_document"],
        keys, (), keys, oracles, RECERTIFICATION_UNITS, EXTERNAL_INPUTS,
        phases["red"], phases["green"], phases["recertify"], commands,
    )
    if previous.committed_claim(root) is None:
        raise ValueError("MEM04 requires the committed MEM03 authority chain")
    _, authorities, _, registry = previous.task_definition(root)
    claim = TransitionClaim(TRANSITION, "MEM04", keys, tuple(row.oracle_id for row in oracles), keys)
    return authority, {**authorities, "MEM04": authority}, plan, TransitionRegistry((*registry.claims, claim))


def current_membership(root):
    base = base_membership(root)
    expected = set(base) | set(SOURCE_ROLES["immutable_additions"])
    observed = tuple(path.decode() for path in git(root, "ls-files", "-z").split(b"\0") if path)
    if len(observed) != len(set(observed)) or set(observed) != expected:
        raise ValueError("MEM04 staged membership is incomplete or contains unowned inputs")
    roots = tuple(sorted({Path(path).parts[0] for path in expected}))
    if git(root, "ls-files", "--others", "-z", "--", *roots):
        raise ValueError("MEM04 source namespaces contain untracked or ignored inputs")
    return tuple(sorted(expected)), base


def external_inputs(root):
    import shutil
    from tests.host.diagnostic_history_evidence import closed_tool_inputs
    from tests.host.runtime_memory_evidence import native_compiler
    from tests.host.selected_address_space_evidence import native_symbols_tool
    from tools.stack_bound_fatal_firmware import resolve_default_firmware

    compiler, nm = native_compiler(root), native_symbols_tool()
    qemu = shutil.which("qemu-system-riscv64")
    if qemu is None:
        raise ValueError("MEM04 requires its configured target tool")
    return compiler, nm, {
        "@mem04/native-compiler": digest(compiler), "@mem04/native-nm": digest(nm),
        "@mem04/closed-tools": digest(closed_tool_inputs(root)),
        "@mem04/native-cache": digest(read_regular(Path(root) / "build/host-debug/CMakeCache.txt")[1]),
        "@mem04/qemu-firmware": digest(resolve_default_firmware(qemu)),
        "@mem04/accepted-design": digest({
            "pins": DESIGN_PINS,
            **{name: hashlib.sha256(design_blob(root, DESIGN_PINS[name])).hexdigest()
               for name in (
                   "design_document", "adr_document", "research_document",
                   "reporting_adr_document", "reporting_contract_document", "handoff_adr_document",
                   "stack_adr_document", "stack_contract_document", "fatal_adr_document",
               )},
        }),
        "@mem04/fixed-adapters": digest({
            path: read_regular(Path(root) / path)[1]
            for path in (
                *SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["unchanged_candidates"],
                *SOURCE_ROLES["stack_admission_inputs"],
            )
        }),
    }


def capture_author_context(root, phase):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host.evidence_authority_validation import validate_transition
    from tests.host.selected_address_space_source import fixed_source, validate_retained_source, validate_scaffold
    from tests.host.work_count_types import Stage
    from tools.stack_bound_manifest import owned_artifact
    from tools.validation_context import capture_candidate

    if phase not in PHASE_KINDS:
        raise ValueError("MEM04 source scope requires an exact author phase")
    root = Path(root).resolve()
    candidate = capture_candidate(root)
    if not candidate.clean:
        raise ValueError("MEM04 requires the complete clean staged candidate")
    members, base = current_membership(root)
    files = {path: read_regular(owned_artifact(root, path))[1] for path in members}
    validate_scaffold(root)
    for path in SOURCE_ROLES["production_paths"]:
        data = read_regular(root / path)[0]
        stage = Stage.RED if data == fixed_source(str(root), path) else Stage.GREEN
        if phase == "red" and stage is not Stage.RED:
            raise ValueError("MEM04 actual Red must precede every production edit")
        validate_retained_source(root, path, data.decode(), stage)
    for path, (mode, oid) in base.items():
        if path in SOURCE_ROLES["production_paths"] or path in SOURCE_ROLES["fixed_adapters"]:
            continue
        if not path.startswith(("arch/", "kernel/", "include/", "lib/", "servers/", "user/")) and path not in SOURCE_ROLES["unchanged_candidates"]:
            continue
        data, identity = read_regular(root / path)
        observed = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if (identity["mode"], observed) != (mode, oid):
            raise ValueError("MEM04 changed production/literals outside its seventeen subjects: " + path)
    authority, authorities, plan, registry = task_definition(root)
    compiler, nm, external = external_inputs(root)
    candidates = candidate_paths()
    immutable = tuple(sorted(
        [(path, digest(files[path])) for path in members if path not in candidates] + list(external.items())
    ))
    red = []
    for path in candidates:
        if path in SOURCE_ROLES["production_paths"]:
            data = fixed_source(str(root), path)
            identity = {"kind": "file", "mode": base[path][0], "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        else:
            identity = files[path]
        red.append((path, digest(identity)))
    current = tuple(sorted((path, digest(files[path])) for path in candidates))
    environment = Environment(
        immutable, tuple(sorted(red)), current, tuple((unit, current) for unit in RECERTIFICATION_UNITS),
        candidate.source["head"], candidate.source["tree"],
    )
    inventory = current_inventory(root)
    result = validate_transition(inventory, authorities, registry, plan, (), environment)
    if not result.ok or result.state is not TransitionState.REPLACEMENT_REGISTERED:
        raise ValueError("MEM04 source/declaration closure failed: " + str(result.error))
    if capture_candidate(root) != candidate:
        raise ValueError("MEM04 source changed during complete input capture")
    return AuthorContext(root, phase, candidate, authority, authorities, plan, registry,
                         inventory, environment, files, compiler, nm)


def recheck_context(context):
    if capture_author_context(context.root, context.phase) != context:
        raise ValueError("MEM04 source/tool/input identities changed during execution or publication")


def _literal_declarations(authority_source, source_source):
    result = {}
    for text, names in (
        (authority_source, ("DESIGN_PINS", "ORACLE_RULES", "COMPLETED_CLAIM")),
        (source_source, ("SOURCE_ROLES", "RECIPE_ADMISSION", "STATE_WORK_CONTRACT", "SOURCE_PROJECTION")),
    ):
        tree = ast.parse(text.decode())
        result.update((name, literal_value(single_assignment(tree, name).value)) for name in names)
    return result


def committed_claim(root):
    root = Path(root)
    current = _literal_declarations(read_regular(root / AUTHORITY_PATH)[0], read_regular(root / SOURCE_PATH)[0])
    if current != {**read_declarations(root), "COMPLETED_CLAIM": COMPLETED_CLAIM} or COMPLETED_CLAIM != (TRANSITION, "MEM04", ("B02-F005",)):
        raise ValueError("MEM04 completion claim changed its declared design or scope")
    paths = tuple(name.decode() for name in git(
        root, "ls-tree", "-r", "--name-only", "-z", "HEAD", "--", AUTHORITY_PATH, SOURCE_PATH,
    ).split(b"\0") if name)
    if not paths:
        return None
    if set(paths) != {AUTHORITY_PATH, SOURCE_PATH}:
        raise ValueError("MEM04 committed claim has an incomplete owner set")
    previous = _literal_declarations(
        git(root, "show", "HEAD:" + AUTHORITY_PATH), git(root, "show", "HEAD:" + SOURCE_PATH),
    )
    if previous != current:
        raise ValueError("MEM04 staged claim cannot borrow earlier committed authority")
    keys = SOURCE_PROJECTION["retirement_keys"]
    return TransitionClaim(TRANSITION, "MEM04", keys, tuple(row[0] for row in ORACLE_RULES), keys)


def journal_directory(root):
    directory = Path(root)
    for part in Path(JOURNAL_PATH).parts:
        directory /= part
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("MEM04 journal namespace is linked or non-directory")
    return directory


@contextmanager
def author_lock(root):
    directory = Path(root)
    for part in Path(JOURNAL_PATH).parts:
        directory /= part
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("MEM04 journal namespace is linked or non-directory")
        directory.mkdir(mode=0o700, exist_ok=True)
    descriptor = os.open(directory / ".writer.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ValueError("MEM04 writer lock is not an unlinked regular file")
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield directory
    finally:
        os.close(descriptor)


def read_selection(directory):
    from tests.host.diagnostic_history_authority import read_selection as decode

    return decode(directory)


def read_current(directory):
    return read_selection(directory)[0]


def begin_attempt(directory):
    from tools.stack_bound_manifest import publish_json

    directory = Path(directory)
    (directory / "current.json").unlink(missing_ok=True)
    attempt = uuid.uuid4().hex
    selected = directory / attempt
    selected.mkdir(mode=0o700)
    publish_json(directory / "current.json", {"version": 1, "attempt": attempt, "count": 0, "sha256": ""})
    return Journal(selected, attempt, (), "", read_selection(directory)[1])


def read_phase_record(directory, phase, attempt, previous):
    from tests.host.diagnostic_history_authority import decode_environment, decode_event, json_pairs, record_fields

    if phase not in PHASE_KINDS or re.fullmatch(r"[a-f0-9]{32}", attempt) is None:
        raise ValueError("MEM04 phase/attempt is not admitted")
    directory = Path(directory)
    data, identity = read_regular(directory / (phase + ".json"))
    value = json.loads(data, object_pairs_hook=json_pairs)
    record_fields(value, ("version", "attempt", "phase", "previous", "event", "environment", "logs"))
    if (
        type(value["version"]) is not int or value["version"] != 1
        or value["attempt"] != attempt or value["phase"] != phase or value["previous"] != previous
        or (phase == "red") != (previous == "")
        or previous and re.fullmatch(r"[a-f0-9]{64}", previous) is None
    ):
        raise ValueError("MEM04 phase record has a borrowed or broken predecessor")
    event = decode_event(value["event"])
    if event.kind is not PHASE_KINDS[phase]:
        raise ValueError("MEM04 phase record has the wrong event kind")
    record_fields(value["logs"], {"execution"} | (set(RECERTIFICATION_UNITS) if phase == "recertify" else set()))
    for name, declared in value["logs"].items():
        record_fields(declared, ("size", "sha256"))
        if (
            type(declared["size"]) is not int or declared["size"] < 0
            or type(declared["sha256"]) is not str or re.fullmatch(r"[a-f0-9]{64}", declared["sha256"]) is None
        ):
            raise ValueError("MEM04 execution log has an invalid byte identity")
        _, observed = read_regular(directory / (phase + "-" + name + ".log"))
        if any(observed[key] != declared[key] for key in declared):
            raise ValueError("MEM04 log bytes changed after execution")
    return {**value, "event": event, "environment": decode_environment(value["environment"])}, identity


def validate_journal(context, journal):
    from tests.host.diagnostic_history_authority import json_pairs, record_fields, record_strings
    from tests.host.evidence_authority_validation import validate_transition
    from tests.host.selected_address_space_evidence import validate_execution_log

    result = validate_transition(
        context.inventory, context.authorities, context.registry, context.plan,
        tuple(row["event"] for row in journal.records), context.environment,
    )
    if not result.ok:
        raise ValueError("MEM04 transition rejected: " + str(result.error))
    reference = None
    for phase, record in zip(PHASE_KINDS, journal.records):
        if record["event"].expected_property != ";".join(oracle.property for oracle in context.plan.replacement_oracles):
            raise ValueError("MEM04 event changed its complete declared property set")
        replay = json.loads(read_regular(journal.directory / (phase + "-execution.log"))[0], object_pairs_hook=json_pairs)
        outcomes = validate_execution_log(context, replay, phase=phase, attempt=journal.attempt, reference=reference)
        if outcomes != record["event"].oracle_results:
            raise ValueError("MEM04 event borrows results from another execution")
        if phase == "red":
            reference = replay["native"]
        if phase != "recertify":
            continue
        targets = {tuple(value["command"]): value for value in replay["targets"].values()}
        for unit, command in context.plan.unit_commands:
            value = json.loads(read_regular(journal.directory / ("recertify-" + unit + ".log"))[0], object_pairs_hook=json_pairs)
            record_fields(value, ("version", "unit", "command", "exit_code", "output"))
            if (
                type(value["version"]) is not int or value["version"] != 1
                or value["unit"] != unit or record_strings(value["command"]) != command
                or type(value["exit_code"]) is not int or value["exit_code"] != 0
                or type(value["output"]) is not str or not value["output"]
            ):
                raise ValueError("MEM04 unit log lacks its actual canonical successful execution")
            if command in targets and any(value[key] != targets[command][key] for key in ("exit_code", "output")):
                raise ValueError("MEM04 target/unit evidence is not the same fresh in-phase execution")
    return result


def read_verified_journal(context):
    from tools.stack_bound_manifest import owned_artifact

    directory = journal_directory(context.root)
    current, selector_sha256 = read_selection(directory)
    if current is None:
        raise ValueError("MEM04 requires a recorded --red attempt")
    selected = owned_artifact(directory, current["attempt"])
    if not selected.is_dir():
        raise ValueError("MEM04 selected attempt is absent")
    records, previous = [], ""
    for phase in tuple(PHASE_KINDS)[:current["count"]]:
        record, identity = read_phase_record(selected, phase, current["attempt"], previous)
        environment = record["environment"]
        if (
            environment.immutable_inputs != context.environment.immutable_inputs
            or environment.red_candidate_inputs != context.environment.red_candidate_inputs
            or phase != "red" and environment != context.environment
        ):
            raise ValueError("MEM04 source/oracle/tool/adapter inputs changed; start a new --red attempt")
        records.append(record)
        previous = identity["sha256"]
    if previous != current["sha256"] or read_selection(directory) != (current, selector_sha256):
        raise ValueError("MEM04 selector changed or does not bind the complete raw chain")
    journal = Journal(selected, current["attempt"], tuple(records), previous, selector_sha256)
    return journal, validate_journal(context, journal)


def read_journal(context):
    return read_verified_journal(context)[0]


def baseline_execution(journal):
    from tests.host.diagnostic_history_authority import json_pairs

    if not journal.records:
        raise ValueError("MEM04 lacks its actual original execution")
    return json.loads(read_regular(journal.directory / "red-execution.log")[0], object_pairs_hook=json_pairs)["native"]


@contextmanager
def provisional_retirement(context, journal, replay):
    from tests.host.selected_address_space_evidence import validate_execution_log

    global _provisional
    if _provisional is not None or len(journal.records) != 2 or context.phase != "recertify":
        raise ValueError("MEM04 provisional retirement requires one complete Green transaction")
    validate_journal(context, journal)
    outcomes = validate_execution_log(
        context, replay, phase="recertify", attempt=journal.attempt, reference=baseline_execution(journal),
    )
    if outcomes != journal.records[-1]["event"].oracle_results:
        raise ValueError("MEM04 recertification lacks complete actual Green replay")
    recheck_context(context)
    _provisional = os.getpid(), context
    try:
        yield
        recheck_context(context)
    finally:
        _provisional = None


def retired_identity_keys(root):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host.evidence_authority_validation import validate_transition

    root = Path(root).resolve()
    keys = SOURCE_PROJECTION["retirement_keys"]
    if any(not (root / path).exists() and not (root / path).is_symlink() for path in (AUTHORITY_PATH, SOURCE_PATH)):
        return frozenset()
    if _provisional is not None and _provisional[0] == os.getpid() and _provisional[1].root == root:
        from tools.validation_context import capture_candidate

        if capture_candidate(root) != _provisional[1].candidate:
            raise ValueError("MEM04 candidate changed inside its checked author transaction")
        return frozenset(keys)
    if committed_claim(root) is not None:
        _, authorities, _, registry = task_definition(root)
        result = validate_transition(current_inventory(root), authorities, registry, None, (), None)
        if not result.ok:
            raise ValueError("MEM04 committed retirement failed ordinary authority validation")
        return frozenset(key for key in keys if key not in result.active_identity_keys)
    selected, _ = read_selection(journal_directory(root))
    if selected is not None and selected["count"] == 3:
        context = capture_author_context(root, "recertify")
        _, result = read_verified_journal(context)
        recheck_context(context)
        return frozenset(key for key in keys if key not in result.active_identity_keys)
    return frozenset()


def identity_required(root, key):
    if key not in SOURCE_PROJECTION["retirement_keys"]:
        from tests.host.selected_ownership_authority import identity_required as previous

        return previous(root, key)
    return key not in retired_identity_keys(root)
