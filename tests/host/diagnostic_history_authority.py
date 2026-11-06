"""PLAT01's independently declared merged-design and execution authority."""

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
import subprocess
import sys
import uuid

from tests.host.diagnostic_history_source import (
    AUTHORITY_PATH, SOURCE_PATH, SOURCE_PROJECTION, checked_oracle_rules,
)
from tests.host.evidence_authority_source import literal_value, single_assignment
from tests.host.evidence_authority_types import (
    AuthorityClass, AuthorityKind, Environment, Event, EventKind, ObservedResult,
    Oracle, OracleResult, Plan, Provenance, RunStatus, TaskAuthority, TransitionClaim,
    TransitionRegistry, TransitionState, UnitResult, digest,
)

DESIGN_PINS = {
    "original_design_commit": "60ca0ebad264714b079e0630a5c7bb47726e1250",
    "original_design_tree": "a95816f60d6dfbf269a73449a44fc58cef2dbac3",
    "collector_design_commit": "fe9e15e811d92e0b3789cddf9b898bb962a6cb53",
    "collector_design_tree": "23e2c24d0aceef8519f8a9bd4f38b0d436275590",
    "design_commit": "9397adf2557e1046119f0239ee108f1b8b57f1a7",
    "design_tree": "e6ab2ecb56d89f7aa15f8cabd2f959f5392cecce",
    "design_document": "docs/architecture/diagnostic-history.md",
    "adr_document": "docs/adr/0093-minix-diagnostic-histories.md",
    "correction_adr_document": "docs/adr/0094-retrigger-safe-diagnostic-probe.md",
    "runtime_reference": "32f19782075ab34ff2bd6992f80540b6bfd47a4f",
    "minix_reference": "4db99f4012570a577414fe2a43697b2f239b699e",
}

SOURCE_ROLES = {
    "base_membership_sha256": "d4cd8a033398a6584d998449c4797426de898c923cd3f539f71c7068a9459006",
    "candidate_paths": (
        "arch/riscv64/uart.c",
        "include/micros/tty.h",
        "kernel/boot/boot_component_tests.c",
        "kernel/diagnostic/panic.c",
        "kernel/scheduler/scheduler_idle.c",
        "kernel/scheduler/scheduler_return.c",
        "kernel/tests/tty/tty_service_test.c",
        "kernel/tty/tty_control_core.c",
        "kernel/tty/tty_control_core.h",
        "kernel/tty/tty_control_syscall.c",
        "servers/tty/tty_control.c",
        "servers/tty/tty_control.h",
        "servers/tty/tty_service.c",
        "tests/qemu/tty_service_vfs.c",
        "tests/host/kernel_layout_source.py",
        "tests/host/test_grant_data_plane_source_layout.py",
        "tests/host/test_kernel_object_source_layout.py",
        "tests/host/test_tty_source_layout.py",
        "include/micros/diagnostic.h",
        "kernel/diagnostic/diagnostic_history_core.h",
        "kernel/diagnostic/diagnostic_history_core.c",
        "kernel/diagnostic/diagnostic_history.h",
        "kernel/diagnostic/diagnostic_history.c",
        "kernel/diagnostic/diagnostic_control_core.h",
        "kernel/diagnostic/diagnostic_control_core.c",
        "kernel/diagnostic/diagnostic_control.c",
        "kernel/diagnostic/panic_history.c",
        "servers/tty/tty_diagnostic.h",
        "servers/tty/tty_diagnostic.c",
    ),
    "immutable_additions": (
        "tests/host/diagnostic_history_test.c",
        "tests/host/diagnostic_control_test.c",
        "tests/host/tty_diagnostic_test.c",
        "tests/host/diagnostic_history_test.h",
        "tests/host/diagnostic_control_fixture.c",
        "tests/qemu/diagnostic_history_kernel.c",
        "tests/qemu/diagnostic_history_kernel.h",
        "tests/qemu/diagnostic_history_tty.c",
        "tests/qemu/diagnostic_history_tty.h",
        "tests/host/diagnostic_history_authority.py",
        "tests/host/diagnostic_history_source.py",
        "tests/host/diagnostic_history_evidence.py",
        "tests/host/test_diagnostic_history.py",
        "tools/check_diagnostic_history.py",
    ),
}

RECIPE_ADMISSION = {
    "reference_cmake_sha256": "58c75c0c83f5f19652ca46ca67ee8a2a9c289ed06f748193234bab92be9884fa",
    "native_target": "diagnostic_history_test",
    "native_case": "diagnostic-history",
    "native_sources": (
        "tests/host/diagnostic_history_test.c",
        "tests/host/diagnostic_control_test.c",
        "tests/host/tty_diagnostic_test.c",
        "tests/qemu/diagnostic_history_tty.c",
        "kernel/diagnostic/diagnostic_history_core.c",
        "kernel/diagnostic/diagnostic_control_core.c",
        "kernel/tty/tty_control_core.c",
        "servers/tty/tty_diagnostic.c",
        "servers/tty/tty_control.c",
        "servers/tty/tty_core.c",
        "${MICROS_TTY_SERVICE_CORE_SOURCES}",
        "lib/runtime/memory.c",
        "tests/work/work_counts.c",
    ),
    "native_tiers": ("test-unit-fast", "test-unit"),
    "native_definition": "MICROS_TEST_WORK_COUNTS=1",
    "kernel_sources": (
        "kernel/diagnostic/diagnostic_history_core.c",
        "kernel/diagnostic/diagnostic_history.c",
        "kernel/diagnostic/diagnostic_control_core.c",
        "kernel/diagnostic/diagnostic_control.c",
        "kernel/diagnostic/panic_history.c",
    ),
    "tty_source": "servers/tty/tty_diagnostic.c",
    "kernel_fixture": "tests/qemu/diagnostic_history_kernel.c",
    "tty_fixture": "tests/qemu/diagnostic_history_tty.c",
    "kernel_fixture_definitions": (
        "MICROS_BUILD_PANIC_TEST", "MICROS_BUILD_TTY_SERVICE_TEST",
    ),
    "tty_fixture_definition": "MICROS_BUILD_TTY_SERVICE_TEST",
    "panic_requirements": (
        ("test-qemu-bootstrap-manifest-panic", "retained"),
        ("test-qemu-bootstrap-ready-timeout", "retained"),
        ("test-qemu-ipc-syscall-panic", "retained"),
        ("test-qemu-panic", "seeded"),
        ("test-qemu-scheduler-invalid-context", "retained"),
        ("test-qemu-trap-panic", "retained"),
        ("test-qemu-vm-ready-early", "retained"),
        ("test-qemu-vm-self-fault", "retained"),
        ("test-qemu-vm-self-fault-sealed", "retained"),
    ),
    "tty_requirement": ("test-qemu-tty", "--require-diagnostic-forwarding"),
}

ORACLE_RULES = (
    (
        "plat01-history-storage",
        "tests.host.diagnostic_history_evidence.DiagnosticHistoryOracles.test_storage",
        "history=diagnostic_history;ring=10000;tail=2000;scalars=u32*3;bytes=12012",
        "ADR-0093",
        "Preserve both reference mechanisms",
        (1,),
    ),
    (
        "plat01-reader-commands",
        "tests.host.diagnostic_history_evidence.DiagnosticHistoryOracles.test_reader_commands",
        "register=3;read=4;version=1;endpoint=0x1003;address=0x40010000;size=10016;cursor=17;result=0",
        "ADR-0093",
        "One exact TTY reader",
        (6, 7, 8, 12, 13, 14, 15, 17, 18),
    ),
    (
        "plat01-forwarded-diagnostics",
        "tests.host.diagnostic_history_evidence.DiagnosticHistoryOracles.test_forwarding",
        "transfer=8193;pages=3;envelope=irq|diagnostic;output=tty;early=drained;kernel-thr-delta=0;retained-irq=deferred",
        "ADR-0094",
        "Preserve the original outcome",
        (1, 4, 5, 6, 7, 8, 9, 12, 13, 14, 15, 16, 17, 18, 19, 20),
    ),
    (
        "plat01-panic-history",
        "tests.host.diagnostic_history_evidence.DiagnosticHistoryOracles.test_panic_tail",
        "seed=2000;tail=seed[2:2000];records=hex32;core=5;reset=failure",
        "ADR-0093",
        "Panic consumption and finite proof",
        (1, 2, 3, 10, 11),
    ),
)

TRANSITION = "PLAT01-diagnostic-history"
EXTERNAL_INPUTS = (
    "@plat01/native-compiler", "@plat01/target-tools", "@plat01/python-runtime",
    "@plat01/native-cache", "@plat01/qemu-firmware", "@plat01/accepted-design",
)
RECERTIFICATION_UNITS = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
PHASE_KINDS = {
    "red": EventKind.RED_OBSERVED,
    "green": EventKind.GREEN_OBSERVED,
    "recertify": EventKind.AUTHOR_RECERTIFIED,
}
JOURNAL_PATH = "build/evidence-transitions/PLAT01"
COMPLETED_CLAIM = ("PLAT01-diagnostic-history", "PLAT01", "B01-F002")
_provisional = None


@dataclass(frozen=True)
class AuthorContext:
    root: Path
    candidate: object
    authority: TaskAuthority
    authorities: dict
    plan: Plan
    registry: TransitionRegistry
    inventory: object
    environment: Environment
    files: dict
    native_compiler: dict
    allow_absent: bool


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
        capture_output=True, check=False, timeout=30,
    )
    if completed.returncode:
        raise ValueError("PLAT01 immutable Git authority is unavailable: " + completed.stderr.decode())
    return completed.stdout


def read_regular(path):
    from tools.stack_artifact_files import read_regular as read

    return read(path)


def design_blob(root, path):
    if path.startswith("/") or "\\" in path or ".." in Path(path).parts:
        raise ValueError("PLAT01 design input leaves its fixed namespace")
    for prefix in ("original_design", "collector_design", "design"):
        if git(root, "rev-parse", DESIGN_PINS[prefix + "_commit"] + "^{tree}").decode().strip() != (
            DESIGN_PINS[prefix + "_tree"]
        ):
            raise ValueError("PLAT01 merged design commit/tree binding changed")
    return git(root, "show", DESIGN_PINS["design_commit"] + ":" + path)


def base_membership(root):
    result = {}
    for row in git(root, "ls-tree", "-r", "-z", DESIGN_PINS["design_commit"]).split(b"\0"):
        if not row:
            continue
        prefix, separator, path = row.partition(b"\t")
        parts = prefix.split()
        name = path.decode("utf-8")
        if (
            not separator or len(parts) != 3 or parts[0] not in (b"100644", b"100755")
            or parts[1] != b"blob" or name in result
            or Path(name).is_absolute() or ".." in Path(name).parts or "\\" in name
        ):
            raise ValueError("PLAT01 design contains an unsupported or duplicate source member")
        result[name] = (int(parts[0], 8) & 0o777, parts[2].decode("ascii"))
    if digest(tuple(sorted(result))) != SOURCE_ROLES["base_membership_sha256"]:
        raise ValueError("PLAT01 complete design membership changed")
    return result


def declared_scope(root):
    contract = design_blob(root, DESIGN_PINS["design_document"]).decode("utf-8")
    for key in ("adr_document", "correction_adr_document"):
        if b"\n- Status: Accepted\n" not in design_blob(root, DESIGN_PINS[key]):
            raise ValueError("PLAT01 requires both separately merged Accepted decisions")
    blocks = re.findall(r"```text\n(.*?)\n```", contract, re.S)
    selections = (
        ("tests/host/kernel_layout_source.py:data-entry:EXTERNAL", SOURCE_PROJECTION["retirement_keys"]),
        ("arch/riscv64/uart.c\ninclude/micros/tty.h", SOURCE_ROLES["candidate_paths"]),
        ("tests/host/diagnostic_history_test.c", SOURCE_ROLES["immutable_additions"]),
    )
    for prefix, expected in selections:
        actual = tuple(tuple(block.splitlines()) for block in blocks if block.startswith(prefix))
        if actual != (expected,):
            raise ValueError("PLAT01 declaration changed its exact merged-design source roles")
    normalized = " ".join(contract.split())
    if (
        "Use transition `PLAT01-diagnostic-history`, task `PLAT01`, primary unit `B01`, "
        "and finding set exactly `{B01-F002}`."
    ) not in normalized:
        raise ValueError("PLAT01 design has a foreign task or finding owner")
    if git(
        root, "diff", "--name-only", DESIGN_PINS["runtime_reference"], DESIGN_PINS["design_commit"],
        "--", "arch", "kernel", "include", "lib", "servers", "user",
    ):
        raise ValueError("PLAT01 design did not retain the fixed runtime baseline")
    return contract


def declared_commands(contract, executable):
    names = re.findall(
        r"^<MICROS_PYTHON> -E -S -B tools/check_diagnostic_history.py --(red|green|recertify)$",
        contract, re.MULTILINE,
    )
    if tuple(names) != tuple(PHASE_KINDS):
        raise ValueError("PLAT01 recording intents are not the exact design declaration")
    phases = {
        phase: (executable, "-E", "-S", "-B", "tools/check_diagnostic_history.py", "--" + phase)
        for phase in names
    }
    rows = re.findall(r"^\| (B[0-9]{2}) \| (.*?) \|$", contract, re.MULTILINE)
    if tuple(unit for unit, _ in rows) != RECERTIFICATION_UNITS:
        raise ValueError("PLAT01 recertification units are missing, extra or reordered")
    units = []
    for unit, value in rows:
        if unit == "B14":
            if value != "The actual `--recertify` driver invocation above":
                raise ValueError("PLAT01 B14 is not the actual author transaction")
            command = phases["recertify"]
        else:
            if not value.startswith("`") or not value.endswith("`"):
                raise ValueError("PLAT01 unit command has unbound syntax")
            command = tuple(shlex.split(value[1:-1]))
            if len(command) != 4 or command[:3] != ("cmake", "--workflow", "--preset"):
                raise ValueError("PLAT01 unit command left its closed workflow form")
        units.append((unit, command))
    return phases, tuple(units)


def task_definition(root):
    from tools.python_host_runtime import require_current_runtime, selected_executable
    from tests.host import runtime_memory_authority as memory

    contract = declared_scope(root)
    require_current_runtime()
    phases, unit_commands = declared_commands(contract, selected_executable(dict(os.environ)))
    candidates = SOURCE_ROLES["candidate_paths"]
    members = tuple(sorted(set(base_membership(root)) | set(candidates) | set(SOURCE_ROLES["immutable_additions"])))
    immutable = tuple(path for path in members if path not in candidates)
    pins = DESIGN_PINS
    authority = TaskAuthority(
        "PLAT01", "B01", ("B01-F002",), pins["design_commit"], pins["design_tree"], pins["design_document"],
        RECERTIFICATION_UNITS, phases["red"], phases["green"], phases["recertify"], unit_commands,
        immutable, tuple((unit, candidates) for unit in RECERTIFICATION_UNITS),
        pins["design_commit"], pins["design_tree"],
    )
    inputs = (
        pins["design_document"], pins["adr_document"], pins["correction_adr_document"],
        *SOURCE_ROLES["immutable_additions"],
    )
    rules = checked_oracle_rules(ORACLE_RULES, SOURCE_PROJECTION["retirement_keys"])
    oracles = tuple(Oracle(
        row["id"], row["test"], row["property"], row["replaces"], AuthorityClass.BASELINE_ORACLE,
        Provenance(
            AuthorityKind.ACCEPTED_ADAPTATION,
            path=pins["correction_adr_document"] if row["provenance"][1] == "ADR-0094" else pins["adr_document"],
            adr=row["provenance"][1], section=row["provenance"][2],
        ), inputs,
    ) for row in rules)
    keys = SOURCE_PROJECTION["retirement_keys"]
    plan = Plan(
        TRANSITION, "PLAT01", "B01", ("B01-F002",),
        pins["design_commit"], pins["design_tree"], pins["design_document"],
        keys, (), keys, oracles, RECERTIFICATION_UNITS, EXTERNAL_INPUTS,
        phases["red"], phases["green"], phases["recertify"], unit_commands,
    )
    prior = memory.committed_claim(root)
    if prior is None:
        raise ValueError("PLAT01 requires the already committed COPY01 retirement")
    memory_authority, _, memory_registry = memory.task_definition(root)
    claim = TransitionClaim(TRANSITION, "PLAT01", keys, tuple(row.oracle_id for row in oracles), keys)
    return authority, {authority.task_id: authority, memory_authority.task_id: memory_authority}, plan, TransitionRegistry(
        (*memory_registry.claims, claim),
    )


def current_membership(root, *, allow_absent):
    base = base_membership(root)
    candidates = set(SOURCE_ROLES["candidate_paths"])
    expected = set(base) | candidates | set(SOURCE_ROLES["immutable_additions"])
    observed = tuple(
        name.decode("utf-8") for name in git(root, "ls-files", "-z").split(b"\0") if name
    )
    absent = expected - set(observed)
    if (
        len(observed) != len(set(observed)) or set(observed) - expected
        or absent - (candidates - set(base) if allow_absent else set())
        or any((root / path).exists() or (root / path).is_symlink() for path in absent)
    ):
        raise ValueError("PLAT01 staged source membership is incomplete or contains unowned inputs")
    roots = tuple(sorted({Path(path).parts[0] for path in expected}))
    if git(root, "ls-files", "--others", "-z", "--", *roots):
        raise ValueError("PLAT01 source namespaces contain untracked or ignored inputs")
    return tuple(sorted(expected)), base, absent


def runtime_image(metadata):
    path = Path(metadata["base_executable"])
    if sys.platform == "darwin":
        path = Path(metadata["base_prefix"]) / "Resources/Python.app/Contents/MacOS/Python"
    path = path.resolve(strict=True)
    return {"path": str(path), "identity": read_regular(path)[1]}


def external_inputs(root):
    from tests.host.diagnostic_history_evidence import closed_tool_inputs, native_compiler
    from tools.stack_bound_fatal_firmware import resolve_default_firmware
    from tools.stack_bound_manifest import build_tools

    compiler = native_compiler(root)
    tools = build_tools(root / "build/host-debug")
    runtime = tools.pop("python")
    runtime["runtime"]["paths"] = runtime["runtime"]["paths"][-3:]
    runtime["implementation_image"] = runtime_image(runtime["runtime"])
    llvm = Path(tools["compiler"]["path"]).parent
    for name in ("llvm-nm", "llvm-readelf"):
        path = llvm / name
        resolved = path.resolve(strict=True)
        completed = subprocess.run(
            (str(path), "--version"), stdin=subprocess.DEVNULL, capture_output=True, check=True, timeout=30,
        )
        tools[name] = {
            "path": str(path), "resolved": str(resolved),
            "identity": read_regular(resolved)[1], "version_sha256": hashlib.sha256(completed.stdout).hexdigest(),
        }
    qemu = shutil.which("qemu-system-riscv64")
    if qemu is None:
        raise ValueError("PLAT01 requires the configured QEMU")
    firmware = resolve_default_firmware(qemu)
    configuration = {name: os.environ.get(name) for name in (
        "CLANG_NO_DEFAULT_CONFIG", "SDKROOT", "CPATH", "LIBRARY_PATH", "DEVELOPER_DIR",
    )}
    return compiler, {
        "@plat01/native-compiler": digest({"compiler": compiler, "configuration": configuration}),
        "@plat01/target-tools": digest({"tools": tools, "closure": closed_tool_inputs(root)}),
        "@plat01/python-runtime": digest(runtime),
        "@plat01/native-cache": digest(read_regular(root / "build/host-debug/CMakeCache.txt")[1]),
        "@plat01/qemu-firmware": digest(firmware),
        "@plat01/accepted-design": digest({
            "pins": DESIGN_PINS,
            "contract": hashlib.sha256(design_blob(root, DESIGN_PINS["design_document"])).hexdigest(),
            "adr": hashlib.sha256(design_blob(root, DESIGN_PINS["adr_document"])).hexdigest(),
            "correction": hashlib.sha256(design_blob(root, DESIGN_PINS["correction_adr_document"])).hexdigest(),
        }),
    }


def absence_identity(path):
    return {"kind": "absent", "path": path}


def capture_author_context(root, *, allow_absent=False):
    from tests.host.diagnostic_history_source import (
        LEGACY_SUBJECT_CHANGES, SCHEDULER_PUMP_CHANGES, validate_legacy_subject, validate_source_adapters,
        validate_retrigger_kernel_fixture, validate_retrigger_read_adapter,
    )
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host.evidence_authority_validation import validate_transition
    from tools.stack_bound_manifest import owned_artifact
    from tools.validation_context import capture_candidate

    if type(allow_absent) is not bool:
        raise ValueError("PLAT01 missing-source policy is not a phase-derived Boolean")
    root = Path(root).resolve()
    candidate = capture_candidate(root)
    if not candidate.clean:
        raise ValueError("PLAT01 requires the complete clean staged candidate")
    paths, base, absent = current_membership(root, allow_absent=allow_absent)
    files = {
        path: absence_identity(path) if path in absent else read_regular(owned_artifact(root, path))[1]
        for path in paths
    }
    validate_source_adapters(root)
    validate_retrigger_kernel_fixture(read_regular(root / "tests/qemu/diagnostic_history_kernel.c")[0].decode("utf-8"))
    if "kernel/diagnostic/diagnostic_control.c" not in absent:
        validate_retrigger_read_adapter(read_regular(root / "kernel/diagnostic/diagnostic_control.c")[0].decode("utf-8"))
    for path in (*LEGACY_SUBJECT_CHANGES, *SCHEDULER_PUMP_CHANGES):
        validate_legacy_subject(
            root, path, read_regular(root / path)[0].decode("utf-8"), required=not allow_absent,
        )
    authority, authorities, plan, registry = task_definition(root)
    compiler, external = external_inputs(root)
    candidates = SOURCE_ROLES["candidate_paths"]
    immutable = tuple(sorted(
        [(path, digest(files[path])) for path in paths if path not in candidates] + list(external.items())
    ))
    red = []
    for path in candidates:
        identity = absence_identity(path)
        if path in base:
            data = design_blob(root, path)
            identity = {
                "kind": "file", "mode": base[path][0], "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        red.append((path, digest(identity)))
    current = tuple(sorted((path, digest(files[path])) for path in candidates))
    environment = Environment(
        immutable, tuple(sorted(red)), current,
        tuple((unit, current) for unit in plan.recertification_units),
        candidate.source["head"], candidate.source["tree"],
    )
    inventory = current_inventory(root)
    result = validate_transition(inventory, authorities, registry, plan, (), environment)
    if not result.ok or result.state is not TransitionState.REPLACEMENT_REGISTERED:
        raise ValueError(f"PLAT01 source/declaration closure failed: {result.error}")
    if capture_candidate(root) != candidate:
        raise ValueError("PLAT01 source changed while its inputs were captured")
    return AuthorContext(
        root, candidate, authority, authorities, plan, registry, inventory,
        environment, files, compiler, allow_absent,
    )


def recheck_context(context):
    if capture_author_context(context.root, allow_absent=context.allow_absent) != context:
        raise ValueError("PLAT01 source/tool/input identities changed during execution or publication")


def journal_directory(root):
    directory = Path(root)
    for part in Path(JOURNAL_PATH).parts:
        directory /= part
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("PLAT01 journal namespace is linked or non-directory")
    return directory


@contextmanager
def author_lock(root):
    directory = Path(root)
    for part in Path(JOURNAL_PATH).parts:
        directory /= part
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("PLAT01 journal namespace is linked or non-directory")
        directory.mkdir(mode=0o700, exist_ok=True)
    descriptor = os.open(directory / ".writer.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ValueError("PLAT01 writer lock is not an unlinked regular file")
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield directory
    finally:
        os.close(descriptor)


def record_fields(value, expected):
    if type(value) is not dict or set(value) != set(expected):
        raise ValueError("PLAT01 record has unknown or missing fields")


def record_strings(value, *, empty=False):
    if type(value) is not list or (not value and not empty) or any(type(item) is not str or not item for item in value):
        raise ValueError("PLAT01 record lacks its exact string sequence")
    return tuple(value)


def json_pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("PLAT01 record contains a duplicate field")
        result[key] = value
    return result


def decode_event(value):
    record_fields(value, (field.name for field in fields(Event)))
    scalar = (
        "kind", "result", "expected_property", "plan_digest", "prior_event_digest",
        "immutable_oracle_projection", "candidate_projection", "candidate_head",
        "candidate_tree", "event_digest",
    )
    if any(type(value[name]) is not str for name in scalar):
        raise ValueError("PLAT01 event has a malformed scalar")
    for name in ("plan_digest", "immutable_oracle_projection", "candidate_projection", "event_digest"):
        if re.fullmatch(r"[a-f0-9]{64}", value[name]) is None:
            raise ValueError("PLAT01 event lacks a complete digest")
    if value["prior_event_digest"] and re.fullmatch(r"[a-f0-9]{64}", value["prior_event_digest"]) is None:
        raise ValueError("PLAT01 event has an invalid predecessor")
    if any(re.fullmatch(r"[a-f0-9]{40}", value[name]) is None for name in ("candidate_head", "candidate_tree")):
        raise ValueError("PLAT01 event lacks its exact Git identity")
    if type(value["oracle_results"]) is not list or type(value["unit_results"]) is not list:
        raise ValueError("PLAT01 event has untyped result lists")
    oracles = []
    for row in value["oracle_results"]:
        record_fields(row, (field.name for field in fields(OracleResult)))
        if any(type(item) is not str or not item for item in row.values()):
            raise ValueError("PLAT01 oracle result has an invalid scalar")
        oracles.append(OracleResult(
            row["oracle_id"], RunStatus(row["status"]), ObservedResult(row["observed"]), row["expected_property"],
        ))
    units = []
    for row in value["unit_results"]:
        record_fields(row, (field.name for field in fields(UnitResult)))
        if any(type(row[name]) is not str or not row[name] for name in ("unit_id", "status", "observed", "projection")):
            raise ValueError("PLAT01 unit result has an invalid scalar")
        if re.fullmatch(r"[a-f0-9]{64}", row["projection"]) is None:
            raise ValueError("PLAT01 unit result lacks its exact projection")
        units.append(UnitResult(
            row["unit_id"], RunStatus(row["status"]), ObservedResult(row["observed"]),
            record_strings(row["commands"]), row["projection"], record_strings(row["suppressed_identity_keys"]),
        ))
    return Event(**{
        **value, "kind": EventKind(value["kind"]), "result": ObservedResult(value["result"]),
        "command": record_strings(value["command"]), "oracle_results": tuple(oracles),
        "unit_results": tuple(units),
        "suppressed_identity_keys": record_strings(value["suppressed_identity_keys"], empty=True),
    })


def decode_environment(value):
    record_fields(value, (field.name for field in fields(Environment)))

    def entries(rows):
        if type(rows) is not list:
            raise ValueError("PLAT01 environment lacks its input rows")
        result = tuple(record_strings(row) for row in rows)
        if any(len(row) != 2 for row in result) or len({row[0] for row in result}) != len(result):
            raise ValueError("PLAT01 environment has ambiguous input bindings")
        return result

    if any(
        type(value[name]) is not str or re.fullmatch(r"[a-f0-9]{40}", value[name]) is None
        for name in ("current_head", "current_tree")
    ) or type(value["unit_inputs"]) is not list:
        raise ValueError("PLAT01 environment lacks exact source/unit identities")
    units = []
    for row in value["unit_inputs"]:
        if type(row) is not list or len(row) != 2 or type(row[0]) is not str or not row[0]:
            raise ValueError("PLAT01 environment has malformed unit inputs")
        units.append((row[0], entries(row[1])))
    if len({unit for unit, _ in units}) != len(units):
        raise ValueError("PLAT01 environment repeats a unit")
    return Environment(
        entries(value["immutable_inputs"]), entries(value["red_candidate_inputs"]),
        entries(value["current_candidate_inputs"]), tuple(units),
        value["current_head"], value["current_tree"],
    )


def read_selection(directory):
    path = Path(directory) / "current.json"
    if not path.exists() and not path.is_symlink():
        return None, ""
    data, identity = read_regular(path)
    value = json.loads(data, object_pairs_hook=json_pairs)
    record_fields(value, ("version", "attempt", "count", "sha256"))
    if (
        type(value["version"]) is not int or value["version"] != 1
        or type(value["attempt"]) is not str or re.fullmatch(r"[a-f0-9]{32}", value["attempt"]) is None
        or type(value["count"]) is not int or not 0 <= value["count"] <= 3
        or type(value["sha256"]) is not str
        or value["count"] == 0 and value["sha256"] != ""
        or value["count"] != 0 and re.fullmatch(r"[a-f0-9]{64}", value["sha256"]) is None
    ):
        raise ValueError("PLAT01 current journal selector is malformed")
    return value, identity["sha256"]


def read_current(directory):
    return read_selection(directory)[0]


def begin_attempt(directory):
    from tools.stack_bound_manifest import publish_json

    directory = Path(directory)
    (directory / "current.json").unlink(missing_ok=True)
    attempt = uuid.uuid4().hex
    target = directory / attempt
    target.mkdir(mode=0o700)
    publish_json(directory / "current.json", {
        "version": 1, "attempt": attempt, "count": 0, "sha256": "",
    })
    return Journal(target, attempt, (), "", read_selection(directory)[1])


def read_phase_record(directory, phase, attempt, previous):
    if phase not in PHASE_KINDS or re.fullmatch(r"[a-f0-9]{32}", attempt) is None:
        raise ValueError("PLAT01 phase/attempt is not admitted")
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
        raise ValueError("PLAT01 phase record has a borrowed or broken predecessor")
    event = decode_event(value["event"])
    if event.kind is not PHASE_KINDS[phase]:
        raise ValueError("PLAT01 phase record has the wrong event kind")
    record_fields(value["logs"], {"execution"} | (set(RECERTIFICATION_UNITS) if phase == "recertify" else set()))
    for name, declared in value["logs"].items():
        record_fields(declared, ("size", "sha256"))
        if (
            type(declared["size"]) is not int or declared["size"] < 0
            or type(declared["sha256"]) is not str
            or re.fullmatch(r"[a-f0-9]{64}", declared["sha256"]) is None
        ):
            raise ValueError("PLAT01 log has an invalid byte identity")
        _, observed = read_regular(directory / (phase + "-" + name + ".log"))
        if any(observed[key] != declared[key] for key in declared):
            raise ValueError("PLAT01 log bytes changed after execution")
    return {**value, "event": event, "environment": decode_environment(value["environment"])}, identity


def validate_journal(context, journal):
    from tests.host.diagnostic_history_evidence import validate_execution_log
    from tests.host.evidence_authority_validation import validate_transition

    result = validate_transition(
        context.inventory, context.authorities, context.registry, context.plan,
        tuple(row["event"] for row in journal.records), context.environment,
    )
    if not result.ok:
        raise ValueError(f"PLAT01 transition rejected: {result.error}")
    for phase, record in zip(PHASE_KINDS, journal.records):
        if record["event"].expected_property != ";".join(oracle.property for oracle in context.plan.replacement_oracles):
            raise ValueError("PLAT01 event changed its complete declared property set")
        replay = json.loads(
            read_regular(journal.directory / (phase + "-execution.log"))[0], object_pairs_hook=json_pairs,
        )
        outcomes = validate_execution_log(context, replay, phase=phase, attempt=journal.attempt)
        if outcomes != record["event"].oracle_results:
            raise ValueError("PLAT01 event borrows results from another execution")
        if phase != "recertify":
            continue
        for unit, command in context.plan.unit_commands:
            value = json.loads(
                read_regular(journal.directory / ("recertify-" + unit + ".log"))[0], object_pairs_hook=json_pairs,
            )
            record_fields(value, ("version", "unit", "command", "exit_code", "output"))
            if (
                type(value["version"]) is not int or value["version"] != 1
                or value["unit"] != unit or record_strings(value["command"]) != command
                or type(value["exit_code"]) is not int or value["exit_code"] != 0
                or type(value["output"]) is not str or not value["output"]
            ):
                raise ValueError("PLAT01 unit log lacks its actual canonical successful execution")
    return result


def read_verified_journal(context):
    from tools.stack_bound_manifest import owned_artifact

    directory = journal_directory(context.root)
    current, selector_sha256 = read_selection(directory)
    if current is None:
        raise ValueError("PLAT01 requires a recorded --red attempt")
    target = owned_artifact(directory, current["attempt"])
    if not target.is_dir():
        raise ValueError("PLAT01 selected attempt directory is absent")
    records, previous = [], ""
    for phase in tuple(PHASE_KINDS)[:current["count"]]:
        record, identity = read_phase_record(target, phase, current["attempt"], previous)
        environment = record["environment"]
        if (
            environment.immutable_inputs != context.environment.immutable_inputs
            or environment.red_candidate_inputs != context.environment.red_candidate_inputs
            or phase != "red" and environment != context.environment
        ):
            raise ValueError("PLAT01 earlier source/oracle/tool inputs changed; start a new --red attempt")
        records.append(record)
        previous = identity["sha256"]
    if previous != current["sha256"] or read_selection(directory) != (current, selector_sha256):
        raise ValueError("PLAT01 selector changed or does not bind its complete raw record chain")
    journal = Journal(target, current["attempt"], tuple(records), previous, selector_sha256)
    return journal, validate_journal(context, journal)


def read_journal(context):
    return read_verified_journal(context)[0]


@contextmanager
def provisional_retirement(context, journal, replay):
    from tests.host.diagnostic_history_evidence import validate_execution_log

    global _provisional
    if _provisional is not None or len(journal.records) != 2 or context.allow_absent:
        raise ValueError("PLAT01 provisional retirement requires one complete Green transaction")
    validate_journal(context, journal)
    if validate_execution_log(
        context, replay, phase="recertify", attempt=journal.attempt,
    ) != journal.records[-1]["event"].oracle_results:
        raise ValueError("PLAT01 recertification lacks complete actual Green replay")
    recheck_context(context)
    _provisional = os.getpid(), context
    try:
        yield
        recheck_context(context)
    finally:
        _provisional = None


def claim_declaration(authority_source, projection_source):
    tree = ast.parse(authority_source.decode("utf-8"))
    expected = {
        "DESIGN_PINS": DESIGN_PINS, "SOURCE_ROLES": SOURCE_ROLES,
        "RECIPE_ADMISSION": RECIPE_ADMISSION, "ORACLE_RULES": ORACLE_RULES,
        "COMPLETED_CLAIM": COMPLETED_CLAIM,
    }
    values = {name: literal_value(single_assignment(tree, name).value) for name in expected}
    projection = literal_value(single_assignment(
        ast.parse(projection_source.decode("utf-8")), "SOURCE_PROJECTION",
    ).value)
    if values != expected or projection != SOURCE_PROJECTION or COMPLETED_CLAIM != (
        "PLAT01-diagnostic-history", "PLAT01", "B01-F002",
    ):
        raise ValueError("PLAT01 completed claim changed its immutable scope or design")
    keys = projection["retirement_keys"]
    return TransitionClaim(
        TRANSITION, "PLAT01", keys, tuple(row[0] for row in ORACLE_RULES), keys,
    )


def committed_claim(root):
    root = Path(root)
    current = claim_declaration(
        read_regular(root / AUTHORITY_PATH)[0], read_regular(root / SOURCE_PATH)[0],
    )
    paths = tuple(name.decode() for name in git(
        root, "ls-tree", "-r", "--name-only", "-z", "HEAD", "--", AUTHORITY_PATH, SOURCE_PATH,
    ).split(b"\0") if name)
    if not paths:
        return None
    if set(paths) != {AUTHORITY_PATH, SOURCE_PATH}:
        raise ValueError("PLAT01 committed claim has an incomplete source owner set")
    previous = claim_declaration(
        git(root, "show", "HEAD:" + AUTHORITY_PATH), git(root, "show", "HEAD:" + SOURCE_PATH),
    )
    if previous != current:
        raise ValueError("PLAT01 staged claim cannot borrow earlier committed authority")
    return previous


def identity_required(root, key):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host.evidence_authority_validation import validate_transition

    if key not in SOURCE_PROJECTION["retirement_keys"]:
        return True
    root = Path(root).resolve()
    if any(
        not (root / path).exists() and not (root / path).is_symlink()
        for path in (AUTHORITY_PATH, SOURCE_PATH)
    ):
        return True
    if _provisional is not None and _provisional[0] == os.getpid() and _provisional[1].root == root:
        from tools.validation_context import capture_candidate

        if capture_candidate(root) != _provisional[1].candidate:
            raise ValueError("PLAT01 candidate changed inside its checked author transaction")
        return False
    if committed_claim(root) is not None:
        _, authorities, _, registry = task_definition(root)
        result = validate_transition(current_inventory(root), authorities, registry, None, (), None)
        if not result.ok:
            raise ValueError("PLAT01 committed retirement failed ordinary authority validation")
        return key in result.active_identity_keys
    selected = read_current(journal_directory(root))
    if selected is not None and selected["count"] == 3:
        context = capture_author_context(root)
        _, result = read_verified_journal(context)
        recheck_context(context)
        return key in result.active_identity_keys
    return True
