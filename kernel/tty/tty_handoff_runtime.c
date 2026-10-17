#include "kernel/tty/tty_handoff_runtime_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "micros/sv39.h"

enum micros_tty_handoff_error micros_tty_handoff_runtime_prepare(
    const struct micros_bootstrap_binding *binding,
    const struct micros_bootstrap_manifest_entry *entry
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    struct micros_tty_handoff_runtime_state candidate;

    if (binding == NULL || entry == NULL) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    if (context->tty_handoff_state.service_id != 0) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    if (
        binding->service_id != MICROS_TTY_SERVICE_ID
        || entry->service_id != MICROS_TTY_SERVICE_ID
        || binding->service_id != entry->service_id
        || binding->process.slot != MICROS_TTY_PROCESS_SLOT
        || binding->process.slot != entry->process_slot
        || !tty_handoff_endpoint_matches_process(
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

    tty_handoff_clear_bytes(&candidate, sizeof(candidate));
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
        || !tty_handoff_identity_is_valid(&candidate)
    ) {
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    tty_handoff_copy_bytes(
        &context->tty_handoff_state,
        &candidate,
        sizeof(context->tty_handoff_state)
    );
    return MICROS_TTY_HANDOFF_OK;
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_bind_bootstrap(
    const struct micros_bootstrap_control_state *bootstrap
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    if (bootstrap == NULL) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    if (
        context->tty_handoff_state.service_id == 0
        || context->tty_bootstrap_state != NULL
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    if (!tty_handoff_binding_identity_matches(context, bootstrap)) {
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    context->tty_bootstrap_state = bootstrap;
    return MICROS_TTY_HANDOFF_OK;
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_reset(void)
{
    struct tty_handoff_context *context = tty_handoff_context();

    if (
        context->tty_handoff_state.service_id != 0
        && (
            micros_tty_handoff_validate(
                &context->tty_handoff_state.handoff
            ) != MICROS_TTY_HANDOFF_OK
            || context->tty_handoff_state.handoff.console_phase
                != MICROS_TTY_CONSOLE_EARLY
        )
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    tty_handoff_clear_bytes(&context->tty_handoff_state, sizeof(context->tty_handoff_state));
    context->tty_bootstrap_state = NULL;
    return MICROS_TTY_HANDOFF_OK;
}

const struct micros_tty_handoff_runtime_state *
micros_tty_handoff_runtime_state(void)
{
    struct tty_handoff_context *context = tty_handoff_context();

    return context->tty_handoff_state.service_id == 0
        ? NULL
        : &context->tty_handoff_state;
}

struct micros_tty_handoff_runtime_state *
micros_tty_handoff_runtime_authoritative_state(void)
{
    struct tty_handoff_context *context = tty_handoff_context();

    return context->tty_handoff_state.service_id == 0
        ? NULL
        : &context->tty_handoff_state;
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_begin(
    uint64_t now,
    uint64_t interval
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    struct micros_tty_handoff candidate;
    enum micros_tty_handoff_error error;

    error = micros_tty_handoff_runtime_prepare_begin(
        now,
        interval,
        &candidate
    );
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    tty_handoff_copy_bytes(
        &context->tty_handoff_state.handoff,
        &candidate,
        sizeof(context->tty_handoff_state.handoff)
    );
    return MICROS_TTY_HANDOFF_OK;
}

bool micros_tty_handoff_runtime_deadline_expired(uint64_t now)
{
    struct tty_handoff_context *context = tty_handoff_context();

    return (
        context->tty_handoff_state.service_id != 0
        && micros_tty_handoff_validate(
            &context->tty_handoff_state.handoff
        ) == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_deadline_expired(
            &context->tty_handoff_state.handoff,
            now
        )
    );
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_panic(void)
{
    struct tty_handoff_context *context = tty_handoff_context();

    if (context->tty_handoff_state.service_id == 0) {
        return MICROS_TTY_HANDOFF_OK;
    }
    return micros_tty_handoff_panic(&context->tty_handoff_state.handoff);
}
