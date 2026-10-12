#include "kernel/user_address_space_internal.h"

#include "arch/riscv64/mmu.h"

static enum micros_user_address_space_error
require_read_authority(
    struct micros_user_address_space_runtime_view *view,
    const struct micros_frame_ownership **ledger,
    const struct micros_kernel_objects **objects,
    const struct micros_kernel_address_space_report **kernel_report
)
{
    enum micros_frame_owner_kind leaf_owner_kind;
    enum micros_user_address_space_error error =
        micros_user_address_space_runtime_require_read(view);

    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = micros_user_address_space_leaf_owner_kind(
        view->ownership_phase,
        &leaf_owner_kind
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    *ledger = view->ledger;
    *objects = view->objects;
    *kernel_report = view->kernel_report;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error resolve_process_with_root(
    struct micros_user_address_space_runtime_view *view,
    struct micros_process_handle process,
    const struct micros_process **resolved
)
{
    return micros_user_address_space_runtime_resolve_process(
        view,
        process,
        true,
        resolved
    );
}

static enum micros_user_address_space_error acquire_scratch(
    const struct micros_user_address_space_runtime_view *view,
    struct micros_user_address_space_scratch_lease *lease
)
{
    return micros_user_address_space_acquire_scratch(view, lease);
}

static void release_scratch(
    struct micros_user_address_space_scratch_lease *lease
)
{
    micros_user_address_space_release_scratch(lease);
}

static uint64_t expected_satp(uint64_t root)
{
    return micros_user_address_space_expected_satp(root);
}

enum micros_user_address_space_error
micros_user_address_space_activate(
    struct micros_process_handle process
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    uint64_t observed_satp;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    saved_status = micros_user_address_space_runtime_lock();
    error = require_read_authority(
        &runtime,
        &ledger,
        &objects,
        &kernel_report
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(&runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = micros_user_address_space_validate_locked(
        &runtime,
        &scratch,
        process,
        &resolved
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    observed_satp = micros_riscv_activate_sv39(
        resolved->address_space_root
    );
    if (observed_satp != expected_satp(resolved->address_space_root)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_SATP;
        goto done_with_scratch;
    }
    error = MICROS_USER_ADDRESS_SPACE_OK;

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)ledger;
    (void)objects;
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_activate_kernel(void)
{
    const struct micros_kernel_address_space_report *kernel_report =
        micros_kernel_address_space_report();
    uint64_t observed_satp;
    uintptr_t saved_status;

    if (kernel_report == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_INITIALIZED;
    }
    saved_status = micros_user_address_space_runtime_lock();
    observed_satp = micros_riscv_activate_sv39(
        kernel_report->root_physical_address
    );
    micros_user_address_space_runtime_unlock(saved_status);
    if (
        observed_satp
            != expected_satp(kernel_report->root_physical_address)
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_SATP;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}
