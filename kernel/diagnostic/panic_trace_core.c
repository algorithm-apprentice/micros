#include "kernel/diagnostic/panic_trace_core.h"

#include <stddef.h>

#include "micros/work_observation.h"

#if defined(MICROS_TEST_PANIC_TRACE_COUNTS) && MICROS_TEST_PANIC_TRACE_COUNTS != 1
#error "MICROS_TEST_PANIC_TRACE_COUNTS must be undefined or equal to 1"
#endif

static bool trace_extent(uintptr_t bottom, uintptr_t top, uintptr_t size)
{
    return bottom != 0 && top >= bottom && top - bottom == size
        && (bottom & 15) == 0 && (top & 15) == 0;
}

static bool trace_separate(uintptr_t first_bottom, uintptr_t first_top, uintptr_t second_bottom, uintptr_t second_top)
{
    return first_top <= second_bottom || second_top <= first_bottom;
}

static bool trace_layout(const struct micros_panic_trace_view *view)
{
    return trace_extent(view->boot_bottom, view->boot_top, 16384)
        && trace_extent(view->primary_bottom, view->primary_top, 16384)
        && trace_extent(view->emergency_bottom, view->emergency_top, 4096)
        && trace_extent(view->bank_bottom, view->bank_top, 128 * 16384)
        && (view->bank_bottom & 4095) == 0
        && trace_separate(view->boot_bottom, view->boot_top, view->primary_bottom, view->primary_top)
        && trace_separate(view->boot_bottom, view->boot_top, view->emergency_bottom, view->emergency_top)
        && trace_separate(view->boot_bottom, view->boot_top, view->bank_bottom, view->bank_top)
        && trace_separate(view->primary_bottom, view->primary_top, view->emergency_bottom, view->emergency_top)
        && trace_separate(view->primary_bottom, view->primary_top, view->bank_bottom, view->bank_top)
        && trace_separate(view->emergency_bottom, view->emergency_top, view->bank_bottom, view->bank_top);
}

static bool trace_contains(uintptr_t sp, uintptr_t bottom, uintptr_t top)
{
    return (sp & 15) == 0 && bottom <= sp && sp <= top && top - sp >= 16;
}

static void trace_select(
    enum micros_panic_trace_owner selected,
    uintptr_t bottom,
    uintptr_t top,
    uintptr_t root,
    uintptr_t sp,
    enum micros_panic_trace_owner *owner,
    struct micros_panic_trace_bounds *bounds
)
{
    if (trace_contains(sp, bottom, root)) {
        bounds->bottom = bottom;
        bounds->top = top;
        bounds->root = root;
        *owner = selected;
    }
}

static bool trace_hart(const struct micros_panic_trace_view *view)
{
    return view->registry_ready && view->hart_live && view->traps_installed && view->anchor != 0
        && view->idle_bottom == view->primary_bottom && view->idle_top == view->primary_top
        && view->installed_emergency_bottom == view->emergency_bottom
        && view->installed_emergency_top == view->emergency_top;
}

bool micros_panic_trace_owner_select(
    const struct micros_panic_trace_view *view,
    enum micros_panic_trace_owner *owner,
    struct micros_panic_trace_bounds *bounds
)
{
    uintptr_t bottom;
    uintptr_t top;
    uintptr_t slot;

    if (view == NULL || owner == NULL || bounds == NULL) {
        return false;
    }
    *owner = MICROS_PANIC_TRACE_OWNER_UNAVAILABLE;
    bounds->bottom = 0;
    bounds->top = 0;
    bounds->root = 0;
    if (!trace_layout(view)) {
        return true;
    }
    if (trace_contains(view->sp, view->boot_bottom, view->boot_top)) {
        trace_select(
            MICROS_PANIC_TRACE_OWNER_BOOT, view->boot_bottom, view->boot_top,
            view->boot_top, view->sp, owner, bounds
        );
        return true;
    }
    if (!trace_hart(view)) {
        return true;
    }
    if (trace_contains(view->sp, view->primary_bottom, view->primary_top)) {
        if (view->scratch == 0 || view->scratch == view->anchor) {
            trace_select(
                MICROS_PANIC_TRACE_OWNER_PRIMARY, view->primary_bottom, view->primary_top,
                view->primary_top - (view->scratch == 0 ? 288 : 0), view->sp, owner, bounds
            );
        }
        return true;
    }
    if (view->scratch != 0) {
        return true;
    }
    if (trace_contains(view->sp, view->emergency_bottom, view->emergency_top)) {
        trace_select(
            MICROS_PANIC_TRACE_OWNER_EMERGENCY, view->emergency_bottom, view->emergency_top,
            view->emergency_top, view->sp, owner, bounds
        );
        return true;
    }
    if (!trace_contains(view->sp, view->bank_bottom, view->bank_top)) {
        return true;
    }
    slot = (view->sp - view->bank_bottom) / 16384;
    bottom = view->bank_bottom + slot * 16384;
    top = bottom + 16384;
    if (
        view->current_slot == slot && view->thread_live && view->process_live && view->context_attached
        && view->attached_bottom == bottom && view->attached_top == top
        && view->selected_bottom == bottom && view->selected_top == top
    ) {
        trace_select(MICROS_PANIC_TRACE_OWNER_THREAD, bottom, top, top - 288, view->sp, owner, bounds);
    }
    return true;
}

static bool trace_frame(uintptr_t lower, uintptr_t root, uintptr_t frame)
{
    return (frame & 15) == 0 && frame >= 16 && lower <= frame - 16 && frame <= root;
}

bool micros_panic_trace_cursor_open(
    enum micros_panic_trace_owner owner,
    const struct micros_panic_trace_bounds *bounds,
    uintptr_t sp,
    uintptr_t frame,
    uintptr_t text_bottom,
    uintptr_t text_top,
    struct micros_panic_trace_cursor *cursor
)
{
    if (
        cursor == NULL || bounds == NULL
        || owner < MICROS_PANIC_TRACE_OWNER_UNAVAILABLE || owner > MICROS_PANIC_TRACE_OWNER_EMERGENCY
        || text_bottom == 0 || text_top <= text_bottom || ((text_bottom | text_top) & 1) != 0
    ) {
        return false;
    }
    if (owner == MICROS_PANIC_TRACE_OWNER_UNAVAILABLE) {
        if (bounds->bottom != 0 || bounds->top != 0 || bounds->root != 0) {
            return false;
        }
    } else if (
        bounds->bottom == 0 || bounds->bottom > sp || bounds->root > bounds->top
        || !trace_contains(sp, bounds->bottom, bounds->root)
        || ((bounds->bottom | bounds->top | bounds->root) & 15) != 0
        || !trace_separate(bounds->bottom, bounds->top, text_bottom, text_top)
    ) {
        return false;
    }
    cursor->lower = owner == MICROS_PANIC_TRACE_OWNER_UNAVAILABLE ? 0 : sp;
    cursor->root = bounds->root;
    cursor->text_bottom = text_bottom;
    cursor->text_top = text_top;
    cursor->frame = owner == MICROS_PANIC_TRACE_OWNER_UNAVAILABLE ? 0 : frame;
    cursor->last = 0;
    cursor->count = 0;
    if (owner == MICROS_PANIC_TRACE_OWNER_UNAVAILABLE) {
        cursor->stop = MICROS_PANIC_TRACE_OWNER;
    } else if (frame == 0) {
        cursor->stop = MICROS_PANIC_TRACE_END;
    } else {
        cursor->stop = trace_frame(sp, bounds->root, frame)
            ? MICROS_PANIC_TRACE_ACTIVE : MICROS_PANIC_TRACE_INVALID_FRAME;
    }
    return true;
}

static bool trace_advance(struct micros_panic_trace_cursor *cursor)
{
    if (cursor->frame == cursor->root) {
        cursor->stop = MICROS_PANIC_TRACE_BOUNDARY;
    } else if (cursor->last == 0) {
        cursor->stop = MICROS_PANIC_TRACE_END;
    } else if (cursor->last <= cursor->frame) {
        cursor->stop = MICROS_PANIC_TRACE_NONMONOTONIC;
    } else if (!trace_frame(cursor->lower, cursor->root, cursor->last)) {
        cursor->stop = MICROS_PANIC_TRACE_INVALID_FRAME;
    } else if (cursor->count == 32) {
        cursor->stop = MICROS_PANIC_TRACE_LIMIT;
    } else {
        cursor->frame = cursor->last;
#ifdef MICROS_TEST_PANIC_TRACE_COUNTS
        MICROS_TEST_WORK_HIT(
            "panic-trace-advance", MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC, MICROS_WORK_KIND_LINK_TRAVERSE, 1
        );
#endif
        return true;
    }
    return false;
}

bool micros_panic_trace_cursor_plan(
    struct micros_panic_trace_cursor *cursor,
    uintptr_t *pc_address,
    uintptr_t *link_address
)
{
    if (cursor == NULL || pc_address == NULL || link_address == NULL || pc_address == link_address) {
        return false;
    }
    if (cursor->stop == MICROS_PANIC_TRACE_HAVE_VALUES) {
        if (cursor->count == 0 || cursor->count > 32 || !trace_advance(cursor)) {
            return false;
        }
    } else if (cursor->stop != MICROS_PANIC_TRACE_ACTIVE || cursor->count != 0) {
        return false;
    }
    if (!trace_frame(cursor->lower, cursor->root, cursor->frame)) {
        cursor->stop = MICROS_PANIC_TRACE_INVALID_FRAME;
        return false;
    }
    *pc_address = cursor->frame - 8;
    *link_address = cursor->frame - 16;
    cursor->stop = MICROS_PANIC_TRACE_READ_PENDING;
    return true;
}

bool micros_panic_trace_cursor_consume(
    struct micros_panic_trace_cursor *cursor,
    uint64_t pc,
    uint64_t previous
)
{
    if (cursor == NULL || cursor->stop != MICROS_PANIC_TRACE_READ_PENDING || cursor->count >= 32) {
        return false;
    }
    if (pc == 0 || (pc & 1) != 0 || pc < cursor->text_bottom || pc >= cursor->text_top) {
        cursor->stop = MICROS_PANIC_TRACE_INVALID_PC;
        return false;
    }
    cursor->last = previous;
    ++cursor->count;
    cursor->stop = MICROS_PANIC_TRACE_HAVE_VALUES;
    return true;
}
