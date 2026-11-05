#!/usr/bin/env -S python3.15 -E -S -B

"""Record MEM02's actual native/target Red, Green and affected-unit recertification."""

import argparse
from dataclasses import asdict
import io
import os
from pathlib import Path
import subprocess
import sys
import unittest

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.evidence_authority_types import (
    Event, ObservedResult, RunStatus, UnitResult, plan_digest, projection_digest,
    seal_event, unit_projection_digest,
)
from tests.host.kernel_mapping_authority import (
    Journal, PHASE_KINDS, author_lock, begin_attempt, capture_author_context,
    committed_claim, provisional_retirement, read_journal, read_phase_record,
    read_selection, recheck_context, validate_journal,
)
from tests.host.kernel_mapping_evidence import execution_log, measure_native, measure_target, run_oracles
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
    return parser.parse_args(argv)


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
        raise ValueError("MEM02 cannot skip or replace a recorded phase")
    environment, plan = context.environment, context.plan
    expected = ObservedResult.EXPECTED_FAILURE if phase == "red" else ObservedResult.PASS
    if {result.observed for result in results} != {expected}:
        raise ValueError("MEM02 phase lacks all three actual required oracle outcomes")
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
            raise ValueError("MEM02 selected attempt changed during phase publication")
        if read_journal(context) != journal:
            raise ValueError("MEM02 prior event or log changed during phase publication")

    unchanged()
    logs = {"execution": write_log(journal.directory / (phase + "-execution.log"), replay)}
    for unit, value in unit_logs.items():
        logs[unit] = write_log(journal.directory / (phase + "-" + unit + ".log"), value)
    path = journal.directory / (phase + ".json")
    if path.exists() or path.is_symlink():
        raise ValueError("MEM02 earlier phases are immutable; start a new --red attempt")
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
            raise ValueError("MEM02 selector changed during atomic phase publication")
        previous = ""
        for previous_phase, row in zip(PHASE_KINDS, updated.records):
            loaded, identity = read_phase_record(journal.directory, previous_phase, journal.attempt, previous)
            if loaded != row:
                raise ValueError("MEM02 prior event/log bytes changed during selection publication")
            previous = identity["sha256"]
        if previous != selection["sha256"]:
            raise ValueError("MEM02 selected record byte chain changed during publication")

    publish_json(journal.directory.parent / "current.json", selection, validate=selected_inputs)
    return Journal(
        updated.directory, updated.attempt, updated.records, updated.sha256,
        read_selection(journal.directory.parent)[1],
    )


def retained_checks(root):
    names = (
        "tests.host.test_kernel_mapping",
        "tests.host.test_bootstrap_allocation",
        "tests.host.test_kernel_address_space_source_layout",
        "tests.host.test_kernel_directory_layout",
        "tests.host.test_python_host_runtime.PythonHostRuntimeTests.test_python_process_construction_has_one_reviewed_owner_inventory",
        "tests.host.test_evidence_authority",
        "tests.host.test_runtime_memory.RuntimeMemoryAuthorityTest",
        "tests.host.test_diagnostic_history.DiagnosticHistoryAuthorityTest",
        "tests.host.test_terminal_failure.TerminalAuthorityTest",
        "tests.host.test_panic_trace.PanicTraceAuthorityTest",
        "tests.host.test_panic_trace.PanicTraceExecutionScopeTest",
    )
    suite = unittest.defaultTestLoader.loadTestsFromNames(names)
    expected = suite.countTestCases()
    output = io.StringIO()
    result = unittest.TextTestRunner(stream=output, verbosity=2).run(suite)
    passed = expected > 0 and result.testsRun == expected and result.wasSuccessful() and not result.skipped
    return 0 if passed else 1, output.getvalue()


def execute(root, phase):
    with author_lock(root) as directory:
        context = capture_author_context(root, phase)
        if committed_claim(root) is not None:
            raise ValueError("MEM02 is already committed prior authority, not a new author transaction")
        journal = begin_attempt(directory) if phase == "red" else read_journal(context)
        if len(journal.records) != tuple(PHASE_KINDS).index(phase):
            raise ValueError("MEM02 phase order is incomplete or already recorded")
        print(f"MICROS_MEM02_BEGIN phase={phase} attempt={journal.attempt}", flush=True)
        stage = Stage.RED if phase == "red" else Stage.GREEN
        native = measure_native(root, stage)
        print("MICROS_MEM02_NATIVE_CAPTURED cases=17 snapshots=85", flush=True)
        target = measure_target(root, stage)
        print("MICROS_MEM02_TARGET_CAPTURED memories=128M,256M", flush=True)
        results, oracle_output = run_oracles(native, target)
        print(oracle_output, flush=True)
        replay = execution_log(native, target, results, oracle_output, phase=phase, attempt=journal.attempt)
        recheck_context(context)
        units = {}
        if phase == "recertify":
            with provisional_retirement(context, journal, replay):
                for unit, command in context.plan.unit_commands:
                    print(f"MICROS_MEM02_UNIT_BEGIN {unit} {' '.join(command)}", flush=True)
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
                        raise ValueError("MEM02 affected-unit recertification failed: " + unit)
                    print(f"MICROS_MEM02_UNIT_PASS {unit}", flush=True)
        updated = append_phase(context, journal, phase, replay, results, units)
        print(
            f"MICROS_MEM02_RECORDED phase={phase} events={len(updated.records)} "
            f"tree={context.environment.current_tree} record={updated.sha256}", flush=True,
        )
        return 1 if phase == "red" else 0


def require_invocation(phase, runtime):
    from tests.host.diagnostic_history_authority import runtime_image

    selected = os.environ.get("MICROS_PYTHON")
    if not selected:
        raise ValueError("MEM02 author execution requires explicit MICROS_PYTHON")
    command = (selected, "-E", "-S", "-B", "tools/check_kernel_mapping.py", "--" + phase)
    allowed = {selected, str(Path(selected).resolve()), runtime_image(runtime)["path"]}
    if not sys.orig_argv or sys.orig_argv[0] not in allowed or tuple(sys.orig_argv[1:]) != command[1:]:
        raise ValueError("MEM02 requires its exact documented interpreter and phase invocation")


def main(argv=None):
    arguments = parse_arguments(argv)
    require_invocation(arguments.phase, require_current_runtime())
    if Path.cwd().resolve() != ROOT:
        raise ValueError("MEM02 requires its exact documented invocation from the repository root")
    return execute(ROOT, arguments.phase)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_MEM02_FAIL " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
