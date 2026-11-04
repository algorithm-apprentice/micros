#ifndef MICROS_KERNEL_PANIC_TRACE_CORE_H
#define MICROS_KERNEL_PANIC_TRACE_CORE_H

#include <stdbool.h>
#include <stdint.h>

enum micros_panic_trace_owner {
    MICROS_PANIC_TRACE_OWNER_UNAVAILABLE = 0,
    MICROS_PANIC_TRACE_OWNER_BOOT = 1,
    MICROS_PANIC_TRACE_OWNER_PRIMARY = 2,
    MICROS_PANIC_TRACE_OWNER_THREAD = 3,
    MICROS_PANIC_TRACE_OWNER_EMERGENCY = 4,
};

enum micros_panic_trace_stop {
    MICROS_PANIC_TRACE_ACTIVE = 0,
    MICROS_PANIC_TRACE_END = 1,
    MICROS_PANIC_TRACE_BOUNDARY = 2,
    MICROS_PANIC_TRACE_LIMIT = 3,
    MICROS_PANIC_TRACE_OWNER = 4,
    MICROS_PANIC_TRACE_INVALID_FRAME = 5,
    MICROS_PANIC_TRACE_INVALID_PC = 6,
    MICROS_PANIC_TRACE_NONMONOTONIC = 7,
    MICROS_PANIC_TRACE_READ_PENDING = 8,
    MICROS_PANIC_TRACE_HAVE_VALUES = 9,
};

struct micros_panic_trace_view {
    uintptr_t sp;
    uintptr_t scratch;
    uintptr_t anchor;
    uintptr_t boot_bottom;
    uintptr_t boot_top;
    uintptr_t primary_bottom;
    uintptr_t primary_top;
    uintptr_t emergency_bottom;
    uintptr_t emergency_top;
    uintptr_t bank_bottom;
    uintptr_t bank_top;
    uintptr_t idle_bottom;
    uintptr_t idle_top;
    uintptr_t installed_emergency_bottom;
    uintptr_t installed_emergency_top;
    uintptr_t selected_bottom;
    uintptr_t selected_top;
    uintptr_t attached_bottom;
    uintptr_t attached_top;
    uint32_t current_slot;
    bool registry_ready;
    bool hart_live;
    bool traps_installed;
    bool thread_live;
    bool process_live;
    bool context_attached;
};

struct micros_panic_trace_bounds {
    uintptr_t bottom;
    uintptr_t top;
    uintptr_t root;
};

struct micros_panic_trace_cursor {
    uintptr_t lower;
    uintptr_t root;
    uintptr_t text_bottom;
    uintptr_t text_top;
    uintptr_t frame;
    uintptr_t last;
    uint32_t count;
    enum micros_panic_trace_stop stop;
};

_Static_assert(sizeof(uintptr_t) == 8, "panic trace requires RV64-width addresses");
_Static_assert(sizeof(struct micros_panic_trace_cursor) <= 64, "panic trace cursor exceeds fixed storage");
_Static_assert(
    sizeof(struct micros_panic_trace_view) + sizeof(struct micros_panic_trace_bounds)
        + sizeof(enum micros_panic_trace_owner) <= 256,
    "panic trace ownership exceeds fixed storage"
);

bool micros_panic_trace_owner_select(
    const struct micros_panic_trace_view *view,
    enum micros_panic_trace_owner *owner,
    struct micros_panic_trace_bounds *bounds
);
bool micros_panic_trace_cursor_open(
    enum micros_panic_trace_owner owner,
    const struct micros_panic_trace_bounds *bounds,
    uintptr_t sp,
    uintptr_t frame,
    uintptr_t text_bottom,
    uintptr_t text_top,
    struct micros_panic_trace_cursor *cursor
);
bool micros_panic_trace_cursor_plan(
    struct micros_panic_trace_cursor *cursor,
    uintptr_t *pc_address,
    uintptr_t *link_address
);
bool micros_panic_trace_cursor_consume(
    struct micros_panic_trace_cursor *cursor,
    uint64_t pc,
    uint64_t previous
);

#endif
