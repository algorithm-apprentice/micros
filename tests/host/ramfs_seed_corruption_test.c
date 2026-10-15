#include "tests/host/ramfs_seed_test_internal.h"

#include <stdlib.h>
#include <string.h>

static bool seed_header_size_layout_rejections(
    struct ramfs_seed_test_fixture *fixture
)
{
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->seed_image,
            MICROS_RAMFS_SEED_DATA_OFFSET - 1,
            MICROS_RAMFS_SEED_ERROR_SIZE
        )
    );
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            MICROS_RAMFS_SEED_IMAGE_MAX + 1,
            MICROS_RAMFS_SEED_ERROR_SIZE
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    fixture->mutated_image[0] ^= UINT8_C(0x01);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_MAGIC
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(&fixture->mutated_image[4], 2);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_VERSION
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(&fixture->mutated_image[6], 63);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_LAYOUT
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(&fixture->mutated_image[8], 127);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_LAYOUT
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(&fixture->mutated_image[10], 63);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_LAYOUT
        )
    );
    return true;
}

static bool seed_header_entry_metadata_rejections(
    struct ramfs_seed_test_fixture *fixture
)
{
    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(&fixture->mutated_image[12], 0);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_CAPACITY
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(
        &fixture->mutated_image[12],
        MICROS_RAMFS_SEED_ENTRY_CAPACITY + 1
    );
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_CAPACITY
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    fixture->mutated_image[15] = 1;
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_RESERVED
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    fixture->mutated_image[32] = 1;
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_RESERVED
        )
    );
    return true;
}

static bool seed_header_data_extent_rejections(
    struct ramfs_seed_test_fixture *fixture
)
{
    ramfs_seed_test_build_maximum_data_seed(fixture->mutated_image);
    ramfs_seed_test_write_u32_le(
        &fixture->mutated_image[16],
        MICROS_RAMFS_FILE_SIZE_MAX + 1
    );
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            MICROS_RAMFS_SEED_IMAGE_MAX,
            MICROS_RAMFS_SEED_ERROR_CAPACITY
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u32_le(&fixture->mutated_image[20], (uint32_t)fixture->seed_image_size + 1);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_SIZE
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u32_le(&fixture->mutated_image[16], 14);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_SIZE
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    fixture->mutated_image[fixture->seed_image_size] = 0;
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size + 1,
            MICROS_RAMFS_SEED_ERROR_SIZE
        )
    );
    return true;
}

static bool seed_header_digest_rejection(
    struct ramfs_seed_test_fixture *fixture
)
{
    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    fixture->mutated_image[fixture->seed_image_size - 1] ^= UINT8_C(0x01);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_DIGEST
        )
    );
    return true;
}

static bool seed_record_reserved_rejections(
    struct ramfs_seed_test_fixture *fixture,
    const size_t motd
)
{
    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    fixture->mutated_image[motd + 80] = 1;
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_RESERVED
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    fixture->mutated_image[64 + 3 * 128] = 1;
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_RESERVED
        )
    );
    return true;
}

static bool seed_record_name_length_rejections(
    struct ramfs_seed_test_fixture *fixture,
    const size_t root,
    const size_t etc
)
{
    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(&fixture->mutated_image[root + 2], 1);
    fixture->mutated_image[root + 16] = 'r';
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(&fixture->mutated_image[etc + 2], 0);
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(
        &fixture->mutated_image[etc + 2],
        MICROS_RAMFS_NAME_MAX + 1
    );
    memset(&fixture->mutated_image[etc + 16], 'a', MICROS_RAMFS_NAME_MAX + 1);
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );
    return true;
}

static bool seed_record_name_byte_rejections(
    struct ramfs_seed_test_fixture *fixture,
    const size_t etc
)
{
    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    fixture->mutated_image[etc + 16] = 0;
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    fixture->mutated_image[etc + 17] = '/';
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    fixture->mutated_image[etc + 19] = 1;
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );
    return true;
}

static bool seed_record_special_name_rejections(
    struct ramfs_seed_test_fixture *fixture,
    const size_t etc,
    const size_t motd
)
{
    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(&fixture->mutated_image[etc + 2], 1);
    memset(&fixture->mutated_image[etc + 16], 0, 64);
    fixture->mutated_image[etc + 16] = '.';
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(&fixture->mutated_image[etc + 2], 2);
    memset(&fixture->mutated_image[etc + 16], 0, 64);
    memcpy(&fixture->mutated_image[etc + 16], "..", 2);
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(&fixture->mutated_image[motd], 0);
    ramfs_seed_test_write_u16_le(&fixture->mutated_image[motd + 2], 3);
    memset(&fixture->mutated_image[motd + 16], 0, 64);
    memcpy(&fixture->mutated_image[motd + 16], "etc", 3);
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_NAME
        )
    );
    return true;
}

bool ramfs_seed_test_header_corruption_is_rejected(
    struct ramfs_seed_test_fixture *fixture
)
{
    return seed_header_size_layout_rejections(fixture)
        && seed_header_entry_metadata_rejections(fixture)
        && seed_header_data_extent_rejections(fixture)
        && seed_header_digest_rejection(fixture);
}

bool ramfs_seed_test_record_name_and_reserved_corruption_is_rejected(
    struct ramfs_seed_test_fixture *fixture
)
{
    const size_t root = MICROS_RAMFS_SEED_HEADER_SIZE;
    const size_t etc = root + MICROS_RAMFS_SEED_ENTRY_SIZE;
    const size_t motd = etc + MICROS_RAMFS_SEED_ENTRY_SIZE;

    return seed_record_reserved_rejections(fixture, motd)
        && seed_record_name_length_rejections(fixture, root, etc)
        && seed_record_name_byte_rejections(fixture, etc)
        && seed_record_special_name_rejections(fixture, etc, motd);
}

bool ramfs_seed_test_parent_and_mode_corruption_is_rejected(
    struct ramfs_seed_test_fixture *fixture
)
{
    const size_t root = MICROS_RAMFS_SEED_HEADER_SIZE;
    const size_t etc = root + MICROS_RAMFS_SEED_ENTRY_SIZE;
    const size_t motd = etc + MICROS_RAMFS_SEED_ENTRY_SIZE;
    size_t image_size;

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(&fixture->mutated_image[root], 1);
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_PARENT
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u16_le(&fixture->mutated_image[etc], 1);
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_PARENT
        )
    );

    image_size = ramfs_seed_test_build_nondirectory_parent_seed(fixture, fixture->mutated_image);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            image_size,
            MICROS_RAMFS_SEED_ERROR_PARENT
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u32_le(
        &fixture->mutated_image[root + 4],
        MICROS_RAMFS_MODE_REGULAR | UINT32_C(0755)
    );
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_MODE
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u32_le(&fixture->mutated_image[motd + 4], UINT32_C(0644));
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_MODE
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u32_le(
        &fixture->mutated_image[motd + 4],
        MICROS_RAMFS_MODE_REGULAR
            | UINT32_C(0644)
            | UINT32_C(0x00010000)
    );
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_MODE
        )
    );
    return true;
}

bool ramfs_seed_test_data_and_capacity_corruption_is_rejected(
    struct ramfs_seed_test_fixture *fixture
)
{
    const size_t root = MICROS_RAMFS_SEED_HEADER_SIZE;
    const size_t etc = root + MICROS_RAMFS_SEED_ENTRY_SIZE;
    const size_t motd = etc + MICROS_RAMFS_SEED_ENTRY_SIZE;
    size_t image_size;

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u32_le(&fixture->mutated_image[etc + 8], 1);
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_DATA
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u32_le(&fixture->mutated_image[etc + 12], 1);
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_DATA
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u32_le(&fixture->mutated_image[motd + 8], 1);
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_DATA
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    ramfs_seed_test_write_u32_le(&fixture->mutated_image[motd + 12], 14);
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_DATA
        )
    );

    memcpy(fixture->mutated_image, fixture->seed_image, fixture->seed_image_size);
    fixture->mutated_image[fixture->seed_image_size] = 0;
    ramfs_seed_test_write_u32_le(&fixture->mutated_image[16], 14);
    ramfs_seed_test_write_u32_le(&fixture->mutated_image[20], (uint32_t)fixture->seed_image_size + 1);
    ramfs_seed_test_refresh_digest(fixture->mutated_image, fixture->seed_image_size + 1);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            fixture->seed_image_size + 1,
            MICROS_RAMFS_SEED_ERROR_DATA
        )
    );

    image_size = ramfs_seed_test_build_maximum_data_seed(fixture->mutated_image);
    ramfs_seed_test_write_u32_le(
        &fixture->mutated_image[
            MICROS_RAMFS_SEED_HEADER_SIZE
            + MICROS_RAMFS_SEED_ENTRY_SIZE
            + 12
        ],
        MICROS_RAMFS_FILE_SIZE_MAX + 1
    );
    ramfs_seed_test_refresh_digest(fixture->mutated_image, image_size);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            image_size,
            MICROS_RAMFS_SEED_ERROR_DATA
        )
    );

    image_size = ramfs_seed_test_build_block_exhaustion_seed(fixture->mutated_image);
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            fixture->mutated_image,
            image_size,
            MICROS_RAMFS_SEED_ERROR_CAPACITY
        )
    );
    return true;
}
