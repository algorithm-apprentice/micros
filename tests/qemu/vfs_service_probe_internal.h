#ifndef MICROS_TESTS_QEMU_VFS_SERVICE_PROBE_INTERNAL_H
#define MICROS_TESTS_QEMU_VFS_SERVICE_PROBE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/runtime.h"
#include "micros/vfs.h"

struct probe_vfs_result {
    enum micros_vfs_result result;
    uint32_t descriptor;
    uint32_t mode;
    uint64_t transferred_count;
    uint64_t position;
};

struct vfs_service_probe_context {
    micros_endpoint_t launcher_endpoint;
    micros_endpoint_t vfs_endpoint;
    uint8_t *buffer;
    size_t buffer_size;
};

#endif
