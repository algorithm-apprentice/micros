#include "ipc_operation_test_internal.h"

static struct ipc_operation_test_context operation_context;

struct ipc_operation_test_context *ipc_operation_test_context_get(void)
{
    return &operation_context;
}

bool ipc_operation_test_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
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

struct micros_ipc_message ipc_operation_test_message_pattern(
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

static bool prepare_thread(
    struct ipc_operation_test_context *fixture,
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

bool ipc_operation_test_setup_operation_fixture(struct ipc_operation_test_context *fixture)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "CLIENT",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND,
            .send_targets = UINT32_C(1) << 2,
        },
        {
            .id = 2,
            .name = "SERVER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND,
            .send_targets = UINT32_C(1) << 1,
        },
        {
            .id = 3,
            .name = "RECEIVE_ONLY",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
        {
            .id = 4,
            .name = "ISOLATED_SEND",
            .operations = MICROS_PRIVILEGE_OPERATION_SEND,
            .send_targets = UINT32_C(1) << 4,
        },
    };
    static const uint8_t profile_ids[OPERATION_PROCESS_COUNT] = {
        1,
        1,
        2,
        3,
        4,
        1,
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
    for (index = 0; index < OPERATION_PROCESS_COUNT; ++index) {
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
                index != OPERATION_PROCESS_RESERVED
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
    for (index = 0; index < OPERATION_PROCESS_COUNT; ++index) {
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
    return true;
}

bool ipc_operation_test_prepare_destination_extra_thread(struct ipc_operation_test_context *fixture)
{
    return prepare_thread(
        fixture,
        fixture->processes[OPERATION_PROCESS_DESTINATION],
        UINT64_C(0x2000),
        UINT64_C(0x11000000),
        &fixture->destination_extra_thread
    );
}

struct micros_ipc_message ipc_operation_test_canonical_message(
    const struct micros_ipc_message *message,
    micros_endpoint_t source
)
{
    struct micros_ipc_message canonical = *message;

    canonical.source = source;
    canonical.reply_token = 0;
    return canonical;
}

static bool state_is_unchanged(
    struct ipc_operation_test_context *fixture,
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

bool ipc_operation_test_expect_send_failure_unchanged(
    struct ipc_operation_test_context *fixture,
    enum micros_ipc_error expected,
    struct micros_thread_handle sender,
    micros_endpoint_t destination,
    const struct micros_ipc_message *message
)
{
    struct micros_endpoint_registry registry_snapshot = fixture->registry;
    struct micros_kernel_objects objects_snapshot = fixture->objects;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_send(
            &fixture->registry,
            &fixture->objects,
            sender,
            destination,
            message
        )
    );
    EXPECT_TRUE(state_is_unchanged(
        fixture,
        &registry_snapshot,
        &objects_snapshot
    ));
    return true;
}

bool ipc_operation_test_expect_receive_failure_unchanged(
    struct ipc_operation_test_context *fixture,
    enum micros_ipc_error expected,
    struct micros_thread_handle receiver,
    micros_endpoint_t source,
    uintptr_t receive_buffer
)
{
    struct micros_endpoint_registry registry_snapshot = fixture->registry;
    struct micros_kernel_objects objects_snapshot = fixture->objects;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            receiver,
            source,
            receive_buffer
        )
    );
    EXPECT_TRUE(state_is_unchanged(
        fixture,
        &registry_snapshot,
        &objects_snapshot
    ));
    return true;
}

bool ipc_operation_test_make_current_held(struct ipc_operation_test_context *fixture, struct micros_thread_handle thread)
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
