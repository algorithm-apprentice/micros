#!/usr/bin/env -S python3.15 -E -S -B

"""Record MEM03's actual native Red, Green and affected-unit recertification."""

import argparse
from dataclasses import asdict
import io
import os
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.evidence_authority_types import (
    Event, ObservedResult, RunStatus, UnitResult, plan_digest, projection_digest,
    seal_event, unit_projection_digest,
)
from tests.host.selected_ownership_authority import (
    Journal, PHASE_KINDS, author_lock, baseline_execution, begin_attempt,
    capture_author_context, committed_claim, provisional_retirement, read_journal,
    read_phase_record, read_selection, recheck_context, validate_journal,
)
from tests.host.selected_ownership_evidence import (
    check_current_fixture_syntax, check_native_products, execution_log,
    measure_complete_helpers, measure_native, run_oracles,
)
from tests.host.selected_ownership_source import measure_audit_boundaries
from tests.host.work_count_types import Stage
from tools.python_host_runtime import require_current_runtime
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import encoded, publish_json


ROOT = Path(__file__).resolve().parents[1]


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    phases = parser.add_mutually_exclusive_group(required=True)
    for phase in PHASE_KINDS:
        phases.add_argument("--" + phase, action="store_const", const=phase, dest="phase")
    phases.add_argument("--native", action="store_const", const="native", dest="phase")
    parser.add_argument("--executable", action="append", default=[])
    arguments = parser.parse_args(argv)
    if len(arguments.executable) != (6 if arguments.phase == "native" else 0):
        parser.error("--native requires six executables; author phases accept none")
    return arguments


def write_log(path, value):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, "wb") as stream:
        stream.write(encoded(value))
        stream.flush()
        os.fsync(stream.fileno())
    identity = read_regular(path)[1]
    return {name: identity[name] for name in ("size", "sha256")}


def append_phase(context, journal, phase, replay, results, unit_logs):
    index = tuple(PHASE_KINDS).index(phase)
    if len(journal.records) != index:
        raise ValueError("MEM03 cannot skip or replace a recorded phase")
    environment, plan = context.environment, context.plan
    expected = ObservedResult.EXPECTED_FAILURE if phase == "red" else ObservedResult.PASS
    if len(results) != 3 or {result.observed for result in results} != {expected}:
        raise ValueError("MEM03 phase lacks all three actual required oracle outcomes")
    units = tuple(
        UnitResult(
            unit, RunStatus.RUN,
            ObservedResult.PASS if unit_logs[unit]["exit_code"] == 0 else ObservedResult.FAIL,
            tuple(unit_logs[unit]["command"]),
            unit_projection_digest(dict(environment.unit_inputs)[unit], plan.planned_retirement_identity_keys),
            plan.planned_retirement_identity_keys,
        )
        for unit in plan.recertification_units
    ) if phase == "recertify" else ()
    commands = (plan.red_command, plan.green_command, plan.recertification_command)
    event = seal_event(Event(
        PHASE_KINDS[phase], commands[index], expected,
        ";".join(oracle.property for oracle in plan.replacement_oracles), results, units,
        plan_digest(plan), journal.records[-1]["event"].event_digest if index else "",
        projection_digest(environment.immutable_inputs),
        projection_digest(environment.red_candidate_inputs if phase == "red" else environment.current_candidate_inputs),
        context.authority.base_head if phase == "red" else environment.current_head,
        context.authority.base_tree if phase == "red" else environment.current_tree,
        plan.planned_retirement_identity_keys if phase == "recertify" else (), "",
    ))
    current = {"version": 1, "attempt": journal.attempt, "count": index, "sha256": journal.sha256}

    def unchanged():
        recheck_context(context)
        if read_selection(journal.directory.parent) != (current, journal.selector_sha256):
            raise ValueError("MEM03 selected attempt changed during phase publication")
        if read_journal(context) != journal:
            raise ValueError("MEM03 prior event or log changed during phase publication")

    unchanged()
    logs = {"execution": write_log(journal.directory / (phase + "-execution.log"), replay)}
    for unit, value in unit_logs.items():
        logs[unit] = write_log(journal.directory / (phase + "-" + unit + ".log"), value)
    path = journal.directory / (phase + ".json")
    if path.exists() or path.is_symlink():
        raise ValueError("MEM03 earlier phases are immutable; start a new --red attempt")
    publish_json(path, {
        "version": 1, "attempt": journal.attempt, "phase": phase, "previous": journal.sha256,
        "event": asdict(event), "environment": asdict(environment), "logs": logs,
    }, validate=unchanged)
    record, identity = read_phase_record(journal.directory, phase, journal.attempt, journal.sha256)
    updated = Journal(
        journal.directory, journal.attempt, (*journal.records, record), identity["sha256"], journal.selector_sha256,
    )
    validate_journal(context, updated)
    selection = {"version": 1, "attempt": journal.attempt, "count": index + 1, "sha256": identity["sha256"]}

    def selected_inputs():
        recheck_context(context)
        observed, selector_identity = read_selection(journal.directory.parent)
        if observed not in (current, selection) or observed == current and selector_identity != journal.selector_sha256:
            raise ValueError("MEM03 selector changed during atomic phase publication")
        previous = ""
        for previous_phase, row in zip(PHASE_KINDS, updated.records):
            loaded, identity = read_phase_record(journal.directory, previous_phase, journal.attempt, previous)
            if loaded != row:
                raise ValueError("MEM03 prior event/log bytes changed during selection publication")
            previous = identity["sha256"]
        if previous != selection["sha256"]:
            raise ValueError("MEM03 selected record byte chain changed during publication")

    publish_json(journal.directory.parent / "current.json", selection, validate=selected_inputs)
    return Journal(
        updated.directory, updated.attempt, updated.records, updated.sha256,
        read_selection(journal.directory.parent)[1],
    )


def retained_checks(root):
    from tools.run_host_tests import run_suite, select_suite

    names = (
        "tests.host.test_selected_ownership",
        "tests.host.test_bootstrap_allocation",
        "tests.host.test_kernel_mapping",
        "tests.host.test_frame_ownership_source_layout",
        "tests.host.test_frame_validation_hot_path",
        "tests.host.test_kernel_address_space_source_layout",
        "tests.host.test_kernel_bootstrap_source_layout",
        "tests.host.test_kernel_directory_layout",
        "tests.host.test_kernel_vm_handoff_source_layout",
        "tests.host.test_kernel_pm_control_source_layout",
        "tests.host.test_python_host_runtime.PythonHostRuntimeTests.test_python_process_construction_has_one_reviewed_owner_inventory",
        "tests.host.test_evidence_authority",
        "tests.host.test_evidence_authority_scope",
        "tests.host.test_host_test_runner",
        "tests.host.test_runtime_memory.RuntimeMemoryAuthorityTest",
        "tests.host.test_diagnostic_history.DiagnosticHistoryAuthorityTest",
        "tests.host.test_terminal_failure.TerminalAuthorityTest",
        "tests.host.test_panic_trace.PanicTraceAuthorityTest",
        "tests.host.test_panic_trace.PanicTraceExecutionScopeTest",
    )
    class Output(io.StringIO):
        def write(self, text):
            sys.stdout.write(text)
            sys.stdout.flush()
            return super().write(text)

    suite = select_suite(root, names=tuple(name.removeprefix("tests.host.") for name in names))
    output = Output()
    status = run_suite(suite, root, stream=output)
    return status, output.getvalue()


def execute(root, phase):
    with author_lock(root) as directory:
        context = capture_author_context(root, phase)
        if committed_claim(root) is not None:
            raise ValueError("MEM03 is already committed prior authority, not a new author transaction")
        journal = begin_attempt(directory) if phase == "red" else read_journal(context)
        if len(journal.records) != tuple(PHASE_KINDS).index(phase):
            raise ValueError("MEM03 phase order is incomplete or already recorded")
        print(f"MICROS_MEM03_BEGIN phase={phase} attempt={journal.attempt}", flush=True)
        check_current_fixture_syntax(root)
        stage = Stage.RED if phase == "red" else Stage.GREEN
        native = measure_native(root, stage)
        print("MICROS_MEM03_NATIVE_CAPTURED cases=99 snapshots=495 ordinary=99", flush=True)
        audits = measure_audit_boundaries(root, stage)
        helpers = measure_complete_helpers(root, stage)
        print("MICROS_MEM03_HELPER_TOKENS_CAPTURED count=2", flush=True)
        reference = None if phase == "red" else baseline_execution(journal)
        results, oracle_output = run_oracles(root, native, audits, helpers, reference)
        print(oracle_output, flush=True)
        replay = execution_log(native, audits, helpers, results, oracle_output, phase=phase, attempt=journal.attempt)
        recheck_context(context)
        units = {}
        if phase == "recertify":
            with provisional_retirement(context, journal, replay):
                for unit, command in context.plan.unit_commands:
                    print(f"MICROS_MEM03_UNIT_BEGIN {unit} {' '.join(command)}", flush=True)
                    recheck_context(context)
                    if unit == "B14":
                        exit_code, output = retained_checks(root)
                    else:
                        completed = subprocess.run(
                            command, cwd=root, stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False,
                        )
                        exit_code, output = completed.returncode, completed.stdout
                    units[unit] = {
                        "version": 1, "unit": unit, "command": list(command),
                        "exit_code": exit_code, "output": output,
                    }
                    recheck_context(context)
                    if exit_code:
                        print(output, file=sys.stderr)
                        raise ValueError("MEM03 affected-unit recertification failed: " + unit)
                    print(f"MICROS_MEM03_UNIT_PASS {unit}", flush=True)
        updated = append_phase(context, journal, phase, replay, results, units)
        print(
            f"MICROS_MEM03_RECORDED phase={phase} events={len(updated.records)} "
            f"tree={context.environment.current_tree} record={updated.sha256}", flush=True,
        )
        return 1 if phase == "red" else 0


def require_invocation(phase, runtime):
    from tests.host.diagnostic_history_authority import runtime_image

    selected = os.environ.get("MICROS_PYTHON")
    if not selected:
        raise ValueError("MEM03 author execution requires explicit MICROS_PYTHON")
    command = (selected, "-E", "-S", "-B", "tools/check_selected_ownership.py", "--" + phase)
    allowed = {selected, str(Path(selected).resolve()), runtime_image(runtime)["path"]}
    if not sys.orig_argv or sys.orig_argv[0] not in allowed or tuple(sys.orig_argv[1:]) != command[1:]:
        raise ValueError("MEM03 requires its exact documented interpreter and phase invocation")


def main(argv=None):
    arguments = parse_arguments(argv)
    runtime = require_current_runtime()
    if arguments.phase == "native":
        if Path.cwd().resolve() != ROOT:
            raise ValueError("MEM03 native orchestration requires its declared source root")
        from tests.host.evidence_authority_scope import checked_source_view

        with checked_source_view(ROOT):
            check_native_products(ROOT, arguments.executable)
        print("MICROS_MEM03_CURRENT_PASS cases=99 snapshots=495 guards=58 ordinary=99")
        return 0
    require_invocation(arguments.phase, runtime)
    if Path.cwd().resolve() != ROOT:
        raise ValueError("MEM03 requires its exact documented invocation from the repository root")
    return execute(ROOT, arguments.phase)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_MEM03_FAIL " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
