#ifndef MICROS_KERNEL_BOOTSTRAP_CONTROL_INTERNAL_H
#define MICROS_KERNEL_BOOTSTRAP_CONTROL_INTERNAL_H

#include "micros/bootstrap_control.h"
#include "micros/grant.h"
#include "micros/ipc.h"

struct micros_bootstrap_binding {
    uint16_t manifest_index;
    uint16_t reserved;
    uint32_t service_id;
    struct micros_process_handle process;
    struct micros_thread_handle thread;
    uintptr_t root;
    micros_endpoint_t endpoint;
    uint32_t prepared_page_count;
    uint8_t scheduler_priority;
    bool scheduler_preemptible;
    uint16_t reserved2;
    uint64_t scheduler_quantum_counter_ticks;
};

struct micros_bootstrap_control_state {
    enum micros_bootstrap_phase phase;
    uint16_t entry_count;
    uint16_t reserved;
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_manifest_plan plan;
    struct micros_bootstrap_runtime transitions;
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct micros_process_handle controller_process;
    struct micros_thread_handle controller_thread;
    micros_endpoint_t controller_endpoint;
};

struct micros_bootstrap_ready_plan {
    bool active;
    uint16_t binding_index;
    uint16_t reserved;
    struct micros_bootstrap_runtime transitions;
    struct micros_ipc_message acknowledgment;
};

struct micros_bootstrap_complete_plan {
    bool active;
    uint16_t controller_binding_index;
    uint16_t reserved;
    struct micros_bootstrap_runtime transitions;
};

enum micros_bootstrap_error micros_bootstrap_control_state_prepare(
    struct micros_bootstrap_control_state *state,
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_manifest_plan *plan,
    const struct micros_bootstrap_binding *bindings,
    size_t binding_count
);

enum micros_bootstrap_error
micros_bootstrap_control_publish_controller(
    struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
);

enum micros_bootstrap_error micros_bootstrap_control_validate(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
);

enum micros_bootstrap_error micros_bootstrap_control_release(
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    uint32_t service_id,
    uint64_t now,
    bool role_gate_ready
);

enum micros_bootstrap_error micros_bootstrap_control_prepare_ready(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t now,
    bool role_gate_ready,
    struct micros_bootstrap_ready_plan *plan
);

void micros_bootstrap_control_commit_ready_prevalidated(
    struct micros_bootstrap_control_state *state,
    struct micros_bootstrap_ready_plan *plan
);

enum micros_bootstrap_error
micros_bootstrap_control_prepare_complete(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_grant_registry *grants,
    struct micros_bootstrap_complete_plan *plan
);

void micros_bootstrap_control_commit_complete_prevalidated(
    struct micros_bootstrap_control_state *state,
    struct micros_bootstrap_complete_plan *plan
);

const struct micros_bootstrap_binding *
micros_bootstrap_control_find_binding(
    const struct micros_bootstrap_control_state *state,
    uint32_t service_id
);

#endif
