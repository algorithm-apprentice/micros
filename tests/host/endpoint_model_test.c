#include "micros/endpoint.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    MODEL_SLOT_COUNT = 4,
    MODEL_STEPS = 4096,
    MODEL_TRACE_COUNT = 32,
};

enum model_endpoint_phase {
    MODEL_ENDPOINT_UNUSED = 0,
    MODEL_ENDPOINT_RESERVED,
    MODEL_ENDPOINT_PROFILED,
    MODEL_ENDPOINT_ACTIVE,
    MODEL_ENDPOINT_CLOSED,
};

enum model_operation {
    MODEL_OPERATION_CREATE = 0,
    MODEL_OPERATION_RESERVE,
    MODEL_OPERATION_INSTALL,
    MODEL_OPERATION_ACTIVATE,
    MODEL_OPERATION_AUTHORIZE,
    MODEL_OPERATION_CLOSE,
    MODEL_OPERATION_RELEASE,
    MODEL_OPERATION_STALE,
    MODEL_OPERATION_COUNT,
};

struct model_slot {
    bool process_live;
    bool thread_live;
    uint32_t generation;
    struct micros_process_handle process;
    struct micros_thread_handle thread;
    enum model_endpoint_phase endpoint_phase;
    micros_endpoint_t endpoint;
    micros_endpoint_t stale_endpoint;
    uint8_t profile;
};

struct model_coverage {
    size_t create;
    size_t release;
    size_t reserve;
    size_t reserve_rejected;
    size_t install_client;
    size_t install_server;
    size_t install_invalid;
    size_t activate;
    size_t close;
    size_t authorize_allowed;
    size_t authorize_denied;
    size_t authorize_state;
    size_t notify_allowed;
    size_t notify_denied;
    size_t reply_allowed;
    size_t reply_denied;
    size_t stale;
};

struct model_trace_entry {
    size_t step;
    enum model_operation operation;
    size_t slot;
    uint32_t value;
};

static const char *const operation_names[MODEL_OPERATION_COUNT] = {
    "create",
    "reserve",
    "install",
    "activate",
    "authorize",
    "close",
    "release",
    "stale",
};

static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_hart_handle hart;
static struct model_trace_entry trace[MODEL_TRACE_COUNT];
static size_t trace_count;

bool micros_endpoint_model_test_run(void);

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void record_transition(
    size_t step,
    enum model_operation operation,
    size_t slot,
    uint32_t value
)
{
    size_t index = trace_count % MODEL_TRACE_COUNT;

    trace[index].step = step;
    trace[index].operation = operation;
    trace[index].slot = slot;
    trace[index].value = value;
    ++trace_count;
}

static bool model_fail(
    size_t step,
    enum model_operation operation,
    uint32_t value,
    const char *message
)
{
    size_t available = trace_count < MODEL_TRACE_COUNT
        ? trace_count
        : MODEL_TRACE_COUNT;
    size_t first = trace_count - available;
    size_t offset;

    fprintf(
        stderr,
        "endpoint model seed=0xe029cafe step=%zu "
        "operation=%s value=0x%08x: %s\n",
        step,
        operation_names[operation],
        (unsigned)value,
        message
    );
    for (offset = 0; offset < available; ++offset) {
        const struct model_trace_entry *entry =
            &trace[(first + offset) % MODEL_TRACE_COUNT];

        fprintf(
            stderr,
            "  trace step=%zu operation=%s slot=%zu value=0x%08x\n",
            entry->step,
            operation_names[entry->operation],
            entry->slot,
            (unsigned)entry->value
        );
    }
    return false;
}

static bool setup_fixture(void)
{
    static const struct micros_privilege_profile profiles[2] = {
        {
            .id = 1,
            .name = "CLIENT",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_CALL
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .call_targets = UINT32_C(1) << 2,
            .notify_targets = UINT32_C(1) << 1,
        },
        {
            .id = 2,
            .name = "SERVER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .send_targets = UINT32_C(1) << 1,
            .notify_targets = UINT32_C(1) << 2,
        },
    };

    memset(&registry, 0, sizeof(registry));
    memset(&objects, 0, sizeof(objects));
    memset(trace, 0, sizeof(trace));
    trace_count = 0;
    return (
        micros_endpoint_registry_initialize(
            &registry,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) == MICROS_ENDPOINT_OK
        && micros_kernel_objects_initialize(&objects, 1, 1)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_register(&objects, 0, &hart)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_install_trap_stacks(
            &objects,
            hart,
            UINT64_C(0x01000000),
            UINT64_C(0x01004000),
            UINT64_C(0x02000000),
            UINT64_C(0x02001000)
        ) == MICROS_KERNEL_OBJECT_OK
    );
}

static bool phase_has_endpoint(enum model_endpoint_phase phase)
{
    return (
        phase == MODEL_ENDPOINT_RESERVED
        || phase == MODEL_ENDPOINT_PROFILED
        || phase == MODEL_ENDPOINT_ACTIVE
    );
}

static size_t find_slot(
    const struct model_slot model[MODEL_SLOT_COUNT],
    uint32_t random,
    bool (*matches)(const struct model_slot *slot)
)
{
    size_t start = (random >> 16) % MODEL_SLOT_COUNT;
    size_t offset;

    for (offset = 0; offset < MODEL_SLOT_COUNT; ++offset) {
        size_t index = (start + offset) % MODEL_SLOT_COUNT;

        if (matches(&model[index])) {
            return index;
        }
    }
    return MODEL_SLOT_COUNT;
}

static bool slot_is_live(const struct model_slot *slot)
{
    return slot->process_live;
}

static bool slot_has_endpoint(const struct model_slot *slot)
{
    return slot->process_live
        && phase_has_endpoint(slot->endpoint_phase);
}

static bool slot_has_nonactive_endpoint(const struct model_slot *slot)
{
    return slot->process_live
        && (
            slot->endpoint_phase == MODEL_ENDPOINT_RESERVED
            || slot->endpoint_phase == MODEL_ENDPOINT_PROFILED
        );
}

static bool slot_is_releasable(const struct model_slot *slot)
{
    return slot->process_live
        && !phase_has_endpoint(slot->endpoint_phase);
}

static bool slot_has_stale_endpoint(const struct model_slot *slot)
{
    return slot->stale_endpoint != 0;
}

static size_t find_active_profile(
    const struct model_slot model[MODEL_SLOT_COUNT],
    uint32_t random,
    uint8_t profile
)
{
    size_t start = (random >> 16) % MODEL_SLOT_COUNT;
    size_t offset;

    for (offset = 0; offset < MODEL_SLOT_COUNT; ++offset) {
        size_t index = (start + offset) % MODEL_SLOT_COUNT;

        if (
            model[index].process_live
            && model[index].endpoint_phase == MODEL_ENDPOINT_ACTIVE
            && model[index].profile == profile
        ) {
            return index;
        }
    }
    return MODEL_SLOT_COUNT;
}

static bool target_is_allowed(
    uint8_t source_profile,
    uint32_t operation,
    uint8_t target_profile
)
{
    if (
        source_profile == 1
        && (
            (
                target_profile == 2
                && operation == MICROS_PRIVILEGE_OPERATION_CALL
            )
            || (
                target_profile == 1
                && operation == MICROS_PRIVILEGE_OPERATION_NOTIFY
            )
        )
    ) {
        return true;
    }
    return (
        source_profile == 2
        && (
            (
                target_profile == 1
                && operation == MICROS_PRIVILEGE_OPERATION_SEND
            )
            || (
                target_profile == 2
                && operation == MICROS_PRIVILEGE_OPERATION_NOTIFY
            )
        )
    );
}

static bool validate_model(
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
        micros_endpoint_registry_validate_objects(&registry, &objects)
            != MICROS_ENDPOINT_OK
        || registry.last_reply_token != 0
    ) {
        return model_fail(
            step,
            operation,
            value,
            "combined validation or token state failed"
        );
    }
    for (index = 0; index < MODEL_SLOT_COUNT; ++index) {
        const struct model_slot *expected = &model[index];
        const struct micros_process *process =
            &objects.processes[index];
        const struct micros_endpoint_record *endpoint =
            &registry.endpoints[index];

        if (!expected->process_live) {
            if (
                process->slot_state
                    != MICROS_KERNEL_OBJECT_SLOT_FREE
                || process->generation != expected->generation
                || endpoint->state != MICROS_ENDPOINT_STATE_FREE
            ) {
                return model_fail(
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
        if (
            process->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || process->generation != expected->generation
            || process->live_thread_count != 1
            || !expected->thread_live
        ) {
            return model_fail(
                step,
                operation,
                value,
                "live process diverged"
            );
        }
        if (
            objects.threads[expected->thread.slot].slot_state
                != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || objects.threads[expected->thread.slot].generation
                != expected->thread.generation
            || objects.threads[expected->thread.slot].owner.slot
                != expected->process.slot
            || objects.threads[expected->thread.slot].owner.generation
                != expected->process.generation
            || objects.threads[expected->thread.slot].runtime_flags
                != MICROS_THREAD_RTS_INACTIVE
            || objects.threads[expected->thread.slot].scheduler_assigned
        ) {
            return model_fail(
                step,
                operation,
                value,
                "thread state diverged"
            );
        }

        if (expected->endpoint_phase == MODEL_ENDPOINT_UNUSED) {
            if (
                endpoint->state != MICROS_ENDPOINT_STATE_FREE
                || process->primary_endpoint
                    != MICROS_PROCESS_ENDPOINT_NONE
                || process->privilege_profile != 0
                || process->endpoint_lifecycle_consumed
            ) {
                return model_fail(
                    step,
                    operation,
                    value,
                    "unused endpoint state diverged"
                );
            }
            continue;
        }
        if (expected->endpoint_phase == MODEL_ENDPOINT_CLOSED) {
            if (
                endpoint->state != MICROS_ENDPOINT_STATE_FREE
                || process->primary_endpoint
                    != MICROS_PROCESS_ENDPOINT_NONE
                || process->privilege_profile != 0
                || !process->endpoint_lifecycle_consumed
            ) {
                return model_fail(
                    step,
                    operation,
                    value,
                    "closed endpoint state diverged"
                );
            }
            continue;
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
            return model_fail(
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
            return model_fail(
                step,
                operation,
                value,
                "endpoint phase diverged"
            );
        }
    }
    if (
        objects.live_process_count != expected_processes
        || objects.live_thread_count != expected_threads
    ) {
        return model_fail(
            step,
            operation,
            value,
            "object counts diverged"
        );
    }
    return true;
}

bool micros_endpoint_model_test_run(void)
{
    const uint32_t seed = UINT32_C(0xe029cafe);
    static const enum model_operation scripted_operations[] = {
        MODEL_OPERATION_CREATE,
        MODEL_OPERATION_CREATE,
        MODEL_OPERATION_RESERVE,
        MODEL_OPERATION_RESERVE,
        MODEL_OPERATION_INSTALL,
        MODEL_OPERATION_INSTALL,
        MODEL_OPERATION_INSTALL,
        MODEL_OPERATION_ACTIVATE,
        MODEL_OPERATION_ACTIVATE,
        MODEL_OPERATION_AUTHORIZE,
        MODEL_OPERATION_AUTHORIZE,
        MODEL_OPERATION_AUTHORIZE,
        MODEL_OPERATION_AUTHORIZE,
        MODEL_OPERATION_AUTHORIZE,
        MODEL_OPERATION_AUTHORIZE,
    };
    static const uint32_t scripted_values[] = {
        0,
        0,
        UINT32_C(0) << 16,
        UINT32_C(1) << 16,
        3,
        UINT32_C(0) << 16,
        UINT32_C(1) << 16,
        UINT32_C(0) << 16,
        UINT32_C(1) << 16,
        UINT32_C(0) << 16,
        (UINT32_C(0) << 16) | 1,
        (UINT32_C(1) << 16) | 1,
        (UINT32_C(0) << 16) | 2,
        (UINT32_C(1) << 16) | 4,
        (UINT32_C(0) << 16) | 4,
    };
    struct model_slot model[MODEL_SLOT_COUNT];
    struct model_coverage coverage;
    uint32_t random_state = seed;
    size_t step;

    memset(model, 0, sizeof(model));
    memset(&coverage, 0, sizeof(coverage));
    if (!setup_fixture()) {
        fprintf(stderr, "endpoint model fixture failed\n");
        return false;
    }

    for (step = 0; step < MODEL_STEPS; ++step) {
        uint32_t random = next_random(&random_state);
        enum model_operation operation =
            (enum model_operation)(
                (random >> 24) % MODEL_OPERATION_COUNT
            );
        size_t slot = MODEL_SLOT_COUNT;
        uint32_t value = next_random(&random_state);

        if (
            step
                < sizeof(scripted_operations)
                    / sizeof(scripted_operations[0])
        ) {
            operation = scripted_operations[step];
            value = scripted_values[step];
        }
        switch (operation) {
        case MODEL_OPERATION_CREATE:
            for (slot = 0; slot < MODEL_SLOT_COUNT; ++slot) {
                if (!model[slot].process_live) {
                    break;
                }
            }
            if (slot != MODEL_SLOT_COUNT) {
                struct micros_process_handle process;
                struct micros_thread_handle thread;

                if (
                    micros_process_create(&objects, &process)
                        != MICROS_KERNEL_OBJECT_OK
                    || process.slot != slot
                    || process.generation
                        != model[slot].generation + 1
                    || micros_thread_create(
                        &objects,
                        process,
                        &thread
                    ) != MICROS_KERNEL_OBJECT_OK
                ) {
                    return model_fail(
                        step,
                        operation,
                        value,
                        "create transition failed"
                    );
                }
                model[slot].process_live = true;
                model[slot].thread_live = true;
                model[slot].generation = process.generation;
                model[slot].process = process;
                model[slot].thread = thread;
                model[slot].endpoint_phase =
                    MODEL_ENDPOINT_UNUSED;
                model[slot].endpoint = 0;
                model[slot].profile = 0;
                ++coverage.create;
            }
            break;
        case MODEL_OPERATION_RESERVE:
            slot = find_slot(model, value, slot_is_live);
            if (slot != MODEL_SLOT_COUNT) {
                micros_endpoint_t endpoint = MICROS_ENDPOINT_ANY;
                bool allowed =
                    model[slot].endpoint_phase
                        == MODEL_ENDPOINT_UNUSED;
                enum micros_endpoint_error error =
                    micros_endpoint_reserve(
                        &registry,
                        &objects,
                        model[slot].process,
                        &endpoint
                    );

                if (
                    error
                        != (
                            allowed
                                ? MICROS_ENDPOINT_OK
                                : MICROS_ENDPOINT_ERROR_STATE
                        )
                    || (!allowed && endpoint != MICROS_ENDPOINT_ANY)
                ) {
                    return model_fail(
                        step,
                        operation,
                        value,
                        "reserve result diverged"
                    );
                }
                if (allowed) {
                    model[slot].endpoint_phase =
                        MODEL_ENDPOINT_RESERVED;
                    model[slot].endpoint = endpoint;
                    model[slot].profile = 0;
                    ++coverage.reserve;
                } else {
                    ++coverage.reserve_rejected;
                }
            }
            break;
        case MODEL_OPERATION_INSTALL:
            slot = find_slot(model, value, slot_has_endpoint);
            if (slot != MODEL_SLOT_COUNT) {
                uint8_t profile = (value & 3) == 3
                    ? 3
                    : (uint8_t)(slot % 2 + 1);
                bool installable =
                    model[slot].endpoint_phase
                        == MODEL_ENDPOINT_RESERVED;
                enum micros_endpoint_error expected =
                    installable
                        ? (
                            profile <= 2
                                ? MICROS_ENDPOINT_OK
                                : MICROS_ENDPOINT_ERROR_PROFILE
                        )
                        : MICROS_ENDPOINT_ERROR_STATE;
                enum micros_endpoint_error error =
                    micros_endpoint_install_profile(
                        &registry,
                        &objects,
                        model[slot].process,
                        profile
                    );

                value = profile;
                if (error != expected) {
                    return model_fail(
                        step,
                        operation,
                        value,
                        "profile installation diverged"
                    );
                }
                if (error == MICROS_ENDPOINT_OK) {
                    model[slot].endpoint_phase =
                        MODEL_ENDPOINT_PROFILED;
                    model[slot].profile = profile;
                    if (profile == 1) {
                        ++coverage.install_client;
                    } else {
                        ++coverage.install_server;
                    }
                } else if (
                    error == MICROS_ENDPOINT_ERROR_PROFILE
                ) {
                    ++coverage.install_invalid;
                }
            }
            break;
        case MODEL_OPERATION_ACTIVATE:
            slot = find_slot(model, value, slot_has_endpoint);
            if (slot != MODEL_SLOT_COUNT) {
                bool allowed =
                    model[slot].endpoint_phase
                        == MODEL_ENDPOINT_PROFILED;
                enum micros_endpoint_error error =
                    micros_endpoint_activate(
                        &registry,
                        &objects,
                        model[slot].endpoint
                    );

                if (
                    error
                        != (
                            allowed
                                ? MICROS_ENDPOINT_OK
                                : MICROS_ENDPOINT_ERROR_STATE
                        )
                ) {
                    return model_fail(
                        step,
                        operation,
                        value,
                        "activation result diverged"
                    );
                }
                if (allowed) {
                    model[slot].endpoint_phase =
                        MODEL_ENDPOINT_ACTIVE;
                    ++coverage.activate;
                }
            }
            break;
        case MODEL_OPERATION_AUTHORIZE:
        {
            static const uint32_t operations[3] = {
                MICROS_PRIVILEGE_OPERATION_CALL,
                MICROS_PRIVILEGE_OPERATION_SEND,
                MICROS_PRIVILEGE_OPERATION_NOTIFY,
            };
            size_t target;
            uint32_t target_random = next_random(&random_state);
            uint32_t authorization_operation =
                operations[
                    (next_random(&random_state) >> 16) % 3
                ];
            enum micros_endpoint_error expected;
            enum micros_endpoint_error error;

            slot = find_slot(model, value, slot_has_endpoint);
            if ((value & 4) != 0) {
                authorization_operation =
                    MICROS_PRIVILEGE_OPERATION_REPLY;
                if (
                    slot == MODEL_SLOT_COUNT
                    || model[slot].endpoint_phase
                        != MODEL_ENDPOINT_ACTIVE
                ) {
                    expected = MICROS_ENDPOINT_ERROR_STATE;
                } else if (model[slot].profile == 2) {
                    expected = MICROS_ENDPOINT_OK;
                } else {
                    expected = MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
                }
                if (slot == MODEL_SLOT_COUNT) {
                    break;
                }
                error = micros_endpoint_authorize_operation(
                    &registry,
                    &objects,
                    model[slot].endpoint,
                    authorization_operation
                );
                value = authorization_operation;
                if (error != expected) {
                    return model_fail(
                        step,
                        operation,
                        value,
                        "operation authorization diverged"
                    );
                }
                if (error == MICROS_ENDPOINT_OK) {
                    ++coverage.reply_allowed;
                } else if (
                    error == MICROS_ENDPOINT_ERROR_UNAUTHORIZED
                ) {
                    ++coverage.reply_denied;
                } else {
                    ++coverage.authorize_state;
                }
                break;
            }
            target = MODEL_SLOT_COUNT;
            if (
                slot != MODEL_SLOT_COUNT
                && model[slot].endpoint_phase
                    == MODEL_ENDPOINT_ACTIVE
            ) {
                if ((value & 2) != 0) {
                    target = slot;
                    authorization_operation =
                        MICROS_PRIVILEGE_OPERATION_NOTIFY;
                }
            }
            if (
                target == MODEL_SLOT_COUNT
                && slot != MODEL_SLOT_COUNT
                && model[slot].endpoint_phase
                    == MODEL_ENDPOINT_ACTIVE
            ) {
                uint8_t target_profile =
                    model[slot].profile == 1 ? 2 : 1;

                target = find_active_profile(
                    model,
                    target_random,
                    target_profile
                );
                if (target != MODEL_SLOT_COUNT) {
                    if ((value & 1) != 0) {
                        authorization_operation =
                            MICROS_PRIVILEGE_OPERATION_NOTIFY;
                    } else {
                        authorization_operation =
                            model[slot].profile == 1
                                ? MICROS_PRIVILEGE_OPERATION_CALL
                                : MICROS_PRIVILEGE_OPERATION_SEND;
                    }
                }
            }
            if (target == MODEL_SLOT_COUNT) {
                target = find_slot(
                    model,
                    target_random,
                    slot_has_endpoint
                );
            }
            if (
                slot == MODEL_SLOT_COUNT
                || target == MODEL_SLOT_COUNT
            ) {
                break;
            }
            if (
                model[slot].endpoint_phase
                    != MODEL_ENDPOINT_ACTIVE
                || model[target].endpoint_phase
                    != MODEL_ENDPOINT_ACTIVE
            ) {
                expected = MICROS_ENDPOINT_ERROR_STATE;
            } else if (
                target_is_allowed(
                    model[slot].profile,
                    authorization_operation,
                    model[target].profile
                )
            ) {
                expected = MICROS_ENDPOINT_OK;
            } else {
                expected = MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
            }
            error = micros_endpoint_authorize_target(
                &registry,
                &objects,
                model[slot].endpoint,
                authorization_operation,
                model[target].endpoint
            );
            value = authorization_operation;
            if (error != expected) {
                return model_fail(
                    step,
                    operation,
                    value,
                    "authorization result diverged"
                );
            }
            if (error == MICROS_ENDPOINT_OK) {
                ++coverage.authorize_allowed;
                if (
                    authorization_operation
                        == MICROS_PRIVILEGE_OPERATION_NOTIFY
                ) {
                    ++coverage.notify_allowed;
                }
            } else if (error == MICROS_ENDPOINT_ERROR_UNAUTHORIZED) {
                ++coverage.authorize_denied;
                if (
                    authorization_operation
                        == MICROS_PRIVILEGE_OPERATION_NOTIFY
                ) {
                    ++coverage.notify_denied;
                }
            } else {
                ++coverage.authorize_state;
            }
            break;
        }
        case MODEL_OPERATION_CLOSE:
            slot = find_slot(
                model,
                value,
                slot_has_nonactive_endpoint
            );
            if (slot == MODEL_SLOT_COUNT) {
                slot = find_slot(model, value, slot_has_endpoint);
            }
            if (slot != MODEL_SLOT_COUNT) {
                if (
                    micros_endpoint_close(
                        &registry,
                        &objects,
                        model[slot].endpoint
                    ) != MICROS_ENDPOINT_OK
                ) {
                    return model_fail(
                        step,
                        operation,
                        value,
                        "close transition failed"
                    );
                }
                model[slot].endpoint_phase = MODEL_ENDPOINT_CLOSED;
                model[slot].stale_endpoint = model[slot].endpoint;
                model[slot].endpoint = 0;
                model[slot].profile = 0;
                ++coverage.close;
            }
            break;
        case MODEL_OPERATION_RELEASE:
            slot = find_slot(model, value, slot_is_releasable);
            if (slot != MODEL_SLOT_COUNT) {
                if (
                    micros_thread_release(
                        &objects,
                        model[slot].thread
                    ) != MICROS_KERNEL_OBJECT_OK
                    || micros_process_release(
                        &objects,
                        model[slot].process
                    ) != MICROS_KERNEL_OBJECT_OK
                ) {
                    return model_fail(
                        step,
                        operation,
                        value,
                        "release transition failed"
                    );
                }
                model[slot].process_live = false;
                model[slot].thread_live = false;
                model[slot].endpoint_phase =
                    MODEL_ENDPOINT_UNUSED;
                model[slot].endpoint = 0;
                model[slot].profile = 0;
                ++coverage.release;
            }
            break;
        case MODEL_OPERATION_STALE:
            slot = find_slot(
                model,
                value,
                slot_has_stale_endpoint
            );
            if (slot != MODEL_SLOT_COUNT) {
                const struct micros_endpoint_record *record =
                    (const struct micros_endpoint_record *)(uintptr_t)1;

                if (
                    micros_endpoint_resolve_internal(
                        &registry,
                        &objects,
                        model[slot].stale_endpoint,
                        &record
                    ) != MICROS_ENDPOINT_ERROR_STALE
                    || record
                        != (
                            const struct micros_endpoint_record *
                        )(uintptr_t)1
                ) {
                    return model_fail(
                        step,
                        operation,
                        value,
                        "stale resolution diverged"
                    );
                }
                ++coverage.stale;
            }
            break;
        default:
            return model_fail(
                step,
                operation,
                value,
                "unknown model operation"
            );
        }
        record_transition(step, operation, slot, value);
        if (!validate_model(model, step, operation, value)) {
            return false;
        }
    }

    for (step = 0; step < MODEL_SLOT_COUNT; ++step) {
        if (!model[step].process_live) {
            continue;
        }
        if (phase_has_endpoint(model[step].endpoint_phase)) {
            if (
                micros_endpoint_close(
                    &registry,
                    &objects,
                    model[step].endpoint
                ) != MICROS_ENDPOINT_OK
            ) {
                return model_fail(
                    MODEL_STEPS,
                    MODEL_OPERATION_CLOSE,
                    (uint32_t)step,
                    "cleanup close failed"
                );
            }
        }
        if (
            micros_thread_release(&objects, model[step].thread)
                != MICROS_KERNEL_OBJECT_OK
            || micros_process_release(
                &objects,
                model[step].process
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return model_fail(
                MODEL_STEPS,
                MODEL_OPERATION_RELEASE,
                (uint32_t)step,
                "cleanup release failed"
            );
        }
    }
    if (
        micros_endpoint_registry_validate_objects(&registry, &objects)
            != MICROS_ENDPOINT_OK
        || objects.live_process_count != 0
        || objects.live_thread_count != 0
        || objects.registered_hart_count != 1
    ) {
        return model_fail(
            MODEL_STEPS,
            MODEL_OPERATION_RELEASE,
            0,
            "cleanup baseline validation failed"
        );
    }
    if (
        coverage.create == 0
        || coverage.release == 0
        || coverage.reserve == 0
        || coverage.reserve_rejected == 0
        || coverage.install_client == 0
        || coverage.install_server == 0
        || coverage.install_invalid == 0
        || coverage.activate == 0
        || coverage.close == 0
        || coverage.authorize_allowed == 0
        || coverage.authorize_denied == 0
        || coverage.authorize_state == 0
        || coverage.notify_allowed == 0
        || coverage.notify_denied == 0
        || coverage.reply_allowed == 0
        || coverage.reply_denied == 0
        || coverage.stale == 0
    ) {
        fprintf(
            stderr,
            "endpoint model seed=0xe029cafe coverage incomplete "
            "create=%zu release=%zu reserve=%zu reject=%zu "
            "client=%zu server=%zu invalid=%zu activate=%zu "
            "close=%zu allow=%zu deny=%zu state=%zu "
            "notify-allow=%zu notify-deny=%zu "
            "reply-allow=%zu reply-deny=%zu stale=%zu\n",
            coverage.create,
            coverage.release,
            coverage.reserve,
            coverage.reserve_rejected,
            coverage.install_client,
            coverage.install_server,
            coverage.install_invalid,
            coverage.activate,
            coverage.close,
            coverage.authorize_allowed,
            coverage.authorize_denied,
            coverage.authorize_state,
            coverage.notify_allowed,
            coverage.notify_denied,
            coverage.reply_allowed,
            coverage.reply_denied,
            coverage.stale
        );
        return false;
    }
    return true;
}
