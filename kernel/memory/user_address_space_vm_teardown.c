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
        && micros_user_address_space_process_handles_equal(
            record->process,
            process
        )
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

struct vm_teardown_context {
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *process;
};

static enum micros_user_address_space_error
preflight_vm_teardown(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_process_handle process,
    uint64_t root_physical_address,
    const uint64_t
        table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    const struct micros_vm_kernel_mapping_entry *entries,
    size_t entry_count,
    struct vm_teardown_context *context
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
        return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
    }
    error = resolve_process_with_root(
        runtime,
        process,
        &context->process
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (
        context->process->address_space_root
            != root_physical_address
        || context->process->live_thread_count != 0
        || context->process->primary_endpoint
            != MICROS_PROCESS_ENDPOINT_NONE
        || context->process->privilege_profile != 0
        || active_root() == root_physical_address
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
validate_vm_teardown_data_frames(
    const struct micros_frame_ownership *ledger,
    struct micros_process_handle process,
    const struct micros_vm_kernel_mapping_entry *entries,
    size_t entry_count
)
{
    struct micros_frame_owner owner;
    size_t index;

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
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
validate_vm_teardown_table_frames(
    const struct micros_frame_ownership *ledger,
    struct micros_process_handle process,
    const uint64_t
        table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT]
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
                table_frames[index],
                &owner
            ) != MICROS_FRAME_OWNERSHIP_OK
            || !owner_is_exact_process_kind(
                owner,
                MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
                process
            )
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
validate_vm_teardown_table_plan(
    struct micros_process_handle process,
    const uint64_t
        table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    const struct micros_vm_kernel_mapping_entry *entries,
    size_t entry_count,
    const struct micros_kernel_address_space_report *kernel_report
)
{
    struct micros_user_address_space_vm_mapping
        mappings[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    struct micros_user_address_space_vm_table_plan table_plan;
    enum micros_user_address_space_error error;

    clear_bytes(mappings, sizeof(mappings));
    copy_vm_action_mappings(entries, entry_count, mappings);
    error = micros_user_address_space_prepare_vm_table_plan(
        process,
        table_frames,
        mappings,
        entry_count,
        &table_plan
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    return micros_user_address_space_vm_table_plan_matches_memory(
        &table_plan,
        kernel_report
    )
        ? MICROS_USER_ADDRESS_SPACE_OK
        : MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
}

static void prepare_vm_teardown_candidate(
    struct micros_process_handle process,
    uint64_t root_physical_address,
    const uint64_t
        table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    const struct micros_vm_kernel_mapping_entry *entries,
    size_t entry_count,
    struct micros_user_address_space_vm_teardown_plan *candidate
)
{
    size_t index;

    candidate->active = true;
    candidate->entry_count = (uint32_t)entry_count;
    candidate->process = process;
    candidate->root_physical_address = root_physical_address;
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        candidate->table_frames[index] = table_frames[index];
    }
    for (index = 0; index < entry_count; ++index) {
        candidate->data_frames[index] =
            entries[index].physical_address;
    }
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
    struct micros_user_address_space_vm_teardown_plan candidate;
    struct vm_teardown_context context;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    if (
        table_frames == NULL
        || entries == NULL
        || plan == NULL
        || entry_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    saved_status = micros_user_address_space_runtime_lock();
    error = preflight_vm_teardown(
        &runtime,
        process,
        root_physical_address,
        table_frames,
        entries,
        entry_count,
        &context
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error =
        micros_user_address_space_validate_all_process_roots_locked(
        &runtime,
        context.objects,
        &scratch
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = validate_vm_teardown_data_frames(
        context.ledger,
        process,
        entries,
        entry_count
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = validate_vm_teardown_table_frames(
        context.ledger,
        process,
        table_frames
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = validate_vm_teardown_table_plan(
        process,
        table_frames,
        entries,
        entry_count,
        context.kernel_report
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    prepare_vm_teardown_candidate(
        process,
        root_physical_address,
        table_frames,
        entries,
        entry_count,
        &candidate
    );
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
