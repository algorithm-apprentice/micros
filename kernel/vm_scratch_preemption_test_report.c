#include "kernel/vm_scratch_preemption_test_internal.h"

#include <stddef.h>
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/pm/pm_control_runtime.h"
#include "kernel/vm/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/vm.h"

struct scratch_report_authority {
    const struct micros_bootstrap_control_state *bootstrap;
    const struct micros_vm_handoff_state *handoff;
    const struct micros_pm_control_state *control;
    const struct micros_frame_ownership *ownership;
    const struct micros_endpoint_registry *registry;
    const struct micros_kernel_objects *objects;
    const struct micros_bootstrap_binding *vm;
    const struct micros_vm_kernel_mapping_record *record;
};

static struct scratch_report_authority acquire_report_authority(
    const struct vm_scratch_preemption_test_context *context
)
{
    struct scratch_report_authority authority;

    authority.bootstrap = micros_bootstrap_runtime_state();
    authority.handoff = micros_vm_handoff_runtime_state();
    authority.control = micros_pm_control_runtime_state();
    authority.ownership = micros_frame_ownership_runtime_ledger();
    authority.registry = micros_ipc_runtime_registry();
    authority.objects = micros_kernel_object_runtime_registry();
    authority.vm = authority.bootstrap == NULL
        ? NULL
        : micros_bootstrap_control_find_binding(
            authority.bootstrap, MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID
        );
    authority.record = authority.handoff == NULL
            || context->scratch_process.slot >= MICROS_PROCESS_CAPACITY
        ? NULL
        : &authority.handoff->mappings[context->scratch_process.slot];
    return authority;
}

static bool report_authority_is_complete(
    const struct vm_scratch_preemption_test_context *context,
    const struct scratch_report_authority *authority,
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    return (
        context->test_phase == SCRATCH_PREEMPTION_WAITING_STALE_FAULT
        && hart != NULL && frame != NULL
        && authority->bootstrap != NULL && authority->handoff != NULL
        && authority->control != NULL && authority->ownership != NULL
        && authority->registry != NULL && authority->objects != NULL
        && authority->vm != NULL && authority->record != NULL
    );
}

static bool report_fault_context_is_valid(
    const struct scratch_report_authority *authority,
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    return (
        frame->scause == MICROS_TEST_EXCEPTION_LOAD_PAGE_FAULT
        && frame->sepc == micros_vm_scratch_preemption_stale_load_fault_address
        && frame->stval == MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
        && frame->a0 == MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
        && vm_scratch_test_current_is(hart, authority->vm)
        && vm_scratch_test_read_satp()
            == vm_scratch_test_expected_satp(
                authority->objects->processes[authority->vm->process.slot]
                    .address_space_root
            )
    );
}

static bool report_load_record_is_valid(
    const struct vm_scratch_preemption_test_context *context,
    const struct scratch_report_authority *authority
)
{
    const struct micros_vm_handoff_state *handoff = authority->handoff;
    const struct micros_pm_control_state *control = authority->control;
    const struct micros_vm_kernel_mapping_record *record = authority->record;

    return (
        handoff->load.phase == MICROS_VM_LOAD_PHASE_LOADING
        && handoff->load.scratch_phase == MICROS_VM_SCRATCH_CLEAR
        && handoff->load.load_token == context->scratch_load_token
        && handoff->load.process.slot == context->scratch_process.slot
        && handoff->load.process.generation == context->scratch_process.generation
        && control->active_transaction == handoff->load.process_reservation_token
        && control->reserved_process.slot == context->scratch_process.slot
        && control->reserved_process.generation == context->scratch_process.generation
        && record->phase == MICROS_VM_KERNEL_MAPPING_LOADING
        && record->load_token == context->scratch_load_token
        && record->entry_count == 1
        && record->entries[0].virtual_address == MICROS_USER_VIRTUAL_BASE
        && record->entries[0].physical_address == context->scratch_physical_address
        && record->entries[0].permissions
            == (
                MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
            )
    );
}

static bool report_removed_mapping_is_valid(
    const struct vm_scratch_preemption_test_context *context,
    const struct scratch_report_authority *authority
)
{
    struct micros_frame_owner owner;
    uint64_t physical = UINT64_C(0x1122334455667788);
    uint32_t permissions = UINT32_C(0xa5a5a5a5);

    return (
        micros_user_address_space_lookup(
            authority->vm->process, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            &physical, &permissions
        ) == MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED
        && physical == UINT64_C(0x1122334455667788)
        && permissions == UINT32_C(0xa5a5a5a5)
        && micros_user_address_space_validate(authority->vm->process)
            == MICROS_USER_ADDRESS_SPACE_OK
        && micros_frame_ownership_lookup(
            authority->ownership, context->scratch_physical_address, &owner
        ) == MICROS_FRAME_OWNERSHIP_OK
        && owner.kind == MICROS_FRAME_OWNER_VM_TRANSFERABLE
        && owner.slot == 0 && owner.generation == 0 && owner.reserved == 0
        && *(const volatile uint64_t *)(uintptr_t)context->scratch_physical_address
            == MICROS_VM_SCRATCH_PREEMPTION_SENTINEL
        && micros_vm_handoff_runtime_validate(
            authority->bootstrap, authority->registry, authority->objects
        ) == MICROS_VM_HANDOFF_OK
        && micros_pm_control_runtime_validate(
            authority->bootstrap, authority->registry, authority->objects
        ) == MICROS_PM_CONTROL_OK
    );
}

_Noreturn void vm_scratch_test_report_success(
    const struct vm_scratch_preemption_test_context *context,
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct scratch_report_authority authority = acquire_report_authority(context);

    if (
        !report_authority_is_complete(context, &authority, hart, frame)
        || !report_fault_context_is_valid(&authority, hart, frame)
        || !report_load_record_is_valid(context, &authority)
        || !report_removed_mapping_is_valid(context, &authority)
    ) {
        vm_scratch_test_failure(hart, frame, "vm-scratch-preemption-final");
    }
    uart_write(
        "MICROS_VM_SCRATCH_PREEMPTION_TEST_PASS "
        "map=real timer=preempted roots=distinct "
        "inactive-validation=accepted reactivation=fenced "
        "sentinel=preserved commit=real stale-translation=faulted "
        "pte=absent ownership=transferable scratch=clear\n"
    );
    uart_flush();
    (void)sbi_system_reset(SBI_RESET_TYPE_SHUTDOWN, SBI_RESET_REASON_NONE);
    for (;;) {
        __asm__ volatile("wfi");
    }
}
