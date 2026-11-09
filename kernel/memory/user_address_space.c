#include "micros/user_address_space.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/memory/user_address_space_internal.h"
#include "kernel/objects/kernel_object_runtime_internal.h"
#include "micros/frame_allocator.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_object_runtime.h"

static struct micros_user_address_space_page_table *table_at(
    uint64_t physical_address
)
{
    return micros_user_address_space_table_at(physical_address);
}

static void clear_page(uint64_t physical_address)
{
    micros_user_address_space_clear_page(physical_address);
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

static uint64_t active_root(void)
{
    return micros_user_address_space_active_root();
}

static _Noreturn void panic_invariant(const char *reason)
{
    micros_user_address_space_panic_invariant(reason);
}

static enum micros_user_address_space_error
map_object_error(enum micros_kernel_object_error error)
{
    return micros_user_address_space_map_object_error(error);
}

static enum micros_user_address_space_error
map_ownership_error(enum micros_frame_ownership_error error)
{
    return micros_user_address_space_map_ownership_error(error);
}

static enum micros_user_address_space_error
require_bootstrap(
    struct micros_user_address_space_runtime_view *view,
    const struct micros_frame_ownership **ledger,
    const struct micros_kernel_objects **objects,
    const struct micros_kernel_address_space_report **kernel_report
)
{
    enum micros_user_address_space_error error =
        micros_user_address_space_runtime_require_bootstrap(view);

    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    *ledger = view->ledger;
    *objects = view->objects;
    *kernel_report = view->kernel_report;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error resolve_process(
    struct micros_user_address_space_runtime_view *view,
    struct micros_process_handle process,
    const struct micros_process **resolved
)
{
    return micros_user_address_space_runtime_resolve_process(
        view,
        process,
        false,
        resolved
    );
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

static bool physical_address_for_frame_index(
    const struct micros_frame_ownership *ledger,
    uint64_t frame_index,
    uint64_t *physical_address
)
{
    return micros_user_address_space_physical_address(
        ledger,
        frame_index,
        physical_address
    );
}

static void release_owned_frame_or_panic(
    struct micros_frame_owner owner,
    uint64_t physical_address
)
{
    if (
        micros_frame_ownership_runtime_release(
            owner,
            physical_address
        ) != MICROS_FRAME_OWNERSHIP_OK
    ) {
        panic_invariant("user-address-space-rollback");
    }
}

static enum micros_user_address_space_error make_process_owner(
    enum micros_frame_owner_kind kind,
    struct micros_process_handle process,
    struct micros_frame_owner *owner
)
{
    if (
        micros_frame_owner_make_process(kind, process, owner)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static void copy_shared_kernel_entries(
    struct micros_user_address_space_page_table *root,
    const struct micros_user_address_space_page_table *kernel_root
)
{
    size_t index;

    for (
        index = 0;
        index < MICROS_SV39_TABLE_ENTRY_COUNT;
        ++index
    ) {
        if (
            index != MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX
            && kernel_root->entries[index] != 0
        ) {
            root->entries[index] = kernel_root->entries[index];
        }
    }
}

enum micros_user_address_space_error
micros_user_address_space_create(struct micros_process_handle process)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    const struct micros_user_address_space_page_table *kernel_root;
    struct micros_user_address_space_page_table *root;
    struct micros_frame_owner owner;
    uint64_t root_physical_address;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    saved_status = micros_user_address_space_runtime_lock();
    error = require_bootstrap(
        &runtime,
        &ledger,
        &objects,
        &kernel_report
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process(&runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (resolved->address_space_root != 0) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_validate_metadata(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done_with_scratch;
    }
    kernel_root = table_at(kernel_report->root_physical_address);
    if (kernel_root->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX] != 0) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done_with_scratch;
    }
    error = make_process_owner(
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        process,
        &owner
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = micros_user_address_space_allocate_owned_frame(
        owner,
        &root_physical_address
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    clear_page(root_physical_address);
    root = table_at(root_physical_address);
    copy_shared_kernel_entries(root, kernel_root);
    {
        enum micros_kernel_object_error attach_error =
            micros_kernel_object_runtime_attach_address_space(
                process,
                root_physical_address
            );

        if (attach_error != MICROS_KERNEL_OBJECT_OK) {
            clear_page(root_physical_address);
            release_owned_frame_or_panic(
                owner,
                root_physical_address
            );
            error = map_object_error(attach_error);
            goto done_with_scratch;
        }
    }
    error = micros_user_address_space_validate_locked(
        &runtime,
        &scratch,
        process,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        if (
            micros_kernel_object_runtime_detach_address_space(
                process,
                root_physical_address
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            panic_invariant("user-address-space-create-detach");
        }
        clear_page(root_physical_address);
        release_owned_frame_or_panic(owner, root_physical_address);
    }

done_with_scratch:
    release_scratch(&scratch);
done:
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

static void clear_released_frames_or_panic(
    const struct micros_frame_ownership *ledger,
    const struct micros_user_address_space_scratch_lease *scratch
)
{
    const struct micros_frame_selection *selection = scratch->release_selection;
    uint64_t frame_index;
    uint64_t physical_address;
    uint64_t word;
    size_t position;
    size_t word_index;
    unsigned bit_index;

    for (position = 0; position < selection->word_count; ++position) {
        word_index = selection->words[position];
        word = selection->bitmap[word_index];
        for (bit_index = 0; bit_index < 64; ++bit_index) {
            if ((word & (UINT64_C(1) << bit_index)) == 0) {
                continue;
            }
            frame_index = (uint64_t)word_index * 64 + bit_index;
            if (!physical_address_for_frame_index(ledger, frame_index, &physical_address)) {
                panic_invariant("user-address-space-destroy-index");
            }
            clear_page(physical_address);
        }
    }
}

enum micros_user_address_space_error
micros_user_address_space_destroy(struct micros_process_handle process)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    struct micros_user_address_space_bootstrap_plan plan;
    struct micros_kernel_objects *mutable_objects;
    struct micros_process *mutable_process;
    uint64_t root_physical_address;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    saved_status = micros_user_address_space_runtime_lock();
    error = require_bootstrap(
        &runtime,
        &ledger,
        &objects,
        &kernel_report
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process(&runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(&runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = micros_user_address_space_acquire_selected_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = micros_user_address_space_collect_bootstrap_release_locked(
        &runtime,
        &scratch,
        process,
        &plan
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    resolved = plan.process;
    if (
        resolved->live_thread_count != 0
        || active_root() == resolved->address_space_root
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done_with_scratch;
    }
    root_physical_address = plan.root_physical_address;
    mutable_objects = micros_kernel_object_runtime_authoritative_registry();
    if (mutable_objects == NULL || mutable_objects != objects) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done_with_scratch;
    }
    mutable_process = &mutable_objects->processes[process.slot];
    if (mutable_process != resolved || mutable_process->address_space_root != root_physical_address
        || mutable_process->generation != process.generation) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done_with_scratch;
    }
    error = map_ownership_error(
        micros_frame_ownership_runtime_release_process_set(
            process,
            scratch.release_selection
        )
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    clear_released_frames_or_panic(ledger, &scratch);
    mutable_process->address_space_root = 0;
    error = MICROS_USER_ADDRESS_SPACE_OK;

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}
