#ifndef MICROS_LIB_VM_BOOT_INTERNAL_H
#define MICROS_LIB_VM_BOOT_INTERNAL_H

#include <stdbool.h>
#include "micros/vm_bootstrap.h"

bool vm_boot_header_is_valid(const struct micros_vm_boot_info *info);

/* The caller has already admitted the immutable active geometry. */
bool vm_boot_physical_to_frame_index(
    const struct micros_vm_boot_info *info,
    uint64_t physical_address,
    uint64_t *frame_index
);

#endif
