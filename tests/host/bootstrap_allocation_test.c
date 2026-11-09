#include "bootstrap_allocation_test.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "micros/frame_ownership.h"

#define REQUIRE(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return false; \
        } \
    } while (false)

#define TEST_BASE UINT64_C(0x80400000)
#define TEST_KERNEL_START UINT64_C(0x80200000)
#define TEST_PRODUCER UINT64_C(0x4d454d3031000000)

static struct micros_fdt_memory_map memory_map;
static struct micros_physical_range input_memory[16];
static struct micros_physical_range input_reserved[80];
static struct micros_frame_allocator allocator_before;
static struct micros_frame_ownership ownership;
static struct micros_frame_ownership ownership_before;
static uint64_t selected_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
#if !defined(MICROS_BOOTSTRAP_ALLOCATION_BASELINE)
static struct micros_frame_selection selected_frames;
#endif
static uint64_t case_index;

#if defined(MICROS_TEST_BOOTSTRAP_MEMORY_COUNTS)
static struct micros_test_work_snapshot observation;
#endif

static struct micros_frame_allocator *allocator(void)
{
    return micros_bootstrap_allocation_native_allocator();
}

static bool search_state_is_valid(void)
{
#if !defined(MICROS_BOOTSTRAP_ALLOCATION_BASELINE)
    uint64_t index;

    REQUIRE(allocator()->allocation_cursor <= allocator()->managed_frame_count);
    for (index = 0; index < allocator()->allocation_cursor; ++index) {
        REQUIRE(
            (allocator()->allocated_bitmap[index / 64]
             & (UINT64_C(1) << (index % 64))) != 0
        );
    }
#endif
    return true;
}

static bool initialize_dense(uint64_t frames)
{
    memset(&memory_map, 0, sizeof(memory_map));
    memset(allocator(), 0, sizeof(*allocator()));
    memset(&ownership, 0, sizeof(ownership));
    memory_map.memory_range_count = 1;
    memory_map.memory_ranges[0].base = UINT64_C(0x80000000);
    memory_map.memory_ranges[0].size = UINT64_C(0x400000)
        + frames * MICROS_FRAME_SIZE;
    input_memory[0] = (struct micros_physical_range){
        memory_map.memory_ranges[0].base,
        memory_map.memory_ranges[0].size,
    };
    input_reserved[0] = (struct micros_physical_range){0, TEST_BASE};
    micros_bootstrap_allocation_native_bounds(TEST_KERNEL_START, TEST_BASE);
    REQUIRE(
        micros_frame_allocator_initialize(
            allocator(), input_memory, 1, input_reserved, 1
        ) == MICROS_FRAME_ALLOCATOR_OK
    );
    REQUIRE(allocator()->managed_frame_count == frames);
    REQUIRE(allocator()->free_frame_count == frames);
    REQUIRE(search_state_is_valid());
    return true;
}

static bool initialize_split(bool fdt)
{
    size_t index;
    size_t hole;
    size_t holes_per_range = fdt ? 4 : 5;
    size_t reservation_count = holes_per_range * 16;
    uint64_t stride = fdt ? 10 : 12;
    uint64_t pages = fdt ? 9 : 11;

    memset(&memory_map, 0, sizeof(memory_map));
    memset(allocator(), 0, sizeof(*allocator()));
    memset(&ownership, 0, sizeof(ownership));
    memory_map.memory_range_count = 16;
    for (index = 0; index < 16; ++index) {
        uint64_t base = TEST_BASE + index * stride * MICROS_FRAME_SIZE;
        size_t reverse = 15 - index;

        input_memory[reverse] = (struct micros_physical_range){
            base, pages * MICROS_FRAME_SIZE,
        };
        memory_map.memory_ranges[reverse].base = base;
        memory_map.memory_ranges[reverse].size = pages * MICROS_FRAME_SIZE;
        for (hole = 0; hole < holes_per_range; ++hole) {
            size_t ordinal = index * holes_per_range + hole;
            uint64_t address = base + (2 * hole + 1) * MICROS_FRAME_SIZE;

            input_reserved[reservation_count - ordinal - 1] =
                (struct micros_physical_range){address, MICROS_FRAME_SIZE};
            if (fdt) {
                if (ordinal < 32) {
                    memory_map.reservation_ranges[31 - ordinal].base = address;
                    memory_map.reservation_ranges[31 - ordinal].size =
                        MICROS_FRAME_SIZE;
                } else {
                    memory_map.reserved_memory_ranges[63 - ordinal].base =
                        address;
                    memory_map.reserved_memory_ranges[63 - ordinal].size =
                        MICROS_FRAME_SIZE;
                }
            }
        }
    }
    if (fdt) {
        memory_map.reservation_range_count = 32;
        memory_map.reserved_memory_range_count = 32;
        input_reserved[reservation_count++] =
            (struct micros_physical_range){0, TEST_BASE};
    }
    micros_bootstrap_allocation_native_bounds(TEST_KERNEL_START, TEST_BASE);
    REQUIRE(
        micros_frame_allocator_initialize(
            allocator(), input_memory, 16, input_reserved, reservation_count
        ) == MICROS_FRAME_ALLOCATOR_OK
    );
    REQUIRE(allocator()->managed_frame_count == (fdt ? 80U : 96U));
    REQUIRE(allocator()->managed_range_count == (fdt ? 80U : 96U));
    REQUIRE(search_state_is_valid());
    return true;
}

static bool initialize_ownership(void)
{
    REQUIRE(
        micros_frame_ownership_initialize(&ownership, allocator())
            == MICROS_FRAME_OWNERSHIP_OK
    );
    return true;
}

static bool begin_capture(void)
{
    ++case_index;
#if defined(MICROS_TEST_BOOTSTRAP_MEMORY_COUNTS)
    REQUIRE(
        micros_test_work_begin(
            TEST_PRODUCER,
            case_index,
            UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_MEMORY
        ) == MICROS_TEST_WORK_OK
    );
#endif
    return true;
}

static bool finish_capture(void)
{
#if defined(MICROS_TEST_BOOTSTRAP_MEMORY_COUNTS)
    REQUIRE(micros_test_work_finish(&observation) == MICROS_TEST_WORK_OK);
#endif
    return true;
}

static void emit_capture(void)
{
#if defined(MICROS_TEST_BOOTSTRAP_MEMORY_COUNTS)
    size_t owner;
    size_t kind;

    printf(
        "{\"status\":0,\"snapshot\":{\"version\":%" PRIu32
        ",\"reserved0\":%" PRIu32 ",\"producer_label\":%" PRIu64
        ",\"capture_label\":%" PRIu64 ",\"owner_mask\":%" PRIu32
        ",\"reserved1\":%" PRIu32 ",\"counts\":[",
        observation.version, observation.reserved0,
        observation.producer_label, observation.capture_label,
        observation.owner_mask, observation.reserved1
    );
    for (owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        printf("%s[", owner == 0 ? "" : ",");
        for (kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            printf("%s%" PRIu64, kind == 0 ? "" : ",", observation.counts[owner][kind]);
        }
        printf("]");
    }
    printf("]}}");
#else
    printf("null");
#endif
}

static void emit_case(
    const char *operation,
    uint64_t frames,
    uint64_t allocations,
    uint64_t memory_ranges,
    uint64_t reservations,
    uint64_t managed_ranges
)
{
    printf(
        "{\"operation\":\"%s\",\"frames\":%" PRIu64
        ",\"allocations\":%" PRIu64 ",\"memory_ranges\":%" PRIu64
        ",\"reservations\":%" PRIu64 ",\"managed_ranges\":%" PRIu64
        ",\"semantic\":true,\"capture\":",
        operation, frames, allocations, memory_ranges, reservations,
        managed_ranges
    );
    emit_capture();
    printf("}\n");
}

static bool dense_case(uint64_t frames, unsigned operation)
{
    uint64_t count = operation < 2
        ? (frames < 33 ? frames : 33)
        : (frames < 512 ? frames : 512);
    uint64_t index;
    uint64_t address = UINT64_MAX;
    bool valid = false;
    struct micros_frame_owner owner = {
        0, 0, MICROS_FRAME_OWNER_KERNEL_TEMPORARY, 0,
    };

    REQUIRE(initialize_dense(frames));
    if (operation == 3) {
        REQUIRE(initialize_ownership());
    }
    if (operation == 1) {
        for (index = 0; index < count; ++index) {
            REQUIRE(micros_frame_allocator_allocate(allocator(), &address) == MICROS_FRAME_ALLOCATOR_OK);
            REQUIRE(address == TEST_BASE + index * MICROS_FRAME_SIZE);
        }
    }
    REQUIRE(begin_capture());
    if (operation < 2) {
        valid = micros_bootstrap_allocation_native_validate(&memory_map, operation == 0);
    } else {
        for (index = 0; index < count; ++index) {
            if (operation == 2) {
                REQUIRE(micros_frame_allocator_allocate(allocator(), &address) == MICROS_FRAME_ALLOCATOR_OK);
            } else {
                REQUIRE(micros_frame_ownership_allocate(&ownership, owner, &address) == MICROS_FRAME_OWNERSHIP_OK);
            }
            REQUIRE(address == TEST_BASE + index * MICROS_FRAME_SIZE);
        }
    }
    REQUIRE(finish_capture());
    REQUIRE(operation >= 2 || valid);
    REQUIRE(allocator()->free_frame_count == frames - (operation == 0 ? 0 : count));
    REQUIRE(search_state_is_valid());
    if (operation == 3) {
        REQUIRE(micros_frame_ownership_validate(&ownership) == MICROS_FRAME_OWNERSHIP_OK);
        REQUIRE(ownership.owned_frame_count == count);
    }
    emit_case(
        operation == 0 ? "bootstrap-free"
        : operation == 1 ? "bootstrap-live"
        : operation == 2 ? "allocate" : "owned-allocate",
        frames, operation == 0 ? 0 : count, 1, 0, 1
    );
    return true;
}

static bool split_case(bool fdt, bool live)
{
    uint64_t index;
    uint64_t address;
    uint64_t frames = fdt ? 80 : 96;
    uint64_t count = fdt ? (live ? 33 : 0) : 96;
    bool valid = false;

    REQUIRE(initialize_split(fdt));
    if (fdt && live) {
        for (index = 0; index < count; ++index) {
            REQUIRE(micros_frame_allocator_allocate(allocator(), &address) == MICROS_FRAME_ALLOCATOR_OK);
            REQUIRE(address == TEST_BASE + (index / 5 * 10 + index % 5 * 2) * MICROS_FRAME_SIZE);
        }
    }
    REQUIRE(begin_capture());
    if (fdt) {
        valid = micros_bootstrap_allocation_native_validate(&memory_map, !live);
    } else {
        for (index = 0; index < frames; ++index) {
            REQUIRE(micros_frame_allocator_allocate(allocator(), &address) == MICROS_FRAME_ALLOCATOR_OK);
            REQUIRE(address == TEST_BASE + (index / 6 * 12 + index % 6 * 2) * MICROS_FRAME_SIZE);
        }
    }
    REQUIRE(finish_capture());
    REQUIRE(!fdt || valid);
    REQUIRE(allocator()->free_frame_count == frames - count);
    REQUIRE(search_state_is_valid());
    emit_case(
        fdt ? (live ? "bootstrap-split-live" : "bootstrap-split-free") : "allocate-split",
        frames, count, 16, fdt ? 64 : 80, frames
    );
    return true;
}

static bool release_guards(void)
{
    static const uint64_t released[] = {255, 63, 511, 0, 127, 64};
    static const uint64_t expected[] = {0, 63, 64, 127, 255, 511};
    uint64_t index;
    uint64_t address;

    REQUIRE(initialize_dense(4096));
    for (index = 0; index < 512; ++index) {
        REQUIRE(micros_frame_allocator_allocate(allocator(), &address) == MICROS_FRAME_ALLOCATOR_OK);
    }
    for (index = 0; index < 6; ++index) {
        REQUIRE(micros_frame_allocator_release(
            allocator(), TEST_BASE + released[index] * MICROS_FRAME_SIZE
        ) == MICROS_FRAME_ALLOCATOR_OK);
        REQUIRE(search_state_is_valid());
    }
    for (index = 0; index < 6; ++index) {
        REQUIRE(micros_frame_allocator_allocate(allocator(), &address) == MICROS_FRAME_ALLOCATOR_OK);
        REQUIRE(address == TEST_BASE + expected[index] * MICROS_FRAME_SIZE);
    }
    allocator_before = *allocator();
    REQUIRE(micros_frame_allocator_release(allocator(), TEST_BASE + 1) == MICROS_FRAME_ALLOCATOR_ERROR_UNALIGNED);
    REQUIRE(memcmp(&allocator_before, allocator(), sizeof(allocator_before)) == 0);
    REQUIRE(micros_frame_allocator_release(allocator(), TEST_BASE - MICROS_FRAME_SIZE) == MICROS_FRAME_ALLOCATOR_ERROR_UNMANAGED);
    REQUIRE(memcmp(&allocator_before, allocator(), sizeof(allocator_before)) == 0);
    REQUIRE(micros_frame_allocator_release(allocator(), TEST_BASE) == MICROS_FRAME_ALLOCATOR_OK);
    allocator_before = *allocator();
    REQUIRE(micros_frame_allocator_release(allocator(), TEST_BASE) == MICROS_FRAME_ALLOCATOR_ERROR_NOT_ALLOCATED);
    REQUIRE(memcmp(&allocator_before, allocator(), sizeof(allocator_before)) == 0);
    return true;
}

static bool initialization_and_exhaustion_guards(void)
{
    uint64_t address = UINT64_MAX;
    uint64_t index;

    REQUIRE(initialize_dense(65));
    allocator_before = *allocator();
    REQUIRE(micros_frame_allocator_initialize(
        allocator(), input_memory, 17, input_reserved, 1
    ) == MICROS_FRAME_ALLOCATOR_ERROR_CAPACITY);
    REQUIRE(memcmp(&allocator_before, allocator(), sizeof(allocator_before)) == 0);
    input_memory[0] = (struct micros_physical_range){UINT64_MAX - 1, 4};
    REQUIRE(micros_frame_allocator_initialize(
        allocator(), input_memory, 1, NULL, 0
    ) == MICROS_FRAME_ALLOCATOR_ERROR_RANGE);
    REQUIRE(memcmp(&allocator_before, allocator(), sizeof(allocator_before)) == 0);
    for (index = 0; index < 65; ++index) {
        REQUIRE(micros_frame_allocator_allocate(allocator(), &address) == MICROS_FRAME_ALLOCATOR_OK);
    }
    allocator_before = *allocator();
    address = UINT64_MAX;
    REQUIRE(micros_frame_allocator_allocate(allocator(), &address) == MICROS_FRAME_ALLOCATOR_ERROR_EXHAUSTED);
    REQUIRE(address == UINT64_MAX);
    REQUIRE(memcmp(&allocator_before, allocator(), sizeof(allocator_before)) == 0);
    return true;
}

static bool bootstrap_validity_guards(void)
{
    uint64_t address;

    REQUIRE(initialize_dense(65));
    REQUIRE(micros_bootstrap_allocation_native_validate(&memory_map, true));
    REQUIRE(micros_frame_allocator_allocate(allocator(), &address) == MICROS_FRAME_ALLOCATOR_OK);
    REQUIRE(micros_bootstrap_allocation_native_frame_allowed(&memory_map, address));
    REQUIRE(!micros_bootstrap_allocation_native_frame_allowed(&memory_map, TEST_BASE - MICROS_FRAME_SIZE));
    allocator_before = *allocator();
    REQUIRE(!micros_bootstrap_allocation_native_validate(&memory_map, true));
    REQUIRE(micros_bootstrap_allocation_native_validate(&memory_map, false));
    memory_map.reservation_range_count = 1;
    memory_map.reservation_ranges[0].base = TEST_BASE;
    memory_map.reservation_ranges[0].size = MICROS_FRAME_SIZE;
    REQUIRE(!micros_bootstrap_allocation_native_validate(&memory_map, false));
    memory_map.reservation_range_count = 0;
    memory_map.reserved_memory_range_count = 1;
    memory_map.reserved_memory_ranges[0].base = TEST_BASE + MICROS_FRAME_SIZE;
    memory_map.reserved_memory_ranges[0].size = MICROS_FRAME_SIZE;
    REQUIRE(!micros_bootstrap_allocation_native_validate(&memory_map, false));
    memory_map.reserved_memory_range_count = 0;
    memory_map.memory_ranges[0].size = UINT64_MAX;
    REQUIRE(!micros_bootstrap_allocation_native_validate(&memory_map, false));
    REQUIRE(memcmp(&allocator_before, allocator(), sizeof(allocator_before)) == 0);

    REQUIRE(initialize_dense(65));
    REQUIRE(initialize_ownership());
    allocator()->allocated_bitmap[1] = UINT64_C(1) << 1;
    REQUIRE(micros_frame_ownership_validate(&ownership) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
#if defined(MICROS_BOOTSTRAP_ALLOCATION_BASELINE)
    REQUIRE(micros_bootstrap_allocation_native_validate(&memory_map, true));
#else
    REQUIRE(!micros_bootstrap_allocation_native_validate(&memory_map, true));
#endif
    allocator()->allocated_bitmap[1] = 0;
    allocator()->allocated_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS - 1] = UINT64_C(1);
    REQUIRE(micros_frame_ownership_validate(&ownership) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
#if defined(MICROS_BOOTSTRAP_ALLOCATION_BASELINE)
    REQUIRE(micros_bootstrap_allocation_native_validate(&memory_map, true));
#else
    REQUIRE(!micros_bootstrap_allocation_native_validate(&memory_map, true));
#endif
    return true;
}

static bool process_set_guard(void)
{
    static const uint64_t expected[] = {0, 64, 127};
    struct micros_process_handle process = {0, 1};
    struct micros_frame_owner owner = {1, 0, MICROS_FRAME_OWNER_PROCESS_USER, 0};
    uint64_t index;
    uint64_t address;

    REQUIRE(initialize_dense(257));
    REQUIRE(initialize_ownership());
    for (index = 0; index < 128; ++index) {
        REQUIRE(micros_frame_ownership_allocate(&ownership, owner, &address) == MICROS_FRAME_OWNERSHIP_OK);
    }
    memset(selected_bitmap, 0, sizeof(selected_bitmap));
#if !defined(MICROS_BOOTSTRAP_ALLOCATION_BASELINE)
    memset(&selected_frames, 0, sizeof(selected_frames));
#endif
    for (index = 0; index < 3; ++index) {
        selected_bitmap[expected[index] / 64] |= UINT64_C(1) << (expected[index] % 64);
#if !defined(MICROS_BOOTSTRAP_ALLOCATION_BASELINE)
        REQUIRE(micros_frame_selection_add(&selected_frames, expected[index]));
#endif
    }
#if defined(MICROS_BOOTSTRAP_ALLOCATION_BASELINE)
    REQUIRE(micros_frame_ownership_release_process_set(
        &ownership, process, selected_bitmap, MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
    ) == MICROS_FRAME_OWNERSHIP_OK);
#else
    REQUIRE(micros_frame_ownership_release_process_set(
        &ownership, process, &selected_frames
    ) == MICROS_FRAME_OWNERSHIP_OK);
#endif
    REQUIRE(search_state_is_valid());
    for (index = 0; index < 3; ++index) {
        REQUIRE(micros_frame_ownership_allocate(&ownership, owner, &address) == MICROS_FRAME_OWNERSHIP_OK);
        REQUIRE(address == TEST_BASE + expected[index] * MICROS_FRAME_SIZE);
    }
    REQUIRE(micros_frame_ownership_validate(&ownership) == MICROS_FRAME_OWNERSHIP_OK);
    return true;
}

static bool rollback_guard(void)
{
    struct micros_frame_owner owner = {0, 0, MICROS_FRAME_OWNER_KERNEL_TEMPORARY, 0};
    uint64_t index;
    uint64_t address = UINT64_MAX;

    REQUIRE(initialize_dense(257));
    REQUIRE(initialize_ownership());
    for (index = 0; index < 65; ++index) {
        REQUIRE(micros_frame_ownership_allocate(&ownership, owner, &address) == MICROS_FRAME_OWNERSHIP_OK);
    }
    ownership.owners[65] = owner;
#if !defined(MICROS_BOOTSTRAP_ALLOCATION_BASELINE)
    allocator()->allocation_cursor = 64;
#endif
    allocator_before = *allocator();
    ownership_before = ownership;
    address = UINT64_MAX;
    REQUIRE(micros_frame_ownership_allocate(&ownership, owner, &address) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(address == UINT64_MAX);
    REQUIRE(memcmp(&allocator_before, allocator(), sizeof(allocator_before)) == 0);
    REQUIRE(memcmp(&ownership_before, &ownership, sizeof(ownership)) == 0);
    ownership.owners[65] = (struct micros_frame_owner){0, 0, MICROS_FRAME_OWNER_FREE, 0};
    REQUIRE(micros_frame_ownership_allocate(&ownership, owner, &address) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(address == TEST_BASE + 65 * MICROS_FRAME_SIZE);
    REQUIRE(micros_frame_ownership_validate(&ownership) == MICROS_FRAME_OWNERSHIP_OK);
    return true;
}

static bool vm_fixture(void)
{
    struct micros_frame_owner owner = {0, 0, MICROS_FRAME_OWNER_KERNEL_RETAINED, 0};
    uint64_t index;
    uint64_t address;

    REQUIRE(initialize_dense(257));
    REQUIRE(initialize_ownership());
    for (index = 0; index < 16; ++index) {
        REQUIRE(micros_frame_ownership_allocate(&ownership, owner, &address) == MICROS_FRAME_OWNERSHIP_OK);
        REQUIRE(micros_frame_ownership_prepare_handoff(
            &ownership, address, owner, MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        ) == MICROS_FRAME_OWNERSHIP_OK);
    }
    REQUIRE(micros_frame_ownership_complete_handoff(&ownership) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(search_state_is_valid());
    return true;
}

static bool vm_guards(void)
{
    struct micros_frame_vm_transferable_release_plan release;
    struct micros_frame_vm_claim_plan claim;
    struct micros_frame_vm_freeze_plan freeze;
    struct micros_frame_vm_process_release_plan process_release;
    struct micros_process_handle process = {0, 1};
    uint64_t data_frames[2] = {TEST_BASE, TEST_BASE + MICROS_FRAME_SIZE};
    uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
    size_t index;

    REQUIRE(vm_fixture());
    REQUIRE(micros_frame_ownership_vm_release_transferable_preflight(
        &ownership, data_frames, 1, &release
    ) == MICROS_FRAME_OWNERSHIP_OK);
    micros_frame_ownership_vm_release_transferable_commit_prevalidated(&ownership, &release);
    REQUIRE(search_state_is_valid());
#if !defined(MICROS_BOOTSTRAP_ALLOCATION_BASELINE)
    REQUIRE(allocator()->allocation_cursor == 0);
#endif
    REQUIRE(micros_frame_ownership_vm_claim_preflight(&ownership, TEST_BASE, &claim) == MICROS_FRAME_OWNERSHIP_OK);
    micros_frame_ownership_vm_claim_commit_prevalidated(&ownership, &claim);
    REQUIRE(micros_frame_ownership_validate(&ownership) == MICROS_FRAME_OWNERSHIP_OK);

    REQUIRE(vm_fixture());
    for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        table_frames[index] = TEST_BASE + (16 + index) * MICROS_FRAME_SIZE;
    }
    REQUIRE(micros_frame_ownership_vm_freeze_preflight(
        &ownership, process, data_frames, 2, table_frames, &freeze
    ) == MICROS_FRAME_OWNERSHIP_OK);
    micros_frame_ownership_vm_freeze_commit_prevalidated(&ownership, &freeze);
    REQUIRE(search_state_is_valid());
    REQUIRE(micros_frame_ownership_vm_release_process_preflight(
        &ownership, process, data_frames, 2, table_frames, &process_release
    ) == MICROS_FRAME_OWNERSHIP_OK);
    micros_frame_ownership_vm_release_process_commit_prevalidated(&ownership, &process_release);
    REQUIRE(search_state_is_valid());
#if !defined(MICROS_BOOTSTRAP_ALLOCATION_BASELINE)
    REQUIRE(allocator()->allocation_cursor == 0);
#endif
    REQUIRE(micros_frame_ownership_validate(&ownership) == MICROS_FRAME_OWNERSHIP_OK);
    return true;
}

static bool cursor_guards(void)
{
#if !defined(MICROS_BOOTSTRAP_ALLOCATION_BASELINE)
    uint64_t address = UINT64_MAX;
    struct micros_frame_owner owner = {0, 0, MICROS_FRAME_OWNER_KERNEL_TEMPORARY, 0};

    REQUIRE(initialize_dense(65));
    REQUIRE(initialize_ownership());
    allocator()->allocation_cursor = 66;
    allocator_before = *allocator();
    REQUIRE(micros_frame_allocator_allocate(allocator(), &address) == MICROS_FRAME_ALLOCATOR_ERROR_INVARIANT);
    REQUIRE(address == UINT64_MAX);
    REQUIRE(memcmp(&allocator_before, allocator(), sizeof(allocator_before)) == 0);
    REQUIRE(!micros_bootstrap_allocation_native_validate(&memory_map, true));
    REQUIRE(micros_frame_ownership_validate(&ownership) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    allocator()->allocation_cursor = 0;
    REQUIRE(micros_frame_ownership_allocate(&ownership, owner, &address) == MICROS_FRAME_OWNERSHIP_OK);
    allocator()->allocated_bitmap[0] &= ~UINT64_C(1);
    ++allocator()->free_frame_count;
    ownership.owners[0] = (struct micros_frame_owner){0, 0, MICROS_FRAME_OWNER_FREE, 0};
    --ownership.owned_frame_count;
    --ownership.owner_counts[MICROS_FRAME_OWNER_KERNEL_TEMPORARY];
    allocator_before = *allocator();
    ownership_before = ownership;
    REQUIRE(!micros_bootstrap_allocation_native_validate(&memory_map, false));
    REQUIRE(micros_frame_ownership_validate(&ownership) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(memcmp(&allocator_before, allocator(), sizeof(allocator_before)) == 0);
    REQUIRE(memcmp(&ownership_before, &ownership, sizeof(ownership)) == 0);
#endif
    return true;
}

static bool run(void)
{
    static const uint64_t sizes[] = {1, 63, 64, 65, 257, 4096, 262143, 262144};
    size_t index;
    unsigned operation;

    REQUIRE(release_guards());
    REQUIRE(initialization_and_exhaustion_guards());
    REQUIRE(bootstrap_validity_guards());
    REQUIRE(process_set_guard());
    REQUIRE(rollback_guard());
    REQUIRE(vm_guards());
    REQUIRE(cursor_guards());
    printf(
        "{\"operation\":\"guards\",\"semantic\":true,\"cursor_layout\":\"%s\"}\n",
#if defined(MICROS_BOOTSTRAP_ALLOCATION_BASELINE)
        "absent"
#else
        "current"
#endif
    );
    for (index = 0; index < sizeof(sizes) / sizeof(sizes[0]); ++index) {
        for (operation = 0; operation < 4; ++operation) {
            REQUIRE(dense_case(sizes[index], operation));
        }
    }
    REQUIRE(split_case(true, false));
    REQUIRE(split_case(true, true));
    REQUIRE(split_case(false, false));
    return true;
}

int main(void)
{
    return run() ? 0 : 1;
}
