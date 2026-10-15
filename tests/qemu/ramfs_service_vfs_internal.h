#ifndef MICROS_TESTS_QEMU_RAMFS_SERVICE_VFS_INTERNAL_H
#define MICROS_TESTS_QEMU_RAMFS_SERVICE_VFS_INTERNAL_H

#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap_control.h"
#include "micros/ramfs.h"
#include "micros/tty.h"
#include "tests/qemu/ramfs_service_protocol.h"

struct vfs_ramfs_result {
    enum micros_ramfs_result result;
    uint64_t node;
    uint64_t file_size;
    uint64_t position;
    uint32_t count;
    uint32_t mode;
};

struct vfs_tty_result {
    enum micros_tty_result result;
    uint64_t request_id;
    uint64_t transferred_count;
};

struct vfs_service_fixture {
    uint8_t vfs_ramfs_buffer[MICROS_RAMFS_TRANSFER_MAX];
    uint8_t vfs_tty_buffer[256];
    micros_endpoint_t vfs_ramfs_endpoint;
    micros_endpoint_t vfs_tty_endpoint;
};

void vfs_clear_bytes(void *storage, size_t size);

void vfs_fill_bytes(uint8_t *bytes, uint8_t value, size_t size);

void vfs_copy_bytes(
    uint8_t *destination,
    const uint8_t *source,
    size_t size
);

bool vfs_bytes_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size
);

bool vfs_bytes_are_zero(const uint8_t *bytes, size_t size);

void vfs_write_u32_le(uint8_t *bytes, uint32_t value);

void vfs_write_u64_le(uint8_t *bytes, uint64_t value);

uint32_t vfs_read_u32_le(const uint8_t *bytes);

uint64_t vfs_read_u64_le(const uint8_t *bytes);

bool vfs_validate_configuration(
    struct vfs_service_fixture *fixture
);

bool vfs_send_ready(void);

bool vfs_result_has_no_fields(
    const struct vfs_ramfs_result *result
);

bool vfs_mount(
    struct vfs_service_fixture *fixture,
    uint64_t *root
);

bool vfs_lookup(
    struct vfs_service_fixture *fixture,
    uint64_t start,
    uint64_t root,
    const uint8_t *path,
    size_t length,
    struct vfs_ramfs_result *result
);

bool vfs_create_node(
    struct vfs_service_fixture *fixture,
    uint32_t type,
    uint64_t parent,
    uint32_t mode,
    const uint8_t *name,
    size_t length,
    struct vfs_ramfs_result *result
);

bool vfs_transfer(
    struct vfs_service_fixture *fixture,
    uint32_t type,
    uint64_t node,
    uint64_t file_offset,
    uint32_t count,
    uint32_t permissions,
    struct vfs_ramfs_result *result
);

bool vfs_transfer_without_grant(
    struct vfs_service_fixture *fixture,
    uint32_t type,
    uint64_t node,
    uint64_t file_offset,
    uint32_t count,
    struct vfs_ramfs_result *result
);

bool vfs_getdent(
    struct vfs_service_fixture *fixture,
    uint64_t node,
    uint64_t cursor,
    struct vfs_ramfs_result *result
);

bool vfs_putnode(
    struct vfs_service_fixture *fixture,
    uint64_t node,
    uint32_t count,
    enum micros_ramfs_result expected
);

bool vfs_malformed_version_is_exact(
    struct vfs_service_fixture *fixture,
    uint64_t root
);

bool vfs_run_filesystem_scenario(
    struct vfs_service_fixture *fixture
);

bool vfs_write_pass_marker(
    struct vfs_service_fixture *fixture
);

struct vfs_service_fixture *vfs_service_context(void);

uint64_t vfs_initialized_data_value(void);

void micros_ramfs_service_test_report(uint64_t magic);

#endif
