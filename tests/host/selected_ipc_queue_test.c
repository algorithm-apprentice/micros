#include "tests/host/selected_ipc_validation_test.h"

#include <stdio.h>
#include <string.h>

enum sender_queue_operation {
    SENDER_APPEND,
    SENDER_BAD_MIDDLE,
    SENDER_UNCONSUMED_SUFFIX,
    SENDER_STALE_LINK,
    SENDER_RANGE_LINK,
    SENDER_NONHEAD_CYCLE,
    SENDER_NO_MATCH,
    SENDER_CLOSE,
};

struct sender_queue_case {
    const char *name;
    size_t length;
    bool permuted;
    enum sender_queue_operation operation;
};

static const struct sender_queue_case queue_cases[] = {
    {"sender-append-empty", 0, false, SENDER_APPEND},
    {"sender-append-single", 1, false, SENDER_APPEND},
    {"sender-append-short", 7, false, SENDER_APPEND},
    {"sender-append-long", 63, false, SENDER_APPEND},
    {"sender-append-full", 127, false, SENDER_APPEND},
    {"sender-append-permuted", 7, true, SENDER_APPEND},
    {"sender-middle-corrupt", 7, false, SENDER_BAD_MIDDLE},
    {"sender-suffix-unconsumed", 7, false, SENDER_UNCONSUMED_SUFFIX},
    {"sender-stale-link", 7, false, SENDER_STALE_LINK},
    {"sender-range-link", 7, false, SENDER_RANGE_LINK},
    {"sender-nonhead-cycle", 7, false, SENDER_NONHEAD_CYCLE},
    {"sender-no-match", 7, false, SENDER_NO_MATCH},
    {"sender-close-full", 128, false, SENDER_CLOSE},
};

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "IPC04_FIXTURE_FAILURE line=%d expression=%s\n", __LINE__, #condition); \
            return false; \
        } \
    } while (false)

static struct micros_thread_handle sender_at(const struct sender_queue_case *test, size_t index)
{
    static const uint16_t slots[] = {127, 1, 65, 2, 64, 126, 3, 66};
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();

    return fixture->threads[test->permuted ? slots[index] : index];
}

static struct micros_ipc_message sender_message(size_t position)
{
    struct micros_ipc_message message = {
        .source = MICROS_ENDPOINT_ANY,
        .type = UINT32_C(0x6400) + (uint32_t)position,
        .reply_token = UINT64_MAX,
    };

    message.payload[0] = (uint8_t)position;
    message.payload[47] = (uint8_t)(position ^ UINT8_C(0xa5));
    return message;
}

static bool sender_chain_matches(const struct sender_queue_case *test, size_t first, size_t count)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    struct micros_thread_handle current =
        fixture->registry.endpoints[MICROS_PROCESS_CAPACITY - 1].sender_head;

    for (size_t index = first; index < first + count; ++index) {
        struct micros_thread_handle expected = sender_at(test, index);
        const struct micros_thread *thread = &fixture->objects.threads[expected.slot];
        struct micros_ipc_message message = sender_message(index);

        message.source = fixture->endpoints[0];
        message.reply_token = 0;
        REQUIRE(current.slot == expected.slot && current.generation == expected.generation);
        REQUIRE(thread->runtime_flags == MICROS_THREAD_RTS_IPC_SEND
                && thread->ipc_send_destination == fixture->endpoints[1]
                && memcmp(&thread->ipc_outbound_message, &message, sizeof(message)) == 0);
        current = thread->ipc_next;
    }
    REQUIRE(current.slot == 0 && current.generation == 0);
    return true;
}

static bool prepare_sender_queue(const struct sender_queue_case *test)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    size_t count = test->permuted ? MICROS_THREAD_CAPACITY
        : test->length + (test->operation != SENDER_CLOSE);

    REQUIRE(selected_ipc_setup(MICROS_THREAD_CAPACITY));
    selected_ipc_test_name(test->name);
    REQUIRE(micros_hart_register(&fixture->objects, 0, &fixture->hart) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_hart_install_trap_stacks(
        &fixture->objects, fixture->hart,
        UINT64_C(0x20000000), UINT64_C(0x20000000) + MICROS_PRIMARY_TRAP_STACK_MIN_SIZE,
        UINT64_C(0x30000000), UINT64_C(0x30000000) + MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE
    ) == MICROS_KERNEL_OBJECT_OK);
    for (size_t index = 0; index < count; ++index) {
        REQUIRE(selected_ipc_prepare_thread(
            (test->operation == SENDER_UNCONSUMED_SUFFIX || test->operation == SENDER_NO_MATCH)
                && index == test->length
                ? MICROS_PROCESS_CAPACITY - 1 : 0
        ));
    }
    for (size_t index = 0; index < test->length; ++index) {
        struct micros_ipc_message message = sender_message(index);

        REQUIRE(micros_ipc_sender_enqueue(
            &fixture->registry, &fixture->objects, sender_at(test, index),
            fixture->endpoints[1], &message, 0, 0
        ) == MICROS_IPC_OK);
    }
    REQUIRE(sender_chain_matches(test, 0, test->length));
    REQUIRE(micros_endpoint_registry_validate_objects(&fixture->registry, &fixture->objects)
            == MICROS_ENDPOINT_OK);
    return true;
}

static bool corrupt_sender_queue(const struct sender_queue_case *test)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    struct micros_thread *middle = &fixture->objects.threads[sender_at(test, 3).slot];
    struct micros_thread *last = &fixture->objects.threads[sender_at(test, test->length - 1).slot];

    switch (test->operation) {
    case SENDER_BAD_MIDDLE:
        middle->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
        break;
    case SENDER_UNCONSUMED_SUFFIX:
        last->ipc_queue_kind = MICROS_IPC_QUEUE_RECEIVER;
        break;
    case SENDER_STALE_LINK:
        ++middle->ipc_next.generation;
        break;
    case SENDER_RANGE_LINK:
        middle->ipc_next.slot = MICROS_THREAD_CAPACITY;
        break;
    case SENDER_NONHEAD_CYCLE:
        last->ipc_next = sender_at(test, 3);
        break;
    default:
        return false;
    }
    return micros_endpoint_registry_validate_objects(&fixture->registry, &fixture->objects)
        == MICROS_ENDPOINT_ERROR_INVARIANT;
}

static bool closed_senders_are_released(const struct sender_queue_case *test)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();

    REQUIRE(fixture->registry.endpoints[MICROS_PROCESS_CAPACITY - 1].state == MICROS_ENDPOINT_STATE_FREE);
    for (size_t index = 0; index < test->length; ++index) {
        const struct micros_thread *thread = &fixture->objects.threads[sender_at(test, index).slot];

        REQUIRE(thread->runtime_flags == 0 && thread->ready_linked
                && thread->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
                && thread->ipc_next.slot == 0 && thread->ipc_next.generation == 0
                && thread->ipc_delivery_pending
                && thread->ipc_staged_result == MICROS_IPC_ERROR_DEAD_ENDPOINT);
    }
    return true;
}

static bool run_sender_queue_case(size_t index, const struct sender_queue_case *test)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    struct micros_ipc_message message = sender_message(test->length);
    struct micros_thread_handle matched = {UINT16_MAX, UINT32_MAX};
    bool malformed = test->operation >= SENDER_BAD_MIDDLE && test->operation <= SENDER_NONHEAD_CYCLE;
    bool preserved = (malformed && test->operation != SENDER_UNCONSUMED_SUFFIX)
        || test->operation == SENDER_NO_MATCH;
    enum micros_ipc_error expected = test->operation == SENDER_NO_MATCH ? MICROS_IPC_ERROR_NOT_READY
        : preserved ? MICROS_IPC_ERROR_INVARIANT : MICROS_IPC_OK;
    enum micros_ipc_error result;

    REQUIRE(prepare_sender_queue(test));
    if (malformed) {
        REQUIRE(corrupt_sender_queue(test));
    }
    fixture->before_objects = fixture->objects;
    fixture->before_registry = fixture->registry;
    REQUIRE(selected_ipc_begin_capture(index));
    if (test->operation == SENDER_UNCONSUMED_SUFFIX || test->operation == SENDER_NO_MATCH) {
        result = micros_ipc_receiver_commit_delivery(
            &fixture->registry, &fixture->objects, fixture->threads[test->length],
            test->operation == SENDER_NO_MATCH ? fixture->endpoints[1] : MICROS_ENDPOINT_ANY,
            UINT64_C(0x50000000), &matched
        );
    } else if (test->operation == SENDER_CLOSE) {
        result = micros_ipc_endpoint_close(&fixture->registry, &fixture->objects, fixture->endpoints[1]);
    } else {
        result = micros_ipc_sender_enqueue(
            &fixture->registry, &fixture->objects, sender_at(test, test->length),
            fixture->endpoints[1], &message, 0, 0
        );
    }
    REQUIRE(selected_ipc_finish_capture());
    if (result != expected) {
        fprintf(stderr, "IPC04_CASE_FAILURE case=%s expected=%u actual=%u\n",
                test->name, (unsigned)expected, (unsigned)result);
        return false;
    }
    if (test->operation == SENDER_APPEND) {
        REQUIRE(sender_chain_matches(test, 0, test->length + 1));
    } else if (test->operation == SENDER_NO_MATCH) {
        REQUIRE(matched.slot == UINT16_MAX && matched.generation == UINT32_MAX);
        REQUIRE(sender_chain_matches(test, 0, test->length));
    } else if (test->operation == SENDER_CLOSE) {
        REQUIRE(closed_senders_are_released(test));
    } else if (test->operation == SENDER_UNCONSUMED_SUFFIX) {
        const struct micros_thread *sender = &fixture->objects.threads[fixture->threads[0].slot];
        const struct micros_thread *receiver = &fixture->objects.threads[fixture->threads[test->length].slot];
        struct micros_ipc_message delivered = sender_message(0);

        delivered.source = fixture->endpoints[0];
        delivered.reply_token = 0;
        REQUIRE(matched.slot == fixture->threads[0].slot
                && matched.generation == fixture->threads[0].generation);
        REQUIRE(sender->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
                && sender->ipc_next.slot == 0 && sender->ipc_next.generation == 0
                && (sender->runtime_flags & MICROS_THREAD_RTS_IPC_SEND) == 0);
        REQUIRE(receiver->ipc_delivery_pending
                && memcmp(&receiver->ipc_inbound_message, &delivered, sizeof(delivered)) == 0);
        REQUIRE(sender_chain_matches(test, 1, test->length - 1));
        REQUIRE(fixture->objects.threads[sender_at(test, test->length - 1).slot].ipc_queue_kind
                == MICROS_IPC_QUEUE_RECEIVER);
    }
    REQUIRE(micros_endpoint_registry_validate_objects(&fixture->registry, &fixture->objects)
            == (malformed ? MICROS_ENDPOINT_ERROR_INVARIANT : MICROS_ENDPOINT_OK));
    return selected_ipc_report_case(index, test->name, (unsigned)result, test->length, preserved);
}

size_t selected_ipc_queue_case_count(void)
{
    return sizeof(queue_cases) / sizeof(queue_cases[0]);
}

bool selected_ipc_queue_cases(size_t first_case)
{
    for (size_t index = 0; index < selected_ipc_queue_case_count(); ++index) {
        if (!run_sender_queue_case(first_case + index, &queue_cases[index])) {
            return false;
        }
    }
    return true;
}
