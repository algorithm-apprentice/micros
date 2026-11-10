#!/usr/bin/env -S python3.15 -E -S -B

"""Record OBJ02's actual native/target Red, Green and recertification."""

import argparse
from dataclasses import asdict
import io
import os
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.common_scheduler_return_authority import (
    Journal, PHASE_KINDS, author_lock, baseline_execution, begin_attempt,
    capture_author_context, committed_claim, provisional_retirement, read_journal,
    read_phase_record, read_selection, recheck_context, validate_journal,
)
from tests.host.common_scheduler_return_evidence import (
    check_native_products, execution_log, measure_native, measure_target, run_oracles,
)
from tests.host.evidence_authority_scope import checked_source_view
from tests.host.evidence_authority_types import (
    Event, ObservedResult, RunStatus, UnitResult, plan_digest, projection_digest,
    seal_event, unit_projection_digest,
)
from tools.python_host_runtime import require_current_runtime
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import encoded, publish_json


ROOT = Path(__file__).resolve().parents[1]


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    phases = parser.add_mutually_exclusive_group(required=True)
    for phase in ("red", "green", "recertify", "native"):
        phases.add_argument("--" + phase, action="store_const", const=phase, dest="phase")
    parser.add_argument("--executable", action="append", default=[])
    arguments = parser.parse_args(argv)
    if len(arguments.executable) != (4 if arguments.phase == "native" else 0):
        parser.error("--native requires four executables; author phases accept none")
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
        raise ValueError("OBJ02 cannot skip or replace a recorded phase")
    environment, plan = context.environment, context.plan
    expected = ObservedResult.EXPECTED_FAILURE if phase == "red" else ObservedResult.PASS
    if len(results) != 2 or {result.observed for result in results} != {expected}:
        raise ValueError("OBJ02 phase lacks both actual required outcomes")
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
            raise ValueError("OBJ02 selected attempt changed during publication")
        if read_journal(context) != journal:
            raise ValueError("OBJ02 earlier event or log changed during publication")

    unchanged()
    logs = {"execution": write_log(journal.directory / (phase + "-execution.log"), replay)}
    for unit, value in unit_logs.items():
        logs[unit] = write_log(journal.directory / (phase + "-" + unit + ".log"), value)
    path = journal.directory / (phase + ".json")
    if path.exists() or path.is_symlink():
        raise ValueError("OBJ02 earlier phases are immutable; start a fresh --red attempt")
    publish_json(path, {
        "version": 1, "attempt": journal.attempt, "phase": phase, "previous": journal.sha256,
        "event": asdict(event), "environment": asdict(environment), "logs": logs,
    }, validate=unchanged)
    record, identity = read_phase_record(journal.directory, phase, journal.attempt, journal.sha256)
    updated = Journal(journal.directory, journal.attempt, (*journal.records, record), identity["sha256"], journal.selector_sha256)
    validate_journal(context, updated)
    selection = {"version": 1, "attempt": journal.attempt, "count": index + 1, "sha256": identity["sha256"]}

    def selected_inputs():
        recheck_context(context)
        observed, selector_identity = read_selection(journal.directory.parent)
        if observed not in (current, selection) or observed == current and selector_identity != journal.selector_sha256:
            raise ValueError("OBJ02 selector changed during atomic phase publication")
        previous = ""
        for earlier, row in zip(PHASE_KINDS, updated.records):
            loaded, identity = read_phase_record(journal.directory, earlier, journal.attempt, previous)
            if loaded != row:
                raise ValueError("OBJ02 earlier record or log changed during publication")
            previous = identity["sha256"]
        if previous != selection["sha256"]:
            raise ValueError("OBJ02 selected raw record chain changed during publication")

    publish_json(journal.directory.parent / "current.json", selection, validate=selected_inputs)
    return Journal(updated.directory, updated.attempt, updated.records, updated.sha256,
                   read_selection(journal.directory.parent)[1])


def retained_host_suite(root):
    import unittest
    from tools.check_object_hart_validation import retained_host_suite as previous
    from tools.run_host_tests import select_suite, test_cases

    cases = (*test_cases(previous(root)), *test_cases(select_suite(root, names=("test_common_scheduler_return",))))
    if len(cases) != len({case.id() for case in cases}):
        raise ValueError("OBJ02 retained host selection repeats a case")
    return unittest.TestSuite(cases)


def retained_checks(root):
    from tests.host import object_hart_validation_evidence as objects
    from tools.check_object_hart_validation import retained_memory_work
    from tools.run_host_tests import run_suite

    with checked_source_view(root):
        output = retained_memory_work(root)
        products = objects.measure_native(root, "green")
        objects._require_discriminating_red(
            products["baseline"], objects.work_failures(products["baseline"], "baseline"),
        )
        objects.compare_source_stages(products["baseline"], products["current"])
        if any(objects.work_failures(products["current"], "current").values()):
            raise ValueError("OBJ02 changed retained actual OBJ01 native work")
        output += "\nOBJ01 actual fixed/current work and legacy plan observations PASS\n"
    for command in (
        ("cmake", "--build", "build/host-debug", "--target", "kernel_objects_test", "scheduler_test"),
        ("ctest", "--test-dir", "build/host-debug", "--output-on-failure", "-R", "^(kernel-objects|scheduler-core)$"),
    ):
        completed = subprocess.run(
            command, cwd=root, stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=600,
        )
        output += "\n" + " ".join(command) + "\n" + completed.stdout
        if completed.returncode:
            return completed.returncode, output

    class Output(io.StringIO):
        def write(self, text):
            sys.stdout.write(text)
            sys.stdout.flush()
            return super().write(text)

    stream = Output()
    status = run_suite(retained_host_suite(root), root, stream=stream)
    return status, output + stream.getvalue()


def execute(root, phase):
    with author_lock(root) as directory:
        context = capture_author_context(root, phase)
        if committed_claim(root) is not None:
            raise ValueError("OBJ02 is committed prior authority, not a new author transaction")
        journal = begin_attempt(directory) if phase == "red" else read_journal(context)
        if len(journal.records) != tuple(PHASE_KINDS).index(phase):
            raise ValueError("OBJ02 phase order is incomplete or already recorded")
        print(f"MICROS_OBJ02_BEGIN phase={phase} attempt={journal.attempt}", flush=True)
        native = measure_native(root, phase)
        recheck_context(context)
        target = measure_target(root, "baseline" if phase == "red" else "current")
        recheck_context(context)
        reference = None if phase == "red" else baseline_execution(journal)
        results, oracle_output = run_oracles(native, target, phase, reference)
        print(oracle_output, flush=True)
        replay = execution_log(native, target, results, oracle_output, phase=phase, attempt=journal.attempt)
        recheck_context(context)
        units = {}
        if phase == "recertify":
            with provisional_retirement(context, journal, replay):
                for unit, command in sorted(context.plan.unit_commands, key=lambda row: row[0] != "B14"):
                    print(f"MICROS_OBJ02_UNIT_BEGIN {unit} {' '.join(command)}", flush=True)
                    recheck_context(context)
                    if unit == "B14":
                        exit_code, output = retained_checks(root)
                    else:
                        completed = subprocess.run(
                            command, cwd=root, stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=600,
                        )
                        exit_code, output = completed.returncode, completed.stdout
                    units[unit] = {
                        "version": 1, "unit": unit, "command": list(command),
                        "exit_code": exit_code, "output": output,
                    }
                    recheck_context(context)
                    if exit_code:
                        print(output, file=sys.stderr)
                        raise ValueError("OBJ02 affected-unit recertification failed: " + unit)
                    print("MICROS_OBJ02_UNIT_PASS " + unit, flush=True)
        updated = append_phase(context, journal, phase, replay, results, units)
        print(
            f"MICROS_OBJ02_RECORDED phase={phase} events={len(updated.records)} "
            f"tree={context.environment.current_tree} record={updated.sha256}", flush=True,
        )
        return 1 if phase == "red" else 0


def require_invocation(phase, runtime):
    from tests.host.diagnostic_history_authority import runtime_image

    selected = os.environ.get("MICROS_PYTHON")
    if not selected:
        raise ValueError("OBJ02 author execution requires explicit MICROS_PYTHON")
    command = (selected, "-E", "-S", "-B", "tools/check_common_scheduler_return.py", "--" + phase)
    allowed = {selected, str(Path(selected).resolve()), runtime_image(runtime)["path"]}
    if not sys.orig_argv or sys.orig_argv[0] not in allowed or tuple(sys.orig_argv[1:]) != command[1:]:
        raise ValueError("OBJ02 requires its exact documented interpreter and phase invocation")


def main(argv=None):
    arguments = parse_arguments(argv)
    runtime = require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("OBJ02 author execution requires the project root")
    if arguments.phase == "native":
        with checked_source_view(ROOT):
            check_native_products(ROOT, arguments.executable)
        print("MICROS_OBJ02_CURRENT_PASS products=4")
        return 0
    require_invocation(arguments.phase, runtime)
    return execute(ROOT, arguments.phase)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_OBJ02_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
