#include "kernel/tests/vm/vm_scratch_preemption_test_internal.h"

#include <stddef.h>
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/pm/pm_control_runtime.h"
#include "kernel/vm/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/panic.h"
#include "micros/sv39.h"
#include "micros/vm.h"

_Noreturn void vm_scratch_test_failure(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
)
{
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        reason,
        frame
    );
}

uint64_t vm_scratch_test_read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

uint64_t vm_scratch_test_expected_satp(uint64_t root)
{
    return MICROS_RISCV_SATP_MODE_SV39 | (root >> 12);
}

bool vm_scratch_test_current_is(
    const struct micros_hart *hart,
    const struct micros_bootstrap_binding *binding
)
{
    return (
        hart != NULL
        && binding != NULL
        && hart->current_thread.slot == binding->thread.slot
        && hart->current_thread.generation
            == binding->thread.generation
    );
}

bool vm_scratch_test_runtime_is_valid(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_vm_handoff_state *handoff,
    const struct micros_pm_control_state *control,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    return (
        bootstrap != NULL
        && handoff != NULL
        && control != NULL
        && registry != NULL
        && objects != NULL
        && bootstrap->phase == MICROS_BOOTSTRAP_PHASE_SEALED
        && handoff->phase == MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        && handoff->load.phase == MICROS_VM_LOAD_PHASE_LOADING
        && handoff->load.scratch_phase
            == MICROS_VM_SCRATCH_MAPPED
        && handoff->load.process_reservation_token
            == control->active_transaction
        && control->active_transaction != 0
        && handoff->load.load_token != 0
        && handoff->load.scratch.virtual_address
            == MICROS_USER_VIRTUAL_BASE
        && handoff->load.scratch.permissions
            == (
                MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
            )
        && handoff->load.scratch.physical_address != 0
        && micros_bootstrap_runtime_validate()
            == MICROS_BOOTSTRAP_OK
        && micros_vm_handoff_runtime_validate(
            bootstrap,
            registry,
            objects
        ) == MICROS_VM_HANDOFF_OK
        && micros_pm_control_runtime_validate(
            bootstrap,
            registry,
            objects
        ) == MICROS_PM_CONTROL_OK
        && micros_grant_runtime_validate() == MICROS_GRANT_OK
        && micros_frame_ownership_runtime_validate(objects)
            == MICROS_FRAME_OWNERSHIP_OK
    );
}
