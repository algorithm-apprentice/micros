#include "kernel/tty_handoff_runtime_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/endpoint_internal.h"
#include "micros/sv39.h"

static bool tty_handoff_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool tty_handoff_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool tty_handoff_endpoint_matches_process(
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

bool tty_handoff_identity_is_valid(
    const struct micros_tty_handoff_runtime_state *state
)
{
    return (
        state != NULL
        && state->service_id == MICROS_TTY_SERVICE_ID
        && state->image_id != 0
        && state->profile_id == MICROS_PRIVILEGE_PROFILE_TTY
        && state->process.slot == MICROS_TTY_PROCESS_SLOT
        && tty_handoff_endpoint_matches_process(state->endpoint, state->process)
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
tty_handoff_runtime_entry(
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

bool tty_handoff_binding_identity_matches(
    struct tty_handoff_context *context,
    const struct micros_bootstrap_control_state *bootstrap
)
{
    const struct micros_bootstrap_binding *binding;
    const struct micros_bootstrap_manifest_entry *entry;

    if (
        bootstrap == NULL
        || !tty_handoff_identity_is_valid(&context->tty_handoff_state)
    ) {
        return false;
    }
    binding = micros_bootstrap_control_find_binding(
        bootstrap,
        context->tty_handoff_state.service_id
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
            == context->tty_handoff_state.service_id
        && binding->service_id == context->tty_handoff_state.service_id
        && binding->endpoint == context->tty_handoff_state.endpoint
        && binding->root
            == context->tty_handoff_state.root_physical_address
        && tty_handoff_process_handles_equal(
            binding->process,
            context->tty_handoff_state.process
        )
        && tty_handoff_thread_handles_equal(
            binding->thread,
            context->tty_handoff_state.thread
        )
        && entry->service_id == context->tty_handoff_state.service_id
        && entry->image_id == context->tty_handoff_state.image_id
        && entry->process_slot == context->tty_handoff_state.process.slot
        && entry->profile_id == context->tty_handoff_state.profile_id
        && entry->role_flags
            == MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
        && entry->irq_source == MICROS_TTY_UART_IRQ_SOURCE
        && entry->device_base == MICROS_TTY_UART_PHYSICAL_BASE
        && entry->device_length == MICROS_TTY_UART_MAPPED_LENGTH
    );
}

struct tty_handoff_live_binding {
    const struct micros_process *process;
    const struct micros_thread *thread;
    const struct micros_endpoint_record *endpoint;
    bool prepared;
    bool active;
};

static void tty_handoff_classify_live_binding(
    struct tty_handoff_context *context,
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_runtime_entry *transition,
    struct tty_handoff_live_binding *work
)
{
    work->prepared = (
        transition->state == MICROS_BOOTSTRAP_SERVICE_PREPARED
        && transition->endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_RESERVED
        && !transition->profile_installed
        && !transition->scheduler_assigned
        && transition->ready_deadline == 0
        && work->process->privilege_profile == 0
        && work->endpoint->state == MICROS_ENDPOINT_STATE_RESERVED
        && work->thread->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && !work->thread->scheduler_assigned
    );
    work->active = (
        (
            transition->state == MICROS_BOOTSTRAP_SERVICE_STARTING
            || transition->state == MICROS_BOOTSTRAP_SERVICE_READY
        )
        && transition->endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        && transition->profile_installed
        && transition->scheduler_assigned
        && work->process->privilege_profile
            == context->tty_handoff_state.profile_id
        && work->endpoint->state == MICROS_ENDPOINT_STATE_ACTIVE
        && work->thread->scheduler_assigned
        && work->thread->scheduler_priority
            == binding->scheduler_priority
        && work->thread->scheduler_preemptible
            == binding->scheduler_preemptible
        && work->thread->quantum_counter_ticks
            == binding->scheduler_quantum_counter_ticks
    );
}

static bool tty_handoff_live_binding_matches(
    struct tty_handoff_context *context,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_runtime_entry *transition
)
{
    struct tty_handoff_live_binding work;


    if (
        registry == NULL
        || objects == NULL
        || binding == NULL
        || transition == NULL
        || micros_process_resolve(
            objects,
            context->tty_handoff_state.process,
            &work.process
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_resolve(
            objects,
            context->tty_handoff_state.thread,
            &work.thread
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_endpoint_resolve_internal(
            registry,
            objects,
            context->tty_handoff_state.endpoint,
            &work.endpoint
        ) != MICROS_ENDPOINT_OK
        || work.process->address_space_root
            != context->tty_handoff_state.root_physical_address
        || work.process->primary_endpoint != context->tty_handoff_state.endpoint
        || !tty_handoff_process_handles_equal(
            work.thread->owner,
            context->tty_handoff_state.process
        )
        || !work.thread->context_attached
        || !tty_handoff_process_handles_equal(
            work.endpoint->owner,
            context->tty_handoff_state.process
        )
        || work.endpoint->value != context->tty_handoff_state.endpoint
    ) {
        return false;
    }

    tty_handoff_classify_live_binding(context, binding, transition, &work);

    switch (context->tty_handoff_state.handoff.console_phase) {
    case MICROS_TTY_CONSOLE_EARLY:
    case MICROS_TTY_CONSOLE_MAP_REQUESTED:
    case MICROS_TTY_CONSOLE_MAPPED:
        return work.prepared;
    case MICROS_TTY_CONSOLE_STARTING:
        return (
            work.active
            && transition->state
                == MICROS_BOOTSTRAP_SERVICE_STARTING
            && context->tty_handoff_state.handoff.deadline_armed
            && transition->ready_deadline
                == context->tty_handoff_state.handoff.deadline
        );
    case MICROS_TTY_CONSOLE_OWNED:
        if (!work.active) {
            return false;
        }
        if (
            transition->state
                == MICROS_BOOTSTRAP_SERVICE_STARTING
        ) {
            return (
                context->tty_handoff_state.handoff.deadline_armed
                && transition->ready_deadline
                    == context->tty_handoff_state.handoff.deadline
            );
        }
        return (
            !context->tty_handoff_state.handoff.deadline_armed
            && context->tty_handoff_state.handoff.deadline == 0
            && transition->ready_deadline == 0
        );
    case MICROS_TTY_CONSOLE_PANIC:
        return work.prepared || work.active;
    }
    return false;
}

bool micros_tty_handoff_runtime_role_ready(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    return (
        context->tty_bootstrap_state != NULL
        && micros_tty_handoff_runtime_validate(
            context->tty_bootstrap_state,
            registry,
            objects
        ) == MICROS_TTY_HANDOFF_OK
        && context->tty_handoff_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_OWNED
        && (
            context->tty_handoff_state.handoff.route_phase
                == MICROS_TTY_ROUTE_IDLE
            || context->tty_handoff_state.handoff.route_phase
                == MICROS_TTY_ROUTE_IN_SERVICE
        )
    );
}

enum micros_tty_device_authority_status
micros_tty_handoff_runtime_device_authority(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    struct micros_tty_device_authority *authority
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    struct micros_tty_device_authority candidate;

    if (authority == NULL) {
        return MICROS_TTY_DEVICE_AUTHORITY_INVARIANT;
    }
    if (context->tty_handoff_state.service_id == 0) {
        return MICROS_TTY_DEVICE_AUTHORITY_NONE;
    }
    if (!tty_handoff_identity_is_valid(&context->tty_handoff_state)) {
        return MICROS_TTY_DEVICE_AUTHORITY_INVARIANT;
    }
    if (
        context->tty_handoff_state.handoff.console_phase
            != MICROS_TTY_CONSOLE_MAPPED
        && context->tty_handoff_state.handoff.console_phase
            != MICROS_TTY_CONSOLE_STARTING
        && context->tty_handoff_state.handoff.console_phase
            != MICROS_TTY_CONSOLE_OWNED
    ) {
        return MICROS_TTY_DEVICE_AUTHORITY_NONE;
    }
    if (
        context->tty_bootstrap_state == NULL
        || micros_tty_handoff_runtime_validate(
            context->tty_bootstrap_state,
            registry,
            objects
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        return MICROS_TTY_DEVICE_AUTHORITY_INVARIANT;
    }
    candidate = (struct micros_tty_device_authority){
        .process = context->tty_handoff_state.process,
        .root_physical_address =
            context->tty_handoff_state.root_physical_address,
        .console_phase =
            context->tty_handoff_state.handoff.console_phase,
    };
    tty_handoff_copy_bytes(authority, &candidate, sizeof(candidate));
    return MICROS_TTY_DEVICE_AUTHORITY_ACTIVE;
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_validate(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    const struct micros_bootstrap_binding *binding;
    const struct micros_bootstrap_runtime_entry *transition;

    if (
        bootstrap == NULL
        || registry == NULL
        || objects == NULL
        || bootstrap != context->tty_bootstrap_state
        || !tty_handoff_identity_is_valid(&context->tty_handoff_state)
        || !tty_handoff_binding_identity_matches(context, bootstrap)
    ) {
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    binding = micros_bootstrap_control_find_binding(
        bootstrap,
        context->tty_handoff_state.service_id
    );
    if (
        binding == NULL
        || binding->manifest_index >= bootstrap->entry_count
    ) {
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    transition = tty_handoff_runtime_entry(
        &bootstrap->transitions,
        context->tty_handoff_state.service_id
    );
    if (
        transition == NULL
        || !tty_handoff_live_binding_matches(
            context,
            registry,
            objects,
            binding,
            transition
        )
        || micros_privilege_profile_allows_kernel_operation(
            registry,
            (uint8_t)context->tty_handoff_state.profile_id,
            3
        ) != MICROS_ENDPOINT_OK
    ) {
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    return MICROS_TTY_HANDOFF_OK;
}
