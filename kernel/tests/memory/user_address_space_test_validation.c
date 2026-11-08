#include "kernel/tests/memory/user_address_space_test_internal.h"

#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "micros/sv39.h"

bool micros_user_address_space_test_root_matches_kernel(
    const struct micros_user_address_space_test_context *context,
    struct micros_process_handle process,
    uint64_t *process_root
)
{
    const struct micros_user_address_space_test_page_table *kernel_root;
    const struct micros_user_address_space_test_page_table *root;
    uint64_t root_address;
    size_t index;

    if (context == NULL || context->kernel_report == NULL
        || !micros_user_address_space_test_resolve_process_root(
            context->objects,
            process,
            &root_address
        )
        || root_address
            == context->kernel_report->root_physical_address) {
        return false;
    }
    kernel_root = micros_user_address_space_test_table_at(
        context->kernel_report->root_physical_address
    );
    root = micros_user_address_space_test_table_at(root_address);
    for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
        if (root->entries[index] != kernel_root->entries[index]) {
            return false;
        }
    }
    if (process_root != NULL) {
        *process_root = root_address;
    }
    return true;
}

bool micros_user_address_space_test_validate_kernel_roots(
    struct micros_user_address_space_test_context *context
)
{
    if (context == NULL
        || !micros_user_address_space_test_root_matches_kernel(
            context,
            context->process_one,
            &context->process_one_root
        )
        || !micros_user_address_space_test_root_matches_kernel(
            context,
            context->process_two,
            &context->process_two_root
        )
        || context->process_one_root == context->process_two_root) {
        return false;
    }
    return (
        micros_user_address_space_test_frame_has_exact_owner(
            context->ledger,
            context->process_one_root,
            MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
            context->process_one
        )
        && micros_user_address_space_test_frame_has_exact_owner(
            context->ledger,
            context->process_two_root,
            MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
            context->process_two
        )
    );
}

static void write_selected_field(const char *name, uint64_t value)
{
    uart_write(name);
    uart_write_hex64(value);
}

static bool poisoned_table_is_rejected(
    struct micros_user_address_space_test_context *context,
    bool poison_root
)
{
    struct micros_process *process =
        &context->objects->processes[context->process_one.slot];
    struct micros_user_address_space_test_page_table *root =
        micros_user_address_space_test_table_at(context->process_one_root);
    uint64_t saved_entry = root->entries[MICROS_USER_ADDRESS_SPACE_TEST_USER_ROOT_INDEX];
    uint64_t physical_address = UINT64_C(0xfeedfacefeedface);
    uint32_t permissions = UINT32_MAX;
    uint64_t before_satp = micros_user_address_space_test_read_satp();
    uint64_t after_satp;
    uint64_t poison_entry;
    enum micros_user_address_space_error lookup;
    enum micros_user_address_space_error activation;
    bool poison_unchanged;
    bool state_unchanged;
    bool outputs_unchanged;

    if (before_satp != (MICROS_RISCV_SATP_MODE_SV39
            | (context->kernel_report->root_physical_address >> 12))
        || micros_sv39_make_table_pte(UINT64_C(0x1000), &poison_entry) != MICROS_SV39_OK
        || !micros_user_address_space_test_snapshot_runtime(
            context, context->process_one, MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS
        )) {
        return false;
    }
    if (poison_root) {
        process->address_space_root = UINT64_C(0x1000);
    } else {
        root->entries[MICROS_USER_ADDRESS_SPACE_TEST_USER_ROOT_INDEX] = poison_entry;
    }
    lookup = micros_user_address_space_lookup(
        context->process_one, MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
        &physical_address, &permissions
    );
    activation = micros_user_address_space_activate(context->process_one);
    after_satp = micros_user_address_space_test_read_satp();
    poison_unchanged = poison_root
        ? process->address_space_root == UINT64_C(0x1000)
        : root->entries[MICROS_USER_ADDRESS_SPACE_TEST_USER_ROOT_INDEX] == poison_entry;
    process->address_space_root = context->process_one_root;
    root->entries[MICROS_USER_ADDRESS_SPACE_TEST_USER_ROOT_INDEX] = saved_entry;
    state_unchanged = micros_user_address_space_test_snapshot_matches(context);
    outputs_unchanged = physical_address == UINT64_C(0xfeedfacefeedface)
        && permissions == UINT32_MAX;
    uart_write("MEM04_TABLE version=1");
    write_selected_field(" case=", poison_root ? 0 : 1);
    write_selected_field(" lookup=", lookup);
    write_selected_field(" activation=", activation);
    write_selected_field(" before_satp=", before_satp);
    write_selected_field(" after_satp=", after_satp);
    write_selected_field(" poison_unchanged=", poison_unchanged);
    write_selected_field(" state_unchanged=", state_unchanged);
    write_selected_field(" outputs_unchanged=", outputs_unchanged);
    uart_write("\n");
    uart_flush();
    return lookup == MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        && activation == MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        && before_satp == after_satp
        && poison_unchanged && state_unchanged && outputs_unchanged;
}

static bool corrupted_tree_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t virtual_address,
    enum micros_user_address_space_error expected_error,
    uint32_t case_id
)
{
    uint64_t unchanged = UINT64_C(0xfeedfacefeedface);
    uint64_t before_satp = micros_user_address_space_test_read_satp();
    uint64_t after_satp;
    uint64_t restored_satp;
    uint64_t process_satp = MICROS_RISCV_SATP_MODE_SV39 | (context->process_one_root >> 12);
    enum micros_user_address_space_error diagnostic;
    enum micros_user_address_space_error activation;
    enum micros_user_address_space_error release;
    enum micros_user_address_space_error destroy;
    bool state_unchanged;

    if (before_satp != (MICROS_RISCV_SATP_MODE_SV39
            | (context->kernel_report->root_physical_address >> 12))
        || !micros_user_address_space_test_snapshot_runtime(
            context, context->process_one, virtual_address
        )) {
        return false;
    }
    diagnostic = micros_user_address_space_validate(context->process_one);
    state_unchanged = micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == before_satp;
    activation = micros_user_address_space_activate(context->process_one);
    after_satp = micros_user_address_space_test_read_satp();
    state_unchanged = micros_user_address_space_test_snapshot_matches(context) && state_unchanged;
    if (micros_user_address_space_activate_kernel() != MICROS_USER_ADDRESS_SPACE_OK) {
        return false;
    }
    restored_satp = micros_user_address_space_test_read_satp();
    release = micros_user_address_space_release_page(
        context->process_one, virtual_address, &unchanged
    );
    state_unchanged = micros_user_address_space_test_snapshot_matches(context) && state_unchanged;
    destroy = micros_user_address_space_destroy(context->process_one);
    state_unchanged = micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == restored_satp && state_unchanged;
    uart_write("MEM04_ACTIVATION version=1");
    write_selected_field(" case=", case_id);
    write_selected_field(" diagnostic=", diagnostic);
    write_selected_field(" activation=", activation);
    write_selected_field(" release=", release);
    write_selected_field(" destroy=", destroy);
    write_selected_field(" before_satp=", before_satp);
    write_selected_field(" after_satp=", after_satp);
    write_selected_field(" process_satp=", process_satp);
    write_selected_field(" restored_satp=", restored_satp);
    write_selected_field(" state_unchanged=", state_unchanged);
    write_selected_field(" output_unchanged=", unchanged == UINT64_C(0xfeedfacefeedface));
    uart_write("\n");
    uart_flush();
    return diagnostic == expected_error && release == expected_error && destroy == expected_error
        && activation == MICROS_USER_ADDRESS_SPACE_OK && after_satp == process_satp
        && restored_satp == before_satp && state_unchanged
        && unchanged == UINT64_C(0xfeedfacefeedface);
}

static bool foreign_owner_leaf_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t *leaf,
    uint64_t original_pte,
    uint32_t permissions
)
{
    uint64_t corrupted_pte;
    bool rejected;

    if (micros_sv39_make_leaf_pte(
            context->process_two_frame,
            permissions | MICROS_SV39_PERMISSION_USER,
            &corrupted_pte
        )
        != MICROS_SV39_OK) {
        return false;
    }
    *leaf = corrupted_pte;
    rejected = corrupted_tree_is_rejected(
        context,
        MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        0
    );
    if (rejected) {
        *leaf = original_pte;
    }
    return rejected;
}

static bool page_table_leaf_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t *leaf,
    uint64_t original_pte,
    uint32_t permissions
)
{
    uint64_t corrupted_pte;
    bool rejected;

    if (micros_sv39_make_leaf_pte(
            context->process_one_root,
            permissions | MICROS_SV39_PERMISSION_USER,
            &corrupted_pte
        )
        != MICROS_SV39_OK) {
        return false;
    }
    *leaf = corrupted_pte;
    rejected = corrupted_tree_is_rejected(
        context,
        MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        1
    );
    if (rejected) {
        *leaf = original_pte;
    }
    return rejected;
}

static bool malformed_leaf_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t *leaf,
    uint64_t original_pte
)
{
    bool rejected;

    *leaf = (UINT64_C(1) << 63) | UINT64_C(1);
    rejected = corrupted_tree_is_rejected(
        context,
        MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
        MICROS_USER_ADDRESS_SPACE_ERROR_PTE,
        2
    );
    if (rejected) {
        *leaf = original_pte;
    }
    return rejected;
}

static bool alias_leaf_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t original_pte
)
{
    uint64_t *alias_leaf;
    uint64_t original_alias_pte;
    bool rejected;

    if (!micros_user_address_space_test_resolve_leaf_entry(
            context->objects,
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_ALIAS_VIRTUAL_ADDRESS,
            &alias_leaf
        )) {
        return false;
    }
    original_alias_pte = *alias_leaf;
    *alias_leaf = original_pte;
    rejected = corrupted_tree_is_rejected(
        context,
        MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        3
    );
    if (rejected) {
        *alias_leaf = original_alias_pte;
    }
    return rejected;
}

static bool unreachable_owned_frame_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t *leaf,
    uint64_t original_pte
)
{
    bool rejected;

    *leaf = 0;
    rejected = corrupted_tree_is_rejected(
        context,
        MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        4
    );
    if (rejected) {
        *leaf = original_pte;
    }
    return rejected;
}

bool micros_user_address_space_test_reject_corruption(
    struct micros_user_address_space_test_context *context
)
{
    const uint32_t permissions
        = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;
    uint64_t *leaf;
    uint64_t original_pte;

    if (context == NULL
        || !micros_user_address_space_test_resolve_leaf_entry(
            context->objects,
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
            &leaf
        )) {
        return false;
    }
    original_pte = *leaf;
    return (
        poisoned_table_is_rejected(context, true)
        && poisoned_table_is_rejected(context, false)
        && foreign_owner_leaf_is_rejected(
            context,
            leaf,
            original_pte,
            permissions
        )
        && page_table_leaf_is_rejected(
            context,
            leaf,
            original_pte,
            permissions
        )
        && malformed_leaf_is_rejected(context, leaf, original_pte)
        && alias_leaf_is_rejected(context, original_pte)
        && micros_user_address_space_test_resolve_leaf_entry(
            context->objects,
            context->process_one,
            MICROS_USER_ADDRESS_SPACE_TEST_REUSE_VIRTUAL_ADDRESS,
            &leaf
        )
        && unreachable_owned_frame_is_rejected(
            context,
            leaf,
            original_pte
        )
    );
}
