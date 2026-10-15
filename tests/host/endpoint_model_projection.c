#include "endpoint_model_internal.h"

#include <stdio.h>
#include <string.h>

static bool live_process_and_thread_match(
    const struct endpoint_model_fixture *fixture,
    const struct model_slot *expected,
    const struct micros_process *process,
    size_t step,
    enum model_operation operation,
    uint32_t value
)
{
    if (
        process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || process->generation != expected->generation
        || process->live_thread_count != 1
        || !expected->thread_live
    ) {
        return endpoint_model_model_fail(
            fixture,
            step,
            operation,
            value,
            "live process diverged"
        );
    }
    if (
        fixture->objects.threads[expected->thread.slot].slot_state
            != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || fixture->objects.threads[expected->thread.slot].generation
            != expected->thread.generation
        || fixture->objects.threads[expected->thread.slot].owner.slot
            != expected->process.slot
        || fixture->objects.threads[expected->thread.slot].owner.generation
            != expected->process.generation
        || fixture->objects.threads[expected->thread.slot].runtime_flags
            != MICROS_THREAD_RTS_INACTIVE
        || fixture->objects.threads[expected->thread.slot].scheduler_assigned
    ) {
        return endpoint_model_model_fail(
            fixture,
            step,
            operation,
            value,
            "thread state diverged"
        );
    }
    return true;
}

static bool endpoint_phase_matches(
    const struct endpoint_model_fixture *fixture,
    const struct model_slot *expected,
    const struct micros_process *process,
    const struct micros_endpoint_record *endpoint,
    size_t step,
    enum model_operation operation,
    uint32_t value
)
{
    if (expected->endpoint_phase == MODEL_ENDPOINT_UNUSED) {
        if (
            endpoint->state != MICROS_ENDPOINT_STATE_FREE
            || process->primary_endpoint
                != MICROS_PROCESS_ENDPOINT_NONE
            || process->privilege_profile != 0
            || process->endpoint_lifecycle_consumed
        ) {
            return endpoint_model_model_fail(
                fixture,
                step,
                operation,
                value,
                "unused endpoint state diverged"
            );
        }
        return true;
    }
    if (expected->endpoint_phase == MODEL_ENDPOINT_CLOSED) {
        if (
            endpoint->state != MICROS_ENDPOINT_STATE_FREE
            || process->primary_endpoint
                != MICROS_PROCESS_ENDPOINT_NONE
            || process->privilege_profile != 0
            || !process->endpoint_lifecycle_consumed
        ) {
            return endpoint_model_model_fail(
                fixture,
                step,
                operation,
                value,
                "closed endpoint state diverged"
            );
        }
        return true;
    }
    if (
        endpoint->owner.slot != expected->process.slot
        || endpoint->owner.generation
            != expected->process.generation
        || endpoint->value != expected->endpoint
        || process->primary_endpoint != expected->endpoint
        || !process->endpoint_lifecycle_consumed
        || process->privilege_profile != expected->profile
    ) {
        return endpoint_model_model_fail(
            fixture,
            step,
            operation,
            value,
            "bound endpoint state diverged"
        );
    }
    if (
        expected->endpoint_phase == MODEL_ENDPOINT_ACTIVE
            ? endpoint->state != MICROS_ENDPOINT_STATE_ACTIVE
            : endpoint->state != MICROS_ENDPOINT_STATE_RESERVED
    ) {
        return endpoint_model_model_fail(
            fixture,
            step,
            operation,
            value,
            "endpoint phase diverged"
        );
    }
    return true;
}

bool endpoint_model_validate_model(
    const struct endpoint_model_fixture *fixture,
    const struct model_slot model[MODEL_SLOT_COUNT],
    size_t step,
    enum model_operation operation,
    uint32_t value
)
{
    size_t expected_processes = 0;
    size_t expected_threads = 0;
    size_t index;

    if (
        micros_endpoint_registry_validate_objects(&fixture->registry, &fixture->objects)
            != MICROS_ENDPOINT_OK
        || fixture->registry.last_reply_token != 0
    ) {
        return endpoint_model_model_fail(
            fixture,
            step,
            operation,
            value,
            "combined validation or token state failed"
        );
    }
    for (index = 0; index < MODEL_SLOT_COUNT; ++index) {
        const struct model_slot *expected = &model[index];
        const struct micros_process *process =
            &fixture->objects.processes[index];
        const struct micros_endpoint_record *endpoint =
            &fixture->registry.endpoints[index];

        if (!expected->process_live) {
            if (
                process->slot_state
                    != MICROS_KERNEL_OBJECT_SLOT_FREE
                || process->generation != expected->generation
                || endpoint->state != MICROS_ENDPOINT_STATE_FREE
            ) {
                return endpoint_model_model_fail(
                    fixture,
                    step,
                    operation,
                    value,
                    "free slot diverged"
                );
            }
            continue;
        }
        ++expected_processes;
        ++expected_threads;
        if (!live_process_and_thread_match(fixture, expected, process, step, operation, value)) {
            return false;
        }
        if (!endpoint_phase_matches(fixture, expected, process, endpoint, step, operation, value)) {
            return false;
        }
    }
    if (
        fixture->objects.live_process_count != expected_processes
        || fixture->objects.live_thread_count != expected_threads
    ) {
        return endpoint_model_model_fail(
            fixture,
            step,
            operation,
            value,
            "object counts diverged"
        );
    }
    return true;
}
