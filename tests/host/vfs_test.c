#include "tests/host/vfs_core_test_fixture.h"
#include "tests/host/vfs_test_suites.h"

int main(void)
{
    static struct vfs_core_test_context context;

    vfs_core_test_context_initialize(&context);
    return (
        vfs_core_state_constants_and_initialization_run(&context)
        && vfs_core_namespace_mount_run(&context)
        && vfs_core_process_attach_share_detach_run(&context)
        && vfs_core_process_trusted_result_validation_run(
            &context
        )
        && vfs_core_process_exit_run(&context)
        && vfs_core_state_corruption_run(&context)
        && vfs_core_io_open_read_close_run(&context)
        && vfs_core_namespace_path_grant_precedence_run(
            &context
        )
        && vfs_core_io_backend_reply_validation_run(&context)
        && vfs_core_namespace_mutation_begin(&context)
        && vfs_core_io_namespace_mutation_write(&context)
        && vfs_core_namespace_mutation_continue(&context)
        && vfs_core_io_namespace_mutation_read(&context)
        && vfs_core_namespace_mutation_finish(&context)
        && vfs_core_directory_translation_run(&context)
        && vfs_core_console_async_cleanup_run(&context)
        && vfs_core_console_exit_cleanup_run(&context)
        && vfs_core_io_backend_reference_cleanup_run(&context)
        && vfs_core_console_test_drain_run(&context)
        && micros_vfs_spawn_test_run()
    ) ? 0 : 1;
}
