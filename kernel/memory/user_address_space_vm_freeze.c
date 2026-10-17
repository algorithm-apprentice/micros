#include "kernel/memory/user_address_space_internal.h"

#include "arch/riscv64/mmu.h"
#include "kernel/vm/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_object_runtime.h"

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

static bool bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
    const uint8_t *left_bytes = left;
    const uint8_t *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

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

static uint64_t active_root(void)
{
    return micros_user_address_space_active_root();
}

static _Noreturn void panic_invariant(const char *reason)
{
    micros_user_address_space_panic_invariant(reason);
}

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
        && micros_user_address_space_process_handles_equal(
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
        && micros_user_address_space_process_handles_equal(
            record->process,
            action->process
        )
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

struct vm_freeze_context {
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *process;
};

static enum micros_user_address_space_error
preflight_vm_freeze(
    struct micros_user_address_space_runtime_view *runtime,
    const struct micros_vm_load_freeze_action *action,
    struct vm_freeze_context *context
)
{
    const struct micros_vm_handoff_state *state;
    enum micros_user_address_space_error error;

    error = require_read_authority(
        runtime,
        &context->ledger,
        &context->objects,
        &context->kernel_report
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (
        context->ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
    }
    state = micros_vm_handoff_runtime_state();
    if (
        state == NULL
        || micros_vm_handoff_state_validate(state)
            != MICROS_VM_HANDOFF_OK
        || !freeze_action_matches_runtime(state, action)
        || active_root() != state->root_physical_address
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
    }
    error = resolve_process(
        runtime,
        action->process,
        &context->process
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    return process_is_empty_for_vm_freeze(context->process)
        ? MICROS_USER_ADDRESS_SPACE_OK
        : MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
}

static enum micros_user_address_space_error
validate_vm_freeze_address_spaces(
    struct micros_user_address_space_runtime_view *runtime,
    const struct vm_freeze_context *context,
    struct micros_user_address_space_scratch_lease *scratch
)
{
    if (
        micros_frame_ownership_runtime_validate(context->objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || table_at(context->kernel_report->root_physical_address)
                ->entries[MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX]
            != 0
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    return micros_user_address_space_validate_all_process_roots_locked(
        runtime,
        context->objects,
        scratch
    );
}

static enum micros_user_address_space_error
validate_vm_freeze_data_frames(
    const struct micros_frame_ownership *ledger,
    const struct micros_vm_load_freeze_action *action
)
{
    struct micros_frame_owner owner;
    size_t index;

    if (
        action->entry_count
            > MICROS_APPLICATION_RESIDENT_PAGE_MAX
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
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
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
validate_vm_freeze_table_frames(
    const struct micros_frame_ownership *ledger,
    const struct micros_vm_load_freeze_action *action
)
{
    struct micros_frame_owner owner;
    size_t index;

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
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
prepare_vm_freeze_candidate(
    const struct micros_vm_load_freeze_action *action,
    struct micros_user_address_space_vm_freeze_plan *candidate
)
{
    struct micros_user_address_space_vm_mapping
        mappings[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    enum micros_user_address_space_error error;

    clear_bytes(mappings, sizeof(mappings));
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
        &candidate->tables
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    candidate->phase =
        MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_PREPARED;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_freeze(
    const struct micros_vm_load_freeze_action *action,
    struct micros_user_address_space_vm_freeze_plan *plan
)
{
    struct micros_user_address_space_vm_freeze_plan candidate;
    struct vm_freeze_context context;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    if (action == NULL || plan == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    saved_status = micros_user_address_space_runtime_lock();
    error = preflight_vm_freeze(
        &runtime,
        action,
        &context
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = validate_vm_freeze_address_spaces(
        &runtime,
        &context,
        &scratch
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = validate_vm_freeze_data_frames(
        context.ledger,
        action
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = validate_vm_freeze_table_frames(
        context.ledger,
        action
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = prepare_vm_freeze_candidate(action, &candidate);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
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
        || !micros_user_address_space_vm_table_plan_matches_memory(
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
