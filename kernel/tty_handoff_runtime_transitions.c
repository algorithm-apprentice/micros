#include "kernel/tty_handoff_runtime_internal.h"

#include <stddef.h>
#include <stdint.h>

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_begin(
    uint64_t now,
    uint64_t interval,
    struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    struct micros_tty_handoff local;
    enum micros_tty_handoff_error error;

    if (candidate == NULL || context->tty_handoff_state.service_id == 0) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    tty_handoff_copy_bytes(
        &local,
        &context->tty_handoff_state.handoff,
        sizeof(local)
    );
    error = micros_tty_handoff_begin(&local, now, interval);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    tty_handoff_copy_bytes(candidate, &local, sizeof(local));
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_begin_deadline_prevalidated(
    const struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    context->tty_handoff_state.handoff.deadline = candidate->deadline;
    context->tty_handoff_state.handoff.deadline_armed =
        candidate->deadline_armed;
}

void micros_tty_handoff_runtime_commit_begin_phase_prevalidated(
    const struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    context->tty_handoff_state.handoff.console_phase =
        candidate->console_phase;
}

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_mapped(
    struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    struct micros_tty_handoff local;
    enum micros_tty_handoff_error error;

    if (context->tty_handoff_state.service_id == 0) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    if (candidate == NULL) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    tty_handoff_copy_bytes(
        &local,
        &context->tty_handoff_state.handoff,
        sizeof(local)
    );
    error = micros_tty_handoff_mark_mapped(&local);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    tty_handoff_copy_bytes(candidate, &local, sizeof(local));
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_mapped_prevalidated(
    const struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    tty_handoff_copy_bytes(
        &context->tty_handoff_state.handoff,
        candidate,
        sizeof(context->tty_handoff_state.handoff)
    );
}

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_release(
    struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    struct micros_tty_handoff local;
    enum micros_tty_handoff_error error;

    if (candidate == NULL || context->tty_handoff_state.service_id == 0) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    tty_handoff_copy_bytes(
        &local,
        &context->tty_handoff_state.handoff,
        sizeof(local)
    );
    error = micros_tty_handoff_release(&local);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    tty_handoff_copy_bytes(candidate, &local, sizeof(local));
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_release_prevalidated(
    const struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    tty_handoff_copy_bytes(
        &context->tty_handoff_state.handoff,
        candidate,
        sizeof(context->tty_handoff_state.handoff)
    );
}

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_commit(
    struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    struct micros_tty_handoff local;
    enum micros_tty_handoff_error error;

    if (candidate == NULL || context->tty_handoff_state.service_id == 0) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    tty_handoff_copy_bytes(
        &local,
        &context->tty_handoff_state.handoff,
        sizeof(local)
    );
    error = micros_tty_handoff_commit(&local);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    tty_handoff_copy_bytes(candidate, &local, sizeof(local));
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_console_prevalidated(
    const struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    tty_handoff_copy_bytes(
        &context->tty_handoff_state.handoff,
        candidate,
        sizeof(context->tty_handoff_state.handoff)
    );
}

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_claim(
    uint32_t source,
    struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    struct micros_tty_handoff local;
    enum micros_tty_handoff_error error;

    if (candidate == NULL || context->tty_handoff_state.service_id == 0) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    tty_handoff_copy_bytes(
        &local,
        &context->tty_handoff_state.handoff,
        sizeof(local)
    );
    error = micros_tty_handoff_claim(&local, source);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    tty_handoff_copy_bytes(candidate, &local, sizeof(local));
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_claim_prevalidated(
    const struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    tty_handoff_copy_bytes(
        &context->tty_handoff_state.handoff,
        candidate,
        sizeof(context->tty_handoff_state.handoff)
    );
}

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_complete(
    uint32_t source,
    struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    struct micros_tty_handoff local;
    enum micros_tty_handoff_error error;

    if (candidate == NULL || context->tty_handoff_state.service_id == 0) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    tty_handoff_copy_bytes(
        &local,
        &context->tty_handoff_state.handoff,
        sizeof(local)
    );
    error = micros_tty_handoff_complete(&local, source);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    tty_handoff_copy_bytes(candidate, &local, sizeof(local));
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_complete_prevalidated(
    const struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    tty_handoff_copy_bytes(
        &context->tty_handoff_state.handoff,
        candidate,
        sizeof(context->tty_handoff_state.handoff)
    );
}

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_ready(
    uint64_t now,
    struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    struct micros_tty_handoff local;
    enum micros_tty_handoff_error error;

    if (candidate == NULL || context->tty_handoff_state.service_id == 0) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    tty_handoff_copy_bytes(
        &local,
        &context->tty_handoff_state.handoff,
        sizeof(local)
    );
    error = micros_tty_handoff_accept_ready(&local, now);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    tty_handoff_copy_bytes(candidate, &local, sizeof(local));
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_ready_prevalidated(
    const struct micros_tty_handoff *candidate
)
{
    struct tty_handoff_context *context = tty_handoff_context();

    tty_handoff_copy_bytes(
        &context->tty_handoff_state.handoff,
        candidate,
        sizeof(context->tty_handoff_state.handoff)
    );
}
