#include "kernel_mapping_test.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return false; \
        } \
    } while (false)

#define PAGE UINT64_C(4096)
#define BASE UINT64_C(0xc0000000)
#define PRODUCER UINT64_C(0x4d454d3032000000)

enum {
    DOMAIN_CAPACITY = MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES + 7,
};

struct mapping_domain {
    uint64_t start;
    uint64_t end;
    uint32_t permissions;
};

struct mapping_topology {
    uint64_t tables;
    uint64_t middle_tables;
    uint64_t leaves[3];
};

static struct micros_physical_range input_memory[16];
static struct micros_physical_range input_reserved[80];
static struct micros_managed_frame_range model_ranges[96];
static struct mapping_domain domains[DOMAIN_CAPACITY];
static struct micros_kernel_page_table backing[1024];
static struct micros_kernel_page_table backing_before[1024];
static struct micros_frame_allocator allocator_before;
static struct micros_frame_ownership ownership_before;
static struct micros_kernel_address_space_private_state state_before;
static bool expected_middle[512];
static bool expected_lower[512][512];
static bool observed_tables[1024];
static size_t memory_count;
static size_t reserved_count;
static size_t model_range_count;
static size_t domain_count;
static uint64_t model_frames;
static uint64_t bounds[4];
static uint64_t capture_label;
static struct mapping_topology expected;
static struct mapping_topology observed;

#if defined(MICROS_TEST_KERNEL_MAPPING_COUNTS)
static struct micros_test_work_snapshot snapshot;
#endif

static struct micros_frame_allocator *allocator(void)
{
    return micros_kernel_mapping_native_allocator();
}

static struct micros_frame_ownership *ownership(void)
{
    return micros_kernel_mapping_native_ownership();
}

static struct micros_kernel_address_space_private_state *state(void)
{
    return micros_kernel_mapping_native_state();
}

static uint64_t model_index(uint64_t address)
{
    size_t index;

    if (address % PAGE != 0) {
        return UINT64_MAX;
    }
    for (index = 0; index < model_range_count; ++index) {
        const struct micros_managed_frame_range *range = &model_ranges[index];

        if (address >= range->base
            && address - range->base < range->frame_count * PAGE) {
            return range->bitmap_offset + (address - range->base) / PAGE;
        }
    }
    return UINT64_MAX;
}

struct micros_kernel_page_table *micros_kernel_mapping_native_table_at(
    uint64_t physical_address
)
{
    uint64_t index = model_index(physical_address);

    if (index >= 1024 || index >= model_frames
        || ownership()->owners[index].kind != MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
        || (allocator()->allocated_bitmap[index / 64]
            & (UINT64_C(1) << (index % 64))) == 0) {
        fprintf(stderr, "MEM02 unsafe native table access: 0x%016" PRIx64 "\n", physical_address);
        abort();
    }
    return &backing[index];
}

static void reset_inputs(void)
{
    memset(input_memory, 0, sizeof(input_memory));
    memset(input_reserved, 0, sizeof(input_reserved));
    memset(model_ranges, 0, sizeof(model_ranges));
    memory_count = 0;
    reserved_count = 0;
    model_range_count = 0;
    model_frames = 0;
    bounds[0] = UINT64_C(0x80200000);
    bounds[1] = UINT64_C(0x80201000);
    bounds[2] = UINT64_C(0x80202000);
    bounds[3] = UINT64_C(0x80203000);
}

static void add_model_range(uint64_t base, uint64_t count)
{
    if (model_range_count >= 96) {
        abort();
    }
    model_ranges[model_range_count++] =
        (struct micros_managed_frame_range){base, count, model_frames};
    model_frames += count;
}

static void configure_dense(uint64_t base, uint64_t count)
{
    reset_inputs();
    input_memory[0] = (struct micros_physical_range){base, count * PAGE};
    memory_count = 1;
    add_model_range(base, count);
}

static void configure_sparse(void)
{
    size_t index;
    size_t offset;

    reset_inputs();
    memory_count = 16;
    reserved_count = 80;
    for (index = 0; index < 16; ++index) {
        uint64_t base = BASE + index * UINT64_C(0x40000000);

        input_memory[15 - index] = (struct micros_physical_range){base, 11 * PAGE};
        for (offset = 0; offset < 11; ++offset) {
            if ((offset & 1U) == 0) {
                add_model_range(base + offset * PAGE, 1);
            } else {
                size_t ordinal = index * 5 + offset / 2;

                input_reserved[79 - ordinal] =
                    (struct micros_physical_range){base + offset * PAGE, PAGE};
            }
        }
    }
}

static bool configure_case(size_t index)
{
    static const uint64_t dense[] = {
        9, 63, 64, 65, 511, 512, 513, 4096, 262143, 262144,
    };

    if (index < sizeof(dense) / sizeof(dense[0])) {
        configure_dense(BASE, dense[index]);
    } else if (index == 10) {
        configure_dense(UINT64_C(0xc01ff000), 65);
    } else if (index == 11 || index == 12) {
        configure_dense(UINT64_C(0xbffff000), index == 11 ? 513 : 514);
    } else if (index == 13) {
        configure_dense(BASE, 4096);
        bounds[3] = UINT64_C(0x80400000);
    } else if (index == 14) {
        configure_dense(UINT64_C(0x80401000), 4096);
        bounds[3] = UINT64_C(0x80401000);
    } else if (index == 15) {
        configure_dense(BASE, 16);
        input_memory[1] = (struct micros_physical_range){UINT64_C(0x3ffffff000), PAGE};
        memory_count = 2;
        add_model_range(UINT64_C(0x3ffffff000), 1);
    } else if (index == 16) {
        configure_sparse();
    } else {
        return false;
    }
    return true;
}

static bool initialize_case(void)
{
    size_t index;

    micros_kernel_mapping_native_ready(false);
    micros_kernel_mapping_native_irq(false);
    memset(allocator(), 0, sizeof(*allocator()));
    memset(ownership(), 0, sizeof(*ownership()));
    memset(state(), 0, sizeof(*state()));
    memset(backing, 0xa5, sizeof(backing));
    micros_kernel_mapping_native_bounds(bounds);
    REQUIRE(micros_frame_allocator_initialize(
        allocator(), input_memory, memory_count, input_reserved, reserved_count
    ) == MICROS_FRAME_ALLOCATOR_OK);
    REQUIRE(allocator()->managed_frame_count == model_frames);
    REQUIRE(allocator()->managed_range_count == model_range_count);
    for (index = 0; index < model_range_count; ++index) {
        REQUIRE(allocator()->managed_ranges[index].base == model_ranges[index].base);
        REQUIRE(allocator()->managed_ranges[index].frame_count == model_ranges[index].frame_count);
        REQUIRE(allocator()->managed_ranges[index].bitmap_offset == model_ranges[index].bitmap_offset);
    }
    REQUIRE(micros_frame_ownership_initialize(ownership(), allocator()) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(micros_frame_ownership_validate(ownership()) == MICROS_FRAME_OWNERSHIP_OK);
    micros_kernel_mapping_native_ready(true);
    REQUIRE(micros_bootstrap_frame_allocator() == allocator());
    REQUIRE(micros_frame_ownership_runtime_ledger() == ownership());
    REQUIRE(micros_kernel_address_space_report() == NULL);
    return true;
}

static void add_domain(uint64_t start, uint64_t end, uint32_t permissions)
{
    if (domain_count >= DOMAIN_CAPACITY || start >= end) {
        abort();
    }
    domains[domain_count++] = (struct mapping_domain){start, end, permissions};
}

static void prepare_domains(void)
{
    static const uint64_t devices[] = {
        UINT64_C(0x10000000), UINT64_C(0x0c000000),
        UINT64_C(0x0c002000), UINT64_C(0x0c201000),
    };
    size_t index;
    uint32_t rw = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;

    domain_count = 0;
    add_domain(bounds[0], bounds[1], MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE);
    add_domain(bounds[1], bounds[2], MICROS_SV39_PERMISSION_READ);
    add_domain(bounds[2], bounds[3], rw);
    for (index = 0; index < model_range_count; ++index) {
        add_domain(model_ranges[index].base,
                   model_ranges[index].base + model_ranges[index].frame_count * PAGE, rw);
    }
    for (index = 0; index < 4; ++index) {
        add_domain(devices[index], devices[index] + PAGE, rw);
    }
}

static unsigned required_level(const struct mapping_domain *domain, uint64_t address)
{
    uint64_t block = address & ~UINT64_C(0x3fffffff);

    if (block >= domain->start && block + UINT64_C(0x40000000) <= domain->end) {
        return 2;
    }
    block = address & ~UINT64_C(0x1fffff);
    return block >= domain->start && block + UINT64_C(0x200000) <= domain->end ? 1U : 0U;
}

static uint64_t leaf_size(unsigned level)
{
    if (level >= 3) {
        abort();
    }
    return PAGE << (9 * level);
}

static void reference_topology(void)
{
    size_t domain_index;
    size_t root;
    size_t middle;

    memset(&expected, 0, sizeof(expected));
    memset(expected_middle, 0, sizeof(expected_middle));
    memset(expected_lower, 0, sizeof(expected_lower));
    expected.tables = 1;
    for (domain_index = 0; domain_index < domain_count; ++domain_index) {
        const struct mapping_domain *domain = &domains[domain_index];
        uint64_t address;

        for (address = domain->start; address < domain->end; address += PAGE) {
            unsigned level = required_level(domain, address);

            if (address % leaf_size(level) == 0) {
                ++expected.leaves[level];
            }
            if (level < 2) {
                expected_middle[(address >> 30) & 511] = true;
            }
            if (level == 0) {
                expected_lower[(address >> 30) & 511][(address >> 21) & 511] = true;
            }
        }
    }
    for (root = 0; root < 512; ++root) {
        expected.tables += expected_middle[root] ? 1 : 0;
        expected.middle_tables += expected_middle[root] ? 1 : 0;
        for (middle = 0; middle < 512; ++middle) {
            expected.tables += expected_lower[root][middle] ? 1 : 0;
        }
    }
}

static bool leaf_has_domain(uint64_t address, unsigned level, struct micros_sv39_decoded_pte entry)
{
    uint64_t size = leaf_size(level);
    size_t index;

    REQUIRE(entry.physical_address == address && address % size == 0);
    REQUIRE(entry.accessed && entry.dirty == ((entry.permissions & MICROS_SV39_PERMISSION_WRITE) != 0));
    REQUIRE((entry.permissions & MICROS_SV39_PERMISSION_USER) == 0);
    for (index = 0; index < domain_count; ++index) {
        if (address >= domains[index].start && address + size <= domains[index].end
            && entry.permissions == domains[index].permissions) {
            return true;
        }
    }
    return false;
}

static bool visit_table(uint64_t physical)
{
    uint64_t index = model_index(physical);

    REQUIRE(index < 1024 && index < state()->table_count);
    REQUIRE(!observed_tables[index]);
    REQUIRE(state()->table_frames[index] == physical);
    observed_tables[index] = true;
    ++observed.tables;
    return true;
}

static bool inspect_lower(uint64_t physical, uint64_t prefix)
{
    struct micros_kernel_page_table *table;
    size_t index;

    REQUIRE(visit_table(physical));
    table = micros_kernel_mapping_native_table_at(physical);
    for (index = 0; index < 512; ++index) {
        struct micros_sv39_decoded_pte entry;

        REQUIRE(micros_sv39_decode_pte(table->entries[index], &entry) == MICROS_SV39_OK);
        REQUIRE(entry.kind != MICROS_SV39_PTE_TABLE);
        if (entry.kind == MICROS_SV39_PTE_LEAF) {
            REQUIRE(leaf_has_domain(prefix + index * PAGE, 0, entry));
            ++observed.leaves[0];
        }
    }
    return true;
}

static bool inspect_middle(uint64_t physical, uint64_t prefix)
{
    struct micros_kernel_page_table *table;
    size_t index;

    REQUIRE(visit_table(physical));
    ++observed.middle_tables;
    table = micros_kernel_mapping_native_table_at(physical);
    for (index = 0; index < 512; ++index) {
        struct micros_sv39_decoded_pte entry;
        uint64_t address = prefix + index * UINT64_C(0x200000);

        REQUIRE(micros_sv39_decode_pte(table->entries[index], &entry) == MICROS_SV39_OK);
        if (entry.kind == MICROS_SV39_PTE_LEAF) {
            REQUIRE(leaf_has_domain(address, 1, entry));
            ++observed.leaves[1];
        } else if (entry.kind == MICROS_SV39_PTE_TABLE) {
            REQUIRE(inspect_lower(entry.physical_address, address));
        }
    }
    return true;
}

static bool inspect_topology(void)
{
    struct micros_kernel_page_table *root;
    size_t index;

    memset(&observed, 0, sizeof(observed));
    memset(observed_tables, 0, sizeof(observed_tables));
    REQUIRE(visit_table(state()->root_physical_address));
    root = micros_kernel_mapping_native_table_at(state()->root_physical_address);
    for (index = 0; index < 512; ++index) {
        struct micros_sv39_decoded_pte entry;
        uint64_t address = index * UINT64_C(0x40000000);

        REQUIRE(micros_sv39_decode_pte(root->entries[index], &entry) == MICROS_SV39_OK);
        if (entry.kind == MICROS_SV39_PTE_LEAF) {
            REQUIRE(leaf_has_domain(address, 2, entry));
            ++observed.leaves[2];
        } else if (entry.kind == MICROS_SV39_PTE_TABLE) {
            REQUIRE(inspect_middle(entry.physical_address, address));
        }
    }
    REQUIRE(observed.tables == state()->table_count);
    REQUIRE(observed.leaves[0] + observed.leaves[1] + observed.leaves[2] == state()->leaf_count);
    for (index = 0; index < state()->table_count; ++index) {
        REQUIRE(observed_tables[index]);
    }
    return true;
}

static bool lookup_independent(uint64_t address, uint64_t *physical, uint32_t *permissions)
{
    uint64_t table_address = state()->root_physical_address;
    int level;

    for (level = 2; level >= 0; --level) {
        struct micros_kernel_page_table *table = micros_kernel_mapping_native_table_at(table_address);
        struct micros_sv39_decoded_pte entry;
        size_t index = (size_t)((address >> (12 + 9 * level)) & 511);

        REQUIRE(micros_sv39_decode_pte(table->entries[index], &entry) == MICROS_SV39_OK);
        if (entry.kind == MICROS_SV39_PTE_LEAF) {
            uint64_t size = leaf_size((unsigned)level);

            REQUIRE(entry.physical_address % size == 0);
            *physical = entry.physical_address + address % size;
            *permissions = entry.permissions;
            return true;
        }
        if (entry.kind != MICROS_SV39_PTE_TABLE || level == 0) {
            return false;
        }
        table_address = entry.physical_address;
    }
    return false;
}

static bool complete_semantics(void)
{
    size_t index;

    REQUIRE(inspect_topology());
    for (index = 0; index < domain_count; ++index) {
        uint64_t address;

        for (address = domains[index].start; address < domains[index].end; address += PAGE) {
            uint64_t physical;
            uint32_t permissions;

            REQUIRE(lookup_independent(address, &physical, &permissions));
            REQUIRE(physical == address && permissions == domains[index].permissions);
        }
    }
    REQUIRE(ownership()->owned_frame_count == observed.tables);
    REQUIRE(ownership()->owner_counts[MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE] == observed.tables);
    REQUIRE(allocator()->free_frame_count == model_frames - observed.tables);
    REQUIRE(allocator()->allocation_cursor == observed.tables);
    REQUIRE(micros_frame_ownership_validate(ownership()) == MICROS_FRAME_OWNERSHIP_OK);
    for (index = 0; index < observed.tables; ++index) {
        struct micros_frame_owner owner = ownership()->owners[index];

        REQUIRE(owner.kind == MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE);
        REQUIRE(owner.generation == 0 && owner.slot == 0 && owner.reserved == 0);
    }
    return true;
}

static bool begin_capture(void)
{
    ++capture_label;
#if defined(MICROS_TEST_KERNEL_MAPPING_COUNTS)
    REQUIRE(micros_test_work_begin(
        PRODUCER, capture_label, UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_MEMORY
    ) == MICROS_TEST_WORK_OK);
#endif
    return true;
}

static bool finish_capture(void)
{
#if defined(MICROS_TEST_KERNEL_MAPPING_COUNTS)
    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
#endif
    return true;
}

static bool check_capture(const char *operation)
{
#if defined(MICROS_TEST_KERNEL_MAPPING_COUNTS) && !defined(MICROS_KERNEL_MAPPING_BASELINE)
    const uint64_t *counts = snapshot.counts[MICROS_WORK_OWNER_KERNEL_MEMORY];
    uint64_t tables = observed.tables;
    uint64_t leaves = observed.leaves[0] + observed.leaves[1] + observed.leaves[2];
    uint64_t required = expected.leaves[0] + expected.leaves[1] + expected.leaves[2];

    if (strcmp(operation, "pristine") == 0) {
        REQUIRE(counts[MICROS_WORK_KIND_BITMAP_WORD_LOAD] == model_frames);
    } else if (strcmp(operation, "construct") == 0) {
        REQUIRE(counts[MICROS_WORK_KIND_INDEX_PROBE] <= 32 * (leaves + tables + model_range_count + 1));
        REQUIRE(counts[MICROS_WORK_KIND_PTE_VISIT] <= 4 * leaves + 2 * tables);
        REQUIRE(counts[MICROS_WORK_KIND_RANGE_VISIT] <= tables * (192 + 4 * model_range_count) + 8 * leaves + 2 * model_range_count + 32);
        REQUIRE(counts[MICROS_WORK_KIND_RECORD_VISIT] <= tables * (192 + 4 * model_range_count) + 16 * leaves + 16 * tables + 128);
        REQUIRE(counts[MICROS_WORK_KIND_BITMAP_WORD_LOAD] <= 8 * (leaves + tables + 1));
        REQUIRE(counts[MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL] == 0);
        REQUIRE(counts[MICROS_WORK_KIND_CLEAR_UNIT] == 512 * (tables + observed.middle_tables));
        REQUIRE(counts[MICROS_WORK_KIND_CLEARED_BYTE] == 4096 * tables + 1024 * observed.middle_tables);
    } else if (strcmp(operation, "validate") == 0) {
        REQUIRE(counts[MICROS_WORK_KIND_RANGE_VISIT] <= 384 + 4 * tables);
        REQUIRE(counts[MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL] == 1);
    } else if (strcmp(operation, "tree") == 0) {
        REQUIRE(counts[MICROS_WORK_KIND_INDEX_PROBE] <= 16 * (tables + 1));
        REQUIRE(counts[MICROS_WORK_KIND_PTE_VISIT] == 512 * tables);
    } else {
        REQUIRE(strcmp(operation, "coverage") == 0);
        REQUIRE(counts[MICROS_WORK_KIND_PTE_VISIT] <= 3 * (required + tables + 4));
    }
#else
    (void)operation;
#endif
    return true;
}

static void print_capture(void)
{
#if defined(MICROS_TEST_KERNEL_MAPPING_COUNTS)
    size_t owner;
    size_t kind;

    printf("{\"status\":0,\"snapshot\":{\"version\":%" PRIu32
           ",\"reserved0\":%" PRIu32 ",\"producer_label\":%" PRIu64
           ",\"capture_label\":%" PRIu64 ",\"owner_mask\":%" PRIu32
           ",\"reserved1\":%" PRIu32 ",\"counts\":[",
           snapshot.version, snapshot.reserved0, snapshot.producer_label,
           snapshot.capture_label, snapshot.owner_mask, snapshot.reserved1);
    for (owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        printf("%s[", owner ? "," : "");
        for (kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            printf("%s%" PRIu64, kind ? "," : "", snapshot.counts[owner][kind]);
        }
        printf("]");
    }
    printf("]}}");
#else
    printf("null");
#endif
}

static void print_record(size_t case_index, const char *operation)
{
    printf("{\"case\":%zu,\"operation\":\"%s\",\"frames\":%" PRIu64
           ",\"managed_ranges\":%zu,\"tables\":%" PRIu64 ",\"middle_tables\":%" PRIu64
           ",\"leaves\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 "]"
           ",\"expected_tables\":%" PRIu64 ",\"expected_leaves\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 "]"
           ",\"semantic\":true,\"capture\":",
           case_index + 1, operation, model_frames, model_range_count,
           observed.tables, observed.middle_tables, observed.leaves[0], observed.leaves[1], observed.leaves[2],
           expected.tables, expected.leaves[0], expected.leaves[1], expected.leaves[2]);
    print_capture();
    printf("}\n");
}

static bool measured_case(size_t index)
{
    bool valid;
    enum micros_kernel_address_space_error error;

    REQUIRE(configure_case(index) && initialize_case());
    prepare_domains();
    reference_topology();
    memset(&observed, 0, sizeof(observed));
    REQUIRE(begin_capture());
    valid = micros_kernel_address_space_allocator_is_pristine(allocator());
    REQUIRE(finish_capture() && valid);
    REQUIRE(check_capture("pristine"));
    print_record(index, "pristine");
    REQUIRE(begin_capture());
    error = micros_kernel_mapping_native_construct();
    REQUIRE(finish_capture() && error == MICROS_KERNEL_ADDRESS_SPACE_OK);
    REQUIRE(complete_semantics());
    REQUIRE(check_capture("construct"));
    print_record(index, "construct");
    REQUIRE(begin_capture());
    valid = micros_kernel_mapping_native_validate();
    REQUIRE(finish_capture() && valid);
    REQUIRE(check_capture("validate"));
    print_record(index, "validate");
    micros_kernel_mapping_native_prepare_tree();
    REQUIRE(begin_capture());
    valid = micros_kernel_mapping_native_tree();
    REQUIRE(finish_capture() && valid);
    REQUIRE(check_capture("tree"));
    print_record(index, "tree");
    REQUIRE(micros_kernel_mapping_native_validate());
    REQUIRE(begin_capture());
    valid = micros_kernel_mapping_native_coverage();
    REQUIRE(finish_capture() && valid);
    REQUIRE(check_capture("coverage"));
    print_record(index, "coverage");
#if !defined(MICROS_KERNEL_MAPPING_BASELINE)
    REQUIRE(memcmp(&expected, &observed, sizeof(expected)) == 0);
#endif
    return true;
}

static void take_snapshot(void)
{
    memcpy(&allocator_before, allocator(), sizeof(allocator_before));
    memcpy(&ownership_before, ownership(), sizeof(ownership_before));
    memcpy(&state_before, state(), sizeof(state_before));
    memcpy(backing_before, backing, sizeof(backing_before));
}

static bool snapshot_is_unchanged(void)
{
    return memcmp(&allocator_before, allocator(), sizeof(allocator_before)) == 0
        && memcmp(&ownership_before, ownership(), sizeof(ownership_before)) == 0
        && memcmp(&state_before, state(), sizeof(state_before)) == 0
        && memcmp(backing_before, backing, sizeof(backing_before)) == 0;
}

static bool rejected_without_writes(void)
{
    take_snapshot();
    REQUIRE(!micros_kernel_mapping_native_validate());
    REQUIRE(snapshot_is_unchanged());
    return true;
}

static bool failed_allocation_guards(void)
{
    uint64_t frames;

    for (frames = 1; frames <= 8; ++frames) {
        configure_dense(BASE, frames);
        REQUIRE(initialize_case());
        REQUIRE(micros_kernel_mapping_native_construct() == MICROS_KERNEL_ADDRESS_SPACE_ERROR_ALLOCATION);
        REQUIRE(!state()->initialized && micros_kernel_address_space_report() == NULL);
        REQUIRE(allocator()->free_frame_count == 0 && state()->table_count == frames);
        REQUIRE(ownership()->owned_frame_count == frames);
        REQUIRE(micros_frame_ownership_validate(ownership()) == MICROS_FRAME_OWNERSHIP_OK);
    }
    configure_dense(UINT64_C(1) << 56, 16);
    REQUIRE(initialize_case());
    {
        uint64_t physical = UINT64_MAX;

        take_snapshot();
        REQUIRE(micros_kernel_mapping_native_allocate_table(&physical) == MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE);
        REQUIRE(snapshot_is_unchanged());
    }
    return true;
}

static bool scalar_and_owner_guards(void)
{
    uint64_t saved;
    struct micros_frame_owner owner;

    saved = state()->table_count;
    state()->table_count = 1025;
    REQUIRE(rejected_without_writes());
    state()->table_count = saved;
    ++state()->leaf_count;
    REQUIRE(rejected_without_writes());
    --state()->leaf_count;
    saved = state()->root_physical_address;
    state()->root_physical_address = 0;
    REQUIRE(rejected_without_writes());
    state()->root_physical_address = saved;
    owner = ownership()->owners[0];
    ownership()->owners[0].kind = MICROS_FRAME_OWNER_KERNEL_TEMPORARY;
    REQUIRE(rejected_without_writes());
    ownership()->owners[0] = owner;
    allocator()->allocated_bitmap[0] ^= 1;
    REQUIRE(rejected_without_writes());
    allocator()->allocated_bitmap[0] ^= 1;
    return true;
}

static bool pte_guards(void)
{
    struct micros_kernel_page_table *root =
        micros_kernel_mapping_native_table_at(state()->root_physical_address);
    struct micros_sv39_decoded_pte entry;
    struct micros_kernel_page_table *middle;
    struct micros_kernel_page_table *lower;
    uint64_t saved_root = root->entries[2];
    uint64_t saved;
    uint64_t extra;

    REQUIRE(micros_sv39_decode_pte(saved_root, &entry) == MICROS_SV39_OK);
    middle = micros_kernel_mapping_native_table_at(entry.physical_address);
    REQUIRE(micros_sv39_decode_pte(middle->entries[1], &entry) == MICROS_SV39_OK);
    lower = micros_kernel_mapping_native_table_at(entry.physical_address);
    saved = lower->entries[0];
    lower->entries[0] &= ~UINT64_C(8);
    REQUIRE(rejected_without_writes());
    lower->entries[0] = saved;
    root->entries[2] = 0;
    REQUIRE(rejected_without_writes());
    root->entries[2] = saved_root;
    REQUIRE(micros_sv39_make_leaf_pte(
        bounds[3], MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE, &extra
    ) == MICROS_SV39_OK);
    REQUIRE(lower->entries[3] == 0);
    lower->entries[3] = extra;
    REQUIRE(rejected_without_writes());
#if !defined(MICROS_KERNEL_MAPPING_BASELINE)
    ++state()->leaf_count;
    REQUIRE(rejected_without_writes());
    --state()->leaf_count;
#endif
    lower->entries[3] = 0;
    REQUIRE(micros_sv39_make_leaf_pte(bounds[1], MICROS_SV39_PERMISSION_READ, &extra) == MICROS_SV39_OK);
    root->entries[2] = extra;
    REQUIRE(rejected_without_writes());
    root->entries[2] = saved_root;
    saved = root->entries[3];
    root->entries[3] = saved_root;
    REQUIRE(rejected_without_writes());
    root->entries[3] = saved;
    return true;
}

static bool range_and_capacity_guards(void)
{
    uint64_t saved_count = state()->table_count;
    uint64_t output = UINT64_MAX;
    uint32_t permissions = UINT32_MAX;

    take_snapshot();
    REQUIRE(micros_kernel_mapping_native_map_range(BASE, BASE, MICROS_SV39_PERMISSION_READ)
            == MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE);
    REQUIRE(micros_kernel_mapping_native_map_range(BASE + 1, BASE + PAGE, MICROS_SV39_PERMISSION_READ)
            == MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE);
    REQUIRE(micros_kernel_mapping_native_map_range(UINT64_MAX - PAGE + 1, 0, MICROS_SV39_PERMISSION_READ)
            == MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE);
    REQUIRE(!micros_kernel_mapping_native_lookup(UINT64_C(0x40000000), &output, &permissions));
    REQUIRE(!micros_kernel_mapping_native_lookup(UINT64_C(1) << 39, &output, &permissions));
    REQUIRE(output == UINT64_MAX && permissions == UINT32_MAX && snapshot_is_unchanged());
    state()->table_count = 1024;
    take_snapshot();
    REQUIRE(micros_kernel_mapping_native_allocate_table(NULL) == MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE);
    REQUIRE(micros_kernel_mapping_native_allocate_table(&output) == MICROS_KERNEL_ADDRESS_SPACE_ERROR_CAPACITY);
    REQUIRE(output == UINT64_MAX && snapshot_is_unchanged());
    state()->table_count = saved_count;
    return true;
}

static bool current_metadata_guards(void)
{
#if !defined(MICROS_KERNEL_MAPPING_BASELINE)
    uint16_t saved = state()->level1_table_ids[2];
    uint16_t range = state()->table_range_indices[0];
    uint64_t physical = state()->table_frames[0];

    state()->level1_table_ids[2] = 0;
    REQUIRE(rejected_without_writes());
    state()->level1_table_ids[2] = UINT16_MAX;
    REQUIRE(rejected_without_writes());
    state()->level1_table_ids[2] = state()->level1_table_ids[0];
    REQUIRE(rejected_without_writes());
    state()->level1_table_ids[2] = saved;
    state()->table_range_indices[0] = (uint16_t)model_range_count;
    REQUIRE(rejected_without_writes());
    state()->table_range_indices[0] = range;
    state()->table_frames[0] = state()->table_frames[1];
    REQUIRE(rejected_without_writes());
    state()->table_frames[0] = physical;
    saved = state()->level0_table_ids[2][1];
    state()->level0_table_ids[2][1] = 0;
    REQUIRE(rejected_without_writes());
    state()->level0_table_ids[2][1] = saved;
    state()->level1_table_ids[1] = 1;
    REQUIRE(rejected_without_writes());
    state()->level1_table_ids[1] = 0;
    state()->level0_table_ids[1][511] = UINT16_MAX;
    take_snapshot();
    REQUIRE(micros_kernel_mapping_native_validate() && snapshot_is_unchanged());
    state()->level0_table_ids[1][511] = 0;
    state()->level0_table_ids[2][511] = 1;
    REQUIRE(rejected_without_writes());
    state()->level0_table_ids[2][511] = 0;
#endif
    return true;
}

static bool current_row_preparation_guard(void)
{
#if !defined(MICROS_KERNEL_MAPPING_BASELINE)
    configure_dense(BASE, 65);
    REQUIRE(initialize_case());
    prepare_domains();
    state()->level0_table_ids[2][511] = UINT16_MAX;
    REQUIRE(micros_kernel_mapping_native_construct() == MICROS_KERNEL_ADDRESS_SPACE_OK);
    REQUIRE(state()->level0_table_ids[2][511] == 0);
    REQUIRE(micros_kernel_mapping_native_validate() && complete_semantics());
#endif
    return true;
}

static bool corruption_guards(void)
{
    configure_dense(BASE, 65);
    REQUIRE(initialize_case());
    prepare_domains();
    REQUIRE(micros_kernel_mapping_native_construct() == MICROS_KERNEL_ADDRESS_SPACE_OK);
    REQUIRE(micros_kernel_mapping_native_validate());
    REQUIRE(scalar_and_owner_guards());
    REQUIRE(pte_guards());
    REQUIRE(range_and_capacity_guards());
    REQUIRE(current_metadata_guards());
    REQUIRE(micros_kernel_mapping_native_validate() && complete_semantics());
    return true;
}

int main(void)
{
    size_t index;

    if (!failed_allocation_guards() || !corruption_guards() || !current_row_preparation_guard()) {
        return 1;
    }
    printf("{\"operation\":\"guards\",\"semantic\":true,\"baseline\":");
#if defined(MICROS_KERNEL_MAPPING_BASELINE)
    printf("true");
#else
    printf("false");
#endif
    printf("}\n");
    for (index = 0; index < 17; ++index) {
        if (!measured_case(index)) {
            return 1;
        }
    }
    return capture_label == 85 ? 0 : 1;
}
