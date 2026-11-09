"""MEM05's closed design authority and actual execution transition."""

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
import subprocess
import stat
import uuid

from tests.host.evidence_authority_source import literal_value, single_assignment
from tests.host.evidence_authority_types import (
    AuthorityClass, AuthorityKind, Environment, EventKind, Oracle, Plan, Provenance,
    TaskAuthority, TransitionClaim, TransitionRegistry, TransitionState, digest, fact_set_digest,
)
from tests.host.selected_release_handoff_source import (
    AUTHORITY_PATH, FAMILY, RECIPE_ADMISSION, SOURCE_PATH, SOURCE_PROJECTION, SOURCE_ROLES,
    candidate_paths, read_declarations,
)
from tools.stack_artifact_files import read_regular

TRANSITION = "MEM05-selected-release-handoff"
COMPLETED_CLAIM = ("MEM05-selected-release-handoff", "MEM05", ("B02-F006", "B02-F007"))
DESIGN_PINS = {
    "design_commit": "8f3b19ccae21a06345d05415708cd2dc1db32e76",
    "design_tree": "1a7500dac64cf8605faa2b699c68b2caf6217f6b",
    "implementation_base_commit": "8f3b19ccae21a06345d05415708cd2dc1db32e76",
    "implementation_base_tree": "1a7500dac64cf8605faa2b699c68b2caf6217f6b",
    "runtime_reference": "abe05cdd7bced5e77e86c1ec12754d017f920cab",
    "runtime_tree": "2c34dac3c259a4975d1c213e3e14c18d8a075658",
    "minix_reference": "4db99f4012570a577414fe2a43697b2f239b699e",
    "minix_tree": "80db89de58d5ecaf31efe48b4d1d98c7e285e81f",
    "design_document": "docs/architecture/selected-release-and-wired-handoff.md",
    "adr_document": "docs/adr/0114-selected-release-and-wired-handoff-plans.md",
    "research_document": "docs/research/minix-selected-release-and-wired-handoff.md",
}
ORACLE_RULES = (
    (
        "mem05-owner-index",
        "tests.host.selected_release_handoff_evidence.SelectedReleaseHandoffOracles.test_owner_index",
        "actual-selected-set-and-exact-generation-work;coherent-all-owner-transitions",
        "ADR-0114", "Oracle partition and actual phase commands",
        (0, 2, 3, 4, 5, 6, 7, 19, 20, 21, 22, 23, 25, 26, 27, 28, 29, 33, 34, 38, 39,
         40, 41, 42, 43, 44, 47, 48, 49, 50, 51, 52, 53, 69, 70, 71, 72, 76, 77, 80, 81),
    ),
    (
        "mem05-selected-release",
        "tests.host.selected_release_handoff_evidence.SelectedReleaseHandoffOracles.test_selected_release",
        "actual-selected-page-and-process-release-work;failure-atomicity;target-scoped-release",
        "ADR-0114", "Oracle partition and actual phase commands",
        (8, 9, 11, 12, 13, 14, 15, 16, 17, 18, 30, 31, 32, 35, 54, 55, 56, 57, 58, 73, 75),
    ),
    (
        "mem05-wired-handoff",
        "tests.host.selected_release_handoff_evidence.SelectedReleaseHandoffOracles.test_wired_handoff",
        "actual-linear-handoff-planning;owned-state-versus-target-errors;retained-target-boundaries",
        "ADR-0114", "Oracle partition and actual phase commands",
        (1, 10, 24, 36, 37, 45, 46, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 74, 78, 79),
    ),
)

RECERTIFICATION_UNITS = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
EXTERNAL_INPUTS = (
    "@mem05/native-compiler", "@mem05/native-nm", "@mem05/closed-tools", "@mem05/native-cache",
    "@mem05/qemu-firmware", "@mem05/accepted-design", "@mem05/frozen-candidates",
)
PHASE_KINDS = {
    "red": EventKind.RED_OBSERVED,
    "green": EventKind.GREEN_OBSERVED,
    "recertify": EventKind.AUTHOR_RECERTIFIED,
}
JOURNAL_PATH = "build/evidence-transitions/MEM05"
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
        raise ValueError("MEM05 fixed Git authority is unavailable: " + completed.stderr.decode())
    return completed.stdout


def design_blob(root, path):
    if path not in tuple(DESIGN_PINS[name] for name in ("design_document", "adr_document", "research_document")):
        raise ValueError("MEM05 design input leaves its fixed namespace")
    for commit, tree in (
        (DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"]),
        (DESIGN_PINS["runtime_reference"], DESIGN_PINS["runtime_tree"]),
        (DESIGN_PINS["implementation_base_commit"], DESIGN_PINS["implementation_base_tree"]),
    ):
        if git(root, "rev-parse", commit + "^{tree}").decode().strip() != tree:
            raise ValueError("MEM05 fixed commit/tree identity changed")
    return git(root, "show", DESIGN_PINS["design_commit"] + ":" + path)


def base_membership(root):
    result = {}
    for entry in filter(None, git(root, "ls-tree", "-r", "-z", DESIGN_PINS["implementation_base_commit"]).split(b"\0")):
        metadata, separator, raw_path = entry.partition(b"\t")
        fields = metadata.split()
        path = raw_path.decode()
        if (
            not separator or len(fields) != 3 or fields[0] not in (b"100644", b"100755")
            or fields[1] != b"blob" or path in result or Path(path).is_absolute()
            or ".." in Path(path).parts or "\\" in path
        ):
            raise ValueError("MEM05 implementation base has an unadmitted source member")
        result[path] = (fields[0].decode(), fields[2].decode())
    if len(result) != 1518 or digest(result) != SOURCE_ROLES["base_membership_sha256"]:
        raise ValueError("MEM05 implementation-base mode/blob membership changed")
    return {path: (int(mode, 8) & 0o777, oid) for path, (mode, oid) in result.items()}


def historical_predecessor_records(root, inventory):
    from tests.host.evidence_authority_inventory import historical_object_binding_facts
    from tests.host.evidence_authority_validation import validate_inventory

    checked = validate_inventory(inventory)
    if not checked.ok:
        raise ValueError("MEM05 complete current inventory is invalid: " + str(checked.error))
    tree = ast.parse(git(
        root, "show", DESIGN_PINS["runtime_reference"] + ":tests/host/evidence_authority_inventory.py",
    ).decode())
    expected = literal_value(single_assignment(tree, "DECLARED_FAMILY_FACT_DIGESTS").value)
    if type(expected) is not dict or len(expected) != 22 or FAMILY in expected:
        raise ValueError("MEM05 historical family register changed")
    records = tuple(
        fact for fact in historical_object_binding_facts(root, inventory.facts) if fact.family in expected
    )
    if any(
        fact_set_digest(fact for fact in records if fact.family == name) != fingerprint
        for name, fingerprint in expected.items()
    ):
        raise ValueError("MEM05 changed a historical family fingerprint")
    if len(records) != 5379 or fact_set_digest(records) != SOURCE_PROJECTION["predecessor_facts_sha256"]:
        raise ValueError("MEM05 changed a complete predecessor record")
    return records


def _immutable_adapters_from_contract(contract):
    match = re.search(
        r"\| Inputs \| Exact adaptation \|\n\| --- \| --- \|\n(.*?)\n\nNo ", contract, re.S,
    )
    if match is None:
        raise ValueError("MEM05 design lacks its fixed existing-adapter table")
    paths = []
    for line in match[1].splitlines():
        cell = line.split("|", 2)[1]
        previous = None
        for token in re.findall(r"`([^`]+)`", cell):
            if token == ".h" and previous is not None:
                token = str(Path(previous).with_suffix(".h"))
            elif "/" not in token and previous is not None:
                token = str(Path(previous).parent / token)
            if not token.endswith((".py", ".c", ".h", ".txt")):
                raise ValueError("MEM05 adapter table has an unbound path")
            paths.append(token)
            previous = token
    return tuple(paths)


def _oracle_partition(facts):
    groups = {"mem05-owner-index": [], "mem05-selected-release": [], "mem05-wired-handoff": []}
    production = set(SOURCE_ROLES["production_paths"])
    release = {
        "user_address_space.c", "user_address_space_internal.h", "user_address_space_mutation.c",
        "user_address_space_runtime.c", "user_address_space_validation.c",
    }
    for index, fact in enumerate(facts):
        subjects = production.intersection(fact.payload_paths)
        if len(subjects) != 1:
            raise ValueError("MEM05 identity lacks one exact production subject")
        path = subjects.pop()
        if (
            path.endswith(("/frame_ownership_handoff.c", "/user_address_space_handoff.c"))
            or fact.scope.endswith("/micros_frame_ownership_runtime_prepare_wired_process_user_set")
        ):
            group = "mem05-wired-handoff"
        elif Path(path).name in release:
            group = "mem05-selected-release"
        else:
            group = "mem05-owner-index"
        groups[group].append(index)
    return {name: tuple(indices) for name, indices in groups.items()}


def declared_scope(root):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host import selected_address_space_authority as previous

    if b"\n- Status: Accepted\n" not in design_blob(root, DESIGN_PINS["adr_document"]):
        raise ValueError("MEM05 requires its separately merged Accepted design")
    contract = design_blob(root, DESIGN_PINS["design_document"]).decode()
    blocks = re.findall(r"^```text\n(.*?)\n```$", contract, re.S | re.M)

    def block(prefix):
        matching = [tuple(value.splitlines()) for value in blocks if value.startswith(prefix)]
        if len(matching) != 1:
            raise ValueError("MEM05 design lost a closed input boundary")
        return matching[0]

    if block("tests/host/selected_release_handoff_test.c\n") != SOURCE_ROLES["immutable_additions"]:
        raise ValueError("MEM05 evidence owners differ from the Accepted design")
    if block("kernel/tests/memory/address_space_handoff_test_runtime.c") != SOURCE_ROLES["dependent_target_paths"]:
        raise ValueError("MEM05 target dependency differs from the Accepted design")
    if _immutable_adapters_from_contract(contract) != SOURCE_ROLES["immutable_adapters"]:
        raise ValueError("MEM05 existing immutable adapters differ from the Accepted design")
    census = contract.split("## Prospective exact transport selection\n", 1)[1]
    bound_digest = re.findall(r"^```text\n([a-f0-9]{64})\n```$", census, re.M)[0]
    if bound_digest != SOURCE_PROJECTION["affected_facts_sha256"]:
        raise ValueError("MEM05 affected records lack their independent Accepted digest")
    inventory = current_inventory(root)
    prior = historical_predecessor_records(root, inventory)
    keys = SOURCE_PROJECTION["retirement_keys"]
    facts = tuple(sorted((fact for fact in prior if fact.key in keys), key=lambda fact: fact.key))
    if len(facts) != 82 or fact_set_digest(facts) != bound_digest:
        raise ValueError("MEM05 selected identities differ from the complete fixed records")
    _, _, _, registry = previous.task_definition(root)
    if set(keys) & {key for claim in registry.claims for key in claim.retired_identity_keys}:
        raise ValueError("MEM05 cannot retire a previously retired identity")
    partition = _oracle_partition(facts)
    if any(row[5] != partition[row[0]] for row in ORACLE_RULES):
        raise ValueError("MEM05 oracle identities do not follow the Accepted subject partition")
    closure = {path for fact in facts for path in (fact.owner_path, *fact.payload_paths)}
    dependency = set(SOURCE_ROLES["dependent_target_paths"])
    production = {path for path in closure if path.startswith(("kernel/", "include/")) and not path.startswith("kernel/tests/")}
    if (
        len(closure) != 28 or closure & dependency or closure | dependency != set(candidate_paths())
        or production != set(SOURCE_ROLES["production_paths"])
    ):
        raise ValueError("MEM05 candidate/production roles differ from the exact selected closure")
    return contract


def declared_commands(contract, executable):
    from tools.python_host_runtime import python_command

    phases = re.findall(
        r'^"\$\{MICROS_PYTHON\}" -E -S -B tools/check_selected_release_handoff.py --(red|green|recertify)$',
        contract, re.M,
    )
    if tuple(phases) != tuple(PHASE_KINDS):
        raise ValueError("MEM05 design lacks its three exact recording commands")
    commands = {phase: python_command(executable, RECIPE_ADMISSION["driver"], "--" + phase) for phase in phases}
    rows = re.findall(r"^\| (B[0-9]{2}) \| (.*?) \|$", contract, re.M)
    if tuple(unit for unit, _ in rows) != RECERTIFICATION_UNITS:
        raise ValueError("MEM05 design lacks its exact affected-unit inventory")
    units = []
    for unit, spelling in rows:
        if unit == "B14":
            if spelling != (
                "The actual `--recertify` command above, including the complete current native/target "
                "oracle and retained host checks"
            ):
                raise ValueError("MEM05 B14 is not the actual author transaction")
            command = commands["recertify"]
        else:
            if not spelling.startswith("`") or not spelling.endswith("`"):
                raise ValueError("MEM05 unit command has unbound syntax")
            command = tuple(shlex.split(spelling[1:-1]))
            if len(command) != 4 or command[:3] != ("cmake", "--workflow", "--preset"):
                raise ValueError("MEM05 unit command has an undeclared form")
        units.append((unit, command))
    return commands, tuple(units)


def task_definition(root):
    from tests.host import selected_address_space_authority as previous
    from tools.python_host_runtime import require_current_runtime, selected_executable

    require_current_runtime()
    contract = declared_scope(root)
    phases, commands = declared_commands(contract, selected_executable(dict(os.environ)))
    candidates = candidate_paths()
    members = tuple(sorted(set(base_membership(root)) | set(SOURCE_ROLES["immutable_additions"])))
    immutable = tuple(path for path in members if path not in candidates)
    authority = TaskAuthority(
        "MEM05", "B02", ("B02-F006", "B02-F007"), DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"],
        DESIGN_PINS["design_document"], RECERTIFICATION_UNITS,
        phases["red"], phases["green"], phases["recertify"], commands,
        immutable, tuple((unit, candidates) for unit in RECERTIFICATION_UNITS),
        DESIGN_PINS["runtime_reference"], DESIGN_PINS["runtime_tree"],
    )
    keys = SOURCE_PROJECTION["retirement_keys"]
    inputs = (
        DESIGN_PINS["design_document"], DESIGN_PINS["adr_document"], DESIGN_PINS["research_document"],
        *SOURCE_ROLES["immutable_additions"], *SOURCE_ROLES["immutable_adapters"],
    )
    if any("\n## " + row[4] + "\n" not in contract for row in ORACLE_RULES):
        raise ValueError("MEM05 oracle provenance lacks its Accepted section")
    oracles = tuple(Oracle(
        identifier, test, property_name, tuple(keys[index] for index in positions),
        AuthorityClass.BASELINE_ORACLE,
        Provenance(AuthorityKind.ACCEPTED_ADAPTATION, path=DESIGN_PINS["design_document"], adr=adr, section=section),
        inputs,
    ) for identifier, test, property_name, adr, section, positions in ORACLE_RULES)
    plan = Plan(
        TRANSITION, "MEM05", "B02", ("B02-F006", "B02-F007"),
        DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"], DESIGN_PINS["design_document"],
        keys, (), keys, oracles, RECERTIFICATION_UNITS, EXTERNAL_INPUTS,
        phases["red"], phases["green"], phases["recertify"], commands,
    )
    if previous.committed_claim(root) is None:
        raise ValueError("MEM05 requires the committed MEM04 authority chain")
    _, authorities, _, registry = previous.task_definition(root)
    claim = TransitionClaim(TRANSITION, "MEM05", keys, tuple(row.oracle_id for row in oracles), keys)
    return authority, {**authorities, "MEM05": authority}, plan, TransitionRegistry((*registry.claims, claim))


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
    if (
        current != {**read_declarations(root), "COMPLETED_CLAIM": COMPLETED_CLAIM}
        or COMPLETED_CLAIM != (TRANSITION, "MEM05", ("B02-F006", "B02-F007"))
    ):
        raise ValueError("MEM05 completion claim changed its declared design or scope")
    paths = tuple(name.decode() for name in git(
        root, "ls-tree", "-r", "--name-only", "-z", "HEAD", "--", AUTHORITY_PATH, SOURCE_PATH,
    ).split(b"\0") if name)
    if not paths:
        return None
    if set(paths) != {AUTHORITY_PATH, SOURCE_PATH}:
        raise ValueError("MEM05 committed claim has an incomplete owner set")
    previous = _literal_declarations(
        git(root, "show", "HEAD:" + AUTHORITY_PATH), git(root, "show", "HEAD:" + SOURCE_PATH),
    )
    if previous != current:
        raise ValueError("MEM05 staged claim cannot borrow earlier committed authority")
    keys = SOURCE_PROJECTION["retirement_keys"]
    return TransitionClaim(TRANSITION, "MEM05", keys, tuple(row[0] for row in ORACLE_RULES), keys)


def current_membership(root):
    base = base_membership(root)
    expected = set(base) | set(SOURCE_ROLES["immutable_additions"])
    observed = tuple(path.decode() for path in git(root, "ls-files", "-z").split(b"\0") if path)
    if len(observed) != len(set(observed)) or set(observed) != expected:
        raise ValueError("MEM05 staged membership is incomplete or contains unowned inputs")
    roots = tuple(sorted({Path(path).parts[0] for path in expected}))
    if git(root, "ls-files", "--others", "-z", "--", *roots):
        raise ValueError("MEM05 source namespaces contain untracked or ignored inputs")
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
        raise ValueError("MEM05 requires its configured target tool")
    return compiler, nm, {
        "@mem05/native-compiler": digest(compiler), "@mem05/native-nm": digest(nm),
        "@mem05/closed-tools": digest(closed_tool_inputs(root)),
        "@mem05/native-cache": digest(read_regular(Path(root) / "build/host-debug/CMakeCache.txt")[1]),
        "@mem05/qemu-firmware": digest(resolve_default_firmware(qemu)),
        "@mem05/accepted-design": digest({
            "pins": DESIGN_PINS,
            **{name: hashlib.sha256(design_blob(root, DESIGN_PINS[name])).hexdigest()
               for name in ("design_document", "adr_document", "research_document")},
        }),
        "@mem05/frozen-candidates": digest({
            path: read_regular(Path(root) / path)[1]
            for path in (*SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["unchanged_candidates"])
        }),
    }


def capture_author_context(root, phase):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host.evidence_authority_validation import validate_transition
    from tests.host.selected_release_handoff_source import fixed_source, validate_retained_source, validate_scaffold
    from tests.host.work_count_types import Stage
    from tools.stack_bound_manifest import owned_artifact
    from tools.validation_context import capture_candidate

    if phase not in PHASE_KINDS:
        raise ValueError("MEM05 source scope requires an exact author phase")
    root = Path(root).resolve()
    candidate = capture_candidate(root)
    if not candidate.clean:
        raise ValueError("MEM05 requires the complete clean staged candidate")
    members, base = current_membership(root)
    files = {path: read_regular(owned_artifact(root, path))[1] for path in members}
    validate_scaffold(root)
    for path in SOURCE_ROLES["production_paths"]:
        data = read_regular(root / path)[0]
        stage = Stage.RED if data == fixed_source(str(root), path) else Stage.GREEN
        if phase == "red" and stage is not Stage.RED:
            raise ValueError("MEM05 actual Red must precede every production edit")
        validate_retained_source(root, path, data.decode(), stage)
    allowed = set((
        *SOURCE_ROLES["production_paths"], *SOURCE_ROLES["fixed_adapters"],
        *SOURCE_ROLES["immutable_adapters"],
    ))
    for path, (mode, oid) in base.items():
        if path in allowed or path.endswith(".md"):
            continue
        data, identity = read_regular(root / path)
        observed = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if (identity["mode"], observed) != (mode, oid):
            raise ValueError("MEM05 changed code/literals outside its exact source roles: " + path)
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
        raise ValueError("MEM05 source/declaration closure failed: " + str(result.error))
    if capture_candidate(root) != candidate:
        raise ValueError("MEM05 source changed during complete input capture")
    return AuthorContext(root, phase, candidate, authority, authorities, plan, registry,
                         inventory, environment, files, compiler, nm)


def recheck_context(context):
    if capture_author_context(context.root, context.phase) != context:
        raise ValueError("MEM05 source/tool/input identities changed during execution or publication")


def journal_directory(root):
    directory = Path(root)
    for part in Path(JOURNAL_PATH).parts:
        directory /= part
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("MEM05 journal namespace is linked or non-directory")
    return directory


@contextmanager
def author_lock(root):
    directory = Path(root)
    for part in Path(JOURNAL_PATH).parts:
        directory /= part
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("MEM05 journal namespace is linked or non-directory")
        directory.mkdir(mode=0o700, exist_ok=True)
    descriptor = os.open(directory / ".writer.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        information = os.fstat(descriptor)
        if not stat.S_ISREG(information.st_mode) or information.st_nlink != 1:
            raise ValueError("MEM05 writer lock is not an unlinked regular file")
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
        raise ValueError("MEM05 phase/attempt is not admitted")
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
        raise ValueError("MEM05 phase record has a borrowed or broken predecessor")
    event = decode_event(value["event"])
    if event.kind is not PHASE_KINDS[phase]:
        raise ValueError("MEM05 phase record has the wrong event kind")
    record_fields(value["logs"], {"execution"} | (set(RECERTIFICATION_UNITS) if phase == "recertify" else set()))
    for name, declared in value["logs"].items():
        record_fields(declared, ("size", "sha256"))
        if (
            type(declared["size"]) is not int or declared["size"] < 0
            or type(declared["sha256"]) is not str or re.fullmatch(r"[a-f0-9]{64}", declared["sha256"]) is None
        ):
            raise ValueError("MEM05 execution log has an invalid byte identity")
        _, observed = read_regular(directory / (phase + "-" + name + ".log"))
        if any(observed[key] != declared[key] for key in declared):
            raise ValueError("MEM05 log bytes changed after execution")
    return {**value, "event": event, "environment": decode_environment(value["environment"])}, identity


def validate_journal(context, journal):
    from tests.host.diagnostic_history_authority import json_pairs, record_fields, record_strings
    from tests.host.evidence_authority_validation import validate_transition
    from tests.host.selected_release_handoff_evidence import validate_execution_log

    result = validate_transition(
        context.inventory, context.authorities, context.registry, context.plan,
        tuple(row["event"] for row in journal.records), context.environment,
    )
    if not result.ok:
        raise ValueError("MEM05 transition rejected: " + str(result.error))
    reference = None
    for phase, record in zip(PHASE_KINDS, journal.records):
        if record["event"].expected_property != ";".join(oracle.property for oracle in context.plan.replacement_oracles):
            raise ValueError("MEM05 event changed its complete declared property set")
        replay = json.loads(read_regular(journal.directory / (phase + "-execution.log"))[0], object_pairs_hook=json_pairs)
        outcomes = validate_execution_log(context, replay, phase=phase, attempt=journal.attempt, reference=reference)
        if outcomes != record["event"].oracle_results:
            raise ValueError("MEM05 event borrows results from another execution")
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
                raise ValueError("MEM05 unit log lacks its actual canonical successful execution")
            if command in targets and any(value[key] != targets[command][key] for key in ("exit_code", "output")):
                raise ValueError("MEM05 target/unit evidence is not the same fresh in-phase execution")
    return result


def read_verified_journal(context):
    from tools.stack_bound_manifest import owned_artifact

    directory = journal_directory(context.root)
    current, selector_sha256 = read_selection(directory)
    if current is None:
        raise ValueError("MEM05 requires a recorded --red attempt")
    selected = owned_artifact(directory, current["attempt"])
    if not selected.is_dir():
        raise ValueError("MEM05 selected attempt is absent")
    records, previous = [], ""
    for phase in tuple(PHASE_KINDS)[:current["count"]]:
        record, identity = read_phase_record(selected, phase, current["attempt"], previous)
        environment = record["environment"]
        if (
            environment.immutable_inputs != context.environment.immutable_inputs
            or environment.red_candidate_inputs != context.environment.red_candidate_inputs
            or phase != "red" and environment != context.environment
        ):
            raise ValueError("MEM05 source/oracle/tool/adapter inputs changed; start a new --red attempt")
        records.append(record)
        previous = identity["sha256"]
    if previous != current["sha256"] or read_selection(directory) != (current, selector_sha256):
        raise ValueError("MEM05 selector changed or does not bind the complete raw chain")
    journal = Journal(selected, current["attempt"], tuple(records), previous, selector_sha256)
    return journal, validate_journal(context, journal)


def read_journal(context):
    return read_verified_journal(context)[0]


def baseline_execution(journal):
    from tests.host.diagnostic_history_authority import json_pairs

    if not journal.records:
        raise ValueError("MEM05 lacks its actual original execution")
    return json.loads(read_regular(journal.directory / "red-execution.log")[0], object_pairs_hook=json_pairs)["native"]


@contextmanager
def provisional_retirement(context, journal, replay):
    from tests.host.selected_release_handoff_evidence import validate_execution_log

    global _provisional
    if _provisional is not None or len(journal.records) != 2 or context.phase != "recertify":
        raise ValueError("MEM05 provisional retirement requires one complete Green transaction")
    validate_journal(context, journal)
    outcomes = validate_execution_log(
        context, replay, phase="recertify", attempt=journal.attempt, reference=baseline_execution(journal),
    )
    if outcomes != journal.records[-1]["event"].oracle_results:
        raise ValueError("MEM05 recertification lacks complete actual Green replay")
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
            raise ValueError("MEM05 candidate changed inside its checked author transaction")
        return frozenset(keys)
    if committed_claim(root) is not None:
        _, authorities, _, registry = task_definition(root)
        result = validate_transition(current_inventory(root), authorities, registry, None, (), None)
        if not result.ok:
            raise ValueError("MEM05 committed retirement failed ordinary authority validation")
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
        from tests.host.selected_address_space_authority import identity_required as previous

        return previous(root, key)
    return key not in retired_identity_keys(root)
