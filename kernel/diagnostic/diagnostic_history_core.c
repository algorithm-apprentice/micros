#include "kernel/diagnostic/diagnostic_history_core.h"

#include <stddef.h>

#include "micros/work_observation.h"

_Static_assert(sizeof(struct micros_diagnostic_history) == 12012, "diagnostic history layout");

bool micros_diagnostic_history_valid(const struct micros_diagnostic_history *history)
{
    return (
        history != NULL
        && history->next < MICROS_DIAGNOSTIC_RING_CAPACITY
        && history->size <= MICROS_DIAGNOSTIC_RING_CAPACITY
        && history->tail_position <= MICROS_DIAGNOSTIC_TAIL_PAYLOAD
        && (history->size == MICROS_DIAGNOSTIC_RING_CAPACITY || history->next == history->size)
        && history->tail_position == (
            history->size < MICROS_DIAGNOSTIC_TAIL_PAYLOAD
                ? history->size : MICROS_DIAGNOSTIC_TAIL_PAYLOAD
        )
        && history->tail[history->tail_position] == 0
        && history->tail[MICROS_DIAGNOSTIC_TAIL_CAPACITY - 1] == 0
    );
}

bool micros_diagnostic_append(struct micros_diagnostic_history *history, uint8_t byte)
{
    uint32_t index;

    if (byte == 0 || !micros_diagnostic_history_valid(history)) {
        return false;
    }
    history->ring[history->next] = byte;
    MICROS_TEST_WORK_HIT("diagnostic-ring-unit", MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC, MICROS_WORK_KIND_COPY_UNIT, 1);
    MICROS_TEST_WORK_HIT("diagnostic-ring-byte", MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC, MICROS_WORK_KIND_COPIED_BYTE, 1);
    history->tail[history->tail_position] = byte;
    MICROS_TEST_WORK_HIT("diagnostic-tail-unit", MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC, MICROS_WORK_KIND_COPY_UNIT, 1);
    MICROS_TEST_WORK_HIT("diagnostic-tail-byte", MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC, MICROS_WORK_KIND_COPIED_BYTE, 1);
    if (history->size < MICROS_DIAGNOSTIC_RING_CAPACITY) {
        ++history->size;
    }
    ++history->next;
    if (history->next == MICROS_DIAGNOSTIC_RING_CAPACITY) {
        history->next = 0;
    }
    if (history->tail_position == MICROS_DIAGNOSTIC_TAIL_PAYLOAD) {
        for (index = 0; index < MICROS_DIAGNOSTIC_TAIL_CAPACITY - 1; ++index) {
            history->tail[index] = history->tail[index + 1];
            MICROS_TEST_WORK_HIT("diagnostic-shift-unit", MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC, MICROS_WORK_KIND_COPY_UNIT, 1);
            MICROS_TEST_WORK_HIT("diagnostic-shift-byte", MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC, MICROS_WORK_KIND_COPIED_BYTE, 1);
        }
    } else {
        ++history->tail_position;
    }
    return true;
}

bool micros_diagnostic_span(
    const struct micros_diagnostic_history *history,
    uint32_t previous,
    struct micros_diagnostic_span *span
)
{
    uint32_t next;
    uint32_t length;
    uint32_t first;

    if (
        span == NULL || previous >= MICROS_DIAGNOSTIC_RING_CAPACITY
        || !micros_diagnostic_history_valid(history)
    ) {
        return false;
    }
    next = history->next;
    length = (next + MICROS_DIAGNOSTIC_RING_CAPACITY - previous) % MICROS_DIAGNOSTIC_RING_CAPACITY;
    if (length > history->size) {
        return false;
    }
    first = MICROS_DIAGNOSTIC_RING_CAPACITY - previous;
    if (first > length) {
        first = length;
    }
    *span = (struct micros_diagnostic_span){
        .next = next, .length = length, .first_start = previous, .first_length = first,
    };
    return true;
}
