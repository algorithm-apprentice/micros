#ifndef MICROS_TESTS_HOST_RAMFS_SEED_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_RAMFS_SEED_TEST_INTERNAL_H

#include "servers/ramfs/ramfs_seed.h"

#include <stdbool.h>
#include <stdio.h>

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

struct ramfs_seed_test_fixture {
    uint8_t seed_image[MICROS_RAMFS_SEED_IMAGE_MAX];
    uint8_t mutated_image[MICROS_RAMFS_SEED_IMAGE_MAX];
    size_t seed_image_size;
};

void ramfs_seed_test_write_u16_le(uint8_t *bytes, uint16_t value);

void ramfs_seed_test_write_u32_le(uint8_t *bytes, uint32_t value);

bool ramfs_seed_test_load_seed(
    struct ramfs_seed_test_fixture *fixture,
    const char *path
);

void ramfs_seed_test_refresh_digest(uint8_t *image, size_t size);

size_t ramfs_seed_test_build_maximum_data_seed(uint8_t *image);

size_t ramfs_seed_test_build_maximum_entry_seed(uint8_t *image);

size_t ramfs_seed_test_build_block_exhaustion_seed(uint8_t *image);

size_t ramfs_seed_test_build_nondirectory_parent_seed(
    struct ramfs_seed_test_fixture *fixture,
    uint8_t *image
);

bool ramfs_seed_test_expect_rejection(
    const uint8_t *image,
    size_t image_size,
    enum micros_ramfs_seed_error expected_error
);

bool ramfs_seed_test_production_seed_validates(
    struct ramfs_seed_test_fixture *fixture
);

bool ramfs_seed_test_exact_capacity_boundaries_validate(
    struct ramfs_seed_test_fixture *fixture
);

bool ramfs_seed_test_argument_validation_is_fail_closed(
    struct ramfs_seed_test_fixture *fixture
);

bool ramfs_seed_test_header_corruption_is_rejected(
    struct ramfs_seed_test_fixture *fixture
);

bool ramfs_seed_test_record_name_and_reserved_corruption_is_rejected(
    struct ramfs_seed_test_fixture *fixture
);

bool ramfs_seed_test_parent_and_mode_corruption_is_rejected(
    struct ramfs_seed_test_fixture *fixture
);

bool ramfs_seed_test_data_and_capacity_corruption_is_rejected(
    struct ramfs_seed_test_fixture *fixture
);

struct ramfs_seed_test_fixture *ramfs_seed_test_context(void);

#endif
