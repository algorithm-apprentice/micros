#include "ipc_call_test_internal.h"

static struct ipc_call_test_context call_context;

struct ipc_call_test_context *ipc_call_test_context_get(void)
{
    return &call_context;
}

bool ipc_call_test_thread_handle_is_zero(struct micros_thread_handle handle)
{
    return handle.slot == 0 && handle.generation == 0;
}

bool ipc_call_test_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool ipc_call_test_bytes_are_zero(const void *storage, size_t size)
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

static struct micros_user_context context_pattern(uint64_t base)
{
    struct micros_user_context context;
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    for (
        index = 0;
        index < sizeof(words) / sizeof(words[0]);
        ++index
    ) {
        words[index] = base + index;
    }
    memcpy(&context, words, sizeof(context));
    return context;
}

struct micros_ipc_message ipc_call_test_message_pattern(
    uint32_t source,
    uint32_t type,
    uint64_t reply_token,
    uint8_t payload_seed
)
{
    struct micros_ipc_message message;
    size_t index;

    memset(&message, 0, sizeof(message));
    message.source = source;
    message.type = type;
    message.reply_token = reply_token;
    for (index = 0; index < sizeof(message.payload); ++index) {
        message.payload[index] =
            (uint8_t)(payload_seed + (uint8_t)index);
    }
    return message;
}

struct micros_ipc_message ipc_call_test_canonical_request(
    const struct micros_ipc_message *message,
    micros_endpoint_t source,
    uint64_t reply_token
)
{
    struct micros_ipc_message canonical = *message;

    canonical.source = source;
    canonical.reply_token = reply_token;
    return canonical;
}

static bool prepare_thread(
    struct ipc_call_test_context *fixture,
    struct micros_process_handle owner,
    uint64_t context_base,
    uintptr_t stack_bottom,
    struct micros_thread_handle *thread
)
{
    struct micros_user_context context =
        context_pattern(context_base);

    return (
        micros_thread_create(&fixture->objects, owner, thread)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_attach_execution_context(
            &fixture->objects,
            *thread,
            stack_bottom,
            stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
            &context
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_admit(
            &fixture->objects,
            fixture->hart,
            *thread,
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            100,
            true
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(&fixture->objects, *thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

bool ipc_call_test_setup_call_fixture(struct ipc_call_test_context *fixture)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "CLIENT",
            .operations = MICROS_PRIVILEGE_OPERATION_CALL,
            .call_targets = UINT32_C(1) << 2,
        },
        {
            .id = 2,
            .name = "SERVER",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
        {
            .id = 3,
            .name = "NO_CALL",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
        {
            .id = 4,
            .name = "ISOLATED_CALL",
            .operations = MICROS_PRIVILEGE_OPERATION_CALL,
            .call_targets = UINT32_C(1) << 4,
        },
    };
    static const uint8_t profile_ids[CALL_PROCESS_COUNT] = {
        1,
        2,
        3,
        4,
        2,
    };
    size_t index;

    memset(&fixture->registry, 0, sizeof(fixture->registry));
    memset(&fixture->objects, 0, sizeof(fixture->objects));
    if (
        micros_endpoint_registry_initialize(
            &fixture->registry,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
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
    for (index = 0; index < CALL_PROCESS_COUNT; ++index) {
        if (
            micros_process_create(&fixture->objects, &fixture->processes[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                &fixture->registry,
                &fixture->objects,
                fixture->processes[index],
                &fixture->endpoints[index]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                &fixture->registry,
                &fixture->objects,
                fixture->processes[index],
                profile_ids[index]
            ) != MICROS_ENDPOINT_OK
            || (
                index != CALL_PROCESS_RESERVED
                && micros_endpoint_activate(
                    &fixture->registry,
                    &fixture->objects,
                    fixture->endpoints[index]
                ) != MICROS_ENDPOINT_OK
            )
        ) {
            return false;
        }
    }
    for (index = 0; index < CALL_PROCESS_COUNT; ++index) {
        if (
            !prepare_thread(
                fixture,
                fixture->processes[index],
                UINT64_C(0x1000) + index * UINT64_C(0x100),
                UINT64_C(0x10000000)
                    + index * UINT64_C(0x00008000),
                &fixture->primary_threads[index]
            )
        ) {
            return false;
        }
    }
    return (
        prepare_thread(
            fixture,
            fixture->processes[CALL_PROCESS_CLIENT],
            UINT64_C(0x2000),
            UINT64_C(0x11000000),
            &fixture->client_extra_thread
        )
        && micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
}

bool ipc_call_test_complete_state_is_unchanged(
    struct ipc_call_test_context *fixture,
    const struct micros_endpoint_registry *registry_snapshot,
    const struct micros_kernel_objects *objects_snapshot
)
{
    return (
        memcmp(
            &fixture->registry,
            registry_snapshot,
            sizeof(fixture->registry)
        ) == 0
        && memcmp(
            &fixture->objects,
            objects_snapshot,
            sizeof(fixture->objects)
        ) == 0
    );
}

bool ipc_call_test_expect_call_failure_unchanged(
    struct ipc_call_test_context *fixture,
    enum micros_ipc_error expected,
    struct micros_thread_handle caller,
    micros_endpoint_t destination,
    struct micros_ipc_message *message
)
{
    struct micros_endpoint_registry registry_snapshot = fixture->registry;
    struct micros_kernel_objects objects_snapshot = fixture->objects;
    struct micros_ipc_message message_snapshot = *message;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            caller,
            destination,
            message,
            (uintptr_t)message
        )
    );
    EXPECT_TRUE(ipc_call_test_complete_state_is_unchanged(
        fixture,
        &registry_snapshot,
        &objects_snapshot
    ));
    EXPECT_TRUE(
        memcmp(message, &message_snapshot, sizeof(*message)) == 0
    );
    return true;
}

bool ipc_call_test_make_current_held(struct ipc_call_test_context *fixture, struct micros_thread_handle thread)
{
    struct micros_scheduler_return_plan plan;

    return (
        micros_scheduler_accounting_initialize(&fixture->objects, fixture->hart, 100)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_runtime_flags_unset(
            &fixture->objects,
            thread,
            MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_hart_plan_user_return(&fixture->objects, fixture->hart, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_commit_user_return(&fixture->objects, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(&fixture->objects, thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}
