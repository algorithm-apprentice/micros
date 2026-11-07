"""MEM03 exact source-authority plan and actual execution journal."""

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
from tests.host.selected_ownership_source import (
    AUTHORITY_PATH, RECIPE_ADMISSION, SOURCE_PATH, SOURCE_PROJECTION,
    SOURCE_ROLES, STATE_WORK_CONTRACT, read_declarations,
)
from tools.stack_artifact_files import read_regular


TRANSITION = "MEM03-selected-ownership"
COMPLETED_CLAIM = ("MEM03-selected-ownership", "MEM03", ("B02-F002",))


DESIGN_PINS = {'adr_document': 'docs/adr/0103-selected-frame-ownership-validation.md',
 'correction_adr_document': 'docs/adr/0104-precise-complete-validator-token-authority.md',
 'correction_research_document': 'docs/research/complete-validator-token-scope-gap.md',
 'design_commit': '80941ff270b5d75d7edfce65ca96f28e6501f20d',
 'design_document': 'docs/architecture/selected-frame-ownership.md',
 'design_tree': '5c43367e822884feb8e47031b4a9772662dd2d19',
 'implementation_base_commit': 'dedf9a8d9d72f153519ea07150e50594a6618c6b',
 'implementation_base_tree': '3afdc8e5281fe5199cb2fd45cc4af3a12eaba1f0',
 'minix_reference': '4db99f4012570a577414fe2a43697b2f239b699e',
 'original_design_commit': '97459f6566ce8312760cdd6c5a37507e2b15d7bd',
 'original_design_tree': '6c4f243371ecb50b688f8d66adc43cb472f22585',
 'research_document': 'docs/research/minix-selected-frame-ownership.md',
 'runtime_reference': '8927f9956eb270962517b19715a12bc93a5fa466',
 'runtime_tree': '9f7802dfdfc84034b626068b5c160b90a645dad7',
 'validation_adr_document': 'docs/adr/0105-bounded-validation-feedback.md',
 'validation_design_commit': '7fe20afa839bc396b243537bb5a75b4c5f5d59ed',
 'validation_design_tree': '51fb987e80b8a7d25dc69113c7e64827326b86f0',
 'validation_document': 'docs/architecture/validation-feedback.md'}

ORACLE_RULES = (('mem03-selected-record-work',
  'tests.host.selected_ownership_evidence.SelectedOwnershipOracles.test_portable_selected_work',
  'selected-record-work;no-inactive-audit;exact-state-and-cursor-guards;exact-complete-helper-tokens',
  'ADR-0103',
  'Actual Red, Green and recertification',
  (2, 3, 4, 5, 15, 16, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 31, 32, 33, 34, 35, 36, 37, 38, 39)),
 ('mem03-runtime-owner-work',
  'tests.host.selected_ownership_evidence.SelectedOwnershipOracles.test_runtime_owner_work',
  'actual-runtime-release;no-inherited-full-ledger-or-object-audit',
  'ADR-0103',
  'Actual Red, Green and recertification',
  (14, 17, 18, 29, 30)),
 ('mem03-consumer-audit-boundaries',
  'tests.host.selected_ownership_evidence.SelectedOwnershipOracles.test_consumer_audit_boundaries',
  'actual-PM-consumer;18-migrations-and8-retained-audit-boundaries',
  'ADR-0103',
  'Actual Red, Green and recertification',
  (0, 1, 6, 7, 8, 9, 10, 11, 12, 13)))

RECERTIFICATION_UNITS = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
EXTERNAL_INPUTS = (
    "@mem03/native-compiler", "@mem03/native-nm", "@mem03/closed-tools", "@mem03/native-cache",
    "@mem03/qemu-firmware", "@mem03/accepted-design", "@mem03/fixed-adapters",
)
PHASE_KINDS = {
    "red": EventKind.RED_OBSERVED,
    "green": EventKind.GREEN_OBSERVED,
    "recertify": EventKind.AUTHOR_RECERTIFIED,
}
JOURNAL_PATH = "build/evidence-transitions/MEM03"
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
    result = subprocess.run(
        ("git", *arguments), cwd=root, stdin=subprocess.DEVNULL,
        capture_output=True, check=False, timeout=30,
    )
    if result.returncode:
        raise ValueError("MEM03 fixed Git authority is unavailable: " + result.stderr.decode())
    return result.stdout


def design_blob(root, path):
    if Path(path).is_absolute() or "\\" in path or ".." in Path(path).parts:
        raise ValueError("MEM03 design input leaves its fixed namespace")
    for commit, tree in (
        (DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"]),
        (DESIGN_PINS["original_design_commit"], DESIGN_PINS["original_design_tree"]),
        (DESIGN_PINS["runtime_reference"], DESIGN_PINS["runtime_tree"]),
        (DESIGN_PINS["implementation_base_commit"], DESIGN_PINS["implementation_base_tree"]),
        (DESIGN_PINS["validation_design_commit"], DESIGN_PINS["validation_design_tree"]),
    ):
        if git(root, "rev-parse", commit + "^{tree}").decode().strip() != tree:
            raise ValueError("MEM03 fixed commit/tree identity changed")
    reference = DESIGN_PINS[
        "validation_design_commit"
        if path in (DESIGN_PINS["validation_adr_document"], DESIGN_PINS["validation_document"])
        else "design_commit"
    ]
    return git(root, "show", reference + ":" + path)


def base_membership(root):
    result = {}
    for row in filter(None, git(root, "ls-tree", "-r", "-z", DESIGN_PINS["implementation_base_commit"]).split(b"\0")):
        metadata, separator, raw_path = row.partition(b"\t")
        fields = metadata.split()
        path = raw_path.decode()
        if (
            not separator or len(fields) != 3 or fields[0] not in (b"100644", b"100755")
            or fields[1] != b"blob" or path in result or Path(path).is_absolute()
            or ".." in Path(path).parts or "\\" in path
        ):
            raise ValueError("MEM03 implementation base has an unadmitted source member")
        result[path] = (fields[0].decode(), fields[2].decode())
    if digest(result) != SOURCE_ROLES["base_membership_sha256"]:
        raise ValueError("MEM03 complete implementation-base modes/blob membership changed")
    return {path: (int(mode, 8) & 0o777, oid) for path, (mode, oid) in result.items()}


def declared_scope(root):
    for name in ("adr_document", "correction_adr_document", "validation_adr_document"):
        if b"\n- Status: Accepted\n" not in design_blob(root, DESIGN_PINS[name]):
            raise ValueError("MEM03 requires its separately merged Accepted designs")
    if RECIPE_ADMISSION["native_tiers"] != ("test-unit",) or RECIPE_ADMISSION["native_label"] != "slow":
        raise ValueError("MEM03 orchestration must retain ADR-0105 full-only placement")
    contract = design_blob(root, DESIGN_PINS["design_document"]).decode()
    blocks = re.findall(r"^```text\n(.*?)\n```$", contract, re.S | re.M)
    for prefix, expected in (
        ("tests/host/selected_ownership_test.c", SOURCE_ROLES["immutable_additions"]),
        ("include/micros/frame_ownership.h\n", SOURCE_ROLES["production_paths"]),
    ):
        if [tuple(block.splitlines()) for block in blocks if block.startswith(prefix)] != [expected]:
            raise ValueError("MEM03 source scope differs from its fixed Accepted contract")
    candidates = [tuple(block.splitlines()) for block in blocks if block.startswith("tests/host/bootstrap_allocation_source.py\n")]
    if len(candidates) != 1 or set(candidates[0]) != {*SOURCE_ROLES["fixed_adapters"], SOURCE_ROLES["unchanged_candidate"]}:
        raise ValueError("MEM03 adapter scope differs from its fixed Accepted contract")
    scopes = [block for block in blocks if block.startswith("tests/host/kernel_layout_source.py:data-entry:")]
    if len(scopes) != 1:
        raise ValueError("MEM03 design lacks its exact retired assertion inventory")
    keys = tuple(
        "tests/host/memory_native_source_bindings.py:native-source-admission:" + row
        if row.startswith(("MEM01/", "MEM02/")) else row
        for row in scopes[0].splitlines()
    )
    if keys != SOURCE_PROJECTION["retirement_keys"]:
        raise ValueError("MEM03 retirement differs from its fixed 40-key contract")
    return contract


def declared_commands(contract, executable):
    from tools.python_host_runtime import python_command

    phases = re.findall(
        r"^\$\{MICROS_PYTHON\} -E -S -B tools/check_selected_ownership.py --(red|green|recertify)$",
        contract, re.M,
    )
    if tuple(phases) != tuple(PHASE_KINDS):
        raise ValueError("MEM03 design lacks its three exact recording commands")
    commands = {phase: python_command(executable, RECIPE_ADMISSION["driver"], "--" + phase) for phase in phases}
    rows = re.findall(r"^\| (B[0-9]{2}) \| (.*?) \|$", contract, re.M)
    if tuple(unit for unit, _ in rows) != RECERTIFICATION_UNITS:
        raise ValueError("MEM03 design lacks its exact affected-unit inventory")
    units = []
    for unit, spelling in rows:
        if unit == "B14":
            if spelling != (
                "The actual `--recertify` invocation above, including the complete current native oracle replay, "
                "retained MEM01/MEM02 evidence, authority/source/recipe and Python-owner checks"
            ):
                raise ValueError("MEM03 B14 is not the actual author transaction")
            command = commands["recertify"]
        else:
            if not spelling.startswith("`") or not spelling.endswith("`"):
                raise ValueError("MEM03 unit command has unbound syntax")
            command = tuple(shlex.split(spelling[1:-1]))
            if len(command) != 4 or command[:3] != ("cmake", "--workflow", "--preset"):
                raise ValueError("MEM03 unit command has an undeclared form")
        units.append((unit, command))
    return commands, tuple(units)


def task_definition(root):
    from tests.host import kernel_mapping_authority as previous
    from tools.python_host_runtime import require_current_runtime, selected_executable

    require_current_runtime()
    contract = declared_scope(root)
    phases, commands = declared_commands(contract, selected_executable(dict(os.environ)))
    candidates = SOURCE_ROLES["candidate_paths"]
    members = tuple(sorted(set(base_membership(root)) | set(SOURCE_ROLES["immutable_additions"])))
    immutable = tuple(path for path in members if path not in candidates)
    authority = TaskAuthority(
        "MEM03", "B02", ("B02-F002",),
        DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"], DESIGN_PINS["design_document"],
        RECERTIFICATION_UNITS, phases["red"], phases["green"], phases["recertify"], commands,
        immutable, tuple((unit, candidates) for unit in RECERTIFICATION_UNITS),
        DESIGN_PINS["runtime_reference"], DESIGN_PINS["runtime_tree"],
    )
    inputs = (
        DESIGN_PINS["design_document"], DESIGN_PINS["adr_document"], DESIGN_PINS["research_document"],
        DESIGN_PINS["correction_adr_document"], DESIGN_PINS["correction_research_document"],
        DESIGN_PINS["validation_adr_document"], DESIGN_PINS["validation_document"],
        *SOURCE_ROLES["immutable_additions"],
    )
    keys = SOURCE_PROJECTION["retirement_keys"]
    if any("\n## " + row[4] + "\n" not in contract for row in ORACLE_RULES):
        raise ValueError("MEM03 oracle provenance lacks its Accepted contract section")
    oracles = tuple(Oracle(
        identifier, test, property_name, tuple(keys[index] for index in positions),
        AuthorityClass.BASELINE_ORACLE,
        Provenance(AuthorityKind.ACCEPTED_ADAPTATION, path=DESIGN_PINS["design_document"], adr=adr, section=section),
        inputs,
    ) for identifier, test, property_name, adr, section, positions in ORACLE_RULES)
    plan = Plan(
        TRANSITION, "MEM03", "B02", ("B02-F002",),
        DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"], DESIGN_PINS["design_document"],
        keys, (), keys, oracles, RECERTIFICATION_UNITS, EXTERNAL_INPUTS,
        phases["red"], phases["green"], phases["recertify"], commands,
    )
    if previous.committed_claim(root) is None:
        raise ValueError("MEM03 requires the committed MEM02 authority chain")
    _, authorities, _, registry = previous.task_definition(root)
    claim = TransitionClaim(TRANSITION, "MEM03", keys, tuple(row.oracle_id for row in oracles), keys)
    return authority, {**authorities, "MEM03": authority}, plan, TransitionRegistry((*registry.claims, claim))


def current_membership(root):
    base = base_membership(root)
    expected = set(base) | set(SOURCE_ROLES["immutable_additions"])
    observed = tuple(path.decode() for path in git(root, "ls-files", "-z").split(b"\0") if path)
    if len(observed) != len(set(observed)) or set(observed) != expected:
        raise ValueError("MEM03 staged membership is incomplete or contains unowned inputs")
    roots = tuple(sorted({Path(path).parts[0] for path in expected}))
    if git(root, "ls-files", "--others", "-z", "--", *roots):
        raise ValueError("MEM03 source namespaces contain untracked or ignored inputs")
    return tuple(sorted(expected)), base


def external_inputs(root):
    import shutil
    from tests.host.diagnostic_history_evidence import closed_tool_inputs
    from tests.host.runtime_memory_evidence import native_compiler
    from tests.host.selected_ownership_evidence import native_symbols_tool
    from tools.stack_bound_fatal_firmware import resolve_default_firmware

    compiler, nm = native_compiler(root), native_symbols_tool()
    qemu = shutil.which("qemu-system-riscv64")
    if qemu is None:
        raise ValueError("MEM03 requires its configured recertification tool")
    return compiler, nm, {
        "@mem03/native-compiler": digest(compiler),
        "@mem03/native-nm": digest(nm),
        "@mem03/closed-tools": digest(closed_tool_inputs(root)),
        "@mem03/native-cache": digest(read_regular(Path(root) / "build/host-debug/CMakeCache.txt")[1]),
        "@mem03/qemu-firmware": digest(resolve_default_firmware(qemu)),
        "@mem03/accepted-design": digest({
            "pins": DESIGN_PINS,
            "contract": hashlib.sha256(design_blob(root, DESIGN_PINS["design_document"])).hexdigest(),
            "adr": hashlib.sha256(design_blob(root, DESIGN_PINS["adr_document"])).hexdigest(),
            "correction_adr": hashlib.sha256(design_blob(root, DESIGN_PINS["correction_adr_document"])).hexdigest(),
            "validation_adr": hashlib.sha256(design_blob(root, DESIGN_PINS["validation_adr_document"])).hexdigest(),
            "validation_contract": hashlib.sha256(design_blob(root, DESIGN_PINS["validation_document"])).hexdigest(),
        }),
        "@mem03/fixed-adapters": digest({
            path: read_regular(Path(root) / path)[1] for path in SOURCE_ROLES["fixed_adapters"]
        }),
    }


def capture_author_context(root, phase):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host.evidence_authority_validation import validate_transition
    from tests.host.selected_ownership_source import baseline_source, validate_retained_source, validate_scaffold
    from tests.host.work_count_types import Stage
    from tools.stack_bound_manifest import owned_artifact
    from tools.validation_context import capture_candidate

    if phase not in PHASE_KINDS:
        raise ValueError("MEM03 source scope requires an exact author phase")
    root = Path(root).resolve()
    candidate = capture_candidate(root)
    if not candidate.clean:
        raise ValueError("MEM03 requires the complete clean staged candidate")
    members, base = current_membership(root)
    files = {path: read_regular(owned_artifact(root, path))[1] for path in members}
    validate_scaffold(root)
    for path in SOURCE_ROLES["production_paths"]:
        text = read_regular(root / path)[0].decode()
        selected = Stage.RED if text == baseline_source(str(root), path) else Stage.GREEN
        validate_retained_source(root, path, text, selected)
    for path, (mode, oid) in base.items():
        if (
            not path.startswith(("arch/", "kernel/", "include/", "lib/", "servers/", "user/"))
            and path != SOURCE_ROLES["unchanged_candidate"]
        ) or path in SOURCE_ROLES["production_paths"]:
            continue
        data, identity = read_regular(root / path)
        observed = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if (identity["mode"], observed) != (mode, oid):
            raise ValueError("MEM03 changed production or literal bindings outside its declared subjects: " + path)
    authority, authorities, plan, registry = task_definition(root)
    compiler, nm, external = external_inputs(root)
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
        raise ValueError("MEM03 source/declaration closure failed: " + str(result.error))
    if capture_candidate(root) != candidate:
        raise ValueError("MEM03 source changed while its complete inputs were captured")
    return AuthorContext(root, phase, candidate, authority, authorities, plan, registry,
                         inventory, environment, files, compiler, nm)


def recheck_context(context):
    if capture_author_context(context.root, context.phase) != context:
        raise ValueError("MEM03 source/tool/input identities changed during execution or publication")


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
    if current != expected or COMPLETED_CLAIM != (TRANSITION, "MEM03", ("B02-F002",)):
        raise ValueError("MEM03 completion claim changed its declared design or scope")
    paths = tuple(name.decode() for name in git(
        root, "ls-tree", "-r", "--name-only", "-z", "HEAD", "--", AUTHORITY_PATH, SOURCE_PATH,
    ).split(b"\0") if name)
    if not paths:
        return None
    if set(paths) != {AUTHORITY_PATH, SOURCE_PATH}:
        raise ValueError("MEM03 committed claim has an incomplete owner set")
    previous = _literal_declarations(
        git(root, "show", "HEAD:" + AUTHORITY_PATH), git(root, "show", "HEAD:" + SOURCE_PATH),
    )
    if previous != current:
        raise ValueError("MEM03 staged claim cannot borrow earlier committed authority")
    keys = SOURCE_PROJECTION["retirement_keys"]
    return TransitionClaim(TRANSITION, "MEM03", keys, tuple(row[0] for row in ORACLE_RULES), keys)


def journal_directory(root):
    directory = Path(root)
    for part in Path(JOURNAL_PATH).parts:
        directory /= part
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("MEM03 journal namespace is linked or non-directory")
    return directory


@contextmanager
def author_lock(root):
    directory = Path(root)
    for part in Path(JOURNAL_PATH).parts:
        directory /= part
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("MEM03 journal namespace is linked or non-directory")
        directory.mkdir(mode=0o700, exist_ok=True)
    descriptor = os.open(directory / ".writer.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ValueError("MEM03 writer lock is not an unlinked regular file")
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
        raise ValueError("MEM03 phase/attempt is not admitted")
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
        raise ValueError("MEM03 phase record has a borrowed or broken predecessor")
    event = decode_event(value["event"])
    if event.kind is not PHASE_KINDS[phase]:
        raise ValueError("MEM03 phase record has the wrong event kind")
    record_fields(value["logs"], {"execution"} | (set(RECERTIFICATION_UNITS) if phase == "recertify" else set()))
    for name, declared in value["logs"].items():
        record_fields(declared, ("size", "sha256"))
        if (
            type(declared["size"]) is not int or declared["size"] < 0
            or type(declared["sha256"]) is not str or re.fullmatch(r"[a-f0-9]{64}", declared["sha256"]) is None
        ):
            raise ValueError("MEM03 execution log has an invalid byte identity")
        _, observed = read_regular(directory / (phase + "-" + name + ".log"))
        if any(observed[key] != declared[key] for key in declared):
            raise ValueError("MEM03 log bytes changed after execution")
    return {**value, "event": event, "environment": decode_environment(value["environment"])}, identity


def validate_journal(context, journal):
    from tests.host.diagnostic_history_authority import json_pairs, record_fields, record_strings
    from tests.host.evidence_authority_validation import validate_transition
    from tests.host.selected_ownership_evidence import validate_execution_log

    result = validate_transition(
        context.inventory, context.authorities, context.registry, context.plan,
        tuple(row["event"] for row in journal.records), context.environment,
    )
    if not result.ok:
        raise ValueError("MEM03 transition rejected: " + str(result.error))
    reference = None
    for phase, record in zip(PHASE_KINDS, journal.records):
        if record["event"].expected_property != ";".join(oracle.property for oracle in context.plan.replacement_oracles):
            raise ValueError("MEM03 event changed its complete declared property set")
        replay = json.loads(read_regular(journal.directory / (phase + "-execution.log"))[0], object_pairs_hook=json_pairs)
        outcomes = validate_execution_log(context, replay, phase=phase, attempt=journal.attempt, reference=reference)
        if outcomes != record["event"].oracle_results:
            raise ValueError("MEM03 event borrows results from another execution")
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
                raise ValueError("MEM03 unit log lacks its actual canonical successful execution")
    return result


def read_verified_journal(context):
    from tools.stack_bound_manifest import owned_artifact

    directory = journal_directory(context.root)
    current, selector_sha256 = read_selection(directory)
    if current is None:
        raise ValueError("MEM03 requires a recorded --red attempt")
    selected = owned_artifact(directory, current["attempt"])
    if not selected.is_dir():
        raise ValueError("MEM03 selected attempt is absent")
    records, previous = [], ""
    for phase in tuple(PHASE_KINDS)[:current["count"]]:
        record, identity = read_phase_record(selected, phase, current["attempt"], previous)
        environment = record["environment"]
        if (
            environment.immutable_inputs != context.environment.immutable_inputs
            or environment.red_candidate_inputs != context.environment.red_candidate_inputs
            or phase != "red" and environment != context.environment
        ):
            raise ValueError("MEM03 source/oracle/tool/adapter inputs changed; start a new --red attempt")
        records.append(record)
        previous = identity["sha256"]
    if previous != current["sha256"] or read_selection(directory) != (current, selector_sha256):
        raise ValueError("MEM03 selector changed or does not bind the complete raw chain")
    journal = Journal(selected, current["attempt"], tuple(records), previous, selector_sha256)
    return journal, validate_journal(context, journal)


def read_journal(context):
    return read_verified_journal(context)[0]


def baseline_execution(journal):
    from tests.host.diagnostic_history_authority import json_pairs

    if not journal.records:
        raise ValueError("MEM03 lacks its actual original execution")
    return json.loads(read_regular(journal.directory / "red-execution.log")[0], object_pairs_hook=json_pairs)["native"]


@contextmanager
def provisional_retirement(context, journal, replay):
    from tests.host.selected_ownership_evidence import validate_execution_log

    global _provisional
    if _provisional is not None or len(journal.records) != 2 or context.phase != "recertify":
        raise ValueError("MEM03 provisional retirement requires one complete Green transaction")
    validate_journal(context, journal)
    outcomes = validate_execution_log(
        context, replay, phase="recertify", attempt=journal.attempt, reference=baseline_execution(journal),
    )
    if outcomes != journal.records[-1]["event"].oracle_results:
        raise ValueError("MEM03 recertification lacks complete actual Green replay")
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
            raise ValueError("MEM03 candidate changed inside its checked author transaction")
        return frozenset(keys)
    if committed_claim(root) is not None:
        _, authorities, _, registry = task_definition(root)
        result = validate_transition(current_inventory(root), authorities, registry, None, (), None)
        if not result.ok:
            raise ValueError("MEM03 committed retirement failed ordinary authority validation")
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
        from tests.host.kernel_mapping_authority import identity_required as previous

        return previous(root, key)
    return key not in retired_identity_keys(root)
