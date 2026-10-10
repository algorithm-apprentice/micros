#include "kernel/bootstrap_control_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/endpoint_internal.h"
#include "kernel/scheduler_core_internal.h"
#include "micros/scheduler_core.h"

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool bytes_are_zero(const void *storage, size_t size)
{
    const unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool staged_launcher_acknowledgments_are_valid(
    const struct micros_bootstrap_control_state *state,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t launcher
);

static const struct micros_bootstrap_runtime_entry *
find_runtime_entry(
    const struct micros_bootstrap_control_state *state,
    uint32_t service_id
)
{
    size_t index;

    for (index = 0; index < state->transitions.entry_count; ++index) {
        if (
            state->transitions.entries[index].service_id
                == service_id
        ) {
            return &state->transitions.entries[index];
        }
    }
    return NULL;
}

static struct micros_bootstrap_binding *find_binding_mutable(
    struct micros_bootstrap_control_state *state,
    uint32_t service_id
)
{
    size_t index;

    if (
        state == NULL
        || state->entry_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        return NULL;
    }
    for (index = 0; index < state->entry_count; ++index) {
        if (state->bindings[index].service_id == service_id) {
            return &state->bindings[index];
        }
    }
    return NULL;
}

const struct micros_bootstrap_binding *
micros_bootstrap_control_find_binding(
    const struct micros_bootstrap_control_state *state,
    uint32_t service_id
)
{
    return micros_bootstrap_control_find_binding_bounded(
        state,
        service_id
    );
}

static bool binding_matches_manifest(
    const struct micros_bootstrap_control_state *state,
    const struct micros_bootstrap_binding *binding
)
{
    const struct micros_bootstrap_manifest_entry *entry;
    struct micros_process_handle endpoint_owner;

    if (
        binding->manifest_index >= state->entry_count
        || binding->service_id == 0
        || binding->service_id > 63
        || binding->scheduler_priority
            >= MICROS_SCHEDULER_PRIORITY_COUNT
        || binding->scheduler_quantum_counter_ticks == 0
        || binding->prepared_page_count == 0
        || binding->root == 0
        || micros_endpoint_unpack(
            binding->endpoint,
            &endpoint_owner
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    entry = &state->manifest.entries[binding->manifest_index];
    return (
        binding->service_id == entry->service_id
        && binding->process.slot == entry->process_slot
        && process_handles_equal(endpoint_owner, binding->process)
    );
}

static bool binding_matches_objects(
    const struct micros_bootstrap_control_state *state,
    const struct micros_bootstrap_binding *binding,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_bootstrap_manifest_entry *manifest_entry =
        &state->manifest.entries[binding->manifest_index];
    const struct micros_process *process;
    const struct micros_thread *thread;
    const struct micros_endpoint_record *endpoint;
    const struct micros_bootstrap_runtime_entry *transition = NULL;
    size_t index;

    if (
        micros_process_resolve(objects, binding->process, &process)
            != MICROS_KERNEL_OBJECT_OK
        || micros_thread_resolve(objects, binding->thread, &thread)
            != MICROS_KERNEL_OBJECT_OK
        || !process_handles_equal(thread->owner, binding->process)
        || !thread->context_attached
        || process->address_space_root != binding->root
        || process->primary_endpoint != binding->endpoint
        || micros_endpoint_resolve_internal(
            registry,
            objects,
            binding->endpoint,
            &endpoint
        ) != MICROS_ENDPOINT_OK
        || !process_handles_equal(endpoint->owner, binding->process)
    ) {
        return false;
    }
    for (index = 0; index < state->transitions.entry_count; ++index) {
        if (
            state->transitions.entries[index].service_id
                == binding->service_id
        ) {
            transition = &state->transitions.entries[index];
            break;
        }
    }
    if (state->phase == MICROS_BOOTSTRAP_PHASE_PREPARING) {
        return (
            process->privilege_profile == 0
            && endpoint->state == MICROS_ENDPOINT_STATE_RESERVED
            && thread->runtime_flags == MICROS_THREAD_RTS_INACTIVE
            && !thread->scheduler_assigned
        );
    }
    if (transition == NULL) {
        return false;
    }
    if (
        transition->endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_RESERVED
    ) {
        return (
            process->privilege_profile == 0
            && endpoint->state == MICROS_ENDPOINT_STATE_RESERVED
            && thread->runtime_flags == MICROS_THREAD_RTS_INACTIVE
            && !thread->scheduler_assigned
        );
    }
    if (
        transition->endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY
    ) {
        return (
            process->privilege_profile
                == manifest_entry->profile_id
            && endpoint->state
                == MICROS_ENDPOINT_STATE_SOURCE_ONLY
            && thread->runtime_flags
                == MICROS_THREAD_RTS_INACTIVE
            && !thread->scheduler_assigned
        );
    }
    if (
        process->privilege_profile != manifest_entry->profile_id
        || !thread->scheduler_assigned
        || thread->scheduler_priority != binding->scheduler_priority
        || thread->scheduler_preemptible
            != binding->scheduler_preemptible
        || thread->quantum_counter_ticks
            != binding->scheduler_quantum_counter_ticks
    ) {
        return false;
    }
    return endpoint->state == MICROS_ENDPOINT_STATE_ACTIVE;
}

enum micros_bootstrap_error micros_bootstrap_control_state_prepare(
    struct micros_bootstrap_control_state *state,
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_manifest_plan *plan,
    const struct micros_bootstrap_binding *bindings,
    size_t binding_count
)
{
    struct micros_bootstrap_control_state candidate;
    uint64_t service_ids = 0;
    size_t index;

    if (
        state == NULL
        || manifest == NULL
        || plan == NULL
        || bindings == NULL
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (!bytes_are_zero(state, sizeof(*state))) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (
        binding_count == 0
        || binding_count != plan->entry_count
        || plan->entry_count != manifest->header.entry_count
        || binding_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    candidate.phase = MICROS_BOOTSTRAP_PHASE_PREPARING;
    candidate.entry_count = (uint16_t)binding_count;
    copy_bytes(&candidate.manifest, manifest, sizeof(*manifest));
    copy_bytes(&candidate.plan, plan, sizeof(*plan));
    copy_bytes(
        candidate.bindings,
        bindings,
        binding_count * sizeof(bindings[0])
    );
    for (index = 0; index < binding_count; ++index) {
        const struct micros_bootstrap_binding *binding =
            &candidate.bindings[index];
        uint64_t service_bit;

        if (!binding_matches_manifest(&candidate, binding)) {
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
        service_bit = UINT64_C(1) << (binding->service_id - 1);
        if ((service_ids & service_bit) != 0) {
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
        service_ids |= service_bit;
    }
    copy_bytes(state, &candidate, sizeof(*state));
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error micros_bootstrap_control_validate(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    size_t index;

    if (state == NULL || registry == NULL || objects == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (
        state->phase != MICROS_BOOTSTRAP_PHASE_PREPARING
        && state->phase != MICROS_BOOTSTRAP_PHASE_RUNNING
        && state->phase != MICROS_BOOTSTRAP_PHASE_SEALED
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    if (
        state->entry_count == 0
        || state->entry_count != state->manifest.header.entry_count
        || state->entry_count != state->plan.entry_count
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_endpoint_registry_validate_objects(registry, objects)
            != MICROS_ENDPOINT_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    if (
        state->phase == MICROS_BOOTSTRAP_PHASE_PREPARING
        && !bytes_are_zero(
            &state->transitions,
            sizeof(state->transitions)
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    if (
        state->phase != MICROS_BOOTSTRAP_PHASE_PREPARING
        && state->transitions.phase != state->phase
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    for (index = 0; index < state->entry_count; ++index) {
        if (
            !binding_matches_manifest(state, &state->bindings[index])
            || !binding_matches_objects(
                state,
                &state->bindings[index],
                registry,
                objects
            )
        ) {
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
    }
    if (state->phase == MICROS_BOOTSTRAP_PHASE_RUNNING) {
        const struct micros_bootstrap_binding *controller =
            micros_bootstrap_control_find_binding(
                state,
                state->transitions.controller_service_id
            );

        if (
            controller == NULL
            || !process_handles_equal(
                state->controller_process,
                controller->process
            )
            || !thread_handles_equal(
                state->controller_thread,
                controller->thread
            )
            || state->controller_endpoint != controller->endpoint
        ) {
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
    } else if (
        state->phase == MICROS_BOOTSTRAP_PHASE_SEALED
        && (
            state->controller_process.generation != 0
            || state->controller_thread.generation != 0
            || state->controller_endpoint != 0
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    if (state->phase == MICROS_BOOTSTRAP_PHASE_SEALED) {
        const struct micros_bootstrap_binding *controller =
            micros_bootstrap_control_find_binding(
                state,
                state->plan.controller_service_id
            );

        if (
            controller == NULL
            || !staged_launcher_acknowledgments_are_valid(
                state,
                objects,
                controller->endpoint
            )
        ) {
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
    }
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error
micros_bootstrap_control_publish_controller(
    struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    struct micros_bootstrap_runtime transitions;
    const struct micros_bootstrap_binding *controller;
    const struct micros_thread *thread;
    const struct micros_process *process;
    const struct micros_endpoint_record *endpoint;
    enum micros_bootstrap_error error;

    if (state == NULL || registry == NULL || objects == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    clear_bytes(&transitions, sizeof(transitions));
    if (state->phase != MICROS_BOOTSTRAP_PHASE_PREPARING) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    controller = micros_bootstrap_control_find_binding(
        state,
        state->plan.controller_service_id
    );
    if (
        controller == NULL
        || micros_process_resolve(
            objects,
            controller->process,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_resolve(
            objects,
            controller->thread,
            &thread
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_endpoint_resolve_internal(
            registry,
            objects,
            controller->endpoint,
            &endpoint
        ) != MICROS_ENDPOINT_OK
        || process->privilege_profile
            != state->manifest.entries[
                controller->manifest_index
            ].profile_id
        || endpoint->state != MICROS_ENDPOINT_STATE_ACTIVE
        || !thread->scheduler_assigned
        || thread->runtime_flags != 0
        || thread->scheduler_priority
            != controller->scheduler_priority
        || thread->scheduler_preemptible
            != controller->scheduler_preemptible
        || thread->quantum_counter_ticks
            != controller->scheduler_quantum_counter_ticks
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    error = micros_bootstrap_runtime_initialize(
        &state->manifest,
        &state->plan,
        &transitions
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    copy_bytes(
        &state->transitions,
        &transitions,
        sizeof(state->transitions)
    );
    state->phase = MICROS_BOOTSTRAP_PHASE_RUNNING;
    state->controller_process = controller->process;
    state->controller_thread = controller->thread;
    state->controller_endpoint = controller->endpoint;
    return micros_bootstrap_control_validate(
        state,
        registry,
        objects
    );
}

static enum micros_bootstrap_error map_endpoint_error(
    enum micros_endpoint_error error
)
{
    switch (error) {
    case MICROS_ENDPOINT_OK:
        return MICROS_BOOTSTRAP_OK;
    case MICROS_ENDPOINT_ERROR_ARGUMENT:
    case MICROS_ENDPOINT_ERROR_ENDPOINT:
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    case MICROS_ENDPOINT_ERROR_PROFILE:
        return MICROS_BOOTSTRAP_ERROR_PROFILE;
    case MICROS_ENDPOINT_ERROR_STATE:
    case MICROS_ENDPOINT_ERROR_CLOSING:
        return MICROS_BOOTSTRAP_ERROR_STATE;
    default:
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
}

static enum micros_bootstrap_error control_release(
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    uint32_t service_id,
    uint64_t now,
    uint64_t retained_deadline,
    bool retain_deadline,
    bool role_gate_ready
)
{
    struct micros_bootstrap_runtime transitions;
    struct micros_bootstrap_binding *binding;
    const struct micros_bootstrap_manifest_entry *entry;
    enum micros_bootstrap_error error;
    enum micros_endpoint_error endpoint_error;
    enum micros_kernel_object_error scheduler_error;

    if (state == NULL || registry == NULL || objects == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (
        micros_bootstrap_control_validate(state, registry, objects)
            != MICROS_BOOTSTRAP_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    binding = find_binding_mutable(state, service_id);
    if (state->phase != MICROS_BOOTSTRAP_PHASE_RUNNING) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (binding == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    entry = &state->manifest.entries[binding->manifest_index];
    if (
        (entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER)
            != 0
        ? (!retain_deadline || !role_gate_ready)
        : retain_deadline
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    copy_bytes(
        &transitions,
        &state->transitions,
        sizeof(transitions)
    );
    error = retain_deadline
        ? micros_bootstrap_runtime_release_retaining_deadline(
            &transitions,
            service_id,
            now,
            retained_deadline
        )
        : micros_bootstrap_runtime_release(
            &transitions,
            service_id,
            now
        );
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    endpoint_error = micros_endpoint_preflight_publish(
        registry,
        objects,
        binding->endpoint,
        entry->profile_id
    );
    if (endpoint_error != MICROS_ENDPOINT_OK) {
        return map_endpoint_error(endpoint_error);
    }
    scheduler_error = micros_thread_scheduler_admit_preflight(
        objects,
        hart,
        binding->thread,
        binding->scheduler_priority,
        binding->scheduler_quantum_counter_ticks
    );
    if (scheduler_error != MICROS_KERNEL_OBJECT_OK) {
        if (scheduler_error == MICROS_KERNEL_OBJECT_ERROR_POLICY) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return scheduler_error == MICROS_KERNEL_OBJECT_ERROR_STATE
                || scheduler_error
                    == MICROS_KERNEL_OBJECT_ERROR_STALE
            ? MICROS_BOOTSTRAP_ERROR_STATE
            : MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }

    micros_endpoint_commit_publish_prevalidated(
        registry,
        objects,
        binding->endpoint,
        entry->profile_id
    );
    copy_bytes(
        &state->transitions,
        &transitions,
        sizeof(state->transitions)
    );
    scheduler_error = micros_thread_scheduler_admit(
        objects,
        hart,
        binding->thread,
        binding->scheduler_priority,
        binding->scheduler_quantum_counter_ticks,
        binding->scheduler_preemptible
    );
    if (
        scheduler_error != MICROS_KERNEL_OBJECT_OK
        || micros_bootstrap_control_validate(state, registry, objects)
            != MICROS_BOOTSTRAP_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error micros_bootstrap_control_release(
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    uint32_t service_id,
    uint64_t now,
    bool role_gate_ready
)
{
    return control_release(
        state,
        registry,
        objects,
        hart,
        service_id,
        now,
        0,
        false,
        role_gate_ready
    );
}

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
)
{
    return control_release(
        state,
        registry,
        objects,
        hart,
        service_id,
        now,
        retained_deadline,
        true,
        role_gate_ready
    );
}

static void write_u32_le(unsigned char *bytes, uint32_t value)
{
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
    bytes[2] = (unsigned char)(value >> 16);
    bytes[3] = (unsigned char)(value >> 24);
}

static uint32_t read_u32_le(const unsigned char *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

enum micros_bootstrap_error micros_bootstrap_control_prepare_ready(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t now,
    bool role_gate_ready,
    struct micros_bootstrap_ready_plan *plan
)
{
    struct micros_bootstrap_ready_plan candidate;
    const struct micros_bootstrap_binding *binding;
    const struct micros_bootstrap_manifest_entry *entry;
    enum micros_bootstrap_error error;

    if (
        state == NULL
        || registry == NULL
        || objects == NULL
        || plan == NULL
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    if (
        micros_bootstrap_control_validate(state, registry, objects)
            != MICROS_BOOTSTRAP_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    if (state->phase != MICROS_BOOTSTRAP_PHASE_RUNNING) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    binding = micros_bootstrap_control_find_binding(state, service_id);
    if (binding == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    entry = &state->manifest.entries[binding->manifest_index];
    copy_bytes(
        &candidate.transitions,
        &state->transitions,
        sizeof(candidate.transitions)
    );
    error = micros_bootstrap_runtime_accept_ready(
        &candidate.transitions,
        service_id,
        now
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    if (
        (
            entry->role_flags
            & (
                MICROS_BOOTSTRAP_ROLE_VM
                | MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
            )
        ) != 0
        && !role_gate_ready
    ) {
        return MICROS_BOOTSTRAP_ERROR_ROLE;
    }
    if (endpoint != binding->endpoint) {
        return MICROS_BOOTSTRAP_ERROR_IDENTITY;
    }
    candidate.active = true;
    candidate.binding_index =
        (uint16_t)(binding - &state->bindings[0]);
    candidate.acknowledgment.source = state->controller_endpoint;
    candidate.acknowledgment.type =
        MICROS_BOOTSTRAP_MESSAGE_READY_ACK;
    write_u32_le(
        &candidate.acknowledgment.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    write_u32_le(
        &candidate.acknowledgment.payload[4],
        service_id
    );
    write_u32_le(
        &candidate.acknowledgment.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    write_u32_le(&candidate.acknowledgment.payload[12], 0);
    write_u32_le(
        &candidate.acknowledgment.payload[16],
        binding->endpoint
    );
    copy_bytes(plan, &candidate, sizeof(*plan));
    return MICROS_BOOTSTRAP_OK;
}

void micros_bootstrap_control_commit_ready_prevalidated(
    struct micros_bootstrap_control_state *state,
    struct micros_bootstrap_ready_plan *plan
)
{
    copy_bytes(
        &state->transitions,
        &plan->transitions,
        sizeof(state->transitions)
    );
    clear_bytes(plan, sizeof(*plan));
}

static bool launcher_has_grant_dependency(
    const struct micros_grant_registry *grants,
    micros_endpoint_t launcher
)
{
    size_t index;

    if (grants == NULL) {
        return false;
    }
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        const struct micros_grant_record *grant =
            &grants->grants[index];

        if (
            grant->state == MICROS_GRANT_SLOT_ACTIVE
            && (
                grant->grantor_endpoint == launcher
                || grant->grantee_endpoint == launcher
            )
        ) {
            return true;
        }
    }
    return false;
}

static bool staged_launcher_acknowledgments_are_valid(
    const struct micros_bootstrap_control_state *state,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t launcher
)
{
    size_t thread_index;

    for (
        thread_index = 0;
        thread_index < MICROS_THREAD_CAPACITY;
        ++thread_index
    ) {
        const struct micros_thread *thread =
            &objects->threads[thread_index];
        const struct micros_ipc_message *message;
        const struct micros_bootstrap_binding *binding;
        const struct micros_bootstrap_runtime_entry *transition = NULL;
        uint32_t service_id;
        size_t payload_index;
        size_t transition_index;

        if (
            thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !thread->ipc_delivery_pending
            || thread->ipc_staged_result != MICROS_IPC_OK
            || thread->ipc_inbound_message.source != launcher
        ) {
            continue;
        }
        message = &thread->ipc_inbound_message;
        if (
            message->type != MICROS_BOOTSTRAP_MESSAGE_READY_ACK
            || message->reply_token != 0
            || read_u32_le(&message->payload[0])
                != MICROS_BOOTSTRAP_MANIFEST_VERSION
            || read_u32_le(&message->payload[8])
                != MICROS_BOOTSTRAP_MANIFEST_VERSION
            || read_u32_le(&message->payload[12]) != 0
        ) {
            return false;
        }
        for (
            payload_index = 20;
            payload_index < sizeof(message->payload);
            ++payload_index
        ) {
            if (message->payload[payload_index] != 0) {
                return false;
            }
        }
        service_id = read_u32_le(&message->payload[4]);
        binding = micros_bootstrap_control_find_binding(
            state,
            service_id
        );
        if (
            binding == NULL
            || thread->owner.slot != binding->process.slot
            || thread->owner.generation
                != binding->process.generation
            || read_u32_le(&message->payload[16])
                != binding->endpoint
        ) {
            return false;
        }
        for (
            transition_index = 0;
            transition_index < state->transitions.entry_count;
            ++transition_index
        ) {
            if (
                state->transitions.entries[transition_index].service_id
                    == service_id
            ) {
                transition =
                    &state->transitions.entries[transition_index];
                break;
            }
        }
        if (
            transition == NULL
            || transition->state != MICROS_BOOTSTRAP_SERVICE_READY
        ) {
            return false;
        }
    }
    return true;
}

static enum micros_bootstrap_error find_ready_pm_endpoint(
    const struct micros_bootstrap_control_state *state,
    micros_endpoint_t *endpoint
)
{
    const struct micros_bootstrap_binding *binding;
    const struct micros_bootstrap_manifest_entry *pm_entry = NULL;
    const struct micros_bootstrap_runtime_entry *transition;
    size_t index;

    *endpoint = MICROS_ENDPOINT_NONE;
    for (index = 0; index < state->entry_count; ++index) {
        const struct micros_bootstrap_manifest_entry *entry =
            &state->manifest.entries[index];

        if ((entry->role_flags & MICROS_BOOTSTRAP_ROLE_PM) == 0) {
            continue;
        }
        if (pm_entry != NULL) {
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
        pm_entry = entry;
    }
    if (state->plan.pm_service_id == 0) {
        return pm_entry == NULL
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    binding = micros_bootstrap_control_find_binding(
        state,
        state->plan.pm_service_id
    );
    transition = find_runtime_entry(
        state,
        state->plan.pm_service_id
    );
    if (
        pm_entry == NULL
        || pm_entry->service_id != state->plan.pm_service_id
        || pm_entry->role_flags != MICROS_BOOTSTRAP_ROLE_PM
        || pm_entry->profile_id != MICROS_PRIVILEGE_PROFILE_PM
        || binding == NULL
        || &state->manifest.entries[binding->manifest_index]
            != pm_entry
        || transition == NULL
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    if (
        transition->state != MICROS_BOOTSTRAP_SERVICE_READY
        || transition->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    *endpoint = binding->endpoint;
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error map_notification_error(
    enum micros_ipc_error error
)
{
    switch (error) {
    case MICROS_IPC_OK:
        return MICROS_BOOTSTRAP_OK;
    case MICROS_IPC_ERROR_STATE:
        return MICROS_BOOTSTRAP_ERROR_STATE;
    default:
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
}

enum micros_bootstrap_error
micros_bootstrap_control_prepare_complete(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_grant_registry *grants,
    struct micros_bootstrap_complete_plan *plan
)
{
    struct micros_bootstrap_complete_plan candidate;
    const struct micros_bootstrap_binding *controller;
    const struct micros_thread *controller_thread;
    micros_endpoint_t pm_endpoint;
    enum micros_bootstrap_error error;

    if (
        state == NULL
        || registry == NULL
        || objects == NULL
        || plan == NULL
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    if (
        micros_bootstrap_control_validate(state, registry, objects)
            != MICROS_BOOTSTRAP_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    if (state->phase != MICROS_BOOTSTRAP_PHASE_RUNNING) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    controller = micros_bootstrap_control_find_binding(
        state,
        state->transitions.controller_service_id
    );
    if (
        controller == NULL
        || micros_thread_resolve(
            objects,
            controller->thread,
            &controller_thread
        ) != MICROS_KERNEL_OBJECT_OK
        || controller_thread->runtime_flags != 0
        || !micros_thread_ipc_state_is_clear(controller_thread)
        || !staged_launcher_acknowledgments_are_valid(
            state,
            objects,
            controller->endpoint
        )
        || launcher_has_grant_dependency(
            grants,
            controller->endpoint
        )
    ) {
        return controller == NULL
            ? MICROS_BOOTSTRAP_ERROR_INVARIANT
            : MICROS_BOOTSTRAP_ERROR_STATE;
    }
    error = find_ready_pm_endpoint(state, &pm_endpoint);
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    copy_bytes(
        &candidate.transitions,
        &state->transitions,
        sizeof(candidate.transitions)
    );
    error = micros_bootstrap_runtime_complete(
        &candidate.transitions
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    if (
        micros_endpoint_preflight_source_only(
            registry,
            objects,
            controller->endpoint
        ) != MICROS_ENDPOINT_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (pm_endpoint != MICROS_ENDPOINT_NONE) {
        error = map_notification_error(
            micros_ipc_prepare_kernel_notification(
                registry,
                objects,
                pm_endpoint,
                MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED,
                &candidate.pm_notification
            )
        );
        if (error != MICROS_BOOTSTRAP_OK) {
            return error;
        }
    }
    candidate.active = true;
    candidate.controller_binding_index =
        (uint16_t)(controller - &state->bindings[0]);
    copy_bytes(plan, &candidate, sizeof(*plan));
    return MICROS_BOOTSTRAP_OK;
}

void micros_bootstrap_control_commit_complete_prevalidated(
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_bootstrap_complete_plan *plan
)
{
    copy_bytes(
        &state->transitions,
        &plan->transitions,
        sizeof(state->transitions)
    );
    state->phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    state->controller_process =
        (struct micros_process_handle){0, 0};
    state->controller_thread =
        (struct micros_thread_handle){0, 0};
    state->controller_endpoint = 0;
    if (plan->pm_notification.active) {
        micros_ipc_commit_kernel_notification_prevalidated(
            registry,
            objects,
            &plan->pm_notification
        );
    }
    clear_bytes(plan, sizeof(*plan));
}
