#ifndef MICROS_TESTS_HOST_VFS_SERVICE_TEST_SUITES_H
#define MICROS_TESTS_HOST_VFS_SERVICE_TEST_SUITES_H

#include <stdbool.h>

struct vfs_service_test_context;

bool vfs_service_public_constants_run(struct vfs_service_test_context *context);
bool vfs_service_application_protocol_run(struct vfs_service_test_context *context);
bool vfs_service_backend_protocol_run(struct vfs_service_test_context *context);
bool vfs_service_pm_control_protocol_run(struct vfs_service_test_context *context);
bool vfs_service_configuration_run(struct vfs_service_test_context *context);
bool vfs_service_dispatch_run(struct vfs_service_test_context *context);

#endif
