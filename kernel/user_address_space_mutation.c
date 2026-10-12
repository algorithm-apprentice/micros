#include "kernel/user_address_space_internal.h"

#include "micros/frame_allocator.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/sv39.h"

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

static void clear_release_bitmap(
    const struct micros_user_address_space_scratch_lease *lease
)
{
    micros_user_address_space_clear_bitmap(
        lease->release_bitmap,
        lease->word_count
    );
}

static bool scratch_bitmap_add(
    const struct micros_user_address_space_scratch_lease *lease,
    uint64_t *bitmap,
    uint64_t frame_index
)
{
    return micros_user_address_space_bitmap_add(
        bitmap,
        lease->word_count,
        frame_index
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

static uint64_t active_root(void)
{
    return micros_user_address_space_active_root();
}

static _Noreturn void panic_invariant(const char *reason)
{
    micros_user_address_space_panic_invariant(reason);
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

static bool user_virtual_address_is_valid(uint64_t virtual_address)
{
    return micros_user_address_space_user_page_is_valid(
        virtual_address
    );
}

static bool user_permissions_are_valid(uint32_t permissions)
{
    return micros_user_address_space_permissions_are_valid(
        permissions
    );
}

static bool frame_index_for_physical_address(
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    uint64_t *frame_index
)
{
    return micros_user_address_space_frame_index(
        ledger,
        physical_address,
        frame_index
    );
}

static enum micros_user_address_space_error decode_pte(
    uint64_t pte,
    struct micros_sv39_decoded_pte *decoded
)
{
    return micros_user_address_space_decode_pte(pte, decoded);
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

static bool table_has_other_entries(
    const struct micros_user_address_space_page_table *table,
    size_t excluded_index
)
{
    size_t index;

    for (
        index = 0;
        index < MICROS_SV39_TABLE_ENTRY_COUNT;
        ++index
    ) {
        if (index != excluded_index && table->entries[index] != 0) {
            return true;
        }
    }
    return false;
}

struct user_page_allocation {
    struct micros_process_handle process;
    const struct micros_process *resolved;
    struct micros_user_address_space_page_table *root;
    struct micros_user_address_space_page_table *middle;
    struct micros_user_address_space_page_table *leaf;
    struct micros_frame_owner table_owner;
    struct micros_frame_owner user_owner;
    uint64_t virtual_address;
    uint64_t new_middle;
    uint64_t new_leaf;
    uint64_t new_user;
    uint64_t root_pte;
    uint64_t middle_pte;
    uint64_t user_pte;
    uint32_t permissions;
    uint16_t root_index;
    uint16_t middle_index;
    uint16_t leaf_index;
    bool need_middle;
    bool need_leaf;
};

static enum micros_user_address_space_error
inspect_page_allocation(struct user_page_allocation *allocation)
{
    struct micros_sv39_decoded_pte root_entry;
    struct micros_sv39_decoded_pte middle_entry;
    struct micros_sv39_decoded_pte leaf_entry;
    enum micros_user_address_space_error error;

    if (
        micros_sv39_vpn_index(
            allocation->virtual_address,
            2,
            &allocation->root_index
        ) != MICROS_SV39_OK
        || allocation->root_index
            != MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX
        || micros_sv39_vpn_index(
            allocation->virtual_address,
            1,
            &allocation->middle_index
        ) != MICROS_SV39_OK
        || micros_sv39_vpn_index(
            allocation->virtual_address,
            0,
            &allocation->leaf_index
        ) != MICROS_SV39_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
    }
    allocation->root =
        table_at(allocation->resolved->address_space_root);
    error = decode_pte(
        allocation->root->entries[allocation->root_index],
        &root_entry
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    allocation->need_middle =
        root_entry.kind == MICROS_SV39_PTE_ABSENT;
    if (
        !allocation->need_middle
        && root_entry.kind != MICROS_SV39_PTE_TABLE
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    if (allocation->need_middle) {
        allocation->middle = NULL;
        allocation->need_leaf = true;
    } else {
        allocation->middle = table_at(root_entry.physical_address);
        error = decode_pte(
            allocation->middle->entries[allocation->middle_index],
            &middle_entry
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        allocation->need_leaf =
            middle_entry.kind == MICROS_SV39_PTE_ABSENT;
        if (
            !allocation->need_leaf
            && middle_entry.kind != MICROS_SV39_PTE_TABLE
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        }
    }
    if (allocation->need_leaf) {
        return MICROS_USER_ADDRESS_SPACE_OK;
    }
    allocation->leaf = table_at(middle_entry.physical_address);
    error = decode_pte(
        allocation->leaf->entries[allocation->leaf_index],
        &leaf_entry
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    return leaf_entry.kind == MICROS_SV39_PTE_ABSENT
        ? MICROS_USER_ADDRESS_SPACE_OK
        : MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT;
}

static enum micros_user_address_space_error
prepare_page_allocation(struct user_page_allocation *allocation)
{
    enum micros_user_address_space_error error;

    error = make_process_owner(
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        allocation->process,
        &allocation->table_owner
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = make_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        allocation->process,
        &allocation->user_owner
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (allocation->need_middle) {
        error = micros_user_address_space_allocate_owned_frame(
            allocation->table_owner,
            &allocation->new_middle
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        clear_page(allocation->new_middle);
        if (
            micros_sv39_make_table_pte(
                allocation->new_middle,
                &allocation->root_pte
            ) != MICROS_SV39_OK
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        }
        allocation->middle = table_at(allocation->new_middle);
    }
    if (allocation->need_leaf) {
        error = micros_user_address_space_allocate_owned_frame(
            allocation->table_owner,
            &allocation->new_leaf
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        clear_page(allocation->new_leaf);
        if (
            micros_sv39_make_table_pte(
                allocation->new_leaf,
                &allocation->middle_pte
            ) != MICROS_SV39_OK
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        }
        allocation->leaf = table_at(allocation->new_leaf);
    }
    error = micros_user_address_space_allocate_owned_frame(
        allocation->user_owner,
        &allocation->new_user
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    clear_page(allocation->new_user);
    if (
        micros_sv39_make_leaf_pte(
            allocation->new_user,
            allocation->permissions | MICROS_SV39_PERMISSION_USER,
            &allocation->user_pte
        ) != MICROS_SV39_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static void rollback_page_allocation(
    const struct user_page_allocation *allocation
)
{
    if (allocation->new_user != 0) {
        clear_page(allocation->new_user);
        release_owned_frame_or_panic(
            allocation->user_owner,
            allocation->new_user
        );
    }
    if (allocation->new_leaf != 0) {
        clear_page(allocation->new_leaf);
        release_owned_frame_or_panic(
            allocation->table_owner,
            allocation->new_leaf
        );
    }
    if (allocation->new_middle != 0) {
        clear_page(allocation->new_middle);
        release_owned_frame_or_panic(
            allocation->table_owner,
            allocation->new_middle
        );
    }
}

static void commit_page_allocation(
    const struct user_page_allocation *allocation,
    uint64_t *physical_address
)
{
    if (allocation->need_middle) {
        allocation->root->entries[allocation->root_index] =
            allocation->root_pte;
    }
    if (allocation->need_leaf) {
        allocation->middle->entries[allocation->middle_index] =
            allocation->middle_pte;
    }
    allocation->leaf->entries[allocation->leaf_index] =
        allocation->user_pte;
    *physical_address = allocation->new_user;
}

enum micros_user_address_space_error
micros_user_address_space_allocate_page(
    struct micros_process_handle process,
    uint64_t virtual_address,
    uint32_t permissions,
    uint64_t *physical_address
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    struct user_page_allocation allocation;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    if (physical_address == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
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
    if (!user_virtual_address_is_valid(virtual_address)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
        goto done;
    }
    if (!user_permissions_are_valid(permissions)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION;
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
    if (active_root() == resolved->address_space_root) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done_with_scratch;
    }
    allocation.process = process;
    allocation.resolved = resolved;
    allocation.virtual_address = virtual_address;
    allocation.permissions = permissions;
    allocation.new_middle = 0;
    allocation.new_leaf = 0;
    allocation.new_user = 0;
    error = inspect_page_allocation(&allocation);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = prepare_page_allocation(&allocation);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        rollback_page_allocation(&allocation);
        goto done_with_scratch;
    }
    commit_page_allocation(&allocation, physical_address);
    error = MICROS_USER_ADDRESS_SPACE_OK;

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)ledger;
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

struct user_page_release {
    const struct micros_frame_ownership *ledger;
    const struct micros_process *resolved;
    struct micros_user_address_space_scratch_lease *scratch;
    struct micros_process_handle process;
    struct micros_sv39_decoded_pte leaf_entry;
    struct micros_user_address_space_page_table *root;
    struct micros_user_address_space_page_table *middle;
    struct micros_user_address_space_page_table *leaf;
    uint64_t *leaf_pte;
    uint64_t middle_physical_address;
    uint64_t leaf_physical_address;
    uint16_t middle_index;
    uint16_t leaf_index;
    bool release_leaf_table;
    bool release_middle_table;
};

static enum micros_user_address_space_error inspect_page_release(
    struct user_page_release *release,
    uint64_t virtual_address
)
{
    enum micros_user_address_space_error error;

    error = micros_user_address_space_lookup_leaf_locked(
        release->resolved,
        virtual_address,
        &release->leaf_entry,
        &release->leaf_pte,
        &release->middle_physical_address,
        &release->leaf_physical_address
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (
        micros_sv39_vpn_index(
            virtual_address,
            1,
            &release->middle_index
        ) != MICROS_SV39_OK
        || micros_sv39_vpn_index(
            virtual_address,
            0,
            &release->leaf_index
        ) != MICROS_SV39_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
    }
    release->root = table_at(release->resolved->address_space_root);
    release->middle = table_at(release->middle_physical_address);
    release->leaf = table_at(release->leaf_physical_address);
    release->release_leaf_table =
        !table_has_other_entries(
            release->leaf,
            release->leaf_index
        );
    release->release_middle_table = (
        release->release_leaf_table
        && !table_has_other_entries(
            release->middle,
            release->middle_index
        )
    );
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error add_release_frame(
    struct user_page_release *release,
    uint64_t physical_address
)
{
    uint64_t frame_index;

    if (
        !frame_index_for_physical_address(
            release->ledger,
            physical_address,
            &frame_index
        )
        || !scratch_bitmap_add(
            release->scratch,
            release->scratch->release_bitmap,
            frame_index
        )
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
prepare_page_release_set(struct user_page_release *release)
{
    enum micros_user_address_space_error error;

    clear_release_bitmap(release->scratch);
    error = add_release_frame(
        release,
        release->leaf_entry.physical_address
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (release->release_leaf_table) {
        error = add_release_frame(
            release,
            release->leaf_physical_address
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
    }
    if (release->release_middle_table) {
        error = add_release_frame(
            release,
            release->middle_physical_address
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error release_page_frames(
    const struct user_page_release *release
)
{
    return map_ownership_error(
        micros_frame_ownership_runtime_release_process_set(
            release->process,
            release->scratch->release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
}

static void commit_page_release(
    const struct user_page_release *release,
    uint64_t *physical_address
)
{
    *release->leaf_pte = 0;
    if (release->release_leaf_table) {
        clear_page(release->leaf_physical_address);
        release->middle->entries[release->middle_index] = 0;
    }
    if (release->release_middle_table) {
        clear_page(release->middle_physical_address);
        release->root
            ->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX] = 0;
    }
    *physical_address = release->leaf_entry.physical_address;
}

enum micros_user_address_space_error
micros_user_address_space_release_page(
    struct micros_process_handle process,
    uint64_t virtual_address,
    uint64_t *physical_address
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    struct user_page_release release;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    if (physical_address == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
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
    if (!user_virtual_address_is_valid(virtual_address)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
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
    if (active_root() == resolved->address_space_root) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done_with_scratch;
    }
    release.ledger = ledger;
    release.resolved = resolved;
    release.scratch = &scratch;
    release.process = process;
    error = inspect_page_release(&release, virtual_address);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = prepare_page_release_set(&release);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = release_page_frames(&release);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    commit_page_release(&release, physical_address);

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
void micros_user_address_space_test_fail_after_allocations(
    size_t successful_allocations
)
{
    micros_user_address_space_test_set_allocation_limit(
        successful_allocations
    );
}
#endif
