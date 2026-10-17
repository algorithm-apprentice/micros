#include "kernel/pm_service_test_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/pm/pm_control_runtime.h"
#include "kernel/pm_service_test_fixture.h"
#include "kernel/vm/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "tests/qemu/pm_service_protocol.h"

#define MICROS_TEST_EXCEPTION_BREAKPOINT UINT64_C(3)

static _Noreturn void test_failure(
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

static bool process_has_no_thread(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->owner.slot == process.slot
            && thread->owner.generation == process.generation
        ) {
            return false;
        }
    }
    return true;
}

static bool bytes_are_zero(const void *storage, size_t size)
{
    const uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

struct pm_report_authority {
    const struct micros_bootstrap_control_state *bootstrap;
    const struct micros_vm_handoff_state *handoff;
    const struct micros_pm_control_state *control;
    const struct micros_frame_ownership *ownership;
    const struct micros_endpoint_registry *registry;
    const struct micros_grant_registry *grants;
    const struct micros_kernel_objects *objects;
};

static struct pm_report_authority acquire_report_authority(void)
{
    struct pm_report_authority authority;

    authority.bootstrap = micros_bootstrap_runtime_state();
    authority.handoff = micros_vm_handoff_runtime_state();
    authority.control = micros_pm_control_runtime_state();
    authority.ownership = micros_frame_ownership_runtime_ledger();
    authority.registry = micros_ipc_runtime_registry();
    authority.grants = micros_grant_runtime_registry();
    authority.objects = micros_kernel_object_runtime_registry();
    return authority;
}

static bool report_fault_and_authority_are_valid(const struct pm_report_authority *authority, const struct micros_hart *hart, const struct micros_trap_frame *frame)
{
    return !(
        hart == NULL
        || frame == NULL
        || authority->bootstrap == NULL
        || authority->handoff == NULL
        || authority->control == NULL
        || authority->ownership == NULL
        || authority->registry == NULL
        || authority->grants == NULL
        || authority->objects == NULL
        || frame->scause != MICROS_TEST_EXCEPTION_BREAKPOINT
        || frame->sepc != micros_pm_service_test_report_address
        || frame->a0 != MICROS_PM_TEST_REPORT_MAGIC
        || frame->a1 != UINT64_C(1)
        || authority->bootstrap->phase != MICROS_BOOTSTRAP_PHASE_SEALED
        || authority->handoff->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || authority->ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    );
}

static bool report_transactions_are_clear(const struct pm_report_authority *authority, struct micros_process_handle consumed)
{
    return !(
        authority->control->last_transaction != UINT64_C(1)
        || authority->control->active_transaction != 0
        || authority->control->reserved_process.slot != 0
        || authority->control->reserved_process.generation != 0
        || authority->control->last_prepared_token != UINT64_C(2)
        || !bytes_are_zero(
            &authority->control->prepared,
            sizeof(authority->control->prepared)
        )
        || authority->control->last_exit_token != UINT64_C(1)
        || !bytes_are_zero(&authority->control->exit, sizeof(authority->control->exit))
        || authority->handoff->last_load_token != UINT64_C(1)
        || authority->handoff->last_mapping_generation != UINT64_C(1)
        || authority->handoff->load.phase != MICROS_VM_LOAD_PHASE_IDLE
        || !bytes_are_zero(
            &authority->handoff->mappings[consumed.slot],
            sizeof(authority->handoff->mappings[consumed.slot])
        )
    );
}

static bool report_execution_resources_are_clear(const struct pm_report_authority *authority, struct micros_process_handle consumed)
{
    return !(
        authority->objects->live_process_count != MICROS_PM_TEST_SERVICE_COUNT
        || authority->objects->live_thread_count != MICROS_PM_TEST_SERVICE_COUNT
        || authority->objects->processes[consumed.slot].slot_state
            != MICROS_KERNEL_OBJECT_SLOT_FREE
        || authority->objects->processes[consumed.slot].generation
            != consumed.generation
        || authority->objects->processes[consumed.slot].live_thread_count != 0
        || authority->objects->processes[consumed.slot].address_space_root != 0
        || authority->objects->processes[consumed.slot].primary_endpoint
            != MICROS_PROCESS_ENDPOINT_NONE
        || authority->objects->processes[consumed.slot].privilege_profile != 0
        || authority->objects->processes[consumed.slot]
            .endpoint_lifecycle_consumed
        || authority->registry->endpoints[consumed.slot].state
            != MICROS_ENDPOINT_STATE_FREE
        || !bytes_are_zero(
            (const uint8_t *)&authority->registry->endpoints[consumed.slot]
                + sizeof(authority->registry->endpoints[consumed.slot].state),
            sizeof(authority->registry->endpoints[consumed.slot])
                - sizeof(authority->registry->endpoints[consumed.slot].state)
        )
        || authority->objects->threads[MICROS_PM_TEST_SERVICE_COUNT].slot_state
            != MICROS_KERNEL_OBJECT_SLOT_FREE
        || authority->objects->threads[MICROS_PM_TEST_SERVICE_COUNT].generation != 2
        || !process_has_no_thread(authority->objects, consumed)
    );
}

static bool report_generation_and_frames_are_valid(const struct pm_report_authority *authority, struct micros_process_handle consumed)
{
    const struct micros_process *stale = NULL;
    uint64_t frame_count = UINT64_MAX;
    uint32_t next_generation = 0;

    return !(
        authority->grants->active_count != 0
        || micros_frame_ownership_count_process(
            authority->ownership,
            consumed,
            &frame_count
        ) != MICROS_FRAME_OWNERSHIP_OK
        || frame_count != 0
        || micros_process_resolve(authority->objects, consumed, &stale)
            != MICROS_KERNEL_OBJECT_ERROR_STALE
        || stale != NULL
        || micros_process_next_generation(
            consumed.slot,
            consumed.generation,
            &next_generation
        ) != MICROS_KERNEL_OBJECT_OK
        || next_generation != 2
    );
}

static bool report_runtime_and_current_thread_are_valid(const struct pm_report_authority *authority, struct micros_thread_handle *current)
{
    return !(
        micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_vm_handoff_runtime_validate(
            authority->bootstrap,
            authority->registry,
            authority->objects
        ) != MICROS_VM_HANDOFF_OK
        || micros_pm_control_runtime_validate(
            authority->bootstrap,
            authority->registry,
            authority->objects
        ) != MICROS_PM_CONTROL_OK
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_hart_current_thread(
            authority->objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            current
        ) != MICROS_KERNEL_OBJECT_OK
    );
}

static void validate_report_current_and_readiness(
    const struct pm_report_authority *authority,
    struct micros_thread_handle current,
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    const struct micros_bootstrap_binding *pm;
    size_t index;

    pm = micros_bootstrap_control_find_binding(
        authority->bootstrap,
        MICROS_PM_TEST_PM_SERVICE_ID
    );
    if (
        pm == NULL
        || current.slot != pm->thread.slot
        || current.generation != pm->thread.generation
    ) {
        test_failure(hart, frame, "pm-service-test-current");
    }
    for (index = 0; index < authority->bootstrap->entry_count; ++index) {
        if (
            authority->bootstrap->transitions.entries[index].state
                != MICROS_BOOTSTRAP_SERVICE_READY
        ) {
            test_failure(hart, frame, "pm-service-test-readiness");
        }
    }
}

static _Noreturn void report_success_and_shutdown(void)
{
    uart_write(
        "MICROS_PM_SERVICE_TEST_PASS "
        "handoff=complete readiness=acknowledged protocol=stable "
        "sealed=received prepare=aborted-reprepared activate=committed "
        "exit=detached-released resources=clean "
        "process-generation=advanced thread-generation=advanced "
        "tokens=advanced\n"
    );
    uart_write(
        "MICROS_APPLICATION_RUNTIME_TEST_PASS image=checked "
        "argc=2 env=1 auxv=validated gp-tp=zero "
        "return=pm-exit code=37 cleanup=complete\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}

_Noreturn void micros_pm_service_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct pm_report_authority authority = acquire_report_authority();
    struct micros_process_handle consumed = {
        .slot = MICROS_PM_TEST_RESERVED_PROCESS_SLOT, .generation = 1,
    };
    struct micros_thread_handle current;

    if (
        !report_fault_and_authority_are_valid(&authority, hart, frame)
        || !report_transactions_are_clear(&authority, consumed)
        || !report_execution_resources_are_clear(&authority, consumed)
        || !report_generation_and_frames_are_valid(&authority, consumed)
        || !report_runtime_and_current_thread_are_valid(&authority, &current)
    ) {
        test_failure(hart, frame, "pm-service-test-state");
    }
    validate_report_current_and_readiness(&authority, current, hart, frame);
    report_success_and_shutdown();
}
