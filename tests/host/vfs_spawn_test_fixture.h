#ifndef MICROS_TESTS_HOST_VFS_SPAWN_TEST_FIXTURE_H
#define MICROS_TESTS_HOST_VFS_SPAWN_TEST_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/spawn/spawn_request.h"
#include "micros/spawn.h"
#include "servers/vfs/vfs_core.h"
#include "tests/host/vfs_test_fixture.h"

struct vfs_spawn_test_context {
    struct micros_vfs_state state;
    struct micros_vfs_state snapshot;
    struct vfs_test_fixture fixture;
    micros_endpoint_t vfs_endpoint;
    micros_endpoint_t vm_endpoint;
    micros_endpoint_t pm_endpoint;
    micros_endpoint_t ramfs_endpoint;
    micros_endpoint_t tty_endpoint;
    micros_endpoint_t parent_endpoint;
    uint8_t request_bytes[MICROS_SPAWN_REQUEST_SIZE_MAX];
    uint8_t executable_bytes[MICROS_APPLICATION_EXECUTABLE_SIZE_MAX + 1];
};

void vfs_spawn_test_context_initialize(struct vfs_spawn_test_context *context);

void vfs_spawn_test_write_u16_le(uint8_t *bytes, uint16_t value);

void vfs_spawn_test_write_u32_le(uint8_t *bytes, uint32_t value);

bool vfs_spawn_test_initialize_fixture(struct vfs_spawn_test_context *context);

bool vfs_spawn_test_bytes_are_zero(const void *storage, size_t size);

size_t vfs_spawn_test_build_request(struct vfs_spawn_test_context *context, const char *path);

void vfs_spawn_test_set_descriptor_actions(struct vfs_spawn_test_context *context,
                                           const struct micros_spawn_descriptor_action *actions,
                                           size_t action_count);

size_t vfs_spawn_test_build_oversized_stack_request(struct vfs_spawn_test_context *context,
                                                    const char *path);

struct micros_vfs_control_request
vfs_spawn_test_prepare_request(struct vfs_spawn_test_context *context, size_t request_size,
                               uint64_t spawn_token);

struct micros_vfs_control_request
vfs_spawn_test_release_request(const struct vfs_spawn_test_context *context, uint64_t spawn_token,
                               uint64_t image_token);

struct micros_vfs_control_request
vfs_spawn_test_prepare_descriptors_request(const struct vfs_spawn_test_context *context,
                                           uint64_t spawn_token, uint64_t image_token);

struct micros_vfs_control_request
vfs_spawn_test_abort_descriptors_request(const struct vfs_spawn_test_context *context,
                                         uint64_t spawn_token, uint64_t image_token,
                                         uint64_t descriptor_token);

struct micros_vfs_control_request
vfs_spawn_test_hidden_request(const struct vfs_spawn_test_context *context,
                              enum micros_vfs_control_command command, uint64_t spawn_token,
                              uint64_t image_token, uint64_t descriptor_token,
                              micros_endpoint_t child_endpoint);

void vfs_spawn_test_fill_executable(struct vfs_spawn_test_context *context, size_t size);

size_t vfs_spawn_test_active_process_count(const struct vfs_spawn_test_context *context);

size_t vfs_spawn_test_active_console_open_file_count(const struct vfs_spawn_test_context *context,
                                                     uint32_t access);

bool vfs_spawn_image_successful_prepare_release_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_descriptor_prepare_abort_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_publication_commit_revert_release_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_publication_trusted_init_release_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_descriptor_capacity_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_descriptor_token_exhaustion_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_descriptor_state_corruption_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_descriptor_failures_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_descriptor_trusted_init_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_descriptor_parent_detach_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_image_expected_failures_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_image_transaction_failures_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_image_trusted_init_bounds_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_image_trusted_input_failures_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_image_capacity_tokens_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_image_snapshot_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_image_grant_alias_run(struct vfs_spawn_test_context *context);
bool vfs_spawn_image_state_corruption_run(struct vfs_spawn_test_context *context);

#endif
