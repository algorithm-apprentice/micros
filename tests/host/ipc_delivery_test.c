#include "micros/ipc_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/scheduler_core.h"

enum {
    DELIVERY_SOURCE_COUNT = 3,
    DELIVERY_DESTINATION_THREAD_COUNT = 4,
    DELIVERY_PROCESS_COUNT = 5,
    DELIVERY_PROCESS_DESTINATION = 3,
    DELIVERY_PROCESS_ANCHOR = 4,
};

static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_process_handle processes[DELIVERY_PROCESS_COUNT];
static micros_endpoint_t endpoints[DELIVERY_PROCESS_COUNT];
static struct micros_thread_handle source_threads[DELIVERY_SOURCE_COUNT];
static struct micros_thread_handle
    destination_threads[DELIVERY_DESTINATION_THREAD_COUNT];
static struct micros_thread_handle anchor_thread;
static struct micros_hart_handle hart;
static struct micros_ipc_message
    source_messages[DELIVERY_SOURCE_COUNT];

bool micros_ipc_delivery_test_run(void);

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

static bool thread_handle_is_zero(struct micros_thread_handle handle)
{
    return handle.slot == 0 && handle.generation == 0;
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
    uint64_t token,
    uint8_t payload_seed
)
{
    struct micros_ipc_message message;
    size_t index;

    memset(&message, 0, sizeof(message));
    message.source = source;
    message.type = type;
    message.reply_token = token;
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

static bool setup_delivery_fixture(void)
{
    static const struct micros_privilege_profile profile = {
        .id = 1,
        .name = "DELIVERY",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_SEND,
        .call_targets = UINT32_C(1) << 1,
        .send_targets = UINT32_C(1) << 1,
    };
    size_t index;

    memset(&registry, 0, sizeof(registry));
    memset(&objects, 0, sizeof(objects));
    memset(source_messages, 0, sizeof(source_messages));
    if (
        micros_endpoint_registry_initialize(&registry, &profile, 1)
            != MICROS_ENDPOINT_OK
        || micros_kernel_objects_initialize(&objects, 4, 1)
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
    for (index = 0; index < DELIVERY_PROCESS_COUNT; ++index) {
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
                1
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                &registry,
                &objects,
                endpoints[index]
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
    }
    for (index = 0; index < DELIVERY_SOURCE_COUNT; ++index) {
        if (
            !prepare_thread(
                processes[index],
                UINT64_C(0x1000) + index * UINT64_C(0x100),
                UINT64_C(0x10000000)
                    + index * UINT64_C(0x00008000),
                &source_threads[index]
            )
        ) {
            return false;
        }
    }
    for (
        index = 0;
        index < DELIVERY_DESTINATION_THREAD_COUNT;
        ++index
    ) {
        if (
            !prepare_thread(
                processes[DELIVERY_PROCESS_DESTINATION],
                UINT64_C(0x2000) + index * UINT64_C(0x100),
                UINT64_C(0x11000000)
                    + index * UINT64_C(0x00008000),
                &destination_threads[index]
            )
        ) {
            return false;
        }
    }
    return prepare_thread(
        processes[DELIVERY_PROCESS_ANCHOR],
        UINT64_C(0x3000),
        UINT64_C(0x12000000),
        &anchor_thread
    );
}

static bool make_anchor_ready(void)
{
    return micros_thread_runtime_flags_unset(
        &objects,
        anchor_thread,
        MICROS_THREAD_RTS_INACTIVE
    ) == MICROS_KERNEL_OBJECT_OK;
}

static bool ready_queue_equals(
    const struct micros_thread_handle *expected,
    size_t count
)
{
    const struct micros_hart *resolved_hart =
        &objects.harts[hart.slot];
    struct micros_thread_handle current =
        resolved_hart->ready_head[
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER
        ];
    size_t index;

    if (count == 0) {
        return (
            thread_handle_is_zero(current)
            && thread_handle_is_zero(
                resolved_hart->ready_tail[
                    MICROS_SCHEDULER_PRIORITY_DEFAULT_USER
                ]
            )
        );
    }
    for (index = 0; index < count; ++index) {
        if (!thread_handles_equal(current, expected[index])) {
            return false;
        }
        current = objects.threads[current.slot].ready_next;
    }
    return (
        thread_handle_is_zero(current)
        && thread_handles_equal(
            resolved_hart->ready_tail[
                MICROS_SCHEDULER_PRIORITY_DEFAULT_USER
            ],
            expected[count - 1]
        )
    );
}

static bool sender_queue_equals(
    const size_t *source_indices,
    size_t count
)
{
    const struct micros_endpoint_record *endpoint =
        &registry.endpoints[DELIVERY_PROCESS_DESTINATION];
    struct micros_thread_handle current = endpoint->sender_head;
    size_t index;

    if (count == 0) {
        return (
            thread_handle_is_zero(current)
            && thread_handle_is_zero(endpoint->sender_tail)
        );
    }
    for (index = 0; index < count; ++index) {
        struct micros_thread_handle expected =
            source_threads[source_indices[index]];

        if (!thread_handles_equal(current, expected)) {
            return false;
        }
        current = objects.threads[current.slot].ipc_next;
    }
    return (
        thread_handle_is_zero(current)
        && thread_handles_equal(
            endpoint->sender_tail,
            source_threads[source_indices[count - 1]]
        )
    );
}

static bool receiver_queue_equals(
    const size_t *receiver_indices,
    size_t count
)
{
    const struct micros_endpoint_record *endpoint =
        &registry.endpoints[DELIVERY_PROCESS_DESTINATION];
    struct micros_thread_handle current = endpoint->receiver_head;
    size_t index;

    if (count == 0) {
        return (
            thread_handle_is_zero(current)
            && thread_handle_is_zero(endpoint->receiver_tail)
        );
    }
    for (index = 0; index < count; ++index) {
        struct micros_thread_handle expected =
            destination_threads[receiver_indices[index]];

        if (!thread_handles_equal(current, expected)) {
            return false;
        }
        current = objects.threads[current.slot].ipc_next;
    }
    return (
        thread_handle_is_zero(current)
        && thread_handles_equal(
            endpoint->receiver_tail,
            destination_threads[receiver_indices[count - 1]]
        )
    );
}

static bool enqueue_sender(size_t index, bool call)
{
    struct micros_ipc_message message = message_pattern(
        MICROS_ENDPOINT_ANY,
        (uint32_t)(UINT32_C(100) + index),
        UINT64_C(0xfeed0000) + index,
        (uint8_t)(10 + index)
    );

    if (call) {
        source_messages[index] = message;
        return micros_ipc_call(
            &registry,
            &objects,
            source_threads[index],
            endpoints[DELIVERY_PROCESS_DESTINATION],
            &source_messages[index]
        ) == MICROS_IPC_OK;
    }
    return micros_ipc_sender_enqueue(
        &registry,
        &objects,
        source_threads[index],
        endpoints[DELIVERY_PROCESS_DESTINATION],
        &message,
        0,
        0
    ) == MICROS_IPC_OK;
}

static bool enqueue_receiver(
    size_t destination_thread_index,
    micros_endpoint_t source
)
{
    return micros_ipc_receiver_enqueue(
        &registry,
        &objects,
        destination_threads[destination_thread_index],
        source,
        UINT64_C(0x50000000)
            + destination_thread_index * UINT64_C(0x1000)
    ) == MICROS_IPC_OK;
}

static bool test_receiver_dequeues_position(size_t match_index)
{
    struct micros_ipc_message expected_message;
    struct micros_thread_handle matched = {
        UINT16_C(99),
        UINT32_C(0xdeadbeef),
    };
    struct micros_thread_handle expected_ready[3];
    size_t expected_queue[2];
    const struct micros_thread *receiver;
    const struct micros_thread *sender;
    size_t index;
    size_t queue_index = 0;

    EXPECT_TRUE(setup_delivery_fixture());
    EXPECT_TRUE(make_anchor_ready());
    for (index = 0; index < DELIVERY_SOURCE_COUNT; ++index) {
        EXPECT_TRUE(enqueue_sender(index, false));
    }
    expected_message =
        objects.threads[source_threads[match_index].slot]
            .ipc_outbound_message;
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_commit_delivery(
            &registry,
            &objects,
            destination_threads[0],
            match_index == 0
                ? MICROS_ENDPOINT_ANY
                : endpoints[match_index],
            UINT64_C(0x60000000) + match_index * UINT64_C(0x1000),
            &matched
        )
    );
    EXPECT_TRUE(
        thread_handles_equal(matched, source_threads[match_index])
    );
    for (index = 0; index < DELIVERY_SOURCE_COUNT; ++index) {
        if (index != match_index) {
            expected_queue[queue_index++] = index;
        }
    }
    EXPECT_TRUE(sender_queue_equals(expected_queue, 2));
    receiver = &objects.threads[destination_threads[0].slot];
    sender = &objects.threads[source_threads[match_index].slot];
    EXPECT_TRUE(
        receiver->runtime_flags == 0
        && receiver->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && thread_handle_is_zero(receiver->ipc_next)
        && receiver->ipc_receive_source == 0
        && receiver->ipc_receive_buffer
            == UINT64_C(0x60000000)
                + match_index * UINT64_C(0x1000)
        && receiver->ipc_delivery_pending
        && receiver->ipc_staged_result == MICROS_IPC_OK
        && memcmp(
            &receiver->ipc_inbound_message,
            &expected_message,
            sizeof(expected_message)
        ) == 0
    );
    EXPECT_TRUE(
        sender->runtime_flags == 0
        && sender->ready_linked
        && micros_thread_ipc_state_is_clear(sender)
    );
    expected_ready[0] = anchor_thread;
    expected_ready[1] = destination_threads[0];
    expected_ready[2] = source_threads[match_index];
    EXPECT_TRUE(ready_queue_equals(expected_ready, 3));
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(&registry, &objects)
            == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_receiver_keeps_call_in_reply_wait(void)
{
    struct micros_ipc_message expected_message;
    struct micros_thread_handle matched = {0, 0};
    struct micros_thread_handle expected_ready[2];
    const struct micros_thread *caller;
    const struct micros_thread *receiver;
    const size_t expected_queue[] = {0, 2};

    EXPECT_TRUE(setup_delivery_fixture());
    EXPECT_TRUE(make_anchor_ready());
    EXPECT_TRUE(enqueue_sender(0, false));
    EXPECT_TRUE(enqueue_sender(1, true));
    EXPECT_TRUE(enqueue_sender(2, false));
    expected_message =
        objects.threads[source_threads[1].slot].ipc_outbound_message;
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_commit_delivery(
            &registry,
            &objects,
            destination_threads[0],
            endpoints[1],
            UINT64_C(0x60010000),
            &matched
        )
    );
    EXPECT_TRUE(thread_handles_equal(matched, source_threads[1]));
    EXPECT_TRUE(sender_queue_equals(expected_queue, 2));
    caller = &objects.threads[source_threads[1].slot];
    receiver = &objects.threads[destination_threads[0].slot];
    EXPECT_TRUE(
        caller->runtime_flags == MICROS_THREAD_RTS_IPC_REPLY
        && !caller->ready_linked
        && caller->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && thread_handle_is_zero(caller->ipc_next)
        && caller->ipc_send_destination == 0
        && caller->ipc_receive_buffer
            == (uintptr_t)&source_messages[1]
        && caller->ipc_reply_token == 1
        && caller->ipc_reply_callee
            == endpoints[DELIVERY_PROCESS_DESTINATION]
    );
    EXPECT_TRUE(
        memcmp(
            &receiver->ipc_inbound_message,
            &expected_message,
            sizeof(expected_message)
        ) == 0
        && receiver->ipc_inbound_message.reply_token
            == 1
    );
    expected_ready[0] = anchor_thread;
    expected_ready[1] = destination_threads[0];
    EXPECT_TRUE(ready_queue_equals(expected_ready, 2));
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(&registry, &objects)
            == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_sender_dequeues_position(size_t match_index)
{
    struct micros_ipc_message message = message_pattern(
        MICROS_ENDPOINT_ANY,
        UINT32_C(200) + (uint32_t)match_index,
        UINT64_C(0xabcdef0123456789),
        (uint8_t)(30 + match_index)
    );
    struct micros_ipc_message expected_message = message;
    struct micros_thread_handle matched = {
        UINT16_C(98),
        UINT32_C(0xcafebabe),
    };
    struct micros_thread_handle expected_ready[3];
    micros_endpoint_t receive_sources[3];
    size_t expected_queue[2];
    const struct micros_thread *sender;
    const struct micros_thread *receiver;
    size_t index;
    size_t queue_index = 0;

    EXPECT_TRUE(setup_delivery_fixture());
    EXPECT_TRUE(make_anchor_ready());
    receive_sources[0] = match_index == 0
        ? MICROS_ENDPOINT_ANY
        : endpoints[1];
    receive_sources[1] = match_index == 1
        ? MICROS_ENDPOINT_ANY
        : endpoints[2];
    receive_sources[2] = endpoints[0];
    for (index = 0; index < 3; ++index) {
        EXPECT_TRUE(enqueue_receiver(index + 1, receive_sources[index]));
    }
    expected_message.source = endpoints[0];
    expected_message.reply_token = 0;
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_sender_commit_delivery(
            &registry,
            &objects,
            source_threads[0],
            endpoints[DELIVERY_PROCESS_DESTINATION],
            &message,
            &matched
        )
    );
    EXPECT_TRUE(
        thread_handles_equal(
            matched,
            destination_threads[match_index + 1]
        )
    );
    for (index = 0; index < 3; ++index) {
        if (index != match_index) {
            expected_queue[queue_index++] = index + 1;
        }
    }
    EXPECT_TRUE(receiver_queue_equals(expected_queue, 2));
    sender = &objects.threads[source_threads[0].slot];
    receiver = &objects.threads[
        destination_threads[match_index + 1].slot
    ];
    EXPECT_TRUE(
        sender->runtime_flags == 0
        && sender->ready_linked
        && micros_thread_ipc_state_is_clear(sender)
    );
    EXPECT_TRUE(
        receiver->runtime_flags == 0
        && receiver->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && thread_handle_is_zero(receiver->ipc_next)
        && receiver->ipc_receive_source == 0
        && receiver->ipc_receive_buffer
            == UINT64_C(0x50000000)
                + (match_index + 1) * UINT64_C(0x1000)
        && receiver->ipc_delivery_pending
        && receiver->ipc_staged_result == MICROS_IPC_OK
        && memcmp(
            &receiver->ipc_inbound_message,
            &expected_message,
            sizeof(expected_message)
        ) == 0
    );
    expected_ready[0] = anchor_thread;
    expected_ready[1] = source_threads[0];
    expected_ready[2] = destination_threads[match_index + 1];
    EXPECT_TRUE(ready_queue_equals(expected_ready, 3));
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(&registry, &objects)
            == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool state_and_output_unchanged(
    const struct micros_endpoint_registry *registry_snapshot,
    const struct micros_kernel_objects *objects_snapshot,
    struct micros_thread_handle output,
    struct micros_thread_handle expected_output
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
        && thread_handles_equal(output, expected_output)
    );
}

static bool test_no_match_preserves_state_and_output(void)
{
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_thread_handle output = {
        UINT16_C(97),
        UINT32_C(0x11223344),
    };
    const struct micros_thread_handle output_snapshot = output;
    struct micros_ipc_message message =
        message_pattern(1, 300, 2, 40);

    EXPECT_TRUE(setup_delivery_fixture());
    EXPECT_TRUE(enqueue_sender(0, false));
    EXPECT_TRUE(enqueue_sender(1, false));
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_NOT_READY,
        micros_ipc_receiver_commit_delivery(
            &registry,
            &objects,
            destination_threads[0],
            endpoints[2],
            UINT64_C(0x60020000),
            &output
        )
    );
    EXPECT_TRUE(
        state_and_output_unchanged(
            &registry_snapshot,
            &objects_snapshot,
            output,
            output_snapshot
        )
    );

    EXPECT_TRUE(setup_delivery_fixture());
    EXPECT_TRUE(enqueue_receiver(1, endpoints[1]));
    EXPECT_TRUE(enqueue_receiver(2, endpoints[2]));
    output = output_snapshot;
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_NOT_READY,
        micros_ipc_sender_commit_delivery(
            &registry,
            &objects,
            source_threads[0],
            endpoints[DELIVERY_PROCESS_DESTINATION],
            &message,
            &output
        )
    );
    EXPECT_TRUE(
        state_and_output_unchanged(
            &registry_snapshot,
            &objects_snapshot,
            output,
            output_snapshot
        )
    );
    return true;
}

static bool test_stale_and_corrupt_queues_preserve_state(void)
{
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_thread_handle output = {
        UINT16_C(96),
        UINT32_C(0x55667788),
    };
    const struct micros_thread_handle output_snapshot = output;
    struct micros_ipc_message message =
        message_pattern(1, 400, 2, 50);

    EXPECT_TRUE(setup_delivery_fixture());
    EXPECT_TRUE(enqueue_sender(0, false));
    ++registry.endpoints[DELIVERY_PROCESS_DESTINATION]
        .sender_head.generation;
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_INVARIANT,
        micros_ipc_receiver_commit_delivery(
            &registry,
            &objects,
            destination_threads[0],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60030000),
            &output
        )
    );
    EXPECT_TRUE(
        state_and_output_unchanged(
            &registry_snapshot,
            &objects_snapshot,
            output,
            output_snapshot
        )
    );

    EXPECT_TRUE(setup_delivery_fixture());
    EXPECT_TRUE(enqueue_receiver(1, MICROS_ENDPOINT_ANY));
    registry.endpoints[DELIVERY_PROCESS_DESTINATION].receiver_tail =
        destination_threads[2];
    output = output_snapshot;
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_INVARIANT,
        micros_ipc_sender_commit_delivery(
            &registry,
            &objects,
            source_threads[0],
            endpoints[DELIVERY_PROCESS_DESTINATION],
            &message,
            &output
        )
    );
    EXPECT_TRUE(
        state_and_output_unchanged(
            &registry_snapshot,
            &objects_snapshot,
            output,
            output_snapshot
        )
    );
    return true;
}

static bool test_independently_blocked_receiver_is_preserved(void)
{
    struct micros_thread_handle output = {0, 0};
    struct micros_ipc_message message =
        message_pattern(1, 500, 2, 60);
    struct micros_ipc_message expected = message;
    const struct micros_thread *sender;
    const struct micros_thread *receiver;

    EXPECT_TRUE(setup_delivery_fixture());
    EXPECT_TRUE(enqueue_receiver(1, MICROS_ENDPOINT_ANY));
    objects.threads[destination_threads[1].slot].runtime_flags |=
        MICROS_THREAD_RTS_INACTIVE;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(&registry, &objects)
            == MICROS_ENDPOINT_OK
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_sender_commit_delivery(
            &registry,
            &objects,
            source_threads[0],
            endpoints[DELIVERY_PROCESS_DESTINATION],
            &message,
            &output
        )
    );
    expected.source = endpoints[0];
    expected.reply_token = 0;
    sender = &objects.threads[source_threads[0].slot];
    receiver = &objects.threads[destination_threads[1].slot];
    EXPECT_TRUE(
        thread_handles_equal(output, destination_threads[1])
        && sender->runtime_flags == 0
        && sender->ready_linked
        && receiver->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && !receiver->ready_linked
        && receiver->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && thread_handle_is_zero(receiver->ipc_next)
        && receiver->ipc_receive_source == 0
        && receiver->ipc_delivery_pending
        && memcmp(
            &receiver->ipc_inbound_message,
            &expected,
            sizeof(expected)
        ) == 0
        && thread_handle_is_zero(
            registry.endpoints[
                DELIVERY_PROCESS_DESTINATION
            ].receiver_head
        )
        && thread_handle_is_zero(
            registry.endpoints[
                DELIVERY_PROCESS_DESTINATION
            ].receiver_tail
        )
        && micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_delivery_preempts_through_scheduler(void)
{
    struct micros_scheduler_return_plan plan;
    struct micros_thread_handle matched = {0, 0};
    struct micros_thread_handle expected_ready[2];
    const struct micros_hart *resolved_hart;
    const struct micros_thread *current;

    EXPECT_TRUE(setup_delivery_fixture());
    EXPECT_TRUE(enqueue_sender(0, false));
    EXPECT_TRUE(
        micros_thread_install_policy(
            &objects,
            anchor_thread,
            MICROS_SCHEDULER_PRIORITY_LOWEST,
            100
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_install_policy(
            &objects,
            source_threads[2],
            MICROS_SCHEDULER_PRIORITY_LOWEST,
            100
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_scheduler_accounting_initialize(
            &objects,
            hart,
            100
        ) == MICROS_KERNEL_OBJECT_OK
        && make_anchor_ready()
        && micros_thread_runtime_flags_unset(
            &objects,
            source_threads[2],
            MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_hart_plan_user_return(&objects, hart, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_commit_user_return(&objects, &plan)
            == MICROS_KERNEL_OBJECT_OK
    );
    EXPECT_IPC_ERROR(
        MICROS_IPC_OK,
        micros_ipc_receiver_commit_delivery(
            &registry,
            &objects,
            destination_threads[0],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60050000),
            &matched
        )
    );
    EXPECT_TRUE(thread_handles_equal(matched, source_threads[0]));
    resolved_hart = &objects.harts[hart.slot];
    current = &objects.threads[anchor_thread.slot];
    EXPECT_TRUE(
        thread_handles_equal(
            resolved_hart->current_thread,
            anchor_thread
        )
        && current->runtime_flags == MICROS_THREAD_RTS_PREEMPTED
        && !current->ready_linked
        && thread_handles_equal(
            resolved_hart->ready_head[
                MICROS_SCHEDULER_PRIORITY_LOWEST
            ],
            source_threads[2]
        )
        && thread_handles_equal(
            resolved_hart->ready_tail[
                MICROS_SCHEDULER_PRIORITY_LOWEST
            ],
            source_threads[2]
        )
    );
    expected_ready[0] = destination_threads[0];
    expected_ready[1] = source_threads[0];
    EXPECT_TRUE(ready_queue_equals(expected_ready, 2));
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(&registry, &objects)
            == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool test_scheduler_rejection_preserves_delivery(void)
{
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_scheduler_return_plan plan;
    struct micros_thread_handle output = {
        UINT16_C(95),
        UINT32_C(0x99aabbcc),
    };
    const struct micros_thread_handle output_snapshot = output;

    EXPECT_TRUE(setup_delivery_fixture());
    EXPECT_TRUE(enqueue_sender(0, false));
    EXPECT_TRUE(
        micros_thread_install_policy(
            &objects,
            anchor_thread,
            MICROS_SCHEDULER_PRIORITY_LOWEST,
            100
        ) == MICROS_KERNEL_OBJECT_OK
    );
    EXPECT_TRUE(
        micros_scheduler_accounting_initialize(&objects, hart, 100)
            == MICROS_KERNEL_OBJECT_OK
    );
    EXPECT_TRUE(make_anchor_ready());
    EXPECT_TRUE(
        micros_hart_plan_user_return(&objects, hart, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_commit_user_return(&objects, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_scheduler_account_enter_thread(
            &objects,
            hart,
            anchor_thread,
            120
        ) == MICROS_KERNEL_OBJECT_OK
    );
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_IPC_ERROR(
        MICROS_IPC_ERROR_STATE,
        micros_ipc_receiver_commit_delivery(
            &registry,
            &objects,
            destination_threads[0],
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x60040000),
            &output
        )
    );
    EXPECT_TRUE(
        state_and_output_unchanged(
            &registry_snapshot,
            &objects_snapshot,
            output,
            output_snapshot
        )
    );
    return true;
}

bool micros_ipc_delivery_test_run(void)
{
    return (
        test_receiver_dequeues_position(0)
        && test_receiver_dequeues_position(1)
        && test_receiver_dequeues_position(2)
        && test_receiver_keeps_call_in_reply_wait()
        && test_sender_dequeues_position(0)
        && test_sender_dequeues_position(1)
        && test_sender_dequeues_position(2)
        && test_no_match_preserves_state_and_output()
        && test_stale_and_corrupt_queues_preserve_state()
        && test_independently_blocked_receiver_is_preserved()
        && test_delivery_preempts_through_scheduler()
        && test_scheduler_rejection_preserves_delivery()
    );
}
