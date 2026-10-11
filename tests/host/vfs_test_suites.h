#ifndef MICROS_TESTS_HOST_VFS_TEST_SUITES_H
#define MICROS_TESTS_HOST_VFS_TEST_SUITES_H

#include <stdbool.h>

struct vfs_core_test_context;

bool vfs_core_state_constants_and_initialization_run(
    struct vfs_core_test_context *context
);
bool vfs_core_namespace_mount_run(
    struct vfs_core_test_context *context
);
bool vfs_core_process_attach_share_detach_run(
    struct vfs_core_test_context *context
);
bool vfs_core_process_trusted_result_validation_run(
    struct vfs_core_test_context *context
);
bool vfs_core_state_corruption_run(
    struct vfs_core_test_context *context
);
bool vfs_core_io_open_read_close_run(
    struct vfs_core_test_context *context
);
bool vfs_core_namespace_path_grant_precedence_run(
    struct vfs_core_test_context *context
);
bool vfs_core_io_backend_reply_validation_run(
    struct vfs_core_test_context *context
);
bool vfs_core_namespace_mutation_begin(
    struct vfs_core_test_context *context
);
bool vfs_core_io_namespace_mutation_write(
    struct vfs_core_test_context *context
);
bool vfs_core_namespace_mutation_continue(
    struct vfs_core_test_context *context
);
bool vfs_core_io_namespace_mutation_read(
    struct vfs_core_test_context *context
);
bool vfs_core_namespace_mutation_finish(
    struct vfs_core_test_context *context
);
bool vfs_core_directory_translation_run(
    struct vfs_core_test_context *context
);
bool vfs_core_console_async_cleanup_run(
    struct vfs_core_test_context *context
);
bool vfs_core_io_backend_reference_cleanup_run(
    struct vfs_core_test_context *context
);
bool vfs_core_console_test_drain_run(
    struct vfs_core_test_context *context
);
bool micros_vfs_spawn_test_run(void);

#endif
