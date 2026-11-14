#ifndef MICROS_KERNEL_BOOTSTRAP_CONTROL_INTERNAL_H
#define MICROS_KERNEL_BOOTSTRAP_CONTROL_INTERNAL_H

#include "kernel/boot/bootstrap_transitions_internal.h"
#include "micros/bootstrap_control.h"
#include "micros/grant.h"
#include "micros/ipc.h"
#include "micros/ipc_core.h"

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

struct micros_bootstrap_service_selection {
    const struct micros_bootstrap_binding *binding;
    const struct micros_bootstrap_manifest_entry *manifest;
    const struct micros_bootstrap_runtime_entry *runtime;
    const struct micros_process *process;
    const struct micros_thread *thread;
    const struct micros_endpoint_record *endpoint;
    uint16_t binding_index;
    uint16_t runtime_index;
};

_Static_assert(sizeof(struct micros_bootstrap_service_selection) <= 64,
               "Bootstrap selected service view exceeds its storage bound");

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
    uint8_t binding_slots[MICROS_BOOTSTRAP_SERVICE_ID_LIMIT + 1];
};

struct micros_bootstrap_ready_plan {
    bool active;
    uint16_t binding_index;
    uint16_t reserved;
    struct bootstrap_transition_plan transition;
    struct micros_ipc_message acknowledgment;
};

struct micros_bootstrap_complete_plan {
    bool active;
    uint16_t controller_binding_index;
    uint16_t reserved;
    struct bootstrap_transition_plan transition;
    struct micros_ipc_kernel_notification_plan pm_notification;
};

_Static_assert(sizeof(struct micros_bootstrap_control_state) <= 2416,
               "Bootstrap control state exceeds its storage bound");
_Static_assert(sizeof(struct micros_bootstrap_ready_plan) <= 136,
               "Bootstrap ready plan exceeds its storage bound");
_Static_assert(sizeof(struct micros_bootstrap_complete_plan) <= 200,
               "Bootstrap complete plan exceeds its storage bound");

static inline const struct micros_bootstrap_binding *
micros_bootstrap_control_find_binding_bounded(
    const struct micros_bootstrap_control_state *state,
    uint32_t service_id
)
{
    const struct micros_bootstrap_binding *binding;
    uint8_t slot;

    if (
        state == NULL
        || service_id == 0
        || service_id > MICROS_BOOTSTRAP_SERVICE_ID_LIMIT
        || state->entry_count == 0
        || state->entry_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        return NULL;
    }
    slot = state->binding_slots[service_id];
    if (slot == 0 || slot > state->entry_count) {
        return NULL;
    }
    binding = &state->bindings[slot - 1];
    return binding->service_id == service_id ? binding : NULL;
}

enum micros_bootstrap_error micros_bootstrap_control_state_prepare(
    struct micros_bootstrap_control_state *state,
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_manifest_plan *plan,
    const struct micros_bootstrap_binding *bindings,
    size_t binding_count
);

enum micros_bootstrap_error micros_bootstrap_control_select_service(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    uint32_t service_id,
    struct micros_bootstrap_service_selection *selection
);
enum micros_bootstrap_error micros_bootstrap_control_select_sealed_service(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    uint32_t service_id,
    struct micros_bootstrap_service_selection *selection
);

enum micros_bootstrap_error micros_bootstrap_control_preflight_command(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    struct micros_bootstrap_service_selection *controller
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

enum micros_bootstrap_error
micros_bootstrap_control_release_console(
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    uint32_t service_id,
    uint64_t now,
    uint64_t retained_deadline,
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
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_bootstrap_complete_plan *plan
);

const struct micros_bootstrap_binding *
micros_bootstrap_control_find_binding(
    const struct micros_bootstrap_control_state *state,
    uint32_t service_id
);

#endif
