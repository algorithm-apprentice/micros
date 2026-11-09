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
    uint64_t before_satp = micros_user_address_space_test_read_satp();
    uint64_t after_satp;
    uint64_t restored_satp;
    uint64_t process_satp = MICROS_RISCV_SATP_MODE_SV39 | (context->process_one_root >> 12);
    enum micros_user_address_space_error diagnostic;
    enum micros_user_address_space_error activation;
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
    destroy = micros_user_address_space_destroy(context->process_one);
    state_unchanged = micros_user_address_space_test_snapshot_matches(context)
        && micros_user_address_space_test_read_satp() == restored_satp && state_unchanged;
    uart_write("MEM04_ACTIVATION version=2");
    write_selected_field(" case=", case_id);
    write_selected_field(" diagnostic=", diagnostic);
    write_selected_field(" activation=", activation);
    write_selected_field(" destroy=", destroy);
    write_selected_field(" before_satp=", before_satp);
    write_selected_field(" after_satp=", after_satp);
    write_selected_field(" process_satp=", process_satp);
    write_selected_field(" restored_satp=", restored_satp);
    write_selected_field(" state_unchanged=", state_unchanged);
    uart_write("\n");
    uart_flush();
    return diagnostic == expected_error && destroy == expected_error
        && activation == MICROS_USER_ADDRESS_SPACE_OK && after_satp == process_satp
        && restored_satp == before_satp && state_unchanged;
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

struct isolated_release_case {
    struct micros_process_handle process;
    uint64_t root;
    uint64_t frame;
    uint64_t original_pte;
    uint64_t *leaf;
    uint64_t *alias;
    uint64_t free_before;
    uint64_t owned_before;
};

struct release_observation {
    uint64_t diagnostic;
    uint64_t release;
    uint64_t before_satp;
    uint64_t after_satp;
    uint64_t output;
    uint64_t selected_before;
    uint64_t selected_after;
    uint64_t owner_before;
    uint64_t owner_after;
    bool state_unchanged;
    bool effect_matches;
    bool unrelated_unchanged;
    bool data_unchanged;
    bool cleanup_complete;
};

static bool create_isolated_release_case(
    struct micros_user_address_space_test_context *context,
    struct isolated_release_case *test
)
{
    test->free_before = context->ledger->allocator->free_frame_count;
    test->owned_before = context->ledger->owned_frame_count;
    if (micros_process_create(context->objects, &test->process) != MICROS_KERNEL_OBJECT_OK
        || micros_user_address_space_create(test->process) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            test->process, MICROS_USER_VIRTUAL_BASE,
            MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE, &test->frame
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || !micros_user_address_space_test_resolve_process_root(context->objects, test->process, &test->root)
        || !micros_user_address_space_test_resolve_leaf_entry(
            context->objects, test->process, MICROS_USER_VIRTUAL_BASE, &test->leaf
        )
        || !micros_user_address_space_test_resolve_leaf_entry(
            context->objects, test->process, MICROS_USER_VIRTUAL_BASE + MICROS_SV39_PAGE_SIZE, &test->alias
        )
        || *test->alias != 0) {
        return false;
    }
    test->original_pte = *test->leaf;
    micros_user_address_space_test_fill_page(test->frame, UINT64_C(0x5a5a5a5a5a5a5a5a));
    return true;
}

static bool cleanup_isolated_release_case(
    struct micros_user_address_space_test_context *context,
    const struct isolated_release_case *test
)
{
    const struct micros_process *resolved;

    return micros_user_address_space_destroy(test->process) == MICROS_USER_ADDRESS_SPACE_OK
        && micros_frame_ownership_runtime_release_process(context->objects, test->process) == MICROS_KERNEL_OBJECT_OK
        && micros_process_resolve(context->objects, test->process, &resolved) == MICROS_KERNEL_OBJECT_ERROR_STALE
        && context->ledger->allocator->free_frame_count == test->free_before
        && context->ledger->owned_frame_count == test->owned_before
        && micros_frame_ownership_runtime_validate(context->objects) == MICROS_FRAME_OWNERSHIP_OK;
}

static bool isolated_table_is_rejected(
    struct micros_user_address_space_test_context *context,
    bool poison_root
)
{
    struct isolated_release_case test;
    struct micros_user_address_space_test_page_table *root;
    uint64_t saved_entry;
    uint64_t poison_entry;
    uint64_t output = UINT64_C(0xfeedfacefeedface);
    uint64_t before_satp;
    uint64_t after_satp;
    enum micros_user_address_space_error release;
    enum micros_user_address_space_error destroy;
    bool poison_unchanged;
    bool state_unchanged;
    bool cleanup_complete;

    if (!create_isolated_release_case(context, &test)
        || micros_sv39_make_table_pte(UINT64_C(0x1000), &poison_entry) != MICROS_SV39_OK
        || !micros_user_address_space_test_snapshot_runtime(context, test.process, MICROS_USER_VIRTUAL_BASE)) {
        return false;
    }
    root = micros_user_address_space_test_table_at(test.root);
    saved_entry = root->entries[MICROS_USER_ADDRESS_SPACE_TEST_USER_ROOT_INDEX];
    before_satp = micros_user_address_space_test_read_satp();
    if (poison_root) {
        context->objects->processes[test.process.slot].address_space_root = UINT64_C(0x1000);
    } else {
        root->entries[MICROS_USER_ADDRESS_SPACE_TEST_USER_ROOT_INDEX] = poison_entry;
    }
    release = micros_user_address_space_release_page(test.process, MICROS_USER_VIRTUAL_BASE, &output);
    destroy = micros_user_address_space_destroy(test.process);
    after_satp = micros_user_address_space_test_read_satp();
    poison_unchanged = poison_root
        ? context->objects->processes[test.process.slot].address_space_root == UINT64_C(0x1000)
        : root->entries[MICROS_USER_ADDRESS_SPACE_TEST_USER_ROOT_INDEX] == poison_entry;
    context->objects->processes[test.process.slot].address_space_root = test.root;
    root->entries[MICROS_USER_ADDRESS_SPACE_TEST_USER_ROOT_INDEX] = saved_entry;
    state_unchanged = micros_user_address_space_test_snapshot_matches(context);
    cleanup_complete = cleanup_isolated_release_case(context, &test);
    uart_write("MEM05_TABLE version=1");
    write_selected_field(" case=", poison_root ? 0 : 1);
    write_selected_field(" release=", release);
    write_selected_field(" destroy=", destroy);
    write_selected_field(" before_satp=", before_satp);
    write_selected_field(" after_satp=", after_satp);
    write_selected_field(" poison_unchanged=", poison_unchanged);
    write_selected_field(" state_unchanged=", state_unchanged);
    write_selected_field(" output_unchanged=", output == UINT64_C(0xfeedfacefeedface));
    write_selected_field(" cleanup_complete=", cleanup_complete);
    uart_write("\n");
    uart_flush();
    return release == MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        && destroy == MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        && before_satp == after_satp && poison_unchanged && state_unchanged
        && output == UINT64_C(0xfeedfacefeedface) && cleanup_complete;
}

static bool inject_isolated_release_corruption(
    const struct micros_user_address_space_test_context *context,
    struct isolated_release_case *test,
    unsigned int case_id
)
{
    if (case_id < 2) {
        return micros_sv39_make_leaf_pte(
            case_id == 0 ? context->process_two_frame : test->root,
            MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE | MICROS_SV39_PERMISSION_USER,
            test->leaf
        ) == MICROS_SV39_OK;
    }
    if (case_id == 2) {
        *test->leaf = (UINT64_C(1) << 63) | UINT64_C(1);
    } else if (case_id == 3) {
        *test->leaf = 0;
    } else if (case_id == 4) {
        *test->alias = test->original_pte;
    } else {
        return false;
    }
    return true;
}

static bool released_frame_index(
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    uint64_t *frame_index
)
{
    size_t index;

    for (index = 0; index < ledger->managed_range_count; ++index) {
        const struct micros_frame_range_snapshot *range = &ledger->managed_ranges[index];
        uint64_t offset;

        if (physical_address < range->base) {
            continue;
        }
        offset = physical_address - range->base;
        if (offset % MICROS_SV39_PAGE_SIZE == 0 && offset / MICROS_SV39_PAGE_SIZE < range->frame_count) {
            *frame_index = range->bitmap_offset + offset / MICROS_SV39_PAGE_SIZE;
            return true;
        }
    }
    return false;
}

static bool expect_isolated_release_delta(
    struct micros_user_address_space_test_context *context,
    const struct isolated_release_case *test
)
{
    uint64_t frame_index;
    uintptr_t leaf_address = (uintptr_t)test->leaf;
    size_t index;

    if (!released_frame_index(context->ledger, test->frame, &frame_index)
        || frame_index >= context->ledger->managed_frame_count
        || context->ownership_snapshot.owners[frame_index].kind != MICROS_FRAME_OWNER_PROCESS_USER) {
        return false;
    }
    context->ownership_snapshot.owners[frame_index] = (struct micros_frame_owner){0};
    context->ownership_snapshot.handoff_targets[frame_index] = MICROS_FRAME_HANDOFF_NONE;
    --context->ownership_snapshot.owned_frame_count;
    --context->ownership_snapshot.owner_counts[MICROS_FRAME_OWNER_PROCESS_USER];
    context->allocator_snapshot.allocated_bitmap[frame_index / 64] &= ~(UINT64_C(1) << (frame_index % 64));
    ++context->allocator_snapshot.free_frame_count;
    if (frame_index < context->allocator_snapshot.allocation_cursor) {
        context->allocator_snapshot.allocation_cursor = frame_index;
    }
    for (index = 0; index < context->tree_snapshot_count; ++index) {
        uintptr_t base = (uintptr_t)context->tree_snapshot_addresses[index];

        if (leaf_address >= base && leaf_address - base < MICROS_SV39_PAGE_SIZE) {
            context->tree_snapshots[index].entries[(leaf_address - base) / sizeof(uint64_t)] = 0;
            return true;
        }
    }
    return false;
}

static bool isolated_tables_match(
    const struct micros_user_address_space_test_context *context
)
{
    size_t index;

    for (index = 0; index < context->tree_snapshot_count; ++index) {
        if (!micros_user_address_space_test_bytes_equal(
            micros_user_address_space_test_table_at(context->tree_snapshot_addresses[index]),
            &context->tree_snapshots[index], sizeof(context->tree_snapshots[index])
        )) {
            return false;
        }
    }
    return true;
}

static bool isolated_user_data_matches(
    const struct micros_user_address_space_test_context *context
)
{
    size_t index;

    for (index = 0; index < context->user_snapshot_count; ++index) {
        if (!micros_user_address_space_test_bytes_equal(
            (const void *)(uintptr_t)context->user_snapshot_addresses[index],
            context->user_frame_snapshots[index], MICROS_SV39_PAGE_SIZE
        )) {
            return false;
        }
    }
    return true;
}

static bool isolated_metadata_matches(
    const struct micros_user_address_space_test_context *context
)
{
    size_t common_size = offsetof(struct micros_frame_ownership, handoff_targets)
        + sizeof(context->ledger->handoff_targets);

    /* Native MEM05 models check exact index deltas; the target checks its complete validity. */
    return micros_user_address_space_test_bytes_equal(&context->ownership_snapshot, context->ledger, common_size)
        && micros_user_address_space_test_bytes_equal(
            &context->allocator_snapshot, context->ledger->allocator, sizeof(context->allocator_snapshot)
        )
        && micros_user_address_space_test_bytes_equal(
            &context->objects_snapshot, context->objects, sizeof(context->objects_snapshot)
        )
        && micros_frame_ownership_validate(context->ledger) == MICROS_FRAME_OWNERSHIP_OK;
}

static void write_release_observation(
    unsigned int case_id,
    uint64_t frame,
    const struct release_observation *observation
)
{
    uart_write("MEM05_RELEASE version=1");
    write_selected_field(" case=", case_id);
    write_selected_field(" diagnostic=", observation->diagnostic);
    write_selected_field(" release=", observation->release);
    write_selected_field(" before_satp=", observation->before_satp);
    write_selected_field(" after_satp=", observation->after_satp);
    write_selected_field(" frame=", frame);
    write_selected_field(" output=", observation->output);
    write_selected_field(" selected_before=", observation->selected_before);
    write_selected_field(" selected_after=", observation->selected_after);
    write_selected_field(" owner_before=", observation->owner_before);
    write_selected_field(" owner_after=", observation->owner_after);
    write_selected_field(" state_unchanged=", observation->state_unchanged);
    write_selected_field(" effect_matches=", observation->effect_matches);
    write_selected_field(" unrelated_unchanged=", observation->unrelated_unchanged);
    write_selected_field(" data_unchanged=", observation->data_unchanged);
    write_selected_field(" cleanup_complete=", observation->cleanup_complete);
    uart_write("\n");
    uart_flush();
}

static bool isolated_release_is_scoped(
    struct micros_user_address_space_test_context *context,
    unsigned int case_id
)
{
    struct isolated_release_case test;
    struct release_observation observation;
    struct micros_frame_owner owner;
    enum micros_user_address_space_error expected =
        case_id == 2 ? MICROS_USER_ADDRESS_SPACE_ERROR_PTE
        : case_id == 3 ? MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED
        : case_id == 4 ? MICROS_USER_ADDRESS_SPACE_OK
        : MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;

    if (!create_isolated_release_case(context, &test)
        || !inject_isolated_release_corruption(context, &test, case_id)
        || !micros_user_address_space_test_snapshot_runtime(context, test.process, MICROS_USER_VIRTUAL_BASE)
        || micros_frame_ownership_lookup(context->ledger, test.frame, &owner) != MICROS_FRAME_OWNERSHIP_OK) {
        return false;
    }
    observation.owner_before = owner.kind;
    observation.before_satp = micros_user_address_space_test_read_satp();
    observation.selected_before = *test.leaf;
    observation.output = UINT64_C(0xfeedfacefeedface);
    observation.diagnostic = micros_user_address_space_validate(test.process);
    if (!micros_user_address_space_test_snapshot_matches(context)) {
        return false;
    }
    observation.release = micros_user_address_space_release_page(test.process, MICROS_USER_VIRTUAL_BASE, &observation.output);
    observation.after_satp = micros_user_address_space_test_read_satp();
    observation.selected_after = *test.leaf;
    observation.state_unchanged = micros_user_address_space_test_snapshot_matches(context);
    if (micros_frame_ownership_lookup(context->ledger, test.frame, &owner) != MICROS_FRAME_OWNERSHIP_OK) {
        return false;
    }
    observation.owner_after = owner.kind;
    observation.data_unchanged = isolated_user_data_matches(context);
    if (observation.release == MICROS_USER_ADDRESS_SPACE_OK) {
        observation.effect_matches = case_id == 4
            && expect_isolated_release_delta(context, &test)
            && isolated_metadata_matches(context) && isolated_tables_match(context)
            && observation.output == test.frame && observation.selected_after == 0
            && observation.owner_after == MICROS_FRAME_OWNER_FREE;
        observation.unrelated_unchanged = observation.effect_matches && observation.data_unchanged;
    } else {
        observation.effect_matches = observation.state_unchanged
            && observation.output == UINT64_C(0xfeedfacefeedface);
        observation.unrelated_unchanged = observation.state_unchanged;
    }
    if (case_id == 4) {
        *test.alias = 0;
    } else {
        *test.leaf = test.original_pte;
    }
    observation.cleanup_complete = cleanup_isolated_release_case(context, &test);
    write_release_observation(case_id, test.frame, &observation);
    return observation.diagnostic == (case_id == 2 ? MICROS_USER_ADDRESS_SPACE_ERROR_PTE : MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP)
        && observation.release == expected && observation.before_satp == observation.after_satp
        && observation.effect_matches && observation.unrelated_unchanged
        && observation.data_unchanged && observation.cleanup_complete;
}

static bool isolated_release_cases_pass(
    struct micros_user_address_space_test_context *context
)
{
    unsigned int case_id;

    if (!isolated_table_is_rejected(context, true) || !isolated_table_is_rejected(context, false)) {
        return false;
    }
    for (case_id = 0; case_id < 5; ++case_id) {
        if (!isolated_release_is_scoped(context, case_id)) {
            return false;
        }
    }
    return true;
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
        && isolated_release_cases_pass(context)
    );
}
