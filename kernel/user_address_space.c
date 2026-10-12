#include "micros/user_address_space.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/mmu.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/user_address_space_internal.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/frame_allocator.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/sv39.h"
#include "micros/user_address_space_core.h"
#include "micros/user_execution.h"

static bool process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
);

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size)
{
    uint8_t *output = destination;
    const uint8_t *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
static size_t test_fail_after_allocations = SIZE_MAX;
static size_t test_successful_allocations;
#endif

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

static bool bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

static bool scratch_bitmap_contains(
    const struct micros_user_address_space_scratch_lease *lease,
    const uint64_t *bitmap,
    uint64_t frame_index
)
{
    return micros_user_address_space_bitmap_contains(
        bitmap,
        lease->word_count,
        frame_index
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

static void clear_release_bitmap(
    const struct micros_user_address_space_scratch_lease *lease
)
{
    micros_user_address_space_clear_bitmap(
        lease->release_bitmap,
        lease->word_count
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

static enum micros_user_address_space_error
require_read_authority(
    struct micros_user_address_space_runtime_view *view,
    const struct micros_frame_ownership **ledger,
    const struct micros_kernel_objects **objects,
    const struct micros_kernel_address_space_report **kernel_report,
    enum micros_frame_owner_kind *leaf_owner_kind
)
{
    enum micros_frame_owner_kind observed_leaf_owner_kind;
    enum micros_user_address_space_error error =
        micros_user_address_space_runtime_require_read(view);

    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = micros_user_address_space_leaf_owner_kind(
        view->ownership_phase,
        &observed_leaf_owner_kind
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (leaf_owner_kind != NULL) {
        *leaf_owner_kind = observed_leaf_owner_kind;
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

static bool user_virtual_address_is_valid(uint64_t virtual_address)
{
    return micros_user_address_space_user_page_is_valid(
        virtual_address
    );
}

static bool user_virtual_byte_address_is_valid(uint64_t virtual_address)
{
    return micros_user_address_space_user_byte_address_is_valid(
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

static enum micros_user_address_space_error mark_owned_frame(
    const struct micros_frame_ownership *ledger,
    const struct micros_user_address_space_scratch_lease *lease,
    uint64_t physical_address,
    enum micros_user_address_space_frame_role role,
    struct micros_process_handle process
)
{
    struct micros_frame_owner observed;
    uint64_t frame_index;

    if (
        micros_frame_ownership_lookup(
            ledger,
            physical_address,
            &observed
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_user_address_space_validate_frame_owner(
            ledger->phase,
            role,
            process,
            observed
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || !frame_index_for_physical_address(
            ledger,
            physical_address,
            &frame_index
        )
        || scratch_bitmap_contains(
            lease,
            lease->reachable_bitmap,
            frame_index
        )
        || !scratch_bitmap_add(
            lease,
            lease->reachable_bitmap,
            frame_index
        )
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error decode_pte(
    uint64_t pte,
    struct micros_sv39_decoded_pte *decoded
)
{
    return micros_user_address_space_decode_pte(pte, decoded);
}

static enum micros_user_address_space_error
load_tty_device_authority(
    bool *active,
    struct micros_tty_device_authority *authority
)
{
    const struct micros_endpoint_registry *registry =
        micros_ipc_runtime_registry();
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    enum micros_tty_device_authority_status status;

    if (active == NULL || authority == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    *active = false;
    status = micros_tty_handoff_runtime_device_authority(
        registry,
        objects,
        authority
    );
    if (status == MICROS_TTY_DEVICE_AUTHORITY_NONE) {
        return MICROS_USER_ADDRESS_SPACE_OK;
    }
    if (status != MICROS_TTY_DEVICE_AUTHORITY_ACTIVE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    *active = true;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
load_vm_scratch_authority(
    bool *active,
    struct micros_user_address_space_vm_scratch_authority
        *authority
)
{
    enum micros_vm_scratch_authority_status status;

    if (active == NULL || authority == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    *active = false;
    clear_bytes(authority, sizeof(*authority));
    status = micros_vm_handoff_runtime_scratch_authority(
        authority
    );
    if (status == MICROS_VM_SCRATCH_AUTHORITY_NONE) {
        return MICROS_USER_ADDRESS_SPACE_OK;
    }
    if (status != MICROS_VM_SCRATCH_AUTHORITY_ACTIVE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    *active = true;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static bool leaf_matches_vm_scratch_authority(
    bool authority_active,
    const struct micros_user_address_space_vm_scratch_authority
        *authority,
    struct micros_process_handle process,
    uint64_t root_physical_address,
    uint64_t virtual_address,
    const struct micros_sv39_decoded_pte *leaf
)
{
    return (
        authority_active
        && authority != NULL
        && leaf != NULL
        && process_handles_equal(authority->process, process)
        && authority->root_physical_address
            == root_physical_address
        && authority->virtual_address == virtual_address
        && authority->physical_address == leaf->physical_address
        && leaf->permissions
            == (
                MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_USER
            )
        && leaf->accessed
        && leaf->dirty
    );
}

static enum micros_user_address_space_error
classify_tty_device_leaf(
    bool authority_active,
    const struct micros_tty_device_authority *authority,
    struct micros_process_handle process,
    uint64_t root_physical_address,
    uint64_t virtual_address,
    const struct micros_sv39_decoded_pte *leaf,
    bool *is_device
)
{
    enum micros_tty_device_leaf_class classification;

    if (leaf == NULL || is_device == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    *is_device = false;
    if (!authority_active) {
        return MICROS_USER_ADDRESS_SPACE_OK;
    }
    if (
        micros_tty_device_leaf_classify(
            authority,
            process,
            root_physical_address,
            virtual_address,
            leaf->physical_address,
            leaf->permissions,
            &classification
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    if (classification == MICROS_TTY_DEVICE_LEAF_INVALID) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    *is_device = classification == MICROS_TTY_DEVICE_LEAF_EXACT;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
reject_tty_device_leaf(
    struct micros_process_handle process,
    uint64_t root_physical_address,
    uint64_t virtual_address,
    const struct micros_sv39_decoded_pte *leaf
)
{
    struct micros_tty_device_authority authority;
    struct micros_user_address_space_vm_scratch_authority
        scratch_authority;
    enum micros_user_address_space_error error;
    bool authority_active;
    bool scratch_authority_active;
    bool is_device;

    error = load_vm_scratch_authority(
        &scratch_authority_active,
        &scratch_authority
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (
        leaf_matches_vm_scratch_authority(
            scratch_authority_active,
            &scratch_authority,
            process,
            root_physical_address,
            virtual_address
                - virtual_address % MICROS_SV39_PAGE_SIZE,
            leaf
        )
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    error = load_tty_device_authority(
        &authority_active,
        &authority
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = classify_tty_device_leaf(
        authority_active,
        &authority,
        process,
        root_physical_address,
        virtual_address
            - virtual_address % MICROS_SV39_PAGE_SIZE,
        leaf,
        &is_device
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    return is_device
        ? MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        : MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error mark_managed_leaf(
    const struct micros_frame_ownership *ledger,
    const struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    uint64_t root_physical_address,
    uint64_t virtual_address,
    const struct micros_sv39_decoded_pte *leaf,
    const struct micros_user_address_space_vm_scratch_authority
        *scratch_authority,
    enum micros_user_address_space_leaf_class *classification
)
{
    struct micros_frame_owner observed;
    uint64_t frame_index;
    enum micros_user_address_space_error error;

    if (
        micros_frame_ownership_lookup(
            ledger,
            leaf->physical_address,
            &observed
        ) != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    error = micros_user_address_space_classify_managed_leaf(
        ledger->phase,
        process,
        root_physical_address,
        virtual_address,
        leaf,
        observed,
        scratch_authority,
        classification
    );
    if (
        error != MICROS_USER_ADDRESS_SPACE_OK
        || !frame_index_for_physical_address(
            ledger,
            leaf->physical_address,
            &frame_index
        )
        || scratch_bitmap_contains(
            lease,
            lease->reachable_bitmap,
            frame_index
        )
        || !scratch_bitmap_add(
            lease,
            lease->reachable_bitmap,
            frame_index
        )
    ) {
        return error == MICROS_USER_ADDRESS_SPACE_OK
            ? MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
            : error;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error validate_locked(
    struct micros_user_address_space_runtime_view *runtime,
    const struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    const struct micros_process **resolved_process
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    const struct micros_user_address_space_page_table *kernel_root;
    const struct micros_user_address_space_page_table *root;
    struct micros_sv39_decoded_pte root_entry;
    struct micros_tty_device_authority device_authority;
    struct micros_user_address_space_vm_scratch_authority
        scratch_authority;
    enum micros_frame_owner_kind leaf_owner_kind;
    enum micros_user_address_space_error error;
    size_t root_index;
    uint64_t frame_index;
    bool device_authority_active;
    bool device_leaf_found = false;
    bool scratch_authority_active;
    bool scratch_leaf_found = false;

    micros_user_address_space_clear_bitmap(
        lease->reachable_bitmap,
        lease->word_count
    );
    error = require_read_authority(
        runtime,
        &ledger,
        &objects,
        &kernel_report,
        &leaf_owner_kind
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = load_vm_scratch_authority(
        &scratch_authority_active,
        &scratch_authority
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = load_tty_device_authority(
        &device_authority_active,
        &device_authority
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (
        micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    error = resolve_process(runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (resolved->address_space_root == 0) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
    }
    error = mark_owned_frame(
        ledger,
        lease,
        resolved->address_space_root,
        MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE,
        process
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }

    kernel_root = table_at(kernel_report->root_physical_address);
    root = table_at(resolved->address_space_root);
    if (kernel_root->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX] != 0) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    for (
        root_index = 0;
        root_index < MICROS_SV39_TABLE_ENTRY_COUNT;
        ++root_index
    ) {
        if (
            root_index != MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX
            && root->entries[root_index]
                != kernel_root->entries[root_index]
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        }
    }

    error = decode_pte(
        root->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX],
        &root_entry
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (root_entry.kind == MICROS_SV39_PTE_LEAF) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    if (root_entry.kind == MICROS_SV39_PTE_TABLE) {
        const struct micros_user_address_space_page_table *middle =
            table_at(root_entry.physical_address);
        size_t middle_index;

        error = mark_owned_frame(
            ledger,
            lease,
            root_entry.physical_address,
            MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE,
            process
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        for (
            middle_index = 0;
            middle_index < MICROS_SV39_TABLE_ENTRY_COUNT;
            ++middle_index
        ) {
            struct micros_sv39_decoded_pte middle_entry;

            error = decode_pte(
                middle->entries[middle_index],
                &middle_entry
            );
            if (error != MICROS_USER_ADDRESS_SPACE_OK) {
                return error;
            }
            if (middle_entry.kind == MICROS_SV39_PTE_ABSENT) {
                continue;
            }
            if (middle_entry.kind != MICROS_SV39_PTE_TABLE) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
            }
            error = mark_owned_frame(
                ledger,
                lease,
                middle_entry.physical_address,
                MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE,
                process
            );
            if (error != MICROS_USER_ADDRESS_SPACE_OK) {
                return error;
            }
            {
                const struct micros_user_address_space_page_table *leaf =
                    table_at(middle_entry.physical_address);
                size_t leaf_index;

                for (
                    leaf_index = 0;
                    leaf_index < MICROS_SV39_TABLE_ENTRY_COUNT;
                    ++leaf_index
                ) {
                    struct micros_sv39_decoded_pte leaf_entry;
                    uint64_t virtual_address;
                    enum micros_user_address_space_leaf_class
                        leaf_class;
                    bool is_device;

                    error = decode_pte(
                        leaf->entries[leaf_index],
                        &leaf_entry
                    );
                    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
                        return error;
                    }
                    if (leaf_entry.kind
                        == MICROS_SV39_PTE_ABSENT) {
                        continue;
                    }
                    if (
                        leaf_entry.kind != MICROS_SV39_PTE_LEAF
                        || (
                            leaf_entry.permissions
                            & MICROS_SV39_PERMISSION_USER
                        ) == 0
                    ) {
                        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
                    }
                    virtual_address =
                        MICROS_USER_VIRTUAL_BASE
                        + (
                            (
                                (uint64_t)middle_index
                                * MICROS_SV39_TABLE_ENTRY_COUNT
                            ) + leaf_index
                        ) * MICROS_SV39_PAGE_SIZE;
                    error = classify_tty_device_leaf(
                        device_authority_active,
                        &device_authority,
                        process,
                        resolved->address_space_root,
                        virtual_address,
                        &leaf_entry,
                        &is_device
                    );
                    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
                        return error;
                    }
                    if (is_device) {
                        if (device_leaf_found) {
                            return
                                MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
                        }
                        device_leaf_found = true;
                        continue;
                    }
                    error = mark_managed_leaf(
                        ledger,
                        lease,
                        process,
                        resolved->address_space_root,
                        virtual_address,
                        &leaf_entry,
                        scratch_authority_active
                            ? &scratch_authority
                            : NULL,
                        &leaf_class
                    );
                    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
                        return error;
                    }
                    if (
                        leaf_class
                            == MICROS_USER_ADDRESS_SPACE_LEAF_VM_SCRATCH
                    ) {
                        if (scratch_leaf_found) {
                            return
                                MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
                        }
                        scratch_leaf_found = true;
                    }
                }
            }
        }
    }

    if (
        device_authority_active
        && micros_tty_device_leaf_required(
            &device_authority,
            process,
            resolved->address_space_root
        )
        && !device_leaf_found
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    if (
        scratch_authority_active
        && process_handles_equal(
            scratch_authority.process,
            process
        )
        && scratch_authority.root_physical_address
            == resolved->address_space_root
        && !scratch_leaf_found
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        struct micros_frame_owner owner =
            ledger->owners[frame_index];
        bool exact_process = (
            owner.slot == process.slot
            && owner.generation == process.generation
        );

        if (
            exact_process
            && (
                owner.kind
                    == MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
                || owner.kind == leaf_owner_kind
            )
        ) {
            if (
                !scratch_bitmap_contains(
                    lease,
                    lease->reachable_bitmap,
                    frame_index
                )
            ) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            }
        } else if (
            exact_process
            && owner.kind != MICROS_FRAME_OWNER_FREE
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
    }
    if (resolved_process != NULL) {
        *resolved_process = resolved;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
validate_all_process_roots_locked(
    struct micros_user_address_space_runtime_view *runtime,
    const struct micros_kernel_objects *objects,
    const struct micros_user_address_space_scratch_lease *lease
)
{
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_process *record =
            &objects->processes[index];
        struct micros_process_handle process;
        enum micros_user_address_space_error error;

        if (
            record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || record->address_space_root == 0
        ) {
            continue;
        }
        process = (struct micros_process_handle){
            .slot = (uint16_t)index,
            .generation = record->generation,
        };
        error = validate_locked(runtime, lease, process, NULL);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
visit_user_mappings_locked(
    struct micros_process_handle process_handle,
    const struct micros_process *process,
    micros_user_mapping_visitor visitor,
    void *context,
    size_t *mapping_count
)
{
    const struct micros_user_address_space_page_table *root =
        table_at(process->address_space_root);
    struct micros_sv39_decoded_pte root_entry;
    struct micros_tty_device_authority device_authority;
    struct micros_user_address_space_vm_scratch_authority
        scratch_authority;
    size_t count = 0;
    enum micros_user_address_space_error error;
    bool device_authority_active;
    bool scratch_authority_active;

    error = load_tty_device_authority(
        &device_authority_active,
        &device_authority
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = load_vm_scratch_authority(
        &scratch_authority_active,
        &scratch_authority
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }

    error = decode_pte(
        root->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX],
        &root_entry
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (root_entry.kind == MICROS_SV39_PTE_ABSENT) {
        *mapping_count = 0;
        return MICROS_USER_ADDRESS_SPACE_OK;
    }
    if (root_entry.kind != MICROS_SV39_PTE_TABLE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    {
        const struct micros_user_address_space_page_table *middle =
            table_at(root_entry.physical_address);
        size_t middle_index;

        for (
            middle_index = 0;
            middle_index < MICROS_SV39_TABLE_ENTRY_COUNT;
            ++middle_index
        ) {
            struct micros_sv39_decoded_pte middle_entry;

            error = decode_pte(
                middle->entries[middle_index],
                &middle_entry
            );
            if (error != MICROS_USER_ADDRESS_SPACE_OK) {
                return error;
            }
            if (middle_entry.kind == MICROS_SV39_PTE_ABSENT) {
                continue;
            }
            if (middle_entry.kind != MICROS_SV39_PTE_TABLE) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
            }
            {
                const struct micros_user_address_space_page_table *leaf =
                    table_at(middle_entry.physical_address);
                size_t leaf_index;

                for (
                    leaf_index = 0;
                    leaf_index < MICROS_SV39_TABLE_ENTRY_COUNT;
                    ++leaf_index
                ) {
                    struct micros_sv39_decoded_pte leaf_entry;
                    uint64_t virtual_address;
                    bool is_device;

                    error = decode_pte(
                        leaf->entries[leaf_index],
                        &leaf_entry
                    );
                    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
                        return error;
                    }
                    if (
                        leaf_entry.kind
                            == MICROS_SV39_PTE_ABSENT
                    ) {
                        continue;
                    }
                    if (
                        leaf_entry.kind != MICROS_SV39_PTE_LEAF
                        || (
                            leaf_entry.permissions
                            & MICROS_SV39_PERMISSION_USER
                        ) == 0
                        || count == SIZE_MAX
                    ) {
                        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
                    }
                    virtual_address =
                        MICROS_USER_VIRTUAL_BASE
                        + (
                            (
                                (uint64_t)middle_index
                                * MICROS_SV39_TABLE_ENTRY_COUNT
                            ) + leaf_index
                        ) * MICROS_SV39_PAGE_SIZE;
                    error = classify_tty_device_leaf(
                        device_authority_active,
                        &device_authority,
                        process_handle,
                        process->address_space_root,
                        virtual_address,
                        &leaf_entry,
                        &is_device
                    );
                    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
                        return error;
                    }
                    if (is_device) {
                        continue;
                    }
                    if (
                        leaf_matches_vm_scratch_authority(
                            scratch_authority_active,
                            &scratch_authority,
                            process_handle,
                            process->address_space_root,
                            virtual_address,
                            &leaf_entry
                        )
                    ) {
                        continue;
                    }
                    if (
                        !visitor(
                            context,
                            virtual_address,
                            leaf_entry.physical_address,
                            leaf_entry.permissions
                                & ~MICROS_SV39_PERMISSION_USER
                        )
                    ) {
                        return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
                    }
                    ++count;
                }
            }
        }
    }
    *mapping_count = count;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error lookup_leaf_locked(
    const struct micros_process *process,
    uint64_t virtual_address,
    struct micros_sv39_decoded_pte *leaf_entry,
    uint64_t **leaf_pte,
    uint64_t *middle_physical_address,
    uint64_t *leaf_physical_address
)
{
    struct micros_user_address_space_page_table *root =
        table_at(process->address_space_root);
    struct micros_sv39_decoded_pte root_entry;
    struct micros_sv39_decoded_pte middle_entry;
    struct micros_user_address_space_page_table *middle;
    struct micros_user_address_space_page_table *leaf;
    uint16_t root_index;
    uint16_t middle_index;
    uint16_t leaf_index;
    enum micros_user_address_space_error error;

    if (
        micros_sv39_vpn_index(
            virtual_address,
            2,
            &root_index
        ) != MICROS_SV39_OK
        || root_index != MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX
        || micros_sv39_vpn_index(
            virtual_address,
            1,
            &middle_index
        ) != MICROS_SV39_OK
        || micros_sv39_vpn_index(
            virtual_address,
            0,
            &leaf_index
        ) != MICROS_SV39_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
    }
    error = decode_pte(root->entries[root_index], &root_entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (root_entry.kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    if (root_entry.kind != MICROS_SV39_PTE_TABLE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    middle = table_at(root_entry.physical_address);
    error = decode_pte(
        middle->entries[middle_index],
        &middle_entry
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (middle_entry.kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    if (middle_entry.kind != MICROS_SV39_PTE_TABLE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    leaf = table_at(middle_entry.physical_address);
    error = decode_pte(leaf->entries[leaf_index], leaf_entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (leaf_entry->kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    if (leaf_entry->kind != MICROS_SV39_PTE_LEAF) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    if (leaf_pte != NULL) {
        *leaf_pte = &leaf->entries[leaf_index];
    }
    if (middle_physical_address != NULL) {
        *middle_physical_address = root_entry.physical_address;
    }
    if (leaf_physical_address != NULL) {
        *leaf_physical_address = middle_entry.physical_address;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
locate_leaf_slot_locked(
    const struct micros_process *process,
    uint64_t virtual_address,
    struct micros_sv39_decoded_pte *leaf_entry,
    uint64_t **leaf_pte,
    uint64_t *leaf_table_physical_address
)
{
    struct micros_user_address_space_page_table *root =
        table_at(process->address_space_root);
    struct micros_sv39_decoded_pte root_entry;
    struct micros_sv39_decoded_pte middle_entry;
    struct micros_user_address_space_page_table *middle;
    struct micros_user_address_space_page_table *leaf;
    uint16_t root_index;
    uint16_t middle_index;
    uint16_t leaf_index;
    enum micros_user_address_space_error error;

    if (
        leaf_entry == NULL
        || leaf_pte == NULL
        || leaf_table_physical_address == NULL
        || micros_sv39_vpn_index(
            virtual_address,
            2,
            &root_index
        ) != MICROS_SV39_OK
        || root_index != MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX
        || micros_sv39_vpn_index(
            virtual_address,
            1,
            &middle_index
        ) != MICROS_SV39_OK
        || micros_sv39_vpn_index(
            virtual_address,
            0,
            &leaf_index
        ) != MICROS_SV39_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
    }
    error = decode_pte(root->entries[root_index], &root_entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (root_entry.kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    if (root_entry.kind != MICROS_SV39_PTE_TABLE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    middle = table_at(root_entry.physical_address);
    error = decode_pte(
        middle->entries[middle_index],
        &middle_entry
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (middle_entry.kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    if (middle_entry.kind != MICROS_SV39_PTE_TABLE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    leaf = table_at(middle_entry.physical_address);
    error = decode_pte(leaf->entries[leaf_index], leaf_entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    *leaf_pte = &leaf->entries[leaf_index];
    *leaf_table_physical_address = middle_entry.physical_address;
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

static enum micros_user_address_space_error allocate_owned_frame(
    struct micros_frame_owner owner,
    uint64_t *physical_address
)
{
#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
    if (test_successful_allocations >= test_fail_after_allocations) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ALLOCATION;
    }
#endif
    {
        enum micros_frame_ownership_error error =
            micros_frame_ownership_runtime_allocate(
                owner,
                physical_address
            );

        if (error != MICROS_FRAME_OWNERSHIP_OK) {
            return map_ownership_error(error);
        }
    }
#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
    ++test_successful_allocations;
#endif
    return MICROS_USER_ADDRESS_SPACE_OK;
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
    size_t index;

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
        micros_frame_ownership_runtime_validate(objects)
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
    error = allocate_owned_frame(owner, &root_physical_address);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    clear_page(root_physical_address);
    root = table_at(root_physical_address);
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
    error = validate_locked(&runtime, &scratch, process, NULL);
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
    struct micros_user_address_space_page_table *root;
    struct micros_user_address_space_page_table *middle;
    struct micros_user_address_space_page_table *leaf;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    struct micros_sv39_decoded_pte root_entry;
    struct micros_sv39_decoded_pte middle_entry;
    struct micros_sv39_decoded_pte leaf_entry;
    struct micros_frame_owner table_owner;
    struct micros_frame_owner user_owner;
    uint64_t new_middle = 0;
    uint64_t new_leaf = 0;
    uint64_t new_user = 0;
    uint64_t root_pte = 0;
    uint64_t middle_pte = 0;
    uint64_t user_pte = 0;
    uint16_t root_index;
    uint16_t middle_index;
    uint16_t leaf_index;
    bool need_middle;
    bool need_leaf;
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
    error = validate_locked(
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
    if (
        micros_sv39_vpn_index(
            virtual_address,
            2,
            &root_index
        ) != MICROS_SV39_OK
        || root_index != MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX
        || micros_sv39_vpn_index(
            virtual_address,
            1,
            &middle_index
        ) != MICROS_SV39_OK
        || micros_sv39_vpn_index(
            virtual_address,
            0,
            &leaf_index
        ) != MICROS_SV39_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
        goto done_with_scratch;
    }
    root = table_at(resolved->address_space_root);
    error = decode_pte(root->entries[root_index], &root_entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    need_middle = root_entry.kind == MICROS_SV39_PTE_ABSENT;
    if (!need_middle && root_entry.kind != MICROS_SV39_PTE_TABLE) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        goto done_with_scratch;
    }
    if (need_middle) {
        middle = NULL;
        need_leaf = true;
    } else {
        middle = table_at(root_entry.physical_address);
        error = decode_pte(
            middle->entries[middle_index],
            &middle_entry
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto done_with_scratch;
        }
        need_leaf = middle_entry.kind == MICROS_SV39_PTE_ABSENT;
        if (
            !need_leaf
            && middle_entry.kind != MICROS_SV39_PTE_TABLE
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
            goto done_with_scratch;
        }
    }
    if (!need_leaf) {
        leaf = table_at(middle_entry.physical_address);
        error = decode_pte(leaf->entries[leaf_index], &leaf_entry);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto done_with_scratch;
        }
        if (leaf_entry.kind != MICROS_SV39_PTE_ABSENT) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT;
            goto done_with_scratch;
        }
    }

    error = make_process_owner(
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        process,
        &table_owner
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = make_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        process,
        &user_owner
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (need_middle) {
        error = allocate_owned_frame(table_owner, &new_middle);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto rollback;
        }
        clear_page(new_middle);
        if (
            micros_sv39_make_table_pte(new_middle, &root_pte)
                != MICROS_SV39_OK
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
            goto rollback;
        }
        middle = table_at(new_middle);
    }
    if (need_leaf) {
        error = allocate_owned_frame(table_owner, &new_leaf);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto rollback;
        }
        clear_page(new_leaf);
        if (
            micros_sv39_make_table_pte(new_leaf, &middle_pte)
                != MICROS_SV39_OK
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
            goto rollback;
        }
        leaf = table_at(new_leaf);
    }
    error = allocate_owned_frame(user_owner, &new_user);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto rollback;
    }
    clear_page(new_user);
    if (
        micros_sv39_make_leaf_pte(
            new_user,
            permissions | MICROS_SV39_PERMISSION_USER,
            &user_pte
        ) != MICROS_SV39_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        goto rollback;
    }

    if (need_middle) {
        root->entries[root_index] = root_pte;
    }
    if (need_leaf) {
        middle->entries[middle_index] = middle_pte;
    }
    leaf->entries[leaf_index] = user_pte;
    *physical_address = new_user;
    error = MICROS_USER_ADDRESS_SPACE_OK;
    goto done_with_scratch;

rollback:
    if (new_user != 0) {
        clear_page(new_user);
        release_owned_frame_or_panic(user_owner, new_user);
    }
    if (new_leaf != 0) {
        clear_page(new_leaf);
        release_owned_frame_or_panic(table_owner, new_leaf);
    }
    if (new_middle != 0) {
        clear_page(new_middle);
        release_owned_frame_or_panic(table_owner, new_middle);
    }

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)ledger;
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_lookup(
    struct micros_process_handle process,
    uint64_t virtual_address,
    uint64_t *physical_address,
    uint32_t *permissions
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_sv39_decoded_pte leaf_entry;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    if (physical_address == NULL || permissions == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    saved_status = micros_user_address_space_runtime_lock();
    error = require_read_authority(
        &runtime,
        &ledger,
        &objects,
        &kernel_report,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(&runtime, process, &resolved);
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
    error = validate_locked(
        &runtime,
        &scratch,
        process,
        &resolved
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = lookup_leaf_locked(
        resolved,
        virtual_address,
        &leaf_entry,
        NULL,
        NULL,
        NULL
    );
    if (error == MICROS_USER_ADDRESS_SPACE_OK) {
        error = reject_tty_device_leaf(
            process,
            resolved->address_space_root,
            virtual_address,
            &leaf_entry
        );
        if (error == MICROS_USER_ADDRESS_SPACE_OK) {
            *physical_address = leaf_entry.physical_address;
            *permissions = leaf_entry.permissions
                & ~MICROS_SV39_PERMISSION_USER;
        }
    }

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)ledger;
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_translate(
    struct micros_process_handle process,
    uint64_t user_address,
    uint64_t *physical_address,
    uint32_t *permissions,
    size_t *contiguous_bytes
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_sv39_decoded_pte leaf_entry;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    enum micros_user_address_space_error error;
    uint64_t offset;
    uintptr_t saved_status;

    if (
        physical_address == NULL
        || permissions == NULL
        || contiguous_bytes == NULL
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    saved_status = micros_user_address_space_runtime_lock();
    error = require_read_authority(
        &runtime,
        &ledger,
        &objects,
        &kernel_report,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(&runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (!user_virtual_byte_address_is_valid(user_address)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = validate_locked(
        &runtime,
        &scratch,
        process,
        &resolved
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = lookup_leaf_locked(
        resolved,
        user_address,
        &leaf_entry,
        NULL,
        NULL,
        NULL
    );
    if (error == MICROS_USER_ADDRESS_SPACE_OK) {
        error = reject_tty_device_leaf(
            process,
            resolved->address_space_root,
            user_address,
            &leaf_entry
        );
        if (error == MICROS_USER_ADDRESS_SPACE_OK) {
            offset = user_address % MICROS_SV39_PAGE_SIZE;
            *physical_address = leaf_entry.physical_address + offset;
            *permissions =
                leaf_entry.permissions
                & ~MICROS_SV39_PERMISSION_USER;
            *contiguous_bytes =
                (size_t)(MICROS_SV39_PAGE_SIZE - offset);
        }
    }

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)ledger;
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_inventory(
    struct micros_process_handle process,
    micros_user_mapping_visitor visitor,
    void *context,
    uint64_t *root_physical_address,
    size_t *mapping_count
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    size_t observed_mapping_count;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    if (
        visitor == NULL
        || root_physical_address == NULL
        || mapping_count == NULL
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    saved_status = micros_user_address_space_runtime_lock();
    error = require_read_authority(
        &runtime,
        &ledger,
        &objects,
        &kernel_report,
        NULL
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
    error = validate_locked(
        &runtime,
        &scratch,
        process,
        &resolved
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = visit_user_mappings_locked(
        process,
        resolved,
        visitor,
        context,
        &observed_mapping_count
    );
    if (error == MICROS_USER_ADDRESS_SPACE_OK) {
        *root_physical_address = resolved->address_space_root;
        *mapping_count = observed_mapping_count;
    }

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)ledger;
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
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
    struct micros_sv39_decoded_pte leaf_entry;
    struct micros_user_address_space_page_table *root;
    struct micros_user_address_space_page_table *middle;
    struct micros_user_address_space_page_table *leaf;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    uint64_t *leaf_pte;
    uint64_t middle_physical_address;
    uint64_t leaf_physical_address;
    uint64_t frame_index;
    uint16_t middle_index;
    uint16_t leaf_index;
    bool release_leaf_table;
    bool release_middle_table;
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
    error = validate_locked(
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
    error = lookup_leaf_locked(
        resolved,
        virtual_address,
        &leaf_entry,
        &leaf_pte,
        &middle_physical_address,
        &leaf_physical_address
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (
        micros_sv39_vpn_index(
            virtual_address,
            1,
            &middle_index
        ) != MICROS_SV39_OK
        || micros_sv39_vpn_index(
            virtual_address,
            0,
            &leaf_index
        ) != MICROS_SV39_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
        goto done_with_scratch;
    }
    root = table_at(resolved->address_space_root);
    middle = table_at(middle_physical_address);
    leaf = table_at(leaf_physical_address);
    release_leaf_table = !table_has_other_entries(leaf, leaf_index);
    release_middle_table = (
        release_leaf_table
        && !table_has_other_entries(middle, middle_index)
    );

    clear_release_bitmap(&scratch);
    if (
        !frame_index_for_physical_address(
            ledger,
            leaf_entry.physical_address,
            &frame_index
        )
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done_with_scratch;
    }
    if (
        !scratch_bitmap_add(
            &scratch,
            scratch.release_bitmap,
            frame_index
        )
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done_with_scratch;
    }
    if (release_leaf_table) {
        if (
            !frame_index_for_physical_address(
                ledger,
                leaf_physical_address,
                &frame_index
            )
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
            goto done_with_scratch;
        }
        if (
            !scratch_bitmap_add(
                &scratch,
                scratch.release_bitmap,
                frame_index
            )
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
            goto done_with_scratch;
        }
    }
    if (release_middle_table) {
        if (
            !frame_index_for_physical_address(
                ledger,
                middle_physical_address,
                &frame_index
            )
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
            goto done_with_scratch;
        }
        if (
            !scratch_bitmap_add(
                &scratch,
                scratch.release_bitmap,
                frame_index
            )
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
            goto done_with_scratch;
        }
    }
    error = map_ownership_error(
        micros_frame_ownership_runtime_release_process_set(
            process,
            scratch.release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }

    *leaf_pte = 0;
    if (release_leaf_table) {
        clear_page(leaf_physical_address);
        middle->entries[middle_index] = 0;
    }
    if (release_middle_table) {
        clear_page(middle_physical_address);
        root->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX] = 0;
    }
    *physical_address = leaf_entry.physical_address;

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

static enum micros_user_address_space_error
copy_reachable_frames_to_release_set(
    const struct micros_frame_ownership *ledger,
    struct micros_user_address_space_scratch_lease *scratch
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        if (
            scratch_bitmap_contains(
                scratch,
                scratch->reachable_bitmap,
                frame_index
            )
            && !scratch_bitmap_add(
                scratch,
                scratch->release_bitmap,
                frame_index
            )
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static void clear_released_frames_or_panic(
    const struct micros_frame_ownership *ledger,
    const struct micros_user_address_space_scratch_lease *scratch
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        uint64_t physical_address;

        if (
            !scratch_bitmap_contains(
                scratch,
                scratch->release_bitmap,
                frame_index
            )
        ) {
            continue;
        }
        if (
            !physical_address_for_frame_index(
                ledger,
                frame_index,
                &physical_address
            )
        ) {
            panic_invariant("user-address-space-destroy-index");
        }
        clear_page(physical_address);
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
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = validate_locked(
        &runtime,
        &scratch,
        process,
        &resolved
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (
        resolved->live_thread_count != 0
        || active_root() == resolved->address_space_root
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done_with_scratch;
    }
    root_physical_address = resolved->address_space_root;
    error = copy_reachable_frames_to_release_set(
        ledger,
        &scratch
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = map_ownership_error(
        micros_frame_ownership_runtime_release_process_set(
            process,
            scratch.release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    clear_released_frames_or_panic(ledger, &scratch);
    if (
        micros_kernel_object_runtime_detach_address_space(
            process,
            root_physical_address
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        panic_invariant("user-address-space-destroy-detach");
    }
    error = MICROS_USER_ADDRESS_SPACE_OK;

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

static bool process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return micros_user_address_space_process_handles_equal(
        left,
        right
    );
}

static bool find_one_prepared_thread(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    struct micros_thread_handle *thread_handle
)
{
    size_t count = 0;
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !process_handles_equal(thread->owner, process)
        ) {
            continue;
        }
        if (!thread->context_attached) {
            return false;
        }
        *thread_handle = (struct micros_thread_handle){
            .slot = (uint16_t)index,
            .generation = thread->generation,
        };
        ++count;
    }
    return count == 1;
}

static bool process_has_no_owned_frames(
    const struct micros_frame_ownership *ledger,
    struct micros_process_handle process
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        const struct micros_frame_owner *owner =
            &ledger->owners[frame_index];

        if (
            owner->slot == process.slot
            && owner->generation == process.generation
            && owner->kind != MICROS_FRAME_OWNER_FREE
        ) {
            return false;
        }
    }
    return true;
}

static bool reachable_user_targets_are_wired(
    const struct micros_frame_ownership *ledger,
    const struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        const struct micros_frame_owner *owner =
            &ledger->owners[frame_index];

        if (
            scratch_bitmap_contains(
                lease,
                lease->reachable_bitmap,
                frame_index
            )
            && owner->kind == MICROS_FRAME_OWNER_PROCESS_USER
            && owner->slot == process.slot
            && owner->generation == process.generation
            && ledger->handoff_targets[frame_index]
                != MICROS_FRAME_HANDOFF_VM_WIRED
        ) {
            return false;
        }
    }
    return true;
}

static enum micros_user_address_space_error
select_reachable_process_user_targets(
    const struct micros_frame_ownership *ledger,
    struct micros_user_address_space_scratch_lease *scratch,
    struct micros_process_handle process
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        const struct micros_frame_owner *owner =
            &ledger->owners[frame_index];

        if (
            !scratch_bitmap_contains(
                scratch,
                scratch->reachable_bitmap,
                frame_index
            )
            || owner->kind != MICROS_FRAME_OWNER_PROCESS_USER
        ) {
            continue;
        }
        if (
            owner->slot != process.slot
            || owner->generation != process.generation
            || scratch_bitmap_contains(
                scratch,
                scratch->release_bitmap,
                frame_index
            )
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
        if (
            !scratch_bitmap_add(
                scratch,
                scratch->release_bitmap,
                frame_index
            )
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
validate_handoff_processes(
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects
)
{
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_process *record =
            &objects->processes[index];
        struct micros_process_handle process;
        struct micros_thread_handle thread_handle;
        enum micros_user_execution_error execution_error;

        if (record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        process = (struct micros_process_handle){
            .slot = (uint16_t)index,
            .generation = record->generation,
        };
        if (record->address_space_root == 0) {
            if (
                record->live_thread_count != 0
                || record->primary_endpoint
                    != MICROS_PROCESS_ENDPOINT_NONE
                || !process_has_no_owned_frames(ledger, process)
            ) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
            }
            continue;
        }
        if (
            record->live_thread_count != 1
            || !find_one_prepared_thread(
                objects,
                process,
                &thread_handle
            )
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        }
        execution_error = micros_user_execution_validate_context(
            thread_handle,
            &objects->threads[thread_handle.slot].user_context
        );
        if (execution_error != MICROS_USER_EXECUTION_OK) {
            return (
                execution_error == MICROS_USER_EXECUTION_ERROR_STATE
                || execution_error
                    == MICROS_USER_EXECUTION_ERROR_CONTEXT
                || execution_error
                    == MICROS_USER_EXECUTION_ERROR_MAPPING
            )
                ? MICROS_USER_ADDRESS_SPACE_ERROR_STATE
                : MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_wired_handoff(void)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;
    size_t process_index;

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
    if (
        micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done;
    }
    error = validate_handoff_processes(ledger, objects);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    for (
        process_index = 0;
        process_index < MICROS_PROCESS_CAPACITY;
        ++process_index
    ) {
        const struct micros_process *record =
            &objects->processes[process_index];
        struct micros_process_handle process;

        if (
            record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || record->address_space_root == 0
        ) {
            continue;
        }
        process = (struct micros_process_handle){
            .slot = (uint16_t)process_index,
            .generation = record->generation,
        };
        error = validate_locked(
            &runtime,
            &scratch,
            process,
            NULL
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto done_with_scratch;
        }
        error = select_reachable_process_user_targets(
            ledger,
            &scratch,
            process
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto done_with_scratch;
        }
    }
    error = map_ownership_error(
        micros_frame_ownership_runtime_prepare_wired_process_user_set(
            scratch.release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_complete_wired_handoff(void)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;
    size_t index;

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
    if (
        micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done;
    }
    error = validate_handoff_processes(ledger, objects);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_process *record =
            &objects->processes[index];
        struct micros_process_handle process;

        if (
            record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || record->address_space_root == 0
        ) {
            continue;
        }
        process = (struct micros_process_handle){
            .slot = (uint16_t)index,
            .generation = record->generation,
        };
        error = validate_locked(
            &runtime,
            &scratch,
            process,
            NULL
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto done_with_scratch;
        }
        if (
            !reachable_user_targets_are_wired(
                ledger,
                &scratch,
                process
            )
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            goto done_with_scratch;
        }
    }
    error = map_ownership_error(
        micros_frame_ownership_runtime_complete_handoff(objects)
    );

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_tty_uart_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address,
    struct micros_user_address_space_tty_uart_plan *plan
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    const struct micros_process *resolved;
    struct micros_user_address_space_tty_uart_plan candidate;
    struct micros_sv39_decoded_pte root_entry;
    struct micros_sv39_decoded_pte middle_entry;
    struct micros_sv39_decoded_pte leaf_entry;
    struct micros_frame_owner ignored_owner;
    struct micros_user_address_space_page_table *root;
    struct micros_user_address_space_page_table *middle;
    struct micros_user_address_space_page_table *leaf;
    enum micros_frame_ownership_error ownership_error;
    enum micros_user_address_space_error error;
    uint64_t leaf_pte;
    uint16_t root_index;
    uint16_t middle_index;
    uint16_t leaf_index;
    uintptr_t saved_status;

    if (
        plan == NULL
        || expected_root_physical_address == 0
        || expected_root_physical_address
            % MICROS_SV39_PAGE_SIZE != 0
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    saved_status = micros_user_address_space_runtime_lock();
    error = require_read_authority(
        &runtime,
        &ledger,
        &objects,
        &kernel_report,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
        goto done;
    }
    error = resolve_process_with_root(&runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        resolved->address_space_root
            != expected_root_physical_address
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = validate_locked(
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
    if (
        micros_sv39_vpn_index(
            MICROS_TTY_UART_VIRTUAL_BASE,
            2,
            &root_index
        ) != MICROS_SV39_OK
        || root_index != MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX
        || micros_sv39_vpn_index(
            MICROS_TTY_UART_VIRTUAL_BASE,
            1,
            &middle_index
        ) != MICROS_SV39_OK
        || micros_sv39_vpn_index(
            MICROS_TTY_UART_VIRTUAL_BASE,
            0,
            &leaf_index
        ) != MICROS_SV39_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done_with_scratch;
    }
    root = table_at(resolved->address_space_root);
    error = decode_pte(root->entries[root_index], &root_entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (root_entry.kind == MICROS_SV39_PTE_ABSENT) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
        goto done_with_scratch;
    }
    if (root_entry.kind != MICROS_SV39_PTE_TABLE) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        goto done_with_scratch;
    }
    middle = table_at(root_entry.physical_address);
    error = decode_pte(
        middle->entries[middle_index],
        &middle_entry
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (middle_entry.kind == MICROS_SV39_PTE_ABSENT) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
        goto done_with_scratch;
    }
    if (middle_entry.kind != MICROS_SV39_PTE_TABLE) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        goto done_with_scratch;
    }
    leaf = table_at(middle_entry.physical_address);
    error = decode_pte(leaf->entries[leaf_index], &leaf_entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (leaf_entry.kind != MICROS_SV39_PTE_ABSENT) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT;
        goto done_with_scratch;
    }
    ownership_error = micros_frame_ownership_lookup(
        ledger,
        MICROS_TTY_UART_PHYSICAL_BASE,
        &ignored_owner
    );
    if (
        ownership_error
            != MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED
    ) {
        error = ownership_error == MICROS_FRAME_OWNERSHIP_OK
            ? MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
            : MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done_with_scratch;
    }
    if (
        micros_sv39_make_leaf_pte(
            MICROS_TTY_UART_PHYSICAL_BASE,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_USER,
            &leaf_pte
        ) != MICROS_SV39_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        goto done_with_scratch;
    }
    candidate.active = true;
    candidate.leaf_index = leaf_index;
    candidate.process = process;
    candidate.root_physical_address =
        resolved->address_space_root;
    candidate.leaf_table_physical_address =
        middle_entry.physical_address;
    candidate.leaf_pte = leaf_pte;
    copy_bytes(plan, &candidate, sizeof(candidate));
    error = MICROS_USER_ADDRESS_SPACE_OK;

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

void micros_user_address_space_commit_tty_uart_mapping(
    struct micros_user_address_space_tty_uart_plan *plan
)
{
    struct micros_user_address_space_page_table *leaf;

    if (
        plan == NULL
        || !plan->active
        || plan->process.slot >= MICROS_PROCESS_CAPACITY
        || plan->process.generation == 0
        || plan->root_physical_address == 0
        || plan->leaf_table_physical_address == 0
        || plan->leaf_index >= MICROS_SV39_TABLE_ENTRY_COUNT
        || plan->leaf_pte == 0
    ) {
        panic_invariant("tty-uart-map-commit-plan");
    }
    leaf = table_at(plan->leaf_table_physical_address);
    if (leaf->entries[plan->leaf_index] != 0) {
        panic_invariant("tty-uart-map-commit-conflict");
    }
    leaf->entries[plan->leaf_index] = plan->leaf_pte;
    micros_riscv_publish_page_table();
    clear_bytes(plan, sizeof(*plan));
}

enum micros_user_address_space_error
micros_user_address_space_validate_tty_uart_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_tty_device_authority authority;
    struct micros_sv39_decoded_pte leaf_entry;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    enum micros_user_address_space_error error;
    bool authority_active;
    bool is_device;
    uintptr_t saved_status;

    if (
        expected_root_physical_address == 0
        || expected_root_physical_address
            % MICROS_SV39_PAGE_SIZE != 0
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    saved_status = micros_user_address_space_runtime_lock();
    error = require_read_authority(
        &runtime,
        &ledger,
        &objects,
        &kernel_report,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(&runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        resolved->address_space_root
            != expected_root_physical_address
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = validate_locked(
        &runtime,
        &scratch,
        process,
        &resolved
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = load_tty_device_authority(
        &authority_active,
        &authority
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (
        !authority_active
        || !micros_tty_device_leaf_required(
            &authority,
            process,
            resolved->address_space_root
        )
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done_with_scratch;
    }
    error = lookup_leaf_locked(
        resolved,
        MICROS_TTY_UART_VIRTUAL_BASE,
        &leaf_entry,
        NULL,
        NULL,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = classify_tty_device_leaf(
        true,
        &authority,
        process,
        resolved->address_space_root,
        MICROS_TTY_UART_VIRTUAL_BASE,
        &leaf_entry,
        &is_device
    );
    if (error == MICROS_USER_ADDRESS_SPACE_OK && !is_device) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)ledger;
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

static bool owner_is_ownerless_kind(
    struct micros_frame_owner owner,
    enum micros_frame_owner_kind kind
)
{
    return (
        owner.kind == kind
        && owner.slot == 0
        && owner.generation == 0
        && owner.reserved == 0
    );
}

static bool owner_is_exact_process_kind(
    struct micros_frame_owner owner,
    enum micros_frame_owner_kind kind,
    struct micros_process_handle process
)
{
    return (
        owner.kind == kind
        && owner.slot == process.slot
        && owner.generation == process.generation
        && owner.reserved == 0
    );
}

static enum micros_user_address_space_error
require_vm_scratch_state(
    struct micros_process_handle process,
    uint64_t root_physical_address,
    enum micros_vm_scratch_phase scratch_phase,
    uint64_t physical_address,
    const struct micros_vm_handoff_state **state
)
{
    const struct micros_vm_handoff_state *observed =
        micros_vm_handoff_runtime_state();

    if (
        observed == NULL
        || micros_vm_handoff_state_validate(observed)
            != MICROS_VM_HANDOFF_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    if (
        observed->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || !process_handles_equal(observed->process, process)
        || observed->root_physical_address
            != root_physical_address
        || observed->load.phase != MICROS_VM_LOAD_PHASE_LOADING
        || observed->load.scratch_phase != scratch_phase
        || (
            scratch_phase == MICROS_VM_SCRATCH_MAPPED
            && observed->load.scratch.physical_address
                != physical_address
        )
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
    }
    if (state != NULL) {
        *state = observed;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_scratch_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address,
    uint64_t physical_address,
    struct micros_user_address_space_vm_scratch_plan *plan
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_user_address_space_vm_scratch_plan candidate;
    struct micros_sv39_decoded_pte leaf_entry;
    struct micros_frame_owner owner;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    uint64_t *leaf_pte;
    uint64_t leaf_table_physical_address;
    uint64_t encoded_pte;
    uint16_t leaf_index;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    if (
        plan == NULL
        || physical_address == 0
        || physical_address % MICROS_SV39_PAGE_SIZE != 0
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    saved_status = micros_user_address_space_runtime_lock();
    error = require_read_authority(
        &runtime,
        &ledger,
        &objects,
        &kernel_report,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
        goto done;
    }
    error = require_vm_scratch_state(
        process,
        expected_root_physical_address,
        MICROS_VM_SCRATCH_CLEAR,
        0,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(&runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        resolved->address_space_root
            != expected_root_physical_address
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done_with_scratch;
    }
    error = validate_all_process_roots_locked(
        &runtime,
        objects,
        &scratch
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (active_root() != resolved->address_space_root) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done_with_scratch;
    }
    if (
        micros_frame_ownership_lookup(
            ledger,
            physical_address,
            &owner
        ) != MICROS_FRAME_OWNERSHIP_OK
        || !owner_is_ownerless_kind(
            owner,
            MICROS_FRAME_OWNER_FREE
        )
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        goto done_with_scratch;
    }
    error = locate_leaf_slot_locked(
        resolved,
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
        &leaf_entry,
        &leaf_pte,
        &leaf_table_physical_address
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (leaf_entry.kind != MICROS_SV39_PTE_ABSENT) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT;
        goto done_with_scratch;
    }
    if (
        micros_sv39_vpn_index(
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            0,
            &leaf_index
        ) != MICROS_SV39_OK
        || micros_sv39_make_leaf_pte(
            physical_address,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_USER,
            &encoded_pte
        ) != MICROS_SV39_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        goto done_with_scratch;
    }
    candidate.active = true;
    candidate.leaf_index = leaf_index;
    candidate.process = process;
    candidate.root_physical_address =
        resolved->address_space_root;
    candidate.leaf_table_physical_address =
        leaf_table_physical_address;
    candidate.physical_address = physical_address;
    candidate.leaf_pte = encoded_pte;
    copy_bytes(plan, &candidate, sizeof(candidate));
    error = MICROS_USER_ADDRESS_SPACE_OK;

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

void micros_user_address_space_commit_vm_scratch_mapping(
    struct micros_user_address_space_vm_scratch_plan *plan
)
{
    struct micros_user_address_space_page_table *leaf;

    if (
        plan == NULL
        || !plan->active
        || plan->process.slot >= MICROS_PROCESS_CAPACITY
        || plan->process.generation == 0
        || plan->reserved1 != 0
        || plan->root_physical_address == 0
        || plan->leaf_table_physical_address == 0
        || plan->physical_address == 0
        || plan->leaf_index >= MICROS_SV39_TABLE_ENTRY_COUNT
        || plan->leaf_pte == 0
        || active_root() != plan->root_physical_address
    ) {
        panic_invariant("vm-scratch-map-commit-plan");
    }
    leaf = table_at(plan->leaf_table_physical_address);
    if (leaf->entries[plan->leaf_index] != 0) {
        panic_invariant("vm-scratch-map-commit-conflict");
    }
    leaf->entries[plan->leaf_index] = plan->leaf_pte;
    micros_riscv_publish_page_table_address(
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
    );
    clear_bytes(plan, sizeof(*plan));
}

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_scratch_removal(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address,
    uint64_t physical_address,
    struct micros_user_address_space_vm_scratch_plan *plan
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_user_address_space_vm_scratch_plan candidate;
    struct micros_sv39_decoded_pte leaf_entry;
    struct micros_frame_owner owner;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    uint64_t *leaf_pte;
    uint64_t leaf_table_physical_address;
    uint64_t expected_pte;
    uint16_t leaf_index;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    if (
        plan == NULL
        || physical_address == 0
        || physical_address % MICROS_SV39_PAGE_SIZE != 0
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    saved_status = micros_user_address_space_runtime_lock();
    error = require_read_authority(
        &runtime,
        &ledger,
        &objects,
        &kernel_report,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
        goto done;
    }
    error = require_vm_scratch_state(
        process,
        expected_root_physical_address,
        MICROS_VM_SCRATCH_MAPPED,
        physical_address,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(&runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        resolved->address_space_root
            != expected_root_physical_address
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = validate_all_process_roots_locked(
        &runtime,
        objects,
        &scratch
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (active_root() != resolved->address_space_root) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done_with_scratch;
    }
    if (
        micros_frame_ownership_lookup(
            ledger,
            physical_address,
            &owner
        ) != MICROS_FRAME_OWNERSHIP_OK
        || !owner_is_ownerless_kind(
            owner,
            MICROS_FRAME_OWNER_VM_TRANSFERABLE
        )
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        goto done_with_scratch;
    }
    error = locate_leaf_slot_locked(
        resolved,
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
        &leaf_entry,
        &leaf_pte,
        &leaf_table_physical_address
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (
        micros_sv39_vpn_index(
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            0,
            &leaf_index
        ) != MICROS_SV39_OK
        || micros_sv39_make_leaf_pte(
            physical_address,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_USER,
            &expected_pte
        ) != MICROS_SV39_OK
        || leaf_entry.kind != MICROS_SV39_PTE_LEAF
        || *leaf_pte != expected_pte
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        goto done_with_scratch;
    }
    candidate.active = true;
    candidate.leaf_index = leaf_index;
    candidate.process = process;
    candidate.root_physical_address =
        resolved->address_space_root;
    candidate.leaf_table_physical_address =
        leaf_table_physical_address;
    candidate.physical_address = physical_address;
    candidate.leaf_pte = expected_pte;
    copy_bytes(plan, &candidate, sizeof(candidate));
    error = MICROS_USER_ADDRESS_SPACE_OK;

done_with_scratch:
    release_scratch(&scratch);
done:
    (void)kernel_report;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

void micros_user_address_space_commit_vm_scratch_removal(
    struct micros_user_address_space_vm_scratch_plan *plan
)
{
    struct micros_user_address_space_page_table *leaf;

    if (
        plan == NULL
        || !plan->active
        || plan->process.slot >= MICROS_PROCESS_CAPACITY
        || plan->process.generation == 0
        || plan->reserved1 != 0
        || plan->root_physical_address == 0
        || plan->leaf_table_physical_address == 0
        || plan->physical_address == 0
        || plan->leaf_index >= MICROS_SV39_TABLE_ENTRY_COUNT
        || plan->leaf_pte == 0
        || active_root() != plan->root_physical_address
    ) {
        panic_invariant("vm-scratch-remove-commit-plan");
    }
    leaf = table_at(plan->leaf_table_physical_address);
    if (leaf->entries[plan->leaf_index] != plan->leaf_pte) {
        panic_invariant("vm-scratch-remove-commit-pte");
    }
    leaf->entries[plan->leaf_index] = 0;
    micros_riscv_publish_page_table_address(
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
    );
    clear_bytes(plan, sizeof(*plan));
}

static bool freeze_action_matches_runtime(
    const struct micros_vm_handoff_state *state,
    const struct micros_vm_load_freeze_action *action
)
{
    const struct micros_vm_kernel_mapping_record *record;

    if (
        state == NULL
        || action == NULL
        || action->process.slot >= MICROS_PROCESS_CAPACITY
    ) {
        return false;
    }
    record = &state->mappings[action->process.slot];
    return (
        state->phase == MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        && state->load.phase == MICROS_VM_LOAD_PHASE_LOADING
        && state->load.scratch_phase == MICROS_VM_SCRATCH_CLEAR
        && process_handles_equal(
            state->load.process,
            action->process
        )
        && state->load.load_token == action->load_token
        && action->mapping_generation != 0
        && action->root_physical_address
            == action->table_frames[
                MICROS_USER_ADDRESS_SPACE_VM_TABLE_ROOT
            ]
        && record->phase == MICROS_VM_KERNEL_MAPPING_LOADING
        && process_handles_equal(record->process, action->process)
        && record->load_token == action->load_token
        && record->entry_count == action->entry_count
        && action->entry == MICROS_APPLICATION_ENTRY
        && bytes_equal(
            record->entries,
            action->entries,
            sizeof(action->entries)
        )
    );
}

static bool process_is_empty_for_vm_freeze(
    const struct micros_process *process
)
{
    return (
        process->live_thread_count == 0
        && process->address_space_root == 0
        && process->primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        && process->privilege_profile == 0
        && !process->endpoint_lifecycle_consumed
    );
}

static void copy_vm_action_mappings(
    const struct micros_vm_kernel_mapping_entry *entries,
    size_t entry_count,
    struct micros_user_address_space_vm_mapping *mappings
)
{
    size_t index;

    for (index = 0; index < entry_count; ++index) {
        mappings[index].virtual_address =
            entries[index].virtual_address;
        mappings[index].physical_address =
            entries[index].physical_address;
        mappings[index].permissions = entries[index].permissions;
        mappings[index].reserved = entries[index].reserved;
    }
}

static bool vm_table_plan_matches_memory(
    const struct micros_user_address_space_vm_table_plan *plan,
    const struct micros_kernel_address_space_report *kernel_report
)
{
    const struct micros_user_address_space_page_table *kernel_root;
    const struct micros_user_address_space_page_table *root;
    const struct micros_user_address_space_page_table *middle;
    const struct micros_user_address_space_page_table *image;
    const struct micros_user_address_space_page_table *stack;
    size_t index;

    if (
        plan == NULL
        || kernel_report == NULL
        || plan->entry_count
            > MICROS_APPLICATION_RESIDENT_PAGE_MAX
    ) {
        return false;
    }
    kernel_root = table_at(kernel_report->root_physical_address);
    root = table_at(
        plan->table_frames[
            MICROS_USER_ADDRESS_SPACE_VM_TABLE_ROOT
        ]
    );
    middle = table_at(
        plan->table_frames[
            MICROS_USER_ADDRESS_SPACE_VM_TABLE_LEVEL_ONE
        ]
    );
    image = table_at(
        plan->table_frames[
            MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE
        ]
    );
    stack = table_at(
        plan->table_frames[
            MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK
        ]
    );
    for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
        uint64_t expected_root = index == plan->root_index
            ? plan->root_pte
            : kernel_root->entries[index];
        uint64_t expected_middle = 0;
        uint64_t expected_image = 0;
        uint64_t expected_stack = 0;
        size_t leaf_index;

        if (index == plan->image_middle_index) {
            expected_middle = plan->image_middle_pte;
        } else if (index == plan->stack_middle_index) {
            expected_middle = plan->stack_middle_pte;
        }
        for (
            leaf_index = 0;
            leaf_index < plan->entry_count;
            ++leaf_index
        ) {
            const struct micros_user_address_space_vm_leaf_plan
                *leaf = &plan->leaves[leaf_index];

            if (leaf->leaf_index != index) {
                continue;
            }
            if (
                leaf->table_role
                    == MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE
            ) {
                expected_image = leaf->pte;
            } else if (
                leaf->table_role
                    == MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK
            ) {
                expected_stack = leaf->pte;
            } else {
                return false;
            }
        }
        if (
            root->entries[index] != expected_root
            || middle->entries[index] != expected_middle
            || image->entries[index] != expected_image
            || stack->entries[index] != expected_stack
        ) {
            return false;
        }
    }
    return true;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_freeze(
    const struct micros_vm_load_freeze_action *action,
    struct micros_user_address_space_vm_freeze_plan *plan
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_vm_handoff_state *state;
    const struct micros_process *process;
    struct micros_user_address_space_vm_mapping
        mappings[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    struct micros_user_address_space_vm_freeze_plan candidate;
    struct micros_frame_owner owner;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;
    size_t index;

    if (action == NULL || plan == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    clear_bytes(mappings, sizeof(mappings));
    clear_bytes(&candidate, sizeof(candidate));
    saved_status = micros_user_address_space_runtime_lock();
    error = require_read_authority(
        &runtime,
        &ledger,
        &objects,
        &kernel_report,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
        goto done;
    }
    state = micros_vm_handoff_runtime_state();
    if (
        state == NULL
        || micros_vm_handoff_state_validate(state)
            != MICROS_VM_HANDOFF_OK
        || !freeze_action_matches_runtime(state, action)
        || active_root() != state->root_physical_address
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done;
    }
    error = resolve_process(&runtime, action->process, &process);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (!process_is_empty_for_vm_freeze(process)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || table_at(kernel_report->root_physical_address)
                ->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX]
            != 0
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done_with_scratch;
    }
    error = validate_all_process_roots_locked(
        &runtime,
        objects,
        &scratch
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (
        action->entry_count
            > MICROS_APPLICATION_RESIDENT_PAGE_MAX
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
        goto done_with_scratch;
    }
    for (index = 0; index < action->entry_count; ++index) {
        if (
            micros_frame_ownership_lookup(
                ledger,
                action->entries[index].physical_address,
                &owner
            ) != MICROS_FRAME_OWNERSHIP_OK
            || !owner_is_ownerless_kind(
                owner,
                MICROS_FRAME_OWNER_VM_TRANSFERABLE
            )
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            goto done_with_scratch;
        }
    }
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        if (
            micros_frame_ownership_lookup(
                ledger,
                action->table_frames[index],
                &owner
            ) != MICROS_FRAME_OWNERSHIP_OK
            || !owner_is_ownerless_kind(
                owner,
                MICROS_FRAME_OWNER_FREE
            )
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            goto done_with_scratch;
        }
    }
    copy_vm_action_mappings(
        action->entries,
        action->entry_count,
        mappings
    );
    error = micros_user_address_space_prepare_vm_table_plan(
        action->process,
        action->table_frames,
        mappings,
        action->entry_count,
        &candidate.tables
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    candidate.phase =
        MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_PREPARED;
    copy_bytes(plan, &candidate, sizeof(candidate));
    error = MICROS_USER_ADDRESS_SPACE_OK;

done_with_scratch:
    release_scratch(&scratch);
done:
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

void micros_user_address_space_construct_vm_freeze_prevalidated(
    struct micros_user_address_space_vm_freeze_plan *plan
)
{
    const struct micros_kernel_address_space_report *kernel_report =
        micros_kernel_address_space_report();
    const struct micros_user_address_space_page_table *kernel_root;
    struct micros_user_address_space_page_table *root;
    struct micros_user_address_space_page_table *middle;
    struct micros_user_address_space_page_table *image;
    struct micros_user_address_space_page_table *stack;
    size_t index;

    if (
        plan == NULL
        || plan->phase
            != MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_PREPARED
        || plan->reserved != 0
        || kernel_report == NULL
    ) {
        panic_invariant("vm-freeze-construct-plan");
    }
    kernel_root = table_at(kernel_report->root_physical_address);
    root = table_at(
        plan->tables.table_frames[
            MICROS_USER_ADDRESS_SPACE_VM_TABLE_ROOT
        ]
    );
    middle = table_at(
        plan->tables.table_frames[
            MICROS_USER_ADDRESS_SPACE_VM_TABLE_LEVEL_ONE
        ]
    );
    image = table_at(
        plan->tables.table_frames[
            MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE
        ]
    );
    stack = table_at(
        plan->tables.table_frames[
            MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK
        ]
    );
    clear_page(
        plan->tables.table_frames[
            MICROS_USER_ADDRESS_SPACE_VM_TABLE_ROOT
        ]
    );
    clear_page(
        plan->tables.table_frames[
            MICROS_USER_ADDRESS_SPACE_VM_TABLE_LEVEL_ONE
        ]
    );
    clear_page(
        plan->tables.table_frames[
            MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE
        ]
    );
    clear_page(
        plan->tables.table_frames[
            MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK
        ]
    );
    for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
        if (index != plan->tables.root_index) {
            root->entries[index] = kernel_root->entries[index];
        }
    }
    root->entries[plan->tables.root_index] =
        plan->tables.root_pte;
    middle->entries[plan->tables.image_middle_index] =
        plan->tables.image_middle_pte;
    middle->entries[plan->tables.stack_middle_index] =
        plan->tables.stack_middle_pte;
    for (index = 0; index < plan->tables.entry_count; ++index) {
        const struct micros_user_address_space_vm_leaf_plan
            *leaf = &plan->tables.leaves[index];

        if (
            leaf->table_role
                == MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE
        ) {
            image->entries[leaf->leaf_index] = leaf->pte;
        } else if (
            leaf->table_role
                == MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK
        ) {
            stack->entries[leaf->leaf_index] = leaf->pte;
        } else {
            panic_invariant("vm-freeze-construct-leaf");
        }
    }
    micros_riscv_publish_page_table();
    plan->phase =
        MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_CONSTRUCTED;
}

void micros_user_address_space_install_vm_freeze_prevalidated(
    struct micros_user_address_space_vm_freeze_plan *plan
)
{
    const struct micros_kernel_address_space_report *kernel_report =
        micros_kernel_address_space_report();

    if (
        plan == NULL
        || plan->phase
            != MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_CONSTRUCTED
        || kernel_report == NULL
        || !vm_table_plan_matches_memory(
            &plan->tables,
            kernel_report
        )
        || micros_kernel_object_runtime_attach_address_space(
            plan->tables.process,
            plan->tables.table_frames[
                MICROS_USER_ADDRESS_SPACE_VM_TABLE_ROOT
            ]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_user_address_space_validate(
            plan->tables.process
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        panic_invariant("vm-freeze-install");
    }
    clear_bytes(plan, sizeof(*plan));
}

static bool teardown_record_matches(
    const struct micros_vm_handoff_state *state,
    struct micros_process_handle process,
    uint64_t root_physical_address,
    const uint64_t
        table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    const struct micros_vm_kernel_mapping_entry *entries,
    size_t entry_count
)
{
    const struct micros_vm_kernel_mapping_record *record;

    if (
        state == NULL
        || process.slot >= MICROS_PROCESS_CAPACITY
        || table_frames == NULL
        || entries == NULL
    ) {
        return false;
    }
    record = &state->mappings[process.slot];
    return (
        state->phase == MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        && (
            record->phase == MICROS_VM_KERNEL_MAPPING_FROZEN
            || record->phase
                == MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED
        )
        && process_handles_equal(record->process, process)
        && record->root_physical_address == root_physical_address
        && record->entry_count == entry_count
        && bytes_equal(
            record->table_frames,
            table_frames,
            sizeof(record->table_frames)
        )
        && bytes_equal(
            record->entries,
            entries,
            entry_count * sizeof(*entries)
        )
    );
}

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_teardown(
    struct micros_process_handle process,
    uint64_t root_physical_address,
    const uint64_t
        table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    const struct micros_vm_kernel_mapping_entry *entries,
    size_t entry_count,
    struct micros_user_address_space_vm_teardown_plan *plan
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_vm_handoff_state *state;
    const struct micros_process *resolved;
    struct micros_user_address_space_vm_mapping
        mappings[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    struct micros_user_address_space_vm_table_plan table_plan;
    struct micros_user_address_space_vm_teardown_plan candidate;
    struct micros_frame_owner owner;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;
    size_t index;

    if (
        table_frames == NULL
        || entries == NULL
        || plan == NULL
        || entry_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    clear_bytes(mappings, sizeof(mappings));
    clear_bytes(&candidate, sizeof(candidate));
    saved_status = micros_user_address_space_runtime_lock();
    error = require_read_authority(
        &runtime,
        &ledger,
        &objects,
        &kernel_report,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
        goto done;
    }
    state = micros_vm_handoff_runtime_state();
    if (
        state == NULL
        || micros_vm_handoff_state_validate(state)
            != MICROS_VM_HANDOFF_OK
        || !teardown_record_matches(
            state,
            process,
            root_physical_address,
            table_frames,
            entries,
            entry_count
        )
        || active_root() != state->root_physical_address
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done;
    }
    error = resolve_process_with_root(&runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        resolved->address_space_root != root_physical_address
        || resolved->live_thread_count != 0
        || resolved->primary_endpoint
            != MICROS_PROCESS_ENDPOINT_NONE
        || resolved->privilege_profile != 0
        || active_root() == root_physical_address
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = validate_all_process_roots_locked(
        &runtime,
        objects,
        &scratch
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    for (index = 0; index < entry_count; ++index) {
        if (
            micros_frame_ownership_lookup(
                ledger,
                entries[index].physical_address,
                &owner
            ) != MICROS_FRAME_OWNERSHIP_OK
            || !owner_is_exact_process_kind(
                owner,
                MICROS_FRAME_OWNER_VM_WIRED,
                process
            )
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            goto done_with_scratch;
        }
    }
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        if (
            micros_frame_ownership_lookup(
                ledger,
                table_frames[index],
                &owner
            ) != MICROS_FRAME_OWNERSHIP_OK
            || !owner_is_exact_process_kind(
                owner,
                MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
                process
            )
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            goto done_with_scratch;
        }
    }
    copy_vm_action_mappings(entries, entry_count, mappings);
    error = micros_user_address_space_prepare_vm_table_plan(
        process,
        table_frames,
        mappings,
        entry_count,
        &table_plan
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (!vm_table_plan_matches_memory(&table_plan, kernel_report)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        goto done_with_scratch;
    }
    candidate.active = true;
    candidate.entry_count = (uint32_t)entry_count;
    candidate.process = process;
    candidate.root_physical_address = root_physical_address;
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        candidate.table_frames[index] = table_frames[index];
    }
    for (index = 0; index < entry_count; ++index) {
        candidate.data_frames[index] =
            entries[index].physical_address;
    }
    copy_bytes(plan, &candidate, sizeof(candidate));
    error = MICROS_USER_ADDRESS_SPACE_OK;

done_with_scratch:
    release_scratch(&scratch);
done:
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

void micros_user_address_space_commit_vm_teardown_prevalidated(
    struct micros_user_address_space_vm_teardown_plan *plan
)
{
    size_t index;

    if (
        plan == NULL
        || !plan->active
        || plan->entry_count
            > MICROS_APPLICATION_RESIDENT_PAGE_MAX
        || plan->process.slot >= MICROS_PROCESS_CAPACITY
        || plan->process.generation == 0
        || plan->reserved1 != 0
        || plan->root_physical_address == 0
        || active_root() == plan->root_physical_address
        || micros_kernel_object_runtime_detach_address_space(
            plan->process,
            plan->root_physical_address
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        panic_invariant("vm-teardown-commit");
    }
    for (index = 0; index < plan->entry_count; ++index) {
        clear_page(plan->data_frames[index]);
    }
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        clear_page(plan->table_frames[index]);
    }
    micros_riscv_publish_page_table();
    clear_bytes(plan, sizeof(*plan));
}

enum micros_user_address_space_error
micros_user_address_space_validate(
    struct micros_process_handle process
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    saved_status = micros_user_address_space_runtime_lock();
    error = require_read_authority(
        &runtime,
        &ledger,
        &objects,
        &kernel_report,
        NULL
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
    error = validate_locked(&runtime, &scratch, process, NULL);
    release_scratch(&scratch);

done:
    (void)ledger;
    (void)objects;
    (void)kernel_report;
    (void)resolved;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
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
        &kernel_report,
        NULL
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
    error = validate_locked(
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

#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
void micros_user_address_space_test_fail_after_allocations(
    size_t successful_allocations
)
{
    test_fail_after_allocations = successful_allocations;
    test_successful_allocations = 0;
}
#endif
