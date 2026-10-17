#include "kernel/tests/vm/vm_scratch_preemption_test_internal.h"

#include <stddef.h>
#include "arch/riscv64/trap_context.h"
#include "kernel/vm/vm_handoff_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/vm.h"

_Noreturn void micros_vm_scratch_preemption_test_launch(void)
{
    struct vm_scratch_preemption_test_context *context =
        vm_scratch_test_context_get();

    context->test_phase = SCRATCH_PREEMPTION_WAITING_PEER_HOLD;
    context->timer_observed = false;
    context->scratch_physical_address = 0;
    context->scratch_load_token = 0;
    context->scratch_process = (struct micros_process_handle){0, 0};
    micros_bootstrap_runtime_launch(&vm_scratch_test_runtime_config);
}

bool micros_vm_scratch_preemption_test_before_user_timer(
    const struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct vm_scratch_preemption_test_context *context =
        vm_scratch_test_context_get();
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();
    const struct micros_bootstrap_binding *vm =
        bootstrap == NULL
            ? NULL
            : micros_bootstrap_control_find_binding(
                bootstrap,
                MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID
            );

    if (context->test_phase != SCRATCH_PREEMPTION_WAITING_TIMER) {
        return true;
    }
    if (
        !vm_scratch_test_current_is(hart, vm)
        || frame == NULL
        || frame->sepc
            != micros_vm_scratch_preemption_vm_hook_spin_address
        || context->timer_observed
    ) {
        return false;
    }
    frame->sepc =
        micros_vm_scratch_preemption_vm_hook_resume_address;
    context->timer_observed = true;
    return true;
}

bool micros_vm_scratch_preemption_test_after_user_timer(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct vm_scratch_preemption_test_context *context =
        vm_scratch_test_context_get();
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    const struct micros_bootstrap_binding *peer =
        bootstrap == NULL
            ? NULL
            : micros_bootstrap_control_find_binding(
                bootstrap,
                MICROS_VM_SCRATCH_PREEMPTION_PM_SERVICE_ID
            );

    if (context->test_phase != SCRATCH_PREEMPTION_WAITING_TIMER) {
        return true;
    }
    if (
        !context->timer_observed
        || hart == NULL
        || frame == NULL
        || objects == NULL
        || !vm_scratch_test_current_is(hart, peer)
        || frame->sepc
            != micros_vm_scratch_preemption_peer_hold_resume_address
        || vm_scratch_test_read_satp()
            != vm_scratch_test_expected_satp(
                objects->processes[peer->process.slot]
                    .address_space_root
            )
    ) {
        return false;
    }
    context->test_phase = SCRATCH_PREEMPTION_PEER_PREEMPTED;
    return true;
}

bool micros_vm_scratch_preemption_test_after_syscall_return(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
)
{
    struct vm_scratch_preemption_test_context *context =
        vm_scratch_test_context_get();
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();
    const struct micros_vm_handoff_state *handoff =
        micros_vm_handoff_runtime_state();
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    const struct micros_bootstrap_binding *vm =
        bootstrap == NULL
            ? NULL
            : micros_bootstrap_control_find_binding(
                bootstrap,
                MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID
            );
    const struct micros_vm_kernel_mapping_record *record;

    if (
        context->test_phase
            == SCRATCH_PREEMPTION_WAITING_VM_REACTIVATION
    ) {
        if (
            hart == NULL
            || frame == NULL
            || objects == NULL
            || syscall_return != MICROS_SYSCALL_RETURN_CAPTURED
            || !vm_scratch_test_current_is(hart, vm)
            || vm_scratch_test_read_satp()
                != vm_scratch_test_expected_satp(
                    objects->processes[vm->process.slot]
                        .address_space_root
                )
        ) {
            return false;
        }
        context->test_phase = SCRATCH_PREEMPTION_VM_REACTIVATED;
        return true;
    }
    if (context->test_phase != SCRATCH_PREEMPTION_VM_REACTIVATED) {
        return true;
    }
    record = (
        handoff == NULL
        || context->scratch_process.slot >= MICROS_PROCESS_CAPACITY
    )
        ? NULL
        : &handoff->mappings[context->scratch_process.slot];
    if (
        hart == NULL
        || frame == NULL
        || objects == NULL
        || handoff == NULL
        || record == NULL
        || syscall_return != MICROS_SYSCALL_RETURN_NORMAL
        || !vm_scratch_test_current_is(hart, vm)
        || frame->a0 != MICROS_SYSCALL_ABI_OK
        || frame->a7 != MICROS_SYSCALL_ABI_VM_HANDOFF
        || handoff->load.scratch_phase != MICROS_VM_SCRATCH_CLEAR
        || handoff->load.load_token != context->scratch_load_token
        || record->entry_count != 1
        || record->entries[0].physical_address
            != context->scratch_physical_address
        || vm_scratch_test_read_satp()
            != vm_scratch_test_expected_satp(
                objects->processes[vm->process.slot]
                    .address_space_root
            )
    ) {
        return false;
    }
    context->test_phase = SCRATCH_PREEMPTION_WAITING_STALE_FAULT;
    return true;
}
