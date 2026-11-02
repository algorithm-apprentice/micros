#!/usr/bin/env -S python3.15 -E -S -B

"""Record COPY01's fixed actual Red, Green and affected-unit recertification."""

import argparse
from dataclasses import asdict
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.python_host_runtime import require_current_runtime
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import encoded, publish_json
from tests.host.evidence_authority_types import (
    Event, ObservedResult, RunStatus, UnitResult, plan_digest, projection_digest,
    seal_event, unit_projection_digest,
)
from tests.host.runtime_memory_authority import (
    Journal, PHASE_KINDS, author_lock, begin_attempt, capture_author_context,
    committed_claim, provisional_retirement, read_current, read_journal,
    read_phase_record, recheck_context, runtime_image, validate_journal,
)
from tests.host.runtime_memory_evidence import (
    assert_geometry_work, measure_native, native_log, prove_linked_runtime, run_oracles,
)
from tests.host.work_count_types import Stage


ROOT = Path(__file__).resolve().parent.parent


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    phase = parser.add_mutually_exclusive_group(required=True)
    for name in ("red", "green", "recertify"):
        phase.add_argument("--" + name, action="store_const", const=name, dest="phase")
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
        raise ValueError("COPY01 cannot skip or replace a recorded phase")
    environment, plan = context.environment, context.plan
    commands = (plan.red_command, plan.green_command, plan.recertification_command)
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
    result_set = {item.observed for item in results}
    expected = ObservedResult.EXPECTED_FAILURE if phase == "red" else ObservedResult.PASS
    if result_set != {expected}:
        raise ValueError("COPY01 phase lacks complete actual replacement results")
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
    current = {
        "version": 1, "attempt": journal.attempt, "count": index, "sha256": journal.sha256,
    }

    def unchanged():
        recheck_context(context)
        if read_current(journal.directory.parent) != current:
            raise ValueError("COPY01 attempt selection changed during phase publication")
        if read_journal(context) != journal:
            raise ValueError("COPY01 prior event or log changed during phase publication")

    unchanged()
    logs = {"native": write_log(journal.directory / (phase + "-native.log"), replay)}
    for unit, value in unit_logs.items():
        logs[unit] = write_log(journal.directory / (phase + "-" + unit + ".log"), value)
    payload = {
        "version": 1, "attempt": journal.attempt, "phase": phase, "previous": journal.sha256,
        "event": asdict(event), "environment": asdict(environment), "logs": logs,
    }
    path = journal.directory / (phase + ".json")
    if path.exists() or path.is_symlink():
        raise ValueError("COPY01 earlier phase records are immutable; start a new --red attempt")
    publish_json(path, payload, validate=unchanged)
    record, identity = read_phase_record(journal.directory, phase, journal.attempt, journal.sha256)
    updated = Journal(journal.directory, journal.attempt, (*journal.records, record), identity["sha256"])
    validate_journal(context, updated)
    selection = {
        "version": 1, "attempt": journal.attempt, "count": index + 1, "sha256": identity["sha256"],
    }

    def current_inputs():
        recheck_context(context)
        if read_current(journal.directory.parent) not in (current, selection):
            raise ValueError("COPY01 selector changed during atomic phase publication")
        previous = ""
        for previous_phase, row in zip(PHASE_KINDS, updated.records):
            loaded, identity = read_phase_record(
                journal.directory, previous_phase, journal.attempt, previous,
            )
            if loaded != row:
                raise ValueError("COPY01 event/log bytes changed during selection publication")
            previous = identity["sha256"]
        if previous != selection["sha256"]:
            raise ValueError("COPY01 selected record byte chain changed during publication")

    publish_json(journal.directory.parent / "current.json", selection, validate=current_inputs)
    return updated


def retained_checks(root):
    names = (
        "tests.host.test_trap_syscall_source_layout",
        "tests.host.test_evidence_authority",
        "tests.host.test_runtime_memory",
        "tests.host.test_kernel_directory_layout.KernelDirectoryLayoutTest."
        "test_existing_source_gate_asts_are_still_the_frozen_modules",
    )
    suite = unittest.defaultTestLoader.loadTestsFromNames(names)
    expected = suite.countTestCases()
    output = io.StringIO()
    output.write("COPY01 linked runtime transfers: " + json.dumps(prove_linked_runtime(root), sort_keys=True) + "\n")
    result = unittest.TextTestRunner(stream=output, verbosity=2).run(suite)
    passed = expected > 0 and result.testsRun == expected and result.wasSuccessful() and not result.skipped
    return 0 if passed else 1, output.getvalue()


def execute(root, phase):
    with author_lock(root) as directory:
        context = capture_author_context(root)
        if committed_claim(root) is not None:
            raise ValueError("COPY01 is already committed prior retirement, not a new author transition")
        if phase == "red":
            journal = begin_attempt(directory)
        else:
            journal = read_journal(context)
            if len(journal.records) != tuple(PHASE_KINDS).index(phase):
                raise ValueError("COPY01 phase order is incomplete or already recorded; start a new --red attempt")
        print(f"MICROS_COPY01_BEGIN phase={phase} attempt={journal.attempt}", flush=True)
        measured = measure_native(root, Stage.RED if phase == "red" else Stage.GREEN)
        if phase != "red":
            assert_geometry_work(measured)
        results, oracle_output = run_oracles(measured)
        replay = native_log(measured, results, oracle_output)
        recheck_context(context)
        logs = {}
        if phase == "recertify":
            with provisional_retirement(context, journal, replay):
                for unit, command in context.plan.unit_commands:
                    print(f"MICROS_COPY01_UNIT_BEGIN {unit} {' '.join(command)}", flush=True)
                    recheck_context(context)
                    if unit == "B14":
                        exit_code, output = retained_checks(root)
                    else:
                        completed = subprocess.run(
                            command, cwd=root, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, check=False,
                        )
                        exit_code, output = completed.returncode, completed.stdout
                    logs[unit] = {
                        "version": 1, "unit": unit, "command": list(command),
                        "exit_code": exit_code, "output": output,
                    }
                    recheck_context(context)
                    if exit_code:
                        print(output, file=sys.stderr)
                        raise ValueError(f"COPY01 {unit} recertification failed with exit {exit_code}")
                    print(f"MICROS_COPY01_UNIT_PASS {unit}", flush=True)
        updated = append_phase(context, journal, phase, replay, results, logs)
        print(
            f"MICROS_COPY01_RECORDED phase={phase} events={len(updated.records)} "
            f"tree={context.environment.current_tree} record={updated.sha256}", flush=True,
        )
        return 1 if phase == "red" else 0


def require_invocation(phase, metadata):
    selected = os.environ.get("MICROS_PYTHON")
    if not selected:
        raise ValueError("COPY01 author execution requires explicit MICROS_PYTHON")
    command = (selected, "-E", "-S", "-B", "tools/check_runtime_memory.py", "--" + phase)
    allowed = {selected, str(Path(selected).resolve()), runtime_image(metadata)["path"]}
    if not sys.orig_argv or sys.orig_argv[0] not in allowed or tuple(sys.orig_argv[1:]) != command[1:]:
        raise ValueError("COPY01 requires its exact command and validated interpreter/runtime image")


def main(argv=None):
    arguments = parse_arguments(argv)
    require_invocation(arguments.phase, require_current_runtime())
    if Path.cwd().resolve() != ROOT:
        raise ValueError("COPY01 requires its exact documented invocation from the repository root")
    return execute(ROOT, arguments.phase)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print(f"MICROS_COPY01_FAIL {error}", file=sys.stderr)
        raise SystemExit(2) from error
