#define _DEFAULT_SOURCE 1

#include "kernel/diagnostic/panic_trace_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "tests/work/work_counts.h"

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%u: %s\n", __FILE__, (unsigned)__LINE__, #condition); \
            return false; \
        } \
    } while (0)

static const struct micros_panic_trace_view owned = {
    .sp = UINT64_C(0x81000200),
    .scratch = UINT64_C(0x80500000),
    .anchor = UINT64_C(0x80500000),
    .boot_bottom = UINT64_C(0x81000000),
    .boot_top = UINT64_C(0x81004000),
    .primary_bottom = UINT64_C(0x82000000),
    .primary_top = UINT64_C(0x82004000),
    .emergency_bottom = UINT64_C(0x82004000),
    .emergency_top = UINT64_C(0x82005000),
    .bank_bottom = UINT64_C(0x83000000),
    .bank_top = UINT64_C(0x83200000),
    .idle_bottom = UINT64_C(0x82000000),
    .idle_top = UINT64_C(0x82004000),
    .installed_emergency_bottom = UINT64_C(0x82004000),
    .installed_emergency_top = UINT64_C(0x82005000),
    .selected_bottom = UINT64_C(0x83004000),
    .selected_top = UINT64_C(0x83008000),
    .attached_bottom = UINT64_C(0x83004000),
    .attached_top = UINT64_C(0x83008000),
    .current_slot = 1,
    .registry_ready = true,
    .hart_live = true,
    .traps_installed = true,
    .thread_live = true,
    .process_live = true,
    .context_attached = true,
};

static struct micros_test_work_snapshot snapshot;
static uint64_t capture_label;
static unsigned char unchanged_stack[16384];

_Static_assert(sizeof(struct micros_panic_trace_cursor) <= 64, "trace cursor exceeds fixed storage");
_Static_assert(
    sizeof(struct micros_panic_trace_view) + sizeof(struct micros_panic_trace_bounds)
        + sizeof(enum micros_panic_trace_owner) <= 256,
    "trace owner view and outputs exceed fixed storage"
);

static bool expect_owner(
    const struct micros_panic_trace_view *view,
    enum micros_panic_trace_owner expected,
    uintptr_t bottom,
    uintptr_t top,
    uintptr_t root
)
{
    enum micros_panic_trace_owner owner = MICROS_PANIC_TRACE_OWNER_EMERGENCY;
    struct micros_panic_trace_bounds bounds = {1, 2, 3};

    REQUIRE(micros_panic_trace_owner_select(view, &owner, &bounds));
    REQUIRE(owner == expected);
    REQUIRE(bounds.bottom == bottom && bounds.top == top && bounds.root == root);
    return true;
}

static bool owner_epochs(void)
{
    struct micros_panic_trace_view view = owned;

    view.registry_ready = false;
    view.hart_live = false;
    view.traps_installed = false;
    REQUIRE(expect_owner(&view, MICROS_PANIC_TRACE_OWNER_BOOT, owned.boot_bottom, owned.boot_top, owned.boot_top));
    view = owned;
    view.sp = owned.primary_bottom + 512;
    view.scratch = 0;
    REQUIRE(expect_owner(
        &view, MICROS_PANIC_TRACE_OWNER_PRIMARY, owned.primary_bottom, owned.primary_top, owned.primary_top - 288
    ));
    view.scratch = owned.anchor;
    REQUIRE(expect_owner(&view, MICROS_PANIC_TRACE_OWNER_PRIMARY, owned.primary_bottom, owned.primary_top, owned.primary_top));
    view.scratch += 16;
    REQUIRE(expect_owner(&view, MICROS_PANIC_TRACE_OWNER_UNAVAILABLE, 0, 0, 0));
    view = owned;
    view.sp = owned.emergency_bottom + 512;
    view.scratch = 0;
    REQUIRE(expect_owner(
        &view, MICROS_PANIC_TRACE_OWNER_EMERGENCY, owned.emergency_bottom, owned.emergency_top, owned.emergency_top
    ));
    view.installed_emergency_top -= 16;
    REQUIRE(expect_owner(&view, MICROS_PANIC_TRACE_OWNER_UNAVAILABLE, 0, 0, 0));
    view = owned;
    view.sp = owned.attached_bottom + 512;
    view.scratch = 0;
    REQUIRE(expect_owner(
        &view, MICROS_PANIC_TRACE_OWNER_THREAD, owned.attached_bottom, owned.attached_top, owned.attached_top - 288
    ));
    return true;
}

static bool denied_owner_relations(void)
{
    unsigned index;

    for (index = 0; index < 13; ++index) {
        struct micros_panic_trace_view view = owned;

        view.sp = owned.attached_bottom + 512;
        view.scratch = 0;
        switch (index) {
        case 0: view.registry_ready = false; break;
        case 1: view.hart_live = false; break;
        case 2: view.traps_installed = false; break;
        case 3: view.thread_live = false; break;
        case 4: view.process_live = false; break;
        case 5: view.context_attached = false; break;
        case 6: view.current_slot = 0; break;
        case 7: view.attached_bottom -= 16384; view.attached_top -= 16384; break;
        case 8: view.selected_bottom -= 16384; view.selected_top -= 16384; break;
        case 9: view.idle_top -= 16; break;
        case 10: view.scratch = view.anchor; break;
        case 11: view.sp = view.attached_top - 16; break;
        default: view.anchor = 0; break;
        }
        REQUIRE(expect_owner(&view, MICROS_PANIC_TRACE_OWNER_UNAVAILABLE, 0, 0, 0));
    }
    for (index = 0; index < 6; ++index) {
        struct micros_panic_trace_view view = owned;

        switch (index) {
        case 0: view.sp = 0; break;
        case 1: view.sp = owned.boot_top; break;
        case 2: view.sp += 1; break;
        case 3: view.boot_bottom = UINTPTR_MAX - 15; view.boot_top = 0; break;
        case 4: view.primary_bottom = view.boot_bottom; view.primary_top = view.boot_top; break;
        default: view.bank_top -= 16; break;
        }
        REQUIRE(expect_owner(&view, MICROS_PANIC_TRACE_OWNER_UNAVAILABLE, 0, 0, 0));
    }
    return true;
}

static bool begin_work(void)
{
    ++capture_label;
    REQUIRE(micros_test_work_begin(
        UINT64_C(0x504c415430330000), capture_label, UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC
    ) == MICROS_TEST_WORK_OK);
    return true;
}

static bool finish_work(uint64_t words, uint64_t advances)
{
    unsigned owner;
    unsigned kind;

    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
    for (owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        for (kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            uint64_t expected = 0;

            if (owner == MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC) {
                if (kind == MICROS_WORK_KIND_RECORD_VISIT) {
                    expected = words;
                } else if (kind == MICROS_WORK_KIND_LINK_TRAVERSE) {
                    expected = advances;
                }
            }
            REQUIRE(snapshot.counts[owner][kind] == expected);
        }
    }
    return true;
}

static bool read_record(struct micros_panic_trace_cursor *cursor, uint64_t *pc)
{
    uintptr_t pc_address;
    uintptr_t link_address;
    uint64_t previous;

    if (!micros_panic_trace_cursor_plan(cursor, &pc_address, &link_address)) {
        return false;
    }
    *pc = *(const volatile uint64_t *)pc_address;
    MICROS_TEST_WORK_HIT(
        "panic-trace-word-pc", MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC, MICROS_WORK_KIND_RECORD_VISIT, 1
    );
    previous = *(const volatile uint64_t *)link_address;
    MICROS_TEST_WORK_HIT(
        "panic-trace-word-link", MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC, MICROS_WORK_KIND_RECORD_VISIT, 1
    );
    return micros_panic_trace_cursor_consume(cursor, *pc, previous);
}

static void fill_chain(uintptr_t first, unsigned count)
{
    unsigned index;

    for (index = 0; index < count; ++index) {
        uintptr_t frame = first + 16 * index;

        *(uint64_t *)(frame - 16) = index + 1 == count ? 0 : frame + 16;
        *(uint64_t *)(frame - 8) = UINT64_C(0x80200010) + 2 * index;
    }
}

static bool walk(
    const struct micros_panic_trace_bounds *bounds, uintptr_t first,
    unsigned count, enum micros_panic_trace_stop stop, uint64_t words, uint64_t advances
)
{
    struct micros_panic_trace_cursor cursor;
    uint64_t pc;
    unsigned emitted = 0;

    memcpy(unchanged_stack, (const void *)bounds->bottom, sizeof(unchanged_stack));
    REQUIRE(micros_panic_trace_cursor_open(
        MICROS_PANIC_TRACE_OWNER_BOOT, bounds, bounds->bottom + 64, first,
        UINT64_C(0x80200000), UINT64_C(0x80300000), &cursor
    ));
    REQUIRE(begin_work());
    while (read_record(&cursor, &pc)) {
        REQUIRE(emitted < 32);
        REQUIRE(cursor.count == emitted + 1);
        REQUIRE(cursor.frame == first + 16 * emitted);
        REQUIRE(pc == UINT64_C(0x80200010) + 2 * emitted);
        ++emitted;
    }
    REQUIRE(cursor.count == count && emitted == count && cursor.stop == stop);
    REQUIRE(finish_work(words, advances));
    REQUIRE(memcmp(unchanged_stack, (const void *)bounds->bottom, sizeof(unchanged_stack)) == 0);
    return true;
}

static bool frame_boundaries(uintptr_t bottom)
{
    struct micros_panic_trace_bounds bounds = {bottom, bottom + 16384, bottom + 16384};
    uintptr_t first = bottom + 128;
    static const unsigned lengths[] = {1, 2, 31, 32, 33};
    unsigned index;

    REQUIRE(walk(&bounds, 0, 0, MICROS_PANIC_TRACE_END, 0, 0));
    REQUIRE(walk(&bounds, 8, 0, MICROS_PANIC_TRACE_INVALID_FRAME, 0, 0));
    REQUIRE(walk(&bounds, bottom + 64, 0, MICROS_PANIC_TRACE_INVALID_FRAME, 0, 0));
    REQUIRE(walk(&bounds, first + 1, 0, MICROS_PANIC_TRACE_INVALID_FRAME, 0, 0));
    REQUIRE(walk(&bounds, bounds.top + 16, 0, MICROS_PANIC_TRACE_INVALID_FRAME, 0, 0));
    for (index = 0; index < sizeof(lengths) / sizeof(lengths[0]); ++index) {
        unsigned count = lengths[index];
        unsigned accepted = count > 32 ? 32 : count;

        fill_chain(first, count);
        REQUIRE(walk(
            &bounds, first, accepted, count > 32 ? MICROS_PANIC_TRACE_LIMIT : MICROS_PANIC_TRACE_END,
            2 * accepted, accepted - 1
        ));
    }
    fill_chain(bounds.root - 16, 2);
    *(uint64_t *)(bounds.root - 16) = bounds.root + 16;
    REQUIRE(walk(&bounds, bounds.root - 16, 2, MICROS_PANIC_TRACE_BOUNDARY, 4, 1));
    return true;
}

static bool successor_and_pc_precedence(uintptr_t bottom)
{
    struct micros_panic_trace_bounds bounds = {bottom, bottom + 16384, bottom + 16384};
    uintptr_t first = bottom + 128;
    static const uint64_t invalid_pc[] = {0, 1, UINT64_C(0x40000000), UINT64_C(0x80300000)};
    unsigned index;

    for (index = 0; index < 6; ++index) {
        uintptr_t previous = index == 0 ? 0 : index == 1 ? first : index == 2 ? UINT64_C(0x40000000)
            : index == 3 ? first + 1 : index == 4 ? bounds.top + 16 : UINTPTR_MAX;
        enum micros_panic_trace_stop stop = index == 0 ? MICROS_PANIC_TRACE_END
            : index < 3 ? MICROS_PANIC_TRACE_NONMONOTONIC : MICROS_PANIC_TRACE_INVALID_FRAME;

        fill_chain(first, 1);
        *(uint64_t *)(first - 16) = previous;
        REQUIRE(walk(&bounds, first, 1, stop, 2, 0));
    }
    for (index = 0; index < sizeof(invalid_pc) / sizeof(invalid_pc[0]); ++index) {
        fill_chain(first, 1);
        *(uint64_t *)(first - 8) = invalid_pc[index];
        *(uint64_t *)(first - 16) = UINTPTR_MAX;
        REQUIRE(walk(&bounds, first, 0, MICROS_PANIC_TRACE_INVALID_PC, 2, 0));
    }
    fill_chain(first, 32);
    *(uint64_t *)(first + 31 * 16 - 16) = first + 31 * 16;
    REQUIRE(walk(&bounds, first, 32, MICROS_PANIC_TRACE_NONMONOTONIC, 64, 31));
    *(uint64_t *)(first + 31 * 16 - 16) = bounds.top + 16;
    REQUIRE(walk(&bounds, first, 32, MICROS_PANIC_TRACE_INVALID_FRAME, 64, 31));
    first = bounds.root - 31 * 16;
    fill_chain(first, 32);
    *(uint64_t *)(bounds.root - 16) = UINTPTR_MAX;
    REQUIRE(walk(&bounds, first, 32, MICROS_PANIC_TRACE_BOUNDARY, 64, 31));
    *(uint64_t *)(bounds.root - 8) = 0;
    REQUIRE(walk(&bounds, first, 31, MICROS_PANIC_TRACE_INVALID_PC, 64, 31));
    return true;
}

static bool output_precedes_another_advance(uintptr_t bottom)
{
    struct micros_panic_trace_bounds bounds = {bottom, bottom + 16384, bottom + 16384};
    struct micros_panic_trace_cursor cursor;
    uint64_t pc;

    fill_chain(bottom + 128, 2);
    REQUIRE(micros_panic_trace_cursor_open(
        MICROS_PANIC_TRACE_OWNER_BOOT, &bounds, bottom + 64, bottom + 128,
        UINT64_C(0x80200000), UINT64_C(0x80300000), &cursor
    ));
    REQUIRE(begin_work());
    REQUIRE(read_record(&cursor, &pc));
    REQUIRE(cursor.frame == bottom + 128 && cursor.count == 1);
    REQUIRE(finish_work(2, 0));
    return true;
}

static bool misuse_preserves_outputs(void)
{
    enum micros_panic_trace_owner owner = MICROS_PANIC_TRACE_OWNER_THREAD;
    struct micros_panic_trace_bounds bounds = {owned.boot_bottom, owned.boot_top, owned.boot_top};
    struct micros_panic_trace_bounds before = bounds;
    struct micros_panic_trace_cursor cursor;
    struct micros_panic_trace_cursor saved;
    uintptr_t pc = 23;
    uintptr_t link = 29;

    REQUIRE(!micros_panic_trace_owner_select(NULL, &owner, &bounds));
    REQUIRE(owner == MICROS_PANIC_TRACE_OWNER_THREAD && memcmp(&bounds, &before, sizeof(bounds)) == 0);
    memset(&cursor, 0xa5, sizeof(cursor));
    memcpy(&saved, &cursor, sizeof(cursor));
    REQUIRE(!micros_panic_trace_cursor_open(
        MICROS_PANIC_TRACE_OWNER_BOOT, NULL, owned.sp, owned.sp + 32, 2, 4, &cursor
    ));
    REQUIRE(memcmp(&cursor, &saved, sizeof(cursor)) == 0);
    REQUIRE(micros_panic_trace_cursor_open(
        MICROS_PANIC_TRACE_OWNER_UNAVAILABLE, &(struct micros_panic_trace_bounds){0, 0, 0},
        0, UINTPTR_MAX, 2, 4, &cursor
    ));
    REQUIRE(cursor.stop == MICROS_PANIC_TRACE_OWNER && cursor.count == 0);
    REQUIRE(!micros_panic_trace_cursor_plan(&cursor, &pc, &link));
    REQUIRE(pc == 23 && link == 29);
    REQUIRE(micros_panic_trace_cursor_open(
        MICROS_PANIC_TRACE_OWNER_BOOT, &bounds, owned.sp, owned.sp + 32, 2, 4, &cursor
    ));
    REQUIRE(micros_panic_trace_cursor_plan(&cursor, &pc, &link));
    memcpy(&saved, &cursor, sizeof(cursor));
    REQUIRE(!micros_panic_trace_cursor_plan(&cursor, &pc, &pc));
    REQUIRE(memcmp(&cursor, &saved, sizeof(cursor)) == 0);
    REQUIRE(micros_panic_trace_cursor_consume(&cursor, 2, 0));
    memcpy(&saved, &cursor, sizeof(cursor));
    REQUIRE(!micros_panic_trace_cursor_consume(&cursor, 2, 0));
    REQUIRE(memcmp(&cursor, &saved, sizeof(cursor)) == 0);
    REQUIRE(!micros_panic_trace_cursor_plan(&cursor, &pc, &link));
    REQUIRE(cursor.stop == MICROS_PANIC_TRACE_END);
    return true;
}

static bool protected_stack(void)
{
    long reported = sysconf(_SC_PAGESIZE);
    size_t page;
    size_t accessible;
    unsigned char *mapping;
    uintptr_t bottom;
    bool result;

    REQUIRE(reported >= 4096 && (uint64_t)reported <= UINT64_C(1048576));
    page = (size_t)reported;
    accessible = page > 16384 ? page : 16384;
    mapping = mmap(NULL, accessible + 2 * page, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    REQUIRE(mapping != MAP_FAILED);
    if (mprotect(mapping + page, accessible, PROT_READ | PROT_WRITE) != 0) {
        (void)munmap(mapping, accessible + 2 * page);
        return false;
    }
    bottom = (uintptr_t)(mapping + page + accessible - 16384);
    memset((void *)bottom, 0xa5, 16384);
    result = frame_boundaries(bottom) && successor_and_pc_precedence(bottom)
        && output_precedes_another_advance(bottom);
    REQUIRE(munmap(mapping, accessible + 2 * page) == 0);
    return result;
}

int main(void)
{
    if (!owner_epochs() || !denied_owner_relations() || !misuse_preserves_outputs() || !protected_stack()) {
        return 1;
    }
    puts("panic trace: authenticated epochs, guarded frame reads, exact precedence and bounded work passed");
    return 0;
}
