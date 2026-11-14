#include "selected_ownership_test.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(MICROS_SELECTED_OWNERSHIP_LAUNCHER)
#include <unistd.h>
#endif

#include "kernel/pm/pm_control_core.h"
#include "kernel/pm/pm_control_runtime_core.h"
#include "micros/bootstrap.h"
#include "micros/kernel_object_runtime.h"

#if defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
#if MICROS_SELECTED_OWNERSHIP_BASELINE != 1
#error "MEM03 baseline must be explicitly equal to 1"
#endif
#else
#include "kernel/memory/frame_ownership_internal.h"
#endif

#define REQUIRE(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return false; \
        } \
    } while (false)

#define TEST_BASE UINT64_C(0xc0000000)
#define TEST_PRODUCER UINT64_C(0x4d454d3033000000)
#define TEST_OWNER_MASK UINT32_C(39036)

enum operation {
    OP_ALLOCATE,
    OP_LOOKUP,
    OP_RELEASE,
    OP_PREPARE_ONE,
    OP_CLAIM,
    OP_RUNTIME_RELEASE,
    OP_PM_IDLE,
    OP_AUDIT_PORTABLE,
    OP_AUDIT_RUNTIME,
    OP_FREEZE,
    OP_RELEASE_TRANSFERABLE,
    OP_RELEASE_PROCESS,
    OP_RELEASE_BITMAP,
    OP_STAGE_WIRED,
    OP_COMPLETE_HANDOFF,
};

struct selected_test_context {
    struct micros_physical_range memory[16];
    struct micros_physical_range reserved[80];
    struct micros_kernel_objects objects;
    struct micros_kernel_objects expected_objects;
    struct micros_frame_allocator expected_allocator;
    struct micros_frame_ownership expected_ownership;
    struct micros_pm_control_state pm;
    struct micros_pm_control_state expected_pm;
    struct micros_process_handle process;
    uint64_t data[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    uint64_t tables[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
    uint64_t bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
    uint64_t selected[3];
    uint64_t selected_count;
    uint64_t frames;
    uint64_t case_id;
    uint64_t producer;
    uint64_t output;
    uint64_t expected_output;
    size_t data_count;
    bool fragmented;
    bool irq;
#if !defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
    struct micros_frame_selection selection;
    uint64_t ordinals[MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES];
    uint32_t ordered_frames[MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES];
    uint64_t next_ordinal;
    bool expected_index_changed;
#endif
#if defined(MICROS_TEST_WORK_COUNTS)
    struct micros_test_work_snapshot observation;
#endif
};

static struct selected_test_context context;

bool micros_selected_native_irq_enabled(void)
{
    return context.irq;
}

const struct micros_kernel_objects *micros_kernel_object_runtime_registry(void)
{
    return &context.objects;
}

static struct micros_frame_allocator *allocator(void)
{
    return micros_selected_native_allocator();
}

static struct micros_frame_ownership *ledger(void)
{
    return micros_selected_native_ownership();
}

static uint64_t frame_address(uint64_t index)
{
    uint64_t page = context.fragmented
        ? (index / 6) * 12 + (index % 6) * 2
        : index;

    return TEST_BASE + page * MICROS_FRAME_SIZE;
}

static struct micros_frame_owner kernel_owner(enum micros_frame_owner_kind kind)
{
    return (struct micros_frame_owner){0, 0, (uint8_t)kind, 0};
}

static struct micros_frame_owner process_owner(enum micros_frame_owner_kind kind)
{
    return (struct micros_frame_owner){
        context.process.generation, context.process.slot, (uint8_t)kind, 0,
    };
}

#if !defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
static bool indexed_owner(struct micros_frame_owner owner)
{
    return owner.kind == MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
        || owner.kind == MICROS_FRAME_OWNER_PROCESS_USER || owner.kind == MICROS_FRAME_OWNER_VM_WIRED;
}

static int compare_index_ordinals(const void *left, const void *right)
{
    uint64_t a = context.ordinals[*(const uint32_t *)left];
    uint64_t b = context.ordinals[*(const uint32_t *)right];

    return a > b ? -1 : a < b ? 1 : 0;
}

static bool rebuild_expected_index(void)
{
    struct micros_frame_ownership *expected = &context.expected_ownership;
    uint32_t previous[MICROS_PROCESS_CAPACITY] = {0};
    size_t count = 0;
    uint64_t index;

    memset(expected->owner_links, 0, sizeof(expected->owner_links));
    memset(expected->process_frames, 0, sizeof(expected->process_frames));
    for (index = 0; index < expected->managed_frame_count; ++index) {
        if (indexed_owner(expected->owners[index])) {
            REQUIRE(context.ordinals[index] != 0 && expected->owners[index].slot < MICROS_PROCESS_CAPACITY);
            context.ordered_frames[count++] = (uint32_t)index;
        }
    }
    qsort(context.ordered_frames, count, sizeof(context.ordered_frames[0]), compare_index_ordinals);
    for (index = 0; index < count; ++index) {
        uint32_t frame = context.ordered_frames[index];
        uint16_t slot = expected->owners[frame].slot;

        expected->owner_links[frame].previous = previous[slot];
        if (previous[slot] == 0) {
            expected->process_frames[slot].head = frame + 1;
        } else {
            expected->owner_links[previous[slot] - 1].next = frame + 1;
        }
        ++expected->process_frames[slot].count;
        previous[slot] = frame + 1;
    }
    context.expected_index_changed = false;
    return true;
}
#endif

static void prepare_selection_input(void)
{
#if !defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
    struct micros_frame_selection *selection = &context.selection;

    memset(selection, 0, sizeof(*selection));
    for (size_t word = 0; word < MICROS_FRAME_ALLOCATOR_BITMAP_WORDS; ++word) {
        uint64_t bits = context.bitmap[word];

        if (bits == 0) {
            continue;
        }
        selection->bitmap[word] = bits;
        selection->words[selection->word_count] = (uint16_t)word;
        selection->positions[word] = (uint16_t)++selection->word_count;
        for (unsigned bit = 0; bit < 64; ++bit) {
            selection->frame_count += (bits >> bit) & 1;
        }
    }
#endif
}

static enum micros_frame_ownership_error release_selected_input(
    struct micros_frame_ownership *ownership,
    struct micros_process_handle process,
    const uint64_t *bitmap,
    size_t words
)
{
#if defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
    return micros_frame_ownership_release_process_set(ownership, process, bitmap, words);
#else
    if (bitmap != NULL && words != MICROS_FRAME_ALLOCATOR_BITMAP_WORDS) {
        context.selection.word_count = MICROS_FRAME_ALLOCATOR_BITMAP_WORDS + 1;
    }
    return micros_frame_ownership_release_process_set(ownership, process, bitmap == NULL ? NULL : &context.selection);
#endif
}

static enum micros_frame_ownership_error stage_selected_input(
    struct micros_frame_ownership *ownership,
    const uint64_t *bitmap,
    size_t words
)
{
#if defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
    return micros_frame_ownership_prepare_wired_process_user_set(ownership, bitmap, words);
#else
    if (bitmap != NULL && words != MICROS_FRAME_ALLOCATOR_BITMAP_WORDS) {
        context.selection.word_count = MICROS_FRAME_ALLOCATOR_BITMAP_WORDS + 1;
    }
    return micros_frame_ownership_prepare_wired_process_user_set(ownership, bitmap == NULL ? NULL : &context.selection);
#endif
}

static bool initialize(uint64_t frames, bool fragmented)
{
    size_t index;
    size_t hole;

    context.frames = frames;
    context.fragmented = fragmented;
    context.irq = false;
    context.data_count = 0;
    context.selected_count = 0;
    memset(&context.objects, 0, sizeof(context.objects));
    memset(&context.pm, 0, sizeof(context.pm));
    memset(context.bitmap, 0, sizeof(context.bitmap));
    memset(context.memory, 0, sizeof(context.memory));
    memset(context.reserved, 0, sizeof(context.reserved));
#if !defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
    memset(context.ordinals, 0, sizeof(context.ordinals));
    memset(&context.selection, 0, sizeof(context.selection));
    context.next_ordinal = 0;
    context.expected_index_changed = false;
#endif
    micros_selected_native_reset();
    if (fragmented) {
        REQUIRE(frames == 96);
        for (index = 0; index < 16; ++index) {
            uint64_t base = TEST_BASE + index * 12 * MICROS_FRAME_SIZE;

            context.memory[15 - index] = (struct micros_physical_range){
                base, 11 * MICROS_FRAME_SIZE,
            };
            for (hole = 0; hole < 5; ++hole) {
                context.reserved[79 - (index * 5 + hole)] =
                    (struct micros_physical_range){
                        base + (2 * hole + 1) * MICROS_FRAME_SIZE,
                        MICROS_FRAME_SIZE,
                    };
            }
        }
    } else {
        context.memory[0] = (struct micros_physical_range){
            TEST_BASE, frames * MICROS_FRAME_SIZE,
        };
    }
    REQUIRE(micros_frame_allocator_initialize(
        allocator(), context.memory, fragmented ? 16 : 1,
        context.reserved, fragmented ? 80 : 0
    ) == MICROS_FRAME_ALLOCATOR_OK);
    REQUIRE(allocator()->managed_frame_count == frames);
    REQUIRE(allocator()->free_frame_count == frames);
    REQUIRE(allocator()->managed_range_count == (fragmented ? 96 : 1));
    for (index = 0; index < allocator()->managed_range_count; ++index) {
        REQUIRE(allocator()->managed_ranges[index].base == frame_address(fragmented ? index : 0));
        REQUIRE(allocator()->managed_ranges[index].frame_count == (fragmented ? 1 : frames));
        REQUIRE(allocator()->managed_ranges[index].bitmap_offset == (fragmented ? index : 0));
    }
    REQUIRE(micros_frame_ownership_runtime_initialize() == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(micros_kernel_objects_initialize(&context.objects, 1, 1) == MICROS_KERNEL_OBJECT_OK);
    context.process = (struct micros_process_handle){0, 1};
    return true;
}

static bool create_process(uint16_t slot)
{
    REQUIRE(micros_process_create_at(
        &context.objects, slot, &context.process
    ) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(context.process.slot == slot && context.process.generation == 1);
    return true;
}

static bool allocate_setup(struct micros_frame_owner owner, uint64_t index)
{
    uint64_t address = UINT64_MAX;

    REQUIRE(micros_frame_ownership_allocate(ledger(), owner, &address) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(address == frame_address(index));
    REQUIRE(memcmp(&ledger()->owners[index], &owner, sizeof(owner)) == 0);
#if !defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
    if (indexed_owner(owner)) {
        context.ordinals[index] = ++context.next_ordinal;
    }
#endif
    return true;
}

static bool claim_setup(uint64_t index)
{
    struct micros_frame_vm_claim_plan plan;

    REQUIRE(micros_frame_ownership_vm_claim_preflight(
        ledger(), frame_address(index), &plan
    ) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(plan.frame_index == index);
    micros_frame_ownership_vm_claim_commit_prevalidated(ledger(), &plan);
    return true;
}

static void save_expected(void)
{
    prepare_selection_input();
#if !defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
    context.expected_index_changed = false;
#endif
    context.expected_allocator = *allocator();
    context.expected_ownership = *ledger();
    context.expected_objects = context.objects;
    context.expected_pm = context.pm;
    context.output = UINT64_MAX;
    context.expected_output = UINT64_MAX;
}

static void expected_owner(uint64_t index, struct micros_frame_owner owner)
{
    struct micros_frame_owner before = context.expected_ownership.owners[index];
    uint64_t bit = UINT64_C(1) << (index % 64);

    if (before.kind == MICROS_FRAME_OWNER_FREE && owner.kind != MICROS_FRAME_OWNER_FREE) {
        context.expected_allocator.allocated_bitmap[index / 64] |= bit;
        --context.expected_allocator.free_frame_count;
        ++context.expected_ownership.owned_frame_count;
    } else if (before.kind != MICROS_FRAME_OWNER_FREE && owner.kind == MICROS_FRAME_OWNER_FREE) {
        context.expected_allocator.allocated_bitmap[index / 64] &= ~bit;
        ++context.expected_allocator.free_frame_count;
        --context.expected_ownership.owned_frame_count;
        if (index < context.expected_allocator.allocation_cursor) {
            context.expected_allocator.allocation_cursor = index;
        }
    }
    if (before.kind != MICROS_FRAME_OWNER_FREE) {
        --context.expected_ownership.owner_counts[before.kind];
    }
    if (owner.kind != MICROS_FRAME_OWNER_FREE) {
        ++context.expected_ownership.owner_counts[owner.kind];
    }
#if !defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
    if (indexed_owner(before) && !indexed_owner(owner)) {
        context.ordinals[index] = 0;
        context.expected_index_changed = true;
    } else if (indexed_owner(owner) && (!indexed_owner(before)
               || before.slot != owner.slot || before.generation != owner.generation)) {
        context.ordinals[index] = ++context.next_ordinal;
        context.expected_index_changed = true;
    }
#endif
    context.expected_ownership.owners[index] = owner;
    context.expected_ownership.handoff_targets[index] = MICROS_FRAME_HANDOFF_NONE;
}

static bool storage_matches_expected(void)
{
#if !defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
    REQUIRE(!context.expected_index_changed || rebuild_expected_index());
#endif
    REQUIRE(memcmp(allocator(), &context.expected_allocator, sizeof(context.expected_allocator)) == 0);
    REQUIRE(memcmp(ledger(), &context.expected_ownership, sizeof(context.expected_ownership)) == 0);
    REQUIRE(memcmp(&context.objects, &context.expected_objects, sizeof(context.objects)) == 0);
    REQUIRE(memcmp(&context.pm, &context.expected_pm, sizeof(context.pm)) == 0);
    REQUIRE(context.output == context.expected_output);
    return true;
}

static bool state_matches_expected(void)
{
    REQUIRE(storage_matches_expected());
    REQUIRE(micros_frame_ownership_runtime_validate(&context.objects) == MICROS_FRAME_OWNERSHIP_OK);
    return true;
}

static bool begin_capture(void)
{
    ++context.case_id;
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(micros_test_work_begin(
        context.producer, context.case_id, TEST_OWNER_MASK
    ) == MICROS_TEST_WORK_OK);
#endif
    return true;
}

static bool finish_capture(enum operation operation, int result)
{
#if defined(MICROS_TEST_WORK_COUNTS)
    unsigned owner;
    unsigned kind;

    REQUIRE(micros_test_work_finish(&context.observation) == MICROS_TEST_WORK_OK);
#endif
    REQUIRE(result == 0);
    REQUIRE(state_matches_expected());
    printf(
        "MICROS_MEM03_CASE {\"case\":%" PRIu64 ",\"operation\":%u,"
        "\"frames\":%" PRIu64 ",\"ranges\":%zu,\"q\":%zu,\"result\":%d,"
        "\"output\":%" PRIu64 ",\"free\":%" PRIu64 ",\"owned\":%" PRIu64
        ",\"cursor\":%" PRIu64 ",\"phase\":%u,\"state\":\"PASS\"}\n",
        context.case_id, (unsigned)operation, context.frames, allocator()->managed_range_count,
        context.data_count, result, context.output, allocator()->free_frame_count,
        ledger()->owned_frame_count, allocator()->allocation_cursor, (unsigned)ledger()->phase
    );
#if defined(MICROS_TEST_WORK_COUNTS)
    printf(
        "MICROS_MEM03_WORK {\"version\":%" PRIu32 ",\"reserved0\":%" PRIu32
        ",\"reserved1\":%" PRIu32 ",\"producer_label\":%" PRIu64
        ",\"capture_label\":%" PRIu64 ",\"owner_mask\":%" PRIu32 ",\"counts\":[",
        context.observation.version, context.observation.reserved0, context.observation.reserved1,
        context.observation.producer_label, context.observation.capture_label, context.observation.owner_mask
    );
    for (owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        printf("%s[", owner == 0 ? "" : ",");
        for (kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            printf("%s%" PRIu64, kind == 0 ? "" : ",", context.observation.counts[owner][kind]);
        }
        printf("]");
    }
    printf("]}\n");
#endif
    return true;
}

static bool prepare_point(enum operation operation)
{
    if (operation == OP_RELEASE) {
        REQUIRE(allocate_setup(kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY), 0));
    } else if (operation == OP_PREPARE_ONE) {
        REQUIRE(create_process(0));
        REQUIRE(allocate_setup(process_owner(MICROS_FRAME_OWNER_PROCESS_USER), 0));
    } else if (operation == OP_CLAIM) {
        REQUIRE(micros_frame_ownership_complete_handoff(ledger()) == MICROS_FRAME_OWNERSHIP_OK);
    } else if (operation == OP_RUNTIME_RELEASE) {
        REQUIRE(create_process(0));
    } else if (operation == OP_PM_IDLE) {
        struct micros_thread_handle thread;

        REQUIRE(create_process(2));
        REQUIRE(micros_thread_create(
            &context.objects, context.process, &thread
        ) == MICROS_KERNEL_OBJECT_OK);
        REQUIRE(micros_pm_control_state_prepare(
            &context.pm, 3,
            (context.process.generation << 12) | context.process.slot,
            MICROS_PRIVILEGE_PROFILE_PM, context.process, thread
        ) == MICROS_PM_CONTROL_OK);
    }
    REQUIRE(micros_frame_ownership_runtime_validate(&context.objects) == MICROS_FRAME_OWNERSHIP_OK);
    save_expected();
    if (operation == OP_ALLOCATE) {
        expected_owner(0, kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY));
        context.expected_allocator.allocation_cursor = 1;
        context.expected_output = frame_address(0);
    } else if (operation == OP_LOOKUP) {
        context.expected_output = MICROS_FRAME_OWNER_FREE;
    } else if (operation == OP_RELEASE) {
        expected_owner(0, kernel_owner(MICROS_FRAME_OWNER_FREE));
    } else if (operation == OP_PREPARE_ONE) {
        context.expected_ownership.handoff_targets[0] = MICROS_FRAME_HANDOFF_VM_WIRED;
    } else if (operation == OP_CLAIM) {
        expected_owner(context.frames - 1, kernel_owner(MICROS_FRAME_OWNER_VM_TRANSFERABLE));
        context.expected_output = context.frames - 1;
    } else if (operation == OP_RUNTIME_RELEASE) {
        context.expected_objects.processes[context.process.slot].slot_state = MICROS_KERNEL_OBJECT_SLOT_FREE;
        --context.expected_objects.live_process_count;
    }
    return true;
}

static int execute_point(enum operation operation)
{
    switch (operation) {
    case OP_ALLOCATE:
        return micros_frame_ownership_allocate(
            ledger(), kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY), &context.output
        );
    case OP_LOOKUP: {
        struct micros_frame_owner owner;
        enum micros_frame_ownership_error error = micros_frame_ownership_lookup(
            ledger(), frame_address(context.frames - 1), &owner
        );

        if (error == MICROS_FRAME_OWNERSHIP_OK) {
            const struct micros_frame_owner expected = {0, 0, MICROS_FRAME_OWNER_FREE, 0};

            if (memcmp(&owner, &expected, sizeof(owner)) != 0) {
                return -1;
            }
            context.output = owner.kind;
        }
        return error;
    }
    case OP_RELEASE:
        return micros_frame_ownership_release(
            ledger(), kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY), frame_address(0)
        );
    case OP_PREPARE_ONE:
        return micros_frame_ownership_prepare_handoff(
            ledger(), frame_address(0), process_owner(MICROS_FRAME_OWNER_PROCESS_USER),
            MICROS_FRAME_HANDOFF_VM_WIRED
        );
    case OP_CLAIM: {
        struct micros_frame_vm_claim_plan plan;
        enum micros_frame_ownership_error error = micros_frame_ownership_vm_claim_preflight(
            ledger(), frame_address(context.frames - 1), &plan
        );

        if (error == MICROS_FRAME_OWNERSHIP_OK) {
            context.output = plan.frame_index;
            micros_frame_ownership_vm_claim_commit_prevalidated(ledger(), &plan);
        }
        return error;
    }
    case OP_RUNTIME_RELEASE:
        return micros_frame_ownership_runtime_release_process(&context.objects, context.process);
    case OP_PM_IDLE:
        return micros_pm_control_runtime_validate_reservation(&context.pm, &context.objects, ledger(), NULL);
    case OP_AUDIT_PORTABLE:
        return micros_frame_ownership_validate(ledger());
    case OP_AUDIT_RUNTIME:
        return micros_frame_ownership_runtime_validate(&context.objects);
    default:
        return -1;
    }
}

static bool point_cases(uint64_t frames, bool fragmented)
{
    enum operation operation;

    for (operation = OP_ALLOCATE; operation <= OP_AUDIT_RUNTIME; ++operation) {
        int result;

        REQUIRE(initialize(frames, fragmented));
        REQUIRE(prepare_point(operation));
        REQUIRE(begin_capture());
        result = execute_point(operation);
        REQUIRE(finish_capture(operation, result));
    }
    return true;
}

static bool prepare_set(uint64_t frames, size_t count, enum operation operation)
{
    size_t index;

    REQUIRE(initialize(frames, false));
    REQUIRE(create_process(0));
    REQUIRE(micros_frame_ownership_complete_handoff(ledger()) == MICROS_FRAME_OWNERSHIP_OK);
    context.data_count = count;
    for (index = 0; index < count; ++index) {
        context.data[index] = frame_address(index);
        REQUIRE(claim_setup(index));
    }
    for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        context.tables[index] = frame_address(count + index);
    }
    if (operation == OP_RELEASE_PROCESS) {
        struct micros_frame_vm_freeze_plan plan;

        REQUIRE(micros_frame_ownership_vm_freeze_preflight(
            ledger(), context.process, context.data, count, context.tables, &plan
        ) == MICROS_FRAME_OWNERSHIP_OK);
        micros_frame_ownership_vm_freeze_commit_prevalidated(ledger(), &plan);
#if !defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
        for (index = 0; index < count + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
            context.ordinals[index] = ++context.next_ordinal;
        }
#endif
    }
    REQUIRE(micros_frame_ownership_runtime_validate(&context.objects) == MICROS_FRAME_OWNERSHIP_OK);
    save_expected();
    for (index = 0; index < count; ++index) {
        expected_owner(index, operation == OP_FREEZE
            ? process_owner(MICROS_FRAME_OWNER_VM_WIRED)
            : kernel_owner(MICROS_FRAME_OWNER_FREE));
    }
    if (operation != OP_RELEASE_TRANSFERABLE) {
        for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
            expected_owner(count + index, operation == OP_FREEZE
                ? process_owner(MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE)
                : kernel_owner(MICROS_FRAME_OWNER_FREE));
        }
    }
    return true;
}

static bool frame_set_plan_is_valid(
    struct micros_process_handle process,
    uint32_t count,
    uint32_t reserved,
    const uint64_t *data,
    const uint64_t *tables
)
{
    size_t index;

    if (
        process.slot != context.process.slot
        || process.generation != context.process.generation
        || count != context.data_count
        || reserved != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_APPLICATION_RESIDENT_PAGE_MAX; ++index) {
        if (data[index] != (index < context.data_count ? index : 0)) {
            return false;
        }
    }
    for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        if (tables[index] != context.data_count + index) {
            return false;
        }
    }
    return true;
}

static int execute_set(enum operation operation)
{
    enum micros_frame_ownership_error error;

    if (operation == OP_FREEZE) {
        struct micros_frame_vm_freeze_plan plan;

        error = micros_frame_ownership_vm_freeze_preflight(
            ledger(), context.process, context.data, context.data_count, context.tables, &plan
        );
        if (error == MICROS_FRAME_OWNERSHIP_OK) {
            if (!frame_set_plan_is_valid(
                plan.process, plan.data_frame_count, plan.reserved,
                plan.data_frame_indices, plan.table_frame_indices
            )) {
                return -1;
            }
            micros_frame_ownership_vm_freeze_commit_prevalidated(ledger(), &plan);
        }
    } else if (operation == OP_RELEASE_TRANSFERABLE) {
        struct micros_frame_vm_transferable_release_plan plan;

        error = micros_frame_ownership_vm_release_transferable_preflight(
            ledger(), context.data, context.data_count, &plan
        );
        if (error == MICROS_FRAME_OWNERSHIP_OK) {
            size_t index;

            if (plan.frame_count != context.data_count || plan.reserved != 0) {
                return -1;
            }
            for (index = 0; index < MICROS_APPLICATION_RESIDENT_PAGE_MAX; ++index) {
                if (plan.frame_indices[index] != (index < context.data_count ? index : 0)) {
                    return -1;
                }
            }
            micros_frame_ownership_vm_release_transferable_commit_prevalidated(ledger(), &plan);
        }
    } else {
        struct micros_frame_vm_process_release_plan plan;

        error = micros_frame_ownership_vm_release_process_preflight(
            ledger(), context.process, context.data, context.data_count, context.tables, &plan
        );
        if (error == MICROS_FRAME_OWNERSHIP_OK) {
            if (!frame_set_plan_is_valid(
                plan.process, plan.data_frame_count, plan.reserved,
                plan.data_frame_indices, plan.table_frame_indices
            )) {
                return -1;
            }
            micros_frame_ownership_vm_release_process_commit_prevalidated(ledger(), &plan);
        }
    }
    return error;
}

static bool set_cases(void)
{
    static const uint64_t frames[] = {65, MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES};
    static const size_t counts[] = {1, MICROS_APPLICATION_RESIDENT_PAGE_MAX};
    size_t memory_index;
    size_t count_index;
    enum operation operation;

    for (memory_index = 0; memory_index < 2; ++memory_index) {
        for (count_index = 0; count_index < 2; ++count_index) {
            for (operation = OP_FREEZE; operation <= OP_RELEASE_PROCESS; ++operation) {
                int result;

                REQUIRE(prepare_set(frames[memory_index], counts[count_index], operation));
                REQUIRE(begin_capture());
                result = execute_set(operation);
                REQUIRE(finish_capture(operation, result));
            }
        }
    }
    return true;
}

static bool selected_index(uint64_t index)
{
    size_t selected;

    for (selected = 0; selected < context.selected_count; ++selected) {
        if (context.selected[selected] == index) {
            return true;
        }
    }
    return false;
}

static bool prepare_bitmap(uint64_t frames, enum operation operation)
{
    uint64_t index;

    REQUIRE(initialize(frames, false));
    REQUIRE(create_process(0));
    context.selected_count = frames == 65 ? 2 : 3;
    context.selected[0] = 0;
    context.selected[1] = frames == 65 ? 64 : 63;
    context.selected[2] = 64;
    context.data_count = (size_t)context.selected_count;
    for (index = 0; index < 65; ++index) {
        REQUIRE(allocate_setup(
            selected_index(index) ? process_owner(MICROS_FRAME_OWNER_PROCESS_USER)
                : kernel_owner(MICROS_FRAME_OWNER_KERNEL_RETAINED),
            index
        ));
        if (selected_index(index)) {
            context.bitmap[index / 64] |= UINT64_C(1) << (index % 64);
        }
    }
    if (operation != OP_RELEASE_BITMAP) {
        REQUIRE(micros_frame_ownership_prepare_handoff(
            ledger(), frame_address(1), kernel_owner(MICROS_FRAME_OWNER_KERNEL_RETAINED),
            MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        ) == MICROS_FRAME_OWNERSHIP_OK);
    }
    prepare_selection_input();
    if (operation == OP_COMPLETE_HANDOFF) {
        REQUIRE(stage_selected_input(
            ledger(), context.bitmap, MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        ) == MICROS_FRAME_OWNERSHIP_OK);
    }
    REQUIRE(micros_frame_ownership_runtime_validate(&context.objects) == MICROS_FRAME_OWNERSHIP_OK);
    save_expected();
    for (index = 0; index < context.selected_count; ++index) {
        uint64_t frame = context.selected[index];

        if (operation == OP_RELEASE_BITMAP) {
            expected_owner(frame, kernel_owner(MICROS_FRAME_OWNER_FREE));
        } else if (operation == OP_STAGE_WIRED) {
            context.expected_ownership.handoff_targets[frame] = MICROS_FRAME_HANDOFF_VM_WIRED;
        } else {
            expected_owner(frame, process_owner(MICROS_FRAME_OWNER_VM_WIRED));
        }
    }
    if (operation == OP_COMPLETE_HANDOFF) {
        expected_owner(1, kernel_owner(MICROS_FRAME_OWNER_VM_TRANSFERABLE));
        context.expected_ownership.phase = MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF;
    }
    return true;
}

static bool bitmap_case(uint64_t frames, enum operation operation)
{
    enum micros_frame_ownership_error result;

    REQUIRE(prepare_bitmap(frames, operation));
    REQUIRE(begin_capture());
    if (operation == OP_RELEASE_BITMAP) {
        result = release_selected_input(
            ledger(), context.process, context.bitmap, MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        );
    } else if (operation == OP_STAGE_WIRED) {
        result = stage_selected_input(
            ledger(), context.bitmap, MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        );
    } else {
        result = micros_frame_ownership_complete_handoff(ledger());
    }
    REQUIRE(finish_capture(operation, result));
    return true;
}

static bool all_cases(void)
{
    static const uint64_t frames[] = {1, 63, 64, 65, 257, 4096, 262143, 262144};
    size_t index;

    for (index = 0; index < sizeof(frames) / sizeof(frames[0]); ++index) {
        REQUIRE(point_cases(frames[index], false));
    }
    REQUIRE(point_cases(96, true));
    REQUIRE(set_cases());
    REQUIRE(bitmap_case(65, OP_RELEASE_BITMAP));
    REQUIRE(bitmap_case(262144, OP_RELEASE_BITMAP));
    REQUIRE(bitmap_case(65, OP_STAGE_WIRED));
    REQUIRE(bitmap_case(65, OP_COMPLETE_HANDOFF));
    REQUIRE(bitmap_case(262144, OP_STAGE_WIRED));
    REQUIRE(bitmap_case(262144, OP_COMPLETE_HANDOFF));
    REQUIRE(context.case_id == 99);
    return true;
}

static bool buffer_is_byte(const void *buffer, size_t size, unsigned char expected)
{
    const unsigned char *bytes = buffer;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != expected) {
            return false;
        }
    }
    return true;
}

static void report_guard(const char *name)
{
    printf("MICROS_MEM03_GUARD %s PASS\n", name);
}

static bool corrupt_record(unsigned mutation)
{
    switch (mutation) {
    case 0: ledger()->owners[0].generation = 1; break;
    case 1: ledger()->owners[0].slot = 1; break;
    case 2: ledger()->owners[0].kind = MICROS_FRAME_OWNER_KIND_COUNT; break;
    case 3: ledger()->owners[0].reserved = 1; break;
    case 4: ledger()->handoff_targets[0] = MICROS_FRAME_HANDOFF_TARGET_COUNT; break;
    case 5: allocator()->allocated_bitmap[0] = 1; break;
    case 6: ledger()->owners[65].generation = 1; break;
    case 7: ledger()->owners[65].slot = 1; break;
    case 8: ledger()->owners[65].kind = MICROS_FRAME_OWNER_KERNEL_RETAINED; break;
    case 9: ledger()->owners[65].reserved = 1; break;
    case 10: ledger()->handoff_targets[65] = MICROS_FRAME_HANDOFF_VM_WIRED; break;
    case 11: allocator()->allocated_bitmap[1] = UINT64_C(1) << 1; break;
    case 12: allocator()->allocated_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS - 1] = UINT64_C(1) << 63; break;
    case 13: allocator()->managed_ranges[95].base = TEST_BASE; break;
    case 14: ledger()->managed_ranges[95].base = TEST_BASE; break;
    default: return false;
    }
    return true;
}

static bool corrupt_metadata(unsigned mutation)
{
    switch (mutation) {
    case 15: allocator()->allocation_cursor = 1; break;
    case 16:
        allocator()->managed_frame_count = 0;
        ledger()->managed_frame_count = 0;
        break;
    case 17:
        allocator()->managed_frame_count = MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES + 1;
        ledger()->managed_frame_count = allocator()->managed_frame_count;
        break;
    case 18:
        allocator()->managed_range_count = MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES + 1;
        ledger()->managed_range_count = allocator()->managed_range_count;
        break;
    case 19: ledger()->owned_frame_count = 1; break;
    case 20: ledger()->owner_counts[MICROS_FRAME_OWNER_KERNEL_RETAINED] = 1; break;
    case 21: ledger()->allocator_identity ^= 1; break;
    case 22: ledger()->phase = 0; break;
    case 23: allocator()->free_frame_count = 66; break;
    case 24: allocator()->allocation_cursor = 66; break;
    case 25: allocator()->initialized = false; break;
    case 26: ++allocator()->managed_ranges[0].base; break;
    case 27: ledger()->managed_ranges[0].base += MICROS_FRAME_SIZE; break;
    case 28: ++allocator()->managed_frame_count; break;
    default: return false;
    }
    return true;
}

static bool diagnostic_guards(void)
{
    unsigned mutation;

    for (mutation = 0; mutation < 29; ++mutation) {
        REQUIRE(initialize(65, false));
        REQUIRE(mutation < 15 ? corrupt_record(mutation) : corrupt_metadata(mutation));
        save_expected();
        REQUIRE(micros_frame_ownership_validate(ledger()) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
        REQUIRE(storage_matches_expected());
        REQUIRE(micros_frame_ownership_runtime_validate(&context.objects) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
        REQUIRE(storage_matches_expected());
        printf("MICROS_MEM03_GUARD diagnostic-corruption-%u PASS\n", mutation);
    }
    return true;
}

static bool argument_guards(void)
{
    struct micros_frame_owner owner;

    REQUIRE(initialize(65, false));
    save_expected();
    memset(&owner, 0xa5, sizeof(owner));
    REQUIRE(micros_frame_ownership_lookup(NULL, TEST_BASE, &owner) == MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT);
    REQUIRE(micros_frame_ownership_lookup(ledger(), TEST_BASE, NULL) == MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT);
    REQUIRE(micros_frame_ownership_lookup(ledger(), TEST_BASE + 1, &owner) == MICROS_FRAME_OWNERSHIP_ERROR_UNALIGNED);
    REQUIRE(micros_frame_ownership_lookup(ledger(), TEST_BASE - MICROS_FRAME_SIZE, &owner) == MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED);
    REQUIRE(buffer_is_byte(&owner, sizeof(owner), 0xa5));
    REQUIRE(micros_frame_ownership_allocate(
        ledger(), kernel_owner(MICROS_FRAME_OWNER_FREE), &context.output
    ) == MICROS_FRAME_OWNERSHIP_ERROR_OWNER);
    REQUIRE(state_matches_expected());
    return true;
}

static bool exhaustion_guard(void)
{
    REQUIRE(initialize(1, false));
    REQUIRE(allocate_setup(kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY), 0));
    save_expected();
    REQUIRE(micros_frame_ownership_allocate(
        ledger(), kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY), &context.output
    ) == MICROS_FRAME_OWNERSHIP_ERROR_EXHAUSTED);
    REQUIRE(state_matches_expected());
    return true;
}

static bool rollback_guard(void)
{
    REQUIRE(initialize(65, false));
    REQUIRE(allocate_setup(kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY), 0));
    allocator()->allocation_cursor = 0;
    ledger()->owners[1] = kernel_owner(MICROS_FRAME_OWNER_KERNEL_RETAINED);
    save_expected();
    REQUIRE(micros_frame_ownership_allocate(
        ledger(), kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY), &context.output
    ) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(storage_matches_expected());
    return true;
}

static bool empty_bootstrap_guard(void)
{
    REQUIRE(initialize(65, false));
    REQUIRE(create_process(0));
    save_expected();
    REQUIRE(release_selected_input(
        ledger(), context.process, context.bitmap, MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
    ) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(state_matches_expected());
    return true;
}

static bool empty_transferable_guard(void)
{
    struct micros_frame_vm_transferable_release_plan plan;

    REQUIRE(initialize(65, false));
    REQUIRE(micros_frame_ownership_complete_handoff(ledger()) == MICROS_FRAME_OWNERSHIP_OK);
    save_expected();
    memset(&plan, 0xa5, sizeof(plan));
    REQUIRE(micros_frame_ownership_vm_release_transferable_preflight(
        ledger(), NULL, 0, &plan
    ) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(buffer_is_byte(&plan, sizeof(plan), 0));
    micros_frame_ownership_vm_release_transferable_commit_prevalidated(ledger(), &plan);
    REQUIRE(state_matches_expected());
    return true;
}

static bool empty_phase_guard(void)
{
    struct micros_frame_vm_transferable_release_plan plan;

    REQUIRE(initialize(65, false));
    REQUIRE(create_process(0));
    save_expected();
    memset(&plan, 0xa5, sizeof(plan));
    REQUIRE(micros_frame_ownership_vm_release_transferable_preflight(
        ledger(), NULL, 0, &plan
    ) == MICROS_FRAME_OWNERSHIP_ERROR_PHASE);
    REQUIRE(buffer_is_byte(&plan, sizeof(plan), 0xa5));
    REQUIRE(state_matches_expected());
    REQUIRE(micros_frame_ownership_complete_handoff(ledger()) == MICROS_FRAME_OWNERSHIP_OK);
    save_expected();
    REQUIRE(release_selected_input(
        ledger(), context.process, context.bitmap, MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
    ) == MICROS_FRAME_OWNERSHIP_ERROR_PHASE);
    REQUIRE(state_matches_expected());
    return true;
}

static bool selection_guard(void)
{
    REQUIRE(initialize(65, false));
    REQUIRE(create_process(0));
    save_expected();
    REQUIRE(release_selected_input(
        ledger(), context.process, NULL, MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
    ) == MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT);
    REQUIRE(release_selected_input(
        ledger(), context.process, context.bitmap, MICROS_FRAME_ALLOCATOR_BITMAP_WORDS - 1
    ) == MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT);
    context.bitmap[1] = UINT64_C(1) << 1;
    prepare_selection_input();
    REQUIRE(release_selected_input(
        ledger(), context.process, context.bitmap, MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
    ) == MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED);
    REQUIRE(state_matches_expected());
    return true;
}

static bool malformed_vm_plan(unsigned mutation)
{
    struct micros_frame_vm_freeze_plan plan;

    REQUIRE(prepare_set(65, 2, OP_FREEZE));
    if (mutation == 0) {
        context.data[1] = context.data[0];
    } else if (mutation == 1) {
        context.tables[0] = context.data[0];
    } else if (mutation == 2) {
        context.tables[1] = context.tables[0];
    } else {
        REQUIRE(mutation == 3);
        REQUIRE(claim_setup(2));
    }
    save_expected();
    memset(&plan, 0xa5, sizeof(plan));
    REQUIRE(micros_frame_ownership_vm_freeze_preflight(
        ledger(), context.process, context.data, 2, context.tables, &plan
    ) == (mutation == 3 ? MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER : MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION));
    REQUIRE(buffer_is_byte(&plan, sizeof(plan), 0xa5));
    REQUIRE(state_matches_expected());
    return true;
}

static bool process_identity_guard(void)
{
    struct micros_process_handle stale;
    struct micros_frame_owner wrong;

    REQUIRE(initialize(65, false));
    REQUIRE(create_process(0));
    stale = context.process;
    ++stale.generation;
    save_expected();
    REQUIRE(micros_frame_ownership_runtime_release_process(&context.objects, stale) == MICROS_KERNEL_OBJECT_ERROR_STALE);
    REQUIRE(micros_frame_ownership_runtime_release_process(
        &context.expected_objects, context.process
    ) == MICROS_KERNEL_OBJECT_ERROR_ARGUMENT);
    REQUIRE(state_matches_expected());
    REQUIRE(allocate_setup(process_owner(MICROS_FRAME_OWNER_PROCESS_USER), 0));
    wrong = process_owner(MICROS_FRAME_OWNER_PROCESS_USER);
    ++wrong.generation;
    save_expected();
    REQUIRE(micros_frame_ownership_release(ledger(), wrong, TEST_BASE) == MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER);
    wrong.generation = context.process.generation;
    ++wrong.slot;
    REQUIRE(micros_frame_ownership_release(ledger(), wrong, TEST_BASE) == MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER);
    REQUIRE(state_matches_expected());
    return true;
}

static bool whole_active_counts_guard(void)
{
    REQUIRE(initialize(65, false));
    REQUIRE(create_process(0));
    REQUIRE(allocate_setup(process_owner(MICROS_FRAME_OWNER_PROCESS_USER), 0));
    ledger()->owner_counts[MICROS_FRAME_OWNER_PROCESS_USER] = 0;
    ledger()->owner_counts[MICROS_FRAME_OWNER_KERNEL_RETAINED] = 1;
    context.bitmap[0] = 1;
    save_expected();
    REQUIRE(stage_selected_input(
        ledger(), context.bitmap, MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
    ) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(storage_matches_expected());
    ledger()->handoff_targets[0] = MICROS_FRAME_HANDOFF_VM_WIRED;
    save_expected();
    REQUIRE(micros_frame_ownership_complete_handoff(ledger()) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(storage_matches_expected());
    return true;
}

static bool common_guards(void)
{
    REQUIRE(diagnostic_guards());
    REQUIRE(argument_guards());
    report_guard("arguments");
    REQUIRE(exhaustion_guard());
    report_guard("exhaustion");
    REQUIRE(rollback_guard());
    report_guard("cursor-rollback");
    REQUIRE(empty_bootstrap_guard());
    report_guard("empty-bootstrap-release");
    REQUIRE(empty_transferable_guard());
    report_guard("empty-transferable-release");
    REQUIRE(empty_phase_guard());
    report_guard("empty-wrong-phase");
    REQUIRE(selection_guard());
    report_guard("selection-bounds");
    REQUIRE(malformed_vm_plan(0));
    report_guard("duplicate-vm-data");
    REQUIRE(malformed_vm_plan(1));
    report_guard("overlapping-vm-tables");
    REQUIRE(malformed_vm_plan(2));
    report_guard("duplicate-vm-tables");
    REQUIRE(malformed_vm_plan(3));
    report_guard("vm-wrong-owner");
    REQUIRE(process_identity_guard());
    report_guard("stale-and-foreign-process");
    REQUIRE(whole_active_counts_guard());
    report_guard("whole-active-counts");
    return true;
}

#if !defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
static bool selected_field_guards(void)
{
    unsigned mutation;

    for (mutation = 0; mutation < 8; ++mutation) {
        struct micros_frame_owner owner;

        REQUIRE(initialize(65, false));
        if (mutation < 6) {
            REQUIRE(corrupt_record(mutation));
        } else if (mutation == 6) {
            allocator()->allocation_cursor = 1;
        } else {
            ledger()->phase = MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF;
            ledger()->owners[0] = process_owner(MICROS_FRAME_OWNER_PROCESS_USER);
            allocator()->allocated_bitmap[0] = 1;
            --allocator()->free_frame_count;
            ledger()->owned_frame_count = 1;
            ledger()->owner_counts[MICROS_FRAME_OWNER_KERNEL_RETAINED] = 1;
        }
        save_expected();
        memset(&owner, 0xa5, sizeof(owner));
        REQUIRE(micros_frame_ownership_validate_metadata(ledger()) == MICROS_FRAME_OWNERSHIP_OK);
        REQUIRE(micros_frame_ownership_selected_record(ledger(), 0, &owner) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
        REQUIRE(micros_frame_ownership_lookup(ledger(), TEST_BASE, &owner) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
        REQUIRE(buffer_is_byte(&owner, sizeof(owner), 0xa5));
        REQUIRE(storage_matches_expected());
        printf("MICROS_MEM03_GUARD selected-fields-%u PASS\n", mutation);
    }
    return true;
}

static bool selected_bounds_guard(void)
{
    struct micros_frame_owner owner;

    REQUIRE(initialize(65, false));
    save_expected();
    memset(&owner, 0xa5, sizeof(owner));
    REQUIRE(micros_frame_ownership_selected_record(NULL, 0, &owner) == MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT);
    REQUIRE(micros_frame_ownership_selected_record(ledger(), 0, NULL) == MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT);
    REQUIRE(micros_frame_ownership_selected_record(ledger(), 65, &owner) == MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED);
    REQUIRE(micros_frame_ownership_selected_record(ledger(), UINT64_MAX, &owner) == MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED);
    REQUIRE(buffer_is_byte(&owner, sizeof(owner), 0xa5));
    REQUIRE(state_matches_expected());
    ledger()->initialization_magic = 0;
    save_expected();
    REQUIRE(micros_frame_ownership_validate_metadata(ledger()) == MICROS_FRAME_OWNERSHIP_ERROR_NOT_INITIALIZED);
    REQUIRE(micros_frame_ownership_selected_record(ledger(), 0, &owner) == MICROS_FRAME_OWNERSHIP_ERROR_NOT_INITIALIZED);
    REQUIRE(buffer_is_byte(&owner, sizeof(owner), 0xa5));
    REQUIRE(storage_matches_expected());
    REQUIRE(micros_frame_ownership_validate_metadata(NULL) == MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT);
    REQUIRE(micros_frame_ownership_validate_active(NULL) == MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT);
    return true;
}

static bool malformed_metadata_guards(void)
{
    unsigned mutation;

    for (mutation = 16; mutation < 29; ++mutation) {
        struct micros_frame_owner owner;

        REQUIRE(initialize(65, false));
        REQUIRE(corrupt_metadata(mutation));
        save_expected();
        memset(&owner, 0xa5, sizeof(owner));
        REQUIRE(micros_frame_ownership_validate_metadata(ledger()) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
        REQUIRE(micros_frame_ownership_validate_active(ledger()) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
        REQUIRE(micros_frame_ownership_lookup(ledger(), TEST_BASE, &owner) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
        REQUIRE(buffer_is_byte(&owner, sizeof(owner), 0xa5));
        REQUIRE(storage_matches_expected());
    }
    REQUIRE(initialize(65, false));
    ledger()->owner_counts[MICROS_FRAME_OWNER_KERNEL_RETAINED] = UINT64_MAX;
    save_expected();
    REQUIRE(micros_frame_ownership_validate_metadata(ledger()) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(micros_frame_ownership_allocate(
        ledger(), kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY), &context.output
    ) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(release_selected_input(
        ledger(), context.process, context.bitmap, MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
    ) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(storage_matches_expected());
    REQUIRE(initialize(65, false));
    REQUIRE(micros_frame_ownership_complete_handoff(ledger()) == MICROS_FRAME_OWNERSHIP_OK);
    ledger()->owner_counts[MICROS_FRAME_OWNER_FREE] = 1;
    save_expected();
    {
        struct micros_frame_vm_transferable_release_plan plan;

        memset(&plan, 0xa5, sizeof(plan));
        REQUIRE(micros_frame_ownership_vm_release_transferable_preflight(
            ledger(), NULL, 0, &plan
        ) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
        REQUIRE(buffer_is_byte(&plan, sizeof(plan), 0xa5));
    }
    REQUIRE(storage_matches_expected());
    return true;
}

static bool unselected_active_guard(void)
{
    struct micros_frame_owner owner;

    REQUIRE(initialize(65, false));
    ledger()->owners[1].reserved = 1;
    save_expected();
    REQUIRE(micros_frame_ownership_lookup(ledger(), TEST_BASE, &owner) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(buffer_is_byte(&owner, sizeof(owner), 0));
    memset(&owner, 0xa5, sizeof(owner));
    REQUIRE(micros_frame_ownership_lookup(ledger(), frame_address(1), &owner) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(buffer_is_byte(&owner, sizeof(owner), 0xa5));
    REQUIRE(storage_matches_expected());
    REQUIRE(micros_frame_ownership_validate(ledger()) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    expected_owner(0, kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY));
    context.expected_allocator.allocation_cursor = 1;
    context.expected_output = TEST_BASE;
    REQUIRE(micros_frame_ownership_allocate(
        ledger(), kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY), &context.output
    ) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(storage_matches_expected());
    return true;
}

static bool inactive_capacity_guard(void)
{
    struct micros_frame_owner owner;

    REQUIRE(initialize(65, false));
    REQUIRE(corrupt_record(9));
    REQUIRE(corrupt_record(10));
    REQUIRE(corrupt_record(12));
    REQUIRE(corrupt_record(13));
    REQUIRE(corrupt_record(14));
    save_expected();
    REQUIRE(micros_frame_ownership_validate_metadata(ledger()) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(micros_frame_ownership_validate_active(ledger()) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(micros_frame_ownership_lookup(ledger(), TEST_BASE, &owner) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(buffer_is_byte(&owner, sizeof(owner), 0));
    REQUIRE(micros_frame_ownership_validate(ledger()) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(storage_matches_expected());
    expected_owner(0, kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY));
    context.expected_allocator.allocation_cursor = 1;
    context.expected_output = TEST_BASE;
    REQUIRE(micros_frame_ownership_allocate(
        ledger(), kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY), &context.output
    ) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(storage_matches_expected());
    save_expected();
    expected_owner(0, kernel_owner(MICROS_FRAME_OWNER_FREE));
    REQUIRE(micros_frame_ownership_release(
        ledger(), kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY), TEST_BASE
    ) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(storage_matches_expected());
    return true;
}

static bool runtime_metadata_guard(void)
{
    REQUIRE(initialize(65, false));
    save_expected();
    REQUIRE(micros_frame_ownership_runtime_validate_metadata(&context.objects) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(micros_frame_ownership_runtime_validate_metadata(NULL) == MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT);
    REQUIRE(micros_frame_ownership_runtime_validate_metadata(
        &context.expected_objects
    ) == MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT);
    REQUIRE(storage_matches_expected());
    context.irq = true;
    REQUIRE(micros_frame_ownership_runtime_validate_metadata(NULL) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(storage_matches_expected());
    context.irq = false;
    ledger()->phase = 0;
    save_expected();
    REQUIRE(micros_frame_ownership_runtime_validate_metadata(NULL) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(storage_matches_expected());
    micros_selected_native_reset();
    save_expected();
    context.irq = true;
    REQUIRE(micros_frame_ownership_runtime_validate_metadata(NULL) == MICROS_FRAME_OWNERSHIP_ERROR_NOT_INITIALIZED);
    REQUIRE(storage_matches_expected());
    return true;
}

static bool selected_mutation_guard(void)
{
    struct micros_frame_vm_claim_plan plan;

    REQUIRE(initialize(65, false));
    REQUIRE(create_process(0));
    REQUIRE(allocate_setup(process_owner(MICROS_FRAME_OWNER_PROCESS_USER), 0));
    ledger()->handoff_targets[0] = MICROS_FRAME_HANDOFF_TARGET_COUNT;
    context.bitmap[0] = 1;
    save_expected();
    REQUIRE(micros_frame_ownership_release(
        ledger(), process_owner(MICROS_FRAME_OWNER_PROCESS_USER), TEST_BASE
    ) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(micros_frame_ownership_prepare_handoff(
        ledger(), TEST_BASE, process_owner(MICROS_FRAME_OWNER_PROCESS_USER), MICROS_FRAME_HANDOFF_VM_WIRED
    ) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(release_selected_input(
        ledger(), context.process, context.bitmap, MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
    ) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(storage_matches_expected());
    REQUIRE(initialize(65, false));
    REQUIRE(micros_frame_ownership_complete_handoff(ledger()) == MICROS_FRAME_OWNERSHIP_OK);
    ledger()->owners[0].reserved = 1;
    save_expected();
    memset(&plan, 0xa5, sizeof(plan));
    REQUIRE(micros_frame_ownership_vm_claim_preflight(ledger(), TEST_BASE, &plan) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(buffer_is_byte(&plan, sizeof(plan), 0xa5));
    REQUIRE(storage_matches_expected());
    return true;
}

static bool selected_count_guard(void)
{
    struct micros_process_handle foreign;

    REQUIRE(initialize(65, false));
    REQUIRE(create_process(0));
    REQUIRE(allocate_setup(process_owner(MICROS_FRAME_OWNER_PROCESS_USER), 0));
    ledger()->handoff_targets[0] = MICROS_FRAME_HANDOFF_TARGET_COUNT;
    save_expected();
    REQUIRE(micros_frame_ownership_count_process(
        ledger(), context.process, &context.output
    ) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(storage_matches_expected());
    foreign = context.process;
    ++foreign.generation;
    REQUIRE(micros_frame_ownership_count_process(ledger(), foreign, &context.output) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(storage_matches_expected());
    return true;
}

static bool vm_set_atomicity_guard(void)
{
    struct micros_frame_vm_freeze_plan freeze;
    struct micros_frame_vm_transferable_release_plan release;
    struct micros_frame_vm_process_release_plan process_release;

    REQUIRE(prepare_set(65, 2, OP_FREEZE));
    allocator()->allocated_bitmap[0] |= UINT64_C(1) << 2;
    save_expected();
    memset(&freeze, 0xa5, sizeof(freeze));
    REQUIRE(micros_frame_ownership_vm_freeze_preflight(
        ledger(), context.process, context.data, 2, context.tables, &freeze
    ) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(buffer_is_byte(&freeze, sizeof(freeze), 0xa5));
    REQUIRE(storage_matches_expected());
    REQUIRE(prepare_set(65, 2, OP_FREEZE));
    ledger()->handoff_targets[1] = MICROS_FRAME_HANDOFF_VM_WIRED;
    save_expected();
    REQUIRE(micros_frame_ownership_vm_freeze_preflight(
        ledger(), context.process, context.data, 2, context.tables, &freeze
    ) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(buffer_is_byte(&freeze, sizeof(freeze), 0xa5));
    REQUIRE(storage_matches_expected());
    REQUIRE(prepare_set(65, 2, OP_RELEASE_TRANSFERABLE));
    ledger()->owners[1].reserved = 1;
    save_expected();
    memset(&release, 0xa5, sizeof(release));
    REQUIRE(micros_frame_ownership_vm_release_transferable_preflight(
        ledger(), context.data, 2, &release
    ) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    REQUIRE(buffer_is_byte(&release, sizeof(release), 0xa5));
    REQUIRE(storage_matches_expected());
    REQUIRE(prepare_set(65, 2, OP_RELEASE_PROCESS));
    ++ledger()->owners[2].generation;
    save_expected();
    memset(&process_release, 0xa5, sizeof(process_release));
    REQUIRE(micros_frame_ownership_vm_release_process_preflight(
        ledger(), context.process, context.data, 2, context.tables, &process_release
    ) == MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER);
    REQUIRE(buffer_is_byte(&process_release, sizeof(process_release), 0xa5));
    REQUIRE(storage_matches_expected());
    return true;
}

static bool current_guards(void)
{
    REQUIRE(selected_field_guards());
    REQUIRE(selected_bounds_guard());
    report_guard("selected-bounds");
    REQUIRE(malformed_metadata_guards());
    report_guard("metadata-errors");
    REQUIRE(unselected_active_guard());
    report_guard("unselected-active");
    REQUIRE(inactive_capacity_guard());
    report_guard("inactive-capacity");
    REQUIRE(runtime_metadata_guard());
    report_guard("runtime-metadata-authority");
    REQUIRE(selected_mutation_guard());
    report_guard("selected-mutation-preflights");
    REQUIRE(selected_count_guard());
    report_guard("selected-process-count");
    REQUIRE(vm_set_atomicity_guard());
    report_guard("vm-set-failure-atomicity");
    return true;
}
#endif

#if defined(MICROS_SELECTED_OWNERSHIP_LAUNCHER)
static int launch_native_products(int argc, char **argv)
{
    char *arguments[19];
    const char *launcher = getenv("__PYVENV_LAUNCHER__");
    size_t count = 0;
    int index;

    if (argc != 6) {
        return 2;
    }
    if (launcher != NULL && launcher[0] != '\0') {
        fputs("MEM03 native orchestration: __PYVENV_LAUNCHER__ is unsupported\n", stderr);
        return 2;
    }
    arguments[count++] = MICROS_SELECTED_OWNERSHIP_PYTHON;
    arguments[count++] = "-E";
    arguments[count++] = "-S";
    arguments[count++] = "-B";
    arguments[count++] = MICROS_SELECTED_OWNERSHIP_DRIVER;
    arguments[count++] = "--native";
    for (index = 1; index < argc; ++index) {
        arguments[count++] = "--executable";
        arguments[count++] = argv[index];
    }
    arguments[count++] = "--executable";
    arguments[count++] = argv[0];
    arguments[count] = NULL;
    if (chdir(MICROS_SELECTED_OWNERSHIP_ROOT) == 0) {
        execv(MICROS_SELECTED_OWNERSHIP_PYTHON, arguments);
    }
    perror("MEM03 native orchestration");
    return 2;
}
#endif

int main(int argc, char **argv)
{
    static const char *const channels[] = {
        "aggregate", "active-frame", "inactive-frame", "active-geometry", "inactive-geometry",
    };
    size_t index;

#if defined(MICROS_SELECTED_OWNERSHIP_LAUNCHER)
    if (argc == 6) {
        return launch_native_products(argc, argv);
    }
#endif
    if (argc != 3 || (strcmp(argv[1], "red") != 0 && strcmp(argv[1], "green") != 0)) {
        return 2;
    }
#if defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
    if (strcmp(argv[1], "red") != 0) {
        return 2;
    }
#else
    if (strcmp(argv[1], "green") != 0) {
        return 2;
    }
#endif
    for (index = 0; index < 5; ++index) {
        if (strcmp(argv[2], channels[index]) == 0) {
            break;
        }
    }
#if defined(MICROS_TEST_WORK_COUNTS)
    if (index == 5) {
        return 2;
    }
#else
    if (strcmp(argv[2], "ordinary") != 0) {
        return 2;
    }
#endif
    context.producer = TEST_PRODUCER + index + 1
        + (strcmp(argv[1], "green") == 0 ? UINT64_C(0x100) : 0);
    if (!all_cases() || !common_guards()) {
        return 2;
    }
#if !defined(MICROS_SELECTED_OWNERSHIP_BASELINE)
    if (!current_guards()) {
        return 2;
    }
#endif
    printf("MICROS_MEM03_SEMANTICS_PASS cases=99\n");
    return 0;
}
