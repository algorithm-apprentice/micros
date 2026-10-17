#include "kernel/tests/vm/vm_scratch_preemption_test_internal.h"

#include <stddef.h>
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/objects/kernel_object_runtime_internal.h"
#include "kernel/pm/pm_control_runtime.h"
#include "kernel/vm/vm_handoff_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/scheduler.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/vm.h"

struct scratch_trap_authority {
    const struct micros_bootstrap_control_state *bootstrap;
    const struct micros_vm_handoff_state *handoff;
    const struct micros_pm_control_state *control;
    const struct micros_endpoint_registry *registry;
    struct micros_kernel_objects *objects;
    const struct micros_bootstrap_binding *vm;
    const struct micros_bootstrap_binding *peer;
};

static bool peer_hold_trap_matches(
    const struct vm_scratch_preemption_test_context *context,
    const struct scratch_trap_authority *authority,
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    return (
        context->test_phase == SCRATCH_PREEMPTION_WAITING_PEER_HOLD
        && frame->scause == MICROS_TEST_EXCEPTION_BREAKPOINT
        && frame->sepc == micros_vm_scratch_preemption_peer_hold_trap_address
        && frame->a0 == MICROS_VM_SCRATCH_PREEMPTION_PEER_HOLD_MAGIC
        && vm_scratch_test_current_is(hart, authority->peer)
        && authority->bootstrap->phase == MICROS_BOOTSTRAP_PHASE_SEALED
        && authority->control != NULL
        && authority->control->active_transaction != 0
        && authority->control->reserved_process.slot
            == MICROS_VM_SCRATCH_PREEMPTION_RESERVED_PROCESS_SLOT
        && authority->control->reserved_process.generation == 1
        && authority->objects->processes[authority->vm->process.slot].address_space_root
            != authority->objects->processes[authority->peer->process.slot].address_space_root
        && vm_scratch_test_read_satp()
            == vm_scratch_test_expected_satp(
                authority->objects->processes[authority->peer->process.slot]
                    .address_space_root
            )
        && authority->objects->threads[authority->peer->thread.slot].runtime_flags == 0
        && authority->objects->threads[authority->peer->thread.slot].scheduler_assigned
    );
}

static bool vm_hook_trap_matches(
    const struct vm_scratch_preemption_test_context *context,
    const struct scratch_trap_authority *authority,
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    return (
        context->test_phase == SCRATCH_PREEMPTION_PEER_HELD
        && frame->scause == MICROS_TEST_EXCEPTION_BREAKPOINT
        && frame->sepc == micros_vm_scratch_preemption_vm_hook_trap_address
        && frame->a0 == MICROS_VM_SCRATCH_PREEMPTION_VM_HOOK_MAGIC
        && vm_scratch_test_current_is(hart, authority->vm)
        && vm_scratch_test_runtime_is_valid(
            authority->bootstrap, authority->handoff, authority->control,
            authority->registry, authority->objects
        )
        && vm_scratch_test_read_satp()
            == vm_scratch_test_expected_satp(
                authority->objects->processes[authority->vm->process.slot]
                    .address_space_root
            )
        && authority->objects->threads[authority->peer->thread.slot].runtime_flags
            == MICROS_THREAD_RTS_INACTIVE
        && authority->objects->threads[authority->peer->thread.slot].scheduler_assigned
        && *(const volatile uint64_t *)(uintptr_t)
            authority->handoff->load.scratch.physical_address
            == MICROS_VM_SCRATCH_PREEMPTION_SENTINEL
    );
}

static bool peer_run_trap_matches(
    const struct vm_scratch_preemption_test_context *context,
    const struct scratch_trap_authority *authority,
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    return (
        context->test_phase == SCRATCH_PREEMPTION_PEER_PREEMPTED
        && frame->scause == MICROS_TEST_EXCEPTION_BREAKPOINT
        && frame->sepc == micros_vm_scratch_preemption_peer_run_trap_address
        && frame->a0 == MICROS_VM_SCRATCH_PREEMPTION_PEER_RUN_MAGIC
        && vm_scratch_test_current_is(hart, authority->peer)
        && vm_scratch_test_read_satp()
            == vm_scratch_test_expected_satp(
                authority->objects->processes[authority->peer->process.slot]
                    .address_space_root
            )
        && vm_scratch_test_runtime_is_valid(
            authority->bootstrap, authority->handoff, authority->control,
            authority->registry, authority->objects
        )
        && micros_user_address_space_validate(authority->vm->process)
            == MICROS_USER_ADDRESS_SPACE_OK
        && *(const volatile uint64_t *)(uintptr_t)context->scratch_physical_address
            == MICROS_VM_SCRATCH_PREEMPTION_SENTINEL
    );
}

static enum micros_vm_scratch_preemption_test_trap_result hold_peer(
    struct vm_scratch_preemption_test_context *context,
    const struct scratch_trap_authority *authority,
    struct micros_trap_frame *frame
)
{
    frame->sepc = micros_vm_scratch_preemption_peer_hold_resume_address;
    if (
        micros_thread_scheduler_hold(authority->objects, authority->peer->thread)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_MISMATCH;
    }
    context->test_phase = SCRATCH_PREEMPTION_PEER_HELD;
    return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_USER_RETURN;
}

static enum micros_vm_scratch_preemption_test_trap_result arm_vm_preemption(
    struct vm_scratch_preemption_test_context *context,
    const struct scratch_trap_authority *authority,
    struct micros_trap_frame *frame
)
{
    context->scratch_physical_address = authority->handoff->load.scratch.physical_address;
    context->scratch_load_token = authority->handoff->load.load_token;
    context->scratch_process = authority->handoff->load.process;
    frame->sepc = micros_vm_scratch_preemption_vm_hook_spin_address;
    if (
        micros_thread_runtime_flags_unset(
            authority->objects, authority->peer->thread, MICROS_THREAD_RTS_INACTIVE
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_MISMATCH;
    }
    authority->objects->threads[authority->vm->thread.slot].remaining_counter_ticks = 1;
    context->timer_observed = false;
    context->test_phase = SCRATCH_PREEMPTION_WAITING_TIMER;
    return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_USER_RETURN;
}

enum micros_vm_scratch_preemption_test_trap_result
micros_vm_scratch_preemption_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct vm_scratch_preemption_test_context *context = vm_scratch_test_context_get();
    struct scratch_trap_authority authority;

    authority.bootstrap = micros_bootstrap_runtime_state();
    authority.handoff = micros_vm_handoff_runtime_state();
    authority.control = micros_pm_control_runtime_state();
    authority.registry = micros_ipc_runtime_registry();
    authority.objects = micros_kernel_object_runtime_authoritative_registry();
    if (
        hart == NULL || frame == NULL
        || authority.bootstrap == NULL || authority.objects == NULL
    ) {
        return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_MISMATCH;
    }
    authority.vm = micros_bootstrap_control_find_binding(
        authority.bootstrap, MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID
    );
    authority.peer = micros_bootstrap_control_find_binding(
        authority.bootstrap, MICROS_VM_SCRATCH_PREEMPTION_PM_SERVICE_ID
    );
    if (authority.vm == NULL || authority.peer == NULL) {
        return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_MISMATCH;
    }
    if (peer_hold_trap_matches(context, &authority, hart, frame)) {
        return hold_peer(context, &authority, frame);
    }
    if (vm_hook_trap_matches(context, &authority, hart, frame)) {
        return arm_vm_preemption(context, &authority, frame);
    }
    if (peer_run_trap_matches(context, &authority, hart, frame)) {
        frame->sepc = micros_vm_scratch_preemption_peer_run_resume_address;
        context->test_phase = SCRATCH_PREEMPTION_WAITING_VM_REACTIVATION;
        return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_USER_RETURN;
    }
    if (
        context->test_phase == SCRATCH_PREEMPTION_WAITING_STALE_FAULT
        && frame->scause == MICROS_TEST_EXCEPTION_LOAD_PAGE_FAULT
        && frame->sepc == micros_vm_scratch_preemption_stale_load_fault_address
    ) {
        vm_scratch_test_report_success(context, hart, frame);
    }
    uart_write("MICROS_TEST_FAILURE vm-scratch-preemption phase=");
    uart_write_hex64(context->test_phase);
    uart_write("\n");
    return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_MISMATCH;
}
