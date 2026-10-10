#ifndef MICROS_SERVERS_RAMFS_SEED_H
#define MICROS_SERVERS_RAMFS_SEED_H

#include <stddef.h>
#include <stdint.h>

#include "micros/ramfs.h"

enum micros_ramfs_seed_error {
    MICROS_RAMFS_SEED_OK = 0,
    MICROS_RAMFS_SEED_ERROR_ARGUMENT,
    MICROS_RAMFS_SEED_ERROR_SIZE,
    MICROS_RAMFS_SEED_ERROR_MAGIC,
    MICROS_RAMFS_SEED_ERROR_VERSION,
    MICROS_RAMFS_SEED_ERROR_LAYOUT,
    MICROS_RAMFS_SEED_ERROR_RESERVED,
    MICROS_RAMFS_SEED_ERROR_DIGEST,
    MICROS_RAMFS_SEED_ERROR_PARENT,
    MICROS_RAMFS_SEED_ERROR_NAME,
    MICROS_RAMFS_SEED_ERROR_MODE,
    MICROS_RAMFS_SEED_ERROR_DATA,
    MICROS_RAMFS_SEED_ERROR_CAPACITY,
};

struct micros_ramfs_seed_plan {
    uint16_t entry_count;
    uint16_t allocated_block_count;
    uint32_t data_size;
    uint32_t image_size;
    uint16_t first_block[MICROS_RAMFS_NODE_CAPACITY];
    uint16_t block_count[MICROS_RAMFS_NODE_CAPACITY];
};

uint64_t micros_ramfs_seed_digest(
    const uint8_t *image,
    size_t image_size
);

enum micros_ramfs_seed_error micros_ramfs_seed_validate(
    const uint8_t *image,
    size_t image_size,
    struct micros_ramfs_seed_plan *plan
);

#endif
