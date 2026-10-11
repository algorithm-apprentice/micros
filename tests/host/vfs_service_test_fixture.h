#ifndef MICROS_TESTS_HOST_VFS_SERVICE_TEST_FIXTURE_H
#define MICROS_TESTS_HOST_VFS_SERVICE_TEST_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap.h"
#include "micros/ipc.h"
#include "servers/vfs/vfs_core.h"
#include "tests/host/vfs_test_fixture.h"

struct vfs_service_test_context {
    struct micros_vfs_state state;
    struct vfs_test_fixture fixture;
    micros_endpoint_t vfs_endpoint;
    micros_endpoint_t vm_endpoint;
    micros_endpoint_t ramfs_endpoint;
    micros_endpoint_t tty_endpoint;
    micros_endpoint_t pm_endpoint;
    micros_endpoint_t application_endpoint;
    micros_endpoint_t spawn_child_endpoint;
};

void vfs_service_test_context_initialize(struct vfs_service_test_context *context);

void vfs_service_test_write_u32_le(uint8_t *bytes, uint32_t value);

void vfs_service_test_write_u64_le(uint8_t *bytes, uint64_t value);

uint32_t vfs_service_test_read_u32_le(const uint8_t *bytes);

uint64_t vfs_service_test_read_u64_le(const uint8_t *bytes);

int32_t vfs_service_test_read_i32_le(const uint8_t *bytes);

size_t vfs_service_test_build_control_spawn_request(uint8_t *bytes, const char *path);

struct micros_ipc_message
vfs_service_test_request_message(const struct vfs_service_test_context *context, uint32_t type);

struct micros_ipc_message
vfs_service_test_pm_control_message(const struct vfs_service_test_context *context,
                                    enum micros_vfs_control_command command);

struct micros_bootstrap_service_config vfs_service_test_production_config(void);

struct micros_bootstrap_service_config
vfs_service_test_config(const struct vfs_service_test_context *context);

bool vfs_service_test_initialize_fixture(struct vfs_service_test_context *context);

#endif
