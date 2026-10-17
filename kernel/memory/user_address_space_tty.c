#include "kernel/memory/user_address_space_internal.h"

#include "arch/riscv64/mmu.h"
#include "kernel/tty_handoff_core.h"
#include "micros/frame_ownership_runtime.h"

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

static enum micros_user_address_space_error
prepare_tty_uart_leaf(
    const struct micros_frame_ownership *ledger,
    const struct micros_process *process,
    struct micros_user_address_space_tty_uart_plan *candidate
)
{
    struct micros_user_address_space_page_indices indices;
    struct micros_sv39_decoded_pte leaf_entry;
    struct micros_frame_owner ignored_owner;
    enum micros_frame_ownership_error ownership_error;
    enum micros_user_address_space_error error;
    uint64_t *leaf_pte;
    uint64_t leaf_table_physical_address;

    if (
        micros_user_address_space_read_page_indices(
            MICROS_TTY_UART_VIRTUAL_BASE,
            &indices
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    error = micros_user_address_space_locate_leaf_slot_locked(
        process,
        MICROS_TTY_UART_VIRTUAL_BASE,
        &leaf_entry,
        &leaf_pte,
        &leaf_table_physical_address
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (leaf_entry.kind != MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT;
    }
    ownership_error = micros_frame_ownership_lookup(
        ledger,
        MICROS_TTY_UART_PHYSICAL_BASE,
        &ignored_owner
    );
    if (ownership_error != MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED) {
        return ownership_error == MICROS_FRAME_OWNERSHIP_OK
            ? MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
            : MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    if (
        micros_sv39_make_leaf_pte(
            MICROS_TTY_UART_PHYSICAL_BASE,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_USER,
            &candidate->leaf_pte
        ) != MICROS_SV39_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    candidate->leaf_index = indices.leaf;
    candidate->leaf_table_physical_address =
        leaf_table_physical_address;
    return MICROS_USER_ADDRESS_SPACE_OK;
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
    enum micros_user_address_space_error error;
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
        &kernel_report
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
    error = prepare_tty_uart_leaf(
        ledger,
        resolved,
        &candidate
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    candidate.active = true;
    candidate.process = process;
    candidate.root_physical_address =
        resolved->address_space_root;
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
        &kernel_report
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
    error = micros_user_address_space_validate_locked(
        &runtime,
        &scratch,
        process,
        &resolved
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = micros_user_address_space_load_tty_device_authority(
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
    error = micros_user_address_space_lookup_leaf_locked(
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
    error = micros_user_address_space_classify_tty_device_leaf(
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
