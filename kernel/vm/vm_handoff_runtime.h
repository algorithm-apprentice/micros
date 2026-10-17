#ifndef MICROS_KERNEL_VM_HANDOFF_RUNTIME_H
#define MICROS_KERNEL_VM_HANDOFF_RUNTIME_H

#include <stdbool.h>

#include "kernel/boot/bootstrap_control_internal.h"
#include "kernel/boot/bootstrap_image.h"
#include "kernel/vm/vm_handoff_core.h"
#include "kernel/vm/vm_snapshot.h"
#include "micros/user_address_space_core.h"

enum micros_vm_scratch_authority_status {
    MICROS_VM_SCRATCH_AUTHORITY_INVALID = 0,
    MICROS_VM_SCRATCH_AUTHORITY_NONE,
    MICROS_VM_SCRATCH_AUTHORITY_ACTIVE,
};

enum micros_vm_handoff_error micros_vm_handoff_runtime_prepare(
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_manifest_entry *entry,
    const struct micros_bootstrap_image *image,
    const struct micros_vm_snapshot_result *snapshot
);

enum micros_vm_handoff_error micros_vm_handoff_runtime_reset(void);

const struct micros_vm_handoff_state *
micros_vm_handoff_runtime_state(void);

struct micros_vm_handoff_state *
micros_vm_handoff_runtime_authoritative_state(void);

enum micros_vm_handoff_error micros_vm_handoff_runtime_validate(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
);

bool micros_vm_handoff_runtime_role_ready(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    uint32_t service_id,
    struct micros_process_handle process,
    micros_endpoint_t endpoint
);

enum micros_vm_scratch_authority_status
micros_vm_handoff_runtime_scratch_authority(
    struct micros_user_address_space_vm_scratch_authority
        *authority
);

#endif
