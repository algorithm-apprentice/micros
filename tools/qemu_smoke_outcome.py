import re
import shlex

from tools.qemu_smoke_records import (
    PANIC_MARKER,
    FAILURE_MARKER,
    SmokeOutcome,
)

from tools.qemu_smoke_platform import (
    _has_complete_fdt_events,
    _has_complete_trap_ready,
    _has_complete_trap_test_report,
    _has_complete_timer_test_report,
    _has_complete_frame_allocator_ready,
    _frame_allocator_ready_precedes_target_outcome,
    _has_complete_frame_allocator_test_report,
    _has_complete_mmu_ready,
    _mmu_ready_precedes_target_outcome,
    _has_complete_mmu_test_report,
    _has_complete_frame_ownership_ready,
    _frame_ownership_ready_precedes_target_outcome,
    _has_complete_frame_ownership_test_report,
)

from tools.qemu_smoke_objects import (
    _has_complete_objects_ready,
    _has_complete_user_address_space_test_report,
    _has_complete_user_execution_test_report,
    _has_complete_scheduler_test_report,
    _has_complete_endpoint_test_report,
    _has_complete_ipc_test_report,
    _objects_ready_precedes_target_outcome,
    _has_complete_object_model_test_report,
    _has_complete_nested_trap_test_report,
)

from tools.qemu_smoke_faults import (
    _has_complete_panic_report,
    _has_complete_trap_context,
    _has_vm_self_fault,
    _has_complete_scheduler_invalid_context,
)


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


def _matches_foundation_reports(
    *,
    result,
    output_lines,
    require_fdt_events,
    require_fdt_reservations,
    require_panic_report,
    require_trap_ready,
    require_trap_test_report,
    require_timer_test_report,
    require_frame_allocator_ready,
    require_frame_allocator_test_report,
):
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
    return True


def _matches_memory_reports(
    *,
    result,
    require_mmu_ready,
    require_mmu_test_report,
    require_frame_ownership_ready,
    require_frame_ownership_test_report,
    require_user_address_space_test_report,
    require_user_execution_test_report,
):
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
    return True


def _matches_object_reports(
    *,
    result,
    require_scheduler_test_report,
    require_endpoint_test_report,
    require_ipc_test_report,
    require_scheduler_invalid_context,
    require_objects_ready,
    require_object_model_test_report,
    require_nested_trap_test_report,
):
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
    return True


def _matches_fault_reports(
    *,
    result,
    output_lines,
    require_trap_context,
    expected_trap_context_sepc,
    expected_trap_context_origin,
    expected_trap_context_scause,
    expected_trap_context_stval,
    require_vm_self_fault,
    expected_vm_self_fault_sepc,
    expected_vm_self_fault_scause,
    expected_vm_self_fault_stval,
    expected_vm_self_fault_ownership,
):
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
    if not _matches_foundation_reports(
        result=result,
        output_lines=output_lines,
        require_fdt_events=require_fdt_events,
        require_fdt_reservations=require_fdt_reservations,
        require_panic_report=require_panic_report,
        require_trap_ready=require_trap_ready,
        require_trap_test_report=require_trap_test_report,
        require_timer_test_report=require_timer_test_report,
        require_frame_allocator_ready=require_frame_allocator_ready,
        require_frame_allocator_test_report=require_frame_allocator_test_report,
    ):
        return False
    if not _matches_memory_reports(
        result=result,
        require_mmu_ready=require_mmu_ready,
        require_mmu_test_report=require_mmu_test_report,
        require_frame_ownership_ready=require_frame_ownership_ready,
        require_frame_ownership_test_report=require_frame_ownership_test_report,
        require_user_address_space_test_report=require_user_address_space_test_report,
        require_user_execution_test_report=require_user_execution_test_report,
    ):
        return False
    if not _matches_object_reports(
        result=result,
        require_scheduler_test_report=require_scheduler_test_report,
        require_endpoint_test_report=require_endpoint_test_report,
        require_ipc_test_report=require_ipc_test_report,
        require_scheduler_invalid_context=require_scheduler_invalid_context,
        require_objects_ready=require_objects_ready,
        require_object_model_test_report=require_object_model_test_report,
        require_nested_trap_test_report=require_nested_trap_test_report,
    ):
        return False
    if not _matches_fault_reports(
        result=result,
        output_lines=output_lines,
        require_trap_context=require_trap_context,
        expected_trap_context_sepc=expected_trap_context_sepc,
        expected_trap_context_origin=expected_trap_context_origin,
        expected_trap_context_scause=expected_trap_context_scause,
        expected_trap_context_stval=expected_trap_context_stval,
        require_vm_self_fault=require_vm_self_fault,
        expected_vm_self_fault_sepc=expected_vm_self_fault_sepc,
        expected_vm_self_fault_scause=expected_vm_self_fault_scause,
        expected_vm_self_fault_stval=expected_vm_self_fault_stval,
        expected_vm_self_fault_ownership=expected_vm_self_fault_ownership,
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
