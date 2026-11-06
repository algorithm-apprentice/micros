"""COPY01's fixed merged-design authority and original memory source."""

import ast
from contextlib import contextmanager
from dataclasses import dataclass, fields
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import stat
import sys
import uuid

from tests.host.evidence_authority_types import (
    AuthorityClass, AuthorityKind, Environment, Event, EventKind, ObservedResult, Oracle,
    OracleResult, Plan, Provenance, RunStatus, TaskAuthority, TransitionClaim,
    TransitionRegistry, TransitionState, UnitResult, digest,
)
from tests.host.ast_evidence import ast_dump
from tests.host.work_count_evidence import project_observed_source
from tests.host.work_count_types import (
    Kind, ObservationSite, Owner, Placement, Primitive, Region, SourceAdmission, Stage,
)
from tools.validation_inputs import _run
from tools.python_host_runtime import python_command, require_current_runtime, selected_executable
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import build_tools, owned_artifact, publish_json
from tools.validation_context import capture_candidate


DESIGN_COMMIT = "1b42b4c304107fb9ea4fcd7f2197d2e1ee350200"
DESIGN_TREE = "6342889a82a49a371f1dd29354a79a015f6b1442"
DESIGN_DOCUMENT = "docs/architecture/runtime-memory-primitives.md"
ADR_DOCUMENT = "docs/adr/0090-minix-word-based-runtime-memory.md"
MEMORY_PATH = "lib/runtime/memory.c"
IDENTITY_OWNER = "tests/host/test_trap_syscall_source_layout.py"
MEMORY_IDENTITY = IDENTITY_OWNER + ":data-entry:STABLE_HASHES/" + MEMORY_PATH
AUTHORITY_PATH = "tests/host/runtime_memory_authority.py"
COMPLETED_CLAIM = (
    "COPY01-runtime-memory",
    "COPY01",
    "tests/host/test_trap_syscall_source_layout.py:data-entry:STABLE_HASHES/lib/runtime/memory.c",
    ("copy01-copy-aligned-word-work", "copy01-fill-aligned-word-work"),
)
CANDIDATE_PATHS = (MEMORY_PATH, IDENTITY_OWNER)
IMPLEMENTATION_PATHS = (
    "tests/host/runtime_memory_fixture.c",
    "tests/host/runtime_memory_evidence.py",
    "tests/host/runtime_memory_authority.py",
    "tests/host/test_runtime_memory.py",
    "tools/check_runtime_memory.py",
)
FIXTURE_PATH = IMPLEMENTATION_PATHS[0]
EXTERNAL_INPUTS = (
    "@copy01/native-compiler", "@copy01/target-tools", "@copy01/python-runtime",
    "@copy01/native-cache", "@copy01/accepted-design",
)
ORACLE_DECLARATIONS = (
    (
        "copy01-copy-aligned-word-work",
        "tests.host.test_runtime_memory.RuntimeMemoryWorkTest.test_copy_aligned_work",
        "COPY_UNIT=8;COPIED_BYTE=64",
    ),
    (
        "copy01-fill-aligned-word-work",
        "tests.host.test_runtime_memory.RuntimeMemoryWorkTest.test_fill_aligned_work",
        "CLEAR_UNIT=16;CLEARED_BYTE=64",
    ),
)
PHASE_KINDS = {
    "red": EventKind.RED_OBSERVED,
    "green": EventKind.GREEN_OBSERVED,
    "recertify": EventKind.AUTHOR_RECERTIFIED,
}
RECERTIFICATION_UNITS = tuple(f"B{index:02d}" for index in range(5, 13)) + ("B14",)
JOURNAL_PATH = "build/evidence-transitions/COPY01"
_provisional = None


@dataclass(frozen=True)
class AuthorContext:
    root: Path
    candidate: object
    authority: TaskAuthority
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


def design_blob(root, path):
    if path not in (DESIGN_DOCUMENT, ADR_DOCUMENT, MEMORY_PATH, IDENTITY_OWNER):
        raise ValueError("COPY01 has no authority for this design input")
    root = Path(root)
    tree = _run(("git", "rev-parse", DESIGN_COMMIT + "^{tree}"), cwd=root).decode().strip()
    if tree != DESIGN_TREE:
        raise ValueError("COPY01 design commit/tree identity changed")
    return _run(("git", "show", DESIGN_COMMIT + ":" + path), cwd=root)


def baseline_memory(root):
    if b"\n- Status: Accepted\n" not in design_blob(root, ADR_DOCUMENT):
        raise ValueError("COPY01 requires its separately merged Accepted design")
    owner = ast.parse(design_blob(root, IDENTITY_OWNER).decode("utf-8"))
    declarations = [
        node for node in owner.body
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id == "STABLE_HASHES"
            for target in node.targets
        )
    ]
    if len(declarations) != 1:
        raise ValueError("COPY01 lacks its unique original transport declaration")
    expected = ast.literal_eval(declarations[0].value)[MEMORY_PATH]
    source = design_blob(root, MEMORY_PATH)
    if hashlib.sha256(source).hexdigest() != expected:
        raise ValueError("COPY01 original source does not match the retained identity")
    return source


def red_translation_unit(root):
    original = baseline_memory(root).decode("utf-8")
    source = '#include "micros/work_observation.h"\n' + original
    sites = []
    region = Region("for (index = 0; index < length; ++index) {")
    for function, primitive, prefix, kinds in (
        ("micros_runtime_memcpy", "target[index] = input[index];", "copy", (Kind.COPY_UNIT, Kind.COPIED_BYTE)),
        ("micros_runtime_memset", "target[index] = value;", "fill", (Kind.CLEAR_UNIT, Kind.CLEARED_BYTE)),
    ):
        additions = ""
        for suffix, kind in zip(("unit", "byte"), kinds):
            identifier = prefix + "-" + suffix
            additions += (
                f'MICROS_TEST_WORK_HIT("{identifier}", MICROS_WORK_OWNER_RUNTIME, '
                f"MICROS_WORK_KIND_{kind.name}, 1);"
            )
            sites.append(ObservationSite(
                function, identifier, Owner.RUNTIME, kind, Placement.AFTER,
                (region,), Primitive(primitive),
            ))
        if source.count(primitive) != 1:
            raise ValueError("COPY01 original primitive occurrence is ambiguous")
        source = source.replace(primitive, primitive + additions)
    admission = SourceAdmission(MEMORY_PATH, Stage.RED, tuple(sites), include_header=True)
    projected = project_observed_source(MEMORY_PATH, source, admission, stage=Stage.RED)
    if projected != original:
        raise ValueError("COPY01 Red instrumentation does not recover the exact original bytes")
    return source


def green_admission():
    copy = "micros_runtime_memcpy"
    fill = "micros_runtime_memset"
    copy_byte = "*(volatile unsigned char *)target = *(const volatile unsigned char *)input;"
    fill_byte = "*(volatile unsigned char *)target = value;"
    copy_word = (
        "__builtin_memcpy_inline((uint64_t *)__builtin_assume_aligned(target, 8), "
        "(const uint64_t *)__builtin_assume_aligned(input, 8), 8);"
    )
    fill_word = "__builtin_memcpy_inline((uint32_t *)__builtin_assume_aligned(target, 4), &pattern, 4);"
    layout = (
        (copy, "copy-unaligned", copy_byte, 1, (
            "if (((uintptr_t)target & 7U) != ((uintptr_t)input & 7U)) {",
            "while (remaining != 0) {",
        )),
        (copy, "copy-prefix", copy_byte, 1, (
            "while (remaining != 0 && ((uintptr_t)target & 7U) != 0) {",
        )),
        (copy, "copy-word", copy_word, 8, ("while (remaining >= 8) {",)),
        (copy, "copy-tail", copy_byte, 1, ("while (remaining != 0) {",)),
        (fill, "fill-short", fill_byte, 1, ("if (remaining < 12) {", "while (remaining != 0) {")),
        (fill, "fill-prefix", fill_byte, 1, ("while (((uintptr_t)target & 3U) != 0) {",)),
        (fill, "fill-word", fill_word, 4, ("while (remaining >= 4) {",)),
        (fill, "fill-tail", fill_byte, 1, ("while (remaining != 0) {",)),
    )
    sites = []
    for function, prefix, primitive, size, regions in layout:
        kinds = (Kind.COPY_UNIT, Kind.COPIED_BYTE) if function == copy else (Kind.CLEAR_UNIT, Kind.CLEARED_BYTE)
        for suffix, kind, units in (("unit", kinds[0], 1), ("byte", kinds[1], size)):
            sites.append(ObservationSite(
                function, prefix + "-" + suffix, Owner.RUNTIME, kind, Placement.AFTER,
                tuple(Region(region) for region in regions), Primitive(primitive), units,
            ))
    return SourceAdmission(MEMORY_PATH, Stage.GREEN, tuple(sites), include_header=True)


def validate_green_source(source):
    return project_observed_source(MEMORY_PATH, source, green_admission(), stage=Stage.GREEN)


def validate_gate_adapter(root, source):
    from tests.host.terminal_failure_source import project_terminal_gate
    from tests.host.work_count_recipe import project_gate_compatibility

    try:
        projected = project_terminal_gate(IDENTITY_OWNER, source)
        projected = project_gate_compatibility(IDENTITY_OWNER, projected)
        expected = design_blob(root, IDENTITY_OWNER).decode("utf-8")
        if ast_dump(ast.parse(projected)) != ast_dump(ast.parse(expected)):
            raise ValueError("COPY01 assertion adapter changed an unrelated original gate node")
    except AssertionError as error:
        raise ValueError("COPY01 assertion adapter is incomplete or wider than its declaration") from error


def design_membership(root):
    data = _run(("git", "ls-tree", "-r", "-z", DESIGN_COMMIT), cwd=root)
    paths = []
    for row in data.split(b"\0"):
        if not row:
            continue
        prefix, separator, path = row.partition(b"\t")
        values = prefix.split()
        if not separator or len(values) != 3 or values[0] not in (b"100644", b"100755") or values[1] != b"blob":
            raise ValueError("COPY01 design has an unsupported tracked input kind")
        paths.append(path.decode("utf-8"))
    if not paths or len(paths) != len(set(paths)):
        raise ValueError("COPY01 design input membership is incomplete")
    return tuple(sorted(set(paths) | set(IMPLEMENTATION_PATHS)))


def declared_commands(text, executable):
    expected_units = tuple(f"B{index:02d}" for index in range(5, 13)) + ("B14",)
    phase_names = re.findall(
        r"^<MICROS_PYTHON> -E -S -B tools/check_runtime_memory.py --(red|green|recertify)$",
        text, re.MULTILINE,
    )
    if tuple(phase_names) != ("red", "green", "recertify"):
        raise ValueError("COPY01 design lacks its complete recording command set")
    phases = {
        phase: python_command(executable, "tools/check_runtime_memory.py", "--" + phase)
        for phase in phase_names
    }
    rows = re.findall(r"^\| (B[0-9]{2}) \| (.*?) \|$", text, re.MULTILINE)
    if tuple(unit for unit, _ in rows) != expected_units:
        raise ValueError("COPY01 design has missing or borrowed recertification units")
    commands = []
    for unit, value in rows:
        if unit == "B14":
            if value != "The actual `--recertify` driver invocation above":
                raise ValueError("COPY01 B14 command is not its actual author transaction")
            command = phases["recertify"]
        else:
            if not value.startswith("`") or not value.endswith("`"):
                raise ValueError("COPY01 unit command has unbound syntax")
            command = tuple(shlex.split(value[1:-1]))
            if len(command) != 4 or command[:3] != ("cmake", "--workflow", "--preset"):
                raise ValueError("COPY01 unit has an undeclared command form")
        commands.append((unit, command))
    return phases, tuple(commands)


def task_definition(root):
    if b"\n- Status: Accepted\n" not in design_blob(root, ADR_DOCUMENT):
        raise ValueError("COPY01 requires Accepted design authority")
    text = design_blob(root, DESIGN_DOCUMENT).decode("utf-8")
    declaration = re.search(
        r"Use transition `([^`]+)`, owner task `([^`]+)`, primary unit\s+"
        r"`([^`]+)`, finding set exactly `\{([^}]+)\}`", text,
    )
    if declaration is None or declaration.groups() != (
        "COPY01-runtime-memory", "COPY01", "B05", "B05-F006",
    ) or MEMORY_IDENTITY not in text or any(
        identifier not in text or test not in text for identifier, test, _ in ORACLE_DECLARATIONS
    ):
        raise ValueError("COPY01 accepted design binding is incomplete or foreign")
    require_current_runtime()
    executable = selected_executable(dict(os.environ))
    phases, commands = declared_commands(text, executable)
    units = tuple(unit for unit, _ in commands)
    immutable = tuple(path for path in design_membership(root) if path not in CANDIDATE_PATHS)
    authority = TaskAuthority(
        "COPY01", "B05", ("B05-F006",), DESIGN_COMMIT, DESIGN_TREE, DESIGN_DOCUMENT,
        units, phases["red"], phases["green"], phases["recertify"], commands,
        immutable, tuple((unit, CANDIDATE_PATHS) for unit in units), DESIGN_COMMIT, DESIGN_TREE,
    )
    inputs = (DESIGN_DOCUMENT, *IMPLEMENTATION_PATHS)
    oracles = tuple(
        Oracle(
            identifier, test, property_name, (MEMORY_IDENTITY,), AuthorityClass.BASELINE_ORACLE,
            Provenance(AuthorityKind.EXPLICIT_BOUND, path=DESIGN_DOCUMENT,
                       bound_reference="Exact work and storage: " + property_name),
            inputs,
        )
        for identifier, test, property_name in ORACLE_DECLARATIONS
    )
    plan = Plan(
        "COPY01-runtime-memory", "COPY01", "B05", ("B05-F006",),
        DESIGN_COMMIT, DESIGN_TREE, DESIGN_DOCUMENT,
        (MEMORY_IDENTITY,), (), (MEMORY_IDENTITY,), oracles, units, EXTERNAL_INPUTS,
        phases["red"], phases["green"], phases["recertify"], commands,
    )
    registry = TransitionRegistry((TransitionClaim(
        plan.transition_id, plan.owner_task, (MEMORY_IDENTITY,),
        tuple(oracle.oracle_id for oracle in oracles), (MEMORY_IDENTITY,),
    ),))
    return authority, plan, registry


def checked_membership(root):
    expected = design_membership(root)
    observed = tuple(
        name.decode("utf-8") for name in _run(("git", "ls-files", "-z"), cwd=root).split(b"\0") if name
    )
    if len(observed) != len(set(observed)) or tuple(sorted(observed)) != expected:
        raise ValueError("COPY01 author source has missing, extra or duplicated declared inputs")
    return expected


def record_fields(value, expected):
    if type(value) is not dict or set(value) != set(expected):
        raise ValueError("COPY01 record has unknown or missing fields")


def record_strings(value, *, empty=False):
    if type(value) is not list or (not value and not empty) or any(
        type(item) is not str or not item for item in value
    ):
        raise ValueError("COPY01 record lacks its exact string sequence")
    return tuple(value)


def decode_event(value):
    record_fields(value, (field.name for field in fields(Event)))
    scalar = (
        "kind", "result", "expected_property", "plan_digest", "prior_event_digest",
        "immutable_oracle_projection", "candidate_projection", "candidate_head",
        "candidate_tree", "event_digest",
    )
    if any(type(value[name]) is not str for name in scalar):
        raise ValueError("COPY01 event has a malformed scalar")
    for name in ("plan_digest", "immutable_oracle_projection", "candidate_projection", "event_digest"):
        if re.fullmatch(r"[a-f0-9]{64}", value[name]) is None:
            raise ValueError("COPY01 event lacks a complete digest")
    if value["prior_event_digest"] and re.fullmatch(r"[a-f0-9]{64}", value["prior_event_digest"]) is None:
        raise ValueError("COPY01 event has an invalid predecessor")
    if any(re.fullmatch(r"[a-f0-9]{40}", value[name]) is None for name in ("candidate_head", "candidate_tree")):
        raise ValueError("COPY01 event lacks its exact Git source identity")
    if type(value["oracle_results"]) is not list or not value["oracle_results"] or type(value["unit_results"]) is not list:
        raise ValueError("COPY01 event lacks complete typed result lists")
    oracles = []
    for row in value["oracle_results"]:
        record_fields(row, (field.name for field in fields(OracleResult)))
        if any(type(item) is not str or not item for item in row.values()):
            raise ValueError("COPY01 oracle result has an invalid scalar")
        oracles.append(OracleResult(
            row["oracle_id"], RunStatus(row["status"]), ObservedResult(row["observed"]), row["expected_property"],
        ))
    units = []
    for row in value["unit_results"]:
        record_fields(row, (field.name for field in fields(UnitResult)))
        if any(type(row[name]) is not str or not row[name] for name in ("unit_id", "status", "observed", "projection")):
            raise ValueError("COPY01 unit result has an invalid scalar")
        if re.fullmatch(r"[a-f0-9]{64}", row["projection"]) is None:
            raise ValueError("COPY01 unit result lacks its exact projection")
        units.append(UnitResult(
            row["unit_id"], RunStatus(row["status"]), ObservedResult(row["observed"]),
            record_strings(row["commands"]), row["projection"], record_strings(row["suppressed_identity_keys"]),
        ))
    return Event(
        **{
            **value, "kind": EventKind(value["kind"]), "result": ObservedResult(value["result"]),
            "command": record_strings(value["command"]),
            "oracle_results": tuple(oracles), "unit_results": tuple(units),
            "suppressed_identity_keys": record_strings(value["suppressed_identity_keys"], empty=True),
        },
    )


def json_pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("COPY01 record has a duplicate field")
        result[key] = value
    return result


def decode_environment(value):
    record_fields(value, (field.name for field in fields(Environment)))

    def entries(rows):
        if type(rows) is not list:
            raise ValueError("COPY01 environment lacks its input rows")
        decoded = tuple(record_strings(row) for row in rows)
        if any(len(row) != 2 for row in decoded) or len({row[0] for row in decoded}) != len(decoded):
            raise ValueError("COPY01 environment has ambiguous input bindings")
        return decoded

    if any(
        type(value[name]) is not str or re.fullmatch(r"[a-f0-9]{40}", value[name]) is None
        for name in ("current_head", "current_tree")
    ) or type(value["unit_inputs"]) is not list:
        raise ValueError("COPY01 environment lacks exact source/unit identities")
    units = []
    for row in value["unit_inputs"]:
        if type(row) is not list or len(row) != 2 or type(row[0]) is not str or not row[0]:
            raise ValueError("COPY01 environment has malformed unit inputs")
        units.append((row[0], entries(row[1])))
    if len({unit for unit, _ in units}) != len(units):
        raise ValueError("COPY01 environment has duplicate units")
    return Environment(
        entries(value["immutable_inputs"]), entries(value["red_candidate_inputs"]),
        entries(value["current_candidate_inputs"]), tuple(units),
        value["current_head"], value["current_tree"],
    )


def read_phase_record(directory, phase, attempt, previous):
    if phase not in PHASE_KINDS or re.fullmatch(r"[a-f0-9]{32}", attempt) is None:
        raise ValueError("COPY01 phase/attempt is not admitted")
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
        raise ValueError("COPY01 phase record has a borrowed or broken predecessor")
    event = decode_event(value["event"])
    if event.kind is not PHASE_KINDS[phase]:
        raise ValueError("COPY01 phase record has the wrong event kind")
    expected_logs = {"native"} | (set(RECERTIFICATION_UNITS) if phase == "recertify" else set())
    record_fields(value["logs"], expected_logs)
    for name, declared in value["logs"].items():
        record_fields(declared, ("size", "sha256"))
        if (
            type(declared["size"]) is not int or declared["size"] < 0
            or type(declared["sha256"]) is not str
            or re.fullmatch(r"[a-f0-9]{64}", declared["sha256"]) is None
        ):
            raise ValueError("COPY01 log has an invalid byte identity")
        _, observed = read_regular(directory / (phase + "-" + name + ".log"))
        if any(observed[key] != declared[key] for key in declared):
            raise ValueError("COPY01 log bytes changed after execution")
    return {
        **value, "event": event, "environment": decode_environment(value["environment"]),
    }, identity


def external_inputs(root):
    from tests.host.runtime_memory_evidence import native_compiler
    from tools.stack_bound_fatal_firmware import resolve_default_firmware

    compiler = native_compiler(root)
    tools = build_tools(root / "build/host-debug")
    runtime = tools.pop("python")
    runtime["runtime"]["paths"] = runtime["runtime"]["paths"][-3:]
    runtime["implementation_image"] = runtime_image(runtime["runtime"])
    qemu = shutil.which("qemu-system-riscv64")
    if qemu is None:
        raise ValueError("COPY01 recertification requires the configured QEMU")
    tools["qemu"] = resolve_default_firmware(qemu)
    configuration = {
        name: os.environ.get(name) for name in (
            "CLANG_NO_DEFAULT_CONFIG", "SDKROOT", "CPATH", "LIBRARY_PATH", "DEVELOPER_DIR",
        )
    }
    return compiler, {
        "@copy01/native-compiler": digest({"compiler": compiler, "configuration": configuration}),
        "@copy01/target-tools": digest(tools),
        "@copy01/python-runtime": digest(runtime),
        "@copy01/native-cache": digest(read_regular(root / "build/host-debug/CMakeCache.txt")[1]),
        "@copy01/accepted-design": digest({
            "commit": DESIGN_COMMIT, "tree": DESIGN_TREE,
            "contract": hashlib.sha256(design_blob(root, DESIGN_DOCUMENT)).hexdigest(),
            "adr": hashlib.sha256(design_blob(root, ADR_DOCUMENT)).hexdigest(),
        }),
    }


def runtime_image(metadata):
    path = Path(metadata["base_executable"])
    if sys.platform == "darwin":
        path = Path(metadata["base_prefix"]) / "Resources/Python.app/Contents/MacOS/Python"
    path = path.resolve(strict=True)
    return {"path": str(path), "identity": read_regular(path)[1]}


def capture_author_context(root):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host.evidence_authority_validation import validate_transition

    root = Path(root).resolve()
    candidate = capture_candidate(root)
    if not candidate.clean:
        raise ValueError("COPY01 requires the complete clean staged candidate")
    paths = checked_membership(root)
    source_roots = tuple(sorted({Path(path).parts[0] for path in paths}))
    if _run(("git", "ls-files", "--others", "-z", "--", *source_roots), cwd=root):
        raise ValueError("COPY01 has untracked or ignored inputs in its source namespaces")
    files = {path: read_regular(owned_artifact(root, path))[1] for path in paths}
    validate_gate_adapter(root, read_regular(root / IDENTITY_OWNER)[0].decode("utf-8"))
    authority, plan, registry = task_definition(root)
    compiler, external = external_inputs(root)
    immutable = tuple(sorted(
        [(path, digest(files[path])) for path in paths if path not in CANDIDATE_PATHS]
        + list(external.items())
    ))
    red = []
    for path in CANDIDATE_PATHS:
        data = design_blob(root, path)
        red.append((path, digest({
            "kind": "file", "mode": 0o644, "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })))
    current = tuple(sorted((path, digest(files[path])) for path in CANDIDATE_PATHS))
    environment = Environment(
        immutable, tuple(sorted(red)), current,
        tuple((unit, current) for unit in plan.recertification_units),
        candidate.source["head"], candidate.source["tree"],
    )
    inventory = current_inventory(root)
    result = validate_transition(
        inventory, {authority.task_id: authority}, registry, plan, (), environment,
    )
    if not result.ok or result.state is not TransitionState.REPLACEMENT_REGISTERED:
        raise ValueError(f"COPY01 source/declaration closure failed: {result.error}")
    if capture_candidate(root) != candidate:
        raise ValueError("COPY01 source changed while its inputs were being captured")
    return AuthorContext(root, candidate, authority, plan, registry, inventory, environment, files, compiler)


def recheck_context(context):
    if capture_author_context(context.root) != context:
        raise ValueError("COPY01 source/tool/input identities changed during execution or publication")


def journal_directory(root):
    directory = Path(root)
    for name in Path(JOURNAL_PATH).parts:
        directory /= name
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("COPY01 journal namespace is not an ordinary directory")
    return directory


@contextmanager
def author_lock(root):
    directory = Path(root)
    for name in Path(JOURNAL_PATH).parts:
        directory /= name
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("COPY01 journal namespace is linked or non-directory")
        directory.mkdir(mode=0o700, exist_ok=True)
    descriptor = os.open(directory / ".writer.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        information = os.fstat(descriptor)
        if not stat.S_ISREG(information.st_mode) or information.st_nlink != 1:
            raise ValueError("COPY01 writer lock is not a private regular file")
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield directory
    finally:
        os.close(descriptor)


def read_current(directory):
    path = Path(directory) / "current.json"
    if not path.exists() and not path.is_symlink():
        return None
    value = json.loads(read_regular(path)[0], object_pairs_hook=json_pairs)
    record_fields(value, ("version", "attempt", "count", "sha256"))
    if (
        type(value["version"]) is not int or value["version"] != 1
        or type(value["attempt"]) is not str or re.fullmatch(r"[a-f0-9]{32}", value["attempt"]) is None
        or type(value["count"]) is not int or not 0 <= value["count"] <= 3
        or type(value["sha256"]) is not str
        or value["count"] == 0 and value["sha256"] != ""
        or value["count"] != 0 and re.fullmatch(r"[a-f0-9]{64}", value["sha256"]) is None
    ):
        raise ValueError("COPY01 current journal selector is malformed")
    return value


def begin_attempt(directory):
    directory = Path(directory)
    (directory / "current.json").unlink(missing_ok=True)
    attempt = uuid.uuid4().hex
    target = directory / attempt
    target.mkdir(mode=0o700)
    publish_json(directory / "current.json", {
        "version": 1, "attempt": attempt, "count": 0, "sha256": "",
    })
    return Journal(target, attempt, (), "")


def validate_journal(context, journal):
    from tests.host.evidence_authority_validation import validate_transition
    from tests.host.runtime_memory_evidence import validate_native_log

    result = validate_transition(
        context.inventory, {context.authority.task_id: context.authority}, context.registry,
        context.plan, tuple(row["event"] for row in journal.records), context.environment,
    )
    if not result.ok:
        raise ValueError(f"COPY01 transition rejected: {result.error}")
    for phase, record in zip(PHASE_KINDS, journal.records):
        if record["event"].expected_property != ";".join(oracle.property for oracle in context.plan.replacement_oracles):
            raise ValueError("COPY01 event changed its complete declared property set")
        value = json.loads(
            read_regular(journal.directory / (phase + "-native.log"))[0], object_pairs_hook=json_pairs,
        )
        try:
            outcomes = validate_native_log(
                context.root, value, Stage.RED if phase == "red" else Stage.GREEN, context.native_compiler,
            )
        except AssertionError as error:
            raise ValueError("COPY01 recorded primitive work contradicts its result") from error
        if outcomes != record["event"].oracle_results:
            raise ValueError("COPY01 event borrows results from a different native execution")
        if phase != "recertify":
            continue
        for unit, command in context.plan.unit_commands:
            value = json.loads(
                read_regular(journal.directory / ("recertify-" + unit + ".log"))[0],
                object_pairs_hook=json_pairs,
            )
            record_fields(value, ("version", "unit", "command", "exit_code", "output"))
            if (
                type(value["version"]) is not int or value["version"] != 1
                or value["unit"] != unit or record_strings(value["command"]) != command
                or type(value["exit_code"]) is not int or value["exit_code"] != 0
                or type(value["output"]) is not str or not value["output"]
            ):
                raise ValueError("COPY01 unit log lacks its actual successful canonical execution")
    return result


def read_verified_journal(context):
    directory = journal_directory(context.root)
    current = read_current(directory)
    if current is None:
        raise ValueError("COPY01 requires a new recorded --red attempt")
    attempt_directory = owned_artifact(directory, current["attempt"])
    if not attempt_directory.is_dir():
        raise ValueError("COPY01 selected attempt directory is absent")
    records, previous = [], ""
    for phase in tuple(PHASE_KINDS)[:current["count"]]:
        record, identity = read_phase_record(attempt_directory, phase, current["attempt"], previous)
        environment = record["environment"]
        if (
            environment.immutable_inputs != context.environment.immutable_inputs
            or environment.red_candidate_inputs != context.environment.red_candidate_inputs
            or phase != "red" and environment != context.environment
        ):
            raise ValueError("COPY01 earlier source/oracle/tool inputs changed; start a new --red attempt")
        records.append(record)
        previous = identity["sha256"]
    if previous != current["sha256"] or read_current(directory) != current:
        raise ValueError("COPY01 selected record changed or does not bind its complete journal")
    journal = Journal(attempt_directory, current["attempt"], tuple(records), previous)
    return journal, validate_journal(context, journal)


def read_journal(context):
    return read_verified_journal(context)[0]


@contextmanager
def provisional_retirement(context, journal, replay):
    from tests.host.runtime_memory_evidence import validate_native_log

    global _provisional

    if _provisional is not None or len(journal.records) != 2:
        raise ValueError("COPY01 provisional retirement requires one verified Green transaction")
    validate_journal(context, journal)
    if validate_native_log(context.root, replay, Stage.GREEN, context.native_compiler) != (
        journal.records[-1]["event"].oracle_results
    ):
        raise ValueError("COPY01 author transaction lacks its complete Green replay")
    recheck_context(context)
    _provisional = (os.getpid(), context)
    try:
        yield
    finally:
        _provisional = None


def decoded_claim(source):
    names = ("DESIGN_COMMIT", "DESIGN_TREE", "DESIGN_DOCUMENT", "ORACLE_DECLARATIONS", "COMPLETED_CLAIM")
    values = {}
    for node in ast.parse(source.decode("utf-8")).body:
        if not isinstance(node, ast.Assign):
            continue
        for target in node.targets:
            if isinstance(target, ast.Name) and target.id in names:
                if target.id in values:
                    raise ValueError("COPY01 registry has duplicate source declarations")
                values[target.id] = ast.literal_eval(node.value)
    if values != {
        "DESIGN_COMMIT": DESIGN_COMMIT, "DESIGN_TREE": DESIGN_TREE,
        "DESIGN_DOCUMENT": DESIGN_DOCUMENT, "ORACLE_DECLARATIONS": ORACLE_DECLARATIONS,
        "COMPLETED_CLAIM": COMPLETED_CLAIM,
    } or COMPLETED_CLAIM != (
        "COPY01-runtime-memory", "COPY01", MEMORY_IDENTITY,
        tuple(identifier for identifier, _, _ in ORACLE_DECLARATIONS),
    ):
        raise ValueError("COPY01 registry changed its accepted scope or authority")
    return COMPLETED_CLAIM


def committed_claim(root):
    root = Path(root)
    current = decoded_claim(read_regular(root / AUTHORITY_PATH)[0])
    entry = _run(("git", "ls-tree", "-z", "HEAD", "--", AUTHORITY_PATH), cwd=root)
    if not entry:
        return None
    previous = decoded_claim(_run(("git", "show", "HEAD:" + AUTHORITY_PATH), cwd=root))
    if previous != current:
        raise ValueError("COPY01 staged claim cannot borrow committed retirement")
    return previous


def memory_identity_required(root):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host.evidence_authority_validation import validate_transition

    root = Path(root).resolve()
    if _provisional is not None and _provisional[0] == os.getpid() and _provisional[1].root == root:
        recheck_context(_provisional[1])
        return False
    if committed_claim(root) is not None:
        authority, _, registry = task_definition(root)
        result = validate_transition(
            current_inventory(root), {authority.task_id: authority}, registry, None, (), None,
        )
        if not result.ok:
            raise ValueError("COPY01 committed retirement failed ordinary authority validation")
        return MEMORY_IDENTITY in result.active_identity_keys
    selected = read_current(journal_directory(root))
    if selected is not None and selected["count"] == 3:
        context = capture_author_context(root)
        _, result = read_verified_journal(context)
        recheck_context(context)
        return MEMORY_IDENTITY in result.active_identity_keys
    return True
