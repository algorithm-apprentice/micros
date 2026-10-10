#include "kernel/tty_control_syscall_core.h"

#include <stddef.h>
#include <stdint.h>

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

static const struct micros_endpoint_record *authority_endpoint(
    const struct micros_endpoint_registry *registry,
    micros_endpoint_t endpoint
)
{
    uint16_t slot;
    uint32_t generation;

    if (
        registry == NULL
        || endpoint == MICROS_ENDPOINT_NONE
        || endpoint == MICROS_ENDPOINT_ANY
    ) {
        return NULL;
    }
    slot = (uint16_t)(
        endpoint & ((UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1)
    );
    generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;
    if (
        slot >= MICROS_PROCESS_CAPACITY
        || generation == 0
        || generation > MICROS_ENDPOINT_GENERATION_MAX
    ) {
        return NULL;
    }
    return &registry->endpoints[slot];
}

static const struct micros_privilege_profile *authority_profile(
    const struct micros_endpoint_registry *registry,
    uint8_t profile_id
)
{
    if (
        registry == NULL
        || profile_id == 0
        || profile_id >= MICROS_PRIVILEGE_PROFILE_CAPACITY
    ) {
        return NULL;
    }
    return &registry->profiles[profile_id];
}

enum micros_tty_control_authority_result
micros_tty_control_syscall_presence_classify(
    const struct micros_tty_handoff_runtime_state *state,
    size_t bootstrap_entry_count,
    uint32_t planned_tty_service_id
)
{
    if (planned_tty_service_id == 0) {
        if (
            bootstrap_entry_count == 0
            || bootstrap_entry_count
                > MICROS_BOOTSTRAP_SERVICE_CAPACITY
        ) {
            return MICROS_TTY_CONTROL_AUTHORITY_INVARIANT;
        }
        return state == NULL
            ? MICROS_TTY_CONTROL_AUTHORITY_UNAUTHORIZED
            : MICROS_TTY_CONTROL_AUTHORITY_INVARIANT;
    }
    if (
        planned_tty_service_id != MICROS_TTY_SERVICE_ID
        || state == NULL
    ) {
        return MICROS_TTY_CONTROL_AUTHORITY_INVARIANT;
    }
    return MICROS_TTY_CONTROL_AUTHORITY_AUTHORIZED;
}

enum micros_tty_control_authority_result
micros_tty_control_syscall_caller_classify(
    const struct micros_tty_handoff_runtime_state *state,
    const struct micros_syscall_context *context,
    const struct micros_process *caller_process,
    const struct micros_thread *caller_thread
)
{
    if (
        state == NULL
        || context == NULL
        || caller_process == NULL
        || caller_thread == NULL
    ) {
        return MICROS_TTY_CONTROL_AUTHORITY_INVARIANT;
    }
    if (
        caller_process->slot_state
            != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || caller_process->generation
            != context->process.generation
        || caller_thread->slot_state
            != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || caller_thread->generation
            != context->current.generation
        || !process_handles_equal(
            caller_thread->owner,
            context->process
        )
    ) {
        return MICROS_TTY_CONTROL_AUTHORITY_INVARIANT;
    }
    if (
        !process_handles_equal(context->process, state->process)
        || !thread_handles_equal(context->current, state->thread)
    ) {
        return MICROS_TTY_CONTROL_AUTHORITY_UNAUTHORIZED;
    }
    return MICROS_TTY_CONTROL_AUTHORITY_AUTHORIZED;
}

enum micros_tty_control_authority_result
micros_tty_control_syscall_authority_classify(
    const struct micros_tty_handoff_runtime_state *state,
    size_t bootstrap_entry_count,
    uint32_t planned_tty_service_id,
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_manifest_entry *entry,
    const struct micros_syscall_context *context,
    const struct micros_process *caller_process,
    const struct micros_thread *caller_thread,
    const struct micros_endpoint_record *endpoint,
    const struct micros_privilege_profile *profile
)
{
    enum micros_tty_control_authority_result caller_result;

    caller_result = micros_tty_control_syscall_caller_classify(
        state,
        context,
        caller_process,
        caller_thread
    );
    if (
        caller_result
            != MICROS_TTY_CONTROL_AUTHORITY_AUTHORIZED
    ) {
        return caller_result;
    }
    if (
        bootstrap_entry_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        return MICROS_TTY_CONTROL_AUTHORITY_INVARIANT;
    }
    if (
        binding == NULL
        || entry == NULL
        || endpoint == NULL
        || profile == NULL
        || bootstrap_entry_count == 0
        || binding->manifest_index >= bootstrap_entry_count
        || planned_tty_service_id != MICROS_TTY_SERVICE_ID
        || planned_tty_service_id != binding->service_id
        || entry->service_id != binding->service_id
        || entry->image_id != state->image_id
        || entry->process_slot != binding->process.slot
        || entry->role_flags
            != MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
        || entry->profile_id != MICROS_PRIVILEGE_PROFILE_TTY
        || entry->irq_source != MICROS_TTY_UART_IRQ_SOURCE
        || entry->device_base != MICROS_TTY_UART_PHYSICAL_BASE
        || entry->device_length != MICROS_TTY_UART_MAPPED_LENGTH
        || state->service_id != binding->service_id
        || state->endpoint != binding->endpoint
        || state->profile_id != entry->profile_id
        || state->root_physical_address != binding->root
        || !process_handles_equal(state->process, binding->process)
        || !thread_handles_equal(state->thread, binding->thread)
        || !endpoint_matches_process(
            binding->endpoint,
            binding->process
        )
        || caller_process->address_space_root != binding->root
        || caller_process->primary_endpoint != binding->endpoint
        || caller_process->privilege_profile != entry->profile_id
        || endpoint->state != MICROS_ENDPOINT_STATE_ACTIVE
        || endpoint->value != binding->endpoint
        || !process_handles_equal(endpoint->owner, binding->process)
        || profile->id != entry->profile_id
        || profile->kernel_operations
            != MICROS_KERNEL_OPERATION_TTY_CONTROL
    ) {
        return MICROS_TTY_CONTROL_AUTHORITY_INVARIANT;
    }
    return MICROS_TTY_CONTROL_AUTHORITY_AUTHORIZED;
}

enum micros_tty_control_authority_result
micros_tty_control_syscall_authority_resolve(
    const struct micros_tty_handoff_runtime_state *state,
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_syscall_context *context
)
{
    const struct micros_bootstrap_binding *binding;
    const struct micros_bootstrap_manifest_entry *entry;
    const struct micros_privilege_profile *profile;
    const struct micros_process *caller_process;
    const struct micros_thread *caller_thread;
    const struct micros_endpoint_record *endpoint;
    enum micros_tty_control_authority_result caller_result;
    enum micros_tty_control_authority_result presence_result;

    if (objects == NULL || context == NULL) {
        return MICROS_TTY_CONTROL_AUTHORITY_INVARIANT;
    }
    if (
        micros_process_resolve(
            objects,
            context->process,
            &caller_process
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_resolve(
            objects,
            context->current,
            &caller_thread
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_TTY_CONTROL_AUTHORITY_INVARIANT;
    }
    if (bootstrap == NULL || registry == NULL) {
        return MICROS_TTY_CONTROL_AUTHORITY_INVARIANT;
    }
    presence_result = micros_tty_control_syscall_presence_classify(
        state,
        bootstrap->entry_count,
        bootstrap->plan.console_service_id
    );
    if (
        presence_result
            != MICROS_TTY_CONTROL_AUTHORITY_AUTHORIZED
    ) {
        return presence_result;
    }
    caller_result = micros_tty_control_syscall_caller_classify(
        state,
        context,
        caller_process,
        caller_thread
    );
    if (
        caller_result
            != MICROS_TTY_CONTROL_AUTHORITY_AUTHORIZED
    ) {
        return caller_result;
    }
    binding = micros_bootstrap_control_find_binding_bounded(
        bootstrap,
        bootstrap->plan.console_service_id
    );
    if (
        binding == NULL
        || binding->manifest_index >= bootstrap->entry_count
    ) {
        return MICROS_TTY_CONTROL_AUTHORITY_INVARIANT;
    }
    entry = &bootstrap->manifest.entries[binding->manifest_index];
    endpoint = authority_endpoint(registry, binding->endpoint);
    profile = authority_profile(registry, entry->profile_id);
    return micros_tty_control_syscall_authority_classify(
        state,
        bootstrap->entry_count,
        bootstrap->plan.console_service_id,
        binding,
        entry,
        context,
        caller_process,
        caller_thread,
        endpoint,
        profile
    );
}
