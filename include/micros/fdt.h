#ifndef MICROS_FDT_H
#define MICROS_FDT_H

#include <stddef.h>
#include <stdint.h>

enum {
    MICROS_FDT_MAX_BLOB_SIZE = 1024 * 1024,
    MICROS_FDT_MAX_DEPTH = 32,
    MICROS_FDT_MAX_MEMORY_RANGES = 16,
    MICROS_FDT_MAX_RESERVATION_RANGES = 32,
    MICROS_FDT_MAX_RESERVED_MEMORY_RANGES = 32,
};

struct micros_fdt_range {
    uint64_t base;
    uint64_t size;
};

struct micros_fdt_memory_map {
    uint32_t blob_size;
    uint32_t address_cells;
    uint32_t size_cells;
    size_t memory_range_count;
    size_t reservation_range_count;
    size_t reserved_memory_range_count;
    struct micros_fdt_range memory_ranges[MICROS_FDT_MAX_MEMORY_RANGES];
    struct micros_fdt_range
        reservation_ranges[MICROS_FDT_MAX_RESERVATION_RANGES];
    struct micros_fdt_range
        reserved_memory_ranges[MICROS_FDT_MAX_RESERVED_MEMORY_RANGES];
};

enum micros_fdt_error {
    MICROS_FDT_OK = 0,
    MICROS_FDT_ERROR_ARGUMENT,
    MICROS_FDT_ERROR_TRUNCATED,
    MICROS_FDT_ERROR_MAGIC,
    MICROS_FDT_ERROR_VERSION,
    MICROS_FDT_ERROR_LAYOUT,
    MICROS_FDT_ERROR_STRUCTURE,
    MICROS_FDT_ERROR_STRING,
    MICROS_FDT_ERROR_CELLS,
    MICROS_FDT_ERROR_PROPERTY,
    MICROS_FDT_ERROR_RANGE,
    MICROS_FDT_ERROR_CAPACITY,
    MICROS_FDT_ERROR_DEPTH,
    MICROS_FDT_ERROR_NO_MEMORY,
};

enum micros_fdt_error micros_fdt_parse_memory_map(
    const void *blob,
    size_t available_size,
    struct micros_fdt_memory_map *memory_map
);

const char *micros_fdt_error_name(enum micros_fdt_error error);

#endif
