#!/usr/bin/env -S python3.15 -E -S -B

"""Record PLAT01's actual Red, Green and affected-unit recertification."""

import argparse
from dataclasses import asdict
import hashlib
import io
import os
from pathlib import Path
import subprocess
import sys
import unittest

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.check_runtime_memory import write_log
from tools.python_host_runtime import require_current_runtime
from tools.stack_bound_manifest import encoded, publish_json
from tests.host.diagnostic_history_authority import (
    Journal, PHASE_KINDS, author_lock, begin_attempt, capture_author_context,
    committed_claim, provisional_retirement, read_journal, read_phase_record,
    read_selection, recheck_context, runtime_image, validate_journal,
)
from tests.host.diagnostic_history_evidence import execute_oracles
from tests.host.evidence_authority_types import (
    Event, ObservedResult, RunStatus, UnitResult, plan_digest, projection_digest,
    seal_event, unit_projection_digest,
)


ROOT = Path(__file__).resolve().parent.parent


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    phases = parser.add_mutually_exclusive_group(required=True)
    for phase in PHASE_KINDS:
        phases.add_argument("--" + phase, action="store_const", const=phase, dest="phase")
    return parser.parse_args(argv)


def phase_event(context, journal, phase, results, unit_logs):
    index = tuple(PHASE_KINDS).index(phase)
    if len(journal.records) != index:
        raise ValueError("PLAT01 cannot skip or replace a recorded phase")
    environment, plan = context.environment, context.plan
    expected_units = set(plan.recertification_units) if phase == "recertify" else set()
    if type(unit_logs) is not dict or set(unit_logs) != expected_units:
        raise ValueError("PLAT01 requires exactly its affected-unit executions in recertification")
    expected = ObservedResult.EXPECTED_FAILURE if phase == "red" else ObservedResult.PASS
    if tuple(row.oracle_id for row in results) != tuple(row.oracle_id for row in plan.replacement_oracles) or any(
        row.status is not RunStatus.RUN or row.observed is not expected for row in results
    ):
        raise ValueError("PLAT01 phase lacks all four actual named replacement results")
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
    return seal_event(Event(
        PHASE_KINDS[phase], commands[index], expected,
        ";".join(oracle.property for oracle in plan.replacement_oracles), results, units,
        plan_digest(plan), journal.records[-1]["event"].event_digest if index else "",
        projection_digest(environment.immutable_inputs),
        projection_digest(environment.red_candidate_inputs if phase == "red" else environment.current_candidate_inputs),
        context.authority.base_head if phase == "red" else environment.current_head,
        context.authority.base_tree if phase == "red" else environment.current_tree,
        plan.planned_retirement_identity_keys if phase == "recertify" else (), "",
    ))


def append_phase(context, journal, phase, replay, results, unit_logs):
    event = phase_event(context, journal, phase, results, unit_logs)
    index = len(journal.records)
    current = {
        "version": 1, "attempt": journal.attempt, "count": index, "sha256": journal.sha256,
    }

    def unchanged():
        recheck_context(context)
        if read_selection(journal.directory.parent) != (current, journal.selector_sha256):
            raise ValueError("PLAT01 selector bytes changed during phase publication")
        if read_journal(context) != journal:
            raise ValueError("PLAT01 prior event or log bytes changed during phase publication")

    unchanged()
    logs = {"execution": write_log(journal.directory / (phase + "-execution.log"), replay)}
    for unit, value in unit_logs.items():
        logs[unit] = write_log(journal.directory / (phase + "-" + unit + ".log"), value)
    payload = {
        "version": 1, "attempt": journal.attempt, "phase": phase, "previous": journal.sha256,
        "event": asdict(event), "environment": asdict(context.environment), "logs": logs,
    }
    path = journal.directory / (phase + ".json")
    if path.exists() or path.is_symlink():
        raise ValueError("PLAT01 previous phase records are immutable; start a new --red attempt")
    publish_json(path, payload, validate=unchanged)
    record, identity = read_phase_record(journal.directory, phase, journal.attempt, journal.sha256)
    selection = {
        "version": 1, "attempt": journal.attempt, "count": index + 1, "sha256": identity["sha256"],
    }
    selector_sha256 = hashlib.sha256(encoded(selection)).hexdigest()
    updated = Journal(
        journal.directory, journal.attempt, (*journal.records, record), identity["sha256"], selector_sha256,
    )
    validate_journal(context, updated)

    def current_inputs():
        recheck_context(context)
        if read_selection(journal.directory.parent) not in (
            (current, journal.selector_sha256), (selection, selector_sha256),
        ):
            raise ValueError("PLAT01 selector bytes changed during atomic publication")
        previous = ""
        for previous_phase, row in zip(PHASE_KINDS, updated.records):
            loaded, measured = read_phase_record(journal.directory, previous_phase, journal.attempt, previous)
            if loaded != row:
                raise ValueError("PLAT01 event/log bytes changed during selection publication")
            previous = measured["sha256"]
        if previous != updated.sha256:
            raise ValueError("PLAT01 raw predecessor chain changed during publication")

    publish_json(journal.directory.parent / "current.json", selection, validate=current_inputs)
    if read_journal(context) != updated:
        raise ValueError("PLAT01 published journal differs from its fully checked byte chain")
    return updated


def retained_checks(root):
    from tests.host.diagnostic_history_source import (
        LEGACY_SUBJECT_CHANGES, SCHEDULER_PUMP_CHANGES, validate_legacy_subject, validate_source_adapters,
    )

    validate_source_adapters(root)
    for path in (*LEGACY_SUBJECT_CHANGES, *SCHEDULER_PUMP_CHANGES):
        validate_legacy_subject(root, path, (root / path).read_text(encoding="utf-8"), required=True)
    names = (
        "tests.host.test_diagnostic_history",
        "tests.host.test_evidence_authority",
        "tests.host.test_kernel_directory_layout",
        "tests.host.test_kernel_bootstrap_source_layout",
        "tests.host.test_kernel_object_source_layout",
        "tests.host.test_grant_data_plane_source_layout",
        "tests.host.test_tty_source_layout",
        "tests.host.test_scheduler_source_layout",
        "tests.host.test_qemu_runner_source_layout",
        "tests.host.test_run_qemu_smoke",
        "tests.host.test_validation_build_impact",
        "tests.host.test_validation_context",
        "tests.host.test_stack_bound_evidence",
        "tests.host.test_stack_fatal_bindings",
    )
    suite = unittest.defaultTestLoader.loadTestsFromNames(names)
    expected = suite.countTestCases()
    output = io.StringIO()
    result = unittest.TextTestRunner(stream=output, verbosity=2).run(suite)
    success = expected > 0 and result.testsRun == expected and result.wasSuccessful() and not result.skipped
    return 0 if success else 1, output.getvalue()


def recertify_units(context, journal, replay):
    logs = {}
    with provisional_retirement(context, journal, replay):
        for unit, command in context.plan.unit_commands:
            print(f"MICROS_PLAT01_UNIT_BEGIN {unit} {' '.join(command)}", flush=True)
            recheck_context(context)
            if unit == "B14":
                exit_code, output = retained_checks(context.root)
            else:
                completed = subprocess.run(
                    command, cwd=context.root, stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False,
                )
                exit_code, output = completed.returncode, completed.stdout
            logs[unit] = {
                "version": 1, "unit": unit, "command": list(command),
                "exit_code": exit_code, "output": output,
            }
            recheck_context(context)
            if exit_code:
                print(output, file=sys.stderr)
                raise ValueError(f"PLAT01 {unit} recertification failed with exit {exit_code}")
            print(f"MICROS_PLAT01_UNIT_PASS {unit}", flush=True)
    return logs


def execute(root, phase):
    with author_lock(root) as directory:
        print(f"MICROS_PLAT01_INPUTS phase={phase} closure=checking", flush=True)
        context = capture_author_context(root, allow_absent=phase == "red")
        if committed_claim(root) is not None:
            raise ValueError("PLAT01 is already committed prior retirement, not a new author transition")
        if phase == "red":
            journal = begin_attempt(directory)
        else:
            journal = read_journal(context)
            if len(journal.records) != tuple(PHASE_KINDS).index(phase):
                raise ValueError("PLAT01 phase order is incomplete or already recorded; start a new --red attempt")
        print(f"MICROS_PLAT01_BEGIN phase={phase} attempt={journal.attempt}", flush=True)
        replay, results = execute_oracles(context, journal, phase)
        recheck_context(context)
        logs = recertify_units(context, journal, replay) if phase == "recertify" else {}
        print(f"MICROS_PLAT01_PUBLISH phase={phase} chain=checking", flush=True)
        updated = append_phase(context, journal, phase, replay, results, logs)
        print(
            f"MICROS_PLAT01_RECORDED phase={phase} events={len(updated.records)} "
            f"tree={context.environment.current_tree} record={updated.sha256}", flush=True,
        )
        return 1 if phase == "red" else 0


def require_invocation(phase, metadata):
    selected = os.environ.get("MICROS_PYTHON")
    if not selected:
        raise ValueError("PLAT01 author execution requires explicit MICROS_PYTHON")
    command = (selected, "-E", "-S", "-B", "tools/check_diagnostic_history.py", "--" + phase)
    allowed = {selected, str(Path(selected).resolve()), runtime_image(metadata)["path"]}
    if not sys.orig_argv or sys.orig_argv[0] not in allowed or tuple(sys.orig_argv[1:]) != command[1:]:
        raise ValueError("PLAT01 requires its exact command and validated interpreter/runtime image")


def main(argv=None):
    arguments = parse_arguments(argv)
    require_invocation(arguments.phase, require_current_runtime())
    if Path.cwd().resolve() != ROOT:
        raise ValueError("PLAT01 requires its documented invocation from the repository root")
    return execute(ROOT, arguments.phase)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print(f"MICROS_PLAT01_FAIL {error}", file=sys.stderr)
        raise SystemExit(2) from error
