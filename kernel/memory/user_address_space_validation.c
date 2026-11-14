#include "kernel/memory/user_address_space_internal.h"
#include "micros/work_observation.h"
#include "tests/qemu/vm_validation_work.h"

#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/vm/vm_handoff_runtime.h"
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
initialize_validation_context(
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
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error bind_validation_process(
    struct micros_user_address_space_runtime_view *runtime,
    struct user_address_space_validation_context *context
)
{
    enum micros_user_address_space_error error;

    if (micros_frame_ownership_runtime_validate_metadata(runtime->objects) != MICROS_FRAME_OWNERSHIP_OK) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    error = resolve_process(runtime, context->process, &context->resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (context->resolved->address_space_root == 0) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
    }
    return mark_owned_frame(context->ledger, context->lease, context->resolved->address_space_root,
                            MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE, context->process);
}

static enum micros_user_address_space_error load_validation_context(
    struct micros_user_address_space_runtime_view *runtime,
    const struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    struct user_address_space_validation_context *context
)
{
    enum micros_user_address_space_error error = initialize_validation_context(runtime, lease, process, context);

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
    return bind_validation_process(runtime, context);
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

static enum micros_user_address_space_error validate_context_root(
    struct user_address_space_validation_context *context,
    const struct micros_process **resolved_process
)
{
    enum micros_user_address_space_error error;

    error = validate_shared_kernel_entries(context);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = validate_user_tree(context);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = validate_required_exceptional_leaves(context);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = validate_owned_frame_set(context);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (resolved_process != NULL) {
        *resolved_process = context->resolved;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error micros_user_address_space_validate_locked(
    struct micros_user_address_space_runtime_view *runtime,
    const struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    const struct micros_process **resolved_process
)
{
#ifdef MICROS_TEST_BOOTSTRAP_COMMAND_COUNTS
    MICROS_TEST_WORK_HIT("boot02-root-locked", MICROS_WORK_OWNER_KERNEL_MEMORY,
                         MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL, 1);
#endif
    struct user_address_space_validation_context context;
    enum micros_user_address_space_error error = load_validation_context(runtime, lease, process, &context);

    return error == MICROS_USER_ADDRESS_SPACE_OK ? validate_context_root(&context, resolved_process) : error;
}

enum micros_user_address_space_error
micros_user_address_space_load_selected_bindings(
    const struct micros_user_address_space_runtime_view *runtime,
    struct micros_user_address_space_bindings *bindings)
{
    struct micros_user_address_space_bindings candidate;
    enum micros_vm_scratch_authority_status vm_status;
    enum micros_tty_device_authority_status tty_status;

    if (runtime == NULL || bindings == NULL || runtime->ledger == NULL || runtime->objects == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    vm_status = micros_vm_handoff_runtime_inspect_scratch_binding(&candidate.vm);
    if (vm_status != MICROS_VM_SCRATCH_AUTHORITY_NONE && vm_status != MICROS_VM_SCRATCH_AUTHORITY_ACTIVE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    tty_status = micros_tty_handoff_runtime_inspect_device_binding(runtime->objects, &candidate.tty);
    if (tty_status != MICROS_TTY_DEVICE_AUTHORITY_NONE && tty_status != MICROS_TTY_DEVICE_AUTHORITY_ACTIVE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    candidate.tty_active = tty_status == MICROS_TTY_DEVICE_AUTHORITY_ACTIVE;
    if (micros_frame_ownership_runtime_validate_metadata(runtime->objects) != MICROS_FRAME_OWNERSHIP_OK) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    bindings->vm = candidate.vm;
    bindings->tty = candidate.tty;
    bindings->tty_active = candidate.tty_active;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error micros_user_address_space_check_selected_table(
    const struct micros_frame_ownership *ledger,
    struct micros_process_handle process,
    uint64_t physical_address)
{
    struct micros_frame_owner owner;

    if (micros_frame_ownership_lookup(ledger, physical_address, &owner) != MICROS_FRAME_OWNERSHIP_OK) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    return micros_user_address_space_validate_frame_owner(
        ledger->phase, MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE, process, owner
    );
}

enum micros_user_address_space_error micros_user_address_space_classify_selected_leaf(
    const struct micros_user_address_space_runtime_view *runtime,
    struct micros_process_handle process,
    uint64_t virtual_address,
    const struct micros_sv39_decoded_pte *leaf,
    const struct micros_user_address_space_bindings *bindings,
    bool *exceptional)
{
    struct micros_frame_owner owner;
    enum micros_user_address_space_leaf_class classification;
    enum micros_user_address_space_error error;
    bool is_device;

    if (leaf->kind != MICROS_SV39_PTE_LEAF || (leaf->permissions & MICROS_SV39_PERMISSION_USER) == 0) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    error = micros_user_address_space_classify_tty_device_leaf(
        bindings->tty_active, &bindings->tty, process, runtime->process_root, virtual_address,
        leaf, &is_device
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (is_device) {
        *exceptional = true;
        return MICROS_USER_ADDRESS_SPACE_OK;
    }
    if (micros_frame_ownership_lookup(runtime->ledger, leaf->physical_address, &owner) != MICROS_FRAME_OWNERSHIP_OK) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    error = micros_user_address_space_classify_managed_leaf(
        runtime->ownership_phase, process, runtime->process_root, virtual_address,
        leaf, owner, &bindings->vm, &classification
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    *exceptional = classification != MICROS_USER_ADDRESS_SPACE_LEAF_REGULAR;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static bool kernel_root_owner_is_valid(struct micros_frame_owner owner)
{
    return owner.kind == MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
        && owner.generation == 0 && owner.slot == 0 && owner.reserved == 0;
}

static bool selected_kernel_root_is_valid(const struct micros_user_address_space_runtime_view *runtime)
{
    struct micros_frame_owner owner;

    return micros_frame_ownership_lookup(
        runtime->ledger, runtime->kernel_report->root_physical_address, &owner
    ) == MICROS_FRAME_OWNERSHIP_OK && kernel_root_owner_is_valid(owner);
}

enum micros_user_address_space_error micros_user_address_space_validate_vm_kernel_root(
    const struct micros_user_address_space_runtime_view *runtime
)
{
    struct micros_frame_reference reference;

    if (micros_frame_ownership_lookup_prevalidated(
            runtime->ledger, runtime->kernel_report->root_physical_address, &reference)
            != MICROS_FRAME_OWNERSHIP_OK
        || !kernel_root_owner_is_valid(reference.owner)
        || table_at(runtime->kernel_report->root_physical_address)
            ->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX] != 0) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error validate_selected_shared_entries(
    const struct micros_user_address_space_runtime_view *runtime)
{
    const struct micros_user_address_space_page_table *kernel_root =
        table_at(runtime->kernel_report->root_physical_address);
    const struct micros_user_address_space_page_table *root = table_at(runtime->process_root);
    size_t root_index;

    if (kernel_root->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX] != 0) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    for (root_index = 0; root_index < MICROS_SV39_TABLE_ENTRY_COUNT; ++root_index) {
        uint64_t root_pte;
        uint64_t kernel_pte;

        if (root_index == MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX) {
            continue;
        }
        root_pte = root->entries[root_index];
        kernel_pte = kernel_root->entries[root_index];
        if (root_pte != kernel_pte) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error validate_selected_private_descriptor(
    const struct micros_user_address_space_runtime_view *runtime,
    struct micros_process_handle process)
{
    const struct micros_user_address_space_page_table *root = table_at(runtime->process_root);
    struct micros_sv39_decoded_pte entry;
    enum micros_user_address_space_error error;

    error = decode_pte(root->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX], &entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (entry.kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_OK;
    }
    if (entry.kind != MICROS_SV39_PTE_TABLE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    if (entry.physical_address == runtime->process_root) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    return micros_user_address_space_check_selected_table(runtime->ledger, process, entry.physical_address);
}

static enum micros_user_address_space_error validate_selected_controller_slots(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_process_handle process,
    const struct micros_user_address_space_bindings *bindings)
{
    unsigned int slot;

    for (slot = 0; slot < 2; ++slot) {
        struct micros_process_handle owner = slot == 0 ? bindings->vm.process : bindings->tty.process;
        uint64_t root = slot == 0 ? bindings->vm.root_physical_address : bindings->tty.root_physical_address;
        uint64_t virtual_address = slot == 0 ? bindings->vm.virtual_address : MICROS_TTY_UART_VIRTUAL_BASE;
        struct micros_user_address_space_path path;
        enum micros_user_address_space_error error;

        if (owner.generation == 0 || !micros_user_address_space_process_handles_equal(owner, process)
            || root != runtime->process_root) {
            continue;
        }
        error = micros_user_address_space_select_path(runtime, process, virtual_address, bindings, &path);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error micros_user_address_space_validate_selected_root(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_process_handle process,
    const struct micros_user_address_space_bindings *bindings)
{
    enum micros_user_address_space_error error;

    error = micros_user_address_space_check_selected_table(runtime->ledger, process, runtime->process_root);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (!selected_kernel_root_is_valid(runtime)) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    error = validate_selected_shared_entries(runtime);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = validate_selected_private_descriptor(runtime, process);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    return validate_selected_controller_slots(runtime, process, bindings);
}

static bool next_root_process(
    const struct micros_kernel_objects *objects,
    size_t *cursor,
    struct micros_process_handle *process
)
{
    size_t index;

    for (index = *cursor; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_process *record =
            &objects->processes[index];

        if (
            record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || record->address_space_root == 0
        ) {
            continue;
        }
        *process = (struct micros_process_handle){
            .slot = (uint16_t)index,
            .generation = record->generation,
        };
        *cursor = index + 1;
        return true;
    }
    *cursor = MICROS_PROCESS_CAPACITY;
    return false;
}

enum micros_user_address_space_error micros_user_address_space_validate_all_process_roots_locked(
    struct micros_user_address_space_runtime_view *runtime,
    const struct micros_kernel_objects *objects,
    const struct micros_user_address_space_scratch_lease *lease
)
{
    MICROS_VM01_WORK_HIT("vm01-root-enumeration", MICROS_WORK_OWNER_KERNEL_MEMORY,
                        MICROS_WORK_KIND_RECORD_VISIT, 1);
    struct micros_process_handle process;
    size_t cursor = 0;

    while (next_root_process(objects, &cursor, &process)) {
        enum micros_user_address_space_error error =
            micros_user_address_space_validate_locked(runtime, lease, process, NULL);

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
#ifdef MICROS_TEST_BOOTSTRAP_COMMAND_COUNTS
    MICROS_TEST_WORK_HIT("boot02-root", MICROS_WORK_OWNER_KERNEL_MEMORY,
                         MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL, 1);
#endif
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
    struct micros_user_address_space_vm_leaf_plan stack_leaf;
    uint64_t root_physical, middle_physical, image_physical, stack_physical;
    uint64_t expected_link;
    size_t image_count;
    size_t index;

    if (
        plan == NULL
        || kernel_report == NULL
        || plan->entry_count < MICROS_APPLICATION_IMAGE_PAGE_MIN + 1
        || plan->entry_count
            > MICROS_APPLICATION_RESIDENT_PAGE_MAX
        || plan->reserved != 0
        || plan->root_index != MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX
        || plan->image_middle_index != 0
        || plan->stack_middle_index != MICROS_SV39_TABLE_ENTRY_COUNT - 1
    ) {
        return false;
    }
    root_physical = plan->table_frames[MICROS_USER_ADDRESS_SPACE_VM_TABLE_ROOT];
    middle_physical = plan->table_frames[MICROS_USER_ADDRESS_SPACE_VM_TABLE_LEVEL_ONE];
    image_physical = plan->table_frames[MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE];
    stack_physical = plan->table_frames[MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK];
    if (micros_sv39_make_table_pte(middle_physical, &expected_link) != MICROS_SV39_OK
        || expected_link != plan->root_pte
        || micros_sv39_make_table_pte(image_physical, &expected_link) != MICROS_SV39_OK
        || expected_link != plan->image_middle_pte
        || micros_sv39_make_table_pte(stack_physical, &expected_link) != MICROS_SV39_OK
        || expected_link != plan->stack_middle_pte) {
        return false;
    }
    image_count = plan->entry_count - 1;
    stack_leaf = plan->leaves[image_count];
    if (stack_leaf.table_role != MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK
        || stack_leaf.leaf_index != MICROS_SV39_TABLE_ENTRY_COUNT - 1 || stack_leaf.reserved != 0) {
        return false;
    }
    kernel_root = table_at(kernel_report->root_physical_address);
    root = table_at(root_physical);
    middle = table_at(middle_physical);
    image = table_at(image_physical);
    stack = table_at(stack_physical);
    for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
        uint64_t expected_root = index == plan->root_index
            ? plan->root_pte
            : kernel_root->entries[index];
        uint64_t expected_middle = 0;
        uint64_t expected_image = 0;
        uint64_t expected_stack = index == stack_leaf.leaf_index ? stack_leaf.pte : 0;

        if (index == plan->image_middle_index) {
            expected_middle = plan->image_middle_pte;
        } else if (index == plan->stack_middle_index) {
            expected_middle = plan->stack_middle_pte;
        }
        if (index < image_count) {
            const struct micros_user_address_space_vm_leaf_plan *leaf = &plan->leaves[index];

            if (leaf->table_role != MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE
                || leaf->leaf_index != index || leaf->reserved != 0) {
                return false;
            }
            expected_image = leaf->pte;
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

static enum micros_user_address_space_error collect_vm_plan_frames(
    const struct micros_frame_ownership *ledger,
    struct micros_frame_selection *selection,
    const struct micros_user_address_space_vm_table_plan *plan
)
{
    size_t total = plan->entry_count + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;

    for (size_t index = 0; index < total; ++index) {
        struct micros_frame_reference reference;
        uint64_t physical;
        enum micros_frame_owner_kind kind;

        if (index < plan->entry_count) {
            struct micros_sv39_decoded_pte leaf;

            if (micros_sv39_decode_pte(plan->leaves[index].pte, &leaf) != MICROS_SV39_OK
                || leaf.kind != MICROS_SV39_PTE_LEAF
                || (leaf.permissions & MICROS_SV39_PERMISSION_USER) == 0) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
            }
            physical = leaf.physical_address;
            kind = MICROS_FRAME_OWNER_VM_WIRED;
        } else {
            physical = plan->table_frames[index - plan->entry_count];
            kind = MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE;
        }
        if (micros_frame_ownership_lookup_prevalidated(ledger, physical, &reference)
                != MICROS_FRAME_OWNERSHIP_OK
            || reference.owner.kind != kind || reference.owner.slot != plan->process.slot
            || reference.owner.generation != plan->process.generation || reference.owner.reserved != 0
            || micros_frame_selection_contains(selection, reference.frame_index)
            || !micros_frame_selection_add(selection, reference.frame_index)) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static bool vm_owner_chain_matches_selection(
    const struct micros_frame_ownership *ledger,
    struct micros_process_handle process,
    const struct micros_frame_selection *selection,
    uint64_t expected_count
)
{
    uint32_t node = ledger->process_frames[process.slot].head;

    if (selection->frame_count != expected_count) {
        return false;
    }
    for (uint64_t index = 0; index < expected_count; ++index) {
        if (node == 0 || node > ledger->managed_frame_count
            || !micros_frame_selection_contains(selection, node - 1)) {
            return false;
        }
        node = ledger->owner_links[node - 1].next;
    }
    return node == 0;
}

enum micros_user_address_space_error micros_user_address_space_validate_vm_table_plan_locked(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_user_address_space_scratch_lease *lease,
    const struct micros_user_address_space_vm_table_plan *plan
)
{
    MICROS_VM01_WORK_HIT("vm01-memory-root", MICROS_WORK_OWNER_KERNEL_MEMORY,
                        MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL, 1);
    const struct micros_process *resolved;
    enum micros_user_address_space_error error;
    enum micros_frame_ownership_error ownership_error;
    uint64_t count;
    uint64_t expected_count;

    if (runtime == NULL || lease == NULL || plan == NULL
        || plan->entry_count < MICROS_APPLICATION_IMAGE_PAGE_MIN + 1
        || plan->entry_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    error = micros_user_address_space_runtime_require_read(runtime);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (runtime->ownership_phase != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
    }
    error = micros_user_address_space_runtime_resolve_process(runtime, plan->process, false, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (resolved->address_space_root != 0
        && resolved->address_space_root != plan->table_frames[MICROS_USER_ADDRESS_SPACE_VM_TABLE_ROOT]) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
    }
    if (!lease->active || lease->mode != MICROS_USER_ADDRESS_SPACE_SCRATCH_SELECTED
        || lease->reachable_selection == NULL || lease->release_selection == NULL
        || lease->reachable_bitmap != lease->reachable_selection->bitmap
        || lease->release_bitmap != lease->release_selection->bitmap
        || lease->frame_count != runtime->ledger->managed_frame_count
        || lease->word_count != (size_t)((lease->frame_count + 63) / 64)
        || lease->reachable_selection->frame_count != 0 || lease->reachable_selection->word_count != 0
        || lease->release_selection->frame_count != 0 || lease->release_selection->word_count != 0) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    expected_count = plan->entry_count + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
    if (runtime->ledger->process_frames[plan->process.slot].count > expected_count) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    ownership_error = micros_frame_ownership_count_process(runtime->ledger, plan->process, &count);
    if (ownership_error != MICROS_FRAME_OWNERSHIP_OK) {
        return micros_user_address_space_map_ownership_error(ownership_error);
    }
    if (count != expected_count
        || runtime->ledger->process_frames[plan->process.slot].count != expected_count) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    error = collect_vm_plan_frames(runtime->ledger, lease->reachable_selection, plan);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (!vm_owner_chain_matches_selection(runtime->ledger, plan->process,
                                          lease->reachable_selection, expected_count)) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    error = micros_user_address_space_validate_vm_kernel_root(runtime);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    return vm_table_plan_matches_memory(plan, runtime->kernel_report)
        ? MICROS_USER_ADDRESS_SPACE_OK : MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
}

static enum micros_user_address_space_error bootstrap_collect_frame(
    const struct micros_frame_ownership *ledger,
    struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    uint64_t physical_address,
    enum micros_frame_owner_kind kind,
    bool include_tables,
    struct micros_user_address_space_bootstrap_plan *plan
)
{
    struct micros_frame_owner owner;
    uint64_t frame_index;

    if (
        micros_frame_ownership_lookup(ledger, physical_address, &owner) != MICROS_FRAME_OWNERSHIP_OK
        || owner.kind != kind || owner.slot != process.slot || owner.generation != process.generation
        || !frame_index_for_physical_address(ledger, physical_address, &frame_index)
        || micros_frame_selection_contains(lease->reachable_selection, frame_index)
        || !micros_frame_selection_add(lease->reachable_selection, frame_index)
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    if (include_tables || kind == MICROS_FRAME_OWNER_PROCESS_USER) {
        if (micros_frame_selection_contains(lease->release_selection, frame_index)
            || !micros_frame_selection_add(lease->release_selection, frame_index)) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
    }
    if (kind == MICROS_FRAME_OWNER_PROCESS_USER
        && ledger->handoff_targets[frame_index] != MICROS_FRAME_HANDOFF_VM_WIRED) {
        plan->wired_targets = false;
    }
    ++plan->frame_count;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error bootstrap_collect_leaf_table(
    const struct micros_frame_ownership *ledger,
    struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    uint64_t physical_address,
    bool include_tables,
    struct micros_user_address_space_bootstrap_plan *plan
)
{
    const struct micros_user_address_space_page_table *table;
    struct micros_sv39_decoded_pte entry;
    enum micros_user_address_space_error error;
    size_t index;

    error = bootstrap_collect_frame(
        ledger, lease, process, physical_address, MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE, include_tables, plan
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    table = table_at(physical_address);
    for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
        error = decode_pte(table->entries[index], &entry);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        if (entry.kind == MICROS_SV39_PTE_ABSENT) {
            continue;
        }
        if (entry.kind != MICROS_SV39_PTE_LEAF || (entry.permissions & MICROS_SV39_PERMISSION_USER) == 0) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        }
        error = bootstrap_collect_frame(
            ledger, lease, process, entry.physical_address, MICROS_FRAME_OWNER_PROCESS_USER, include_tables, plan
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error bootstrap_collect_middle_table(
    const struct micros_frame_ownership *ledger,
    struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    uint64_t physical_address,
    bool include_tables,
    struct micros_user_address_space_bootstrap_plan *plan
)
{
    const struct micros_user_address_space_page_table *table;
    struct micros_sv39_decoded_pte entry;
    enum micros_user_address_space_error error;
    size_t index;

    error = bootstrap_collect_frame(
        ledger, lease, process, physical_address, MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE, include_tables, plan
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    table = table_at(physical_address);
    for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
        error = decode_pte(table->entries[index], &entry);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        if (entry.kind == MICROS_SV39_PTE_ABSENT) {
            continue;
        }
        if (entry.kind != MICROS_SV39_PTE_TABLE) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        }
        error = bootstrap_collect_leaf_table(ledger, lease, process, entry.physical_address, include_tables, plan);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error bootstrap_collect_root(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    bool include_tables,
    struct micros_user_address_space_bootstrap_plan *plan
)
{
    struct micros_user_address_space_bootstrap_plan candidate;
    const struct micros_process *resolved;
    const struct micros_user_address_space_page_table *root;
    struct micros_sv39_decoded_pte entry;
    enum micros_user_address_space_error error;

    if (runtime == NULL || lease == NULL || plan == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    error = micros_user_address_space_runtime_require_bootstrap(runtime);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (
        !lease->active || lease->mode != MICROS_USER_ADDRESS_SPACE_SCRATCH_SELECTED
        || lease->reachable_selection == NULL || lease->release_selection == NULL
        || lease->reachable_bitmap != lease->reachable_selection->bitmap
        || lease->release_bitmap != lease->release_selection->bitmap
        || lease->frame_count != runtime->ledger->managed_frame_count
        || lease->word_count != (size_t)((lease->frame_count + 63) / 64)
        || micros_frame_ownership_runtime_validate_metadata(runtime->objects) != MICROS_FRAME_OWNERSHIP_OK
        || !micros_frame_selection_clear(lease->reachable_selection)
        || (include_tables && !micros_frame_selection_clear(lease->release_selection))
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    error = resolve_process_with_root(runtime, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    candidate.process = resolved;
    candidate.root_physical_address = resolved->address_space_root;
    candidate.frame_count = 0;
    candidate.wired_targets = true;
    error = bootstrap_collect_frame(
        runtime->ledger, lease, process, candidate.root_physical_address,
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE, include_tables, &candidate
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (!selected_kernel_root_is_valid(runtime)) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    error = validate_selected_shared_entries(runtime);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    root = table_at(candidate.root_physical_address);
    error = decode_pte(root->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX], &entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (entry.kind != MICROS_SV39_PTE_ABSENT) {
        if (entry.kind != MICROS_SV39_PTE_TABLE) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        }
        error = bootstrap_collect_middle_table(
            runtime->ledger, lease, process, entry.physical_address, include_tables, &candidate
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
    }
    *plan = candidate;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_collect_bootstrap_release_locked(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    struct micros_user_address_space_bootstrap_plan *plan
)
{
    struct micros_user_address_space_bootstrap_plan candidate;
    struct micros_frame_owner owner;
    struct micros_frame_owner_links links;
    uint64_t count;
    uint64_t frame_index;
    uint32_t node;
    enum micros_frame_ownership_error ownership_error;
    enum micros_user_address_space_error error;

    if (plan == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    error = bootstrap_collect_root(runtime, lease, process, true, &candidate);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    ownership_error = micros_frame_ownership_count_process(runtime->ledger, process, &count);
    if (ownership_error != MICROS_FRAME_OWNERSHIP_OK) {
        return micros_user_address_space_map_ownership_error(ownership_error);
    }
    if (count != candidate.frame_count) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    node = runtime->ledger->process_frames[process.slot].head;
    for (; node != 0; node = links.next) {
        frame_index = node - 1;
        owner = runtime->ledger->owners[frame_index];
        links = runtime->ledger->owner_links[frame_index];
        if (owner.generation == process.generation
            && !micros_frame_selection_contains(lease->reachable_selection, frame_index)) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
    }
    *plan = candidate;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_collect_bootstrap_handoff_locked(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    uint64_t expected_owned_frames,
    struct micros_user_address_space_bootstrap_plan *plan
)
{
    struct micros_user_address_space_bootstrap_plan candidate;
    enum micros_user_address_space_error error;

    if (plan == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    error = bootstrap_collect_root(runtime, lease, process, false, &candidate);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (candidate.frame_count != expected_owned_frames) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    *plan = candidate;
    return MICROS_USER_ADDRESS_SPACE_OK;
}
