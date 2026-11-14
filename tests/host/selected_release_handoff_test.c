#define _DEFAULT_SOURCE 1

#include "selected_release_handoff_test.h"

#include <inttypes.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "arch/riscv64/mmu.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/memory/frame_ownership_internal.h"
#include "kernel/memory/user_address_space_internal.h"
#include "kernel/objects/kernel_object_runtime_internal.h"
#include "kernel/tty/tty_handoff_runtime_internal.h"
#include "kernel/vm/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"

#if defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE) && MICROS_SELECTED_RELEASE_HANDOFF_BASELINE != 1
#error "MEM05 baseline must be explicitly equal to 1"
#endif
#if defined(MICROS_TEST_WORK_COUNTS)
#if !defined(MICROS_MEM05_NATIVE_CHANNEL) || MICROS_MEM05_NATIVE_CHANNEL < 0 || MICROS_MEM05_NATIVE_CHANNEL > 3
#error "MEM05 observations require one declared native channel"
#endif
#endif

#define REQUIRE(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return false; \
        } \
    } while (false)

#define TEST_PRODUCER UINT64_C(0x4d454d3035000000)
#define TEST_OWNER_MASK UINT32_C(11900)
#define ARENA_BYTES \
    (((size_t)MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES + 16) * MICROS_FRAME_SIZE)
#define SNAPSHOT_PAGES (MICROS_SV39_TABLE_ENTRY_COUNT + 6)

struct release_handoff_case {
    const char *operation;
    const char *pattern;
    uint64_t frames;
    size_t ranges;
    uint64_t selected;
    uint64_t words;
    uint64_t indexed;
    uint64_t owned;
    uint64_t tables;
    uint64_t roots;
    uint64_t rootless;
};

struct release_handoff_context {
    unsigned char *arena;
    uintptr_t status;
    uint64_t satp;
    uint64_t satp_writes;
    uint64_t capture;
    uint64_t producer;
    uint64_t next_ordinal;
    struct micros_physical_range memory[8];
    struct micros_frame_allocator expected_allocator;
    struct micros_frame_ownership expected_ownership;
    struct micros_kernel_objects expected_objects;
    struct micros_endpoint_registry expected_endpoints;
    struct micros_vm_handoff_state expected_vm;
    struct tty_handoff_context expected_tty;
    struct micros_kernel_address_space_report expected_kernel;
    bool expected_kernel_ready;
    bool expected_scratch_busy;
    uintptr_t expected_status;
    uint64_t expected_satp;
    uint64_t expected_satp_writes;
    uint64_t scratch_before[2][MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
    uint64_t page_addresses[SNAPSHOT_PAGES];
    unsigned char expected_pages[SNAPSHOT_PAGES][MICROS_SV39_PAGE_SIZE];
    size_t page_count;
    struct micros_process_handle processes[MICROS_PROCESS_CAPACITY];
    _Alignas(16) unsigned char thread_stacks[MICROS_PROCESS_CAPACITY][MICROS_THREAD_KERNEL_STACK_SIZE];
    uint64_t bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
    uint64_t ordinals[MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES];
#if !defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
    uint32_t ordered_frames[MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES];
    struct micros_frame_selection selection;
    struct micros_frame_selection expected_selection;
    struct micros_frame_selection expected_scratch[2];
    uint8_t expected_scratch_mode;
    size_t guard_count;
    jmp_buf panic_target;
    const char *expected_panic;
    const char *observed_panic;
    struct micros_user_address_space_scratch_lease lease;
    struct micros_user_address_space_scratch_lease expected_lease;
#endif
#if defined(MICROS_TEST_WORK_COUNTS)
    struct micros_test_work_snapshot observation;
#endif
};

static struct release_handoff_context context;

uintptr_t micros_as_native_irq_save(void)
{
    uintptr_t saved = context.status;

    context.status &= ~(uintptr_t)MICROS_RISCV_SSTATUS_SIE;
    return saved;
}

void micros_as_native_irq_restore(uintptr_t status)
{
    context.status = (context.status & ~(uintptr_t)MICROS_RISCV_SSTATUS_SIE)
        | (status & MICROS_RISCV_SSTATUS_SIE);
}

bool micros_as_native_irq_enabled(void)
{
    return (context.status & MICROS_RISCV_SSTATUS_SIE) != 0;
}

uint64_t micros_as_native_read_satp(void)
{
    return context.satp;
}

uint64_t micros_riscv_activate_sv39(uint64_t root_physical_address)
{
    ++context.satp_writes;
    context.satp = MICROS_RISCV_SATP_MODE_SV39 | (root_physical_address >> 12);
    return context.satp;
}

void micros_riscv_publish_page_table(void)
{
}

void micros_riscv_publish_page_table_address(uint64_t virtual_address)
{
    (void)virtual_address;
}

_Noreturn void micros_panic_entry(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line
)
{
#if !defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
    if (context.expected_panic != NULL && strcmp(reason, context.expected_panic) == 0) {
        context.observed_panic = reason;
        longjmp(context.panic_target, 1);
    }
#endif
    fprintf(stderr, "unexpected MEM05 native panic hart=%" PRIuPTR " %s at %s:%" PRIu32 "\n",
            hart_id, reason, file, line);
    exit(3);
}

_Noreturn void micros_trap_panic_entry(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line,
    const struct micros_trap_frame *frame
)
{
    (void)frame;
    micros_panic_entry(hart_id, reason, file, line);
}

static bool process_owner(struct micros_frame_owner owner)
{
    return owner.kind == MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
        || owner.kind == MICROS_FRAME_OWNER_PROCESS_USER
        || owner.kind == MICROS_FRAME_OWNER_VM_WIRED;
}

#if !defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
static int compare_ordinals(const void *left, const void *right)
{
    uint64_t a = context.ordinals[*(const uint32_t *)left];
    uint64_t b = context.ordinals[*(const uint32_t *)right];

    return a > b ? -1 : a < b ? 1 : 0;
}
#endif

static void model_index(struct micros_frame_ownership *ownership)
{
#if !defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
    uint32_t previous[MICROS_PROCESS_CAPACITY] = {0};
    size_t count = 0;
    uint64_t index;

    memset(ownership->owner_links, 0, sizeof(ownership->owner_links));
    memset(ownership->process_frames, 0, sizeof(ownership->process_frames));
    for (index = 0; index < ownership->managed_frame_count; ++index) {
        if (process_owner(ownership->owners[index])) {
            context.ordered_frames[count++] = (uint32_t)index;
        }
    }
    qsort(context.ordered_frames, count, sizeof(context.ordered_frames[0]), compare_ordinals);
    for (index = 0; index < count; ++index) {
        uint32_t frame = context.ordered_frames[index];
        uint16_t slot = ownership->owners[frame].slot;
        uint32_t encoded = frame + 1;

        ownership->owner_links[frame].previous = previous[slot];
        if (previous[slot] == 0) {
            ownership->process_frames[slot].head = encoded;
        } else {
            ownership->owner_links[previous[slot] - 1].next = encoded;
        }
        ++ownership->process_frames[slot].count;
        previous[slot] = encoded;
    }
#else
    (void)ownership;
#endif
}

static bool initialize_geometry(uint64_t frames, size_t ranges)
{
    struct micros_frame_allocator *allocator;
    size_t index;

    REQUIRE(frames != 0 && ranges != 0 && ranges <= 8 && frames % ranges == 0);
    context.status = 0;
    context.satp = 0;
    context.satp_writes = 0;
    context.next_ordinal = 0;
    context.page_count = 0;
    memset(context.processes, 0, sizeof(context.processes));
    memset(context.ordinals, 0, sizeof(context.ordinals));
    memset(context.bitmap, 0, sizeof(context.bitmap));
#if !defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
    memset(&context.selection, 0, sizeof(context.selection));
#endif
    micros_as_native_bootstrap_reset();
    micros_as_native_objects_reset();
    micros_as_native_kernel_reset();
    micros_as_native_ipc_reset();
    micros_as_native_vm_reset();
    micros_mem05_native_scratch_reset();
    memset(tty_handoff_context(), 0, sizeof(*tty_handoff_context()));
    micros_user_address_space_test_fail_after_allocations(SIZE_MAX);
    allocator = micros_as_native_allocator();
    for (index = 0; index < ranges; ++index) {
        context.memory[ranges - index - 1] = (struct micros_physical_range){
            (uint64_t)(uintptr_t)context.arena + index * (frames / ranges + 1) * MICROS_FRAME_SIZE,
            frames / ranges * MICROS_FRAME_SIZE,
        };
    }
    REQUIRE(micros_frame_allocator_initialize(allocator, context.memory, ranges, NULL, 0) == MICROS_FRAME_ALLOCATOR_OK);
    REQUIRE(micros_frame_ownership_runtime_initialize() == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(micros_kernel_object_runtime_initialize(0) == MICROS_KERNEL_OBJECT_OK);
    return true;
}

static void model_owner(uint64_t index, struct micros_frame_owner owner)
{
    struct micros_frame_ownership *expected = &context.expected_ownership;
    struct micros_frame_allocator *allocator = &context.expected_allocator;
    struct micros_frame_owner before = expected->owners[index];
    uint64_t bit = UINT64_C(1) << (index % 64);

    if (before.kind == MICROS_FRAME_OWNER_FREE && owner.kind != MICROS_FRAME_OWNER_FREE) {
        allocator->allocated_bitmap[index / 64] |= bit;
        --allocator->free_frame_count;
        ++expected->owned_frame_count;
    } else if (before.kind != MICROS_FRAME_OWNER_FREE && owner.kind == MICROS_FRAME_OWNER_FREE) {
        allocator->allocated_bitmap[index / 64] &= ~bit;
        ++allocator->free_frame_count;
        --expected->owned_frame_count;
        if (index < allocator->allocation_cursor) {
            allocator->allocation_cursor = index;
        }
    }
    if (before.kind != MICROS_FRAME_OWNER_FREE) {
        --expected->owner_counts[before.kind];
    }
    if (owner.kind != MICROS_FRAME_OWNER_FREE) {
        ++expected->owner_counts[owner.kind];
    }
    if (!process_owner(owner)) {
        context.ordinals[index] = 0;
    } else if (!process_owner(before) || before.slot != owner.slot || before.generation != owner.generation) {
        context.ordinals[index] = ++context.next_ordinal;
    }
    expected->owners[index] = owner;
    expected->handoff_targets[index] = MICROS_FRAME_HANDOFF_NONE;
}

static bool physical_address(uint64_t index, uint64_t *address)
{
    const struct micros_frame_ownership *ownership = micros_as_native_ownership();
    size_t range;

    for (range = 0; range < ownership->managed_range_count; ++range) {
        const struct micros_frame_range_snapshot *entry = &ownership->managed_ranges[range];

        if (index >= entry->bitmap_offset && index - entry->bitmap_offset < entry->frame_count) {
            *address = entry->base + (index - entry->bitmap_offset) * MICROS_FRAME_SIZE;
            return true;
        }
    }
    return false;
}

static bool input_frame_index(uint64_t address, uint64_t *index)
{
    const struct micros_frame_ownership *ownership = micros_as_native_ownership();
    size_t range;

    for (range = 0; range < ownership->managed_range_count; ++range) {
        const struct micros_frame_range_snapshot *entry = &ownership->managed_ranges[range];
        uint64_t offset;

        if (address < entry->base) {
            continue;
        }
        offset = address - entry->base;
        if (offset % MICROS_FRAME_SIZE == 0 && offset / MICROS_FRAME_SIZE < entry->frame_count) {
            *index = entry->bitmap_offset + offset / MICROS_FRAME_SIZE;
            return true;
        }
    }
    return false;
}

static bool snapshot_expected(bool pages)
{
    const struct micros_kernel_address_space_report *kernel = micros_kernel_address_space_report();
    uint64_t index;

    context.expected_allocator = *micros_as_native_allocator();
    context.expected_ownership = *micros_as_native_ownership();
    context.expected_objects = *micros_kernel_object_runtime_authoritative_registry();
    context.expected_endpoints = *micros_as_native_ipc_input();
    context.expected_vm = *micros_as_native_vm_input();
    context.expected_tty = *tty_handoff_context();
    context.expected_kernel_ready = kernel != NULL;
    if (kernel != NULL) {
        context.expected_kernel = *kernel;
    }
    context.expected_status = context.status;
    context.expected_satp = context.satp;
    context.expected_satp_writes = context.satp_writes;
    context.expected_scratch_busy = micros_as_native_scratch_busy();
    for (index = 0; index < 2; ++index) {
        memcpy(context.scratch_before[index], micros_as_native_scratch_bitmap(index != 0),
               sizeof(context.scratch_before[index]));
#if !defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
        context.expected_scratch[index] = *micros_mem05_native_scratch_selection(index != 0);
#endif
    }
#if !defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
    context.expected_scratch_mode = micros_mem05_native_scratch_mode();
#endif
    context.page_count = 0;
    if (pages) {
        for (index = 0; index < context.expected_ownership.managed_frame_count; ++index) {
            struct micros_frame_owner owner = context.expected_ownership.owners[index];
            uint64_t address;

            if (owner.kind == MICROS_FRAME_OWNER_FREE) {
                continue;
            }
            REQUIRE(context.page_count < SNAPSHOT_PAGES && physical_address(index, &address));
            context.page_addresses[context.page_count] = address;
            memcpy(context.expected_pages[context.page_count], (const void *)(uintptr_t)address, MICROS_FRAME_SIZE);
            ++context.page_count;
            if (process_owner(owner)) {
                context.ordinals[index] = index + 1;
            }
        }
        context.next_ordinal = context.expected_ownership.managed_frame_count;
    }
    return true;
}

static bool snapshot_state_matches(void)
{
    const struct micros_kernel_address_space_report *kernel = micros_kernel_address_space_report();
    size_t index;

    REQUIRE(memcmp(micros_as_native_allocator(), &context.expected_allocator, sizeof(context.expected_allocator)) == 0);
    REQUIRE(memcmp(micros_as_native_ownership(), &context.expected_ownership, sizeof(context.expected_ownership)) == 0);
    REQUIRE(memcmp(micros_kernel_object_runtime_authoritative_registry(),
                   &context.expected_objects, sizeof(context.expected_objects)) == 0);
    REQUIRE(memcmp(micros_as_native_ipc_input(), &context.expected_endpoints, sizeof(context.expected_endpoints)) == 0);
    REQUIRE(memcmp(micros_as_native_vm_input(), &context.expected_vm, sizeof(context.expected_vm)) == 0);
    REQUIRE(memcmp(tty_handoff_context(), &context.expected_tty, sizeof(context.expected_tty)) == 0);
    REQUIRE((kernel != NULL) == context.expected_kernel_ready);
    REQUIRE(kernel == NULL || memcmp(kernel, &context.expected_kernel, sizeof(*kernel)) == 0);
    REQUIRE(context.status == context.expected_status && context.satp == context.expected_satp
            && context.satp_writes == context.expected_satp_writes);
    REQUIRE(micros_as_native_scratch_busy() == context.expected_scratch_busy);
    for (index = 0; index < 2; ++index) {
        REQUIRE(memcmp(context.scratch_before[index], micros_as_native_scratch_bitmap(index != 0),
                       sizeof(context.scratch_before[index])) == 0);
#if !defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
        REQUIRE(memcmp(&context.expected_scratch[index], micros_mem05_native_scratch_selection(index != 0),
                       sizeof(context.expected_scratch[index])) == 0);
#endif
    }
#if !defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
    REQUIRE(context.expected_scratch_mode == micros_mem05_native_scratch_mode());
#endif
    for (index = 0; index < context.page_count; ++index) {
        REQUIRE(memcmp(context.expected_pages[index], (const void *)(uintptr_t)context.page_addresses[index],
                       MICROS_FRAME_SIZE) == 0);
    }
    return true;
}

static bool expected_state_matches(void)
{
    model_index(&context.expected_ownership);
    REQUIRE(snapshot_state_matches());
    REQUIRE(micros_frame_ownership_validate(micros_as_native_ownership()) == MICROS_FRAME_OWNERSHIP_OK);
    return true;
}

static bool begin_capture(void)
{
    ++context.capture;
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(micros_test_work_begin(context.producer, context.capture, TEST_OWNER_MASK) == MICROS_TEST_WORK_OK);
#endif
    return true;
}

static bool end_capture(void)
{
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(micros_test_work_finish(&context.observation) == MICROS_TEST_WORK_OK);
#endif
    return true;
}

static void report_case(
    const struct release_handoff_case *test,
    int result,
    uint64_t output
)
{
    printf(
        "MICROS_MEM05_CASE {\"case\":%" PRIu64 ",\"operation\":\"%s\",\"frames\":%" PRIu64
        ",\"ranges\":%zu,\"pattern\":\"%s\",\"selected\":%" PRIu64 ",\"words\":%" PRIu64
        ",\"indexed\":%" PRIu64 ",\"owned\":%" PRIu64 ",\"tables\":%" PRIu64
        ",\"roots\":%" PRIu64 ",\"rootless\":%" PRIu64 ",\"result\":%d,\"output\":%" PRIu64
        ",\"state\":true}\n",
        context.capture, test->operation, test->frames, test->ranges, test->pattern,
        test->selected, test->words, test->indexed, test->owned, test->tables,
        test->roots, test->rootless, result, output
    );
#if defined(MICROS_TEST_WORK_COUNTS)
    printf(
        "MICROS_MEM05_COUNTS {\"version\":1,\"reserved0\":0,\"producer_label\":%" PRIu64
        ",\"capture_label\":%" PRIu64 ",\"owner_mask\":%" PRIu32 ",\"reserved1\":0,\"counts\":[",
        context.observation.producer_label, context.observation.capture_label, context.observation.owner_mask
    );
    for (unsigned int owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        printf("%s[", owner ? "," : "");
        for (unsigned int kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            printf("%s%" PRIu64, kind ? "," : "", context.observation.counts[owner][kind]);
        }
        putchar(']');
    }
    puts("]}");
#endif
}

static bool selected_input(const char *pattern, uint64_t index, uint64_t frames)
{
    if (strcmp(pattern, "last") == 0) {
        return index == frames - 1;
    }
    if (strcmp(pattern, "boundary-three") == 0) {
        return index == 0 || index == 63 || index == 64;
    }
    if (strcmp(pattern, "boundary-four") == 0) {
        return index == 0 || index == 1 || index == 63 || index == 64;
    }
    if (strcmp(pattern, "every-word") == 0) {
        return index % 64 == 0;
    }
    return strcmp(pattern, "all") == 0;
}

static void construct_selection(void)
{
#if !defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
    size_t word;

    memset(&context.selection, 0, sizeof(context.selection));
    for (word = 0; word < MICROS_FRAME_ALLOCATOR_BITMAP_WORDS; ++word) {
        uint64_t bits = context.bitmap[word];

        if (bits == 0) {
            continue;
        }
        context.selection.bitmap[word] = bits;
        context.selection.words[context.selection.word_count] = (uint16_t)word;
        context.selection.positions[word] = (uint16_t)++context.selection.word_count;
        for (unsigned bit = 0; bit < 64; ++bit) {
            context.selection.frame_count += (bits >> bit) & 1;
        }
    }
#endif
}

static void fill_allocated_input(
    const struct release_handoff_case *test,
    struct micros_process_handle process,
    bool count_input
)
{
    struct micros_frame_allocator *allocator = micros_as_native_allocator();
    struct micros_frame_ownership *ownership = micros_as_native_ownership();
    uint64_t ordinal = 0;
    uint64_t index;

    allocator->free_frame_count = 0;
    allocator->allocation_cursor = test->frames;
    ownership->owned_frame_count = test->frames;
    ownership->owner_counts[MICROS_FRAME_OWNER_KERNEL_RETAINED] = test->frames;
    for (index = 0; index < test->frames; ++index) {
        struct micros_frame_owner owner = {.kind = MICROS_FRAME_OWNER_KERNEL_RETAINED};
        bool selected = !count_input && selected_input(test->pattern, index, test->frames);
        bool indexed = count_input && index < test->indexed;

        allocator->allocated_bitmap[index / 64] |= UINT64_C(1) << (index % 64);
        if (selected || indexed) {
            owner = (struct micros_frame_owner){
                .generation = process.generation,
                .slot = process.slot,
                .kind = selected && ordinal % 2 ? MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE : MICROS_FRAME_OWNER_PROCESS_USER,
            };
            if (indexed && (strstr(test->pattern, "foreign") != NULL
                    || (strstr(test->pattern, "mixed") != NULL && index % 2 != 0))) {
                ++owner.generation;
            }
            --ownership->owner_counts[MICROS_FRAME_OWNER_KERNEL_RETAINED];
            ++ownership->owner_counts[owner.kind];
            context.ordinals[index] = ++context.next_ordinal;
            ++ordinal;
        }
        ownership->owners[index] = owner;
        if (selected) {
            context.bitmap[index / 64] |= UINT64_C(1) << (index % 64);
        }
    }
    model_index(ownership);
    construct_selection();
}

static bool portable_release_case(const struct release_handoff_case *test)
{
    struct micros_process_handle process = {63, 2};
    enum micros_frame_ownership_error error;
    uint64_t index;

    REQUIRE(initialize_geometry(test->frames, test->ranges));
    fill_allocated_input(test, process, false);
    REQUIRE(micros_frame_ownership_validate(micros_as_native_ownership()) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(snapshot_expected(false));
    for (index = 0; index < test->frames; ++index) {
        if (selected_input(test->pattern, index, test->frames)) {
            model_owner(index, (struct micros_frame_owner){0});
        }
    }
    REQUIRE(begin_capture());
#if defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
    error = micros_frame_ownership_release_process_set(
        micros_as_native_ownership(), process, context.bitmap, MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
    );
#else
    error = micros_frame_ownership_release_process_set(micros_as_native_ownership(), process, &context.selection);
#endif
    REQUIRE(end_capture());
    REQUIRE(error == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(expected_state_matches());
    report_case(test, error, UINT64_MAX);
    return true;
}

static bool count_process_case(const struct release_handoff_case *test)
{
    struct micros_process_handle process = {63, 2};
    uint64_t count = UINT64_MAX;
    uint64_t expected = strstr(test->pattern, "foreign") ? 0
        : strstr(test->pattern, "mixed") ? test->indexed / 2 : test->indexed;
    enum micros_frame_ownership_error error;

    REQUIRE(initialize_geometry(test->frames, test->ranges));
    fill_allocated_input(test, process, true);
    REQUIRE(micros_frame_ownership_validate(micros_as_native_ownership()) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(snapshot_expected(false));
    REQUIRE(begin_capture());
    error = micros_frame_ownership_count_process(micros_as_native_ownership(), process, &count);
    REQUIRE(end_capture());
    REQUIRE(error == MICROS_FRAME_OWNERSHIP_OK && count == expected);
    REQUIRE(expected_state_matches());
    report_case(test, error, count);
    return true;
}

static bool runtime_release_case(const struct release_handoff_case *test)
{
    struct micros_kernel_objects *objects;
    struct micros_process_handle process;
    enum micros_kernel_object_error error;

    REQUIRE(initialize_geometry(test->frames, test->ranges));
    objects = micros_kernel_object_runtime_authoritative_registry();
    REQUIRE(micros_process_create_at(objects, 63, &process) == MICROS_KERNEL_OBJECT_OK);
    fill_allocated_input(test, process, true);
    REQUIRE(micros_frame_ownership_runtime_validate(objects) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(snapshot_expected(false));
    if (test->indexed == 0) {
        context.expected_objects.processes[process.slot].slot_state = MICROS_KERNEL_OBJECT_SLOT_FREE;
        --context.expected_objects.live_process_count;
    }
    REQUIRE(begin_capture());
    error = micros_frame_ownership_runtime_release_process(objects, process);
    REQUIRE(end_capture());
    REQUIRE(error == (test->indexed ? MICROS_KERNEL_OBJECT_ERROR_STATE : MICROS_KERNEL_OBJECT_OK));
    REQUIRE(expected_state_matches());
    report_case(test, error, UINT64_MAX);
    return true;
}

static bool initialize_kernel(const struct release_handoff_case *test)
{
    struct micros_kernel_address_space_report report = {0};
    struct micros_frame_owner owner = {.kind = MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE};

    REQUIRE(initialize_geometry(test->frames, test->ranges));
    REQUIRE(micros_frame_ownership_runtime_allocate(owner, &report.root_physical_address) == MICROS_FRAME_OWNERSHIP_OK);
    micros_user_address_space_clear_page(report.root_physical_address);
    micros_as_native_kernel_bind(&report);
    REQUIRE(micros_user_address_space_activate_kernel() == MICROS_USER_ADDRESS_SPACE_OK);
    return true;
}

static bool create_root(uint16_t slot, struct micros_process_handle *process)
{
    REQUIRE(micros_process_create_at(micros_kernel_object_runtime_authoritative_registry(), slot, process)
            == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_user_address_space_create(*process) == MICROS_USER_ADDRESS_SPACE_OK);
    return true;
}

static bool map_input_page(
    struct micros_process_handle process,
    uint64_t address,
    uint32_t permissions,
    uint64_t *physical
)
{
    REQUIRE(micros_user_address_space_allocate_page(process, address, permissions, physical) == MICROS_USER_ADDRESS_SPACE_OK);
    memset((void *)(uintptr_t)*physical, 0x5a, MICROS_FRAME_SIZE);
    return true;
}

static bool input_path(
    struct micros_process_handle process,
    uint64_t address,
    uint64_t tables[3],
    uint64_t **leaf
)
{
    const struct micros_process *record;
    struct micros_sv39_decoded_pte decoded;
    uint16_t index;
    unsigned int depth;

    REQUIRE(micros_process_resolve(micros_kernel_object_runtime_authoritative_registry(), process, &record)
            == MICROS_KERNEL_OBJECT_OK);
    tables[0] = record->address_space_root;
    for (depth = 0; depth < 2; ++depth) {
        const struct micros_user_address_space_page_table *table =
            (const struct micros_user_address_space_page_table *)(uintptr_t)tables[depth];

        REQUIRE(micros_sv39_vpn_index(address, 2 - depth, &index) == MICROS_SV39_OK);
        REQUIRE(micros_sv39_decode_pte(table->entries[index], &decoded) == MICROS_SV39_OK);
        REQUIRE(decoded.kind == MICROS_SV39_PTE_TABLE);
        tables[depth + 1] = decoded.physical_address;
    }
    REQUIRE(micros_sv39_vpn_index(address, 0, &index) == MICROS_SV39_OK);
    *leaf = &((struct micros_user_address_space_page_table *)(uintptr_t)tables[2])->entries[index];
    return true;
}

static bool expected_page_clear(uint64_t address)
{
    size_t index;

    for (index = 0; index < context.page_count; ++index) {
        if (context.page_addresses[index] == address) {
            memset(context.expected_pages[index], 0, MICROS_FRAME_SIZE);
            return true;
        }
    }
    return false;
}

static bool expected_pte_store(uint64_t *slot, uint64_t value)
{
    uintptr_t address = (uintptr_t)slot;
    size_t index;

    for (index = 0; index < context.page_count; ++index) {
        uintptr_t base = (uintptr_t)context.page_addresses[index];

        if (address >= base && address - base <= MICROS_FRAME_SIZE - sizeof(value)) {
            memcpy(context.expected_pages[index] + (address - base), &value, sizeof(value));
            return true;
        }
    }
    return false;
}

static bool expect_released_frame(uint64_t address, bool clear)
{
    uint64_t frame;

    REQUIRE(input_frame_index(address, &frame));
    model_owner(frame, (struct micros_frame_owner){0});
    REQUIRE(!clear || expected_page_clear(address));
    return true;
}

static bool prepare_point_case(
    const struct release_handoff_case *test,
    struct micros_process_handle *process,
    uint64_t *selected
)
{
    uint64_t ignored;
    bool keep = strncmp(test->pattern, "keep", 4) == 0;
    bool leaf = strncmp(test->pattern, "prune-leaf", 10) == 0;
    uint64_t other = MICROS_USER_VIRTUAL_BASE + (keep ? MICROS_FRAME_SIZE : UINT64_C(0x200000));
    uint32_t permissions = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;

    REQUIRE(initialize_kernel(test));
    REQUIRE(create_root(0, process));
    REQUIRE(map_input_page(*process, MICROS_USER_VIRTUAL_BASE, permissions, selected));
    if (keep || leaf) {
        REQUIRE(map_input_page(*process, other, permissions, &ignored));
    }
    if (strstr(test->pattern, "dense") != NULL) {
        for (size_t index = 1; index <= 32; ++index) {
            REQUIRE(map_input_page(*process, other + index * MICROS_FRAME_SIZE, permissions, &ignored));
        }
    }
    return true;
}

static bool page_release_case(const struct release_handoff_case *test)
{
    struct micros_process_handle process;
    uint64_t selected;
    uint64_t tables[3];
    uint64_t *leaf;
    uint64_t output = UINT64_MAX;
    uint64_t index;
    bool absent = strcmp(test->pattern, "absent") == 0;
    uint64_t address = MICROS_USER_VIRTUAL_BASE + (absent ? MICROS_FRAME_SIZE : 0);
    enum micros_user_address_space_error error;

    REQUIRE(prepare_point_case(test, &process, &selected));
    REQUIRE(input_path(process, address, tables, &leaf));
    REQUIRE(snapshot_expected(true));
    if (!absent) {
        REQUIRE(expect_released_frame(selected, false));
        REQUIRE(expected_pte_store(leaf, 0));
        if (test->selected >= 2) {
            REQUIRE(expect_released_frame(tables[2], true));
            REQUIRE(expected_pte_store(&((struct micros_user_address_space_page_table *)(uintptr_t)tables[1])->entries[0], 0));
        }
        if (test->selected == 3) {
            REQUIRE(expect_released_frame(tables[1], true));
            REQUIRE(expected_pte_store(&((struct micros_user_address_space_page_table *)(uintptr_t)tables[0])->entries[1], 0));
        }
    }
    REQUIRE(begin_capture());
    error = micros_user_address_space_release_page(process, address, &output);
    REQUIRE(end_capture());
    REQUIRE(error == (absent ? MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED : MICROS_USER_ADDRESS_SPACE_OK));
    REQUIRE(output == (absent ? UINT64_MAX : selected));
    REQUIRE(expected_state_matches());
    REQUIRE(absent || input_frame_index(output, &index));
    report_case(test, error, absent ? UINT64_MAX : index);
    return true;
}

static bool destruction_case(const struct release_handoff_case *test)
{
    struct micros_process_handle process;
    uint64_t leaf_count = test->owned - test->tables;
    uint64_t index;
    uint64_t ignored;
    enum micros_user_address_space_error error;
    uint32_t permissions = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;

    REQUIRE(initialize_kernel(test));
    REQUIRE(create_root(0, &process));
    for (index = 0; index < leaf_count; ++index) {
        uint64_t stride = strcmp(test->pattern, "sparse32") == 0 ? UINT64_C(0x200000) : MICROS_FRAME_SIZE;

        REQUIRE(map_input_page(process, MICROS_USER_VIRTUAL_BASE + index * stride, permissions, &ignored));
    }
    REQUIRE(snapshot_expected(true));
    for (index = 0; index < test->frames; ++index) {
        struct micros_frame_owner owner = context.expected_ownership.owners[index];
        uint64_t address;

        if (!process_owner(owner) || owner.slot != process.slot || owner.generation != process.generation) {
            continue;
        }
        REQUIRE(physical_address(index, &address));
        REQUIRE(expect_released_frame(address, true));
    }
    context.expected_objects.processes[process.slot].address_space_root = 0;
    REQUIRE(begin_capture());
    error = micros_user_address_space_destroy(process);
    REQUIRE(end_capture());
    REQUIRE(error == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(expected_state_matches());
    report_case(test, error, UINT64_MAX);
    return true;
}

static bool prepare_handoff_case(const struct release_handoff_case *test)
{
    struct micros_kernel_objects *objects;
    uint64_t index;
    uint64_t ignored;

    REQUIRE(initialize_kernel(test));
    objects = micros_kernel_object_runtime_authoritative_registry();
    for (index = 0; index < test->roots; ++index) {
        uint16_t slot = (uint16_t)(test->roots == 16 ? index * 4 : index);

        REQUIRE(create_root(slot, &context.processes[index]));
    }
    for (index = 0; index < test->rootless; ++index) {
        struct micros_process_handle empty;

        REQUIRE(micros_process_create_at(objects, (uint16_t)(test->roots + index), &empty) == MICROS_KERNEL_OBJECT_OK);
    }
    for (index = 0; index < test->roots; ++index) {
        REQUIRE(map_input_page(
            context.processes[index], MICROS_USER_VIRTUAL_BASE,
            MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE, &ignored
        ));
        REQUIRE(map_input_page(
            context.processes[index], MICROS_USER_VIRTUAL_BASE + MICROS_FRAME_SIZE,
            MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE, &ignored
        ));
    }
    for (index = test->roots; index != 0; --index) {
        struct micros_thread_handle thread;
        struct micros_user_context user = {
            .sepc = MICROS_USER_VIRTUAL_BASE,
            .sp = MICROS_USER_VIRTUAL_BASE + 2 * MICROS_FRAME_SIZE,
            .sstatus = (UINT64_C(2) << 32) | MICROS_RISCV_SSTATUS_SPIE,
        };
        unsigned char *stack = context.thread_stacks[index - 1];

        REQUIRE(micros_thread_create(objects, context.processes[index - 1], &thread) == MICROS_KERNEL_OBJECT_OK);
        REQUIRE(micros_thread_attach_execution_context(
            objects, thread, (uintptr_t)stack, (uintptr_t)(stack + MICROS_THREAD_KERNEL_STACK_SIZE), &user
        ) == MICROS_KERNEL_OBJECT_OK);
    }
    REQUIRE(micros_kernel_objects_validate(objects) == MICROS_KERNEL_OBJECT_OK);
    if (strcmp(test->operation, "complete-handoff") == 0) {
        REQUIRE(micros_user_address_space_prepare_wired_handoff() == MICROS_USER_ADDRESS_SPACE_OK);
    }
    return true;
}

static bool handoff_case(const struct release_handoff_case *test)
{
    bool complete = strcmp(test->operation, "complete-handoff") == 0;
    enum micros_user_address_space_error error;
    uint64_t index;

    REQUIRE(prepare_handoff_case(test));
    REQUIRE(snapshot_expected(true));
    for (index = 0; index < test->frames; ++index) {
        struct micros_frame_owner owner = context.expected_ownership.owners[index];

        if (owner.kind != MICROS_FRAME_OWNER_PROCESS_USER) {
            continue;
        }
        REQUIRE(context.expected_ownership.handoff_targets[index]
                == (complete ? MICROS_FRAME_HANDOFF_VM_WIRED : MICROS_FRAME_HANDOFF_NONE));
        if (complete) {
            owner.kind = MICROS_FRAME_OWNER_VM_WIRED;
            model_owner(index, owner);
        } else {
            context.expected_ownership.handoff_targets[index] = MICROS_FRAME_HANDOFF_VM_WIRED;
        }
    }
    if (complete) {
        context.expected_ownership.phase = MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF;
    }
    REQUIRE(begin_capture());
    error = complete ? micros_user_address_space_complete_wired_handoff()
        : micros_user_address_space_prepare_wired_handoff();
    REQUIRE(end_capture());
    REQUIRE(error == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(expected_state_matches());
    report_case(test, error, UINT64_MAX);
    return true;
}

static bool scope_case(const struct release_handoff_case *test)
{
    struct micros_process_handle process;
    uint64_t selected;
    uint64_t foreign;
    uint64_t tables[3];
    uint64_t *leaf;
    uint64_t output = UINT64_MAX;
    enum micros_user_address_space_error error;
    enum micros_user_address_space_error expected =
        strcmp(test->pattern, "malformed") == 0 ? MICROS_USER_ADDRESS_SPACE_ERROR_PTE
        : MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    uint32_t permissions = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;

    REQUIRE(initialize_kernel(test));
    REQUIRE(create_root(0, &process));
    REQUIRE(map_input_page(process, MICROS_USER_VIRTUAL_BASE, permissions, &selected));
    REQUIRE(input_path(process, MICROS_USER_VIRTUAL_BASE, tables, &leaf));
    if (strcmp(test->pattern, "foreign") == 0) {
        struct micros_process_handle other;

        REQUIRE(create_root(1, &other));
        REQUIRE(map_input_page(other, MICROS_USER_VIRTUAL_BASE, permissions, &foreign));
        REQUIRE(micros_sv39_make_leaf_pte(foreign, permissions | MICROS_SV39_PERMISSION_USER, leaf) == MICROS_SV39_OK);
    } else if (strcmp(test->pattern, "table") == 0) {
        REQUIRE(micros_sv39_make_leaf_pte(tables[0], permissions | MICROS_SV39_PERMISSION_USER, leaf) == MICROS_SV39_OK);
    } else if (strcmp(test->pattern, "malformed") == 0) {
        *leaf = (UINT64_C(1) << 63) | 1;
    } else if (strcmp(test->pattern, "absent-orphan") == 0) {
        *leaf = 0;
    } else {
        REQUIRE(strcmp(test->pattern, "alias") == 0 && leaf[1] == 0);
        leaf[1] = *leaf;
    }
    REQUIRE(snapshot_expected(true));
    REQUIRE(micros_user_address_space_validate(process) == expected);
    REQUIRE(expected_state_matches());
#if !defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
    if (strcmp(test->pattern, "absent-orphan") == 0) {
        expected = MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    } else if (strcmp(test->pattern, "alias") == 0) {
        expected = MICROS_USER_ADDRESS_SPACE_OK;
        REQUIRE(expect_released_frame(selected, false));
        REQUIRE(expected_pte_store(leaf, 0));
    }
#endif
    REQUIRE(begin_capture());
    error = micros_user_address_space_release_page(process, MICROS_USER_VIRTUAL_BASE, &output);
    REQUIRE(end_capture());
    REQUIRE(error == expected);
    REQUIRE(output == (expected == MICROS_USER_ADDRESS_SPACE_OK ? selected : UINT64_MAX));
    REQUIRE(expected_state_matches());
    if (output != UINT64_MAX) {
        uint64_t frame;

        REQUIRE(input_frame_index(output, &frame));
        output = frame;
    }
    report_case(test, error, output);
    return true;
}

static bool portable_release_cases(void)
{
    const uint64_t frames[] = {1, 63, 64, 65, 4096, 262144};
    struct release_handoff_case test = {.operation = "release-set", .ranges = 1};
    size_t index;

    for (index = 0; index < sizeof(frames) / sizeof(frames[0]); ++index) {
        test.frames = frames[index];
        test.pattern = "empty";
        test.selected = test.words = 0;
        REQUIRE(portable_release_case(&test));
        test.pattern = "last";
        test.selected = test.words = 1;
        REQUIRE(portable_release_case(&test));
    }
    for (index = 3; index < sizeof(frames) / sizeof(frames[0]); ++index) {
        test.frames = frames[index];
        test.words = 2;
        test.pattern = "boundary-three";
        test.selected = 3;
        REQUIRE(portable_release_case(&test));
        test.pattern = "boundary-four";
        test.selected = 4;
        REQUIRE(portable_release_case(&test));
    }
    test.pattern = "every-word";
    test.selected = test.words = 4096;
    REQUIRE(portable_release_case(&test));
    test.pattern = "all";
    test.selected = 262144;
    REQUIRE(portable_release_case(&test));
    return true;
}

static bool process_count_cases(void)
{
    const uint64_t frames[] = {65, 4096, 262144};
    const uint64_t lengths[] = {0, 1, 16, 16, 64, 64, 64};
    const char *const patterns[] = {
        "empty", "one", "sixteen", "sixteen-mixed", "sixty-four", "sixty-four-mixed", "sixty-four-foreign",
    };
    struct release_handoff_case test = {.operation = "count-process", .ranges = 1};
    size_t memory;

    for (memory = 0; memory < 3; ++memory) {
        test.frames = frames[memory];
        for (size_t index = 0; index < 7; ++index) {
            test.pattern = patterns[index];
            test.indexed = lengths[index];
            REQUIRE(count_process_case(&test));
        }
    }
    test.operation = "release-process";
    for (memory = 0; memory < 3; ++memory) {
        test.frames = frames[memory];
        test.pattern = "empty";
        test.indexed = 0;
        REQUIRE(runtime_release_case(&test));
        test.pattern = "owned";
        test.indexed = 1;
        REQUIRE(runtime_release_case(&test));
    }
    return true;
}

static bool page_release_cases(void)
{
    const uint64_t frames[] = {4096, 262144};
    const size_t ranges[] = {1, 8};
    const char *const patterns[] = {"keep", "keep-dense", "prune-leaf", "prune-leaf-dense", "prune-both", "absent"};
    const uint64_t selected[] = {1, 1, 2, 2, 3, 0};
    const uint64_t owned[] = {5, 37, 6, 38, 4, 4};
    const uint64_t tables[] = {3, 3, 4, 4, 3, 3};
    struct release_handoff_case test = {.operation = "release-page", .roots = 1};

    for (size_t memory = 0; memory < 2; ++memory) {
        for (size_t geometry = 0; geometry < 2; ++geometry) {
            test.frames = frames[memory];
            test.ranges = ranges[geometry];
            for (size_t index = 0; index < 6; ++index) {
                test.pattern = patterns[index];
                test.selected = selected[index];
                test.words = selected[index] ? 1 : 0;
                test.owned = owned[index];
                test.tables = tables[index];
                REQUIRE(page_release_case(&test));
            }
        }
    }
    return true;
}

static bool destruction_cases(void)
{
    const uint64_t frames[] = {4096, 262144};
    const size_t ranges[] = {1, 8};
    const char *const patterns[] = {"empty", "one", "dense32", "sparse32", "dense513"};
    const uint64_t leaves[] = {0, 1, 32, 32, 513};
    const uint64_t tables[] = {1, 3, 3, 34, 4};
    struct release_handoff_case test = {.operation = "destroy", .roots = 1};

    for (size_t memory = 0; memory < 2; ++memory) {
        for (size_t geometry = 0; geometry < 2; ++geometry) {
            test.frames = frames[memory];
            test.ranges = ranges[geometry];
            for (size_t index = 0; index < 5; ++index) {
                test.pattern = patterns[index];
                test.tables = tables[index];
                test.owned = test.selected = test.indexed = leaves[index] + tables[index];
                test.words = test.owned / 64 + 1;
                REQUIRE(destruction_case(&test));
            }
        }
    }
    return true;
}

static uint64_t handoff_input_words(uint64_t roots)
{
    uint64_t words = 0;

    for (uint64_t index = 0; index < roots; ++index) {
        uint64_t root_word = (index + 1) / 64;
        uint64_t first_word = (roots + 1 + 4 * index) / 64;
        uint64_t last_word = (roots + 4 + 4 * index) / 64;

        words += 1 + (first_word != root_word) + (last_word != root_word && last_word != first_word);
    }
    return words;
}

static bool handoff_cases(void)
{
    const uint64_t frames[] = {4096, 16384};
    const size_t ranges[] = {1, 8};
    const uint64_t rooted[] = {1, 1, 16, 64};
    const uint64_t rootless[] = {0, 63, 0, 0};
    struct release_handoff_case test = {.pattern = "prepared"};

    for (size_t memory = 0; memory < 2; ++memory) {
        for (size_t geometry = 0; geometry < 2; ++geometry) {
            test.frames = frames[memory];
            test.ranges = ranges[geometry];
            for (size_t index = 0; index < 4; ++index) {
                test.roots = rooted[index];
                test.rootless = rootless[index];
                test.selected = 2 * test.roots;
                test.owned = 5 * test.roots;
                test.tables = 3 * test.roots;
                test.words = handoff_input_words(test.roots);
                test.operation = "prepare-handoff";
                REQUIRE(handoff_case(&test));
                test.operation = "complete-handoff";
                REQUIRE(handoff_case(&test));
            }
        }
    }
    return true;
}

static bool scope_cases(void)
{
    const char *const patterns[] = {"foreign", "table", "malformed", "absent-orphan", "alias"};
    struct release_handoff_case test = {
        .operation = "release-scope", .frames = 4096, .ranges = 1, .owned = 4, .tables = 3, .roots = 1,
    };

    for (size_t index = 0; index < 5; ++index) {
        test.pattern = patterns[index];
        test.selected = test.words = index == 4;
        REQUIRE(scope_case(&test));
    }
    return true;
}

#if !defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
static void report_guard(const char *family, const char *name, int result)
{
    printf(
        "MICROS_MEM05_GUARD {\"family\":\"%s\",\"case\":\"%s\",\"result\":%d,\"state\":true,\"panic\":\"%s\"}\n",
        family, name, result, context.observed_panic == NULL ? "" : context.observed_panic
    );
    ++context.guard_count;
    context.observed_panic = NULL;
}

static void model_selection_add(uint64_t frame)
{
    struct micros_frame_selection *expected = &context.expected_selection;
    uint64_t bit = UINT64_C(1) << (frame % 64);
    size_t word = (size_t)(frame / 64);

    if ((expected->bitmap[word] & bit) != 0) {
        return;
    }
    if (expected->bitmap[word] == 0) {
        expected->words[expected->word_count] = (uint16_t)word;
        expected->positions[word] = (uint16_t)++expected->word_count;
    }
    expected->bitmap[word] |= bit;
    ++expected->frame_count;
}

static bool selection_publication_guards(void)
{
    static const char *const names[] = {"low-bit", "re-add", "word-boundary", "last-bit"};
    static const uint64_t frames[] = {0, 0, 64, MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES - 1};
    struct micros_frame_selection *selection = &context.selection;
    size_t index;

    memset(selection, 0, sizeof(*selection));
    memset(&context.expected_selection, 0, sizeof(context.expected_selection));
    REQUIRE(!micros_frame_selection_contains(selection, 0));
    REQUIRE(micros_frame_selection_clear(selection));
    REQUIRE(memcmp(selection, &context.expected_selection, sizeof(*selection)) == 0);
    report_guard("selection-publication", "empty", 0);
    for (index = 0; index < sizeof(frames) / sizeof(frames[0]); ++index) {
        bool result = micros_frame_selection_add(selection, frames[index]);

        model_selection_add(frames[index]);
        REQUIRE(result && micros_frame_selection_contains(selection, frames[index]));
        REQUIRE(memcmp(selection, &context.expected_selection, sizeof(*selection)) == 0);
        report_guard("selection-publication", names[index], result ? 0 : 1);
    }
    for (index = 0; index < MICROS_FRAME_ALLOCATOR_BITMAP_WORDS; ++index) {
        REQUIRE(micros_frame_selection_add(selection, 64 * index));
        model_selection_add(64 * index);
    }
    REQUIRE(memcmp(selection, &context.expected_selection, sizeof(*selection)) == 0);
    report_guard("selection-publication", "all-words", 0);
    memset(context.bitmap, 0xff, sizeof(context.bitmap));
    construct_selection();
    context.expected_selection = context.selection;
    REQUIRE(micros_frame_selection_add(selection, MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES - 1));
    REQUIRE(memcmp(selection, &context.expected_selection, sizeof(*selection)) == 0);
    report_guard("selection-publication", "full-re-add", 0);
    REQUIRE(micros_frame_selection_clear(selection));
    memset(&context.expected_selection, 0, sizeof(context.expected_selection));
    REQUIRE(memcmp(selection, &context.expected_selection, sizeof(*selection)) == 0);
    report_guard("selection-publication", "clear", 0);
    return true;
}

static void malformed_selection(size_t mutation)
{
    struct micros_frame_selection *selection = &context.selection;

    switch (mutation) {
    case 0:
        break;
    case 1:
        selection->word_count = MICROS_FRAME_ALLOCATOR_BITMAP_WORDS + 1;
        break;
    case 2:
        selection->frame_count = MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES + 1;
        break;
    case 3:
        selection->word_count = 0;
        break;
    case 4:
        selection->frame_count = 0;
        break;
    case 5:
        selection->frame_count = 1;
        break;
    case 6:
        selection->words[0] = MICROS_FRAME_ALLOCATOR_BITMAP_WORDS;
        break;
    case 7:
        selection->words[1] = selection->words[0];
        break;
    case 8:
        selection->positions[0] = 0;
        break;
    case 9:
        selection->positions[0] = 2;
        break;
    case 10:
        selection->bitmap[0] = 0;
        break;
    default:
        ++selection->frame_count;
        break;
    }
}

static bool selection_shape_guards(void)
{
    static const char *const names[] = {
        "null", "word-count", "frame-count", "zero-words", "zero-frames", "frames-below-words",
        "word-range", "duplicate", "missing-inverse", "wrong-inverse", "zero-word", "population",
    };
    static const char *const operations[] = {"release", "wired", "clear"};
    struct micros_process_handle process = {63, 2};
    struct release_handoff_case test = {.frames = 65, .ranges = 1, .pattern = "boundary-three"};
    size_t operation;
    size_t mutation;

    for (operation = 0; operation < 3; ++operation) {
        for (mutation = 0; mutation < sizeof(names) / sizeof(names[0]); ++mutation) {
            struct micros_frame_selection *selection;
            char name[64];
            int result;

            REQUIRE(initialize_geometry(65, 1));
            fill_allocated_input(&test, process, false);
            malformed_selection(mutation);
            context.expected_selection = context.selection;
            REQUIRE(snapshot_expected(false));
            selection = mutation == 0 ? NULL : &context.selection;
            if (operation == 0) {
                result = micros_frame_ownership_release_process_set(micros_as_native_ownership(), process, selection);
            } else if (operation == 1) {
                result = micros_frame_ownership_prepare_wired_process_user_set(micros_as_native_ownership(), selection);
            } else {
                result = micros_frame_selection_clear(selection) ? 0 : MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
            }
            REQUIRE(result == MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT);
            REQUIRE(snapshot_state_matches());
            REQUIRE(memcmp(&context.selection, &context.expected_selection, sizeof(context.selection)) == 0);
            REQUIRE(snprintf(name, sizeof(name), "%s/%s", operations[operation], names[mutation]) > 0);
            report_guard("selection-shape", name, result);
        }
    }
    for (operation = 0; operation < 2; ++operation) {
        int result;

        REQUIRE(initialize_geometry(65, 1));
        fill_allocated_input(&test, process, false);
        context.selection.bitmap[1] |= UINT64_C(2);
        ++context.selection.frame_count;
        context.expected_selection = context.selection;
        REQUIRE(snapshot_expected(false));
        result = operation == 0
            ? micros_frame_ownership_release_process_set(micros_as_native_ownership(), process, &context.selection)
            : micros_frame_ownership_prepare_wired_process_user_set(micros_as_native_ownership(), &context.selection);
        REQUIRE(result == MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED);
        REQUIRE(snapshot_state_matches());
        REQUIRE(memcmp(&context.selection, &context.expected_selection, sizeof(context.selection)) == 0);
        report_guard("selection-shape", operation == 0 ? "release/unmanaged" : "wired/unmanaged", result);
    }
    for (mutation = 0; mutation < 2; ++mutation) {
        bool added;

        memset(&context.selection, 0, sizeof(context.selection));
        if (mutation == 0) {
            context.selection.bitmap[0] = UINT64_C(1);
        } else {
            context.selection.positions[0] = 1;
        }
        context.expected_selection = context.selection;
        added = micros_frame_selection_add(&context.selection, 0);
        REQUIRE(!added && !micros_frame_selection_contains(&context.selection, 0));
        REQUIRE(memcmp(&context.selection, &context.expected_selection, sizeof(context.selection)) == 0);
        report_guard("selection-shape", mutation == 0 ? "add/unpublished-bitmap" : "add/unpublished-inverse", added ? 0 : 1);
    }
    return true;
}

static bool allocate_index_guard(struct micros_frame_owner owner, uint64_t frame)
{
    uint64_t address = UINT64_MAX;
    uint64_t expected;

    REQUIRE(physical_address(frame, &expected) && snapshot_expected(false));
    model_owner(frame, owner);
    context.expected_allocator.allocation_cursor = frame + 1;
    REQUIRE(micros_frame_ownership_allocate(micros_as_native_ownership(), owner, &address) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(address == expected && expected_state_matches());
    return true;
}

static bool release_index_guard(uint64_t frame)
{
    uint64_t address;
    struct micros_frame_owner owner = micros_as_native_ownership()->owners[frame];

    REQUIRE(physical_address(frame, &address) && snapshot_expected(false));
    model_owner(frame, (struct micros_frame_owner){0});
    REQUIRE(micros_frame_ownership_release(micros_as_native_ownership(), owner, address) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(expected_state_matches());
    return true;
}

static bool index_mutation_guards(void)
{
    struct micros_process_handle process = {63, 2};
    struct micros_frame_owner owner = {
        .slot = 63, .generation = 2, .kind = MICROS_FRAME_OWNER_PROCESS_USER,
    };
    struct release_handoff_case test = {.frames = 65, .ranges = 1, .pattern = "boundary-three"};
    static const char *const removals[] = {"remove-head", "remove-middle", "remove-tail", "remove-singleton"};
    static const uint64_t removed[] = {3, 1, 0, 2};
    size_t index;
    uint64_t count;

    REQUIRE(initialize_geometry(65, 1));
    REQUIRE(allocate_index_guard(owner, 0));
    report_guard("index-mutation", "insert-empty", 0);
    REQUIRE(allocate_index_guard(owner, 1));
    report_guard("index-mutation", "insert-head", 0);
    REQUIRE(allocate_index_guard(owner, 2) && allocate_index_guard(owner, 3));
    for (index = 0; index < sizeof(removed) / sizeof(removed[0]); ++index) {
        REQUIRE(release_index_guard(removed[index]));
        report_guard("index-mutation", removals[index], 0);
    }
    for (index = 0; index < 2; ++index) {
        REQUIRE(initialize_geometry(65, 1));
        fill_allocated_input(&test, process, false);
        context.bitmap[0] = UINT64_C(1) << 63;
        construct_selection();
        if (index != 0) {
            context.selection.words[0] = 1;
            context.selection.words[1] = 0;
            context.selection.positions[0] = 2;
            context.selection.positions[1] = 1;
        }
        REQUIRE(snapshot_expected(false));
        model_owner(63, (struct micros_frame_owner){0});
        model_owner(64, (struct micros_frame_owner){0});
        REQUIRE(micros_frame_ownership_release_process_set(micros_as_native_ownership(), process, &context.selection)
                == MICROS_FRAME_OWNERSHIP_OK);
        REQUIRE(expected_state_matches());
        report_guard("index-mutation", index == 0 ? "adjacent-forward" : "adjacent-reverse", 0);
    }
    REQUIRE(initialize_geometry(65, 1));
    test.pattern = "four-mixed";
    test.indexed = 4;
    fill_allocated_input(&test, process, true);
    REQUIRE(snapshot_expected(false));
    count = UINT64_MAX;
    REQUIRE(micros_frame_ownership_count_process(micros_as_native_ownership(), process, &count)
            == MICROS_FRAME_OWNERSHIP_OK && count == 2);
    ++process.generation;
    REQUIRE(micros_frame_ownership_count_process(micros_as_native_ownership(), process, &count)
            == MICROS_FRAME_OWNERSHIP_OK && count == 2);
    REQUIRE(expected_state_matches());
    report_guard("index-mutation", "mixed-generation", 0);
    context.bitmap[0] = UINT64_C(5);
    construct_selection();
    --process.generation;
    model_owner(0, (struct micros_frame_owner){0});
    model_owner(2, (struct micros_frame_owner){0});
    REQUIRE(micros_frame_ownership_release_process_set(micros_as_native_ownership(), process, &context.selection)
            == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(expected_state_matches());
    owner.generation = 4;
    REQUIRE(allocate_index_guard(owner, 0));
    REQUIRE(micros_frame_ownership_count_process(micros_as_native_ownership(), process, &count)
            == MICROS_FRAME_OWNERSHIP_OK && count == 0);
    process.generation = 4;
    REQUIRE(micros_frame_ownership_count_process(micros_as_native_ownership(), process, &count)
            == MICROS_FRAME_OWNERSHIP_OK && count == 1);
    report_guard("index-mutation", "slot-reuse", 0);
    return true;
}

static bool index_rejection_guards(void)
{
    static const char *const names[] = {
        "head-count", "count-head", "head-range", "link-range", "count-range", "head-previous",
        "self-link", "previous", "next", "foreign-neighbor", "free-neighbor", "release-count",
    };
    struct micros_process_handle process = {63, 2};
    struct release_handoff_case test = {.frames = 65, .ranges = 1, .indexed = 4, .pattern = "four"};
    size_t mutation;

    for (mutation = 0; mutation < sizeof(names) / sizeof(names[0]); ++mutation) {
        struct micros_frame_ownership *ownership;
        uint64_t count = UINT64_MAX;
        enum micros_frame_ownership_error error;

        REQUIRE(initialize_geometry(65, 1));
        fill_allocated_input(&test, process, true);
        ownership = micros_as_native_ownership();
        switch (mutation) {
        case 0:
            ownership->process_frames[63].count = 0;
            break;
        case 1:
            ownership->process_frames[63].head = 0;
            break;
        case 2:
            ownership->process_frames[63].head = 66;
            break;
        case 3:
            ownership->owner_links[2].next = 66;
            break;
        case 4:
            ownership->process_frames[63].count = 66;
            break;
        case 5:
            ownership->owner_links[3].previous = 1;
            break;
        case 6:
            ownership->owner_links[2].next = 3;
            break;
        case 7:
            ownership->owner_links[2].previous = 1;
            break;
        case 8:
            ownership->owner_links[3].next = 2;
            break;
        case 9:
            ownership->owners[2].slot = 62;
            break;
        case 10:
            ownership->owners[2] = (struct micros_frame_owner){0};
            break;
        default:
            ownership->process_frames[63].count = 2;
            context.bitmap[0] = UINT64_C(7);
            construct_selection();
            break;
        }
        REQUIRE(snapshot_expected(false));
        error = mutation == 11
            ? micros_frame_ownership_release_process_set(ownership, process, &context.selection)
            : micros_frame_ownership_count_process(ownership, process, &count);
        REQUIRE(error == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT && count == UINT64_MAX);
        REQUIRE(snapshot_state_matches());
        report_guard("index-rejection", names[mutation], error);
    }
    {
        struct micros_frame_ownership *ownership;
        struct micros_frame_vm_freeze_plan plan;
        struct micros_frame_vm_freeze_plan before;
        uint64_t data[2];
        uint64_t tables[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
        enum micros_frame_ownership_error error;

        REQUIRE(initialize_geometry(65, 1));
        ownership = micros_as_native_ownership();
        ownership->phase = MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF;
        REQUIRE(snapshot_expected(false));
        for (uint64_t frame = 0; frame < 61; ++frame) {
            struct micros_frame_owner owner = {.kind = MICROS_FRAME_OWNER_KERNEL_RETAINED};

            if (frame == 0) {
                owner = (struct micros_frame_owner){2, 63, MICROS_FRAME_OWNER_VM_WIRED, 0};
            } else if (frame <= 2) {
                owner.kind = MICROS_FRAME_OWNER_VM_TRANSFERABLE;
            }
            model_owner(frame, owner);
        }
        context.expected_allocator.allocation_cursor = 61;
        model_index(&context.expected_ownership);
        *micros_as_native_allocator() = context.expected_allocator;
        *ownership = context.expected_ownership;
        REQUIRE(micros_frame_ownership_validate(ownership) == MICROS_FRAME_OWNERSHIP_OK);
        ownership->process_frames[63].count = 61;
        REQUIRE(physical_address(1, &data[0]) && physical_address(2, &data[1]));
        for (size_t index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
            REQUIRE(physical_address(61 + index, &tables[index]));
        }
        memset(&plan, 0xa5, sizeof(plan));
        before = plan;
        REQUIRE(snapshot_expected(false));
        error = micros_frame_ownership_vm_freeze_preflight(ownership, process, data, 2, tables, &plan);
        REQUIRE(error == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
        REQUIRE(memcmp(&plan, &before, sizeof(plan)) == 0 && snapshot_state_matches());
        report_guard("index-rejection", "freeze-capacity", error);
    }
    return true;
}

static bool complete_index_guards(void)
{
    static const char *const names[] = {
        "disconnected-cycle", "unindexed-owner", "cross-slot",
        "inactive-previous", "inactive-next", "nonprocess-links",
    };
    static const char *const selected[] = {
        "selected/unrelated-slot", "selected/inactive-link", "selected/unindexed-unreachable",
    };
    struct micros_process_handle process = {63, 2};
    struct release_handoff_case test = {.frames = 65, .ranges = 1, .indexed = 4, .pattern = "four"};
    size_t mutation;

    for (mutation = 0; mutation < 9; ++mutation) {
        struct micros_frame_ownership *ownership;
        uint64_t count = UINT64_MAX;
        enum micros_frame_ownership_error error;

        REQUIRE(initialize_geometry(65, 1));
        fill_allocated_input(&test, process, true);
        ownership = micros_as_native_ownership();
        switch (mutation) {
        case 0:
            ownership->process_frames[63].count = 2;
            ownership->owner_links[2].next = 0;
            ownership->owner_links[0] = (struct micros_frame_owner_links){2, 2};
            ownership->owner_links[1] = (struct micros_frame_owner_links){1, 1};
            break;
        case 1:
        case 8:
            ownership->process_frames[63].count = 3;
            ownership->owner_links[1].next = 0;
            ownership->owner_links[0] = (struct micros_frame_owner_links){0};
            break;
        case 2:
            ownership->process_frames[62] = ownership->process_frames[63];
            break;
        case 3:
        case 7:
            ownership->owner_links[65].previous = 1;
            break;
        case 4:
            ownership->owner_links[65].next = 1;
            break;
        case 5:
            ownership->owner_links[64].next = 1;
            break;
        default:
            ownership->owners[64].reserved = 1;
            break;
        }
        REQUIRE(snapshot_expected(false));
        REQUIRE(micros_frame_ownership_validate(ownership) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
        if (mutation < 6) {
            error = MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
            REQUIRE(snapshot_state_matches());
            report_guard("complete-index", names[mutation], error);
        } else {
            error = micros_frame_ownership_count_process(ownership, process, &count);
            REQUIRE(error == MICROS_FRAME_OWNERSHIP_OK && count == (mutation == 8 ? 3 : 4));
            REQUIRE(snapshot_state_matches());
            report_guard("complete-index", selected[mutation - 6], error);
        }
    }
    {
        struct micros_frame_ownership *ownership;
        struct micros_process_handle foreign = {63, 3};
        struct micros_process_handle other_slot = {62, 2};
        uint64_t count = UINT64_MAX;
        enum micros_frame_ownership_error error;

        REQUIRE(initialize_geometry(65, 1));
        test.indexed = 1;
        fill_allocated_input(&test, process, true);
        ownership = micros_as_native_ownership();
        ownership->handoff_targets[0] = MICROS_FRAME_HANDOFF_TARGET_COUNT;
        REQUIRE(snapshot_expected(false));
        REQUIRE(micros_frame_ownership_count_process(ownership, process, &count)
                == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT && count == UINT64_MAX);
        error = micros_frame_ownership_count_process(ownership, foreign, &count);
        REQUIRE(error == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT && count == UINT64_MAX);
        REQUIRE(snapshot_state_matches());
        report_guard("complete-index", "same-slot-invalid-target", error);
        ownership->handoff_targets[0] = MICROS_FRAME_HANDOFF_NONE;
        REQUIRE(snapshot_expected(false));
        error = micros_frame_ownership_count_process(ownership, foreign, &count);
        REQUIRE(error == MICROS_FRAME_OWNERSHIP_OK && count == 0 && snapshot_state_matches());
        report_guard("complete-index", "selected/valid-other-generation", error);
        ownership->owners[1] = (struct micros_frame_owner){2, 62, MICROS_FRAME_OWNER_PROCESS_USER, 0};
        --ownership->owner_counts[MICROS_FRAME_OWNER_KERNEL_RETAINED];
        ++ownership->owner_counts[MICROS_FRAME_OWNER_PROCESS_USER];
        context.ordinals[1] = ++context.next_ordinal;
        model_index(ownership);
        ownership->handoff_targets[0] = MICROS_FRAME_HANDOFF_TARGET_COUNT;
        REQUIRE(snapshot_expected(false));
        REQUIRE(micros_frame_ownership_validate(ownership) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
        count = UINT64_MAX;
        error = micros_frame_ownership_count_process(ownership, other_slot, &count);
        REQUIRE(error == MICROS_FRAME_OWNERSHIP_OK && count == 1 && snapshot_state_matches());
        report_guard("complete-index", "selected/other-slot-invalid-target", error);
    }
    return true;
}

static bool vm_claim_guard(uint64_t frame)
{
    struct micros_frame_vm_claim_plan plan;
    struct micros_frame_owner owner = {.kind = MICROS_FRAME_OWNER_VM_TRANSFERABLE};
    uint64_t address;

    REQUIRE(physical_address(frame, &address) && snapshot_expected(false));
    REQUIRE(micros_frame_ownership_vm_claim_preflight(micros_as_native_ownership(), address, &plan)
            == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(plan.frame_index == frame && snapshot_state_matches());
    model_owner(frame, owner);
    micros_frame_ownership_vm_claim_commit_prevalidated(micros_as_native_ownership(), &plan);
    REQUIRE(expected_state_matches());
    return true;
}

static bool vm_freeze_guard(
    struct micros_process_handle process,
    uint64_t data[2],
    uint64_t tables[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT]
)
{
    struct micros_frame_vm_freeze_plan plan;
    struct micros_frame_owner wired = {process.generation, process.slot, MICROS_FRAME_OWNER_VM_WIRED, 0};
    struct micros_frame_owner table = {process.generation, process.slot, MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE, 0};
    size_t index;

    for (index = 0; index < 2; ++index) {
        REQUIRE(physical_address(index, &data[index]));
    }
    for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        REQUIRE(physical_address(2 + index, &tables[index]));
    }
    REQUIRE(snapshot_expected(false));
    REQUIRE(micros_frame_ownership_vm_freeze_preflight(micros_as_native_ownership(), process, data, 2, tables, &plan)
            == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(plan.process.slot == process.slot && plan.process.generation == process.generation
            && plan.data_frame_count == 2 && plan.reserved == 0 && snapshot_state_matches());
    for (index = 0; index < 2; ++index) {
        REQUIRE(plan.data_frame_indices[index] == index);
        model_owner(index, wired);
    }
    for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        REQUIRE(plan.table_frame_indices[index] == 2 + index);
        model_owner(2 + index, table);
    }
    micros_frame_ownership_vm_freeze_commit_prevalidated(micros_as_native_ownership(), &plan);
    REQUIRE(expected_state_matches());
    return true;
}

static bool lifecycle_guards(void)
{
    struct micros_process_handle process = {63, 2};
    struct micros_frame_owner user = {2, 63, MICROS_FRAME_OWNER_PROCESS_USER, 0};
    struct micros_frame_owner table = {2, 63, MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE, 0};
    struct micros_frame_owner transferable = {.kind = MICROS_FRAME_OWNER_VM_TRANSFERABLE};
    struct micros_frame_vm_transferable_release_plan rollback;
    struct micros_frame_vm_process_release_plan release;
    uint64_t data[2];
    uint64_t tables[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
    uint64_t address;
    size_t index;

    REQUIRE(initialize_geometry(65, 1));
    REQUIRE(allocate_index_guard(user, 0) && allocate_index_guard(table, 1));
    report_guard("lifecycle", "bootstrap-allocate", 0);
    REQUIRE(release_index_guard(0) && release_index_guard(1));
    report_guard("lifecycle", "bootstrap-release", 0);
    REQUIRE(allocate_index_guard(user, 0) && allocate_index_guard(table, 1));
    context.bitmap[0] = 3;
    construct_selection();
    REQUIRE(snapshot_expected(false));
    model_owner(0, (struct micros_frame_owner){0});
    model_owner(1, (struct micros_frame_owner){0});
    REQUIRE(micros_frame_ownership_release_process_set(micros_as_native_ownership(), process, &context.selection)
            == MICROS_FRAME_OWNERSHIP_OK && expected_state_matches());
    report_guard("lifecycle", "selected-release", 0);
    for (index = 0; index < 3; ++index) {
        struct micros_frame_owner original = index == 2
            ? (struct micros_frame_owner){.kind = MICROS_FRAME_OWNER_KERNEL_RETAINED} : user;
        struct micros_frame_owner result = index == 0
            ? (struct micros_frame_owner){2, 63, MICROS_FRAME_OWNER_VM_WIRED, 0} : transferable;
        enum micros_frame_handoff_target target = index == 0
            ? MICROS_FRAME_HANDOFF_VM_WIRED : MICROS_FRAME_HANDOFF_VM_TRANSFERABLE;

        REQUIRE(initialize_geometry(65, 1));
        REQUIRE(allocate_index_guard(original, 0) && physical_address(0, &address));
        REQUIRE(micros_frame_ownership_prepare_handoff(micros_as_native_ownership(), address, original, target)
                == MICROS_FRAME_OWNERSHIP_OK);
        REQUIRE(snapshot_expected(false));
        model_owner(0, result);
        context.expected_ownership.phase = MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF;
        REQUIRE(micros_frame_ownership_complete_handoff(micros_as_native_ownership()) == MICROS_FRAME_OWNERSHIP_OK);
        REQUIRE(expected_state_matches());
        report_guard("lifecycle", index == 0 ? "wired-handoff" : index == 1 ? "transferable-handoff" : "kernel-transfer", 0);
    }
    REQUIRE(initialize_geometry(65, 1));
    REQUIRE(micros_frame_ownership_complete_handoff(micros_as_native_ownership()) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(vm_claim_guard(0));
    report_guard("lifecycle", "vm-claim", 0);
    REQUIRE(physical_address(0, &address) && snapshot_expected(false));
    REQUIRE(micros_frame_ownership_vm_release_transferable_preflight(micros_as_native_ownership(), &address, 1, &rollback)
            == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(rollback.frame_count == 1 && rollback.frame_indices[0] == 0 && snapshot_state_matches());
    model_owner(0, (struct micros_frame_owner){0});
    micros_frame_ownership_vm_release_transferable_commit_prevalidated(micros_as_native_ownership(), &rollback);
    REQUIRE(expected_state_matches());
    report_guard("lifecycle", "vm-rollback", 0);
    REQUIRE(vm_claim_guard(0) && vm_claim_guard(1));
    REQUIRE(vm_freeze_guard(process, data, tables));
    report_guard("lifecycle", "vm-freeze", 0);
    REQUIRE(snapshot_expected(false));
    REQUIRE(micros_frame_ownership_vm_release_process_preflight(micros_as_native_ownership(), process, data, 2, tables, &release)
            == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(release.data_frame_count == 2 && snapshot_state_matches());
    for (index = 0; index < 2 + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        model_owner(index, (struct micros_frame_owner){0});
    }
    micros_frame_ownership_vm_release_process_commit_prevalidated(micros_as_native_ownership(), &release);
    REQUIRE(expected_state_matches());
    report_guard("lifecycle", "vm-release", 0);
    REQUIRE(initialize_geometry(65, 1) && allocate_index_guard(user, 0));
    micros_as_native_ownership()->owner_links[1].next = 1;
    REQUIRE(snapshot_expected(false));
    address = UINT64_MAX;
    REQUIRE(micros_frame_ownership_allocate(micros_as_native_ownership(), user, &address)
            == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT && address == UINT64_MAX);
    REQUIRE(snapshot_state_matches());
    report_guard("lifecycle", "allocation-compensation", MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
    for (size_t mutation = 0; mutation < 5; ++mutation) {
        static const char *const names[] = {
            "vm-claim-linked", "vm-rollback-linked", "vm-freeze-data-linked",
            "vm-freeze-table-linked", "vm-release-count",
        };
        struct micros_frame_ownership *ownership;
        struct micros_frame_vm_claim_plan claim;
        struct micros_frame_vm_claim_plan claim_before;
        struct micros_frame_vm_freeze_plan freeze;
        struct micros_frame_vm_freeze_plan freeze_before;
        struct micros_frame_vm_process_release_plan release_before;
        struct micros_frame_vm_transferable_release_plan rollback_before;
        enum micros_frame_ownership_error error;

        REQUIRE(initialize_geometry(65, 1));
        ownership = micros_as_native_ownership();
        REQUIRE(micros_frame_ownership_complete_handoff(ownership) == MICROS_FRAME_OWNERSHIP_OK);
        if (mutation != 0) {
            REQUIRE(vm_claim_guard(0) && vm_claim_guard(1));
        }
        REQUIRE(physical_address(0, &data[0]) && physical_address(1, &data[1]));
        for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
            REQUIRE(physical_address(index + 2, &tables[index]));
        }
        if (mutation == 4) {
            REQUIRE(vm_freeze_guard(process, data, tables));
            ownership->process_frames[63].count = 1 + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        } else {
            ownership->owner_links[mutation == 2 ? 1 : mutation == 3 ? 5 : 0].next = 1;
        }
        memset(&claim, 0xa5, sizeof(claim));
        memset(&rollback, 0xa5, sizeof(rollback));
        memset(&freeze, 0xa5, sizeof(freeze));
        memset(&release, 0xa5, sizeof(release));
        claim_before = claim;
        rollback_before = rollback;
        freeze_before = freeze;
        release_before = release;
        REQUIRE(snapshot_expected(false));
        if (mutation == 0) {
            error = micros_frame_ownership_vm_claim_preflight(ownership, data[0], &claim);
        } else if (mutation == 1) {
            error = micros_frame_ownership_vm_release_transferable_preflight(ownership, data, 2, &rollback);
        } else if (mutation == 4) {
            error = micros_frame_ownership_vm_release_process_preflight(ownership, process, data, 2, tables, &release);
        } else {
            error = micros_frame_ownership_vm_freeze_preflight(ownership, process, data, 2, tables, &freeze);
        }
        REQUIRE(error == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT && snapshot_state_matches());
        REQUIRE(memcmp(&claim, &claim_before, sizeof(claim)) == 0);
        REQUIRE(memcmp(&rollback, &rollback_before, sizeof(rollback)) == 0);
        REQUIRE(memcmp(&freeze, &freeze_before, sizeof(freeze)) == 0);
        REQUIRE(memcmp(&release, &release_before, sizeof(release)) == 0);
        report_guard("lifecycle", names[mutation], error);
    }
    return true;
}

static bool omit_index_head(struct micros_frame_ownership *ownership, uint16_t slot, uint64_t frame)
{
    uint32_t next = ownership->owner_links[frame].next;

    REQUIRE(ownership->process_frames[slot].head == frame + 1);
    REQUIRE(ownership->owner_links[frame].previous == 0 && ownership->process_frames[slot].count != 0);
    ownership->process_frames[slot].head = next;
    --ownership->process_frames[slot].count;
    ownership->owner_links[frame] = (struct micros_frame_owner_links){0};
    if (next != 0) {
        ownership->owner_links[next - 1].previous = 0;
    }
    return true;
}

static bool root_atomicity_guards(void)
{
    static const char *const names[] = {
        "foreign", "table", "stale", "free", "malformed", "cycle", "alias", "orphan",
        "shared-root", "indexed-orphan", "reachable-unindexed", "unindexed-unreachable",
        "live-thread", "active-root",
    };
    static const enum micros_user_address_space_error expected[] = {
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP, MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP, MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        MICROS_USER_ADDRESS_SPACE_ERROR_PTE, MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP, MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT, MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP, MICROS_USER_ADDRESS_SPACE_OK,
        MICROS_USER_ADDRESS_SPACE_ERROR_STATE, MICROS_USER_ADDRESS_SPACE_ERROR_STATE,
    };
    struct release_handoff_case test = {.frames = 4096, .ranges = 1, .pattern = "prune-both"};
    uint32_t permissions = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;
    size_t mutation;

    for (mutation = 0; mutation < sizeof(names) / sizeof(names[0]); ++mutation) {
        struct micros_process_handle process;
        struct micros_frame_ownership *ownership;
        uint64_t selected;
        uint64_t foreign = 0;
        uint64_t orphan = 0;
        uint64_t tables[3];
        uint64_t *leaf;
        uint64_t frame;
        uint64_t output = UINT64_MAX;
        enum micros_user_address_space_error error;

        REQUIRE(prepare_point_case(&test, &process, &selected));
        REQUIRE(input_path(process, MICROS_USER_VIRTUAL_BASE, tables, &leaf));
        ownership = micros_as_native_ownership();
        if (mutation == 0) {
            struct micros_process_handle other;

            REQUIRE(create_root(1, &other));
            REQUIRE(map_input_page(other, MICROS_USER_VIRTUAL_BASE, permissions, &foreign));
        }
        if (mutation == 9 || mutation == 11) {
            struct micros_frame_owner owner = {
                process.generation, process.slot, MICROS_FRAME_OWNER_PROCESS_USER, 0,
            };

            REQUIRE(micros_frame_ownership_runtime_allocate(owner, &orphan) == MICROS_FRAME_OWNERSHIP_OK);
            memset((void *)(uintptr_t)orphan, 0x6b, MICROS_FRAME_SIZE);
        }
        if (mutation == 12) {
            struct micros_thread_handle thread;

            REQUIRE(micros_thread_create(micros_kernel_object_runtime_authoritative_registry(), process, &thread)
                    == MICROS_KERNEL_OBJECT_OK);
        } else if (mutation == 13) {
            REQUIRE(micros_user_address_space_activate(process) == MICROS_USER_ADDRESS_SPACE_OK);
        }
        REQUIRE(snapshot_expected(true) && input_frame_index(selected, &frame));
        switch (mutation) {
        case 0:
        case 1:
            REQUIRE(micros_sv39_make_leaf_pte(
                mutation == 0 ? foreign : tables[0], permissions | MICROS_SV39_PERMISSION_USER, leaf
            ) == MICROS_SV39_OK);
            REQUIRE(expected_pte_store(leaf, *leaf));
            break;
        case 2:
            ++ownership->owners[frame].generation;
            ++context.expected_ownership.owners[frame].generation;
            break;
        case 3:
            ownership->owners[frame] = (struct micros_frame_owner){0};
            context.expected_ownership.owners[frame] = (struct micros_frame_owner){0};
            break;
        case 4:
            *leaf = (UINT64_C(1) << 63) | 1;
            REQUIRE(expected_pte_store(leaf, *leaf));
            break;
        case 5: {
            uint64_t *entry = &((struct micros_user_address_space_page_table *)(uintptr_t)tables[0])->entries[1];

            REQUIRE(micros_sv39_make_table_pte(tables[0], entry) == MICROS_SV39_OK);
            REQUIRE(expected_pte_store(entry, *entry));
            break;
        }
        case 6:
            REQUIRE(leaf[1] == 0);
            leaf[1] = *leaf;
            REQUIRE(expected_pte_store(&leaf[1], leaf[1]));
            break;
        case 7:
            *leaf = 0;
            REQUIRE(expected_pte_store(leaf, 0));
            break;
        case 8: {
            uint64_t *entry = &((struct micros_user_address_space_page_table *)(uintptr_t)tables[0])->entries[0];

            *entry = 1;
            REQUIRE(expected_pte_store(entry, 1));
            break;
        }
        case 10:
        case 11:
            REQUIRE(mutation == 10 || input_frame_index(orphan, &frame));
            REQUIRE(omit_index_head(ownership, process.slot, frame));
            REQUIRE(omit_index_head(&context.expected_ownership, process.slot, frame));
            REQUIRE(micros_frame_ownership_validate(ownership) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
            break;
        default:
            break;
        }
        if (mutation <= 5) {
            error = micros_user_address_space_release_page(process, MICROS_USER_VIRTUAL_BASE, &output);
            REQUIRE(error == expected[mutation] && output == UINT64_MAX && snapshot_state_matches());
        }
        if (mutation == 11) {
            for (size_t index = 0; index < 3; ++index) {
                uint64_t released;

                REQUIRE(input_frame_index(tables[index], &released));
                REQUIRE(expect_released_frame(tables[index], true));
                context.expected_ownership.owner_links[released] = (struct micros_frame_owner_links){0};
            }
            REQUIRE(input_frame_index(selected, &frame) && expect_released_frame(selected, true));
            context.expected_ownership.owner_links[frame] = (struct micros_frame_owner_links){0};
            context.expected_ownership.process_frames[process.slot] = (struct micros_process_frame_index){0};
            context.expected_objects.processes[process.slot].address_space_root = 0;
        }
        error = micros_user_address_space_destroy(process);
        REQUIRE(error == expected[mutation] && snapshot_state_matches());
        if (mutation == 11) {
            REQUIRE(micros_frame_ownership_validate(ownership) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
        }
        report_guard("root-atomicity", names[mutation], error);
    }
    for (size_t pruned = 0; pruned < 3; ++pruned) {
        static const char *const patterns[] = {"keep", "prune-leaf", "prune-both"};
        static const char *const guards[] = {"prune-keep", "prune-leaf", "prune-both"};
        struct micros_process_handle process;
        uint64_t selected;
        uint64_t output = UINT64_MAX;
        uint64_t tables[3];
        uint64_t *leaf;
        enum micros_user_address_space_error error;

        test.pattern = patterns[pruned];
        REQUIRE(prepare_point_case(&test, &process, &selected));
        REQUIRE(input_path(process, MICROS_USER_VIRTUAL_BASE, tables, &leaf));
        REQUIRE(snapshot_expected(true));
        REQUIRE(expect_released_frame(selected, false) && expected_pte_store(leaf, 0));
        if (pruned >= 1) {
            REQUIRE(expect_released_frame(tables[2], true));
            REQUIRE(expected_pte_store(&((struct micros_user_address_space_page_table *)(uintptr_t)tables[1])->entries[0], 0));
        }
        if (pruned == 2) {
            REQUIRE(expect_released_frame(tables[1], true));
            REQUIRE(expected_pte_store(&((struct micros_user_address_space_page_table *)(uintptr_t)tables[0])->entries[1], 0));
        }
        error = micros_user_address_space_release_page(process, MICROS_USER_VIRTUAL_BASE, &output);
        REQUIRE(error == MICROS_USER_ADDRESS_SPACE_OK && output == selected && expected_state_matches());
        report_guard("root-atomicity", guards[pruned], error);
    }
    return true;
}

static struct micros_thread *thread_for_process(struct micros_process_handle process)
{
    struct micros_kernel_objects *objects = micros_kernel_object_runtime_authoritative_registry();

    for (size_t index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        struct micros_thread *thread = &objects->threads[index];

        if (thread->slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && thread->owner.slot == process.slot && thread->owner.generation == process.generation) {
            return thread;
        }
    }
    return NULL;
}

static bool handoff_atomicity_guards(void)
{
    static const char *const names[] = {
        "missing-context", "invalid-context", "rootless-thread", "rootless-endpoint", "rootless-owner",
        "wrong-kind-owner", "malformed-root", "alias", "indexed-orphan", "transferable-target",
    };
    struct release_handoff_case test = {.frames = 4096, .ranges = 1, .roots = 1, .rootless = 1};
    size_t phase;
    size_t mutation;

    for (phase = 0; phase < 2; ++phase) {
        test.operation = phase == 0 ? "prepare-handoff" : "complete-handoff";
        for (mutation = 0; mutation < sizeof(names) / sizeof(names[0]); ++mutation) {
            struct micros_kernel_objects *objects;
            struct micros_frame_ownership *ownership;
            struct micros_process_handle rooted;
            struct micros_process_handle rootless;
            struct micros_thread *thread;
            uint64_t tables[3];
            uint64_t *leaf;
            uint64_t address;
            uint64_t frame;
            char name[80];
            enum micros_user_address_space_error error;

            REQUIRE(prepare_handoff_case(&test));
            objects = micros_kernel_object_runtime_authoritative_registry();
            ownership = micros_as_native_ownership();
            rooted = context.processes[0];
            rootless = (struct micros_process_handle){1, objects->processes[1].generation};
            thread = thread_for_process(rooted);
            REQUIRE(thread != NULL && input_path(rooted, MICROS_USER_VIRTUAL_BASE, tables, &leaf));
            switch (mutation) {
            case 0: {
                struct micros_thread_handle handle = {(uint16_t)(thread - objects->threads), thread->generation};

                REQUIRE(micros_thread_detach_execution_context(objects, handle) == MICROS_KERNEL_OBJECT_OK);
                break;
            }
            case 1:
                thread->user_context.sstatus |= MICROS_RISCV_SSTATUS_SPP;
                break;
            case 2: {
                struct micros_thread_handle handle;

                REQUIRE(micros_thread_create(objects, rootless, &handle) == MICROS_KERNEL_OBJECT_OK);
                break;
            }
            case 3:
                objects->processes[1].primary_endpoint = UINT32_C(0x1001);
                break;
            case 4:
            case 5:
            case 8: {
                struct micros_process_handle process = mutation == 8 ? rooted : rootless;
                struct micros_frame_owner owner = {
                    process.generation, process.slot, MICROS_FRAME_OWNER_PROCESS_USER, 0,
                };

                REQUIRE(micros_frame_ownership_runtime_allocate(owner, &address) == MICROS_FRAME_OWNERSHIP_OK);
                memset((void *)(uintptr_t)address, 0x7c, MICROS_FRAME_SIZE);
                if (mutation == 5) {
                    REQUIRE(input_frame_index(address, &frame));
                    ownership->owners[frame].kind = MICROS_FRAME_OWNER_KERNEL_RETAINED;
                    --ownership->owner_counts[MICROS_FRAME_OWNER_PROCESS_USER];
                    ++ownership->owner_counts[MICROS_FRAME_OWNER_KERNEL_RETAINED];
                }
                break;
            }
            case 6:
                REQUIRE(leaf[2] == 0);
                leaf[2] = (UINT64_C(1) << 63) | 1;
                break;
            case 7:
                REQUIRE(leaf[2] == 0);
                leaf[2] = leaf[0];
                break;
            default:
                address = ((*leaf >> 10) << 12);
                REQUIRE(input_frame_index(address, &frame));
                ownership->handoff_targets[frame] = MICROS_FRAME_HANDOFF_VM_TRANSFERABLE;
                break;
            }
            REQUIRE(micros_kernel_objects_validate(objects) == MICROS_KERNEL_OBJECT_OK);
            REQUIRE(snapshot_expected(true));
            error = phase == 0 ? micros_user_address_space_prepare_wired_handoff()
                : micros_user_address_space_complete_wired_handoff();
            REQUIRE(error == (mutation == 9 ? MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
                                           : MICROS_USER_ADDRESS_SPACE_ERROR_STATE));
            REQUIRE(snapshot_state_matches());
            REQUIRE(snprintf(name, sizeof(name), "%s/%s", phase == 0 ? "prepare" : "complete", names[mutation]) > 0);
            report_guard("handoff-atomicity", name, error);
        }
    }
    return true;
}

static bool precedence_guards(void)
{
    struct micros_process_handle invalid = {MICROS_PROCESS_CAPACITY, 0};
    struct micros_process_handle process = {63, 2};
    struct release_handoff_case test = {.frames = 4096, .ranges = 1, .pattern = "prune-both"};
    enum micros_frame_ownership_error ownership_error;
    enum micros_user_address_space_error error;
    size_t index;

    REQUIRE(initialize_geometry(65, 1));
    REQUIRE(micros_frame_ownership_complete_handoff(micros_as_native_ownership()) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(snapshot_expected(false));
    ownership_error = micros_frame_ownership_release_process_set(micros_as_native_ownership(), invalid, NULL);
    REQUIRE(ownership_error == MICROS_FRAME_OWNERSHIP_ERROR_PHASE && snapshot_state_matches());
    report_guard("precedence-lifetime", "phase-null-release", ownership_error);
    ownership_error = micros_frame_ownership_prepare_wired_process_user_set(micros_as_native_ownership(), NULL);
    REQUIRE(ownership_error == MICROS_FRAME_OWNERSHIP_ERROR_PHASE && snapshot_state_matches());
    report_guard("precedence-lifetime", "phase-null-wired", ownership_error);
    REQUIRE(initialize_geometry(65, 1) && snapshot_expected(false));
    ownership_error = micros_frame_ownership_release_process_set(micros_as_native_ownership(), invalid, NULL);
    REQUIRE(ownership_error == MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT && snapshot_state_matches());
    report_guard("precedence-lifetime", "null-invalid-handle", ownership_error);
    context.selection.word_count = MICROS_FRAME_ALLOCATOR_BITMAP_WORDS + 1;
    context.expected_selection = context.selection;
    ownership_error = micros_frame_ownership_release_process_set(micros_as_native_ownership(), invalid, &context.selection);
    REQUIRE(ownership_error == MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT && snapshot_state_matches());
    REQUIRE(memcmp(&context.selection, &context.expected_selection, sizeof(context.selection)) == 0);
    report_guard("precedence-lifetime", "header-invalid-handle", ownership_error);
    memset(&context.selection, 0, sizeof(context.selection));
    context.selection.word_count = 1;
    context.selection.frame_count = 1;
    context.selection.words[0] = MICROS_FRAME_ALLOCATOR_BITMAP_WORDS;
    context.expected_selection = context.selection;
    micros_as_native_ownership()->owned_frame_count = 1;
    REQUIRE(snapshot_expected(false));
    ownership_error = micros_frame_ownership_release_process_set(micros_as_native_ownership(), process, &context.selection);
    REQUIRE(ownership_error == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT && snapshot_state_matches());
    REQUIRE(memcmp(&context.selection, &context.expected_selection, sizeof(context.selection)) == 0);
    report_guard("precedence-lifetime", "metadata-prefix", ownership_error);
    for (index = 0; index < 2; ++index) {
        uint64_t selected;
        uint64_t output = UINT64_MAX;
        uint64_t tables[3];
        uint64_t *leaf;
        uint64_t address = MICROS_USER_VIRTUAL_BASE + (index == 0 ? MICROS_FRAME_SIZE : 0);

        REQUIRE(prepare_point_case(&test, &process, &selected));
        REQUIRE(input_path(process, address, tables, &leaf));
        REQUIRE(micros_user_address_space_activate(process) == MICROS_USER_ADDRESS_SPACE_OK);
        REQUIRE(snapshot_expected(true));
        if (index == 0) {
            REQUIRE(*leaf == 0);
        } else {
            *leaf = (UINT64_C(1) << 63) | 1;
            REQUIRE(expected_pte_store(leaf, *leaf));
        }
        error = micros_user_address_space_release_page(process, address, &output);
        REQUIRE(error == (index == 0 ? MICROS_USER_ADDRESS_SPACE_ERROR_STATE : MICROS_USER_ADDRESS_SPACE_ERROR_PTE));
        REQUIRE(output == UINT64_MAX && snapshot_state_matches());
        report_guard("precedence-lifetime", index == 0 ? "active-absent" : "active-malformed", error);
    }
    test.operation = "prepare-handoff";
    test.roots = 2;
    REQUIRE(prepare_handoff_case(&test));
    {
        uint64_t tables[3];
        uint64_t *leaf;
        uint64_t extra;
        uint64_t padding = 0;
        uint64_t protected_page;
        long host_page_size = sysconf(_SC_PAGESIZE);
        const struct micros_frame_owner retained = {.kind = MICROS_FRAME_OWNER_KERNEL_RETAINED};
        struct micros_thread *later = thread_for_process(context.processes[1]);

        REQUIRE(host_page_size >= MICROS_FRAME_SIZE && host_page_size % MICROS_FRAME_SIZE == 0);
        REQUIRE((uint64_t)host_page_size / MICROS_FRAME_SIZE < SNAPSHOT_PAGES - 16);
        for (size_t page = 0; page < (size_t)host_page_size / MICROS_FRAME_SIZE; ++page) {
            REQUIRE(micros_frame_ownership_runtime_allocate(retained, &padding) == MICROS_FRAME_OWNERSHIP_OK);
            if (padding % (uint64_t)host_page_size == (uint64_t)host_page_size - MICROS_FRAME_SIZE) {
                break;
            }
        }
        REQUIRE(padding % (uint64_t)host_page_size == (uint64_t)host_page_size - MICROS_FRAME_SIZE);
        REQUIRE(map_input_page(context.processes[0], MICROS_USER_VIRTUAL_BASE + UINT64_C(0x200000),
                               MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE, &extra));
        REQUIRE(input_path(context.processes[0], MICROS_USER_VIRTUAL_BASE + UINT64_C(0x200000), tables, &leaf));
        protected_page = tables[2] - tables[2] % (uint64_t)host_page_size;
        REQUIRE(protected_page > padding);
        REQUIRE(later != NULL);
        later->user_context.sstatus |= MICROS_RISCV_SSTATUS_SPP;
        REQUIRE(snapshot_expected(true));
        REQUIRE(mprotect((void *)(uintptr_t)protected_page, (size_t)host_page_size, PROT_NONE) == 0);
        error = micros_user_address_space_prepare_wired_handoff();
        REQUIRE(mprotect((void *)(uintptr_t)protected_page, (size_t)host_page_size, PROT_READ | PROT_WRITE) == 0);
        REQUIRE(error == MICROS_USER_ADDRESS_SPACE_ERROR_STATE && snapshot_state_matches());
        report_guard("precedence-lifetime", "later-context-earlier-root", error);
    }
    test.operation = "complete-handoff";
    REQUIRE(prepare_handoff_case(&test));
    {
        uint64_t tables[3];
        uint64_t *leaf;
        uint64_t frame;

        REQUIRE(input_path(context.processes[0], MICROS_USER_VIRTUAL_BASE, tables, &leaf));
        REQUIRE(input_frame_index((*leaf >> 10) << 12, &frame));
        micros_as_native_ownership()->handoff_targets[frame] = MICROS_FRAME_HANDOFF_VM_TRANSFERABLE;
        REQUIRE(input_path(context.processes[1], MICROS_USER_VIRTUAL_BASE, tables, &leaf) && leaf[2] == 0);
        leaf[2] = (UINT64_C(1) << 63) | 1;
        REQUIRE(snapshot_expected(true));
        error = micros_user_address_space_complete_wired_handoff();
        REQUIRE(error == MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP && snapshot_state_matches());
        report_guard("precedence-lifetime", "earlier-target-later-root", error);
    }
    return true;
}

static bool scratch_lifetime_guards(void)
{
    struct release_handoff_case test = {.frames = 4096, .ranges = 1};
    struct micros_user_address_space_runtime_view view;
    struct micros_user_address_space_scratch_lease other;
    struct micros_user_address_space_scratch_lease before;
    enum micros_user_address_space_error error;
    size_t mode;

    for (mode = 0; mode < 2; ++mode) {
        REQUIRE(initialize_kernel(&test));
        REQUIRE(micros_user_address_space_runtime_require_bootstrap(&view) == MICROS_USER_ADDRESS_SPACE_OK);
        error = mode == 0 ? micros_user_address_space_acquire_selected_scratch(&view, &context.lease)
            : micros_user_address_space_acquire_scratch(&view, &context.lease);
        REQUIRE(error == MICROS_USER_ADDRESS_SPACE_OK);
        memset(&other, 0xa5, sizeof(other));
        before = other;
        context.expected_lease = context.lease;
        REQUIRE(snapshot_expected(false));
        error = micros_user_address_space_acquire_selected_scratch(&view, &other);
        REQUIRE(error == MICROS_USER_ADDRESS_SPACE_ERROR_BUSY);
        REQUIRE(micros_user_address_space_acquire_scratch(&view, &other) == MICROS_USER_ADDRESS_SPACE_ERROR_BUSY);
        REQUIRE(memcmp(&other, &before, sizeof(other)) == 0);
        REQUIRE(memcmp(&context.lease, &context.expected_lease, sizeof(context.lease)) == 0);
        REQUIRE(snapshot_state_matches());
        report_guard("precedence-lifetime", mode == 0 ? "nested-selected" : "nested-full", error);
        micros_user_address_space_release_scratch(&context.lease);
        REQUIRE(!micros_as_native_scratch_busy());
    }
    REQUIRE(initialize_kernel(&test));
    REQUIRE(micros_user_address_space_runtime_require_bootstrap(&view) == MICROS_USER_ADDRESS_SPACE_OK);
    micros_mem05_native_scratch_set_mode(UINT8_C(99));
    memset(&other, 0xa5, sizeof(other));
    before = other;
    REQUIRE(snapshot_expected(false));
    error = micros_user_address_space_acquire_selected_scratch(&view, &other);
    REQUIRE(error == MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT && snapshot_state_matches());
    REQUIRE(memcmp(&other, &before, sizeof(other)) == 0);
    report_guard("precedence-lifetime", "unknown-mode", error);
    REQUIRE(initialize_kernel(&test));
    REQUIRE(micros_user_address_space_runtime_require_bootstrap(&view) == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(micros_user_address_space_acquire_selected_scratch(&view, &context.lease) == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(micros_frame_selection_add(context.lease.reachable_selection, 0));
    micros_mem05_native_scratch_set_mode(MICROS_USER_ADDRESS_SPACE_SCRATCH_IDLE);
    REQUIRE(snapshot_expected(false));
    error = micros_user_address_space_acquire_selected_scratch(&view, &other);
    REQUIRE(error == MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT && snapshot_state_matches());
    REQUIRE(memcmp(&other, &before, sizeof(other)) == 0);
    report_guard("precedence-lifetime", "idle-published", error);
    for (size_t mutation = 0; mutation < 5; ++mutation) {
        static const char *const names[] = {
            "wrong-mode", "wrong-storage", "wrong-bitmap", "inactive-lease", "second-prefix",
        };

        REQUIRE(initialize_kernel(&test));
        REQUIRE(micros_user_address_space_runtime_require_bootstrap(&view) == MICROS_USER_ADDRESS_SPACE_OK);
        REQUIRE(micros_user_address_space_acquire_selected_scratch(&view, &context.lease) == MICROS_USER_ADDRESS_SPACE_OK);
        REQUIRE(micros_frame_selection_add(context.lease.reachable_selection, 0));
        REQUIRE(micros_frame_selection_add(context.lease.release_selection, 1));
        switch (mutation) {
        case 0:
            context.lease.mode = MICROS_USER_ADDRESS_SPACE_SCRATCH_FULL;
            break;
        case 1:
            context.lease.reachable_selection = &context.selection;
            break;
        case 2:
            context.lease.release_bitmap = context.selection.bitmap;
            break;
        case 3:
            context.lease.active = false;
            break;
        default:
            ++context.lease.release_selection->frame_count;
            break;
        }
        context.expected_lease = context.lease;
        REQUIRE(snapshot_expected(false));
        context.expected_panic = "user-address-space-scratch-release";
        context.observed_panic = NULL;
        if (setjmp(context.panic_target) == 0) {
            micros_user_address_space_release_scratch(&context.lease);
            REQUIRE(false);
        }
        context.expected_panic = NULL;
        REQUIRE(context.observed_panic != NULL && snapshot_state_matches());
        REQUIRE(memcmp(&context.lease, &context.expected_lease, sizeof(context.lease)) == 0);
        report_guard("precedence-lifetime", names[mutation], 0);
        micros_mem05_native_scratch_reset();
        memset(&context.lease, 0, sizeof(context.lease));
    }
    return true;
}

struct vm_certificate_fixture {
    struct micros_user_address_space_vm_table_plan plan;
    struct micros_process_handle foreign;
    size_t pages;
};

static bool initialize_vm_certificate(
    struct vm_certificate_fixture *fixture, uint64_t frames, size_t pages)
{
    const struct release_handoff_case test = {.frames = frames, .ranges = 3};
    struct micros_frame_ownership *ownership = micros_as_native_ownership();
    struct micros_frame_vm_freeze_plan freeze;
    struct micros_user_address_space_vm_mapping mappings[MICROS_APPLICATION_RESIDENT_PAGE_MAX] = {0};
    struct micros_process_handle process;
    struct micros_user_address_space_page_table *root, *middle;
    uint64_t data[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    uint64_t tables[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];

    REQUIRE(initialize_kernel(&test) && create_root(1, &fixture->foreign));
    REQUIRE(micros_process_create_at(micros_kernel_object_runtime_authoritative_registry(), 63, &process)
            == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_frame_ownership_complete_handoff(ownership) == MICROS_FRAME_OWNERSHIP_OK);
    fixture->pages = pages;
    for (size_t index = 0; index < pages; ++index) {
        struct micros_frame_vm_claim_plan claim;

        REQUIRE(physical_address(2 + 3 * index, &data[index]));
        REQUIRE(micros_frame_ownership_vm_claim_preflight(ownership, data[index], &claim) == MICROS_FRAME_OWNERSHIP_OK);
        micros_frame_ownership_vm_claim_commit_prevalidated(ownership, &claim);
        mappings[index] = (struct micros_user_address_space_vm_mapping){
            .virtual_address = index + 1 == pages ? MICROS_APPLICATION_STACK_BASE
                : MICROS_APPLICATION_ENTRY + index * MICROS_FRAME_SIZE,
            .physical_address = data[index],
            .permissions = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ
                | (index == 0 ? MICROS_SV39_PERMISSION_EXECUTE
                   : index == 1 ? 0 : MICROS_SV39_PERMISSION_WRITE),
        };
    }
    for (size_t index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        REQUIRE(physical_address(frames - 4 + index, &tables[index]));
    }
    REQUIRE(micros_user_address_space_prepare_vm_table_plan(
        process, tables, mappings, pages, &fixture->plan) == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(fixture->plan.entry_count == pages);
    REQUIRE(micros_frame_ownership_vm_freeze_preflight(
        ownership, process, data, pages, tables, &freeze) == MICROS_FRAME_OWNERSHIP_OK);
    for (size_t index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        micros_user_address_space_clear_page(tables[index]);
    }
    root = micros_user_address_space_table_at(tables[0]);
    middle = micros_user_address_space_table_at(tables[1]);
    memcpy(root, micros_user_address_space_table_at(micros_kernel_address_space_report()->root_physical_address),
           MICROS_FRAME_SIZE);
    root->entries[fixture->plan.root_index] = fixture->plan.root_pte;
    middle->entries[fixture->plan.image_middle_index] = fixture->plan.image_middle_pte;
    middle->entries[fixture->plan.stack_middle_index] = fixture->plan.stack_middle_pte;
    for (size_t index = 0; index < pages; ++index) {
        const struct micros_user_address_space_vm_leaf_plan *leaf = &fixture->plan.leaves[index];
        struct micros_user_address_space_page_table *table = micros_user_address_space_table_at(tables[leaf->table_role]);

        table->entries[leaf->leaf_index] = leaf->pte;
    }
    micros_frame_ownership_vm_freeze_commit_prevalidated(ownership, &freeze);
    REQUIRE(micros_frame_ownership_validate(ownership) == MICROS_FRAME_OWNERSHIP_OK);
    return true;
}

static bool check_vm_certificate(
    struct vm_certificate_fixture *fixture, const char *name, enum micros_user_address_space_error expected)
{
    struct micros_user_address_space_runtime_view runtime;
    struct micros_user_address_space_scratch_lease lease;
    enum micros_user_address_space_error error;

    REQUIRE(snapshot_expected(true));
    REQUIRE(micros_user_address_space_runtime_require_read(&runtime) == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(begin_capture());
    REQUIRE(micros_user_address_space_acquire_selected_scratch(&runtime, &lease) == MICROS_USER_ADDRESS_SPACE_OK);
    error = micros_user_address_space_validate_vm_table_plan_locked(&runtime, &lease, &fixture->plan);
    micros_user_address_space_release_scratch(&lease);
    REQUIRE(end_capture() && error == expected && snapshot_state_matches());
#if defined(MICROS_TEST_WORK_COUNTS)
    if (expected == MICROS_USER_ADDRESS_SPACE_OK) {
        const uint64_t *counts = context.observation.counts[MICROS_WORK_OWNER_KERNEL_MEMORY];
        uint64_t bound = 5 * MICROS_SV39_TABLE_ENTRY_COUNT + fixture->pages + 4;

        if (MICROS_MEM05_NATIVE_CHANNEL == 0 || MICROS_MEM05_NATIVE_CHANNEL == 2) {
            if (counts[MICROS_WORK_KIND_PTE_VISIT] > bound) {
                fprintf(stderr, "VM02_ROOT_PTE_BOUND actual=%" PRIu64 " bound=%" PRIu64 "\n",
                        counts[MICROS_WORK_KIND_PTE_VISIT], bound);
                return false;
            }
            REQUIRE(counts[MICROS_WORK_KIND_PTE_VISIT] >= 4 * MICROS_SV39_TABLE_ENTRY_COUNT
                    && counts[MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL] == 1);
        } else {
            REQUIRE(counts[MICROS_WORK_KIND_PTE_VISIT] == 0);
        }
        REQUIRE(counts[MICROS_WORK_KIND_INDEX_PROBE] <= 96 * (fixture->pages + 4) + 64 * 3 + 1024);
    }
#endif
    report_guard("vm-certificate", name, error);
    return true;
}

static void add_unplanned_vm_owner(struct vm_certificate_fixture *fixture, bool indexed)
{
    struct micros_frame_ownership *ownership = micros_as_native_ownership();
    struct micros_process_handle process = indexed ? fixture->plan.process : fixture->foreign;

    ownership->owners[3] = (struct micros_frame_owner){process.generation, process.slot, MICROS_FRAME_OWNER_VM_WIRED, 0};
    ownership->allocator->allocated_bitmap[0] |= UINT64_C(1) << 3;
    --ownership->allocator->free_frame_count;
    ++ownership->owned_frame_count;
    ++ownership->owner_counts[MICROS_FRAME_OWNER_VM_WIRED];
    if (indexed) {
        uint32_t previous_head = ownership->process_frames[process.slot].head;

        ownership->owner_links[3] = (struct micros_frame_owner_links){0, previous_head};
        ownership->owner_links[previous_head - 1].previous = 4;
        ownership->process_frames[process.slot].head = 4;
        ++ownership->process_frames[process.slot].count;
    }
}

static bool vm_certificate_guards(void)
{
    static const char *const names[] = {
        "shared-root", "middle-hole", "image-hole", "stack-hole", "substituted-table",
        "unmanaged-table", "owner-bit", "owner-generation", "owner-link", "indexed-orphan",
        "unvisited-root", "unindexed-foreign", "oversized-plan",
    };
    static const enum micros_user_address_space_error errors[] = {
        MICROS_USER_ADDRESS_SPACE_ERROR_PTE, MICROS_USER_ADDRESS_SPACE_ERROR_PTE,
        MICROS_USER_ADDRESS_SPACE_ERROR_PTE, MICROS_USER_ADDRESS_SPACE_ERROR_PTE,
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP, MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT, MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT, MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        MICROS_USER_ADDRESS_SPACE_OK, MICROS_USER_ADDRESS_SPACE_OK, MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT,
    };
    struct vm_certificate_fixture fixture;
#if defined(MICROS_TEST_WORK_COUNTS)
    uint64_t minimum[MICROS_WORK_KIND_COUNT];
#endif

    REQUIRE(initialize_vm_certificate(&fixture, 96, 4) && check_vm_certificate(&fixture, "minimum", 0));
#if defined(MICROS_TEST_WORK_COUNTS)
    memcpy(minimum, context.observation.counts[MICROS_WORK_OWNER_KERNEL_MEMORY], sizeof(minimum));
#endif
    REQUIRE(initialize_vm_certificate(&fixture, 262143, 4)
            && check_vm_certificate(&fixture, "maximum-memory", 0));
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(memcmp(minimum, context.observation.counts[MICROS_WORK_OWNER_KERNEL_MEMORY], sizeof(minimum)) == 0);
#endif
    REQUIRE(initialize_vm_certificate(&fixture, 96, 16) && check_vm_certificate(&fixture, "maximum-pages", 0));
    REQUIRE(micros_kernel_object_runtime_attach_address_space(fixture.plan.process, fixture.plan.table_frames[0])
            == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(check_vm_certificate(&fixture, "attached", 0));
    for (unsigned mutation = 0; mutation < sizeof(names) / sizeof(names[0]); ++mutation) {
        struct micros_frame_ownership *ownership = micros_as_native_ownership();
        struct micros_kernel_objects *objects = micros_kernel_object_runtime_authoritative_registry();

        REQUIRE(initialize_vm_certificate(&fixture, 96, 16));
        if (mutation < 4) {
            struct micros_user_address_space_page_table *table =
                micros_user_address_space_table_at(fixture.plan.table_frames[mutation]);

            table->entries[mutation == 0 ? 2 : mutation == 1 ? 1 : mutation == 2 ? 500 : 0] = UINT64_C(1);
        } else if (mutation == 4 || mutation == 5) {
            fixture.plan.table_frames[2] = mutation == 4
                ? objects->processes[fixture.foreign.slot].address_space_root : UINT64_MAX - 4095;
        } else if (mutation == 6) {
            ownership->allocator->allocated_bitmap[0] ^= UINT64_C(1) << 2;
        } else if (mutation == 7) {
            ++ownership->owners[2].generation;
        } else if (mutation == 8) {
            ownership->owner_links[92].previous = 93;
        } else if (mutation == 9 || mutation == 11) {
            add_unplanned_vm_owner(&fixture, mutation == 9);
        } else if (mutation == 10) {
            micros_user_address_space_table_at(objects->processes[fixture.foreign.slot].address_space_root)
                ->entries[1] = UINT64_MAX;
        } else {
            fixture.plan.entry_count = MICROS_APPLICATION_RESIDENT_PAGE_MAX + 1;
        }
        REQUIRE(check_vm_certificate(&fixture, names[mutation], errors[mutation]));
        if (mutation == 10) {
            REQUIRE(micros_user_address_space_validate(fixture.foreign) != MICROS_USER_ADDRESS_SPACE_OK);
        } else if (mutation == 11) {
            REQUIRE(micros_frame_ownership_validate(ownership) == MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT);
        }
    }
    return true;
}

static bool current_guards(void)
{
    return selection_publication_guards() && selection_shape_guards()
        && index_mutation_guards() && index_rejection_guards() && complete_index_guards()
        && lifecycle_guards() && root_atomicity_guards() && handoff_atomicity_guards()
        && precedence_guards() && scratch_lifetime_guards() && vm_certificate_guards();
}
#endif

#if defined(MICROS_SELECTED_RELEASE_HANDOFF_LAUNCHER)
static int launch_native_products(int argc, char **argv)
{
    char *arguments[17];
    size_t count = 0;
    const char *launcher = getenv("__PYVENV_LAUNCHER__");

    if (launcher != NULL && launcher[0] != '\0') {
        fputs("__PYVENV_LAUNCHER__ is unsupported\n", stderr);
        return 2;
    }
    arguments[count++] = MICROS_SELECTED_RELEASE_HANDOFF_PYTHON;
    arguments[count++] = "-E";
    arguments[count++] = "-S";
    arguments[count++] = "-B";
    arguments[count++] = MICROS_SELECTED_RELEASE_HANDOFF_DRIVER;
    arguments[count++] = "--native";
    for (int index = 1; index < argc; ++index) {
        arguments[count++] = "--executable";
        arguments[count++] = argv[index];
    }
    arguments[count++] = "--executable";
    arguments[count++] = argv[0];
    arguments[count] = NULL;
    if (chdir(MICROS_SELECTED_RELEASE_HANDOFF_ROOT) == 0) {
        execv(MICROS_SELECTED_RELEASE_HANDOFF_PYTHON, arguments);
    }
    perror("MEM05 native orchestration");
    return 2;
}
#endif

int main(int argc, char **argv)
{
    bool passed;

#if defined(MICROS_SELECTED_RELEASE_HANDOFF_LAUNCHER)
    if (argc == 5) {
        return launch_native_products(argc, argv);
    }
#endif
    if (argc != 3) {
        return 2;
    }
    context.producer = TEST_PRODUCER;
#if defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
    if (strcmp(argv[1], "red") != 0) {
        return 2;
    }
#else
    if (strcmp(argv[1], "green") != 0) {
        return 2;
    }
    context.producer += 256;
#endif
#if defined(MICROS_TEST_WORK_COUNTS)
    static const char *const channels[] = {"aggregate", "frames", "plans", "context"};

    if (strcmp(argv[2], channels[MICROS_MEM05_NATIVE_CHANNEL]) != 0) {
        return 2;
    }
    context.producer += MICROS_MEM05_NATIVE_CHANNEL + 1;
#else
    if (strcmp(argv[2], "ordinary") != 0) {
        return 2;
    }
#endif
    context.arena = mmap(NULL, ARENA_BYTES, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (context.arena == MAP_FAILED) {
        perror("MEM05 native fixture allocation");
        return 2;
    }
    printf("MICROS_MEM05_NATIVE version=1 stage=%s channel=%s\n", argv[1], argv[2]);
    passed = portable_release_cases() && process_count_cases() && page_release_cases()
        && destruction_cases() && handoff_cases() && scope_cases() && context.capture == 128;
#if !defined(MICROS_SELECTED_RELEASE_HANDOFF_BASELINE)
    passed = passed && current_guards();
#endif
    if (munmap(context.arena, ARENA_BYTES) != 0) {
        return 2;
    }
    if (!passed) {
        return 1;
    }
    puts("MICROS_MEM05_NATIVE_PASS cases=128");
    return 0;
}
