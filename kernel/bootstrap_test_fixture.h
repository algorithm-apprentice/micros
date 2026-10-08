#ifndef MICROS_KERNEL_BOOTSTRAP_TEST_FIXTURE_H
#define MICROS_KERNEL_BOOTSTRAP_TEST_FIXTURE_H

#include "kernel/bootstrap_image.h"
#include "tests/qemu/bootstrap_protocol.h"

extern const struct micros_bootstrap_manifest
    micros_bootstrap_test_manifest;
extern const struct micros_bootstrap_image
    micros_bootstrap_test_images[
        MICROS_BOOTSTRAP_TEST_SERVICE_COUNT
    ];
extern const uint64_t
    micros_bootstrap_test_probe_report_address;

#endif
