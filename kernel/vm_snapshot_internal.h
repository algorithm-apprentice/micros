#ifndef MICROS_KERNEL_VM_SNAPSHOT_INTERNAL_H
#define MICROS_KERNEL_VM_SNAPSHOT_INTERNAL_H

#include <stdbool.h>

#include "kernel/vm_snapshot.h"

bool micros_vm_snapshot_binding_matches_manifest(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_binding *binding
);

#endif
