#include "kernel/user_address_space_test_internal.h"

#include "micros/kernel_object_runtime.h"
#include "micros/sv39.h"

static struct micros_user_address_space_test_context test_context;

void micros_user_address_space_test_copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

bool micros_user_address_space_test_bytes_equal(
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

struct micros_user_address_space_test_context *
micros_user_address_space_test_context(void)
{
    return &test_context;
}

bool micros_user_address_space_test_acquire(
    struct micros_user_address_space_test_context *context
)
{
    if (context == NULL) {
        return false;
    }
    {
        unsigned char *bytes = (unsigned char *)context;
        size_t index;

        for (index = 0; index < sizeof(*context); ++index) {
            bytes[index] = 0;
        }
    }
    context->objects = micros_kernel_object_runtime_test_registry();
    context->kernel_report = micros_kernel_address_space_report();
    context->ledger = micros_frame_ownership_runtime_ledger();
    if (context->objects == NULL || context->kernel_report == NULL
        || context->ledger == NULL
        || context->ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || micros_frame_ownership_runtime_validate(context->objects)
            != MICROS_FRAME_OWNERSHIP_OK) {
        return false;
    }
    context->baseline_free
        = context->ledger->allocator->free_frame_count;
    context->baseline_owned = context->ledger->owned_frame_count;
    return true;
}

void micros_user_address_space_test_reset_fault(
    struct micros_user_address_space_test_context *context
)
{
    if (context != NULL) {
        context->sum_fault_state
            = MICROS_USER_ADDRESS_SPACE_TEST_SUM_FAULT_IDLE;
    }
}

struct micros_user_address_space_test_page_table *
micros_user_address_space_test_table_at(uint64_t physical_address)
{
    return (struct micros_user_address_space_test_page_table
                *)(uintptr_t)physical_address;
}

static bool physical_address_for_frame_index(
    const struct micros_frame_ownership *ledger,
    uint64_t frame_index,
    uint64_t *physical_address
)
{
    size_t range_index;

    if (ledger == NULL || physical_address == NULL
        || frame_index >= ledger->managed_frame_count) {
        return false;
    }
    for (range_index = 0; range_index < ledger->managed_range_count;
         ++range_index) {
        const struct micros_frame_range_snapshot *range
            = &ledger->managed_ranges[range_index];

        if (frame_index >= range->bitmap_offset
            && frame_index
                < range->bitmap_offset + range->frame_count) {
            *physical_address = range->base
                + (frame_index - range->bitmap_offset)
                    * MICROS_SV39_PAGE_SIZE;
            return true;
        }
    }
    return false;
}

uint64_t micros_user_address_space_test_read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

static bool owners_equal(
    struct micros_frame_owner left,
    struct micros_frame_owner right
)
{
    return (
        left.generation == right.generation && left.slot == right.slot
        && left.kind == right.kind && left.reserved == right.reserved
    );
}

bool micros_user_address_space_test_resolve_process_root(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint64_t *root
)
{
    const struct micros_process *resolved;

    if (root == NULL
        || micros_process_resolve(objects, process, &resolved)
            != MICROS_KERNEL_OBJECT_OK
        || resolved->address_space_root == 0) {
        return false;
    }
    *root = resolved->address_space_root;
    return true;
}

bool micros_user_address_space_test_resolve_leaf_entry(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint64_t virtual_address,
    uint64_t **leaf_entry
)
{
    struct micros_sv39_decoded_pte decoded;
    struct micros_user_address_space_test_page_table *table;
    uint64_t root;
    uint16_t index;

    if (leaf_entry == NULL
        || !micros_user_address_space_test_resolve_process_root(
            objects,
            process,
            &root
        )) {
        return false;
    }
    table = micros_user_address_space_test_table_at(root);
    if (micros_sv39_vpn_index(virtual_address, 2, &index)
            != MICROS_SV39_OK
        || micros_sv39_decode_pte(table->entries[index], &decoded)
            != MICROS_SV39_OK
        || decoded.kind != MICROS_SV39_PTE_TABLE) {
        return false;
    }
    table = micros_user_address_space_test_table_at(
        decoded.physical_address
    );
    if (micros_sv39_vpn_index(virtual_address, 1, &index)
            != MICROS_SV39_OK
        || micros_sv39_decode_pte(table->entries[index], &decoded)
            != MICROS_SV39_OK
        || decoded.kind != MICROS_SV39_PTE_TABLE) {
        return false;
    }
    table = micros_user_address_space_test_table_at(
        decoded.physical_address
    );
    if (micros_sv39_vpn_index(virtual_address, 0, &index)
        != MICROS_SV39_OK) {
        return false;
    }
    *leaf_entry = &table->entries[index];
    return true;
}

static bool add_tree_snapshot(
    struct micros_user_address_space_test_context *context,
    uint64_t physical_address
)
{
    if (context->tree_snapshot_count
        >= MICROS_USER_ADDRESS_SPACE_TEST_TREE_FRAME_CAPACITY) {
        return false;
    }
    context->tree_snapshot_addresses[context->tree_snapshot_count]
        = physical_address;
    micros_user_address_space_test_copy_bytes(
        &context->tree_snapshots[context->tree_snapshot_count],
        micros_user_address_space_test_table_at(physical_address),
        sizeof(context->tree_snapshots[context->tree_snapshot_count])
    );
    ++context->tree_snapshot_count;
    return true;
}

static bool snapshot_process_tree(
    struct micros_user_address_space_test_context *context,
    struct micros_process_handle process,
    uint64_t virtual_address
)
{
    struct micros_sv39_decoded_pte decoded;
    struct micros_user_address_space_test_page_table *table;
    uint64_t root;
    uint16_t index;

    context->tree_snapshot_count = 0;
    if (!micros_user_address_space_test_resolve_process_root(
            context->objects,
            process,
            &root
        )
        || !add_tree_snapshot(context, root)) {
        return false;
    }
    table = micros_user_address_space_test_table_at(root);
    if (micros_sv39_vpn_index(virtual_address, 2, &index)
            != MICROS_SV39_OK
        || micros_sv39_decode_pte(table->entries[index], &decoded)
            != MICROS_SV39_OK) {
        return false;
    }
    if (decoded.kind == MICROS_SV39_PTE_ABSENT) {
        return true;
    }
    if (decoded.kind != MICROS_SV39_PTE_TABLE
        || !add_tree_snapshot(context, decoded.physical_address)) {
        return false;
    }
    table = micros_user_address_space_test_table_at(
        decoded.physical_address
    );
    if (micros_sv39_vpn_index(virtual_address, 1, &index)
            != MICROS_SV39_OK
        || micros_sv39_decode_pte(table->entries[index], &decoded)
            != MICROS_SV39_OK) {
        return false;
    }
    if (decoded.kind == MICROS_SV39_PTE_ABSENT) {
        return true;
    }
    return (
        decoded.kind == MICROS_SV39_PTE_TABLE
        && add_tree_snapshot(context, decoded.physical_address)
    );
}

bool micros_user_address_space_test_snapshot_runtime(
    struct micros_user_address_space_test_context *context,
    struct micros_process_handle process,
    uint64_t virtual_address
)
{
    uint64_t frame_index;

    if (context == NULL || context->ledger == NULL
        || context->ledger->allocator == NULL
        || context->objects == NULL
        || !snapshot_process_tree(context, process, virtual_address)) {
        return false;
    }
    micros_user_address_space_test_copy_bytes(
        &context->ownership_snapshot,
        context->ledger,
        sizeof(context->ownership_snapshot)
    );
    micros_user_address_space_test_copy_bytes(
        &context->allocator_snapshot,
        context->ledger->allocator,
        sizeof(context->allocator_snapshot)
    );
    micros_user_address_space_test_copy_bytes(
        &context->objects_snapshot,
        context->objects,
        sizeof(context->objects_snapshot)
    );
    context->user_snapshot_count = 0;
    for (frame_index = 0;
         frame_index < context->ledger->managed_frame_count;
         ++frame_index) {
        uint64_t physical_address;

        if (context->ledger->owners[frame_index].kind
            != MICROS_FRAME_OWNER_PROCESS_USER) {
            continue;
        }
        if (context->user_snapshot_count
                >= MICROS_USER_ADDRESS_SPACE_TEST_USER_FRAME_CAPACITY
            || !physical_address_for_frame_index(
                context->ledger,
                frame_index,
                &physical_address
            )) {
            return false;
        }
        context->user_snapshot_addresses[context->user_snapshot_count]
            = physical_address;
        micros_user_address_space_test_copy_bytes(
            context->user_frame_snapshots[context->user_snapshot_count],
            (const void *)(uintptr_t)physical_address,
            MICROS_SV39_PAGE_SIZE
        );
        ++context->user_snapshot_count;
    }
    return true;
}

bool micros_user_address_space_test_snapshot_core(
    struct micros_user_address_space_test_context *context
)
{
    if (context == NULL || context->ledger == NULL
        || context->ledger->allocator == NULL
        || context->objects == NULL) {
        return false;
    }
    context->tree_snapshot_count = 0;
    context->user_snapshot_count = 0;
    micros_user_address_space_test_copy_bytes(
        &context->ownership_snapshot,
        context->ledger,
        sizeof(context->ownership_snapshot)
    );
    micros_user_address_space_test_copy_bytes(
        &context->allocator_snapshot,
        context->ledger->allocator,
        sizeof(context->allocator_snapshot)
    );
    micros_user_address_space_test_copy_bytes(
        &context->objects_snapshot,
        context->objects,
        sizeof(context->objects_snapshot)
    );
    return true;
}

bool micros_user_address_space_test_snapshot_matches(
    const struct micros_user_address_space_test_context *context
)
{
    size_t index;

    if (context == NULL || context->ledger == NULL
        || context->ledger->allocator == NULL
        || context->objects == NULL
        || !micros_user_address_space_test_bytes_equal(
            context->ledger,
            &context->ownership_snapshot,
            sizeof(context->ownership_snapshot)
        )
        || !micros_user_address_space_test_bytes_equal(
            context->ledger->allocator,
            &context->allocator_snapshot,
            sizeof(context->allocator_snapshot)
        )
        || !micros_user_address_space_test_bytes_equal(
            context->objects,
            &context->objects_snapshot,
            sizeof(context->objects_snapshot)
        )) {
        return false;
    }
    for (index = 0; index < context->tree_snapshot_count; ++index) {
        if (!micros_user_address_space_test_bytes_equal(
                micros_user_address_space_test_table_at(
                    context->tree_snapshot_addresses[index]
                ),
                &context->tree_snapshots[index],
                sizeof(context->tree_snapshots[index])
            )) {
            return false;
        }
    }
    for (index = 0; index < context->user_snapshot_count; ++index) {
        if (!micros_user_address_space_test_bytes_equal(
                (const void *)(uintptr_t)
                    context->user_snapshot_addresses[index],
                context->user_frame_snapshots[index],
                MICROS_SV39_PAGE_SIZE
            )) {
            return false;
        }
    }
    return true;
}

bool micros_user_address_space_test_page_is_zero(
    uint64_t physical_address
)
{
    const uint64_t *words
        = (const uint64_t *)(uintptr_t)physical_address;
    size_t index;

    for (index = 0; index < MICROS_SV39_PAGE_SIZE / sizeof(uint64_t);
         ++index) {
        if (words[index] != 0) {
            return false;
        }
    }
    return true;
}

void micros_user_address_space_test_fill_page(
    uint64_t physical_address,
    uint64_t value
)
{
    uint64_t *words = (uint64_t *)(uintptr_t)physical_address;
    size_t index;

    for (index = 0; index < MICROS_SV39_PAGE_SIZE / sizeof(uint64_t);
         ++index) {
        words[index] = value;
    }
}

bool micros_user_address_space_test_frame_has_exact_owner(
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    enum micros_frame_owner_kind kind,
    struct micros_process_handle process
)
{
    struct micros_frame_owner observed;
    struct micros_frame_owner expected;

    return (
        micros_frame_owner_make_process(kind, process, &expected)
            == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_lookup(
               ledger,
               physical_address,
               &observed
           ) == MICROS_FRAME_OWNERSHIP_OK
        && owners_equal(observed, expected)
    );
}
