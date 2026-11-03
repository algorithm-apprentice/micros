"""Actual PLAT01 compiler, native and QEMU observations under fixed input roles."""

import argparse
from dataclasses import asdict, dataclass
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tarfile
import unittest

from tests.host.diagnostic_history_authority import (
    DESIGN_PINS, JOURNAL_PATH, ORACLE_RULES, PHASE_KINDS, git, json_pairs,
    read_regular, record_fields, record_strings,
)
from tests.host.diagnostic_history_source import project_diagnostic_cmake
from tests.host.evidence_authority_types import ObservedResult, OracleResult, RunStatus, projection_digest
from tests.host.runtime_memory_evidence import native_compiler
from tools.qemu_smoke_arguments import parse_arguments
from tools.qemu_smoke_cli import _matches_observed_run
from tools.qemu_smoke_command import build_qemu_command
from tools.qemu_smoke_execution import run_qemu
from tools.qemu_smoke_faults import _has_diagnostic_forwarding, _has_diagnostic_history
from tools.qemu_smoke_outcome import classify_smoke
from tools.qemu_smoke_records import QemuResult, SmokeOutcome
from tools.stack_artifact_build import ninja_path
from tools.stack_bound_manifest import encoded, owned_artifact, snapshot_files
from tools.stack_bound_observer import qemu_recipe_arguments
from tools.stack_bound_scope import require_image_outputs
from tools.user_elf_parse import load_elf


TARGETS = (
    ("panic", "riscv64-panic-test", "test-qemu-panic"),
    ("tty", "riscv64-tty-service-test", "test-qemu-tty"),
)
PROBE = "tests/host/diagnostic_control_fixture.c"
KERNEL_IMAGE = "kernel/micros.elf"
NATIVE_MARKER = "diagnostic histories, exact work, reader and consumer cases passed"


@dataclass(frozen=True)
class OracleObservations:
    storage: tuple
    reader: tuple
    tty: QemuResult
    panic: QemuResult


def closed_tool_inputs(root):
    from tools.validation_context import (
        DarwinTools, STANDARD_SEARCH_ROOTS, _cache, _validate_qemu_runner_family,
        _validate_qemu_runner_namespace, platform_identity, resolve_preset, validate_environment,
    )
    from tools.validation_inputs import _absent_paths, fingerprint_tree

    root = Path(root)
    environment = dict(os.environ)
    validate_environment(environment, target=True)
    profile = resolve_preset(root, "test-unit")
    cache = _cache(root, profile)
    if cache.get("MICROS_ENABLE_SANITIZERS") != "ON":
        raise ValueError("PLAT01 requires the existing enabled native sanitizer policy")
    tools = DarwinTools(root, profile, cache, environment)
    target_configuration = tools.target_preprocessor()
    tools._tool("target-linker", environment["MICROS_LD_LLD"])
    compiler = Path(environment["MICROS_CLANG"])
    for role in ("llvm-nm", "llvm-readelf"):
        tools._tool(role, compiler.parent / role)
    qemu = shutil.which("qemu-system-riscv64")
    if qemu is None:
        raise ValueError("PLAT01 requires its configured QEMU")
    tools.cache = {**cache, "MICROS_QEMU": qemu}
    qemu_inputs = tools.qemu()
    _validate_qemu_runner_family(root)
    _validate_qemu_runner_namespace(root, tools)
    tools.close_libraries(())
    roots = tuple(sorted(tools.roots))
    return {
        "platform": platform_identity(), "tools": tools.executables, "versions": tools.versions,
        "native_configuration": tools.configuration, "target_configuration": target_configuration,
        "standard_search_roots": _absent_paths(STANDARD_SEARCH_ROOTS),
        "python_spellings": tools.python_spellings, "qemu": qemu_inputs,
        "external_roots": {
            str(path): fingerprint_tree(
                path, roots, inert_link_roots=tools.python_inert_roots if path == tools.python_installation else (),
            )
            for path in roots
        },
    }


def reader_observations(output, *, stage):
    if stage not in ("red", "green") or type(output) is not str or not output.endswith("\n"):
        raise ValueError("PLAT01 reader execution is incomplete or has an unknown source stage")
    rows = tuple(json.loads(line, object_pairs_hook=json_pairs) for line in output.splitlines())
    if len(rows) != 2:
        raise ValueError("PLAT01 requires both actual decoder observations exactly once")
    for command, row in zip((3, 4), rows):
        record_fields(row, ("command", "result", "shape", "preserved"))
        if (
            type(row["command"]) is not int or row["command"] != command
            or type(row["result"]) is not int
            or type(row["shape"]) is not bool or type(row["preserved"]) is not bool
            or (row["result"], row["shape"], row["preserved"]) != (
                (-1, False, True) if stage == "red" else (0, True, False)
            )
        ):
            raise ValueError("PLAT01 decoder did not produce its exact discriminating result")
    return rows


def require_legacy_result(arguments, result):
    legacy = argparse.Namespace(**vars(arguments))
    legacy.require_diagnostic_history = None
    legacy.require_diagnostic_forwarding = False
    outcome = classify_smoke(
        output=result.output, return_code=result.return_code, timed_out=result.timed_out, markers=legacy.marker,
        require_fdt_events=legacy.require_fdt_events,
        require_fdt_reservations=legacy.require_fdt_reservations,
    )
    if not _matches_observed_run(
        legacy, result, outcome, SmokeOutcome(legacy.expect), None, None,
    ) or legacy.serial_input_trigger is not None and not result.serial_input_sent:
        raise ValueError("PLAT01 target failed complete legacy acceptance; this is not an expected Red")


def history_storage(image):
    result = []
    for symbol in image.symbols:
        if symbol.name != "diagnostic_history":
            continue
        if not 0 < symbol.section_index < len(image.sections):
            raise ValueError("PLAT01 history symbol has no defined linked storage")
        section = image.sections[symbol.section_index]
        result.append({
            "size": symbol.size, "binding": symbol.binding, "symbol_type": symbol.symbol_type,
            "section_type": section.section_type, "section_flags": section.flags,
            "aligned": symbol.value % 4 == 0,
            "contained": section.address <= symbol.value
                and symbol.value + symbol.size <= section.address + section.size,
        })
    return tuple(result)


class DiagnosticHistoryOracles(unittest.TestCase):
    observed = None

    def setUp(self):
        if not isinstance(self.observed, OracleObservations):
            raise ValueError("PLAT01 oracles require observations supplied by the actual author executor")

    def test_storage(self):
        self.assertEqual(({
            "size": 12012, "binding": 0, "symbol_type": 1, "section_type": 8,
            "section_flags": 3, "aligned": True, "contained": True,
        },), self.observed.storage)

    def test_reader_commands(self):
        self.assertEqual(tuple(
            {"command": command, "result": 0, "shape": True, "preserved": False}
            for command in (3, 4)
        ), self.observed.reader)

    def test_forwarding(self):
        self.assertTrue(_has_diagnostic_forwarding(self.observed.tty.output))

    def test_panic_tail(self):
        self.assertTrue(_has_diagnostic_history(self.observed.panic.output, "seeded"))


def run_oracles(observed, *, stage):
    if stage not in ("red", "green"):
        raise ValueError("PLAT01 oracle source stage is unknown")
    if stage == "red" and (
        observed.storage or any("MICROS_DIAGNOSTIC" in result.output for result in (observed.tty, observed.panic))
    ):
        raise ValueError("PLAT01 Red is not the exact absence of the new histories and witnesses")
    reader_observations("".join(json.dumps(row) + "\n" for row in observed.reader), stage=stage)
    cases = []
    for row in ORACLE_RULES:
        suite = unittest.defaultTestLoader.loadTestsFromName(row[1])
        if suite.countTestCases() != 1:
            raise ValueError("PLAT01 replacement oracle has ambiguous test ownership")
        case, = suite
        case.observed = observed
        cases.append(case)
    output = io.StringIO()
    result = unittest.TextTestRunner(stream=output, verbosity=2).run(unittest.TestSuite(cases))
    if result.testsRun != 4 or result.errors or result.skipped or result.expectedFailures or result.unexpectedSuccesses:
        raise ValueError("PLAT01 requires all four actual named property assertions, without errors or skips")
    failures = {case.id() for case, _ in result.failures}
    required = {row[1] for row in ORACLE_RULES} if stage == "red" else set()
    if failures != required:
        raise ValueError("PLAT01 property outcomes are nondiscriminating or incomplete:\n" + output.getvalue())
    observed_result = ObservedResult.EXPECTED_FAILURE if stage == "red" else ObservedResult.PASS
    return tuple(OracleResult(row[0], RunStatus.RUN, observed_result, row[2]) for row in ORACLE_RULES), output.getvalue()


def execution_directory(root, attempt, phase):
    if phase not in PHASE_KINDS or re.fullmatch(r"[a-f0-9]{32}", attempt) is None:
        raise ValueError("PLAT01 execution has a foreign phase or attempt")
    return owned_artifact(Path(root), JOURNAL_PATH + "/" + attempt + "/" + phase)


def baseline_members(root):
    result = {}
    for row in git(root, "ls-tree", "-r", "-z", DESIGN_PINS["runtime_reference"]).split(b"\0"):
        if not row:
            continue
        header, name = row.split(b"\t", 1)
        mode, kind, object_id = header.split()
        name = name.decode("utf-8")
        if mode not in (b"100644", b"100755") or kind != b"blob" or name in result:
            raise ValueError("PLAT01 fixed archive has an unsupported or duplicated member")
        owned_artifact(root, name)
        result[name] = (int(mode, 8) & 0o777, object_id.decode("ascii"))
    return result


def require_archive(root, source):
    if Path(source).is_symlink() or not Path(source).is_dir():
        raise ValueError("PLAT01 fixed source archive has a linked or missing root")
    members = baseline_members(root)
    observed = set()
    for path in Path(source).rglob("*"):
        if path.is_symlink():
            raise ValueError("PLAT01 fixed source archive contains a linked member")
        if path.is_dir():
            continue
        name = str(path.relative_to(source))
        if name not in members:
            raise ValueError("PLAT01 fixed source archive contains an extra member")
        data, identity = read_regular(path)
        object_id = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if (identity["mode"], object_id) != members[name]:
            raise ValueError("PLAT01 fixed archive no longer matches its verified Git blobs")
        observed.add(name)
    if observed != set(members):
        raise ValueError("PLAT01 fixed source archive is incomplete")


def fixed_archive(root):
    return git(root, "-c", "tar.umask=0022", "archive", "--format=tar", DESIGN_PINS["runtime_reference"])


def extract_baseline(root, workspace):
    archive = fixed_archive(root)
    path = workspace / "baseline.tar"
    with path.open("xb") as stream:
        stream.write(archive)
    source = workspace / "source"
    source.mkdir(mode=0o700)
    expected = baseline_members(root)
    seen = set()
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as entries:
        for member in entries:
            destination = owned_artifact(source, member.name.rstrip("/"))
            if member.isdir():
                destination.mkdir(parents=True, exist_ok=True)
                continue
            if (
                not member.isfile() or member.name not in expected or member.name in seen
                or member.mode & 0o777 != expected[member.name][0]
            ):
                raise ValueError("PLAT01 fixed Git archive has an unexpected file or mode")
            data = entries.extractfile(member).read()
            destination.parent.mkdir(parents=True, exist_ok=True)
            with destination.open("xb") as stream:
                stream.write(data)
            destination.chmod(expected[member.name][0])
            seen.add(member.name)
    require_archive(root, source)
    return source


def execute_command(command, directory):
    print("MICROS_PLAT01_EXECUTE " + shlex.join(tuple(map(str, command))), flush=True)
    completed = subprocess.run(
        command, cwd=directory, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False,
    )
    record = {
        "argv": list(map(str, command)), "cwd": str(directory),
        "exit_code": completed.returncode, "output": completed.stdout,
    }
    if completed.returncode:
        print(completed.stdout, flush=True)
        raise ValueError("PLAT01 prerequisite execution failed: " + shlex.join(record["argv"]))
    return record


def reader_command(context, source, workspace):
    return (
        context.native_compiler["path"], "-std=c17", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
        "-Wmissing-prototypes", "-Wstrict-prototypes", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
        "-I" + str(source), "-I" + str(source / "include"),
        str(context.root / PROBE), str(source / "kernel/tty/tty_control_core.c"),
        "-o", str(workspace / "decoder"),
    )


def configured_commands(context, source, workspace, *, native=False, configure=None):
    user_presets = source / "CMakeUserPresets.json"
    if user_presets.exists() or user_presets.is_symlink():
        raise ValueError("PLAT01 does not admit caller-owned CMake presets")
    directory = workspace / ("native" if native else configure)
    command = ("cmake", "--fresh", "--preset", "host-debug" if native else configure,
               "-S", str(source), "-B", str(directory), "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
    if native:
        command += ("-DCMAKE_C_COMPILER:FILEPATH=" + context.native_compiler["path"],)
    return command, (
        "cmake", "--build", str(directory), "--clean-first", "--target",
        "diagnostic_history_test" if native else "micros",
    )


def native_test_command(workspace):
    return (
        "ctest", "--test-dir", str(workspace / "native"), "--verbose",
        "--no-tests=error", "-R", "^diagnostic-history$",
    )


def build_artifacts(directory, *, configure=None):
    directory = Path(directory)
    names = {"CMakeCache.txt", "compile_commands.json", "build.ninja", "CMakeFiles/rules.ninja"}
    entries = json.loads(read_regular(directory / "compile_commands.json")[0], object_pairs_hook=json_pairs)
    if type(entries) is not list or not entries:
        raise ValueError("PLAT01 built image has no actual compiler input inventory")
    outputs = set()
    for entry in entries:
        arguments = entry.get("arguments") or shlex.split(entry["command"])
        if (
            entry["directory"] != str(directory) or arguments.count("-c") != 1
            or arguments.count("-o") != 1 or any(word.startswith(("@", "-flto")) for word in arguments)
        ):
            raise ValueError("PLAT01 compiler recipe escaped its ordinary fixed build")
        output = Path(arguments[arguments.index("-o") + 1])
        if output.is_absolute():
            output = output.relative_to(directory)
        name = str(output)
        owned_artifact(directory, name)
        if configure is None and not name.startswith("CMakeFiles/diagnostic_history_test.dir/"):
            continue
        if name in outputs:
            raise ValueError("PLAT01 build repeats a compiler output")
        outputs.add(name)
    if not outputs:
        raise ValueError("PLAT01 build has no compiled subject objects")
    names.update(outputs)
    if configure is None:
        names.add("diagnostic_history_test")
    else:
        names.update(require_image_outputs(directory, configure))
    return tuple(sorted(names))


def target_arguments(root, source, directory, target, stage):
    if read_regular(source / "CMakePresets.json")[0] != git(
        root, "show", DESIGN_PINS["runtime_reference"] + ":CMakePresets.json",
    ):
        raise ValueError("PLAT01 source changed an existing target configuration or workflow")
    recipe = project_diagnostic_cmake(read_regular(source / "CMakeLists.txt")[0].decode("utf-8"))
    if recipe.encode() != git(root, "show", DESIGN_PINS["runtime_reference"] + ":CMakeLists.txt"):
        raise ValueError("PLAT01 target recipe differs from the exact baseline plus admitted additions")
    arguments = qemu_recipe_arguments(source, directory, target)
    parsed = parse_arguments(arguments)
    expected_history = "seeded" if target == "test-qemu-panic" and stage == "green" else None
    if (
        parsed.require_diagnostic_history != expected_history
        or parsed.require_diagnostic_forwarding != (target == "test-qemu-tty" and stage == "green")
        or parsed.kernel.resolve() != directory / KERNEL_IMAGE
        or parsed.memory != ["128M"]
        or parsed.timeout != (8 if target == "test-qemu-panic" else 90)
        or Path(parsed.qemu).resolve() != Path(shutil.which("qemu-system-riscv64")).resolve()
        or parsed.trap_context_sepc_symbol or parsed.require_vm_self_fault
    ):
        raise ValueError("PLAT01 execution changed a fixed target, requirement, timeout or tool")
    return arguments, parsed


def run_target(root, source, workspace, name, configure, target, stage):
    directory = workspace / configure
    arguments, parsed = target_arguments(root, source, directory, target, stage)
    command = build_qemu_command(qemu=parsed.qemu, kernel=str(parsed.kernel), memory=parsed.memory[0])
    before = snapshot_files(directory, build_artifacts(directory, configure=configure))
    print(f"MICROS_PLAT01_TARGET_BEGIN {name} timeout={parsed.timeout:g}", flush=True)
    result = run_qemu(
        command, parsed.timeout,
        serial_input_trigger=parsed.serial_input_trigger, serial_input=parsed.serial_input,
    )
    if snapshot_files(directory, tuple(before)) != before:
        raise ValueError("PLAT01 target artifacts changed during actual execution")
    require_legacy_result(parsed, result)
    print(f"MICROS_PLAT01_TARGET_PASS {name} legacy=complete", flush=True)
    return {"runner": list(arguments), "command": command, "result": asdict(result)}, before


def execute_oracles(context, journal, phase):
    workspace = execution_directory(context.root, journal.attempt, phase)
    workspace.mkdir(mode=0o700)
    stage = "red" if phase == "red" else "green"
    source = extract_baseline(context.root, workspace) if stage == "red" else context.root
    commands = [execute_command(reader_command(context, source, workspace), context.root)]
    artifacts = snapshot_files(workspace, ("decoder",))
    commands.append(execute_command((str(workspace / "decoder"),), context.root))
    if snapshot_files(workspace, ("decoder",)) != artifacts:
        raise ValueError("PLAT01 decoder executable changed during execution")
    reader_observations(commands[-1]["output"], stage=stage)
    targets = {}
    if stage == "red":
        artifacts.update(snapshot_files(workspace, ("baseline.tar",)))
    else:
        for command in configured_commands(context, source, workspace, native=True):
            commands.append(execute_command(command, context.root))
        names = tuple("native/" + path for path in build_artifacts(workspace / "native"))
        before = snapshot_files(workspace, names)
        commands.append(execute_command(native_test_command(workspace), context.root))
        if snapshot_files(workspace, names) != before or NATIVE_MARKER not in commands[-1]["output"]:
            raise ValueError("PLAT01 Green did not execute the complete native semantic/work cases")
        artifacts.update(before)
    for name, configure, target in TARGETS:
        for command in configured_commands(context, source, workspace, configure=configure):
            commands.append(execute_command(command, source))
        targets[name], measured = run_target(context.root, source, workspace, name, configure, target, stage)
        artifacts.update((configure + "/" + path, identity) for path, identity in measured.items())
    if stage == "red":
        require_archive(context.root, source)
    observations = OracleObservations(
        history_storage(load_elf(workspace / TARGETS[0][1] / KERNEL_IMAGE)),
        reader_observations(commands[1]["output"], stage=stage),
        QemuResult(**targets["tty"]["result"]), QemuResult(**targets["panic"]["result"]),
    )
    outcomes, output = run_oracles(observations, stage=stage)
    replay = {
        "version": 1, "attempt": journal.attempt, "phase": phase, "stage": stage,
        "source": str(source), "compiler": context.native_compiler,
        "immutable_projection": projection_digest(context.environment.immutable_inputs),
        "candidate_projection": projection_digest(
            context.environment.red_candidate_inputs if stage == "red" else context.environment.current_candidate_inputs
        ),
        "commands": commands, "targets": targets,
        "artifacts": artifacts,
        "oracle_results": [asdict(row) for row in outcomes], "oracle_output": output,
    }
    validate_execution_log(context, replay, phase=phase, attempt=journal.attempt)
    return replay, outcomes


def require_command(record, command, directory):
    record_fields(record, ("argv", "cwd", "exit_code", "output"))
    if (
        record_strings(record["argv"]) != tuple(map(str, command))
        or record["cwd"] != str(directory)
        or type(record["exit_code"]) is not int or record["exit_code"] != 0
        or type(record["output"]) is not str
    ):
        raise ValueError("PLAT01 log lacks a complete successful canonical compiler/native execution")


def replay_commands(context, source, workspace, records, stage):
    expected = [
        (reader_command(context, source, workspace), context.root),
        ((str(workspace / "decoder"),), context.root),
    ]
    if stage == "green":
        expected.extend((command, context.root) for command in configured_commands(context, source, workspace, native=True))
        expected.append((native_test_command(workspace), context.root))
    for _, configure, _ in TARGETS:
        expected.extend((command, source) for command in configured_commands(context, source, workspace, configure=configure))
    if type(records) is not list or len(records) != len(expected):
        raise ValueError("PLAT01 log omits or adds an actual required execution")
    for record, (command, directory) in zip(records, expected):
        require_command(record, command, directory)
    if stage == "green" and NATIVE_MARKER not in records[4]["output"]:
        raise ValueError("PLAT01 Green lacks complete native semantics and primitive-work evidence")
    return reader_observations(records[1]["output"], stage=stage)


def replay_targets(context, source, workspace, values, stage):
    record_fields(values, ("panic", "tty"))
    result = {}
    for name, configure, target in TARGETS:
        record = values[name]
        record_fields(record, ("runner", "command", "result"))
        arguments, parsed = target_arguments(context.root, source, workspace / configure, target, stage)
        if record_strings(record["runner"]) != arguments or record_strings(record["command"]) != tuple(
            build_qemu_command(qemu=parsed.qemu, kernel=str(parsed.kernel), memory=parsed.memory[0])
        ):
            raise ValueError("PLAT01 log borrows a target command or runner contract")
        fields = record["result"]
        record_fields(fields, ("output", "return_code", "timed_out", "serial_input_sent"))
        if (
            type(fields["output"]) is not str or type(fields["return_code"]) is not int
            or fields["return_code"] != 0 or fields["timed_out"] is not False
            or type(fields["serial_input_sent"]) is not bool
            or fields["serial_input_sent"] != (parsed.serial_input_trigger is not None)
        ):
            raise ValueError("PLAT01 requires a complete successful target execution, not a timeout or partial report")
        result[name] = QemuResult(**fields)
        require_legacy_result(parsed, result[name])
    return result


def validate_execution_log(context, value, *, phase, attempt):
    record_fields(value, (
        "version", "attempt", "phase", "stage", "source", "compiler",
        "immutable_projection", "candidate_projection", "commands", "targets",
        "artifacts", "oracle_results", "oracle_output",
    ))
    workspace = execution_directory(context.root, attempt, phase)
    stage = "red" if phase == "red" else "green"
    source = workspace / "source" if stage == "red" else context.root
    if (
        type(value["version"]) is not int or value["version"] != 1
        or value["attempt"] != attempt or value["phase"] != phase or value["stage"] != stage
        or value["source"] != str(source) or value["compiler"] != context.native_compiler
        or value["immutable_projection"] != projection_digest(context.environment.immutable_inputs)
        or value["candidate_projection"] != projection_digest(
            context.environment.red_candidate_inputs if stage == "red" else context.environment.current_candidate_inputs
        )
    ):
        raise ValueError("PLAT01 execution log has a borrowed attempt, source, compiler or input projection")
    names = {"decoder"}
    if stage == "red":
        names.add("baseline.tar")
        require_archive(context.root, source)
        if read_regular(workspace / "baseline.tar")[0] != fixed_archive(context.root):
            raise ValueError("PLAT01 baseline archive is not the actual fixed Git archive")
    else:
        names.update("native/" + path for path in build_artifacts(workspace / "native"))
    for _, configure, _ in TARGETS:
        names.update(configure + "/" + path for path in build_artifacts(workspace / configure, configure=configure))
    if encoded(snapshot_files(workspace, tuple(sorted(names)))) != encoded(value["artifacts"]):
        raise ValueError("PLAT01 actual compiler/executable/image artifacts changed after execution")
    readers = replay_commands(context, source, workspace, value["commands"], stage)
    targets = replay_targets(context, source, workspace, value["targets"], stage)
    observations = OracleObservations(
        history_storage(load_elf(workspace / TARGETS[0][1] / KERNEL_IMAGE)),
        readers, targets["tty"], targets["panic"],
    )
    outcomes, _ = run_oracles(observations, stage=stage)
    if (
        value["oracle_results"] != [asdict(row) for row in outcomes]
        or type(value["oracle_output"]) is not str
        or "Ran 4 tests" not in value["oracle_output"]
        or any(row[1].rsplit(".", 1)[1] not in value["oracle_output"] for row in ORACLE_RULES)
    ):
        raise ValueError("PLAT01 actual named oracle outcomes disagree with their recorded observations")
    return outcomes
