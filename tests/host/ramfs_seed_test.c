#include "servers/ramfs/ramfs_seed.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

static uint8_t seed_image[MICROS_RAMFS_SEED_IMAGE_MAX];
static uint8_t mutated_image[MICROS_RAMFS_SEED_IMAGE_MAX];
static size_t seed_image_size;

static void write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static bool load_seed(const char *path)
{
    FILE *stream;
    long length;

    stream = fopen(path, "rb");
    if (stream == NULL) {
        return false;
    }
    if (
        fseek(stream, 0, SEEK_END) != 0
        || (length = ftell(stream)) < 0
        || (uint64_t)length > MICROS_RAMFS_SEED_IMAGE_MAX
        || fseek(stream, 0, SEEK_SET) != 0
        || fread(seed_image, 1, (size_t)length, stream) != (size_t)length
        || fclose(stream) != 0
    ) {
        return false;
    }
    seed_image_size = (size_t)length;
    return true;
}

static void refresh_digest(uint8_t *image, size_t size)
{
    write_u64_le(
        &image[MICROS_RAMFS_SEED_DIGEST_OFFSET],
        micros_ramfs_seed_digest(image, size)
    );
}

static void initialize_header(
    uint8_t *image,
    uint16_t entry_count,
    uint32_t data_size,
    uint32_t image_size
)
{
    memset(image, 0, MICROS_RAMFS_SEED_IMAGE_MAX);
    write_u32_le(&image[0], MICROS_RAMFS_SEED_MAGIC);
    write_u16_le(&image[4], MICROS_RAMFS_SEED_VERSION);
    write_u16_le(&image[6], MICROS_RAMFS_SEED_HEADER_SIZE);
    write_u16_le(&image[8], MICROS_RAMFS_SEED_ENTRY_SIZE);
    write_u16_le(&image[10], MICROS_RAMFS_SEED_ENTRY_CAPACITY);
    write_u16_le(&image[12], entry_count);
    write_u32_le(&image[16], data_size);
    write_u32_le(&image[20], image_size);
}

static void initialize_entry(
    uint8_t *image,
    size_t index,
    uint16_t parent,
    const char *name,
    uint32_t mode,
    uint32_t data_offset,
    uint32_t data_size
)
{
    uint8_t *entry = &image[
        MICROS_RAMFS_SEED_HEADER_SIZE
        + index * MICROS_RAMFS_SEED_ENTRY_SIZE
    ];
    size_t name_length = strlen(name);

    write_u16_le(&entry[0], parent);
    write_u16_le(&entry[2], (uint16_t)name_length);
    write_u32_le(&entry[4], mode);
    write_u32_le(&entry[8], data_offset);
    write_u32_le(&entry[12], data_size);
    memcpy(&entry[16], name, name_length);
}

static size_t build_maximum_data_seed(uint8_t *image)
{
    initialize_header(
        image,
        2,
        MICROS_RAMFS_FILE_SIZE_MAX,
        MICROS_RAMFS_SEED_IMAGE_MAX
    );
    initialize_entry(
        image,
        0,
        0,
        "",
        MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
        0,
        0
    );
    initialize_entry(
        image,
        1,
        0,
        "f",
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
        0,
        MICROS_RAMFS_FILE_SIZE_MAX
    );
    refresh_digest(image, MICROS_RAMFS_SEED_IMAGE_MAX);
    return MICROS_RAMFS_SEED_IMAGE_MAX;
}

static size_t build_maximum_entry_seed(uint8_t *image)
{
    size_t index;

    initialize_header(
        image,
        MICROS_RAMFS_SEED_ENTRY_CAPACITY,
        0,
        MICROS_RAMFS_SEED_DATA_OFFSET
    );
    initialize_entry(
        image,
        0,
        0,
        "",
        MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
        0,
        0
    );
    for (index = 1; index < MICROS_RAMFS_SEED_ENTRY_CAPACITY; ++index) {
        char name[4];

        name[0] = 'd';
        name[1] = (char)('0' + (index / 10) % 10);
        name[2] = (char)('0' + index % 10);
        name[3] = '\0';
        initialize_entry(
            image,
            index,
            0,
            name,
            MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
            0,
            0
        );
    }
    refresh_digest(image, MICROS_RAMFS_SEED_DATA_OFFSET);
    return MICROS_RAMFS_SEED_DATA_OFFSET;
}

static size_t build_block_exhaustion_seed(uint8_t *image)
{
    const uint32_t data_size = 8193 + 62;
    uint32_t data_offset = 0;
    size_t index;

    initialize_header(
        image,
        MICROS_RAMFS_SEED_ENTRY_CAPACITY,
        data_size,
        MICROS_RAMFS_SEED_DATA_OFFSET + data_size
    );
    initialize_entry(
        image,
        0,
        0,
        "",
        MICROS_RAMFS_MODE_DIRECTORY | UINT32_C(0755),
        0,
        0
    );
    for (index = 1; index < MICROS_RAMFS_SEED_ENTRY_CAPACITY; ++index) {
        char name[4];
        uint32_t size = index == 1 ? 8193 : 1;

        name[0] = 'f';
        name[1] = (char)('0' + (index / 10) % 10);
        name[2] = (char)('0' + index % 10);
        name[3] = '\0';
        initialize_entry(
            image,
            index,
            0,
            name,
            MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644),
            data_offset,
            size
        );
        data_offset += size;
    }
    refresh_digest(
        image,
        MICROS_RAMFS_SEED_DATA_OFFSET + data_size
    );
    return MICROS_RAMFS_SEED_DATA_OFFSET + data_size;
}

static size_t build_nondirectory_parent_seed(uint8_t *image)
{
    const size_t child = (
        MICROS_RAMFS_SEED_HEADER_SIZE
        + 3 * MICROS_RAMFS_SEED_ENTRY_SIZE
    );

    memcpy(image, seed_image, seed_image_size);
    write_u16_le(&image[12], 4);
    write_u16_le(&image[child], 2);
    write_u16_le(&image[child + 2], 5);
    write_u32_le(
        &image[child + 4],
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644)
    );
    write_u32_le(&image[child + 8], 13);
    memcpy(&image[child + 16], "child", 5);
    refresh_digest(image, seed_image_size);
    return seed_image_size;
}

static bool expect_rejection(
    const uint8_t *image,
    size_t image_size,
    enum micros_ramfs_seed_error expected_error
)
{
    struct micros_ramfs_seed_plan expected_plan;
    struct micros_ramfs_seed_plan plan;
    enum micros_ramfs_seed_error actual_error;

    memset(&expected_plan, 0x5a, sizeof(expected_plan));
    plan = expected_plan;
    actual_error = micros_ramfs_seed_validate(
        image,
        image_size,
        &plan
    );
    if (actual_error != expected_error) {
        fprintf(
            stderr,
            "expected seed error %d, got %d\n",
            (int)expected_error,
            (int)actual_error
        );
        return false;
    }
    if (memcmp(&plan, &expected_plan, sizeof(plan)) != 0) {
        fprintf(stderr, "rejected seed modified the allocation plan\n");
        return false;
    }
    return true;
}

static bool production_seed_validates(void)
{
    struct micros_ramfs_seed_plan plan;

    memset(&plan, 0xa5, sizeof(plan));
    EXPECT_TRUE(
        micros_ramfs_seed_validate(
            seed_image,
            seed_image_size,
            &plan
        ) == MICROS_RAMFS_SEED_OK
    );
    EXPECT_TRUE(plan.entry_count == 3);
    EXPECT_TRUE(plan.allocated_block_count == 1);
    EXPECT_TRUE(plan.data_size == 13);
    EXPECT_TRUE(plan.image_size == 8269);
    EXPECT_TRUE(plan.first_block[0] == 0);
    EXPECT_TRUE(plan.block_count[0] == 0);
    EXPECT_TRUE(plan.first_block[1] == 0);
    EXPECT_TRUE(plan.block_count[1] == 0);
    EXPECT_TRUE(plan.first_block[2] == 0);
    EXPECT_TRUE(plan.block_count[2] == 1);
    return true;
}

static bool exact_capacity_boundaries_validate(void)
{
    struct micros_ramfs_seed_plan plan;
    size_t image_size;

    image_size = build_maximum_data_seed(mutated_image);
    EXPECT_TRUE(
        micros_ramfs_seed_validate(
            mutated_image,
            image_size,
            &plan
        ) == MICROS_RAMFS_SEED_OK
    );
    EXPECT_TRUE(plan.entry_count == 2);
    EXPECT_TRUE(
        plan.allocated_block_count == MICROS_RAMFS_BLOCK_CAPACITY
    );
    EXPECT_TRUE(plan.data_size == MICROS_RAMFS_FILE_SIZE_MAX);
    EXPECT_TRUE(plan.image_size == MICROS_RAMFS_SEED_IMAGE_MAX);
    EXPECT_TRUE(plan.first_block[1] == 0);
    EXPECT_TRUE(plan.block_count[1] == MICROS_RAMFS_BLOCK_CAPACITY);

    image_size = build_maximum_entry_seed(mutated_image);
    EXPECT_TRUE(
        micros_ramfs_seed_validate(
            mutated_image,
            image_size,
            &plan
        ) == MICROS_RAMFS_SEED_OK
    );
    EXPECT_TRUE(
        plan.entry_count == MICROS_RAMFS_SEED_ENTRY_CAPACITY
    );
    EXPECT_TRUE(plan.allocated_block_count == 0);
    return true;
}

static bool argument_validation_is_fail_closed(void)
{
    EXPECT_TRUE(
        expect_rejection(
            NULL,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_ARGUMENT
        )
    );
    EXPECT_TRUE(
        micros_ramfs_seed_validate(
            seed_image,
            seed_image_size,
            NULL
        ) == MICROS_RAMFS_SEED_ERROR_ARGUMENT
    );
    EXPECT_TRUE(micros_ramfs_seed_digest(NULL, seed_image_size) == 0);
    return true;
}

static bool header_corruption_is_rejected(void)
{
    EXPECT_TRUE(
        expect_rejection(
            seed_image,
            MICROS_RAMFS_SEED_DATA_OFFSET - 1,
            MICROS_RAMFS_SEED_ERROR_SIZE
        )
    );
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            MICROS_RAMFS_SEED_IMAGE_MAX + 1,
            MICROS_RAMFS_SEED_ERROR_SIZE
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    mutated_image[0] ^= UINT8_C(0x01);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_MAGIC
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u16_le(&mutated_image[4], 2);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_VERSION
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u16_le(&mutated_image[6], 63);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_LAYOUT
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u16_le(&mutated_image[8], 127);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_LAYOUT
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u16_le(&mutated_image[10], 63);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_LAYOUT
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u16_le(&mutated_image[12], 0);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_CAPACITY
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u16_le(
        &mutated_image[12],
        MICROS_RAMFS_SEED_ENTRY_CAPACITY + 1
    );
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_CAPACITY
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    mutated_image[15] = 1;
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_RESERVED
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    mutated_image[32] = 1;
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_RESERVED
        )
    );

    build_maximum_data_seed(mutated_image);
    write_u32_le(
        &mutated_image[16],
        MICROS_RAMFS_FILE_SIZE_MAX + 1
    );
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            MICROS_RAMFS_SEED_IMAGE_MAX,
            MICROS_RAMFS_SEED_ERROR_CAPACITY
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u32_le(&mutated_image[20], (uint32_t)seed_image_size + 1);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_SIZE
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u32_le(&mutated_image[16], 14);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_SIZE
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    mutated_image[seed_image_size] = 0;
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size + 1,
            MICROS_RAMFS_SEED_ERROR_SIZE
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    mutated_image[seed_image_size - 1] ^= UINT8_C(0x01);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_DIGEST
        )
    );
    return true;
}

static bool record_name_and_reserved_corruption_is_rejected(void)
{
    const size_t root = MICROS_RAMFS_SEED_HEADER_SIZE;
    const size_t etc = root + MICROS_RAMFS_SEED_ENTRY_SIZE;
    const size_t motd = etc + MICROS_RAMFS_SEED_ENTRY_SIZE;

    memcpy(mutated_image, seed_image, seed_image_size);
    mutated_image[motd + 80] = 1;
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_RESERVED
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    mutated_image[64 + 3 * 128] = 1;
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_RESERVED
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u16_le(&mutated_image[root + 2], 1);
    mutated_image[root + 16] = 'r';
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u16_le(&mutated_image[etc + 2], 0);
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u16_le(
        &mutated_image[etc + 2],
        MICROS_RAMFS_NAME_MAX + 1
    );
    memset(&mutated_image[etc + 16], 'a', MICROS_RAMFS_NAME_MAX + 1);
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    mutated_image[etc + 16] = 0;
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    mutated_image[etc + 17] = '/';
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    mutated_image[etc + 19] = 1;
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u16_le(&mutated_image[etc + 2], 1);
    memset(&mutated_image[etc + 16], 0, 64);
    mutated_image[etc + 16] = '.';
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u16_le(&mutated_image[etc + 2], 2);
    memset(&mutated_image[etc + 16], 0, 64);
    memcpy(&mutated_image[etc + 16], "..", 2);
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u16_le(&mutated_image[motd], 0);
    write_u16_le(&mutated_image[motd + 2], 3);
    memset(&mutated_image[motd + 16], 0, 64);
    memcpy(&mutated_image[motd + 16], "etc", 3);
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );
    return true;
}

static bool parent_and_mode_corruption_is_rejected(void)
{
    const size_t root = MICROS_RAMFS_SEED_HEADER_SIZE;
    const size_t etc = root + MICROS_RAMFS_SEED_ENTRY_SIZE;
    const size_t motd = etc + MICROS_RAMFS_SEED_ENTRY_SIZE;
    size_t image_size;

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u16_le(&mutated_image[root], 1);
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_PARENT
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u16_le(&mutated_image[etc], 1);
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_PARENT
        )
    );

    image_size = build_nondirectory_parent_seed(mutated_image);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            image_size,
            MICROS_RAMFS_SEED_ERROR_PARENT
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u32_le(
        &mutated_image[root + 4],
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755)
    );
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_MODE
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u32_le(&mutated_image[motd + 4], UINT32_C(0644));
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_MODE
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u32_le(
        &mutated_image[motd + 4],
        MICROS_RAMFS_MODE_REGULAR
            | UINT32_C(0644)
            | UINT32_C(0x00010000)
    );
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_MODE
        )
    );
    return true;
}

static bool data_and_capacity_corruption_is_rejected(void)
{
    const size_t root = MICROS_RAMFS_SEED_HEADER_SIZE;
    const size_t etc = root + MICROS_RAMFS_SEED_ENTRY_SIZE;
    const size_t motd = etc + MICROS_RAMFS_SEED_ENTRY_SIZE;
    size_t image_size;

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u32_le(&mutated_image[etc + 8], 1);
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_DATA
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u32_le(&mutated_image[etc + 12], 1);
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_DATA
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u32_le(&mutated_image[motd + 8], 1);
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_DATA
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    write_u32_le(&mutated_image[motd + 12], 14);
    refresh_digest(mutated_image, seed_image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size,
            MICROS_RAMFS_SEED_ERROR_DATA
        )
    );

    memcpy(mutated_image, seed_image, seed_image_size);
    mutated_image[seed_image_size] = 0;
    write_u32_le(&mutated_image[16], 14);
    write_u32_le(&mutated_image[20], (uint32_t)seed_image_size + 1);
    refresh_digest(mutated_image, seed_image_size + 1);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            seed_image_size + 1,
            MICROS_RAMFS_SEED_ERROR_DATA
        )
    );

    image_size = build_maximum_data_seed(mutated_image);
    write_u32_le(
        &mutated_image[
            MICROS_RAMFS_SEED_HEADER_SIZE
            + MICROS_RAMFS_SEED_ENTRY_SIZE
            + 12
        ],
        MICROS_RAMFS_FILE_SIZE_MAX + 1
    );
    refresh_digest(mutated_image, image_size);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            image_size,
            MICROS_RAMFS_SEED_ERROR_DATA
        )
    );

    image_size = build_block_exhaustion_seed(mutated_image);
    EXPECT_TRUE(
        expect_rejection(
            mutated_image,
            image_size,
            MICROS_RAMFS_SEED_ERROR_CAPACITY
        )
    );
    return true;
}

int main(int argc, char **argv)
{
    if (
        argc != 2
        || !load_seed(argv[1])
        || !production_seed_validates()
        || !exact_capacity_boundaries_validate()
        || !argument_validation_is_fail_closed()
        || !header_corruption_is_rejected()
        || !record_name_and_reserved_corruption_is_rejected()
        || !parent_and_mode_corruption_is_rejected()
        || !data_and_capacity_corruption_is_rejected()
    ) {
        return 1;
    }
    printf(
        "RAMFS_SEED_TEST_PASS size=%zu digest=0x%016llx\n",
        seed_image_size,
        (unsigned long long)micros_ramfs_seed_digest(
            seed_image,
            seed_image_size
        )
    );
    return 0;
}
