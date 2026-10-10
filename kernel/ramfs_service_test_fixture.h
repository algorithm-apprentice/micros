#ifndef MICROS_KERNEL_RAMFS_SERVICE_TEST_FIXTURE_H
#define MICROS_KERNEL_RAMFS_SERVICE_TEST_FIXTURE_H

#include "kernel/bootstrap_image.h"
#include "micros/ramfs.h"
#include "micros/tty.h"
#include "tests/qemu/ramfs_service_protocol.h"

extern const struct micros_bootstrap_manifest
    micros_ramfs_service_test_manifest;
extern const struct micros_bootstrap_image
    micros_ramfs_service_test_images[
        MICROS_RAMFS_TEST_SERVICE_COUNT
    ];
extern const uint64_t micros_ramfs_service_test_report_address;

#endif
