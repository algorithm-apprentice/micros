#ifndef MICROS_KERNEL_DIAGNOSTIC_HISTORY_CORE_H
#define MICROS_KERNEL_DIAGNOSTIC_HISTORY_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/diagnostic.h"

struct micros_diagnostic_history {
    uint8_t ring[MICROS_DIAGNOSTIC_RING_CAPACITY];
    uint8_t tail[MICROS_DIAGNOSTIC_TAIL_CAPACITY];
    uint32_t next;
    uint32_t size;
    uint32_t tail_position;
};

struct micros_diagnostic_span {
    uint32_t next;
    uint32_t length;
    uint32_t first_start;
    uint32_t first_length;
};

bool micros_diagnostic_history_valid(const struct micros_diagnostic_history *history);
bool micros_diagnostic_append(struct micros_diagnostic_history *history, uint8_t byte);
bool micros_diagnostic_span(
    const struct micros_diagnostic_history *history,
    uint32_t previous,
    struct micros_diagnostic_span *span
);

#endif
