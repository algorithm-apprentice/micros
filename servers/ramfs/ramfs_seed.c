#include "servers/ramfs/ramfs_seed.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint16_t seed_read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)(
        (uint16_t)bytes[0]
        | (uint16_t)bytes[1] << 8
    );
}

static uint32_t seed_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static uint64_t seed_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool seed_bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static void zero_plan(struct micros_ramfs_seed_plan *plan)
{
    uint8_t *bytes = (uint8_t *)plan;
    size_t index;

    for (index = 0; index < sizeof(*plan); ++index) {
        bytes[index] = 0;
    }
}

static const uint8_t *entry_at(
    const uint8_t *image,
    size_t index
)
{
    return &image[
        MICROS_RAMFS_SEED_HEADER_SIZE
        + index * MICROS_RAMFS_SEED_ENTRY_SIZE
    ];
}

static bool seed_mode_is_canonical(uint32_t mode)
{
    uint32_t type = mode & MICROS_RAMFS_MODE_TYPE_MASK;

    return (
        (type == MICROS_RAMFS_MODE_DIRECTORY
            || type == MICROS_RAMFS_MODE_REGULAR)
        && (
            mode
            & ~(
                MICROS_RAMFS_MODE_TYPE_MASK
                | MICROS_RAMFS_MODE_PERMISSIONS
            )
        ) == 0
    );
}

static bool entry_is_directory(const uint8_t *entry)
{
    return (
        seed_read_u32_le(&entry[4]) & MICROS_RAMFS_MODE_TYPE_MASK
    ) == MICROS_RAMFS_MODE_DIRECTORY;
}

static bool seed_names_equal(
    const uint8_t *left,
    const uint8_t *right
)
{
    uint16_t left_length = seed_read_u16_le(&left[2]);
    uint16_t right_length = seed_read_u16_le(&right[2]);
    size_t index;

    if (left_length != right_length) {
        return false;
    }
    for (index = 0; index < left_length; ++index) {
        if (left[16 + index] != right[16 + index]) {
            return false;
        }
    }
    return true;
}

uint64_t micros_ramfs_seed_digest(
    const uint8_t *image,
    size_t image_size
)
{
    uint64_t digest = MICROS_RAMFS_SEED_FNV_OFFSET;
    size_t index;

    if (image == NULL) {
        return 0;
    }
    for (index = 0; index < image_size; ++index) {
        uint8_t byte = image[index];

        if (
            index >= MICROS_RAMFS_SEED_DIGEST_OFFSET
            && index < MICROS_RAMFS_SEED_DIGEST_OFFSET + 8
        ) {
            byte = 0;
        }
        digest ^= byte;
        digest *= MICROS_RAMFS_SEED_FNV_PRIME;
    }
    return digest;
}

static enum micros_ramfs_seed_error validate_header(
    const uint8_t *image,
    size_t image_size,
    uint16_t *entry_count,
    uint32_t *data_size,
    uint32_t *declared_image_size
)
{
    uint32_t expected_size;

    if (
        image_size < MICROS_RAMFS_SEED_DATA_OFFSET
        || image_size > MICROS_RAMFS_SEED_IMAGE_MAX
    ) {
        return MICROS_RAMFS_SEED_ERROR_SIZE;
    }
    if (seed_read_u32_le(&image[0]) != MICROS_RAMFS_SEED_MAGIC) {
        return MICROS_RAMFS_SEED_ERROR_MAGIC;
    }
    if (seed_read_u16_le(&image[4]) != MICROS_RAMFS_SEED_VERSION) {
        return MICROS_RAMFS_SEED_ERROR_VERSION;
    }
    if (
        seed_read_u16_le(&image[6]) != MICROS_RAMFS_SEED_HEADER_SIZE
        || seed_read_u16_le(&image[8]) != MICROS_RAMFS_SEED_ENTRY_SIZE
        || seed_read_u16_le(&image[10])
            != MICROS_RAMFS_SEED_ENTRY_CAPACITY
    ) {
        return MICROS_RAMFS_SEED_ERROR_LAYOUT;
    }

    *entry_count = seed_read_u16_le(&image[12]);
    if (
        *entry_count == 0
        || *entry_count > MICROS_RAMFS_SEED_ENTRY_CAPACITY
    ) {
        return MICROS_RAMFS_SEED_ERROR_CAPACITY;
    }
    if (
        seed_read_u16_le(&image[14]) != 0
        || !seed_bytes_are_zero(&image[32], 32)
    ) {
        return MICROS_RAMFS_SEED_ERROR_RESERVED;
    }

    *data_size = seed_read_u32_le(&image[16]);
    *declared_image_size = seed_read_u32_le(&image[20]);
    if (*data_size > MICROS_RAMFS_FILE_SIZE_MAX) {
        return MICROS_RAMFS_SEED_ERROR_CAPACITY;
    }
    expected_size = MICROS_RAMFS_SEED_DATA_OFFSET + *data_size;
    if (
        *declared_image_size != expected_size
        || image_size != expected_size
    ) {
        return MICROS_RAMFS_SEED_ERROR_SIZE;
    }
    if (
        seed_read_u64_le(&image[MICROS_RAMFS_SEED_DIGEST_OFFSET])
        != micros_ramfs_seed_digest(image, image_size)
    ) {
        return MICROS_RAMFS_SEED_ERROR_DIGEST;
    }
    return MICROS_RAMFS_SEED_OK;
}

static enum micros_ramfs_seed_error validate_name(
    const uint8_t *entry,
    bool root
)
{
    uint16_t length = seed_read_u16_le(&entry[2]);
    size_t index;

    if (root) {
        if (
            length != 0
            || !seed_bytes_are_zero(&entry[16], 64)
        ) {
            return MICROS_RAMFS_SEED_ERROR_NAME;
        }
        return MICROS_RAMFS_SEED_OK;
    }
    if (length == 0 || length > MICROS_RAMFS_NAME_MAX) {
        return MICROS_RAMFS_SEED_ERROR_NAME;
    }
    for (index = 0; index < length; ++index) {
        if (entry[16 + index] == 0 || entry[16 + index] == '/') {
            return MICROS_RAMFS_SEED_ERROR_NAME;
        }
    }
    if (
        (length == 1 && entry[16] == '.')
        || (
            length == 2
            && entry[16] == '.'
            && entry[17] == '.'
        )
    ) {
        return MICROS_RAMFS_SEED_ERROR_NAME;
    }
    if (
        !seed_bytes_are_zero(
            &entry[16 + length],
            64 - length
        )
    ) {
        return MICROS_RAMFS_SEED_ERROR_NAME;
    }
    return MICROS_RAMFS_SEED_OK;
}

static bool has_duplicate_name(
    const uint8_t *image,
    size_t current_index
)
{
    const uint8_t *current = entry_at(image, current_index);
    uint16_t parent = seed_read_u16_le(&current[0]);
    size_t index;

    for (index = 1; index < current_index; ++index) {
        const uint8_t *candidate = entry_at(image, index);

        if (
            seed_read_u16_le(&candidate[0]) == parent
            && seed_names_equal(current, candidate)
        ) {
            return true;
        }
    }
    return false;
}

static enum micros_ramfs_seed_error validate_active_entries(
    const uint8_t *image,
    uint16_t entry_count,
    uint32_t data_size,
    struct micros_ramfs_seed_plan *candidate
)
{
    uint32_t consumed_data = 0;
    uint16_t allocated_blocks = 0;
    size_t index;

    for (index = 0; index < entry_count; ++index) {
        const uint8_t *entry = entry_at(image, index);
        uint16_t parent = seed_read_u16_le(&entry[0]);
        uint32_t mode = seed_read_u32_le(&entry[4]);
        uint32_t entry_data_offset = seed_read_u32_le(&entry[8]);
        uint32_t entry_data_size = seed_read_u32_le(&entry[12]);
        uint32_t type = mode & MICROS_RAMFS_MODE_TYPE_MASK;
        uint32_t blocks;
        enum micros_ramfs_seed_error error;

        if (!seed_bytes_are_zero(&entry[80], 48)) {
            return MICROS_RAMFS_SEED_ERROR_RESERVED;
        }
        if (!seed_mode_is_canonical(mode)) {
            return MICROS_RAMFS_SEED_ERROR_MODE;
        }
        error = validate_name(entry, index == 0);
        if (error != MICROS_RAMFS_SEED_OK) {
            return error;
        }

        if (index == 0) {
            if (parent != 0) {
                return MICROS_RAMFS_SEED_ERROR_PARENT;
            }
            if (type != MICROS_RAMFS_MODE_DIRECTORY) {
                return MICROS_RAMFS_SEED_ERROR_MODE;
            }
        } else {
            if (
                parent >= index
                || !entry_is_directory(entry_at(image, parent))
            ) {
                return MICROS_RAMFS_SEED_ERROR_PARENT;
            }
            if (has_duplicate_name(image, index)) {
                return MICROS_RAMFS_SEED_ERROR_NAME;
            }
        }

        if (type == MICROS_RAMFS_MODE_DIRECTORY) {
            if (entry_data_offset != 0 || entry_data_size != 0) {
                return MICROS_RAMFS_SEED_ERROR_DATA;
            }
            continue;
        }
        if (
            entry_data_offset != consumed_data
            || entry_data_size > MICROS_RAMFS_FILE_SIZE_MAX
            || entry_data_size > data_size - consumed_data
        ) {
            return MICROS_RAMFS_SEED_ERROR_DATA;
        }
        blocks = (
            entry_data_size + MICROS_RAMFS_BLOCK_SIZE - 1
        ) / MICROS_RAMFS_BLOCK_SIZE;
        if (
            blocks > MICROS_RAMFS_BLOCK_CAPACITY - allocated_blocks
        ) {
            return MICROS_RAMFS_SEED_ERROR_CAPACITY;
        }
        candidate->first_block[index] = allocated_blocks;
        candidate->block_count[index] = (uint16_t)blocks;
        allocated_blocks = (uint16_t)(allocated_blocks + blocks);
        consumed_data += entry_data_size;
    }
    if (consumed_data != data_size) {
        return MICROS_RAMFS_SEED_ERROR_DATA;
    }
    candidate->allocated_block_count = allocated_blocks;
    return MICROS_RAMFS_SEED_OK;
}

enum micros_ramfs_seed_error micros_ramfs_seed_validate(
    const uint8_t *image,
    size_t image_size,
    struct micros_ramfs_seed_plan *plan
)
{
    struct micros_ramfs_seed_plan candidate;
    enum micros_ramfs_seed_error error;
    uint16_t entry_count;
    uint32_t data_size;
    uint32_t declared_image_size;
    size_t inactive_offset;
    size_t inactive_size;

    if (image == NULL || plan == NULL) {
        return MICROS_RAMFS_SEED_ERROR_ARGUMENT;
    }
    error = validate_header(
        image,
        image_size,
        &entry_count,
        &data_size,
        &declared_image_size
    );
    if (error != MICROS_RAMFS_SEED_OK) {
        return error;
    }

    inactive_offset = (
        MICROS_RAMFS_SEED_HEADER_SIZE
        + (size_t)entry_count * MICROS_RAMFS_SEED_ENTRY_SIZE
    );
    inactive_size = (
        (MICROS_RAMFS_SEED_ENTRY_CAPACITY - (size_t)entry_count)
        * MICROS_RAMFS_SEED_ENTRY_SIZE
    );
    if (
        !seed_bytes_are_zero(
            &image[inactive_offset],
            inactive_size
        )
    ) {
        return MICROS_RAMFS_SEED_ERROR_RESERVED;
    }

    zero_plan(&candidate);
    candidate.entry_count = entry_count;
    candidate.data_size = data_size;
    candidate.image_size = declared_image_size;
    error = validate_active_entries(
        image,
        entry_count,
        data_size,
        &candidate
    );
    if (error != MICROS_RAMFS_SEED_OK) {
        return error;
    }
    *plan = candidate;
    return MICROS_RAMFS_SEED_OK;
}
