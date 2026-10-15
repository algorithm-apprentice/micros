#include "tests/host/ramfs_test_internal.h"

#include <string.h>

int main(int argc, char **argv)
{
    struct ramfs_test_fixture *fixture = ramfs_test_context();

    if (
        argc != 2
        || !ramfs_test_load_seed(fixture, argv[1])
        || !ramfs_test_seed_state_is_exact_and_invariants_fail_closed(fixture)
        || !ramfs_test_protocol_mount_and_result_layout_are_exact(fixture)
        || !ramfs_test_lookup_semantics_and_reference_limits_are_exact(fixture)
        || !ramfs_test_handles_create_mkdir_and_capacity_are_exact(fixture)
        || !ramfs_test_read_write_sparse_and_block_capacity_are_exact(fixture)
        || !ramfs_test_getdents_cursor_and_records_are_exact(fixture)
        || !ramfs_test_putnode_and_handle_validation_are_exact(fixture)
    ) {
        return 1;
    }
    puts("RAMFS_CORE_TEST_PASS");
    return 0;
}
