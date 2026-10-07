#include "micros/ipc_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/scheduler_core.h"

enum {
    OPERATION_PROCESS_SOURCE_A = 0,
    OPERATION_PROCESS_SOURCE_B,
    OPERATION_PROCESS_DESTINATION,
    OPERATION_PROCESS_NO_SEND,
    OPERATION_PROCESS_ISOLATED_SEND,
    OPERATION_PROCESS_RESERVED,
    OPERATION_PROCESS_COUNT,
};

static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_process_handle processes[OPERATION_PROCESS_COUNT];
static struct micros_thread_handle primary_threads[OPERATION_PROCESS_COUNT];
static struct micros_thread_handle destination_extra_thread;
static micros_endpoint_t endpoints[OPERATION_PROCESS_COUNT];
static struct micros_hart_handle hart;

bool micros_ipc_operation_test_run(void);

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

#define EXPECT_IPC_ERROR(expected, expression) \
    do { \
        enum micros_ipc_error actual = (expression); \
        if (actual != (expected)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %d got %d\n", \
                __FILE__, \
                __LINE__, \
                (int)(expected), \
                (int)actual \
            ); \
            return false; \
        } \
    } while (false)

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

static struct micros_ipc_message message_pattern(
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
    struct micros_process_handle owner,
    uint64_t context_base,
    uintptr_t stack_bottom,
    struct micros_thread_handle *thread
)
{
    struct micros_user_context context =
        context_pattern(context_base);

    return (
        micros_thread_create(&objects, owner, thread)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_attach_execution_context(
            &objects,
            *thread,
            stack_bottom,
            stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
            &context
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_admit(
            &objects,
            hart,
            *thread,
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            100,
            true
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(&objects, *thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool setup_operation_fixture(void)
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

    memset(&registry, 0, sizeof(registry));
    memset(&objects, 0, sizeof(objects));
    if (
        micros_endpoint_registry_initialize(
            &registry,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || micros_kernel_objects_initialize(&objects, 2, 1)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&objects, 0, &hart)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            &objects,
            hart,
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
            micros_process_create(&objects, &processes[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                &registry,
                &objects,
                processes[index],
                &endpoints[index]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                &registry,
                &objects,
                processes[index],
                profile_ids[index]
            ) != MICROS_ENDPOINT_OK
            || (
                index != OPERATION_PROCESS_RESERVED
                && micros_endpoint_activate(
                    &registry,
                    &objects,
                    endpoints[index]
                ) != MICROS_ENDPOINT_OK
            )
        ) {
            return false;
        }
    }
    for (index = 0; index < OPERATION_PROCESS_COUNT; ++index) {
        if (
            !prepare_thread(
                processes[index],
                UINT64_C(0x1000) + index * UINT64_C(0x100),
                UINT64_C(0x10000000)
                    + index * UINT64_C(0x00008000),
                &primary_threads[index]
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool prepare_destination_extra_thread(void)
{
    return prepare_thread(
        processes[OPERATION_PROCESS_DESTINATION],
        UINT64_C(0x2000),
        UINT64_C(0x11000000),
        &destination_extra_thread
    );
}

static struct micros_ipc_message canonical_message(
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
    const struct micros_endpoint_registry *registry_snapshot,
    const struct micros_kernel_objects *objects_snapshot
)
{
    return (
        memcmp(
            &registry,
            registry_snapshot,
            sizeof(registry)
        ) == 0
        && memcmp(
            &objects,
            objects_snapshot,
            sizeof(objects)
        ) == 0
    );
}

static bool expect_send_failure_unchanged(
    enum micros_ipc_error expected,
    struct micros_thread_handle sender,
    micros_endpoint_t destination,
    const struct micros_ipc_message *message
)
{
    struct micros_endpoint_registry registry_snapshot = registry;
    struct micros_kernel_objects objects_snapshot = objects;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_send(
            &registry,
            &objects,
            sender,
            destination,
            message
        )
    );
    EXPECT_TRUE(state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    return true;
}

static bool expect_receive_failure_unchanged(
    enum micros_ipc_error expected,
    struct micros_thread_handle receiver,
    micros_endpoint_t source,
    uintptr_t receive_buffer
)
{
    struct micros_endpoint_registry registry_snapshot = registry;
    struct micros_kernel_objects objects_snapshot = objects;

    EXPECT_IPC_ERROR(
        expected,
        micros_ipc_receive(
            &registry,
            &objects,
            receiver,
            source,
            receive_buffer
        )
    );
    EXPECT_TRUE(state_is_unchanged(
        &registry_snapshot,
        &objects_snapshot
    ));
    return true;
}

static bool make_current_held(struct micros_thread_handle thread)
{
    struct micros_scheduler_return_plan plan;

    return (
        micros_scheduler_accounting_initialize(&objects, hart, 100)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_runtime_flags_unset(
            &objects,
            thread,
            MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_hart_plan_user_return(&objects, hart, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_commit_user_return(&objects, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(&objects, thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool test_authorized_send_blocks_with_snapshot(void)
{
    struct micros_ipc_message message =
        message_pattern(
            MICROS_ENDPOINT_ANY,
            UINT32_C(0x1234),
            UINT64_C(0x1122334455667788),
            UINT8_C(0x20)
        );
    const struct micros_ipc_message original = message;
    struct micros_ipc_message expected;
    const struct micros_endpoint_record *destination;
    const struct micros_thread *sender;

    EXPECT_TRUE(setup_operation_fixture());
    expected = canonical_message(
        &message,
        endpoints[OPERATION_PROCESS_SOURCE_A]
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[OPERATION_PROCESS_SOURCE_A],
            endpoints[OPERATION_PROCESS_DESTINATION],
            &message
        )
    );
    sender = &objects.threads[
        primary_threads[OPERATION_PROCESS_SOURCE_A].slot
    ];
    destination = &registry.endpoints[
        processes[OPERATION_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        memcmp(&message, &original, sizeof(message)) == 0
        && sender->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
        && sender->ipc_queue_kind == MICROS_IPC_QUEUE_SENDER
        && thread_handles_equal(
            destination->sender_head,
            primary_threads[OPERATION_PROCESS_SOURCE_A]
        )
        && thread_handles_equal(
            destination->sender_tail,
            primary_threads[OPERATION_PROCESS_SOURCE_A]
        )
        && memcmp(
            &sender->ipc_outbound_message,
            &expected,
            sizeof(expected)
        ) == 0
    );
    memset(&message, 0xa5, sizeof(message));
    EXPECT_TRUE(
        memcmp(
            &sender->ipc_outbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_authorized_send_matches_first_receiver(void)
{
    struct micros_ipc_message message =
        message_pattern(
            MICROS_ENDPOINT_NONE,
            UINT32_C(0x2234),
            UINT64_C(0x8877665544332211),
            UINT8_C(0x30)
        );
    struct micros_ipc_message expected;
    const struct micros_endpoint_record *destination;
    const struct micros_thread *unmatched;
    const struct micros_thread *matched;
    const struct micros_thread *sender;

    EXPECT_TRUE(setup_operation_fixture());
    EXPECT_TRUE(prepare_destination_extra_thread());
    expected = canonical_message(
        &message,
        endpoints[OPERATION_PROCESS_SOURCE_A]
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[OPERATION_PROCESS_DESTINATION],
            endpoints[OPERATION_PROCESS_SOURCE_B],
            UINT64_C(0x40000000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            destination_extra_thread,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x40001000)
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_send(
            &registry,
            &objects,
            primary_threads[OPERATION_PROCESS_SOURCE_A],
            endpoints[OPERATION_PROCESS_DESTINATION],
            &message
        )
    );
    destination = &registry.endpoints[
        processes[OPERATION_PROCESS_DESTINATION].slot
    ];
    unmatched = &objects.threads[
        primary_threads[OPERATION_PROCESS_DESTINATION].slot
    ];
    matched = &objects.threads[destination_extra_thread.slot];
    sender = &objects.threads[
        primary_threads[OPERATION_PROCESS_SOURCE_A].slot
    ];
    EXPECT_TRUE(
        thread_handles_equal(
            destination->receiver_head,
            primary_threads[OPERATION_PROCESS_DESTINATION]
        )
        && thread_handles_equal(
            destination->receiver_tail,
            primary_threads[OPERATION_PROCESS_DESTINATION]
        )
        && unmatched->runtime_flags
            == MICROS_THREAD_RTS_IPC_RECEIVE
        && unmatched->ipc_receive_source
            == endpoints[OPERATION_PROCESS_SOURCE_B]
        && matched->runtime_flags == 0
        && matched->ready_linked
        && matched->ipc_delivery_pending
        && memcmp(
            &matched->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && sender->runtime_flags == 0
        && sender->ready_linked
        && destination->sender_head.slot == 0
        && destination->sender_head.generation == 0
        && destination->sender_tail.slot == 0
        && destination->sender_tail.generation == 0
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_send_rejections_are_atomic(void)
{
    struct micros_ipc_message message =
        message_pattern(0, UINT32_C(0x3234), 0, UINT8_C(0x40));
    struct micros_ipc_message kernel_message = message;
    micros_endpoint_t stale_destination;

    EXPECT_TRUE(setup_operation_fixture());
    stale_destination =
        endpoints[OPERATION_PROCESS_DESTINATION]
        + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS);
    EXPECT_TRUE(expect_send_failure_unchanged(
        MICROS_IPC_ERROR_UNAUTHORIZED,
        primary_threads[OPERATION_PROCESS_NO_SEND],
        endpoints[OPERATION_PROCESS_DESTINATION],
        &message
    ));
    EXPECT_TRUE(expect_send_failure_unchanged(
        MICROS_IPC_ERROR_UNAUTHORIZED,
        primary_threads[OPERATION_PROCESS_ISOLATED_SEND],
        endpoints[OPERATION_PROCESS_DESTINATION],
        &message
    ));
    EXPECT_TRUE(expect_send_failure_unchanged(
        MICROS_IPC_ERROR_STATE,
        primary_threads[OPERATION_PROCESS_RESERVED],
        endpoints[OPERATION_PROCESS_DESTINATION],
        &message
    ));
    EXPECT_TRUE(expect_send_failure_unchanged(
        MICROS_IPC_ERROR_STATE,
        primary_threads[OPERATION_PROCESS_SOURCE_A],
        endpoints[OPERATION_PROCESS_RESERVED],
        &message
    ));
    EXPECT_TRUE(expect_send_failure_unchanged(
        MICROS_IPC_ERROR_DEAD_ENDPOINT,
        primary_threads[OPERATION_PROCESS_SOURCE_A],
        stale_destination,
        &message
    ));
    kernel_message.type |= MICROS_IPC_TYPE_KERNEL_MASK;
    EXPECT_TRUE(expect_send_failure_unchanged(
        MICROS_IPC_ERROR_ARGUMENT,
        primary_threads[OPERATION_PROCESS_SOURCE_A],
        endpoints[OPERATION_PROCESS_DESTINATION],
        &kernel_message
    ));
    EXPECT_TRUE(setup_operation_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            primary_threads[OPERATION_PROCESS_DESTINATION],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x40002000)
        )
    );
    EXPECT_TRUE(expect_send_failure_unchanged(
        MICROS_IPC_ERROR_UNAUTHORIZED,
        primary_threads[OPERATION_PROCESS_NO_SEND],
        endpoints[OPERATION_PROCESS_DESTINATION],
        &message
    ));

    EXPECT_TRUE(
        setup_operation_fixture()
        && make_current_held(
            primary_threads[OPERATION_PROCESS_SOURCE_A]
        )
    );
    EXPECT_TRUE(expect_send_failure_unchanged(
        MICROS_IPC_ERROR_STATE,
        primary_threads[OPERATION_PROCESS_SOURCE_A],
        endpoints[OPERATION_PROCESS_DESTINATION],
        &message
    ));
    EXPECT_TRUE(setup_operation_fixture());
    objects.threads[
        primary_threads[OPERATION_PROCESS_SOURCE_A].slot
    ].owner.slot = MICROS_PROCESS_CAPACITY;
    EXPECT_TRUE(expect_send_failure_unchanged(
        MICROS_IPC_ERROR_INVARIANT,
        primary_threads[OPERATION_PROCESS_SOURCE_A],
        endpoints[OPERATION_PROCESS_DESTINATION],
        &message
    ));
    return true;
}

static bool test_authorized_receive_blocks(void)
{
    const struct micros_endpoint_record *destination;
    const struct micros_thread *receiver;

    EXPECT_TRUE(setup_operation_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[OPERATION_PROCESS_DESTINATION],
            endpoints[OPERATION_PROCESS_SOURCE_A],
            UINT64_C(0x50000000)
        )
    );
    destination = &registry.endpoints[
        processes[OPERATION_PROCESS_DESTINATION].slot
    ];
    receiver = &objects.threads[
        primary_threads[OPERATION_PROCESS_DESTINATION].slot
    ];
    EXPECT_TRUE(
        receiver->runtime_flags == MICROS_THREAD_RTS_IPC_RECEIVE
        && receiver->ipc_queue_kind == MICROS_IPC_QUEUE_RECEIVER
        && receiver->ipc_receive_source
            == endpoints[OPERATION_PROCESS_SOURCE_A]
        && receiver->ipc_receive_buffer == UINT64_C(0x50000000)
        && thread_handles_equal(
            destination->receiver_head,
            primary_threads[OPERATION_PROCESS_DESTINATION]
        )
        && thread_handles_equal(
            destination->receiver_tail,
            primary_threads[OPERATION_PROCESS_DESTINATION]
        )
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_receive_specific_and_any_fifo(void)
{
    struct micros_ipc_message first =
        message_pattern(0, UINT32_C(0x4234), 0, UINT8_C(0x50));
    struct micros_ipc_message second =
        message_pattern(0, UINT32_C(0x5234), 0, UINT8_C(0x60));
    struct micros_ipc_message expected;
    const struct micros_endpoint_record *destination;
    const struct micros_thread *receiver;

    EXPECT_TRUE(setup_operation_fixture());
    EXPECT_TRUE(prepare_destination_extra_thread());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            primary_threads[OPERATION_PROCESS_SOURCE_B],
            endpoints[OPERATION_PROCESS_DESTINATION],
            &first,
            0,
            0
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            primary_threads[OPERATION_PROCESS_SOURCE_A],
            endpoints[OPERATION_PROCESS_DESTINATION],
            &second,
            0,
            0
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            primary_threads[OPERATION_PROCESS_DESTINATION],
            endpoints[OPERATION_PROCESS_SOURCE_A],
            UINT64_C(0x50001000)
        )
    );
    destination = &registry.endpoints[
        processes[OPERATION_PROCESS_DESTINATION].slot
    ];
    receiver = &objects.threads[
        primary_threads[OPERATION_PROCESS_DESTINATION].slot
    ];
    expected = canonical_message(
        &second,
        endpoints[OPERATION_PROCESS_SOURCE_A]
    );
    EXPECT_TRUE(
        thread_handles_equal(
            destination->sender_head,
            primary_threads[OPERATION_PROCESS_SOURCE_B]
        )
        && thread_handles_equal(
            destination->sender_tail,
            primary_threads[OPERATION_PROCESS_SOURCE_B]
        )
        && receiver->ipc_delivery_pending
        && memcmp(
            &receiver->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );

    EXPECT_TRUE(setup_operation_fixture());
    EXPECT_TRUE(prepare_destination_extra_thread());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            primary_threads[OPERATION_PROCESS_SOURCE_B],
            endpoints[OPERATION_PROCESS_DESTINATION],
            &first,
            0,
            0
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            primary_threads[OPERATION_PROCESS_SOURCE_A],
            endpoints[OPERATION_PROCESS_DESTINATION],
            &second,
            0,
            0
        )
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receive(
            &registry,
            &objects,
            destination_extra_thread,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x50002000)
        )
    );
    destination = &registry.endpoints[
        processes[OPERATION_PROCESS_DESTINATION].slot
    ];
    receiver = &objects.threads[destination_extra_thread.slot];
    expected = canonical_message(
        &first,
        endpoints[OPERATION_PROCESS_SOURCE_B]
    );
    EXPECT_TRUE(
        thread_handles_equal(
            destination->sender_head,
            primary_threads[OPERATION_PROCESS_SOURCE_A]
        )
        && thread_handles_equal(
            destination->sender_tail,
            primary_threads[OPERATION_PROCESS_SOURCE_A]
        )
        && receiver->ipc_delivery_pending
        && memcmp(
            &receiver->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_receive_rejections_are_atomic(void)
{
    micros_endpoint_t stale_source;
    struct micros_thread_handle corrupt = {
        MICROS_THREAD_CAPACITY - 1,
        UINT32_C(0x55aa55aa),
    };

    EXPECT_TRUE(setup_operation_fixture());
    stale_source =
        endpoints[OPERATION_PROCESS_SOURCE_A]
        + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS);
    EXPECT_TRUE(expect_receive_failure_unchanged(
        MICROS_IPC_ERROR_UNAUTHORIZED,
        primary_threads[OPERATION_PROCESS_ISOLATED_SEND],
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x60000000)
    ));
    EXPECT_TRUE(expect_receive_failure_unchanged(
        MICROS_IPC_ERROR_STATE,
        primary_threads[OPERATION_PROCESS_RESERVED],
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x60001000)
    ));
    EXPECT_TRUE(expect_receive_failure_unchanged(
        MICROS_IPC_ERROR_STATE,
        primary_threads[OPERATION_PROCESS_DESTINATION],
        endpoints[OPERATION_PROCESS_RESERVED],
        UINT64_C(0x60002000)
    ));
    EXPECT_TRUE(expect_receive_failure_unchanged(
        MICROS_IPC_ERROR_DEAD_ENDPOINT,
        primary_threads[OPERATION_PROCESS_DESTINATION],
        stale_source,
        UINT64_C(0x60003000)
    ));
    EXPECT_TRUE(expect_receive_failure_unchanged(
        MICROS_IPC_ERROR_ARGUMENT,
        primary_threads[OPERATION_PROCESS_DESTINATION],
        MICROS_ENDPOINT_NONE,
        UINT64_C(0x60004000)
    ));
    EXPECT_TRUE(expect_receive_failure_unchanged(
        MICROS_IPC_ERROR_ARGUMENT,
        primary_threads[OPERATION_PROCESS_DESTINATION],
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x60005001)
    ));

    EXPECT_TRUE(setup_operation_fixture());
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_sender_enqueue(
            &registry,
            &objects,
            primary_threads[OPERATION_PROCESS_SOURCE_A],
            endpoints[OPERATION_PROCESS_ISOLATED_SEND],
            &(struct micros_ipc_message){
                .type = UINT32_C(0x6234),
                .payload = {UINT8_C(0x70)},
            },
            0,
            0
        )
    );
    EXPECT_TRUE(expect_receive_failure_unchanged(
        MICROS_IPC_ERROR_UNAUTHORIZED,
        primary_threads[OPERATION_PROCESS_ISOLATED_SEND],
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x60005500)
    ));

    EXPECT_TRUE(
        setup_operation_fixture()
        && make_current_held(
            primary_threads[OPERATION_PROCESS_DESTINATION]
        )
    );
    EXPECT_TRUE(expect_receive_failure_unchanged(
        MICROS_IPC_ERROR_STATE,
        primary_threads[OPERATION_PROCESS_DESTINATION],
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x60006000)
    ));

    EXPECT_TRUE(setup_operation_fixture());
    registry.endpoints[
        processes[OPERATION_PROCESS_DESTINATION].slot
    ].sender_head = corrupt;
    registry.endpoints[
        processes[OPERATION_PROCESS_DESTINATION].slot
    ].sender_tail = corrupt;
    EXPECT_TRUE(expect_receive_failure_unchanged(
        MICROS_IPC_ERROR_INVARIANT,
        primary_threads[OPERATION_PROCESS_DESTINATION],
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x60007000)
    ));
    EXPECT_TRUE(setup_operation_fixture());
    objects.threads[
        primary_threads[OPERATION_PROCESS_DESTINATION].slot
    ].owner.slot = MICROS_PROCESS_CAPACITY;
    EXPECT_TRUE(expect_receive_failure_unchanged(
        MICROS_IPC_ERROR_INVARIANT,
        primary_threads[OPERATION_PROCESS_DESTINATION],
        MICROS_ENDPOINT_ANY,
        UINT64_C(0x60008000)
    ));
    return true;
}

bool micros_ipc_operation_test_run(void)
{
    return (
        test_authorized_send_blocks_with_snapshot()
        && test_authorized_send_matches_first_receiver()
        && test_send_rejections_are_atomic()
        && test_authorized_receive_blocks()
        && test_receive_specific_and_any_fifo()
        && test_receive_rejections_are_atomic()
    );
}
