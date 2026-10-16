import sys

from tools.qemu_smoke_records import (
    SmokeOutcome,
)

from tools.qemu_smoke_platform import (
    parse_frame_allocator_ready_counts,
    has_expected_frame_allocator_growth,
)

from tools.qemu_smoke_command import (
    resolve_symbol_address,
    build_qemu_command,
)

from tools.qemu_smoke_execution import (
    run_qemu,
)

from tools.qemu_smoke_outcome import (
    matches_expected_result,
    classify_smoke,
    print_tap_result,
)

from tools.qemu_smoke_arguments import (
    parse_arguments,
)


def _matches_observed_run(
    arguments,
    result,
    outcome,
    expected_outcome,
    expected_trap_context_sepc,
    expected_vm_self_fault_sepc,
):
    return matches_expected_result(
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
        accepted = _matches_observed_run(
            arguments, result, outcome, expected_outcome,
            expected_trap_context_sepc, expected_vm_self_fault_sepc,
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
