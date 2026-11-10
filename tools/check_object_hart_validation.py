#!/usr/bin/env -S python3.15 -E -S -B

"""Record OBJ01's actual native Red, Green and complete recertification."""

import argparse
import ast
from dataclasses import asdict, replace
import io
import os
from pathlib import Path
import subprocess
import sys
import unittest

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.evidence_authority_scope import checked_source_view
from tests.host.evidence_authority_source import literal_value, single_assignment
from tests.host.evidence_authority_types import (
    Event, ObservedResult, RunStatus, UnitResult, plan_digest, projection_digest,
    seal_event, unit_projection_digest,
)
from tests.host.object_hart_validation_authority import (
    Journal, PHASE_KINDS, author_lock, baseline_execution, begin_attempt,
    capture_author_context, committed_claim, provisional_retirement, read_journal,
    read_phase_record, read_selection, recheck_context, validate_journal,
)
from tests.host.object_hart_validation_evidence import (
    check_native_products, execution_log, measure_native, run_oracles,
)
from tests.host.object_hart_validation_source import measure_audit_boundaries
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
        raise ValueError("OBJ01 cannot skip or replace a recorded phase")
    environment, plan = context.environment, context.plan
    expected = ObservedResult.EXPECTED_FAILURE if phase == "red" else ObservedResult.PASS
    if len(results) != 3 or {result.observed for result in results} != {expected}:
        raise ValueError("OBJ01 phase lacks all three actual required outcomes")
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
            raise ValueError("OBJ01 selected attempt changed during publication")
        if read_journal(context) != journal:
            raise ValueError("OBJ01 previous event or log changed during publication")

    unchanged()
    logs = {"execution": write_log(journal.directory / (phase + "-execution.log"), replay)}
    for unit, value in unit_logs.items():
        logs[unit] = write_log(journal.directory / (phase + "-" + unit + ".log"), value)
    path = journal.directory / (phase + ".json")
    if path.exists() or path.is_symlink():
        raise ValueError("OBJ01 earlier phases are immutable; start a new --red attempt")
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
            raise ValueError("OBJ01 selector changed during atomic phase publication")
        previous = ""
        for earlier, row in zip(PHASE_KINDS, updated.records):
            loaded, identity = read_phase_record(journal.directory, earlier, journal.attempt, previous)
            if loaded != row:
                raise ValueError("OBJ01 previous record or log changed during publication")
            previous = identity["sha256"]
        if previous != selection["sha256"]:
            raise ValueError("OBJ01 selected raw record chain changed during publication")

    publish_json(journal.directory.parent / "current.json", selection, validate=selected_inputs)
    return Journal(updated.directory, updated.attempt, updated.records, updated.sha256, read_selection(journal.directory.parent)[1])


def retained_memory_work(root):
    from tests.host import (
        bootstrap_allocation_evidence as allocation, kernel_mapping_evidence as mapping,
        selected_ownership_evidence as ownership, selected_address_space_evidence as address,
        selected_release_handoff_evidence as release,
    )
    from tests.host.work_count_types import Stage

    output = io.StringIO()
    for name, owner, methods in (
        ("MEM01", allocation, (
            "test_bootstrap_range_work", "test_allocator_prefix_work", "test_owner_cursor_composition",
        )),
        ("MEM02", mapping, ("test_large_leaf_topology", "test_direct_table_work")),
    ):
        print("MICROS_OBJ01_RETAINED_BEGIN " + name, flush=True)
        observed = owner.measure_native(root, Stage.GREEN)
        ordinary = owner.measure_native(root, Stage.GREEN, observed=False)
        if tuple(replace(row, capture=None) for row in observed.records) != ordinary.records:
            raise ValueError("OBJ01 changed retained observed/ordinary native semantics: " + name)
        kind = allocation.BootstrapAllocationOracles if name == "MEM01" else mapping.KernelMappingOracles
        cases = []
        for method in methods:
            case = kind(method)
            case.measured = observed
            cases.append(case)
        result = unittest.TextTestRunner(stream=output, verbosity=2).run(unittest.TestSuite(cases))
        if not result.wasSuccessful() or result.testsRun != len(methods) or result.skipped:
            raise ValueError("OBJ01 retained native work failed:\n" + output.getvalue())
        print("MICROS_OBJ01_RETAINED_PASS " + name, flush=True)
    print("MICROS_OBJ01_RETAINED_BEGIN MEM03", flush=True)
    reference = ownership.measure_native(root, Stage.RED)
    current = ownership.measure_native(root, Stage.GREEN)
    if any(ownership.work_failures(current, reference).values()):
        raise ValueError("OBJ01 changed retained MEM03 work or PM object/scheduler equality")
    output.write("MEM03 actual baseline/current work and PM equality PASS\n")
    print("MICROS_OBJ01_RETAINED_PASS MEM03", flush=True)
    for name, owner, evaluate in (
        ("MEM04", address, address.work_failures), ("MEM05", release, release.replacement_failures),
    ):
        print("MICROS_OBJ01_RETAINED_BEGIN " + name, flush=True)
        if any(evaluate(owner.measure_native(root, Stage.GREEN)).values()):
            raise ValueError("OBJ01 changed retained native work: " + name)
        output.write(name + " actual current semantic/work products PASS\n")
        print("MICROS_OBJ01_RETAINED_PASS " + name, flush=True)
    return output.getvalue()


def retained_host_suite(root):
    from tests.host.selected_release_handoff_source import fixed_source
    from tools.run_host_tests import select_suite

    tree = ast.parse(fixed_source(str(root), "tools/check_selected_address_space.py"))
    owners = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "retained_checks"]
    if len(owners) != 1:
        raise ValueError("OBJ01 lacks its fixed complete retained host selection")
    names = literal_value(single_assignment(owners[0], "names").value)
    if type(names) is not tuple or len(names) != 26 or len(set(names)) != 26:
        raise ValueError("OBJ01 retained host selection is incomplete")
    covered = ("test_panic_trace.PanicTraceAuthorityTest", "test_panic_trace.PanicTraceExecutionScopeTest")
    if not set(covered) <= set(names):
        raise ValueError("OBJ01 lost its fixed partial panic selection")
    return select_suite(root, names=(
        *(name for name in names if name not in covered),
        "test_kernel_object_source_layout", "test_panic_trace", "test_stack_source_projection",
        "test_selected_release_handoff", "test_object_hart_validation",
    ))


def retained_checks(root):
    from tools.run_host_tests import run_suite

    with checked_source_view(root):
        output = retained_memory_work(root)
    for command in (
        ("cmake", "--build", "build/host-debug", "--target", "kernel_objects_test", "scheduler_test"),
        ("ctest", "--test-dir", "build/host-debug", "--output-on-failure", "-R", "^(kernel-objects|scheduler-core)$"),
    ):
        completed = subprocess.run(
            command, cwd=root, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, timeout=600,
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
    suite = retained_host_suite(root)
    status = run_suite(suite, root, stream=stream)
    return status, output + stream.getvalue()


def execute(root, phase):
    with author_lock(root) as directory:
        context = capture_author_context(root, phase)
        if committed_claim(root) is not None:
            raise ValueError("OBJ01 is committed prior authority, not a new author transaction")
        journal = begin_attempt(directory) if phase == "red" else read_journal(context)
        if len(journal.records) != tuple(PHASE_KINDS).index(phase):
            raise ValueError("OBJ01 phase order is incomplete or already recorded")
        print(f"MICROS_OBJ01_BEGIN phase={phase} attempt={journal.attempt}", flush=True)
        native = measure_native(root, phase)
        recheck_context(context)
        stage = "baseline" if phase == "red" else "current"
        audits = measure_audit_boundaries(root, stage)
        reference = None if phase == "red" else baseline_execution(journal)
        results, oracle_output = run_oracles(root, native, audits, phase, reference)
        print(oracle_output, flush=True)
        replay = execution_log(native, audits, results, oracle_output, phase=phase, attempt=journal.attempt)
        recheck_context(context)
        units = {}
        if phase == "recertify":
            with provisional_retirement(context, journal, replay):
                for unit, command in sorted(context.plan.unit_commands, key=lambda row: row[0] != "B14"):
                    print(f"MICROS_OBJ01_UNIT_BEGIN {unit} {' '.join(command)}", flush=True)
                    recheck_context(context)
                    if unit == "B14":
                        exit_code, output = retained_checks(root)
                    else:
                        completed = subprocess.run(
                            command, cwd=root, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=600,
                        )
                        exit_code, output = completed.returncode, completed.stdout
                    units[unit] = {"version": 1, "unit": unit, "command": list(command), "exit_code": exit_code, "output": output}
                    recheck_context(context)
                    if exit_code:
                        print(output, file=sys.stderr)
                        raise ValueError("OBJ01 affected-unit recertification failed: " + unit)
                    print(f"MICROS_OBJ01_UNIT_PASS {unit}", flush=True)
        updated = append_phase(context, journal, phase, replay, results, units)
        print(
            f"MICROS_OBJ01_RECORDED phase={phase} events={len(updated.records)} "
            f"tree={context.environment.current_tree} record={updated.sha256}", flush=True,
        )
        return 1 if phase == "red" else 0


def require_invocation(phase, runtime):
    from tests.host.diagnostic_history_authority import runtime_image

    selected = os.environ.get("MICROS_PYTHON")
    if not selected:
        raise ValueError("OBJ01 author execution requires explicit MICROS_PYTHON")
    command = (selected, "-E", "-S", "-B", "tools/check_object_hart_validation.py", "--" + phase)
    allowed = {selected, str(Path(selected).resolve()), runtime_image(runtime)["path"]}
    if not sys.orig_argv or sys.orig_argv[0] not in allowed or tuple(sys.orig_argv[1:]) != command[1:]:
        raise ValueError("OBJ01 requires its exact documented interpreter and phase invocation")


def main(argv=None):
    arguments = parse_arguments(argv)
    runtime = require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("OBJ01 author execution requires the project root")
    if arguments.phase == "native":
        with checked_source_view(ROOT):
            check_native_products(ROOT, arguments.executable)
        print("MICROS_OBJ01_CURRENT_PASS cases=171 products=6")
        return 0
    require_invocation(arguments.phase, runtime)
    return execute(ROOT, arguments.phase)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_OBJ01_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
