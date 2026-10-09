#ifndef MICROS_KERNEL_TTY_SERVICE_TEST_FIXTURE_H
#define MICROS_KERNEL_TTY_SERVICE_TEST_FIXTURE_H

#include "kernel/bootstrap_image.h"
#include "micros/tty.h"
#include "tests/qemu/tty_handoff_protocol.h"

extern const struct micros_bootstrap_manifest
    micros_tty_service_test_manifest;
extern const struct micros_bootstrap_image
    micros_tty_service_test_images[
        MICROS_TTY_HANDOFF_TEST_SERVICE_COUNT
    ];
extern const uint64_t micros_tty_service_test_report_address;

#endif
