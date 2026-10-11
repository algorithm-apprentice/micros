#include "tests/host/vfs_spawn_test_fixture.h"
#include "tests/host/vfs_test_suites.h"

bool micros_vfs_spawn_test_run(void) {
    struct vfs_spawn_test_context context;

    vfs_spawn_test_context_initialize(&context);
    return (vfs_spawn_image_successful_prepare_release_run(&context) &&
            vfs_spawn_descriptor_prepare_abort_run(&context) &&
            vfs_spawn_publication_commit_revert_release_run(&context) &&
            vfs_spawn_publication_trusted_init_release_run(&context) &&
            vfs_spawn_descriptor_capacity_run(&context) &&
            vfs_spawn_descriptor_token_exhaustion_run(&context) &&
            vfs_spawn_descriptor_state_corruption_run(&context) &&
            vfs_spawn_descriptor_failures_run(&context) &&
            vfs_spawn_descriptor_trusted_init_run(&context) &&
            vfs_spawn_descriptor_parent_detach_run(&context) &&
            vfs_spawn_image_expected_failures_run(&context) &&
            vfs_spawn_image_transaction_failures_run(&context) &&
            vfs_spawn_image_trusted_init_bounds_run(&context) &&
            vfs_spawn_image_trusted_input_failures_run(&context) &&
            vfs_spawn_image_capacity_tokens_run(&context) &&
            vfs_spawn_image_snapshot_run(&context) && vfs_spawn_image_grant_alias_run(&context) &&
            vfs_spawn_image_state_corruption_run(&context));
}
