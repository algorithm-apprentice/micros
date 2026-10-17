#include "kernel/bootstrap_runtime_internal.h"

#include "kernel/ipc_runtime_internal.h"
#include "kernel/objects/kernel_object_runtime_internal.h"
#include "kernel/pm_control_runtime.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/bootstrap_memory.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

enum micros_bootstrap_error bootstrap_preparation_reset(
    struct bootstrap_runtime_context *runtime
)
{
    bootstrap_runtime_clear_bytes(
        &runtime->prepared_vm_snapshot,
        sizeof(runtime->prepared_vm_snapshot)
    );
    if (
        micros_pm_control_runtime_reset()
            != MICROS_PM_CONTROL_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (
        micros_vm_handoff_runtime_reset()
            != MICROS_VM_HANDOFF_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (
        micros_tty_handoff_runtime_reset()
            != MICROS_TTY_HANDOFF_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    return MICROS_BOOTSTRAP_OK;
}

static bool release_page(
    struct micros_process_handle process,
    uint64_t virtual_address
)
{
    uint64_t physical_address;

    return micros_user_address_space_release_page(
        process,
        virtual_address,
        &physical_address
    ) == MICROS_USER_ADDRESS_SPACE_OK;
}

static bool rollback_record(
    struct preparation_record *record,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects
)
{
    uint32_t image_page;
    uint16_t stack_page;

    if (record->context_prepared) {
        if (
            micros_user_execution_detach(record->thread)
                != MICROS_USER_EXECUTION_OK
        ) {
            return false;
        }
        record->context_prepared = false;
    }
    if (record->thread_created) {
        if (
            micros_thread_release(objects, record->thread)
                != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        record->thread_created = false;
    }
    if (record->endpoint_reserved) {
        if (
            micros_endpoint_close(
                registry,
                objects,
                record->endpoint
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
        record->endpoint_reserved = false;
    }
    if (
        record->manifest_view_allocated
        && !release_page(
            record->process,
            MICROS_BOOTSTRAP_MANIFEST_VIEW
        )
    ) {
        return false;
    }
    record->manifest_view_allocated = false;
    for (
        stack_page = record->stack_pages_allocated;
        stack_page > 0;
        --stack_page
    ) {
        uint64_t address = MICROS_USER_VIRTUAL_END
            - (uint64_t)stack_page * MICROS_SV39_PAGE_SIZE;

        if (!release_page(record->process, address)) {
            return false;
        }
    }
    record->stack_pages_allocated = 0;
    for (
        image_page = record->image_pages_allocated;
        image_page > 0;
        --image_page
    ) {
        uint64_t address = MICROS_USER_VIRTUAL_BASE
            + ((uint64_t)image_page - 1)
                * MICROS_SV39_PAGE_SIZE;

        if (!release_page(record->process, address)) {
            return false;
        }
    }
    record->image_pages_allocated = 0;
    if (
        record->root_created
        && micros_user_address_space_destroy(record->process)
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }
    record->root_created = false;
    if (
        record->process_created
        && micros_process_release(objects, record->process)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    record->process_created = false;
    return true;
}

void bootstrap_preparation_rollback_preparation(
    struct bootstrap_runtime_context *runtime,
    size_t count
)
{
    struct micros_endpoint_registry *registry =
        micros_ipc_runtime_authoritative_registry();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();

    while (count > 0) {
        uint16_t manifest_index;

        --count;
        manifest_index = runtime->preparation_order[count];
        if (
            registry == NULL
            || objects == NULL
            || !rollback_record(
                &runtime->preparation_records[manifest_index],
                registry,
                objects
            )
        ) {
            bootstrap_runtime_panic_runtime("bootstrap-rollback-invariant");
        }
    }
    bootstrap_runtime_clear_bytes(
        runtime->preparation_records,
        sizeof(runtime->preparation_records)
    );
    bootstrap_runtime_clear_bytes(
        runtime->prepared_bindings,
        sizeof(runtime->prepared_bindings)
    );
    bootstrap_runtime_clear_bytes(
        runtime->preparation_order,
        sizeof(runtime->preparation_order)
    );
    bootstrap_runtime_clear_bytes(
        &runtime->prepared_vm_snapshot,
        sizeof(runtime->prepared_vm_snapshot)
    );
    if (
        micros_pm_control_runtime_reset()
            != MICROS_PM_CONTROL_OK
    ) {
        bootstrap_runtime_panic_runtime("bootstrap-pm-control-rollback");
    }
    if (
        micros_vm_handoff_runtime_reset()
            != MICROS_VM_HANDOFF_OK
    ) {
        bootstrap_runtime_panic_runtime("bootstrap-vm-handoff-rollback");
    }
    if (
        micros_tty_handoff_runtime_reset()
            != MICROS_TTY_HANDOFF_OK
    ) {
        bootstrap_runtime_panic_runtime("bootstrap-tty-handoff-rollback");
    }
    if (runtime->preparation_baseline.valid) {
        const struct micros_frame_allocator *allocator =
            micros_bootstrap_frame_allocator();
        const struct micros_frame_ownership *ownership =
            micros_frame_ownership_runtime_ledger();

        if (
            allocator == NULL
            || ownership == NULL
            || objects->live_process_count
                != runtime->preparation_baseline.live_process_count
            || objects->live_thread_count
                != runtime->preparation_baseline.live_thread_count
            || allocator->free_frame_count
                != runtime->preparation_baseline.free_frame_count
            || ownership->owned_frame_count
                != runtime->preparation_baseline.owned_frame_count
            || micros_kernel_objects_validate(objects)
                != MICROS_KERNEL_OBJECT_OK
            || micros_ipc_runtime_validate()
                != MICROS_ENDPOINT_OK
            || micros_frame_ownership_runtime_validate(objects)
                != MICROS_FRAME_OWNERSHIP_OK
        ) {
            bootstrap_runtime_panic_runtime("bootstrap-rollback-baseline");
        }
    }
}
