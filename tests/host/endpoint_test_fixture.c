#include "endpoint_test_internal.h"

#include <string.h>

static struct endpoint_test_fixture fixture_state;

struct endpoint_test_fixture *endpoint_test_context(void)
{
    return &fixture_state;
}

struct micros_privilege_profile endpoint_test_profile(
    uint8_t id,
    const char *name,
    uint32_t operations,
    uint32_t call_targets,
    uint32_t send_targets,
    uint32_t notify_targets,
    uint64_t kernel_operations
)
{
    struct micros_privilege_profile result;
    size_t index;

    memset(&result, 0, sizeof(result));
    result.id = id;
    for (
        index = 0;
        index + 1 < sizeof(result.name) && name[index] != '\0';
        ++index
    ) {
        result.name[index] = name[index];
    }
    result.operations = operations;
    result.call_targets = call_targets;
    result.send_targets = send_targets;
    result.notify_targets = notify_targets;
    result.kernel_operations = kernel_operations;
    return result;
}

struct micros_user_context endpoint_test_context_pattern(uint64_t base)
{
    struct micros_user_context context;
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    for (
        index = 0;
        index < sizeof(context) / sizeof(uint64_t);
        ++index
    ) {
        words[index] = base + index;
    }
    memcpy(&context, words, sizeof(context));
    return context;
}

void endpoint_test_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

bool endpoint_test_setup_lifecycle_fixture(struct endpoint_test_fixture *fixture)
{
    struct micros_privilege_profile profiles[2];
    size_t index;

    memset(&fixture->registry, 0, sizeof(fixture->registry));
    memset(&fixture->objects, 0, sizeof(fixture->objects));
    profiles[0] = endpoint_test_profile(
        1,
        "CLIENT",
        MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_NOTIFY,
        UINT32_C(1) << 2,
        0,
        UINT32_C(1) << 1,
        UINT64_C(1) << 3
    );
    profiles[1] = endpoint_test_profile(
        2,
        "SERVER",
        MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_SEND
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_NOTIFY,
        0,
        UINT32_C(1) << 1,
        UINT32_C(1) << 2,
        0
    );
    if (
        micros_endpoint_registry_initialize(
            &fixture->registry,
            profiles,
            2
        ) != MICROS_ENDPOINT_OK
        || micros_kernel_objects_initialize(&fixture->objects, 2, 1)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&fixture->objects, 0, &fixture->hart)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            &fixture->objects,
            fixture->hart,
            UINT64_C(0x01000000),
            UINT64_C(0x01004000),
            UINT64_C(0x02000000),
            UINT64_C(0x02001000)
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    for (index = 0; index < 2; ++index) {
        if (
            micros_process_create(&fixture->objects, &fixture->processes[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_thread_create(
                &fixture->objects,
                fixture->processes[index],
                &fixture->threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    return true;
}
