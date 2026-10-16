#!/usr/bin/env python3

import argparse
import enum
import os
import re
import selectors
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


sys.path.insert(0, str(Path(__file__).resolve().parent.parent))


from tools.qemu_smoke_records import PANIC_MARKER
from tools.qemu_smoke_records import FAILURE_MARKER
from tools.qemu_smoke_records import TRAP_READY_MARKER
from tools.qemu_smoke_records import TRAP_TEST_MARKER
from tools.qemu_smoke_records import TRAP_TEST_PASS
from tools.qemu_smoke_records import TIMER_TEST_MARKER
from tools.qemu_smoke_records import TIMER_TEST_PASS
from tools.qemu_smoke_records import FRAME_ALLOCATOR_READY_MARKER
from tools.qemu_smoke_records import FRAME_ALLOCATOR_TEST_MARKER
from tools.qemu_smoke_records import FRAME_ALLOCATOR_READY_PATTERN
from tools.qemu_smoke_records import FRAME_ALLOCATOR_TEST_PASS
from tools.qemu_smoke_records import MMU_READY_MARKER
from tools.qemu_smoke_records import MMU_TEST_MARKER
from tools.qemu_smoke_records import MMU_READY_PATTERN
from tools.qemu_smoke_records import MMU_TEST_PASS
from tools.qemu_smoke_records import FRAME_OWNERSHIP_READY_MARKER
from tools.qemu_smoke_records import FRAME_OWNERSHIP_TEST_MARKER
from tools.qemu_smoke_records import FRAME_OWNERSHIP_READY_PATTERN
from tools.qemu_smoke_records import FRAME_OWNERSHIP_TEST_PASS
from tools.qemu_smoke_records import USER_ADDRESS_SPACE_TEST_MARKER
from tools.qemu_smoke_records import USER_ADDRESS_SPACE_TEST_PASS
from tools.qemu_smoke_records import USER_EXECUTION_TEST_MARKER
from tools.qemu_smoke_records import USER_EXECUTION_TEST_PASS
from tools.qemu_smoke_records import SCHEDULER_TEST_MARKER
from tools.qemu_smoke_records import SCHEDULER_TEST_PASS
from tools.qemu_smoke_records import ENDPOINT_TEST_MARKER
from tools.qemu_smoke_records import ENDPOINT_TEST_PASS
from tools.qemu_smoke_records import IPC_TEST_MARKER
from tools.qemu_smoke_records import IPC_ADDRESS_SPACES_MARKER
from tools.qemu_smoke_records import IPC_ADDRESS_SPACES_PASS
from tools.qemu_smoke_records import IPC_TEST_PASS
from tools.qemu_smoke_records import OBJECTS_READY_MARKER
from tools.qemu_smoke_records import OBJECT_MODEL_TEST_MARKER
from tools.qemu_smoke_records import NESTED_TRAP_TEST_MARKER
from tools.qemu_smoke_records import OBJECTS_READY_PATTERN
from tools.qemu_smoke_records import OBJECT_MODEL_TEST_PASS
from tools.qemu_smoke_records import NESTED_TRAP_TEST_PASS
from tools.qemu_smoke_records import FDT_COUNTS_PATTERN
from tools.qemu_smoke_records import FDT_EVENT_NAMES
from tools.qemu_smoke_records import FDT_RANGE_PATTERN
from tools.qemu_smoke_records import PANIC_CORE_PATTERNS
from tools.qemu_smoke_records import TRAP_CONTEXT_PATTERN
from tools.qemu_smoke_records import VM_SELF_FAULT_PATTERN


from tools.qemu_smoke_records import SmokeOutcome


from tools.qemu_smoke_records import QemuResult


from tools.qemu_smoke_platform import _has_complete_fdt_events


from tools.qemu_smoke_records import _split_output_records


from tools.qemu_smoke_faults import _has_complete_panic_report


from tools.qemu_smoke_platform import _has_complete_trap_ready


from tools.qemu_smoke_objects import parse_objects_ready


from tools.qemu_smoke_objects import _has_complete_objects_ready


from tools.qemu_smoke_platform import _has_complete_trap_test_report


from tools.qemu_smoke_platform import _has_complete_timer_test_report


from tools.qemu_smoke_platform import parse_frame_allocator_ready_counts


from tools.qemu_smoke_platform import _has_complete_frame_allocator_ready


from tools.qemu_smoke_platform import _frame_allocator_ready_precedes_target_outcome


from tools.qemu_smoke_platform import has_expected_frame_allocator_growth


from tools.qemu_smoke_platform import _has_complete_frame_allocator_test_report


from tools.qemu_smoke_platform import parse_mmu_ready


from tools.qemu_smoke_platform import _has_complete_mmu_ready


from tools.qemu_smoke_platform import _mmu_ready_precedes_target_outcome


from tools.qemu_smoke_platform import _has_complete_mmu_test_report


from tools.qemu_smoke_platform import parse_frame_ownership_ready


from tools.qemu_smoke_platform import _has_complete_frame_ownership_ready


from tools.qemu_smoke_platform import _frame_ownership_ready_precedes_target_outcome


from tools.qemu_smoke_platform import _has_complete_frame_ownership_test_report


from tools.qemu_smoke_objects import _has_complete_user_address_space_test_report


from tools.qemu_smoke_objects import _has_complete_user_execution_test_report


from tools.qemu_smoke_objects import _has_complete_scheduler_test_report


from tools.qemu_smoke_objects import _has_complete_endpoint_test_report


from tools.qemu_smoke_objects import _has_complete_ipc_test_report


from tools.qemu_smoke_objects import _objects_ready_precedes_target_outcome


from tools.qemu_smoke_objects import _has_complete_object_model_test_report


from tools.qemu_smoke_objects import _has_complete_nested_trap_test_report


from tools.qemu_smoke_faults import _has_complete_trap_context


from tools.qemu_smoke_faults import _has_vm_self_fault


from tools.qemu_smoke_faults import _has_complete_scheduler_invalid_context


from tools.qemu_smoke_command import parse_nm_symbol_address


from tools.qemu_smoke_command import resolve_symbol_address


def _has_required_output(output_lines, markers, patterns):
    if not all(marker in output_lines for marker in markers):
        return False
    return all(
        any(re.fullmatch(pattern, line) for line in output_lines)
        for pattern in patterns
    )


def _has_ordered_patterns(output_lines, patterns):
    next_index = 0

    for pattern in patterns:
        for index in range(next_index, len(output_lines)):
            if re.fullmatch(pattern, output_lines[index]):
                next_index = index + 1
                break
        else:
            return False
    return True


def matches_expected_result(
    *,
    result,
    observed_outcome,
    expected_outcome,
    markers,
    patterns,
    forbidden_markers=(),
    forbidden_patterns=(),
    ordered_patterns=(),
    require_fdt_events=False,
    require_fdt_reservations=False,
    require_panic_report=False,
    require_trap_ready=False,
    require_trap_test_report=False,
    require_timer_test_report=False,
    require_frame_allocator_ready=False,
    require_frame_allocator_test_report=False,
    require_mmu_ready=False,
    require_mmu_test_report=False,
    require_frame_ownership_ready=False,
    require_frame_ownership_test_report=False,
    require_user_address_space_test_report=False,
    require_user_execution_test_report=False,
    require_scheduler_test_report=False,
    require_endpoint_test_report=False,
    require_ipc_test_report=False,
    require_scheduler_invalid_context=None,
    require_objects_ready=False,
    require_object_model_test_report=False,
    require_nested_trap_test_report=False,
    require_trap_context=False,
    expected_trap_context_sepc=None,
    expected_trap_context_origin=None,
    expected_trap_context_scause=None,
    expected_trap_context_stval=None,
    require_vm_self_fault=False,
    expected_vm_self_fault_sepc=None,
    expected_vm_self_fault_scause=None,
    expected_vm_self_fault_stval=None,
    expected_vm_self_fault_ownership=None,
):
    output_lines = result.output.splitlines()

    if (
        observed_outcome is not expected_outcome
        or result.timed_out
        or result.return_code != 0
        or FAILURE_MARKER in result.output
        or any(line.startswith("not ok ") for line in output_lines)
        or not _has_required_output(output_lines, markers, patterns)
        or any(
            forbidden in output_lines
            for forbidden in forbidden_markers
        )
        or any(
            re.fullmatch(pattern, line)
            for pattern in forbidden_patterns
            for line in output_lines
        )
        or not _has_ordered_patterns(output_lines, ordered_patterns)
    ):
        return False
    if (
        require_fdt_events or require_fdt_reservations
    ) and not _has_complete_fdt_events(
        output_lines,
        require_reservations=require_fdt_reservations,
    ):
        return False
    if require_panic_report and not _has_complete_panic_report(result.output):
        return False
    if require_trap_ready and not _has_complete_trap_ready(output_lines):
        return False
    if (
        require_trap_test_report
        and not _has_complete_trap_test_report(output_lines)
    ):
        return False
    if (
        require_timer_test_report
        and not _has_complete_timer_test_report(result.output)
    ):
        return False
    if (
        require_frame_allocator_ready
        and (
            not _has_complete_frame_allocator_ready(result.output)
            or not _frame_allocator_ready_precedes_target_outcome(
                result.output
            )
        )
    ):
        return False
    if (
        require_frame_allocator_test_report
        and not _has_complete_frame_allocator_test_report(result.output)
    ):
        return False
    if (
        require_mmu_ready
        and (
            not _has_complete_mmu_ready(result.output)
            or not _mmu_ready_precedes_target_outcome(result.output)
        )
    ):
        return False
    if (
        require_mmu_test_report
        and not _has_complete_mmu_test_report(result.output)
    ):
        return False
    if (
        require_frame_ownership_ready
        and (
            not _has_complete_frame_ownership_ready(result.output)
            or not _frame_ownership_ready_precedes_target_outcome(
                result.output
            )
        )
    ):
        return False
    if (
        require_frame_ownership_test_report
        and not _has_complete_frame_ownership_test_report(result.output)
    ):
        return False
    if (
        require_user_address_space_test_report
        and not _has_complete_user_address_space_test_report(
            result.output
        )
    ):
        return False
    if (
        require_user_execution_test_report
        and not _has_complete_user_execution_test_report(
            result.output
        )
    ):
        return False
    if (
        require_scheduler_test_report
        and not _has_complete_scheduler_test_report(result.output)
    ):
        return False
    if (
        require_endpoint_test_report
        and not _has_complete_endpoint_test_report(result.output)
    ):
        return False
    if (
        require_ipc_test_report
        and not _has_complete_ipc_test_report(result.output)
    ):
        return False
    if (
        require_scheduler_invalid_context is not None
        and not _has_complete_scheduler_invalid_context(
            result.output,
            require_scheduler_invalid_context,
        )
    ):
        return False
    if (
        require_objects_ready
        and (
            not _has_complete_objects_ready(result.output)
            or not _objects_ready_precedes_target_outcome(result.output)
        )
    ):
        return False
    if (
        require_object_model_test_report
        and not _has_complete_object_model_test_report(result.output)
    ):
        return False
    if (
        require_nested_trap_test_report
        and not _has_complete_nested_trap_test_report(result.output)
    ):
        return False
    has_trap_context = any(
        line.startswith("MICROS_TRAP_CONTEXT")
        for line in output_lines
    )
    if (
        require_trap_context
        and not _has_complete_trap_context(
            result.output,
            expected_sepc=expected_trap_context_sepc,
            expected_origin=expected_trap_context_origin,
            expected_scause=expected_trap_context_scause,
            expected_stval=expected_trap_context_stval,
        )
    ):
        return False
    if not require_trap_context and has_trap_context:
        return False
    if (
        require_vm_self_fault
        and (
            expected_vm_self_fault_sepc is None
            or expected_vm_self_fault_scause is None
            or expected_vm_self_fault_stval is None
            or expected_vm_self_fault_ownership is None
            or not _has_vm_self_fault(
                result.output,
                expected_sepc=expected_vm_self_fault_sepc,
                expected_scause=expected_vm_self_fault_scause,
                expected_stval=expected_vm_self_fault_stval,
                expected_ownership=expected_vm_self_fault_ownership,
            )
        )
    ):
        return False
    return True


def classify_smoke(
    *,
    output,
    return_code,
    timed_out,
    markers,
    require_fdt_events=False,
    require_fdt_reservations=False,
):
    if PANIC_MARKER in output:
        return SmokeOutcome.PANIC
    if FAILURE_MARKER in output or any(
        line.startswith("not ok ") for line in output.splitlines()
    ):
        return SmokeOutcome.FAILURE
    if timed_out:
        return SmokeOutcome.TIMEOUT
    output_lines = output.splitlines()
    if (
        return_code == 0
        and all(marker in output_lines for marker in markers)
        and (
            not (require_fdt_events or require_fdt_reservations)
            or _has_complete_fdt_events(
                output_lines,
                require_reservations=require_fdt_reservations,
            )
        )
    ):
        return SmokeOutcome.PASS
    return SmokeOutcome.UNEXPECTED_EXIT


from tools.qemu_smoke_command import build_qemu_command


from tools.qemu_smoke_execution import _decode_output


from tools.qemu_smoke_execution import _serial_trigger_observed


from tools.qemu_smoke_execution import _run_qemu_with_serial_input


from tools.qemu_smoke_execution import run_qemu


def print_tap_result(
    outcome,
    result,
    command,
    expected_outcome=SmokeOutcome.PASS,
    accepted=None,
):
    if accepted is None:
        accepted = outcome is SmokeOutcome.PASS

    print("TAP version 13")
    if accepted:
        print(
            "ok 1 - QEMU smoke test observed expected "
            f"{expected_outcome.value}"
        )
        print(f"# outcome: {outcome.value}")
        print(f"# expected outcome: {expected_outcome.value}")
        return 0

    print(
        "not ok 1 - QEMU smoke test expected "
        f"{expected_outcome.value} but reported {outcome.value}"
    )
    print(f"# outcome: {outcome.value}")
    print(f"# expected outcome: {expected_outcome.value}")
    print(f"# command: {shlex.join(command)}")
    if result.return_code is None:
        print("# qemu return code: none")
    else:
        print(f"# qemu return code: {result.return_code}")
    for line in result.output.rstrip().splitlines():
        print(f"# {line}")

    return {
        SmokeOutcome.FAILURE: 1,
        SmokeOutcome.PANIC: 2,
        SmokeOutcome.UNEXPECTED_EXIT: 3,
        SmokeOutcome.TIMEOUT: 4,
    }.get(outcome, 3)


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        description="Run the deterministic micros QEMU boot smoke test."
    )
    parser.add_argument(
        "--qemu",
        default=os.environ.get("MICROS_QEMU", "qemu-system-riscv64"),
        help="QEMU RISC-V system executable",
    )
    parser.add_argument(
        "--nm",
        default=os.environ.get("MICROS_NM", "llvm-nm"),
        help="LLVM nm executable used for exact target-symbol checks",
    )
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument(
        "--memory",
        action="append",
        default=[],
        help="QEMU guest RAM size; may be repeated for one ELF",
    )
    parser.add_argument(
        "--expected-frame-allocator-managed-delta",
        type=lambda value: int(value, 0),
        help="Required managed-frame growth across exactly two memory sizes",
    )
    parser.add_argument(
        "--marker",
        action="append",
        required=True,
        help="Exact serial line required for success; may be repeated",
    )
    parser.add_argument(
        "--pattern",
        action="append",
        default=[],
        help="Full-line regular expression required in serial output",
    )
    parser.add_argument(
        "--forbid-marker",
        action="append",
        default=[],
        help="Exact serial line that must be absent; may be repeated",
    )
    parser.add_argument(
        "--forbid-pattern",
        action="append",
        default=[],
        help="Full-line regular expression that must be absent; may be repeated",
    )
    parser.add_argument(
        "--ordered-pattern",
        action="append",
        default=[],
        help="Full-line regex required in the given order; may be repeated",
    )
    parser.add_argument(
        "--serial-input-trigger",
        help="Exact complete serial line after which input is written once",
    )
    parser.add_argument(
        "--serial-input-hex",
        help="Hexadecimal serial bytes written after the input trigger",
    )
    parser.add_argument(
        "--expect",
        choices=("pass", "panic"),
        default="pass",
        help="Observed outcome required for a successful test",
    )
    parser.add_argument(
        "--require-fdt-events",
        action="store_true",
        help="Require FDT range events to match the guest's count summary",
    )
    parser.add_argument(
        "--require-fdt-reservations",
        action="store_true",
        help="Require at least one firmware reservation in the FDT events",
    )
    parser.add_argument(
        "--require-panic-report",
        action="store_true",
        help="Require the ordered five-record panic report",
    )
    parser.add_argument(
        "--require-trap-ready",
        action="store_true",
        help="Require exactly one trap-ready record",
    )
    parser.add_argument(
        "--require-trap-test-report",
        action="store_true",
        help="Require the ordered trap recovery test records",
    )
    parser.add_argument(
        "--require-timer-test-report",
        action="store_true",
        help="Require the ordered timer interrupt test record",
    )
    parser.add_argument(
        "--require-frame-allocator-ready",
        action="store_true",
        help="Require a nonempty frame allocator ready record",
    )
    parser.add_argument(
        "--require-frame-allocator-test-report",
        action="store_true",
        help="Require the ordered frame allocator test record",
    )
    parser.add_argument(
        "--require-mmu-ready",
        action="store_true",
        help="Require an ordered Sv39 MMU ready record",
    )
    parser.add_argument(
        "--require-mmu-test-report",
        action="store_true",
        help="Require the ordered MMU permission test record",
    )
    parser.add_argument(
        "--require-frame-ownership-ready",
        action="store_true",
        help="Require the ordered typed frame ownership record",
    )
    parser.add_argument(
        "--require-frame-ownership-test-report",
        action="store_true",
        help="Require the ordered frame ownership test record",
    )
    parser.add_argument(
        "--require-user-address-space-test-report",
        action="store_true",
        help="Require the ordered user address-space test record",
    )
    parser.add_argument(
        "--require-user-execution-test-report",
        action="store_true",
        help="Require the ordered user execution test record",
    )
    parser.add_argument(
        "--require-scheduler-test-report",
        action="store_true",
        help="Require the ordered scheduler test record",
    )
    parser.add_argument(
        "--require-endpoint-test-report",
        action="store_true",
        help="Require the ordered endpoint and profile test record",
    )
    parser.add_argument(
        "--require-ipc-test-report",
        action="store_true",
        help="Require the ordered blocking IPC acceptance record",
    )
    parser.add_argument(
        "--require-scheduler-invalid-context",
        choices=("outgoing", "next"),
        help="Require one exact invalid scheduler-context diagnostic",
    )
    parser.add_argument(
        "--require-objects-ready",
        action="store_true",
        help="Require the ordered kernel-object readiness record",
    )
    parser.add_argument(
        "--require-object-model-test-report",
        action="store_true",
        help="Require the ordered kernel-object model test record",
    )
    parser.add_argument(
        "--require-nested-trap-test-report",
        action="store_true",
        help="Require the ordered per-hart nested-trap test record",
    )
    parser.add_argument(
        "--require-trap-context",
        action="store_true",
        help="Require one trap context immediately after the panic core",
    )
    parser.add_argument(
        "--trap-context-sepc-symbol",
        help="ELF symbol whose address must equal trap-context sepc",
    )
    parser.add_argument(
        "--trap-context-origin",
        choices=("S", "U"),
        help="Exact required trap-context origin",
    )
    parser.add_argument(
        "--trap-context-scause",
        type=lambda value: int(value, 0),
        help="Exact required trap-context scause",
    )
    parser.add_argument(
        "--trap-context-stval",
        type=lambda value: int(value, 0),
        help="Exact required trap-context stval",
    )
    parser.add_argument(
        "--require-vm-self-fault",
        action="store_true",
        help="Require one exact VM self-fault record",
    )
    parser.add_argument(
        "--vm-self-fault-elf",
        type=Path,
        help="ELF containing the expected VM fault-PC symbol",
    )
    parser.add_argument(
        "--vm-self-fault-symbol",
        help="Symbol whose address must equal VM self-fault sepc",
    )
    parser.add_argument(
        "--vm-self-fault-scause",
        type=lambda value: int(value, 0),
        help="Exact required VM self-fault scause",
    )
    parser.add_argument(
        "--vm-self-fault-stval",
        type=lambda value: int(value, 0),
        help="Exact required VM self-fault stval",
    )
    parser.add_argument(
        "--vm-self-fault-ownership",
        choices=("bootstrap", "handed-off"),
        help="Exact required VM self-fault ownership phase",
    )
    parser.add_argument("--timeout", type=float, default=10.0)
    arguments = parser.parse_args(argv)

    if arguments.timeout <= 0:
        parser.error("--timeout must be greater than zero")
    if any(not marker for marker in arguments.marker):
        parser.error("--marker values must not be empty")
    if not arguments.memory:
        arguments.memory = ["128M"]
    if any(not memory for memory in arguments.memory):
        parser.error("--memory must not be empty")
    if (
        arguments.serial_input_trigger is None
    ) != (
        arguments.serial_input_hex is None
    ):
        parser.error(
            "--serial-input-trigger and --serial-input-hex "
            "must be provided together"
        )
    arguments.serial_input = None
    if arguments.serial_input_hex is not None:
        if not arguments.serial_input_trigger:
            parser.error("--serial-input-trigger must not be empty")
        try:
            arguments.serial_input = bytes.fromhex(
                arguments.serial_input_hex
            )
        except ValueError as error:
            parser.error(f"invalid --serial-input-hex: {error}")
        if not arguments.serial_input:
            parser.error("--serial-input-hex must not be empty")
    if (
        arguments.expected_frame_allocator_managed_delta is not None
        and (
            len(arguments.memory) != 2
            or not arguments.require_frame_allocator_ready
                and not arguments.require_frame_allocator_test_report
        )
    ):
        parser.error(
            "--expected-frame-allocator-managed-delta requires "
            "two --memory values and a frame allocator report"
        )
    for pattern in (
        arguments.pattern
        + arguments.forbid_pattern
        + arguments.ordered_pattern
    ):
        try:
            re.compile(pattern)
        except re.error as error:
            parser.error(f"invalid --pattern {pattern!r}: {error}")
    if not arguments.kernel.is_file():
        parser.error(f"kernel image does not exist: {arguments.kernel}")
    if (
        arguments.trap_context_sepc_symbol
        and not arguments.require_trap_context
    ):
        parser.error(
            "--trap-context-sepc-symbol requires "
            "--require-trap-context"
        )
    if (
        any(
            value is not None
            for value in (
                arguments.trap_context_origin,
                arguments.trap_context_scause,
                arguments.trap_context_stval,
            )
        )
        and not arguments.require_trap_context
    ):
        parser.error(
            "trap-context field requirements need "
            "--require-trap-context"
        )
    vm_fault_arguments = (
        arguments.vm_self_fault_elf,
        arguments.vm_self_fault_symbol,
        arguments.vm_self_fault_scause,
        arguments.vm_self_fault_stval,
        arguments.vm_self_fault_ownership,
    )
    if arguments.require_vm_self_fault:
        if any(value is None for value in vm_fault_arguments):
            parser.error(
                "--require-vm-self-fault needs ELF, symbol, "
                "scause, stval, and ownership"
            )
        if not arguments.vm_self_fault_elf.is_file():
            parser.error(
                "VM self-fault ELF does not exist: "
                f"{arguments.vm_self_fault_elf}"
            )
    elif any(value is not None for value in vm_fault_arguments):
        parser.error(
            "VM self-fault expectations need "
            "--require-vm-self-fault"
        )
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)
    expected_trap_context_sepc = None
    expected_vm_self_fault_sepc = None
    if arguments.trap_context_sepc_symbol:
        try:
            expected_trap_context_sepc = resolve_symbol_address(
                arguments.nm,
                arguments.kernel,
                arguments.trap_context_sepc_symbol,
            )
        except (FileNotFoundError, RuntimeError, ValueError) as error:
            print(str(error), file=sys.stderr)
            return 2
    if arguments.require_vm_self_fault:
        try:
            expected_vm_self_fault_sepc = resolve_symbol_address(
                arguments.nm,
                arguments.vm_self_fault_elf,
                arguments.vm_self_fault_symbol,
            )
        except (FileNotFoundError, RuntimeError, ValueError) as error:
            print(str(error), file=sys.stderr)
            return 2
        if expected_trap_context_sepc is None:
            expected_trap_context_sepc = expected_vm_self_fault_sepc

    expected_outcome = SmokeOutcome(arguments.expect)
    successful_runs = []
    for memory in arguments.memory:
        command = build_qemu_command(
            qemu=arguments.qemu,
            kernel=str(arguments.kernel.resolve()),
            memory=memory,
        )
        try:
            result = run_qemu(
                command,
                arguments.timeout,
                serial_input_trigger=arguments.serial_input_trigger,
                serial_input=arguments.serial_input,
            )
        except FileNotFoundError:
            print(
                f"QEMU executable not found: {arguments.qemu}",
                file=sys.stderr,
            )
            return 2

        outcome = classify_smoke(
            output=result.output,
            return_code=result.return_code,
            timed_out=result.timed_out,
            markers=arguments.marker,
            require_fdt_events=arguments.require_fdt_events,
            require_fdt_reservations=arguments.require_fdt_reservations,
        )
        accepted = matches_expected_result(
            result=result,
            observed_outcome=outcome,
            expected_outcome=expected_outcome,
            markers=arguments.marker,
            patterns=arguments.pattern,
            forbidden_markers=arguments.forbid_marker,
            forbidden_patterns=arguments.forbid_pattern,
            ordered_patterns=arguments.ordered_pattern,
            require_fdt_events=arguments.require_fdt_events,
            require_fdt_reservations=arguments.require_fdt_reservations,
            require_panic_report=arguments.require_panic_report,
            require_trap_ready=arguments.require_trap_ready,
            require_trap_test_report=arguments.require_trap_test_report,
            require_timer_test_report=arguments.require_timer_test_report,
            require_frame_allocator_ready=(
                arguments.require_frame_allocator_ready
            ),
            require_frame_allocator_test_report=(
                arguments.require_frame_allocator_test_report
            ),
            require_mmu_ready=arguments.require_mmu_ready,
            require_mmu_test_report=arguments.require_mmu_test_report,
            require_frame_ownership_ready=(
                arguments.require_frame_ownership_ready
            ),
            require_frame_ownership_test_report=(
                arguments.require_frame_ownership_test_report
            ),
            require_user_address_space_test_report=(
                arguments.require_user_address_space_test_report
            ),
            require_user_execution_test_report=(
                arguments.require_user_execution_test_report
            ),
            require_scheduler_test_report=(
                arguments.require_scheduler_test_report
            ),
            require_endpoint_test_report=(
                arguments.require_endpoint_test_report
            ),
            require_ipc_test_report=(
                arguments.require_ipc_test_report
            ),
            require_scheduler_invalid_context=(
                arguments.require_scheduler_invalid_context
            ),
            require_objects_ready=arguments.require_objects_ready,
            require_object_model_test_report=(
                arguments.require_object_model_test_report
            ),
            require_nested_trap_test_report=(
                arguments.require_nested_trap_test_report
            ),
            require_trap_context=arguments.require_trap_context,
            expected_trap_context_sepc=expected_trap_context_sepc,
            expected_trap_context_origin=arguments.trap_context_origin,
            expected_trap_context_scause=arguments.trap_context_scause,
            expected_trap_context_stval=arguments.trap_context_stval,
            require_vm_self_fault=arguments.require_vm_self_fault,
            expected_vm_self_fault_sepc=expected_vm_self_fault_sepc,
            expected_vm_self_fault_scause=(
                arguments.vm_self_fault_scause
            ),
            expected_vm_self_fault_stval=(
                arguments.vm_self_fault_stval
            ),
            expected_vm_self_fault_ownership=(
                arguments.vm_self_fault_ownership
            ),
        )
        accepted = (
            accepted
            and (
                arguments.serial_input_trigger is None
                or result.serial_input_sent
            )
        )
        if not accepted:
            return print_tap_result(
                outcome,
                result,
                command,
                expected_outcome=expected_outcome,
                accepted=False,
            )
        successful_runs.append((memory, result, outcome, command))

    if arguments.expected_frame_allocator_managed_delta is not None:
        outputs = [run[1].output for run in successful_runs]
        if not has_expected_frame_allocator_growth(
            outputs,
            arguments.expected_frame_allocator_managed_delta,
        ):
            print("TAP version 13")
            print(
                "not ok 1 - frame allocator managed-frame growth "
                "did not match"
            )
            print(
                "# expected managed-frame delta: "
                f"0x{arguments.expected_frame_allocator_managed_delta:016x}"
            )
            for memory, result, _, _ in successful_runs:
                counts = parse_frame_allocator_ready_counts(result.output)
                print(f"# memory {memory}: counts={counts}")
            return 3

    if len(successful_runs) > 1:
        print("TAP version 13")
        print(
            "ok 1 - QEMU smoke test observed expected "
            f"{expected_outcome.value} for every memory size"
        )
        for memory, result, outcome, _ in successful_runs:
            counts = parse_frame_allocator_ready_counts(result.output)
            print(
                f"# memory {memory}: outcome={outcome.value} "
                f"frame-counts={counts}"
            )
        return 0

    _, result, outcome, command = successful_runs[0]
    return print_tap_result(
        outcome,
        result,
        command,
        expected_outcome=expected_outcome,
        accepted=True,
    )


if __name__ == "__main__":
    sys.exit(main())
