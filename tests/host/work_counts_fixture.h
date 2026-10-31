#ifndef MICROS_WORK_COUNTS_FIXTURE_H
#define MICROS_WORK_COUNTS_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool work_counts_fixture_search(
    const uint8_t *values,
    size_t count,
    uint8_t target,
    uint8_t *output
);

#endif
