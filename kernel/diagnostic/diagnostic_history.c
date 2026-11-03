#include "kernel/diagnostic/diagnostic_history.h"

#include <stddef.h>

#include "micros/panic.h"

static struct micros_diagnostic_history diagnostic_history;
static struct {
    uint32_t handoff_cursor;
    bool handed_off;
    bool pending;
} diagnostic_routing;

const struct micros_diagnostic_history *micros_diagnostic_history_state(void)
{
    return &diagnostic_history;
}

void micros_diagnostic_record_byte(uint8_t byte)
{
    if (!micros_diagnostic_append(&diagnostic_history, byte)) {
        MICROS_PANIC(0, "diagnostic-history-invariant");
    }
}

void micros_diagnostic_record_end(void)
{
    if (diagnostic_routing.handed_off) {
        diagnostic_routing.pending = true;
    }
}

void micros_diagnostic_handoff(void)
{
    if (diagnostic_routing.handed_off || !micros_diagnostic_history_valid(&diagnostic_history)) {
        MICROS_PANIC(0, "diagnostic-handoff-invariant");
    }
    diagnostic_routing.handoff_cursor = diagnostic_history.next;
    diagnostic_routing.handed_off = true;
}

bool micros_diagnostic_handoff_cursor(uint32_t *cursor)
{
    if (
        cursor == NULL || !diagnostic_routing.handed_off
        || diagnostic_routing.handoff_cursor >= MICROS_DIAGNOSTIC_RING_CAPACITY
    ) {
        return false;
    }
    *cursor = diagnostic_routing.handoff_cursor;
    return true;
}

bool micros_diagnostic_notification_pending(void)
{
    return diagnostic_routing.pending;
}

void micros_diagnostic_notification_committed(void)
{
    diagnostic_routing.pending = false;
}

int32_t micros_diagnostic_panic_size(void)
{
    return micros_diagnostic_history_valid(&diagnostic_history)
        ? (int32_t)diagnostic_history.tail_position : -1;
}

uint8_t micros_diagnostic_panic_byte_prevalidated(uint32_t index)
{
    return diagnostic_history.tail[index];
}
