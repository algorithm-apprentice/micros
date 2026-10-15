#include "tests/host/ramfs_seed_test_internal.h"

#include <stdlib.h>
#include <string.h>

static struct ramfs_seed_test_fixture seed_fixture;

void ramfs_seed_test_write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

void ramfs_seed_test_write_u32_le(uint8_t *bytes, uint32_t value)
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

bool ramfs_seed_test_load_seed(
    struct ramfs_seed_test_fixture *fixture,
    const char *path
)
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
        || fread(fixture->seed_image, 1, (size_t)length, stream) != (size_t)length
        || fclose(stream) != 0
    ) {
        return false;
    }
    fixture->seed_image_size = (size_t)length;
    return true;
}

void ramfs_seed_test_refresh_digest(uint8_t *image, size_t size)
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
    ramfs_seed_test_write_u32_le(&image[0], MICROS_RAMFS_SEED_MAGIC);
    ramfs_seed_test_write_u16_le(&image[4], MICROS_RAMFS_SEED_VERSION);
    ramfs_seed_test_write_u16_le(&image[6], MICROS_RAMFS_SEED_HEADER_SIZE);
    ramfs_seed_test_write_u16_le(&image[8], MICROS_RAMFS_SEED_ENTRY_SIZE);
    ramfs_seed_test_write_u16_le(&image[10], MICROS_RAMFS_SEED_ENTRY_CAPACITY);
    ramfs_seed_test_write_u16_le(&image[12], entry_count);
    ramfs_seed_test_write_u32_le(&image[16], data_size);
    ramfs_seed_test_write_u32_le(&image[20], image_size);
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

    ramfs_seed_test_write_u16_le(&entry[0], parent);
    ramfs_seed_test_write_u16_le(&entry[2], (uint16_t)name_length);
    ramfs_seed_test_write_u32_le(&entry[4], mode);
    ramfs_seed_test_write_u32_le(&entry[8], data_offset);
    ramfs_seed_test_write_u32_le(&entry[12], data_size);
    memcpy(&entry[16], name, name_length);
}

size_t ramfs_seed_test_build_maximum_data_seed(uint8_t *image)
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
    ramfs_seed_test_refresh_digest(image, MICROS_RAMFS_SEED_IMAGE_MAX);
    return MICROS_RAMFS_SEED_IMAGE_MAX;
}

size_t ramfs_seed_test_build_maximum_entry_seed(uint8_t *image)
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
    ramfs_seed_test_refresh_digest(image, MICROS_RAMFS_SEED_DATA_OFFSET);
    return MICROS_RAMFS_SEED_DATA_OFFSET;
}

size_t ramfs_seed_test_build_block_exhaustion_seed(uint8_t *image)
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
    ramfs_seed_test_refresh_digest(
        image,
        MICROS_RAMFS_SEED_DATA_OFFSET + data_size
    );
    return MICROS_RAMFS_SEED_DATA_OFFSET + data_size;
}

size_t ramfs_seed_test_build_nondirectory_parent_seed(
    struct ramfs_seed_test_fixture *fixture,
    uint8_t *image
)
{
    const size_t child = (
        MICROS_RAMFS_SEED_HEADER_SIZE
        + 3 * MICROS_RAMFS_SEED_ENTRY_SIZE
    );

    memcpy(image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(&image[12], 4);
    ramfs_seed_test_write_u16_le(&image[child], 2);
    ramfs_seed_test_write_u16_le(&image[child + 2], 5);
    ramfs_seed_test_write_u32_le(
        &image[child + 4],
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0644)
    );
    ramfs_seed_test_write_u32_le(&image[child + 8], 13);
    memcpy(&image[child + 16], "child", 5);
    ramfs_seed_test_refresh_digest(image, fixture->seed_image_size);
    return fixture->seed_image_size;
}

bool ramfs_seed_test_expect_rejection(
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

struct ramfs_seed_test_fixture *ramfs_seed_test_context(void)
{
    return &seed_fixture;
}
