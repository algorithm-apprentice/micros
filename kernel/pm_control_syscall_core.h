#ifndef MICROS_KERNEL_PM_CONTROL_SYSCALL_CORE_H
#define MICROS_KERNEL_PM_CONTROL_SYSCALL_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/bootstrap_control_internal.h"
#include "kernel/pm_control_core.h"
#include "kernel/syscall.h"
#include "kernel/vm_handoff_core.h"
#include "micros/bootstrap.h"
#include "micros/endpoint.h"
#include "micros/frame_ownership.h"
#include "micros/pm.h"

enum {
    MICROS_PM_CONTROL_OUTPUT_CHUNK_CAPACITY = 2,
};

enum micros_pm_control_output_error {
    MICROS_PM_CONTROL_OUTPUT_OK = 0,
    MICROS_PM_CONTROL_OUTPUT_ERROR_MEMORY_FAULT,
    MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT,
};

enum micros_pm_control_authority_result {
    MICROS_PM_CONTROL_AUTHORITY_AUTHORIZED = 0,
    MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED,
    MICROS_PM_CONTROL_AUTHORITY_INVARIANT,
};

struct micros_pm_control_output_chunk {
    uintptr_t physical_address;
    size_t size;
};

struct micros_pm_control_output_plan {
    size_t chunk_count;
    struct micros_pm_control_output_chunk
        chunks[MICROS_PM_CONTROL_OUTPUT_CHUNK_CAPACITY];
};

typedef enum micros_pm_control_output_error
(*micros_pm_control_output_translate)(
    void *context,
    uint64_t user_address,
    size_t requested_size,
    uintptr_t *physical_address,
    size_t *contiguous_size
);

bool micros_pm_control_syscall_phase_is_ready(
    enum micros_bootstrap_phase bootstrap_phase,
    enum micros_vm_handoff_phase vm_handoff_phase,
    enum micros_frame_ownership_phase ownership_phase
);

enum micros_pm_control_authority_result
micros_pm_control_syscall_caller_classify(
    const struct micros_pm_control_state *state,
    const struct micros_syscall_context *context,
    const struct micros_process *caller_process,
    const struct micros_thread *caller_thread
);

enum micros_pm_control_authority_result
micros_pm_control_syscall_authority_classify(
    const struct micros_pm_control_state *state,
    size_t bootstrap_entry_count,
    uint32_t planned_pm_service_id,
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_manifest_entry *entry,
    const struct micros_syscall_context *context,
    const struct micros_process *caller_process,
    const struct micros_thread *caller_thread,
    const struct micros_endpoint_record *endpoint,
    const struct micros_privilege_profile *profile
);

enum micros_pm_control_authority_result
micros_pm_control_syscall_authority_resolve(
    const struct micros_pm_control_state *state,
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_syscall_context *context
);

enum micros_pm_control_output_error
micros_pm_control_output_prepare(
    uint64_t user_address,
    micros_pm_control_output_translate translate,
    void *translate_context,
    struct micros_pm_control_output_plan *plan
);

void micros_pm_control_output_commit_prevalidated(
    const struct micros_pm_control_output_plan *plan,
    const struct micros_pm_reservation_result *result
);

#endif
