#ifndef MICROS_KERNEL_PM_SERVICE_TEST_FIXTURE_H
#define MICROS_KERNEL_PM_SERVICE_TEST_FIXTURE_H

#include "kernel/bootstrap_image.h"
#include "tests/qemu/pm_service_protocol.h"

extern const struct micros_bootstrap_manifest
    micros_pm_service_test_manifest;
extern const struct micros_bootstrap_image
    micros_pm_service_test_images[MICROS_PM_TEST_SERVICE_COUNT];
extern const uint64_t micros_pm_service_test_report_address;

#endif
