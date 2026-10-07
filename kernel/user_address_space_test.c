#include "micros/user_address_space.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/trap_context.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_buffer.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/sv39.h"

#define MICROS_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define MICROS_SCAUSE_CODE_MASK (MICROS_SCAUSE_INTERRUPT - 1)

enum {
    USER_ADDRESS_SPACE_TEST_LOAD_PAGE_FAULT = 13,
    TEST_TREE_FRAME_CAPACITY = 3,
    TEST_USER_FRAME_SNAPSHOT_CAPACITY = 4,
    TEST_USER_ROOT_INDEX = 1,
};

static const uint64_t TEST_USER_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE;
static const uint64_t TEST_REUSE_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + MICROS_SV39_PAGE_SIZE;
static const uint64_t TEST_ALIAS_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + (2 * MICROS_SV39_PAGE_SIZE);
static const uint64_t TEST_GUARD_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + (3 * MICROS_SV39_PAGE_SIZE);
static const uint64_t TEST_ACTIVE_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00400000);

struct test_page_table {
    uint64_t entries[MICROS_SV39_TABLE_ENTRY_COUNT];
};

enum sum_fault_state {
    SUM_FAULT_IDLE,
    SUM_FAULT_ARMED,
    SUM_FAULT_HANDLED,
};

void micros_user_address_space_sum_fault_trigger(uint64_t virtual_address);
uint64_t micros_user_address_space_sum_load(uint64_t virtual_address);

extern const unsigned char micros_user_address_space_sum_fault[];
extern const unsigned char micros_user_address_space_sum_resume[];

static struct micros_frame_ownership ownership_snapshot;
static struct micros_frame_allocator allocator_snapshot;
static struct micros_kernel_objects objects_snapshot;
static struct test_page_table
    tree_snapshots[TEST_TREE_FRAME_CAPACITY];
static uint64_t tree_snapshot_addresses[TEST_TREE_FRAME_CAPACITY];
static size_t tree_snapshot_count;
static unsigned char user_frame_snapshots[
    TEST_USER_FRAME_SNAPSHOT_CAPACITY
][MICROS_SV39_PAGE_SIZE];
static uint64_t user_snapshot_addresses[
    TEST_USER_FRAME_SNAPSHOT_CAPACITY
];
static size_t user_snapshot_count;
static volatile enum sum_fault_state sum_fault_state;
static volatile uint64_t sum_fault_virtual_address;

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static bool bytes_equal(
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

static struct test_page_table *table_at(uint64_t physical_address)
{
    return (struct test_page_table *)(uintptr_t)physical_address;
}

static bool physical_address_for_frame_index(
    const struct micros_frame_ownership *ledger,
    uint64_t frame_index,
    uint64_t *physical_address
)
{
    size_t range_index;

    if (
        ledger == NULL
        || physical_address == NULL
        || frame_index >= ledger->managed_frame_count
    ) {
        return false;
    }
    for (
        range_index = 0;
        range_index < ledger->managed_range_count;
        ++range_index
    ) {
        const struct micros_frame_range_snapshot *range =
            &ledger->managed_ranges[range_index];

        if (
            frame_index >= range->bitmap_offset
            && frame_index
                < range->bitmap_offset + range->frame_count
        ) {
            *physical_address = range->base
                + (
                    frame_index - range->bitmap_offset
                ) * MICROS_SV39_PAGE_SIZE;
            return true;
        }
    }
    return false;
}

static uint64_t read_satp(void)
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
        left.generation == right.generation
        && left.slot == right.slot
        && left.kind == right.kind
        && left.reserved == right.reserved
    );
}

static bool resolve_process_root(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint64_t *root
)
{
    const struct micros_process *resolved;

    if (
        root == NULL
        || micros_process_resolve(
            objects,
            process,
            &resolved
        ) != MICROS_KERNEL_OBJECT_OK
        || resolved->address_space_root == 0
    ) {
        return false;
    }
    *root = resolved->address_space_root;
    return true;
}

static bool resolve_leaf_entry(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint64_t virtual_address,
    uint64_t **leaf_entry
)
{
    struct micros_sv39_decoded_pte decoded;
    struct test_page_table *table;
    uint64_t root;
    uint16_t index;

    if (
        leaf_entry == NULL
        || !resolve_process_root(objects, process, &root)
    ) {
        return false;
    }
    table = table_at(root);
    if (
        micros_sv39_vpn_index(virtual_address, 2, &index)
            != MICROS_SV39_OK
        || micros_sv39_decode_pte(
            table->entries[index],
            &decoded
        ) != MICROS_SV39_OK
        || decoded.kind != MICROS_SV39_PTE_TABLE
    ) {
        return false;
    }
    table = table_at(decoded.physical_address);
    if (
        micros_sv39_vpn_index(virtual_address, 1, &index)
            != MICROS_SV39_OK
        || micros_sv39_decode_pte(
            table->entries[index],
            &decoded
        ) != MICROS_SV39_OK
        || decoded.kind != MICROS_SV39_PTE_TABLE
    ) {
        return false;
    }
    table = table_at(decoded.physical_address);
    if (
        micros_sv39_vpn_index(virtual_address, 0, &index)
            != MICROS_SV39_OK
    ) {
        return false;
    }
    *leaf_entry = &table->entries[index];
    return true;
}

static bool add_tree_snapshot(uint64_t physical_address)
{
    if (tree_snapshot_count >= TEST_TREE_FRAME_CAPACITY) {
        return false;
    }
    tree_snapshot_addresses[tree_snapshot_count] = physical_address;
    copy_bytes(
        &tree_snapshots[tree_snapshot_count],
        table_at(physical_address),
        sizeof(tree_snapshots[tree_snapshot_count])
    );
    ++tree_snapshot_count;
    return true;
}

static bool snapshot_process_tree(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint64_t virtual_address
)
{
    struct micros_sv39_decoded_pte decoded;
    struct test_page_table *table;
    uint64_t root;
    uint16_t index;

    tree_snapshot_count = 0;
    if (!resolve_process_root(objects, process, &root)) {
        return false;
    }
    if (!add_tree_snapshot(root)) {
        return false;
    }
    table = table_at(root);
    if (
        micros_sv39_vpn_index(virtual_address, 2, &index)
            != MICROS_SV39_OK
        || micros_sv39_decode_pte(
            table->entries[index],
            &decoded
        ) != MICROS_SV39_OK
    ) {
        return false;
    }
    if (decoded.kind == MICROS_SV39_PTE_ABSENT) {
        return true;
    }
    if (
        decoded.kind != MICROS_SV39_PTE_TABLE
        || !add_tree_snapshot(decoded.physical_address)
    ) {
        return false;
    }
    table = table_at(decoded.physical_address);
    if (
        micros_sv39_vpn_index(virtual_address, 1, &index)
            != MICROS_SV39_OK
        || micros_sv39_decode_pte(
            table->entries[index],
            &decoded
        ) != MICROS_SV39_OK
    ) {
        return false;
    }
    if (decoded.kind == MICROS_SV39_PTE_ABSENT) {
        return true;
    }
    return (
        decoded.kind == MICROS_SV39_PTE_TABLE
        && add_tree_snapshot(decoded.physical_address)
    );
}

static bool snapshot_runtime_state(
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint64_t virtual_address
)
{
    uint64_t frame_index;

    if (
        ledger == NULL
        || ledger->allocator == NULL
        || objects == NULL
        || !snapshot_process_tree(
            objects,
            process,
            virtual_address
        )
    ) {
        return false;
    }
    copy_bytes(
        &ownership_snapshot,
        ledger,
        sizeof(ownership_snapshot)
    );
    copy_bytes(
        &allocator_snapshot,
        ledger->allocator,
        sizeof(allocator_snapshot)
    );
    copy_bytes(
        &objects_snapshot,
        objects,
        sizeof(objects_snapshot)
    );
    user_snapshot_count = 0;
    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        uint64_t physical_address;

        if (
            ledger->owners[frame_index].kind
                != MICROS_FRAME_OWNER_PROCESS_USER
        ) {
            continue;
        }
        if (
            user_snapshot_count
                >= TEST_USER_FRAME_SNAPSHOT_CAPACITY
            || !physical_address_for_frame_index(
                ledger,
                frame_index,
                &physical_address
            )
        ) {
            return false;
        }
        user_snapshot_addresses[user_snapshot_count] =
            physical_address;
        copy_bytes(
            user_frame_snapshots[user_snapshot_count],
            (const void *)(uintptr_t)physical_address,
            MICROS_SV39_PAGE_SIZE
        );
        ++user_snapshot_count;
    }
    return true;
}

static bool snapshot_core_state(
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects
)
{
    if (
        ledger == NULL
        || ledger->allocator == NULL
        || objects == NULL
    ) {
        return false;
    }
    tree_snapshot_count = 0;
    user_snapshot_count = 0;
    copy_bytes(
        &ownership_snapshot,
        ledger,
        sizeof(ownership_snapshot)
    );
    copy_bytes(
        &allocator_snapshot,
        ledger->allocator,
        sizeof(allocator_snapshot)
    );
    copy_bytes(
        &objects_snapshot,
        objects,
        sizeof(objects_snapshot)
    );
    return true;
}

static bool runtime_state_matches_snapshot(
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects
)
{
    size_t index;

    if (
        ledger == NULL
        || ledger->allocator == NULL
        || objects == NULL
        || !bytes_equal(
            ledger,
            &ownership_snapshot,
            sizeof(ownership_snapshot)
        )
        || !bytes_equal(
            ledger->allocator,
            &allocator_snapshot,
            sizeof(allocator_snapshot)
        )
        || !bytes_equal(
            objects,
            &objects_snapshot,
            sizeof(objects_snapshot)
        )
    ) {
        return false;
    }
    for (index = 0; index < tree_snapshot_count; ++index) {
        if (
            !bytes_equal(
                table_at(tree_snapshot_addresses[index]),
                &tree_snapshots[index],
                sizeof(tree_snapshots[index])
            )
        ) {
            return false;
        }
    }
    for (index = 0; index < user_snapshot_count; ++index) {
        if (
            !bytes_equal(
                (const void *)(uintptr_t)
                    user_snapshot_addresses[index],
                user_frame_snapshots[index],
                MICROS_SV39_PAGE_SIZE
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool root_matches_kernel(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint64_t *process_root
)
{
    const struct micros_kernel_address_space_report *kernel_report =
        micros_kernel_address_space_report();
    const struct test_page_table *kernel_root;
    const struct test_page_table *root;
    uint64_t root_address;
    size_t index;

    if (
        kernel_report == NULL
        || !resolve_process_root(
            objects,
            process,
            &root_address
        )
        || root_address == kernel_report->root_physical_address
    ) {
        return false;
    }
    kernel_root = table_at(kernel_report->root_physical_address);
    root = table_at(root_address);
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

static bool page_is_zero(uint64_t physical_address)
{
    const uint64_t *words =
        (const uint64_t *)(uintptr_t)physical_address;
    size_t index;

    for (
        index = 0;
        index < MICROS_SV39_PAGE_SIZE / sizeof(uint64_t);
        ++index
    ) {
        if (words[index] != 0) {
            return false;
        }
    }
    return true;
}

static void fill_page(uint64_t physical_address, uint64_t value)
{
    uint64_t *words = (uint64_t *)(uintptr_t)physical_address;
    size_t index;

    for (
        index = 0;
        index < MICROS_SV39_PAGE_SIZE / sizeof(uint64_t);
        ++index
    ) {
        words[index] = value;
    }
}

static bool frame_has_exact_owner(
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    enum micros_frame_owner_kind kind,
    struct micros_process_handle process
)
{
    struct micros_frame_owner observed;
    struct micros_frame_owner expected;

    return (
        micros_frame_owner_make_process(
            kind,
            process,
            &expected
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_lookup(
            ledger,
            physical_address,
            &observed
        ) == MICROS_FRAME_OWNERSHIP_OK
        && owners_equal(observed, expected)
    );
}

static bool corrupted_tree_is_rejected(
    const struct micros_frame_ownership *ledger,
    struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint64_t virtual_address,
    enum micros_user_address_space_error expected_error
)
{
    uint64_t unchanged = UINT64_C(0xfeedfacefeedface);
    uint64_t expected_satp = read_satp();

    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            process,
            virtual_address
        )
        || micros_user_address_space_validate(process)
            != expected_error
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            process,
            virtual_address
        )
        || micros_user_address_space_activate(process)
            != expected_error
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            process,
            virtual_address
        )
        || micros_user_address_space_release_page(
            process,
            virtual_address,
            &unchanged
        ) != expected_error
        || unchanged != UINT64_C(0xfeedfacefeedface)
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }
    return (
        snapshot_runtime_state(
            ledger,
            objects,
            process,
            virtual_address
        )
        && micros_user_address_space_destroy(process)
            == expected_error
        && runtime_state_matches_snapshot(ledger, objects)
        && read_satp() == expected_satp
    );
}

static bool active_mutations_are_rejected(
    const struct micros_frame_ownership *ledger,
    struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint32_t permissions
)
{
    uint64_t physical_address;

    physical_address = UINT64_C(0xfeedfacefeedface);
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            process,
            TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_allocate_page(
            process,
            TEST_ACTIVE_VIRTUAL_ADDRESS,
            permissions,
            &physical_address
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || !runtime_state_matches_snapshot(ledger, objects)
    ) {
        return false;
    }

    physical_address = UINT64_C(0xfeedfacefeedface);
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            process,
            TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_release_page(
            process,
            TEST_USER_VIRTUAL_ADDRESS,
            &physical_address
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || !runtime_state_matches_snapshot(ledger, objects)
    ) {
        return false;
    }
    return (
        snapshot_runtime_state(
            ledger,
            objects,
            process,
            TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_destroy(process)
            == MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        && runtime_state_matches_snapshot(ledger, objects)
    );
}

static bool invalid_range_operations_are_rejected(
    const struct micros_frame_ownership *ledger,
    struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint64_t invalid_address,
    uint32_t permissions
)
{
    uint64_t physical_address;
    uint32_t observed_permissions;
    uint64_t expected_satp = read_satp();

    physical_address = UINT64_C(0xfeedfacefeedface);
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            process,
            TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_allocate_page(
            process,
            invalid_address,
            permissions,
            &physical_address
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_RANGE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }

    physical_address = UINT64_C(0xfeedfacefeedface);
    observed_permissions = UINT32_MAX;
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            process,
            TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_lookup(
            process,
            invalid_address,
            &physical_address,
            &observed_permissions
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_RANGE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || observed_permissions != UINT32_MAX
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }

    physical_address = UINT64_C(0xfeedfacefeedface);
    return (
        snapshot_runtime_state(
            ledger,
            objects,
            process,
            TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_release_page(
            process,
            invalid_address,
            &physical_address
        ) == MICROS_USER_ADDRESS_SPACE_ERROR_RANGE
        && physical_address == UINT64_C(0xfeedfacefeedface)
        && runtime_state_matches_snapshot(ledger, objects)
        && read_satp() == expected_satp
    );
}

static bool post_handoff_operations_are_revoked(
    const struct micros_frame_ownership *ledger,
    struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint32_t permissions
)
{
    const struct micros_kernel_address_space_report *kernel_report =
        micros_kernel_address_space_report();
    uint64_t physical_address;
    uint32_t observed_permissions;
    size_t contiguous_bytes;
    uint64_t expected_satp = read_satp();

    if (
        !snapshot_core_state(ledger, objects)
        || micros_user_address_space_create(process)
            != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }
    physical_address = UINT64_C(0xfeedfacefeedface);
    observed_permissions = UINT32_MAX;
    contiguous_bytes = SIZE_MAX;
    if (
        !snapshot_core_state(ledger, objects)
        || micros_user_address_space_translate(
            process,
            MICROS_USER_VIRTUAL_END,
            &physical_address,
            &observed_permissions,
            &contiguous_bytes
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || observed_permissions != UINT32_MAX
        || contiguous_bytes != SIZE_MAX
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }
    if (
        !snapshot_core_state(ledger, objects)
        || micros_user_address_space_destroy(process)
            != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }

    physical_address = UINT64_C(0xfeedfacefeedface);
    if (
        !snapshot_core_state(ledger, objects)
        || micros_user_address_space_allocate_page(
            process,
            TEST_USER_VIRTUAL_ADDRESS,
            permissions,
            &physical_address
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }

    physical_address = UINT64_C(0xfeedfacefeedface);
    if (
        !snapshot_core_state(ledger, objects)
        || micros_user_address_space_release_page(
            process,
            TEST_USER_VIRTUAL_ADDRESS,
            &physical_address
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PHASE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }

    physical_address = UINT64_C(0xfeedfacefeedface);
    observed_permissions = UINT32_MAX;
    if (
        !snapshot_core_state(ledger, objects)
        || micros_user_address_space_lookup(
            process,
            TEST_USER_VIRTUAL_ADDRESS,
            &physical_address,
            &observed_permissions
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || observed_permissions != UINT32_MAX
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }
    if (
        !snapshot_core_state(ledger, objects)
        || micros_user_address_space_validate(process)
            != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }
    if (
        !snapshot_core_state(ledger, objects)
        || micros_user_address_space_activate(process)
            != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }
    return (
        kernel_report != NULL
        && snapshot_core_state(ledger, objects)
        && micros_user_address_space_activate_kernel()
            == MICROS_USER_ADDRESS_SPACE_OK
        && read_satp()
            == (
                MICROS_RISCV_SATP_MODE_SV39
                | (kernel_report->root_physical_address >> 12)
            )
        && runtime_state_matches_snapshot(ledger, objects)
    );
}

static bool stale_operations_are_rejected(
    const struct micros_frame_ownership *ledger,
    struct micros_kernel_objects *objects,
    struct micros_process_handle stale_process,
    struct micros_process_handle live_process,
    uint32_t permissions
)
{
    uint64_t physical_address;
    uint32_t observed_permissions;
    uint64_t expected_satp = read_satp();

    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            live_process,
            TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_create(stale_process)
            != MICROS_USER_ADDRESS_SPACE_ERROR_STALE
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            live_process,
            TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_destroy(stale_process)
            != MICROS_USER_ADDRESS_SPACE_ERROR_STALE
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }

    physical_address = UINT64_C(0xfeedfacefeedface);
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            live_process,
            TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_allocate_page(
            stale_process,
            TEST_USER_VIRTUAL_ADDRESS,
            permissions,
            &physical_address
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_STALE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }

    physical_address = UINT64_C(0xfeedfacefeedface);
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            live_process,
            TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_release_page(
            stale_process,
            TEST_USER_VIRTUAL_ADDRESS,
            &physical_address
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_STALE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }

    physical_address = UINT64_C(0xfeedfacefeedface);
    observed_permissions = UINT32_MAX;
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            live_process,
            TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_lookup(
            stale_process,
            TEST_USER_VIRTUAL_ADDRESS,
            &physical_address,
            &observed_permissions
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_STALE
        || physical_address != UINT64_C(0xfeedfacefeedface)
        || observed_permissions != UINT32_MAX
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            live_process,
            TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_validate(stale_process)
            != MICROS_USER_ADDRESS_SPACE_ERROR_STALE
        || !runtime_state_matches_snapshot(ledger, objects)
        || read_satp() != expected_satp
    ) {
        return false;
    }
    return (
        snapshot_runtime_state(
            ledger,
            objects,
            live_process,
            TEST_USER_VIRTUAL_ADDRESS
        )
        && micros_user_address_space_activate(stale_process)
            == MICROS_USER_ADDRESS_SPACE_ERROR_STALE
        && runtime_state_matches_snapshot(ledger, objects)
        && read_satp() == expected_satp
    );
}

enum micros_user_address_space_test_trap_result
micros_user_address_space_handle_test_trap(
    struct micros_trap_frame *frame
)
{
    uint64_t cause;

    if (sum_fault_state != SUM_FAULT_ARMED) {
        return MICROS_USER_ADDRESS_SPACE_TEST_TRAP_INACTIVE;
    }
    if (
        frame == NULL
        || (frame->scause & MICROS_SCAUSE_INTERRUPT) != 0
        || (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) == 0
        || (frame->sstatus & MICROS_RISCV_SSTATUS_SUM) != 0
        || frame->hart_context == 0
    ) {
        return MICROS_USER_ADDRESS_SPACE_TEST_TRAP_MISMATCH;
    }
    cause = frame->scause & MICROS_SCAUSE_CODE_MASK;
    if (
        cause != USER_ADDRESS_SPACE_TEST_LOAD_PAGE_FAULT
        || frame->sepc
            != (uintptr_t)micros_user_address_space_sum_fault
        || frame->stval != sum_fault_virtual_address
    ) {
        return MICROS_USER_ADDRESS_SPACE_TEST_TRAP_MISMATCH;
    }
    sum_fault_state = SUM_FAULT_HANDLED;
    frame->sepc = (uintptr_t)micros_user_address_space_sum_resume;
    return MICROS_USER_ADDRESS_SPACE_TEST_TRAP_HANDLED;
}

bool micros_user_address_space_runtime_run_self_test(void)
{
    const uint32_t user_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    struct micros_process_handle process_one;
    struct micros_process_handle process_two;
    struct micros_process_handle replacement_process;
    struct micros_thread_handle thread;
    uint64_t baseline_free;
    uint64_t baseline_owned;
    uint64_t last_page_baseline_free;
    uint64_t last_page_baseline_owned;
    uint64_t last_page_baseline_process_owned;
    uint64_t observed_process_owned;
    uint64_t process_one_root;
    uint64_t process_two_root;
    uint64_t replacement_root;
    uint64_t process_one_frame;
    uint64_t process_two_frame;
    uint64_t guard_frame;
    uint64_t ipc_second_frame;
    uint64_t reused_frame;
    uint64_t released_frame;
    uint64_t unchanged;
    uint64_t lookup_physical;
    uint64_t *process_one_leaf;
    uint64_t original_process_one_pte;
    uint64_t original_alias_pte;
    uint64_t corrupted_pte;
    uint32_t lookup_permissions;
    struct micros_ipc_message ipc_expected;
    struct micros_ipc_message ipc_observed;
    struct micros_ipc_message ipc_unchanged;
    size_t contiguous_bytes;
    uintptr_t saved_status;
    size_t index;
    bool passed = false;

    saved_status = riscv_irq_save();
    objects = micros_kernel_object_runtime_test_registry();
    kernel_report = micros_kernel_address_space_report();
    ledger = micros_frame_ownership_runtime_ledger();
    if (
        objects == NULL
        || kernel_report == NULL
        || ledger == NULL
        || ledger->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto done;
    }
    baseline_free = ledger->allocator->free_frame_count;
    baseline_owned = ledger->owned_frame_count;

    if (
        micros_process_create(objects, &process_one)
            != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(objects, &process_two)
            != MICROS_KERNEL_OBJECT_OK
        || micros_user_address_space_create(process_one)
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_create(process_two)
            != MICROS_USER_ADDRESS_SPACE_OK
        || !root_matches_kernel(
            objects,
            process_one,
            &process_one_root
        )
        || !root_matches_kernel(
            objects,
            process_two,
            &process_two_root
        )
        || process_one_root == process_two_root
        || !frame_has_exact_owner(
            ledger,
            process_one_root,
            MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
            process_one
        )
        || !frame_has_exact_owner(
            ledger,
            process_two_root,
            MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
            process_two
        )
    ) {
        goto done;
    }

    unchanged = UINT64_C(0xfeedfacefeedface);
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            process_one,
            TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_allocate_page(
            process_one,
            TEST_USER_VIRTUAL_ADDRESS,
            0,
            &unchanged
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION
        || unchanged != UINT64_C(0xfeedfacefeedface)
        || !runtime_state_matches_snapshot(ledger, objects)
    ) {
        goto done;
    }
    {
        static const uint32_t invalid_permissions[] = {
            MICROS_SV39_PERMISSION_USER,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_USER,
            MICROS_SV39_PERMISSION_WRITE,
            MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_EXECUTE,
            UINT32_C(0x80000000),
        };

        for (
            index = 0;
            index
                < sizeof(invalid_permissions)
                    / sizeof(invalid_permissions[0]);
            ++index
        ) {
            unchanged = UINT64_C(0xfeedfacefeedface);
            if (
                !snapshot_runtime_state(
                    ledger,
                    objects,
                    process_one,
                    TEST_USER_VIRTUAL_ADDRESS
                )
                || micros_user_address_space_allocate_page(
                    process_one,
                    TEST_USER_VIRTUAL_ADDRESS,
                    invalid_permissions[index],
                    &unchanged
                ) != MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION
                || unchanged != UINT64_C(0xfeedfacefeedface)
                || !runtime_state_matches_snapshot(ledger, objects)
            ) {
                goto done;
            }
        }
    }

    for (index = 1; index <= 2; ++index) {
        unchanged = UINT64_C(0xfeedfacefeedface);
        micros_user_address_space_test_fail_after_allocations(index);
        if (
            !snapshot_runtime_state(
                ledger,
                objects,
                process_one,
                TEST_USER_VIRTUAL_ADDRESS
            )
            || micros_user_address_space_allocate_page(
                process_one,
                TEST_USER_VIRTUAL_ADDRESS,
                user_permissions,
                &unchanged
            ) != MICROS_USER_ADDRESS_SPACE_ERROR_ALLOCATION
            || unchanged != UINT64_C(0xfeedfacefeedface)
            || !runtime_state_matches_snapshot(ledger, objects)
        ) {
            goto done;
        }
    }
    micros_user_address_space_test_fail_after_allocations(SIZE_MAX);

    {
        static const uint64_t invalid_addresses[] = {
            MICROS_USER_VIRTUAL_BASE - MICROS_SV39_PAGE_SIZE,
            MICROS_USER_VIRTUAL_BASE + 1,
            MICROS_USER_VIRTUAL_END,
        };

        for (
            index = 0;
            index
                < sizeof(invalid_addresses)
                    / sizeof(invalid_addresses[0]);
            ++index
        ) {
            if (
                !invalid_range_operations_are_rejected(
                    ledger,
                    objects,
                    process_one,
                    invalid_addresses[index],
                    user_permissions
                )
            ) {
                goto done;
            }
        }
    }

    last_page_baseline_free = ledger->allocator->free_frame_count;
    last_page_baseline_owned = ledger->owned_frame_count;
    if (
        micros_frame_ownership_count_process(
            ledger,
            process_one,
            &last_page_baseline_process_owned
        ) != MICROS_FRAME_OWNERSHIP_OK
        || table_at(process_one_root)->entries[
            TEST_USER_ROOT_INDEX
        ] != 0
    ) {
        goto done;
    }
    if (
        micros_user_address_space_allocate_page(
            process_one,
            MICROS_USER_VIRTUAL_END - MICROS_SV39_PAGE_SIZE,
            user_permissions,
            &unchanged
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        goto done;
    }
    lookup_physical = UINT64_MAX;
    lookup_permissions = UINT32_MAX;
    if (
        micros_user_address_space_lookup(
            process_one,
            MICROS_USER_VIRTUAL_END - MICROS_SV39_PAGE_SIZE,
            &lookup_physical,
            &lookup_permissions
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || lookup_physical != unchanged
        || lookup_permissions != user_permissions
        || micros_user_address_space_release_page(
            process_one,
            MICROS_USER_VIRTUAL_END - MICROS_SV39_PAGE_SIZE,
            &released_frame
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released_frame != unchanged
        || ledger->allocator->free_frame_count
            != last_page_baseline_free
        || ledger->owned_frame_count != last_page_baseline_owned
        || micros_frame_ownership_count_process(
            ledger,
            process_one,
            &observed_process_owned
        ) != MICROS_FRAME_OWNERSHIP_OK
        || observed_process_owned
            != last_page_baseline_process_owned
        || table_at(process_one_root)->entries[
            TEST_USER_ROOT_INDEX
        ] != 0
        || micros_user_address_space_validate(process_one)
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        goto done;
    }

    if (
        micros_user_address_space_allocate_page(
            process_one,
            TEST_USER_VIRTUAL_ADDRESS,
            user_permissions,
            &process_one_frame
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            process_two,
            TEST_USER_VIRTUAL_ADDRESS,
            user_permissions,
            &process_two_frame
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            process_one,
            TEST_GUARD_VIRTUAL_ADDRESS,
            user_permissions,
            &guard_frame
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || process_one_frame == process_two_frame
        || guard_frame == process_one_frame
        || guard_frame == process_two_frame
        || !page_is_zero(process_one_frame)
        || !page_is_zero(process_two_frame)
        || !page_is_zero(guard_frame)
        || !frame_has_exact_owner(
            ledger,
            process_one_frame,
            MICROS_FRAME_OWNER_PROCESS_USER,
            process_one
        )
        || !frame_has_exact_owner(
            ledger,
            process_two_frame,
            MICROS_FRAME_OWNER_PROCESS_USER,
            process_two
        )
        || !frame_has_exact_owner(
            ledger,
            guard_frame,
            MICROS_FRAME_OWNER_PROCESS_USER,
            process_one
        )
        || micros_user_address_space_validate(process_one)
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_validate(process_two)
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        goto done;
    }

    lookup_physical = UINT64_MAX;
    lookup_permissions = UINT32_MAX;
    if (
        micros_user_address_space_lookup(
            process_one,
            TEST_USER_VIRTUAL_ADDRESS,
            &lookup_physical,
            &lookup_permissions
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || lookup_physical != process_one_frame
        || lookup_permissions != user_permissions
    ) {
        goto done;
    }
    lookup_physical = UINT64_MAX;
    lookup_permissions = UINT32_MAX;
    if (
        micros_user_address_space_lookup(
            process_two,
            TEST_USER_VIRTUAL_ADDRESS,
            &lookup_physical,
            &lookup_permissions
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || lookup_physical != process_two_frame
        || lookup_permissions != user_permissions
    ) {
        goto done;
    }
    lookup_physical = UINT64_C(0xfeedfacefeedface);
    lookup_permissions = UINT32_MAX;
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            process_one,
            TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_lookup(
            process_one,
            TEST_REUSE_VIRTUAL_ADDRESS,
            &lookup_physical,
            &lookup_permissions
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED
        || lookup_physical != UINT64_C(0xfeedfacefeedface)
        || lookup_permissions != UINT32_MAX
        || !runtime_state_matches_snapshot(ledger, objects)
    ) {
        goto done;
    }

    if (
        micros_user_address_space_allocate_page(
            process_two,
            TEST_REUSE_VIRTUAL_ADDRESS,
            user_permissions,
            &ipc_second_frame
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        goto done;
    }
    for (index = 0; index < sizeof(ipc_expected); ++index) {
        ((unsigned char *)&ipc_expected)[index] =
            (unsigned char)(UINT8_C(0x40) + index);
        if (index < 32) {
            ((unsigned char *)(uintptr_t)process_two_frame)[
                MICROS_SV39_PAGE_SIZE - 32 + index
            ] = ((unsigned char *)&ipc_expected)[index];
        } else {
            ((unsigned char *)(uintptr_t)ipc_second_frame)[index - 32] =
                ((unsigned char *)&ipc_expected)[index];
        }
    }
    lookup_physical = 0;
    lookup_permissions = 0;
    contiguous_bytes = 0;
    if (
        micros_user_address_space_translate(
            process_two,
            TEST_REUSE_VIRTUAL_ADDRESS - 32,
            &lookup_physical,
            &lookup_permissions,
            &contiguous_bytes
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || lookup_physical
            != process_two_frame + MICROS_SV39_PAGE_SIZE - 32
        || lookup_permissions != user_permissions
        || contiguous_bytes != 32
        || micros_ipc_buffer_snapshot(
            process_two,
            TEST_REUSE_VIRTUAL_ADDRESS - 32,
            MICROS_IPC_BUFFER_READ | MICROS_IPC_BUFFER_WRITE,
            &ipc_observed
        ) != MICROS_IPC_BUFFER_OK
        || !bytes_equal(
            &ipc_observed,
            &ipc_expected,
            sizeof(ipc_expected)
        )
    ) {
        goto done;
    }
    for (index = 0; index < sizeof(ipc_expected); ++index) {
        ((unsigned char *)&ipc_expected)[index] ^=
            (unsigned char)(UINT8_C(0x80) + index);
    }
    if (
        micros_ipc_buffer_write(
            process_two,
            TEST_REUSE_VIRTUAL_ADDRESS - 32,
            &ipc_expected
        ) != MICROS_IPC_BUFFER_OK
        || !bytes_equal(
            (const void *)(uintptr_t)(
                process_two_frame + MICROS_SV39_PAGE_SIZE - 32
            ),
            &ipc_expected,
            32
        )
        || !bytes_equal(
            (const void *)(uintptr_t)ipc_second_frame,
            (const unsigned char *)&ipc_expected + 32,
            32
        )
        || micros_ipc_buffer_validate(
            process_two,
            TEST_REUSE_VIRTUAL_ADDRESS - 31,
            MICROS_IPC_BUFFER_READ
        ) != MICROS_IPC_BUFFER_ERROR_ARGUMENT
        || micros_ipc_buffer_validate(
            process_two,
            TEST_ALIAS_VIRTUAL_ADDRESS,
            MICROS_IPC_BUFFER_READ
        ) != MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT
        || micros_user_address_space_release_page(
            process_two,
            TEST_REUSE_VIRTUAL_ADDRESS,
            &released_frame
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released_frame != ipc_second_frame
    ) {
        goto done;
    }
    for (index = 0; index < sizeof(ipc_unchanged); ++index) {
        ((unsigned char *)&ipc_unchanged)[index] =
            (unsigned char)(UINT8_C(0xa0) + index);
    }
    ipc_observed = ipc_unchanged;
    if (
        micros_ipc_buffer_snapshot(
            process_two,
            TEST_ALIAS_VIRTUAL_ADDRESS,
            MICROS_IPC_BUFFER_READ,
            &ipc_observed
        ) != MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT
        || !bytes_equal(
            &ipc_observed,
            &ipc_unchanged,
            sizeof(ipc_unchanged)
        )
        || micros_ipc_buffer_snapshot(
            process_two,
            TEST_USER_VIRTUAL_ADDRESS,
            MICROS_IPC_BUFFER_WRITE,
            &ipc_observed
        ) != MICROS_IPC_BUFFER_ERROR_ARGUMENT
        || micros_ipc_buffer_validate(
            process_two,
            MICROS_USER_VIRTUAL_END - 32,
            MICROS_IPC_BUFFER_READ
        ) != MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT
        || micros_user_address_space_allocate_page(
            process_two,
            TEST_REUSE_VIRTUAL_ADDRESS,
            MICROS_SV39_PERMISSION_READ,
            &ipc_second_frame
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_ipc_buffer_validate(
            process_two,
            TEST_REUSE_VIRTUAL_ADDRESS,
            MICROS_IPC_BUFFER_WRITE
        ) != MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT
        || micros_ipc_buffer_validate(
            process_two,
            TEST_REUSE_VIRTUAL_ADDRESS,
            MICROS_IPC_BUFFER_READ
        ) != MICROS_IPC_BUFFER_OK
        || micros_user_address_space_release_page(
            process_two,
            TEST_REUSE_VIRTUAL_ADDRESS,
            &released_frame
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released_frame != ipc_second_frame
    ) {
        goto done;
    }

    fill_page(
        process_one_frame,
        UINT64_C(0x1111111111111111)
    );
    fill_page(
        process_two_frame,
        UINT64_C(0x2222222222222222)
    );

    unchanged = UINT64_C(0xfeedfacefeedface);
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            process_one,
            TEST_USER_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_allocate_page(
            process_one,
            TEST_USER_VIRTUAL_ADDRESS,
            user_permissions,
            &unchanged
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT
        || unchanged != UINT64_C(0xfeedfacefeedface)
        || !runtime_state_matches_snapshot(ledger, objects)
    ) {
        goto done;
    }

    if (
        micros_user_address_space_activate(process_one)
            != MICROS_USER_ADDRESS_SPACE_OK
        || read_satp()
            != (
                MICROS_RISCV_SATP_MODE_SV39
                | (process_one_root >> 12)
            )
    ) {
        goto done;
    }
    sum_fault_virtual_address = TEST_USER_VIRTUAL_ADDRESS;
    sum_fault_state = SUM_FAULT_ARMED;
    micros_user_address_space_sum_fault_trigger(
        TEST_USER_VIRTUAL_ADDRESS
    );
    if (
        sum_fault_state != SUM_FAULT_HANDLED
        || micros_user_address_space_sum_load(
            TEST_USER_VIRTUAL_ADDRESS
        ) != UINT64_C(0x1111111111111111)
    ) {
        goto done;
    }
    if (
        !active_mutations_are_rejected(
            ledger,
            objects,
            process_one,
            user_permissions
        )
        || micros_user_address_space_activate(process_two)
            != MICROS_USER_ADDRESS_SPACE_OK
        || read_satp()
            != (
                MICROS_RISCV_SATP_MODE_SV39
                | (process_two_root >> 12)
            )
        || micros_user_address_space_sum_load(
            TEST_USER_VIRTUAL_ADDRESS
        ) != UINT64_C(0x2222222222222222)
        || micros_user_address_space_activate(process_one)
            != MICROS_USER_ADDRESS_SPACE_OK
        || read_satp()
            != (
                MICROS_RISCV_SATP_MODE_SV39
                | (process_one_root >> 12)
            )
        || micros_user_address_space_sum_load(
            TEST_USER_VIRTUAL_ADDRESS
        ) != UINT64_C(0x1111111111111111)
        || micros_user_address_space_activate_kernel()
            != MICROS_USER_ADDRESS_SPACE_OK
        || read_satp()
            != (
                MICROS_RISCV_SATP_MODE_SV39
                | (kernel_report->root_physical_address >> 12)
            )
    ) {
        goto done;
    }

    fill_page(
        process_one_frame,
        UINT64_C(0xa5a5a5a5a5a5a5a5)
    );
    if (
        micros_user_address_space_release_page(
            process_one,
            TEST_USER_VIRTUAL_ADDRESS,
            &released_frame
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released_frame != process_one_frame
        || micros_user_address_space_allocate_page(
            process_one,
            TEST_REUSE_VIRTUAL_ADDRESS,
            user_permissions,
            &reused_frame
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || reused_frame != process_one_frame
        || !page_is_zero(reused_frame)
        || !resolve_leaf_entry(
            objects,
            process_one,
            TEST_REUSE_VIRTUAL_ADDRESS,
            &process_one_leaf
        )
    ) {
        goto done;
    }

    original_process_one_pte = *process_one_leaf;
    if (
        micros_sv39_make_leaf_pte(
            process_two_frame,
            user_permissions | MICROS_SV39_PERMISSION_USER,
            &corrupted_pte
        ) != MICROS_SV39_OK
    ) {
        goto done;
    }
    *process_one_leaf = corrupted_pte;
    if (
        !corrupted_tree_is_rejected(
            ledger,
            objects,
            process_one,
            TEST_REUSE_VIRTUAL_ADDRESS,
            MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        )
    ) {
        goto done;
    }
    *process_one_leaf = original_process_one_pte;

    if (
        micros_sv39_make_leaf_pte(
            process_one_root,
            user_permissions | MICROS_SV39_PERMISSION_USER,
            &corrupted_pte
        ) != MICROS_SV39_OK
    ) {
        goto done;
    }
    *process_one_leaf = corrupted_pte;
    if (
        !corrupted_tree_is_rejected(
            ledger,
            objects,
            process_one,
            TEST_REUSE_VIRTUAL_ADDRESS,
            MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        )
    ) {
        goto done;
    }
    *process_one_leaf = original_process_one_pte;

    *process_one_leaf =
        (UINT64_C(1) << 63) | UINT64_C(1);
    if (
        !corrupted_tree_is_rejected(
            ledger,
            objects,
            process_one,
            TEST_REUSE_VIRTUAL_ADDRESS,
            MICROS_USER_ADDRESS_SPACE_ERROR_PTE
        )
    ) {
        goto done;
    }
    *process_one_leaf = original_process_one_pte;

    if (
        !resolve_leaf_entry(
            objects,
            process_one,
            TEST_ALIAS_VIRTUAL_ADDRESS,
            &process_one_leaf
        )
    ) {
        goto done;
    }
    original_alias_pte = *process_one_leaf;
    *process_one_leaf = original_process_one_pte;
    if (
        !corrupted_tree_is_rejected(
            ledger,
            objects,
            process_one,
            TEST_REUSE_VIRTUAL_ADDRESS,
            MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        )
    ) {
        goto done;
    }
    *process_one_leaf = original_alias_pte;

    if (
        !resolve_leaf_entry(
            objects,
            process_one,
            TEST_REUSE_VIRTUAL_ADDRESS,
            &process_one_leaf
        )
    ) {
        goto done;
    }
    *process_one_leaf = 0;
    if (
        !corrupted_tree_is_rejected(
            ledger,
            objects,
            process_one,
            TEST_REUSE_VIRTUAL_ADDRESS,
            MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
        )
    ) {
        goto done;
    }
    *process_one_leaf = original_process_one_pte;

    if (
        micros_thread_create(objects, process_one, &thread)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        goto done;
    }
    if (
        !snapshot_runtime_state(
            ledger,
            objects,
            process_one,
            TEST_REUSE_VIRTUAL_ADDRESS
        )
        || micros_user_address_space_destroy(process_one)
            != MICROS_USER_ADDRESS_SPACE_ERROR_STATE
        || !runtime_state_matches_snapshot(ledger, objects)
    ) {
        goto done;
    }
    if (
        micros_thread_release(objects, thread)
            != MICROS_KERNEL_OBJECT_OK
        || micros_user_address_space_destroy(process_one)
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        goto done;
    }
    fill_page(
        process_one_root,
        UINT64_C(0xa5a5a5a5a5a5a5a5)
    );
    if (
        micros_frame_ownership_runtime_release_process(
            objects,
            process_one
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(objects, &replacement_process)
            != MICROS_KERNEL_OBJECT_OK
        || replacement_process.slot != process_one.slot
        || replacement_process.generation
            != process_one.generation + 1
        || micros_user_address_space_create(replacement_process)
            != MICROS_USER_ADDRESS_SPACE_OK
        || !root_matches_kernel(
            objects,
            replacement_process,
            &replacement_root
        )
        || replacement_root != process_one_root
    ) {
        goto done;
    }

    if (
        !stale_operations_are_rejected(
            ledger,
            objects,
            process_one,
            replacement_process,
            user_permissions
        )
    ) {
        goto done;
    }

    if (
        micros_user_address_space_release_page(
            process_two,
            TEST_USER_VIRTUAL_ADDRESS,
            &released_frame
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released_frame != process_two_frame
        || micros_user_address_space_destroy(process_two)
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_frame_ownership_runtime_release_process(
            objects,
            process_two
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_user_address_space_destroy(replacement_process)
            != MICROS_USER_ADDRESS_SPACE_OK
        || ledger->owned_frame_count != baseline_owned
        || ledger->allocator->free_frame_count != baseline_free
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_complete_handoff(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto done;
    }

    if (
        !post_handoff_operations_are_revoked(
            ledger,
            objects,
            replacement_process,
            user_permissions
        )
        || micros_frame_ownership_runtime_release_process(
            objects,
            replacement_process
        ) != MICROS_KERNEL_OBJECT_OK
        || objects->live_process_count != 0
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto done;
    }

    passed = true;

done:
    sum_fault_state = SUM_FAULT_IDLE;
    riscv_irq_restore(saved_status);
    return passed;
}
