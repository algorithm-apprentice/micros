#ifndef MICROS_TESTS_QEMU_VFS_SERVICE_PROBE_INTERNAL_H
#define MICROS_TESTS_QEMU_VFS_SERVICE_PROBE_INTERNAL_H

#include "micros/runtime.h"
#include "micros/vfs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct probe_vfs_result {
    enum micros_vfs_result result;
    uint32_t descriptor;
    uint32_t mode;
    uint64_t transferred_count;
    uint64_t position;
};

struct vfs_service_probe_context {
    uint8_t buffer[MICROS_VFS_TRANSFER_MAX];
};

_Static_assert(sizeof(struct vfs_service_probe_context) == MICROS_VFS_TRANSFER_MAX,
               "VFS service probe context must remain one transfer page");

void probe_clear_bytes(void *storage, size_t size);
void probe_copy_bytes(uint8_t *destination, const uint8_t *source, size_t size);
bool probe_bytes_equal(const uint8_t *left, const uint8_t *right, size_t size);
bool probe_bytes_are_zero(const uint8_t *bytes, size_t size);
size_t probe_string_size(const char *string);
void probe_write_u32_le(uint8_t *bytes, uint32_t value);
void probe_write_u64_le(uint8_t *bytes, uint64_t value);
uint32_t probe_read_u32_le(const uint8_t *bytes);
uint64_t probe_read_u64_le(const uint8_t *bytes);

bool probe_call_vfs(micros_endpoint_t vfs_endpoint, struct micros_ipc_message *message,
                    uint32_t request_type,
                    struct probe_vfs_result *result);
bool probe_open(struct vfs_service_probe_context *context, micros_endpoint_t vfs_endpoint,
                const char *path, uint32_t open_flags, uint32_t mode,
                struct probe_vfs_result *result);
bool probe_path_call(struct vfs_service_probe_context *context, micros_endpoint_t vfs_endpoint,
                     uint32_t type, const char *path, uint32_t mode,
                     struct probe_vfs_result *result);
bool probe_close(micros_endpoint_t vfs_endpoint, uint32_t descriptor);
bool probe_transfer(struct vfs_service_probe_context *context, micros_endpoint_t vfs_endpoint,
                    uint32_t type, uint32_t descriptor, uint32_t count,
                    struct probe_vfs_result *result);
bool probe_write(struct vfs_service_probe_context *context, micros_endpoint_t vfs_endpoint,
                 uint32_t descriptor, const uint8_t *bytes, size_t size);
bool probe_read_exact(struct vfs_service_probe_context *context, micros_endpoint_t vfs_endpoint,
                      uint32_t descriptor, const uint8_t *expected, size_t expected_size,
                      bool require_eof);

bool probe_test_filesystem(struct vfs_service_probe_context *context,
                           micros_endpoint_t vfs_endpoint);
bool probe_enumerate_root(struct vfs_service_probe_context *context,
                          micros_endpoint_t vfs_endpoint);
bool probe_test_console(struct vfs_service_probe_context *context,
                        micros_endpoint_t vfs_endpoint);
bool probe_request_drain(micros_endpoint_t vfs_endpoint);

#endif
