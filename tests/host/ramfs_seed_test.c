#include "tests/host/ramfs_seed_test_internal.h"

#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    struct ramfs_seed_test_fixture *fixture = ramfs_seed_test_context();

    if (
        argc != 2
        || !ramfs_seed_test_load_seed(fixture, argv[1])
        || !ramfs_seed_test_production_seed_validates(fixture)
        || !ramfs_seed_test_exact_capacity_boundaries_validate(fixture)
        || !ramfs_seed_test_argument_validation_is_fail_closed(fixture)
        || !ramfs_seed_test_header_corruption_is_rejected(fixture)
        || !ramfs_seed_test_record_name_and_reserved_corruption_is_rejected(fixture)
        || !ramfs_seed_test_parent_and_mode_corruption_is_rejected(fixture)
        || !ramfs_seed_test_data_and_capacity_corruption_is_rejected(fixture)
    ) {
        return 1;
    }
    printf(
        "RAMFS_SEED_TEST_PASS size=%zu digest=0x%016llx\n",
        fixture->seed_image_size,
        (unsigned long long)micros_ramfs_seed_digest(
            fixture->seed_image,
            fixture->seed_image_size
        )
    );
    return 0;
}
