#include "kernel/user_address_space_test_internal.h"

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

static bool corrupted_tree_is_rejected(
    struct micros_user_address_space_test_context *context,
    uint64_t virtual_address,
    enum micros_user_address_space_error expected_error
)
{
    uint64_t unchanged = UINT64_C(0xfeedfacefeedface);
    uint64_t expected_satp = micros_user_address_space_test_read_satp();

    if (!micros_user_address_space_test_snapshot_runtime(
            context,
            context->process_one,
            virtual_address
        )
        || micros_user_address_space_validate(context->process_one)
            != expected_error
        || !micros_user_address_space_test_snapshot_matches(context)
        || micros_user_address_space_test_read_satp()
            != expected_satp) {
        return false;
    }
    if (!micros_user_address_space_test_snapshot_runtime(
            context,
            context->process_one,
            virtual_address
        )
        || micros_user_address_space_activate(context->process_one)
            != expected_error
        || !micros_user_address_space_test_snapshot_matches(context)
        || micros_user_address_space_test_read_satp()
            != expected_satp) {
        return false;
    }
    if (!micros_user_address_space_test_snapshot_runtime(
            context,
            context->process_one,
            virtual_address
        )
        || micros_user_address_space_release_page(
               context->process_one,
               virtual_address,
               &unchanged
           ) != expected_error
        || unchanged != UINT64_C(0xfeedfacefeedface)
        || !micros_user_address_space_test_snapshot_matches(context)
        || micros_user_address_space_test_read_satp()
            != expected_satp) {
        return false;
    }
    return (
        micros_user_address_space_test_snapshot_runtime(
            context,
            context->process_one,
            virtual_address
        )
        && micros_user_address_space_destroy(context->process_one)
            == expected_error
        && micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == expected_satp
    );
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
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
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
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
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
        MICROS_USER_ADDRESS_SPACE_ERROR_PTE
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
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
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
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
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
        foreign_owner_leaf_is_rejected(
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
