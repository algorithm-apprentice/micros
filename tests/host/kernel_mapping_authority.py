"""MEM02's separately merged design and three-key authority declaration."""

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
    TaskAuthority, TransitionClaim, TransitionRegistry, TransitionState, digest,
)
from tests.host.kernel_mapping_source import (
    AUTHORITY_PATH, SOURCE_PATH, SOURCE_PROJECTION, SOURCE_ROLES,
    read_declarations, validate_scaffold,
)

DESIGN_PINS = {
    "design_commit": "e7d936d6d26e372a8794bc5fd8feb6d976fcd529",
    "design_tree": "4f520e80e7f7a7490f67d438029c5d7ff77abcc0",
    "design_document": "docs/architecture/kernel-mapping-topology.md",
    "adr_document": "docs/adr/0101-minix-kernel-mapping-topology.md",
    "research_document": "docs/research/minix-kernel-mapping-topology.md",
    "runtime_reference": "3f7e33026f92c8c1765ce00c2b03b5c2d6855197",
    "runtime_tree": "08f31a8b1dfc639c8b826ea65d092327fea159a1",
    "minix_reference": "4db99f4012570a577414fe2a43697b2f239b699e",
}

ORACLE_RULES = (
    (
        "mem02-large-leaf-topology",
        "tests.host.kernel_mapping_evidence.KernelMappingOracles.test_large_leaf_topology",
        "largest-contained-1g-2m-4k-leaves;exact-domains-permissions-holes;table-coordinate-equality",
        "ADR-0101", "Preserve exact mapping authority", (0, 2),
    ),
    (
        "mem02-direct-table-work",
        "tests.host.kernel_mapping_evidence.KernelMappingOracles.test_direct_table_work",
        "construction-index<=32*(L+t+g+1);tree-index<=16*(t+1);validation-range<=4*gmax+4*t;selected-owner-safety",
        "ADR-0101", "Direct parent-indexed table identities", (1, 2),
    ),
    (
        "mem02-target-mmu-baseline",
        "tests.host.kernel_mapping_evidence.KernelMappingOracles.test_target_mmu_baseline",
        "actual-128m-256m-largest-topology;original-mmu-tty-guards;2m-1g-loads;misaligned-faults;resources-preserved",
        "ADR-0101", "Independent native and hardware evidence", (0, 1, 2),
    ),
)

TRANSITION = "MEM02-kernel-mapping"
COMPLETED_CLAIM = ("MEM02-kernel-mapping", "MEM02", ("B02-F003", "B02-F004"))
RECERTIFICATION_UNITS = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
EXTERNAL_INPUTS = (
    "@mem02/native-compiler", "@mem02/closed-tools", "@mem02/native-cache",
    "@mem02/qemu-firmware", "@mem02/accepted-design",
)
PHASE_KINDS = {
    "red": EventKind.RED_OBSERVED,
    "green": EventKind.GREEN_OBSERVED,
    "recertify": EventKind.AUTHOR_RECERTIFIED,
}
JOURNAL_PATH = "build/evidence-transitions/MEM02"
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


@dataclass(frozen=True)
class Journal:
    directory: Path
    attempt: str
    records: tuple
    sha256: str
    selector_sha256: str


def git(root, *arguments):
    result = subprocess.run(
        ("git", *arguments), cwd=root, stdin=subprocess.DEVNULL,
        capture_output=True, check=False, timeout=30,
    )
    if result.returncode:
        raise ValueError("MEM02 fixed Git authority is unavailable: " + result.stderr.decode())
    return result.stdout


def read_regular(path):
    from tools.stack_artifact_files import read_regular as read

    return read(path)


def design_blob(root, path):
    if Path(path).is_absolute() or "\\" in path or ".." in Path(path).parts:
        raise ValueError("MEM02 design input leaves its fixed namespace")
    for commit, tree in (
        (DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"]),
        (DESIGN_PINS["runtime_reference"], DESIGN_PINS["runtime_tree"]),
    ):
        if git(root, "rev-parse", commit + "^{tree}").decode().strip() != tree:
            raise ValueError("MEM02 fixed commit/tree identity changed")
    return git(root, "show", DESIGN_PINS["design_commit"] + ":" + path)


def base_membership(root):
    result = {}
    for row in filter(None, git(root, "ls-tree", "-r", "-z", DESIGN_PINS["design_commit"]).split(b"\0")):
        metadata, separator, raw_path = row.partition(b"\t")
        fields = metadata.split()
        path = raw_path.decode()
        if (
            not separator or len(fields) != 3 or fields[0] not in (b"100644", b"100755")
            or fields[1] != b"blob" or path in result or Path(path).is_absolute()
            or ".." in Path(path).parts or "\\" in path
        ):
            raise ValueError("MEM02 design has an unadmitted source member")
        result[path] = (int(fields[0], 8) & 0o777, fields[2].decode())
    if digest(tuple(sorted(result))) != SOURCE_ROLES["base_membership_sha256"]:
        raise ValueError("MEM02 complete design membership changed")
    return result


def declared_scope(root):
    if b"\n- Status: Accepted\n" not in design_blob(root, DESIGN_PINS["adr_document"]):
        raise ValueError("MEM02 requires its separately merged Accepted design")
    contract = design_blob(root, DESIGN_PINS["design_document"]).decode()
    blocks = re.findall(r"```text\n(.*?)\n```", contract, re.S)
    for prefix, expected in (
        ("tests/host/kernel_mapping_test.c", SOURCE_ROLES["immutable_additions"]),
        ("tests/host/kernel_layout_source.py:data-entry:", SOURCE_PROJECTION["retirement_keys"]),
        ("kernel/memory/address_space.c\n", SOURCE_ROLES["candidate_paths"][:3]),
        ("tests/host/kernel_layout_source.py\n", SOURCE_ROLES["candidate_paths"][-2:]),
    ):
        if [tuple(block.splitlines()) for block in blocks if block.startswith(prefix)] != [expected]:
            raise ValueError("MEM02 source scope differs from its fixed Accepted contract")
    return contract


def declared_commands(contract, executable):
    from tools.python_host_runtime import python_command

    phases = re.findall(
        r"^\$\{MICROS_PYTHON\} -E -S -B tools/check_kernel_mapping.py --(red|green|recertify)$",
        contract, re.MULTILINE,
    )
    if tuple(phases) != tuple(PHASE_KINDS):
        raise ValueError("MEM02 design lacks its three exact recording commands")
    commands = {phase: python_command(executable, "tools/check_kernel_mapping.py", "--" + phase) for phase in phases}
    rows = re.findall(r"^\| (B[0-9]{2}) \| (.*?) \|$", contract, re.MULTILINE)
    if tuple(unit for unit, _ in rows) != RECERTIFICATION_UNITS:
        raise ValueError("MEM02 design lacks its exact affected-unit inventory")
    units = []
    for unit, spelling in rows:
        if unit == "B14":
            if spelling != (
                "The actual `--recertify` invocation above, including retained "
                "authority/source/recipe/Python-owner checks and complete native/target oracle replay"
            ):
                raise ValueError("MEM02 B14 is not the actual author transaction")
            command = commands["recertify"]
        else:
            if not spelling.startswith("`") or not spelling.endswith("`"):
                raise ValueError("MEM02 unit command has unbound syntax")
            command = tuple(shlex.split(spelling[1:-1]))
            if len(command) != 4 or command[:3] != ("cmake", "--workflow", "--preset"):
                raise ValueError("MEM02 unit command has an undeclared form")
        units.append((unit, command))
    return commands, tuple(units)


def task_definition(root):
    from tests.host import bootstrap_allocation_authority as previous
    from tools.python_host_runtime import require_current_runtime, selected_executable

    require_current_runtime()
    phases, commands = declared_commands(declared_scope(root), selected_executable(dict(os.environ)))
    candidates = SOURCE_ROLES["candidate_paths"]
    members = tuple(sorted(set(base_membership(root)) | set(SOURCE_ROLES["immutable_additions"])))
    immutable = tuple(path for path in members if path not in candidates)
    authority = TaskAuthority(
        "MEM02", "B02", ("B02-F003", "B02-F004"),
        DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"], DESIGN_PINS["design_document"],
        RECERTIFICATION_UNITS, phases["red"], phases["green"], phases["recertify"], commands,
        immutable, tuple((unit, candidates) for unit in RECERTIFICATION_UNITS),
        DESIGN_PINS["runtime_reference"], DESIGN_PINS["runtime_tree"],
    )
    inputs = (
        DESIGN_PINS["design_document"], DESIGN_PINS["adr_document"], DESIGN_PINS["research_document"],
        *SOURCE_ROLES["immutable_additions"],
    )
    keys = SOURCE_PROJECTION["retirement_keys"]
    oracles = tuple(Oracle(
        identifier, test, property_name, tuple(keys[index] for index in positions),
        AuthorityClass.BASELINE_ORACLE,
        Provenance(AuthorityKind.ACCEPTED_ADAPTATION, path=DESIGN_PINS["adr_document"], adr=adr, section=section),
        inputs,
    ) for identifier, test, property_name, adr, section, positions in ORACLE_RULES)
    plan = Plan(
        TRANSITION, "MEM02", "B02", ("B02-F003", "B02-F004"),
        DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"], DESIGN_PINS["design_document"],
        keys, (), keys, oracles, RECERTIFICATION_UNITS, EXTERNAL_INPUTS,
        phases["red"], phases["green"], phases["recertify"], commands,
    )
    if previous.committed_claim(root) is None:
        raise ValueError("MEM02 requires the committed MEM01 authority chain")
    _, authorities, _, registry = previous.task_definition(root)
    claim = TransitionClaim(TRANSITION, "MEM02", keys, tuple(row.oracle_id for row in oracles), keys)
    return authority, {**authorities, "MEM02": authority}, plan, TransitionRegistry((*registry.claims, claim))


def current_membership(root):
    base = base_membership(root)
    expected = set(base) | set(SOURCE_ROLES["immutable_additions"])
    observed = tuple(path.decode() for path in git(root, "ls-files", "-z").split(b"\0") if path)
    if len(observed) != len(set(observed)) or set(observed) != expected:
        raise ValueError("MEM02 staged membership is incomplete or contains unowned inputs")
    roots = tuple(sorted({Path(path).parts[0] for path in expected}))
    if git(root, "ls-files", "--others", "-z", "--", *roots):
        raise ValueError("MEM02 source namespaces contain untracked or ignored inputs")
    return tuple(sorted(expected)), base


def external_inputs(root):
    import shutil
    from tests.host.diagnostic_history_evidence import closed_tool_inputs
    from tests.host.runtime_memory_evidence import native_compiler
    from tools.stack_bound_fatal_firmware import resolve_default_firmware

    compiler = native_compiler(root)
    qemu = shutil.which("qemu-system-riscv64")
    if qemu is None:
        raise ValueError("MEM02 requires its configured target execution tool")
    return compiler, {
        "@mem02/native-compiler": digest(compiler),
        "@mem02/closed-tools": digest(closed_tool_inputs(root)),
        "@mem02/native-cache": digest(read_regular(Path(root) / "build/host-debug/CMakeCache.txt")[1]),
        "@mem02/qemu-firmware": digest(resolve_default_firmware(qemu)),
        "@mem02/accepted-design": digest({
            "pins": DESIGN_PINS,
            "contract": hashlib.sha256(design_blob(root, DESIGN_PINS["design_document"])).hexdigest(),
            "adr": hashlib.sha256(design_blob(root, DESIGN_PINS["adr_document"])).hexdigest(),
        }),
    }


def capture_author_context(root, phase):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host.evidence_authority_validation import validate_transition
    from tools.stack_bound_manifest import owned_artifact
    from tools.validation_context import capture_candidate

    if phase not in PHASE_KINDS:
        raise ValueError("MEM02 source scope requires an exact author phase")
    root = Path(root).resolve()
    candidate = capture_candidate(root)
    if not candidate.clean:
        raise ValueError("MEM02 requires the complete clean staged candidate")
    members, base = current_membership(root)
    files = {path: read_regular(owned_artifact(root, path))[1] for path in members}
    validate_scaffold(root)
    from tests.host.kernel_mapping_source import baseline_source, project_mmu_source_bytes, validate_retained_source
    from tests.host.work_count_types import Stage

    for path in SOURCE_ROLES["candidate_paths"][:3]:
        text = read_regular(root / path)[0].decode()
        selected = Stage.RED if text == baseline_source(str(root), path) else Stage.GREEN
        validate_retained_source(root, path, text, selected)

    for path, (mode, oid) in base.items():
        if not path.startswith(("arch/", "kernel/", "include/", "lib/", "servers/", "user/")):
            continue
        if path in SOURCE_ROLES["candidate_paths"][:3]:
            continue
        data, identity = read_regular(root / path)
        data = project_mmu_source_bytes(path, data)
        observed = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if (identity["mode"], observed) != (mode, oid):
            raise ValueError("MEM02 changed production outside its three declared subjects: " + path)
    authority, authorities, plan, registry = task_definition(root)
    compiler, external = external_inputs(root)
    candidates = SOURCE_ROLES["candidate_paths"]
    immutable = tuple(sorted(
        [(path, digest(files[path])) for path in members if path not in candidates] + list(external.items())
    ))
    red = []
    for path in candidates:
        data = git(root, "show", DESIGN_PINS["runtime_reference"] + ":" + path)
        red.append((path, digest({
            "kind": "file", "mode": base[path][0], "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
        })))
    current = tuple(sorted((path, digest(files[path])) for path in candidates))
    environment = Environment(
        immutable, tuple(sorted(red)), current, tuple((unit, current) for unit in RECERTIFICATION_UNITS),
        candidate.source["head"], candidate.source["tree"],
    )
    inventory = current_inventory(root)
    result = validate_transition(inventory, authorities, registry, plan, (), environment)
    if not result.ok or result.state is not TransitionState.REPLACEMENT_REGISTERED:
        raise ValueError("MEM02 source/declaration closure failed: " + str(result.error))
    if capture_candidate(root) != candidate:
        raise ValueError("MEM02 source changed while its complete inputs were captured")
    return AuthorContext(root, phase, candidate, authority, authorities, plan, registry,
                         inventory, environment, files, compiler)


def recheck_context(context):
    if capture_author_context(context.root, context.phase) != context:
        raise ValueError("MEM02 source/tool/input identities changed during execution or publication")


def journal_directory(root):
    directory = Path(root)
    for part in Path(JOURNAL_PATH).parts:
        directory /= part
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("MEM02 journal namespace is linked or non-directory")
    return directory


@contextmanager
def author_lock(root):
    directory = Path(root)
    for part in Path(JOURNAL_PATH).parts:
        directory /= part
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("MEM02 journal namespace is linked or non-directory")
        directory.mkdir(mode=0o700, exist_ok=True)
    descriptor = os.open(directory / ".writer.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ValueError("MEM02 writer lock is not an unlinked regular file")
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
        raise ValueError("MEM02 phase/attempt is not admitted")
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
        raise ValueError("MEM02 phase record has a borrowed or broken predecessor")
    event = decode_event(value["event"])
    if event.kind is not PHASE_KINDS[phase]:
        raise ValueError("MEM02 phase record has the wrong event kind")
    record_fields(value["logs"], {"execution"} | (set(RECERTIFICATION_UNITS) if phase == "recertify" else set()))
    for name, declared in value["logs"].items():
        record_fields(declared, ("size", "sha256"))
        if (
            type(declared["size"]) is not int or declared["size"] < 0
            or type(declared["sha256"]) is not str or re.fullmatch(r"[a-f0-9]{64}", declared["sha256"]) is None
        ):
            raise ValueError("MEM02 execution log has an invalid byte identity")
        _, observed = read_regular(directory / (phase + "-" + name + ".log"))
        if any(observed[key] != declared[key] for key in declared):
            raise ValueError("MEM02 log bytes changed after execution")
    return {**value, "event": event, "environment": decode_environment(value["environment"])}, identity


def validate_journal(context, journal):
    from tests.host.diagnostic_history_authority import json_pairs, record_fields, record_strings
    from tests.host.evidence_authority_validation import validate_transition
    from tests.host.kernel_mapping_evidence import validate_execution_log

    result = validate_transition(
        context.inventory, context.authorities, context.registry, context.plan,
        tuple(row["event"] for row in journal.records), context.environment,
    )
    if not result.ok:
        raise ValueError("MEM02 transition rejected: " + str(result.error))
    for phase, record in zip(PHASE_KINDS, journal.records):
        if record["event"].expected_property != ";".join(oracle.property for oracle in context.plan.replacement_oracles):
            raise ValueError("MEM02 event changed its complete declared property set")
        replay = json.loads(read_regular(journal.directory / (phase + "-execution.log"))[0], object_pairs_hook=json_pairs)
        if validate_execution_log(context, replay, phase=phase, attempt=journal.attempt) != record["event"].oracle_results:
            raise ValueError("MEM02 event borrows results from another execution")
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
                raise ValueError("MEM02 unit log lacks its actual canonical successful execution")
    return result


def read_verified_journal(context):
    from tools.stack_bound_manifest import owned_artifact

    directory = journal_directory(context.root)
    current, selector_sha256 = read_selection(directory)
    if current is None:
        raise ValueError("MEM02 requires a recorded --red attempt")
    selected = owned_artifact(directory, current["attempt"])
    if not selected.is_dir():
        raise ValueError("MEM02 selected attempt is absent")
    records, previous = [], ""
    for phase in tuple(PHASE_KINDS)[:current["count"]]:
        record, identity = read_phase_record(selected, phase, current["attempt"], previous)
        environment = record["environment"]
        if (
            environment.immutable_inputs != context.environment.immutable_inputs
            or environment.red_candidate_inputs != context.environment.red_candidate_inputs
            or phase != "red" and environment != context.environment
        ):
            raise ValueError("MEM02 source/oracle/tool inputs changed; start a new --red attempt")
        records.append(record)
        previous = identity["sha256"]
    if previous != current["sha256"] or read_selection(directory) != (current, selector_sha256):
        raise ValueError("MEM02 selector changed or does not bind the complete raw chain")
    journal = Journal(selected, current["attempt"], tuple(records), previous, selector_sha256)
    return journal, validate_journal(context, journal)


def read_journal(context):
    return read_verified_journal(context)[0]


@contextmanager
def provisional_retirement(context, journal, replay):
    from tests.host.kernel_mapping_evidence import validate_execution_log

    global _provisional
    if _provisional is not None or len(journal.records) != 2 or context.phase != "recertify":
        raise ValueError("MEM02 provisional retirement requires one complete Green transaction")
    validate_journal(context, journal)
    if validate_execution_log(context, replay, phase="recertify", attempt=journal.attempt) != journal.records[-1]["event"].oracle_results:
        raise ValueError("MEM02 recertification lacks complete actual Green replay")
    recheck_context(context)
    _provisional = os.getpid(), context
    try:
        yield
        recheck_context(context)
    finally:
        _provisional = None


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
    expected = {**read_declarations(root), "COMPLETED_CLAIM": COMPLETED_CLAIM}
    if current != expected or COMPLETED_CLAIM != (TRANSITION, "MEM02", ("B02-F003", "B02-F004")):
        raise ValueError("MEM02 completion claim changed its declared design or scope")
    paths = tuple(name.decode() for name in git(
        root, "ls-tree", "-r", "--name-only", "-z", "HEAD", "--", AUTHORITY_PATH, SOURCE_PATH,
    ).split(b"\0") if name)
    if not paths:
        return None
    if set(paths) != {AUTHORITY_PATH, SOURCE_PATH}:
        raise ValueError("MEM02 committed claim has an incomplete owner set")
    previous = _literal_declarations(
        git(root, "show", "HEAD:" + AUTHORITY_PATH), git(root, "show", "HEAD:" + SOURCE_PATH),
    )
    if previous != current:
        raise ValueError("MEM02 staged claim cannot borrow earlier committed authority")
    keys = SOURCE_PROJECTION["retirement_keys"]
    return TransitionClaim(TRANSITION, "MEM02", keys, tuple(row[0] for row in ORACLE_RULES), keys)


def identity_required(root, key):
    from tests.host import bootstrap_allocation_authority as previous
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host.evidence_authority_validation import validate_transition

    if key not in SOURCE_PROJECTION["retirement_keys"]:
        return previous.identity_required(root, key)
    root = Path(root).resolve()
    if any(not (root / path).exists() and not (root / path).is_symlink() for path in (AUTHORITY_PATH, SOURCE_PATH)):
        return True
    if _provisional is not None and _provisional[0] == os.getpid() and _provisional[1].root == root:
        from tools.validation_context import capture_candidate

        if capture_candidate(root) != _provisional[1].candidate:
            raise ValueError("MEM02 candidate changed inside its checked author transaction")
        return False
    if committed_claim(root) is not None:
        _, authorities, _, registry = task_definition(root)
        result = validate_transition(current_inventory(root), authorities, registry, None, (), None)
        if not result.ok:
            raise ValueError("MEM02 committed retirement failed ordinary authority validation")
        return key in result.active_identity_keys
    selected = read_current(journal_directory(root))
    if selected is not None and selected["count"] == 3:
        context = capture_author_context(root, "recertify")
        _, result = read_verified_journal(context)
        recheck_context(context)
        return key in result.active_identity_keys
    return True
