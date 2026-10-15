#include "tests/host/ramfs_seed_test_internal.h"

#include <stdlib.h>
#include <string.h>

bool ramfs_seed_test_production_seed_validates(
    struct ramfs_seed_test_fixture *fixture
)
{
    struct micros_ramfs_seed_plan plan;

    memset(&plan, 0xa5, sizeof(plan));
    EXPECT_TRUE(
        micros_ramfs_seed_validate(
            fixture->seed_image,
            fixture->seed_image_size,
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

bool ramfs_seed_test_exact_capacity_boundaries_validate(
    struct ramfs_seed_test_fixture *fixture
)
{
    struct micros_ramfs_seed_plan plan;
    size_t image_size;

    image_size = ramfs_seed_test_build_maximum_data_seed(fixture->mutated_image);
    EXPECT_TRUE(
        micros_ramfs_seed_validate(
            fixture->mutated_image,
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

    image_size = ramfs_seed_test_build_maximum_entry_seed(fixture->mutated_image);
    EXPECT_TRUE(
        micros_ramfs_seed_validate(
            fixture->mutated_image,
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

bool ramfs_seed_test_argument_validation_is_fail_closed(
    struct ramfs_seed_test_fixture *fixture
)
{
    EXPECT_TRUE(
        ramfs_seed_test_expect_rejection(
            NULL,
            fixture->seed_image_size,
            MICROS_RAMFS_SEED_ERROR_ARGUMENT
        )
    );
    EXPECT_TRUE(
        micros_ramfs_seed_validate(
            fixture->seed_image,
            fixture->seed_image_size,
            NULL
        ) == MICROS_RAMFS_SEED_ERROR_ARGUMENT
    );
    EXPECT_TRUE(micros_ramfs_seed_digest(NULL, fixture->seed_image_size) == 0);
    return true;
}
