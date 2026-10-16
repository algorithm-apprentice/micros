import argparse
import os
import re
from pathlib import Path


def _add_execution_arguments(
    *,
    parser,
):
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


def _add_platform_report_arguments(
    *,
    parser,
):
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


def _add_object_report_arguments(
    *,
    parser,
):
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


def _add_fault_arguments(
    *,
    parser,
):
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


def _validate_execution_arguments(
    *,
    parser,
    arguments,
):
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


def _validate_fault_arguments(
    *,
    parser,
    arguments,
):
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


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        description="Run the deterministic micros QEMU boot smoke test."
    )
    _add_execution_arguments(
        parser=parser,
    )
    _add_platform_report_arguments(
        parser=parser,
    )
    _add_object_report_arguments(
        parser=parser,
    )
    _add_fault_arguments(
        parser=parser,
    )
    parser.add_argument("--timeout", type=float, default=10.0)
    arguments = parser.parse_args(argv)
    _validate_execution_arguments(
        parser=parser,
        arguments=arguments,
    )
    _validate_fault_arguments(
        parser=parser,
        arguments=arguments,
    )
    return arguments
