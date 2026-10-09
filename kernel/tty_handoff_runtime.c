#include "kernel/tty_handoff_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/endpoint_internal.h"
#include "micros/sv39.h"

static struct micros_tty_handoff_runtime_state tty_handoff_state;
static const struct micros_bootstrap_control_state
    *tty_bootstrap_state;

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size)
{
    uint8_t *output = destination;
    const uint8_t *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
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

static bool endpoint_matches_process(
    micros_endpoint_t endpoint,
    struct micros_process_handle process
)
{
    return (
        process.slot < MICROS_PROCESS_CAPACITY
        && process.generation != 0
        && process.generation <= MICROS_PROCESS_GENERATION_MAX
        && endpoint
            == (
                (process.generation << MICROS_ENDPOINT_SLOT_BITS)
                | process.slot
            )
        && endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
    );
}

static bool identity_is_valid(
    const struct micros_tty_handoff_runtime_state *state
)
{
    return (
        state != NULL
        && state->service_id == MICROS_TTY_SERVICE_ID
        && state->image_id != 0
        && state->profile_id == MICROS_PRIVILEGE_PROFILE_TTY
        && state->process.slot == MICROS_TTY_PROCESS_SLOT
        && endpoint_matches_process(state->endpoint, state->process)
        && state->thread.slot < MICROS_THREAD_CAPACITY
        && state->thread.generation != 0
        && state->root_physical_address != 0
        && state->root_physical_address
            % MICROS_SV39_PAGE_SIZE == 0
        && micros_tty_handoff_validate(&state->handoff)
            == MICROS_TTY_HANDOFF_OK
    );
}

static const struct micros_bootstrap_runtime_entry *
runtime_entry(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t service_id
)
{
    size_t index;

    if (
        runtime == NULL
        || runtime->entry_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        return NULL;
    }
    for (index = 0; index < runtime->entry_count; ++index) {
        if (runtime->entries[index].service_id == service_id) {
            return &runtime->entries[index];
        }
    }
    return NULL;
}

static bool binding_identity_matches(
    const struct micros_bootstrap_control_state *bootstrap
)
{
    const struct micros_bootstrap_binding *binding;
    const struct micros_bootstrap_manifest_entry *entry;

    if (
        bootstrap == NULL
        || !identity_is_valid(&tty_handoff_state)
    ) {
        return false;
    }
    binding = micros_bootstrap_control_find_binding(
        bootstrap,
        tty_handoff_state.service_id
    );
    if (
        binding == NULL
        || binding->manifest_index >= bootstrap->entry_count
    ) {
        return false;
    }
    entry = &bootstrap->manifest.entries[binding->manifest_index];
    return (
        bootstrap->plan.console_service_id
            == tty_handoff_state.service_id
        && binding->service_id == tty_handoff_state.service_id
        && binding->endpoint == tty_handoff_state.endpoint
        && binding->root
            == tty_handoff_state.root_physical_address
        && process_handles_equal(
            binding->process,
            tty_handoff_state.process
        )
        && thread_handles_equal(
            binding->thread,
            tty_handoff_state.thread
        )
        && entry->service_id == tty_handoff_state.service_id
        && entry->image_id == tty_handoff_state.image_id
        && entry->process_slot == tty_handoff_state.process.slot
        && entry->profile_id == tty_handoff_state.profile_id
        && entry->role_flags
            == MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
        && entry->irq_source == MICROS_TTY_UART_IRQ_SOURCE
        && entry->device_base == MICROS_TTY_UART_PHYSICAL_BASE
        && entry->device_length == MICROS_TTY_UART_MAPPED_LENGTH
    );
}

static bool live_binding_matches(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_runtime_entry *transition
)
{
    const struct micros_process *process;
    const struct micros_thread *thread;
    const struct micros_endpoint_record *endpoint;
    bool prepared;
    bool active;

    if (
        registry == NULL
        || objects == NULL
        || binding == NULL
        || transition == NULL
        || micros_process_resolve(
            objects,
            tty_handoff_state.process,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_resolve(
            objects,
            tty_handoff_state.thread,
            &thread
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_endpoint_resolve_internal(
            registry,
            objects,
            tty_handoff_state.endpoint,
            &endpoint
        ) != MICROS_ENDPOINT_OK
        || process->address_space_root
            != tty_handoff_state.root_physical_address
        || process->primary_endpoint != tty_handoff_state.endpoint
        || !process_handles_equal(
            thread->owner,
            tty_handoff_state.process
        )
        || !thread->context_attached
        || !process_handles_equal(
            endpoint->owner,
            tty_handoff_state.process
        )
        || endpoint->value != tty_handoff_state.endpoint
    ) {
        return false;
    }

    prepared = (
        transition->state == MICROS_BOOTSTRAP_SERVICE_PREPARED
        && transition->endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_RESERVED
        && !transition->profile_installed
        && !transition->scheduler_assigned
        && transition->ready_deadline == 0
        && process->privilege_profile == 0
        && endpoint->state == MICROS_ENDPOINT_STATE_RESERVED
        && thread->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && !thread->scheduler_assigned
    );
    active = (
        (
            transition->state == MICROS_BOOTSTRAP_SERVICE_STARTING
            || transition->state == MICROS_BOOTSTRAP_SERVICE_READY
        )
        && transition->endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        && transition->profile_installed
        && transition->scheduler_assigned
        && process->privilege_profile
            == tty_handoff_state.profile_id
        && endpoint->state == MICROS_ENDPOINT_STATE_ACTIVE
        && thread->scheduler_assigned
        && thread->scheduler_priority
            == binding->scheduler_priority
        && thread->scheduler_preemptible
            == binding->scheduler_preemptible
        && thread->quantum_counter_ticks
            == binding->scheduler_quantum_counter_ticks
    );

    switch (tty_handoff_state.handoff.console_phase) {
    case MICROS_TTY_CONSOLE_EARLY:
    case MICROS_TTY_CONSOLE_MAP_REQUESTED:
    case MICROS_TTY_CONSOLE_MAPPED:
        return prepared;
    case MICROS_TTY_CONSOLE_STARTING:
        return (
            active
            && transition->state
                == MICROS_BOOTSTRAP_SERVICE_STARTING
            && tty_handoff_state.handoff.deadline_armed
            && transition->ready_deadline
                == tty_handoff_state.handoff.deadline
        );
    case MICROS_TTY_CONSOLE_OWNED:
        if (!active) {
            return false;
        }
        if (
            transition->state
                == MICROS_BOOTSTRAP_SERVICE_STARTING
        ) {
            return (
                tty_handoff_state.handoff.deadline_armed
                && transition->ready_deadline
                    == tty_handoff_state.handoff.deadline
            );
        }
        return (
            !tty_handoff_state.handoff.deadline_armed
            && tty_handoff_state.handoff.deadline == 0
            && transition->ready_deadline == 0
        );
    case MICROS_TTY_CONSOLE_PANIC:
        return prepared || active;
    }
    return false;
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_prepare(
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_manifest_entry *entry
)
{
    struct micros_tty_handoff_runtime_state candidate;

    if (binding == NULL || entry == NULL) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    if (tty_handoff_state.service_id != 0) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    if (
        binding->service_id != MICROS_TTY_SERVICE_ID
        || entry->service_id != MICROS_TTY_SERVICE_ID
        || binding->service_id != entry->service_id
        || binding->process.slot != MICROS_TTY_PROCESS_SLOT
        || binding->process.slot != entry->process_slot
        || !endpoint_matches_process(
            binding->endpoint,
            binding->process
        )
        || binding->root == 0
        || binding->root % MICROS_SV39_PAGE_SIZE != 0
        || binding->thread.slot >= MICROS_THREAD_CAPACITY
        || binding->thread.generation == 0
        || entry->image_id == 0
        || entry->profile_id != MICROS_PRIVILEGE_PROFILE_TTY
        || entry->role_flags
            != MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
        || entry->irq_source != MICROS_TTY_UART_IRQ_SOURCE
        || entry->device_base != MICROS_TTY_UART_PHYSICAL_BASE
        || entry->device_length != MICROS_TTY_UART_MAPPED_LENGTH
    ) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }

    clear_bytes(&candidate, sizeof(candidate));
    candidate.service_id = binding->service_id;
    candidate.endpoint = binding->endpoint;
    candidate.image_id = entry->image_id;
    candidate.profile_id = entry->profile_id;
    candidate.process = binding->process;
    candidate.thread = binding->thread;
    candidate.root_physical_address = binding->root;
    if (
        micros_tty_handoff_initialize(&candidate.handoff)
            != MICROS_TTY_HANDOFF_OK
        || !identity_is_valid(&candidate)
    ) {
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    copy_bytes(
        &tty_handoff_state,
        &candidate,
        sizeof(tty_handoff_state)
    );
    return MICROS_TTY_HANDOFF_OK;
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_bind_bootstrap(
    const struct micros_bootstrap_control_state *bootstrap
)
{
    if (bootstrap == NULL) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    if (
        tty_handoff_state.service_id == 0
        || tty_bootstrap_state != NULL
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    if (!binding_identity_matches(bootstrap)) {
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    tty_bootstrap_state = bootstrap;
    return MICROS_TTY_HANDOFF_OK;
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_reset(void)
{
    if (
        tty_handoff_state.service_id != 0
        && (
            micros_tty_handoff_validate(
                &tty_handoff_state.handoff
            ) != MICROS_TTY_HANDOFF_OK
            || tty_handoff_state.handoff.console_phase
                != MICROS_TTY_CONSOLE_EARLY
        )
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    clear_bytes(&tty_handoff_state, sizeof(tty_handoff_state));
    tty_bootstrap_state = NULL;
    return MICROS_TTY_HANDOFF_OK;
}

const struct micros_tty_handoff_runtime_state *
micros_tty_handoff_runtime_state(void)
{
    return tty_handoff_state.service_id == 0
        ? NULL
        : &tty_handoff_state;
}

struct micros_tty_handoff_runtime_state *
micros_tty_handoff_runtime_authoritative_state(void)
{
    return tty_handoff_state.service_id == 0
        ? NULL
        : &tty_handoff_state;
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_begin(
    uint64_t now,
    uint64_t interval
)
{
    if (tty_handoff_state.service_id == 0) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    return micros_tty_handoff_begin(
        &tty_handoff_state.handoff,
        now,
        interval
    );
}

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_mapped(
    struct micros_tty_handoff *candidate
)
{
    struct micros_tty_handoff local;
    enum micros_tty_handoff_error error;

    if (candidate == NULL || tty_handoff_state.service_id == 0) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    copy_bytes(
        &local,
        &tty_handoff_state.handoff,
        sizeof(local)
    );
    error = micros_tty_handoff_mark_mapped(&local);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    copy_bytes(candidate, &local, sizeof(local));
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_mapped_prevalidated(
    const struct micros_tty_handoff *candidate
)
{
    copy_bytes(
        &tty_handoff_state.handoff,
        candidate,
        sizeof(tty_handoff_state.handoff)
    );
}

enum micros_tty_device_authority_status
micros_tty_handoff_runtime_device_authority(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    struct micros_tty_device_authority *authority
)
{
    struct micros_tty_device_authority candidate;

    if (authority == NULL) {
        return MICROS_TTY_DEVICE_AUTHORITY_INVARIANT;
    }
    if (tty_handoff_state.service_id == 0) {
        return MICROS_TTY_DEVICE_AUTHORITY_NONE;
    }
    if (!identity_is_valid(&tty_handoff_state)) {
        return MICROS_TTY_DEVICE_AUTHORITY_INVARIANT;
    }
    if (
        tty_handoff_state.handoff.console_phase
            != MICROS_TTY_CONSOLE_MAPPED
        && tty_handoff_state.handoff.console_phase
            != MICROS_TTY_CONSOLE_STARTING
        && tty_handoff_state.handoff.console_phase
            != MICROS_TTY_CONSOLE_OWNED
    ) {
        return MICROS_TTY_DEVICE_AUTHORITY_NONE;
    }
    if (
        tty_bootstrap_state == NULL
        || micros_tty_handoff_runtime_validate(
            tty_bootstrap_state,
            registry,
            objects
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        return MICROS_TTY_DEVICE_AUTHORITY_INVARIANT;
    }
    candidate = (struct micros_tty_device_authority){
        .process = tty_handoff_state.process,
        .root_physical_address =
            tty_handoff_state.root_physical_address,
        .console_phase =
            tty_handoff_state.handoff.console_phase,
    };
    copy_bytes(authority, &candidate, sizeof(candidate));
    return MICROS_TTY_DEVICE_AUTHORITY_ACTIVE;
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_validate(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_bootstrap_binding *binding;
    const struct micros_bootstrap_runtime_entry *transition;

    if (
        bootstrap == NULL
        || registry == NULL
        || objects == NULL
        || bootstrap != tty_bootstrap_state
        || !identity_is_valid(&tty_handoff_state)
        || !binding_identity_matches(bootstrap)
    ) {
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    binding = micros_bootstrap_control_find_binding(
        bootstrap,
        tty_handoff_state.service_id
    );
    if (
        binding == NULL
        || binding->manifest_index >= bootstrap->entry_count
    ) {
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    transition = runtime_entry(
        &bootstrap->transitions,
        tty_handoff_state.service_id
    );
    if (
        transition == NULL
        || !live_binding_matches(
            registry,
            objects,
            binding,
            transition
        )
        || micros_privilege_profile_allows_kernel_operation(
            registry,
            (uint8_t)tty_handoff_state.profile_id,
            3
        ) != MICROS_ENDPOINT_OK
    ) {
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    return MICROS_TTY_HANDOFF_OK;
}
