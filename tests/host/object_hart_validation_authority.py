"""OBJ01's fixed design scope and actual execution authority."""

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
from tests.host.object_hart_validation_source import (
    AUTHORITY_PATH, FAMILY, RECIPE_ADMISSION, SOURCE_PATH, SOURCE_PROJECTION, SOURCE_ROLES,
    candidate_paths, read_declarations,
)
from tools.stack_artifact_files import read_regular


TRANSITION = "OBJ01-selected-object-hart-validation"
COMPLETED_CLAIM = ("OBJ01-selected-object-hart-validation", "OBJ01", ("B03-F001", "B03-F003", "B03-F004"))
DESIGN_PINS = {
    "design_commit": "6d43a88f98c978ecdb4c1ec2ba31db3eca4d8c91",
    "design_tree": "9b547572ba25ff479c9f8f5b17f62a99d44c33e7",
    "original_design_commit": "a803c2d9e2692b148b7c8d74d02e8304406ce89f",
    "original_design_tree": "99736be3086b80f6a08a5223b4ef74bd1fc9e213",
    "implementation_base_commit": "6d43a88f98c978ecdb4c1ec2ba31db3eca4d8c91",
    "implementation_base_tree": "9b547572ba25ff479c9f8f5b17f62a99d44c33e7",
    "runtime_reference": "9b880468192e61177ffe57ec69079fe398160a42",
    "runtime_tree": "80b93b111b5a1387d812da87c539e56da042777b",
    "minix_reference": "4db99f4012570a577414fe2a43697b2f239b699e",
    "minix_tree": "80db89de58d5ecaf31efe48b4d1d98c7e285e81f",
    "design_document": "docs/architecture/selected-object-and-hart-validation.md",
    "adr_document": "docs/adr/0120-obj01-complete-source-authority.md",
    "runtime_adr_document": "docs/adr/0119-selected-object-and-hart-validation.md",
    "research_document": "docs/research/minix-selected-object-and-hart-validation.md",
}
ORACLE_RULES = (
    (
        "obj01-local-validation-work",
        "tests.host.object_hart_validation_evidence.ObjectHartValidationOracles.test_local_validation_work",
        "zero-owned-entry-audits;selected-prefix-and-linear-batch-work;failure-atomicity",
        "ADR-0119", "Work and storage evidence",
        (3, 4, 5, 6, 7, 8, 9, 10, 11, 15, 16, 17, 18, 19, 20, 21, 22, 23,
         28, 29, 30, 31, 32, 33, 34, 35, 36, 40, 41, 42, 43, 44, 45, 46, 47, 48,
         55, 56, 57, 58, 59, 60, 61, 62, 63, 71, 72, 73, 74, 75, 76, 77, 78, 79,
         80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93),
    ),
    (
        "obj01-stack-geometry-work",
        "tests.host.object_hart_validation_evidence.ObjectHartValidationOracles.test_stack_geometry_work",
        "linear-regular-and-bounded-exceptional-stack-work;complete-overlap-diagnosis",
        "ADR-0119", "Work and storage evidence",
        (2, 14, 27, 39, 49, 50, 54, 64, 66, 68, 70),
    ),
    (
        "obj01-direct-current-ownership",
        "tests.host.object_hart_validation_evidence.ObjectHartValidationOracles.test_direct_current_ownership",
        "one-two-three-selected-probes;exact-generations;fixed-context-copy;unsupported-current-diagnostic",
        "ADR-0120", "OBJ01-C01 complete source-authority",
        (0, 1, 12, 13, 24, 25, 26, 37, 38, 51, 52, 53, 65, 67, 69, 94),
    ),
)
RECERTIFICATION_UNITS = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
EXTERNAL_INPUTS = (
    "@obj01/native-compiler", "@obj01/native-nm", "@obj01/closed-tools", "@obj01/native-cache",
    "@obj01/qemu-firmware", "@obj01/accepted-design", "@obj01/frozen-candidates",
)
PHASE_KINDS = {
    "red": EventKind.RED_OBSERVED,
    "green": EventKind.GREEN_OBSERVED,
    "recertify": EventKind.AUTHOR_RECERTIFIED,
}
JOURNAL_PATH = "build/evidence-transitions/OBJ01"
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
        ("git", *arguments), cwd=root, stdin=subprocess.DEVNULL, capture_output=True, timeout=30,
    )
    if completed.returncode:
        raise ValueError("OBJ01 fixed Git authority is unavailable: " + completed.stderr.decode())
    return completed.stdout


def design_blob(root, path):
    documents = tuple(DESIGN_PINS[name] for name in ("design_document", "adr_document", "runtime_adr_document", "research_document"))
    if path not in documents:
        raise ValueError("OBJ01 design input leaves its fixed namespace")
    for commit, tree in (
        (DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"]),
        (DESIGN_PINS["original_design_commit"], DESIGN_PINS["original_design_tree"]),
        (DESIGN_PINS["runtime_reference"], DESIGN_PINS["runtime_tree"]),
        (DESIGN_PINS["implementation_base_commit"], DESIGN_PINS["implementation_base_tree"]),
    ):
        if git(root, "rev-parse", commit + "^{tree}").decode().strip() != tree:
            raise ValueError("OBJ01 fixed commit/tree identity changed")
    raw = git(root, "show", DESIGN_PINS["design_commit"] + ":" + path)
    if path == DESIGN_PINS["runtime_adr_document"] and raw != git(
        root, "show", DESIGN_PINS["original_design_commit"] + ":" + path,
    ):
        raise ValueError("OBJ01 corrective design rewrote its original Accepted decision")
    return raw


def base_membership(root):
    result = {}
    for entry in filter(None, git(root, "ls-tree", "-r", "-z", DESIGN_PINS["implementation_base_commit"]).split(b"\0")):
        metadata, separator, raw_path = entry.partition(b"\t")
        fields, path = metadata.split(), raw_path.decode()
        if (
            not separator or len(fields) != 3 or fields[0] not in (b"100644", b"100755")
            or fields[1] != b"blob" or path in result or Path(path).is_absolute()
            or ".." in Path(path).parts or "\\" in path
        ):
            raise ValueError("OBJ01 implementation base has a nonordinary source member")
        result[path] = (fields[0].decode(), fields[2].decode())
    if len(result) != 1538 or digest(result) != SOURCE_ROLES["base_membership_sha256"]:
        raise ValueError("OBJ01 independently fixed base membership changed")
    return {path: (int(mode, 8) & 0o777, oid) for path, (mode, oid) in result.items()}


def predecessor_records(root, inventory):
    from tests.host.evidence_authority_validation import validate_inventory

    checked = validate_inventory(inventory)
    if not checked.ok:
        raise ValueError("OBJ01 complete current inventory is invalid: " + str(checked.error))
    tree = ast.parse(git(
        root, "show", DESIGN_PINS["runtime_reference"] + ":tests/host/evidence_authority_inventory.py",
    ).decode())
    expected = literal_value(single_assignment(tree, "DECLARED_FAMILY_FACT_DIGESTS").value)
    if type(expected) is not dict or len(expected) != 25 or FAMILY in expected:
        raise ValueError("OBJ01 fixed predecessor families changed")
    records = tuple(fact for fact in inventory.facts if fact.family in expected)
    if (
        len(records) != 5851 or fact_set_digest(records) != SOURCE_PROJECTION["predecessor_facts_sha256"]
        or any(fact_set_digest(fact for fact in records if fact.family == name) != fingerprint
               for name, fingerprint in expected.items())
    ):
        raise ValueError("OBJ01 changed a complete predecessor record or fingerprint")
    return records


def _oracle_partition(facts):
    groups = {name: [] for name, *_ in ORACLE_RULES}
    for index, fact in enumerate(facts):
        scope = fact.scope
        if (
            any(name in scope for name in (
                "kernel_object_context.c", "kernel_object_hart.c", "micros_hart_resolve_context",
                "micros_thread_capture_execution_context",
            )) or scope.endswith("thread_is_current")
            or fact.key == "tests/host/test_kernel_object_source_layout.py:data-entry:STABLE_HASHES/kernel/scheduler_core_validate.c"
        ):
            group = "obj01-direct-current-ownership"
        elif (
            "kernel_object_validation.c" in scope or "micros_kernel_objects_validate_base" in scope
            or scope == "BASE_CORE_DIGEST" or scope.endswith("kernel/kernel_objects.c")
        ):
            group = "obj01-stack-geometry-work"
        else:
            group = "obj01-local-validation-work"
        groups[group].append(index)
    return {name: tuple(indices) for name, indices in groups.items()}


def declared_scope(root):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host import selected_release_handoff_authority as previous

    if any(b"\n- Status: Accepted\n" not in design_blob(root, DESIGN_PINS[name])
           for name in ("adr_document", "runtime_adr_document")):
        raise ValueError("OBJ01 requires its separately merged Accepted design")
    contract = design_blob(root, DESIGN_PINS["design_document"]).decode()
    if SOURCE_PROJECTION["affected_facts_sha256"] not in contract:
        raise ValueError("OBJ01 retirement lacks its independently Accepted record digest")
    prior = predecessor_records(root, current_inventory(root))
    keys = SOURCE_PROJECTION["retirement_keys"]
    facts = tuple(sorted((fact for fact in prior if fact.key in keys), key=lambda fact: fact.key))
    if (
        len(keys) != 95 or keys != tuple(sorted(set(keys))) or len(facts) != 95
        or fact_set_digest(facts) != SOURCE_PROJECTION["affected_facts_sha256"]
    ):
        raise ValueError("OBJ01 selected identities differ from the complete fixed records")
    _, _, _, registry = previous.task_definition(root)
    if set(keys) & {key for claim in registry.claims for key in claim.retired_identity_keys}:
        raise ValueError("OBJ01 cannot retire a previously retired identity")
    partition = _oracle_partition(facts)
    if any(row[5] != partition[row[0]] for row in ORACLE_RULES):
        raise ValueError("OBJ01 oracle ownership changed its fixed subject partition")
    closure = {path for fact in facts for path in (fact.owner_path, *fact.payload_paths)}
    if closure != set(candidate_paths()):
        raise ValueError("OBJ01 candidate domain differs from the exact selected fact closure")
    blocks = re.findall(r"^```text\n(.*?)\n```$", contract, re.M | re.S)
    subjects = tuple(block for block in blocks if block.startswith("kernel/objects/kernel_object_hart.c\n"))
    adapters = tuple(block for block in blocks if block.startswith("tests/host/kernel_layout_source.py\n"))
    from tests.host.object_hart_validation_source import MUTABLE_FUNCTIONS

    if (
        len(subjects) != 1
        or set(subjects[0].splitlines()) != set((*MUTABLE_FUNCTIONS, "kernel/scheduler/scheduler_core_private.h"))
        or len(adapters) != 1
        or set(adapters[0].splitlines()) != set((*SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["unchanged_candidates"]))
    ):
        raise ValueError("OBJ01 source roles differ from the Accepted closed inventories")
    return contract


def declared_commands(contract, executable):
    from tools.python_host_runtime import python_command

    phases = re.findall(
        r'^"\$\{MICROS_PYTHON\}" -E -S -B tools/check_object_hart_validation.py --(red|green|recertify)$',
        contract, re.M,
    )
    if tuple(phases) != tuple(PHASE_KINDS):
        raise ValueError("OBJ01 design lacks its three exact recording commands")
    commands = {phase: python_command(executable, RECIPE_ADMISSION["driver"], "--" + phase) for phase in phases}
    rows = re.findall(r"^\| (B[0-9]{2}) \| (.*?) \|$", contract, re.M)
    if tuple(unit for unit, _ in rows) != RECERTIFICATION_UNITS:
        raise ValueError("OBJ01 design lacks its complete affected-unit inventory")
    units = []
    for unit, spelling in rows:
        if unit == "B14":
            if spelling != (
                "The actual task `--recertify` transaction, including all six native products, original "
                "object/scheduler models, legacy memory work, source/observation/recipe/authority checks "
                "and retained stack qualification"
            ):
                raise ValueError("OBJ01 B14 is not the complete actual author transaction")
            command = commands["recertify"]
        else:
            if not spelling.startswith("`") or not spelling.endswith("`"):
                raise ValueError("OBJ01 affected-unit command has unbound syntax")
            command = tuple(shlex.split(spelling[1:-1]))
            if len(command) != 4 or command[:3] != ("cmake", "--workflow", "--preset"):
                raise ValueError("OBJ01 affected-unit command has an undeclared form")
        units.append((unit, command))
    return commands, tuple(units)


def task_definition(root):
    from tests.host import selected_release_handoff_authority as previous
    from tools.python_host_runtime import require_current_runtime, selected_executable

    require_current_runtime()
    contract = declared_scope(root)
    phases, commands = declared_commands(contract, selected_executable(dict(os.environ)))
    candidates = candidate_paths()
    members = tuple(sorted(set(base_membership(root)) | set(SOURCE_ROLES["immutable_additions"])))
    immutable = tuple(path for path in members if path not in candidates)
    findings = ("B03-F001", "B03-F003", "B03-F004")
    authority = TaskAuthority(
        "OBJ01", "B03", findings, DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"],
        DESIGN_PINS["design_document"], RECERTIFICATION_UNITS,
        phases["red"], phases["green"], phases["recertify"], commands,
        immutable, tuple((unit, candidates) for unit in RECERTIFICATION_UNITS),
        DESIGN_PINS["runtime_reference"], DESIGN_PINS["runtime_tree"],
    )
    keys = SOURCE_PROJECTION["retirement_keys"]
    inputs = (
        DESIGN_PINS["design_document"], DESIGN_PINS["adr_document"],
        DESIGN_PINS["runtime_adr_document"], DESIGN_PINS["research_document"],
        *SOURCE_ROLES["immutable_additions"], *SOURCE_ROLES["immutable_adapters"],
    )
    if any("\n## " + row[4] + "\n" not in contract for row in ORACLE_RULES):
        raise ValueError("OBJ01 oracle provenance lacks its Accepted section")
    oracles = tuple(Oracle(
        identifier, test, property_name, tuple(keys[index] for index in positions),
        AuthorityClass.BASELINE_ORACLE,
        Provenance(AuthorityKind.ACCEPTED_ADAPTATION, path=DESIGN_PINS["design_document"], adr=adr, section=section),
        inputs,
    ) for identifier, test, property_name, adr, section, positions in ORACLE_RULES)
    plan = Plan(
        TRANSITION, "OBJ01", "B03", findings,
        DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"], DESIGN_PINS["design_document"],
        keys, (), keys, oracles, RECERTIFICATION_UNITS, EXTERNAL_INPUTS,
        phases["red"], phases["green"], phases["recertify"], commands,
    )
    if previous.committed_claim(root) is None:
        raise ValueError("OBJ01 requires the complete committed MEM05 authority chain")
    _, authorities, _, registry = previous.task_definition(root)
    claim = TransitionClaim(TRANSITION, "OBJ01", keys, tuple(row.oracle_id for row in oracles), keys)
    return authority, {**authorities, "OBJ01": authority}, plan, TransitionRegistry((*registry.claims, claim))


def _literal_declarations(authority_source, source_source):
    result = {}
    for text, names in (
        (authority_source, ("DESIGN_PINS", "ORACLE_RULES", "COMPLETED_CLAIM")),
        (source_source, tuple(name for name in read_declarations_names() if name not in ("DESIGN_PINS", "ORACLE_RULES"))),
    ):
        tree = ast.parse(text.decode())
        result.update((name, literal_value(single_assignment(tree, name).value)) for name in names)
    return result


def read_declarations_names():
    return (
        "DESIGN_PINS", "ORACLE_RULES", "SOURCE_ROLES", "SOURCE_PROJECTION", "STATE_WORK_CONTRACT",
        "RECIPE_ADMISSION", "MUTABLE_FUNCTIONS", "REMOVED_FUNCTIONS", "CURRENT_HELPERS", "CURRENT_COMPARISONS",
        "CURRENT_SIGNATURES", "MUTABLE_SHAPES", "CALL_CHANGES", "SKELETON_ADDITIONS",
        "AUDIT_SOURCES", "MIGRATED_AUDITS",
    )


def committed_claim(root):
    root = Path(root)
    current = _literal_declarations(read_regular(root / AUTHORITY_PATH)[0], read_regular(root / SOURCE_PATH)[0])
    if (
        current != {**read_declarations(root), "COMPLETED_CLAIM": COMPLETED_CLAIM}
        or COMPLETED_CLAIM != (TRANSITION, "OBJ01", ("B03-F001", "B03-F003", "B03-F004"))
    ):
        raise ValueError("OBJ01 completion declaration changed its design or scope")
    paths = tuple(name.decode() for name in git(
        root, "ls-tree", "-r", "--name-only", "-z", "HEAD", "--", AUTHORITY_PATH, SOURCE_PATH,
    ).split(b"\0") if name)
    if not paths:
        return None
    if set(paths) != {AUTHORITY_PATH, SOURCE_PATH}:
        raise ValueError("OBJ01 committed claim has an incomplete owner set")
    previous = _literal_declarations(
        git(root, "show", "HEAD:" + AUTHORITY_PATH), git(root, "show", "HEAD:" + SOURCE_PATH),
    )
    if previous != current:
        raise ValueError("OBJ01 staged declarations cannot borrow earlier committed authority")
    keys = SOURCE_PROJECTION["retirement_keys"]
    return TransitionClaim(TRANSITION, "OBJ01", keys, tuple(row[0] for row in ORACLE_RULES), keys)


def current_membership(root):
    base = base_membership(root)
    expected = set(base) | set(SOURCE_ROLES["immutable_additions"])
    observed = tuple(path.decode() for path in git(root, "ls-files", "-z").split(b"\0") if path)
    if len(observed) != len(set(observed)) or set(observed) != expected:
        raise ValueError("OBJ01 staged membership is incomplete or contains unowned inputs")
    roots = tuple(sorted({Path(path).parts[0] for path in expected}))
    if git(root, "ls-files", "--others", "-z", "--", *roots):
        raise ValueError("OBJ01 source namespaces contain untracked or ignored inputs")
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
        raise ValueError("OBJ01 requires its configured target tool")
    return compiler, nm, {
        "@obj01/native-compiler": digest(compiler), "@obj01/native-nm": digest(nm),
        "@obj01/closed-tools": digest(closed_tool_inputs(root)),
        "@obj01/native-cache": digest(read_regular(Path(root) / "build/host-debug/CMakeCache.txt")[1]),
        "@obj01/qemu-firmware": digest(resolve_default_firmware(qemu)),
        "@obj01/accepted-design": digest({
            "pins": DESIGN_PINS,
            **{name: hashlib.sha256(design_blob(root, DESIGN_PINS[name])).hexdigest()
               for name in ("design_document", "adr_document", "runtime_adr_document", "research_document")},
        }),
        "@obj01/frozen-candidates": digest({
            path: read_regular(Path(root) / path)[1]
            for path in (*SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["unchanged_candidates"])
        }),
    }


def capture_author_context(root, phase):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host.evidence_authority_validation import validate_transition
    from tests.host.object_hart_validation_source import fixed_source, validate_retained_source, validate_scaffold
    from tools.stack_bound_manifest import owned_artifact
    from tools.validation_context import capture_candidate

    if phase not in PHASE_KINDS:
        raise ValueError("OBJ01 source scope requires an exact author phase")
    root = Path(root).resolve()
    candidate = capture_candidate(root)
    if not candidate.clean:
        raise ValueError("OBJ01 requires the complete clean staged candidate")
    members, base = current_membership(root)
    files = {path: read_regular(owned_artifact(root, path))[1] for path in members}
    validate_scaffold(root)
    for path in SOURCE_ROLES["production_paths"]:
        raw = read_regular(root / path)[0]
        validate_retained_source(root, path, raw.decode(), "baseline" if phase == "red" else "current")
    allowed = set((*SOURCE_ROLES["production_paths"], *SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["immutable_adapters"]))
    for path, (mode, oid) in base.items():
        if path in allowed or path.endswith(".md"):
            continue
        raw, identity = read_regular(root / path)
        observed = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
        if (identity["mode"], observed) != (mode, oid):
            raise ValueError("OBJ01 changed code or literals outside its exact source roles: " + path)
    authority, authorities, plan, registry = task_definition(root)
    compiler, nm, external = external_inputs(root)
    candidates = candidate_paths()
    immutable = tuple(sorted(
        [(path, digest(files[path])) for path in members if path not in candidates] + list(external.items())
    ))
    red = []
    for path in candidates:
        if path in SOURCE_ROLES["production_paths"]:
            raw = fixed_source(str(root), path).encode()
            identity = {"kind": "file", "mode": base[path][0], "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
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
        raise ValueError("OBJ01 source/declaration closure failed: " + str(result.error))
    if capture_candidate(root) != candidate:
        raise ValueError("OBJ01 source changed during complete input capture")
    return AuthorContext(root, phase, candidate, authority, authorities, plan, registry,
                         inventory, environment, files, compiler, nm)


def recheck_context(context):
    if capture_author_context(context.root, context.phase) != context:
        raise ValueError("OBJ01 source, tools or inputs changed during execution")


def journal_directory(root):
    directory = Path(root)
    for part in Path(JOURNAL_PATH).parts:
        directory /= part
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("OBJ01 journal namespace is linked or non-directory")
    return directory


@contextmanager
def author_lock(root):
    directory = Path(root)
    for part in Path(JOURNAL_PATH).parts:
        directory /= part
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("OBJ01 journal namespace is linked or non-directory")
        directory.mkdir(mode=0o700, exist_ok=True)
    descriptor = os.open(directory / ".writer.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        information = os.fstat(descriptor)
        if not stat.S_ISREG(information.st_mode) or information.st_nlink != 1:
            raise ValueError("OBJ01 writer lock is not an ordinary unlinked file")
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
        raise ValueError("OBJ01 phase or attempt is not admitted")
    directory = Path(directory)
    raw, identity = read_regular(directory / (phase + ".json"))
    value = json.loads(raw, object_pairs_hook=json_pairs)
    record_fields(value, ("version", "attempt", "phase", "previous", "event", "environment", "logs"))
    if (
        type(value["version"]) is not int or value["version"] != 1
        or value["attempt"] != attempt or value["phase"] != phase or value["previous"] != previous
        or (phase == "red") != (previous == "")
        or previous and re.fullmatch(r"[a-f0-9]{64}", previous) is None
    ):
        raise ValueError("OBJ01 phase record has a borrowed or broken predecessor")
    event = decode_event(value["event"])
    if event.kind is not PHASE_KINDS[phase]:
        raise ValueError("OBJ01 phase record has the wrong event kind")
    record_fields(value["logs"], {"execution"} | (set(RECERTIFICATION_UNITS) if phase == "recertify" else set()))
    for name, declared in value["logs"].items():
        record_fields(declared, ("size", "sha256"))
        if (
            type(declared["size"]) is not int or declared["size"] < 0
            or type(declared["sha256"]) is not str or re.fullmatch(r"[a-f0-9]{64}", declared["sha256"]) is None
        ):
            raise ValueError("OBJ01 execution log has an invalid byte identity")
        _, observed = read_regular(directory / (phase + "-" + name + ".log"))
        if any(observed[key] != declared[key] for key in declared):
            raise ValueError("OBJ01 log bytes changed after execution")
    return {**value, "event": event, "environment": decode_environment(value["environment"])}, identity


def validate_journal(context, journal):
    from tests.host.diagnostic_history_authority import json_pairs, record_fields, record_strings
    from tests.host.evidence_authority_validation import validate_transition
    from tests.host.object_hart_validation_evidence import validate_execution_log

    result = validate_transition(
        context.inventory, context.authorities, context.registry, context.plan,
        tuple(row["event"] for row in journal.records), context.environment,
    )
    if not result.ok:
        raise ValueError("OBJ01 transition rejected: " + str(result.error))
    reference = None
    for phase, record in zip(PHASE_KINDS, journal.records):
        if record["event"].expected_property != ";".join(oracle.property for oracle in context.plan.replacement_oracles):
            raise ValueError("OBJ01 event changed its complete declared property set")
        replay = json.loads(read_regular(journal.directory / (phase + "-execution.log"))[0], object_pairs_hook=json_pairs)
        outcomes = validate_execution_log(context, replay, phase=phase, attempt=journal.attempt, reference=reference)
        if outcomes != record["event"].oracle_results:
            raise ValueError("OBJ01 event borrows results from another execution")
        if phase == "red":
            reference = replay["native"]
        if phase != "recertify":
            continue
        for unit, command in context.plan.unit_commands:
            value = json.loads(read_regular(journal.directory / ("recertify-" + unit + ".log"))[0], object_pairs_hook=json_pairs)
            record_fields(value, ("version", "unit", "command", "exit_code", "output"))
            if (
                type(value["version"]) is not int or value["version"] != 1
                or value["unit"] != unit or record_strings(value["command"]) != command
                or type(value["exit_code"]) is not int or value["exit_code"] != 0
                or type(value["output"]) is not str or not value["output"]
            ):
                raise ValueError("OBJ01 unit log lacks its actual canonical successful execution")
    return result


def read_verified_journal(context):
    from tools.stack_bound_manifest import owned_artifact

    directory = journal_directory(context.root)
    current, selector_sha256 = read_selection(directory)
    if current is None:
        raise ValueError("OBJ01 requires a recorded --red attempt")
    selected = owned_artifact(directory, current["attempt"])
    if not selected.is_dir():
        raise ValueError("OBJ01 selected attempt is absent")
    records, previous = [], ""
    for phase in tuple(PHASE_KINDS)[:current["count"]]:
        record, identity = read_phase_record(selected, phase, current["attempt"], previous)
        environment = record["environment"]
        if (
            environment.immutable_inputs != context.environment.immutable_inputs
            or environment.red_candidate_inputs != context.environment.red_candidate_inputs
            or phase != "red" and environment != context.environment
        ):
            raise ValueError("OBJ01 oracle, source, tool or adapter inputs changed; start a new --red attempt")
        records.append(record)
        previous = identity["sha256"]
    if previous != current["sha256"] or read_selection(directory) != (current, selector_sha256):
        raise ValueError("OBJ01 selector changed or does not bind the complete raw chain")
    journal = Journal(selected, current["attempt"], tuple(records), previous, selector_sha256)
    return journal, validate_journal(context, journal)


def read_journal(context):
    return read_verified_journal(context)[0]


def baseline_execution(journal):
    from tests.host.diagnostic_history_authority import json_pairs

    if not journal.records:
        raise ValueError("OBJ01 lacks its actual fixed-source execution")
    return json.loads(read_regular(journal.directory / "red-execution.log")[0], object_pairs_hook=json_pairs)["native"]


@contextmanager
def provisional_retirement(context, journal, replay):
    from tests.host.object_hart_validation_evidence import validate_execution_log

    global _provisional
    if _provisional is not None or len(journal.records) != 2 or context.phase != "recertify":
        raise ValueError("OBJ01 provisional retirement requires one complete Green transaction")
    validate_journal(context, journal)
    outcomes = validate_execution_log(
        context, replay, phase="recertify", attempt=journal.attempt, reference=baseline_execution(journal),
    )
    if outcomes != journal.records[-1]["event"].oracle_results:
        raise ValueError("OBJ01 recertification lacks complete actual Green replay")
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
            raise ValueError("OBJ01 candidate changed inside its checked author transaction")
        return frozenset(keys)
    if committed_claim(root) is not None:
        _, authorities, _, registry = task_definition(root)
        result = validate_transition(current_inventory(root), authorities, registry, None, (), None)
        if not result.ok:
            raise ValueError("OBJ01 committed retirement failed ordinary authority validation")
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
        from tests.host.selected_release_handoff_authority import identity_required as previous

        return previous(root, key)
    return key not in retired_identity_keys(root)
