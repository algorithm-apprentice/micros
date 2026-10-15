#include "endpoint_model_internal.h"

#include <stdio.h>
#include <string.h>

static struct endpoint_model_fixture model_fixture_state;

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

struct endpoint_model_fixture *endpoint_model_context(void)
{
    return &model_fixture_state;
}

void endpoint_model_record_transition(
    struct endpoint_model_fixture *fixture,
    size_t step,
    enum model_operation operation,
    size_t slot,
    uint32_t value
)
{
    size_t index = fixture->trace_count % MODEL_TRACE_COUNT;

    fixture->trace[index].step = step;
    fixture->trace[index].operation = operation;
    fixture->trace[index].slot = slot;
    fixture->trace[index].value = value;
    ++fixture->trace_count;
}

bool endpoint_model_model_fail(
    const struct endpoint_model_fixture *fixture,
    size_t step,
    enum model_operation operation,
    uint32_t value,
    const char *message
)
{
    size_t available = fixture->trace_count < MODEL_TRACE_COUNT
        ? fixture->trace_count
        : MODEL_TRACE_COUNT;
    size_t first = fixture->trace_count - available;
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
            &fixture->trace[(first + offset) % MODEL_TRACE_COUNT];

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

bool endpoint_model_setup_fixture(struct endpoint_model_fixture *fixture)
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
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .send_targets = UINT32_C(1) << 1,
            .notify_targets = UINT32_C(1) << 2,
        },
    };

    memset(&fixture->registry, 0, sizeof(fixture->registry));
    memset(&fixture->objects, 0, sizeof(fixture->objects));
    memset(fixture->trace, 0, sizeof(fixture->trace));
    fixture->trace_count = 0;
    return (
        micros_endpoint_registry_initialize(
            &fixture->registry,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) == MICROS_ENDPOINT_OK
        && micros_kernel_objects_initialize(&fixture->objects, 1, 1)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_register(&fixture->objects, 0, &fixture->hart)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_install_trap_stacks(
            &fixture->objects,
            fixture->hart,
            UINT64_C(0x01000000),
            UINT64_C(0x01004000),
            UINT64_C(0x02000000),
            UINT64_C(0x02001000)
        ) == MICROS_KERNEL_OBJECT_OK
    );
}
