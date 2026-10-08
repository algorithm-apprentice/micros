#ifndef MICROS_KERNEL_VM_HANDOFF_TEST_FIXTURE_H
#define MICROS_KERNEL_VM_HANDOFF_TEST_FIXTURE_H

#include "kernel/bootstrap_image.h"
#include "tests/qemu/vm_handoff_protocol.h"

extern const struct micros_bootstrap_manifest
    micros_vm_handoff_test_manifest;
extern const struct micros_bootstrap_image
    micros_vm_handoff_test_images[
        MICROS_VM_HANDOFF_TEST_SERVICE_COUNT
    ];
extern const uint64_t micros_vm_handoff_test_report_address;

#endif
