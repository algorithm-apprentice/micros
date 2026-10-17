#include "kernel/memory/user_address_space_internal.h"

#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static struct micros_user_address_space_page_table *table_at(
    uint64_t physical_address
)
{
    return micros_user_address_space_table_at(physical_address);
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

static enum micros_user_address_space_error decode_pte(
    uint64_t pte,
    struct micros_sv39_decoded_pte *decoded
)
{
    return micros_user_address_space_decode_pte(pte, decoded);
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

enum micros_user_address_space_error
micros_user_address_space_load_tty_device_authority(
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

enum micros_user_address_space_error
micros_user_address_space_load_vm_scratch_authority(
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

bool micros_user_address_space_leaf_matches_vm_scratch_authority(
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
        && micros_user_address_space_process_handles_equal(
            authority->process,
            process
        )
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

enum micros_user_address_space_error
micros_user_address_space_classify_tty_device_leaf(
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

enum micros_user_address_space_error
micros_user_address_space_reject_exceptional_leaf(
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

    error = micros_user_address_space_load_vm_scratch_authority(
        &scratch_authority_active,
        &scratch_authority
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (
        micros_user_address_space_leaf_matches_vm_scratch_authority(
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
    error = micros_user_address_space_load_tty_device_authority(
        &authority_active,
        &authority
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = micros_user_address_space_classify_tty_device_leaf(
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

struct user_address_space_validation_context {
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    const struct micros_user_address_space_scratch_lease *lease;
    struct micros_process_handle process;
    struct micros_tty_device_authority device_authority;
    struct micros_user_address_space_vm_scratch_authority
        scratch_authority;
    enum micros_frame_owner_kind leaf_owner_kind;
    bool device_authority_active;
    bool device_leaf_found;
    bool scratch_authority_active;
    bool scratch_leaf_found;
};

static enum micros_user_address_space_error
load_validation_context(
    struct micros_user_address_space_runtime_view *runtime,
    const struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    struct user_address_space_validation_context *context
)
{
    const struct micros_kernel_objects *objects;
    enum micros_user_address_space_error error;

    micros_user_address_space_clear_bitmap(
        lease->reachable_bitmap,
        lease->word_count
    );
    context->lease = lease;
    context->process = process;
    context->device_leaf_found = false;
    context->scratch_leaf_found = false;
    error = require_read_authority(
        runtime,
        &context->ledger,
        &objects,
        &context->kernel_report,
        &context->leaf_owner_kind
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = micros_user_address_space_load_vm_scratch_authority(
        &context->scratch_authority_active,
        &context->scratch_authority
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = micros_user_address_space_load_tty_device_authority(
        &context->device_authority_active,
        &context->device_authority
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
    error = resolve_process(runtime, process, &context->resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (context->resolved->address_space_root == 0) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
    }
    return mark_owned_frame(
        context->ledger,
        lease,
        context->resolved->address_space_root,
        MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE,
        process
    );
}

static enum micros_user_address_space_error
validate_shared_kernel_entries(
    const struct user_address_space_validation_context *context
)
{
    const struct micros_user_address_space_page_table *kernel_root =
        table_at(context->kernel_report->root_physical_address);
    const struct micros_user_address_space_page_table *root =
        table_at(context->resolved->address_space_root);
    size_t root_index;

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
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
validate_leaf_table(
    struct user_address_space_validation_context *context,
    size_t middle_index,
    uint64_t physical_address
)
{
    const struct micros_user_address_space_page_table *leaf =
        table_at(physical_address);
    size_t leaf_index;

    for (
        leaf_index = 0;
        leaf_index < MICROS_SV39_TABLE_ENTRY_COUNT;
        ++leaf_index
    ) {
        struct micros_sv39_decoded_pte leaf_entry;
        enum micros_user_address_space_leaf_class leaf_class;
        enum micros_user_address_space_error error;
        uint64_t virtual_address;
        bool is_device;

        error = decode_pte(leaf->entries[leaf_index], &leaf_entry);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        if (leaf_entry.kind == MICROS_SV39_PTE_ABSENT) {
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
        error = micros_user_address_space_classify_tty_device_leaf(
            context->device_authority_active,
            &context->device_authority,
            context->process,
            context->resolved->address_space_root,
            virtual_address,
            &leaf_entry,
            &is_device
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        if (is_device) {
            if (context->device_leaf_found) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            }
            context->device_leaf_found = true;
            continue;
        }
        error = mark_managed_leaf(
            context->ledger,
            context->lease,
            context->process,
            context->resolved->address_space_root,
            virtual_address,
            &leaf_entry,
            context->scratch_authority_active
                ? &context->scratch_authority
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
            if (context->scratch_leaf_found) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            }
            context->scratch_leaf_found = true;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
validate_middle_table(
    struct user_address_space_validation_context *context,
    uint64_t physical_address
)
{
    const struct micros_user_address_space_page_table *middle =
        table_at(physical_address);
    size_t middle_index;

    for (
        middle_index = 0;
        middle_index < MICROS_SV39_TABLE_ENTRY_COUNT;
        ++middle_index
    ) {
        struct micros_sv39_decoded_pte middle_entry;
        enum micros_user_address_space_error error;

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
            context->ledger,
            context->lease,
            middle_entry.physical_address,
            MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE,
            context->process
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        error = validate_leaf_table(
            context,
            middle_index,
            middle_entry.physical_address
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error validate_user_tree(
    struct user_address_space_validation_context *context
)
{
    const struct micros_user_address_space_page_table *root =
        table_at(context->resolved->address_space_root);
    struct micros_sv39_decoded_pte root_entry;
    enum micros_user_address_space_error error;

    error = decode_pte(
        root->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX],
        &root_entry
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (root_entry.kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_OK;
    }
    if (root_entry.kind != MICROS_SV39_PTE_TABLE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    error = mark_owned_frame(
        context->ledger,
        context->lease,
        root_entry.physical_address,
        MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE,
        context->process
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    return validate_middle_table(
        context,
        root_entry.physical_address
    );
}

static enum micros_user_address_space_error
validate_required_exceptional_leaves(
    const struct user_address_space_validation_context *context
)
{
    if (
        context->device_authority_active
        && micros_tty_device_leaf_required(
            &context->device_authority,
            context->process,
            context->resolved->address_space_root
        )
        && !context->device_leaf_found
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    if (
        context->scratch_authority_active
        && micros_user_address_space_process_handles_equal(
            context->scratch_authority.process,
            context->process
        )
        && context->scratch_authority.root_physical_address
            == context->resolved->address_space_root
        && !context->scratch_leaf_found
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
validate_owned_frame_set(
    const struct user_address_space_validation_context *context
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < context->ledger->managed_frame_count;
        ++frame_index
    ) {
        struct micros_frame_owner owner =
            context->ledger->owners[frame_index];
        bool exact_process = (
            owner.slot == context->process.slot
            && owner.generation == context->process.generation
        );

        if (
            exact_process
            && (
                owner.kind
                    == MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
                || owner.kind == context->leaf_owner_kind
            )
        ) {
            if (
                !scratch_bitmap_contains(
                    context->lease,
                    context->lease->reachable_bitmap,
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
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_validate_locked(
    struct micros_user_address_space_runtime_view *runtime,
    const struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    const struct micros_process **resolved_process
)
{
    struct user_address_space_validation_context context;
    enum micros_user_address_space_error error;

    error = load_validation_context(
        runtime,
        lease,
        process,
        &context
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = validate_shared_kernel_entries(&context);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = validate_user_tree(&context);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = validate_required_exceptional_leaves(&context);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = validate_owned_frame_set(&context);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (resolved_process != NULL) {
        *resolved_process = context.resolved;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_validate_all_process_roots_locked(
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
        error = micros_user_address_space_validate_locked(
            runtime,
            lease,
            process,
            NULL
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
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
    error = micros_user_address_space_validate_locked(
        &runtime,
        &scratch,
        process,
        NULL
    );
    release_scratch(&scratch);

done:
    (void)ledger;
    (void)objects;
    (void)kernel_report;
    (void)resolved;
    micros_user_address_space_runtime_unlock(saved_status);
    return error;
}

bool micros_user_address_space_vm_table_plan_matches_memory(
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
