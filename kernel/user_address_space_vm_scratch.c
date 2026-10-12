#include "kernel/user_address_space_internal.h"

#include "arch/riscv64/mmu.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/sv39.h"

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

static struct micros_user_address_space_page_table *table_at(
    uint64_t physical_address
)
{
    return micros_user_address_space_table_at(physical_address);
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
        || !micros_user_address_space_process_handles_equal(
            observed->process,
            process
        )
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

struct vm_scratch_operation_context {
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_process *resolved;
};

static enum micros_user_address_space_error
preflight_vm_scratch_operation(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_process_handle process,
    uint64_t expected_root_physical_address,
    enum micros_vm_scratch_phase scratch_phase,
    uint64_t physical_address,
    struct vm_scratch_operation_context *context
)
{
    const struct micros_kernel_address_space_report *kernel_report;
    enum micros_user_address_space_error error;

    error = require_read_authority(
        runtime,
        &context->ledger,
        &context->objects,
        &kernel_report
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
    error = require_vm_scratch_state(
        process,
        expected_root_physical_address,
        scratch_phase,
        physical_address,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = resolve_process_with_root(
        runtime,
        process,
        &context->resolved
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    return context->resolved->address_space_root
            == expected_root_physical_address
        ? MICROS_USER_ADDRESS_SPACE_OK
        : MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
}

static enum micros_user_address_space_error
validate_vm_scratch_frame(
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    enum micros_frame_owner_kind expected_kind
)
{
    struct micros_frame_owner owner;

    if (
        micros_frame_ownership_lookup(
            ledger,
            physical_address,
            &owner
        ) != MICROS_FRAME_OWNERSHIP_OK
        || !owner_is_ownerless_kind(owner, expected_kind)
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
prepare_vm_scratch_mapping_leaf(
    const struct micros_process *process,
    uint64_t physical_address,
    struct micros_user_address_space_vm_scratch_plan *candidate
)
{
    struct micros_sv39_decoded_pte leaf_entry;
    enum micros_user_address_space_error error;
    uint64_t *leaf_pte;

    error = micros_user_address_space_locate_leaf_slot_locked(
        process,
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
        &leaf_entry,
        &leaf_pte,
        &candidate->leaf_table_physical_address
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (leaf_entry.kind != MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT;
    }
    if (
        micros_sv39_vpn_index(
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            0,
            &candidate->leaf_index
        ) != MICROS_SV39_OK
        || micros_sv39_make_leaf_pte(
            physical_address,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_USER,
            &candidate->leaf_pte
        ) != MICROS_SV39_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
prepare_vm_scratch_removal_leaf(
    const struct micros_process *process,
    uint64_t physical_address,
    struct micros_user_address_space_vm_scratch_plan *candidate
)
{
    struct micros_sv39_decoded_pte leaf_entry;
    enum micros_user_address_space_error error;
    uint64_t *leaf_pte;

    error = micros_user_address_space_locate_leaf_slot_locked(
        process,
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
        &leaf_entry,
        &leaf_pte,
        &candidate->leaf_table_physical_address
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (
        micros_sv39_vpn_index(
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            0,
            &candidate->leaf_index
        ) != MICROS_SV39_OK
        || micros_sv39_make_leaf_pte(
            physical_address,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_USER,
            &candidate->leaf_pte
        ) != MICROS_SV39_OK
        || leaf_entry.kind != MICROS_SV39_PTE_LEAF
        || *leaf_pte != candidate->leaf_pte
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
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
    struct vm_scratch_operation_context context;
    struct micros_user_address_space_vm_scratch_plan candidate;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
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
    error = preflight_vm_scratch_operation(
        &runtime,
        process,
        expected_root_physical_address,
        MICROS_VM_SCRATCH_CLEAR,
        0,
        &context
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = acquire_scratch(&runtime, &scratch);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_validate(context.objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done_with_scratch;
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
    if (active_root() != context.resolved->address_space_root) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done_with_scratch;
    }
    error = validate_vm_scratch_frame(
        context.ledger,
        physical_address,
        MICROS_FRAME_OWNER_FREE
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = prepare_vm_scratch_mapping_leaf(
        context.resolved,
        physical_address,
        &candidate
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    candidate.active = true;
    candidate.process = process;
    candidate.root_physical_address =
        context.resolved->address_space_root;
    candidate.physical_address = physical_address;
    copy_bytes(plan, &candidate, sizeof(candidate));
    error = MICROS_USER_ADDRESS_SPACE_OK;

done_with_scratch:
    release_scratch(&scratch);
done:
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
    struct vm_scratch_operation_context context;
    struct micros_user_address_space_vm_scratch_plan candidate;
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease scratch;
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
    error = preflight_vm_scratch_operation(
        &runtime,
        process,
        expected_root_physical_address,
        MICROS_VM_SCRATCH_MAPPED,
        physical_address,
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
    if (active_root() != context.resolved->address_space_root) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done_with_scratch;
    }
    error = validate_vm_scratch_frame(
        context.ledger,
        physical_address,
        MICROS_FRAME_OWNER_VM_TRANSFERABLE
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = prepare_vm_scratch_removal_leaf(
        context.resolved,
        physical_address,
        &candidate
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    candidate.active = true;
    candidate.process = process;
    candidate.root_physical_address =
        context.resolved->address_space_root;
    candidate.physical_address = physical_address;
    copy_bytes(plan, &candidate, sizeof(candidate));
    error = MICROS_USER_ADDRESS_SPACE_OK;

done_with_scratch:
    release_scratch(&scratch);
done:
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
