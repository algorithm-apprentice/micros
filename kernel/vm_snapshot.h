#ifndef MICROS_KERNEL_VM_SNAPSHOT_H
#define MICROS_KERNEL_VM_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>

#include "kernel/bootstrap_control_internal.h"
#include "micros/vm_bootstrap.h"

struct micros_vm_snapshot_result {
    const struct micros_vm_boot_info *info;
    struct micros_vm_boot_summary summary;
    uint16_t vm_binding_index;
    uint16_t reserved;
};

enum micros_bootstrap_error micros_vm_snapshot_prepare(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_binding *bindings,
    size_t binding_count,
    struct micros_vm_snapshot_result *result
);

enum micros_bootstrap_error micros_vm_snapshot_validate_current(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_binding *bindings,
    size_t binding_count,
    const struct micros_vm_boot_info *expected
);

#endif
