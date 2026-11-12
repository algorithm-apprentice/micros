#include "tests/host/selected_ipc_validation_test.h"
#include "kernel/endpoint/endpoint_internal.h"

#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(struct micros_thread) <= 536 + 16, "reply binding storage is bounded");

enum token_operation {
    TOKEN_CALL,
    TOKEN_REPLY,
    TOKEN_REPLY_SIBLING,
    TOKEN_REPLY_EARLY,
    TOKEN_REPLY_RECEIVE,
    TOKEN_REPLY_MISSING,
    TOKEN_STAGED_REQUEST,
    TOKEN_REPLY_STALE_INDEX,
    TOKEN_REPLY_RETURNED_INDEX,
    TOKEN_REPLY_FOREIGN_INDEX,
};

struct token_case {
    const char *name;
    enum token_operation operation;
    uint16_t caller_slot;
};

static const struct token_case token_cases[] = {
    {"token-call-first", TOKEN_CALL, 0},
    {"token-call-middle", TOKEN_CALL, 64},
    {"token-call-high", TOKEN_CALL, 127},
    {"token-reply", TOKEN_REPLY, 0},
    {"token-reply-sibling", TOKEN_REPLY_SIBLING, 0},
    {"token-reply-early", TOKEN_REPLY_EARLY, 0},
    {"token-reply-receive", TOKEN_REPLY_RECEIVE, 0},
    {"token-reply-missing", TOKEN_REPLY_MISSING, 0},
    {"token-staged-request", TOKEN_STAGED_REQUEST, 0},
    {"token-reply-stale-index", TOKEN_REPLY_STALE_INDEX, 0},
    {"token-reply-returned-index", TOKEN_REPLY_RETURNED_INDEX, 0},
    {"token-reply-foreign-index", TOKEN_REPLY_FOREIGN_INDEX, 0},
};

struct token_context {
    struct micros_thread_handle caller;
    struct micros_thread_handle receiver;
    struct micros_thread_handle replier;
    uint64_t token;
};

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "IPC03_FIXTURE_FAILURE line=%d expression=%s\n", __LINE__, #condition); \
            return false; \
        } \
    } while (false)

static bool prepare_token_threads(const struct token_case *test, struct token_context *context)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();

    REQUIRE(selected_ipc_setup(MICROS_THREAD_CAPACITY));
    selected_ipc_test_name(test->name);
    REQUIRE(micros_hart_register(&fixture->objects, 0, &fixture->hart) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_hart_install_trap_stacks(
        &fixture->objects, fixture->hart,
        UINT64_C(0x20000000), UINT64_C(0x20000000) + MICROS_PRIMARY_TRAP_STACK_MIN_SIZE,
        UINT64_C(0x30000000), UINT64_C(0x30000000) + MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE
    ) == MICROS_KERNEL_OBJECT_OK);
    if (test->caller_slot == 0) {
        REQUIRE(selected_ipc_prepare_thread(0));
        context->caller = fixture->threads[0];
        REQUIRE(selected_ipc_prepare_thread(MICROS_PROCESS_CAPACITY - 1));
        context->receiver = fixture->threads[1];
    } else {
        REQUIRE(selected_ipc_prepare_thread(MICROS_PROCESS_CAPACITY - 1));
        context->receiver = fixture->threads[0];
        for (size_t slot = 1; slot < test->caller_slot; ++slot) {
            struct micros_thread_handle filler;

            REQUIRE(micros_thread_create(
                &fixture->objects, fixture->processes[MICROS_PROCESS_CAPACITY - 2], &filler
            ) == MICROS_KERNEL_OBJECT_OK);
            REQUIRE(filler.slot == slot);
        }
        REQUIRE(selected_ipc_prepare_thread(0));
        context->caller = fixture->threads[1];
    }
    REQUIRE(context->caller.slot == test->caller_slot);
    context->replier = context->receiver;
    if (test->operation == TOKEN_REPLY_SIBLING || test->operation == TOKEN_REPLY_EARLY
        || test->operation >= TOKEN_REPLY_STALE_INDEX) {
        REQUIRE(selected_ipc_prepare_thread(MICROS_PROCESS_CAPACITY - 1));
        context->replier = fixture->threads[2];
    }
    context->token = test->caller_slot == 0 ? 128 : test->caller_slot;
    fixture->registry.last_reply_token = context->token - 1;
    return true;
}

static bool prepare_token_request(const struct token_case *test, const struct token_context *context)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    struct micros_ipc_message request = {.type = UINT32_C(0x6301)};
    struct micros_thread *receiver = &fixture->objects.threads[context->receiver.slot];
    struct micros_thread *caller = &fixture->objects.threads[context->caller.slot];

    if (test->operation == TOKEN_CALL || test->operation == TOKEN_REPLY_MISSING) {
        return true;
    }
    REQUIRE(micros_ipc_receive(
        &fixture->registry, &fixture->objects, context->receiver,
        MICROS_ENDPOINT_ANY, UINT64_C(0x50000000)
    ) == MICROS_IPC_OK);
    REQUIRE(micros_ipc_call(
        &fixture->registry, &fixture->objects, context->caller,
        fixture->endpoints[1], &request, UINT64_C(0x60000000)
    ) == MICROS_IPC_OK);
    REQUIRE(receiver->ipc_delivery_pending && receiver->ipc_inbound_message.reply_token == context->token);
    REQUIRE(caller->ipc_reply_phase == MICROS_IPC_REPLY_PHASE_STAGED
            && caller->ipc_request_receiver.slot == context->receiver.slot
            && caller->ipc_request_receiver.generation == context->receiver.generation);
    if (test->operation == TOKEN_REPLY_STALE_INDEX) {
        ++caller->ipc_request_receiver.generation;
    } else if (test->operation == TOKEN_REPLY_RETURNED_INDEX) {
        caller->ipc_reply_phase = MICROS_IPC_REPLY_PHASE_RETURNED;
    } else if (test->operation == TOKEN_REPLY_FOREIGN_INDEX) {
        caller->ipc_request_receiver = context->caller;
    }
    if (test->operation == TOKEN_REPLY_EARLY || test->operation == TOKEN_STAGED_REQUEST
        || test->operation >= TOKEN_REPLY_STALE_INDEX) {
        return true;
    }
    receiver->ipc_receive_buffer = 0;
    receiver->ipc_delivery_pending = false;
    memset(&receiver->ipc_inbound_message, 0, sizeof(receiver->ipc_inbound_message));
    receiver->ipc_staged_result = MICROS_IPC_OK;
    caller->ipc_reply_phase = MICROS_IPC_REPLY_PHASE_RETURNED;
    caller->ipc_request_receiver = (struct micros_thread_handle){0, 0};
    if (test->operation != TOKEN_REPLY_SIBLING) {
        REQUIRE(micros_thread_scheduler_hold(&fixture->objects, context->replier)
                == MICROS_KERNEL_OBJECT_OK);
    }
    return true;
}

static bool run_token_case(size_t index, const struct token_case *test)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    struct token_context context;
    struct micros_ipc_message message = {.type = UINT32_C(0x6302)};
    bool malformed = test->operation >= TOKEN_REPLY_STALE_INDEX;
    enum micros_ipc_error expected = malformed ? MICROS_IPC_ERROR_INVARIANT
        : test->operation == TOKEN_REPLY_EARLY || test->operation == TOKEN_REPLY_MISSING
            ? MICROS_IPC_ERROR_REPLY_TOKEN : MICROS_IPC_OK;
    enum micros_endpoint_error diagnostic = malformed ? MICROS_ENDPOINT_ERROR_INVARIANT : MICROS_ENDPOINT_OK;
    bool preserved = expected != MICROS_IPC_OK || test->operation == TOKEN_STAGED_REQUEST;
    enum micros_ipc_error error;
    const struct micros_thread *caller;

    REQUIRE(prepare_token_threads(test, &context));
    REQUIRE(prepare_token_request(test, &context));
    REQUIRE(micros_endpoint_registry_validate_objects(&fixture->registry, &fixture->objects)
            == diagnostic);
    fixture->before_objects = fixture->objects;
    fixture->before_registry = fixture->registry;
    REQUIRE(selected_ipc_begin_capture(index));
    if (test->operation == TOKEN_CALL) {
        error = micros_ipc_call(
            &fixture->registry, &fixture->objects, context.caller, fixture->endpoints[1],
            &message, UINT64_C(0x60000000)
        );
    } else if (test->operation == TOKEN_REPLY_RECEIVE) {
        error = micros_ipc_reply_receive(
            &fixture->registry, &fixture->objects, context.replier, context.token,
            &message, MICROS_ENDPOINT_ANY, UINT64_C(0x50000000)
        );
    } else if (test->operation == TOKEN_STAGED_REQUEST) {
        error = micros_endpoint_thread_ipc_state_is_valid(
            &fixture->registry, &fixture->objects, context.receiver
        ) ? MICROS_IPC_OK : MICROS_IPC_ERROR_INVARIANT;
    } else {
        error = micros_ipc_reply(
            &fixture->registry, &fixture->objects, context.replier, context.token, &message
        );
    }
    REQUIRE(selected_ipc_finish_capture());
    REQUIRE(error == expected);
    caller = &fixture->objects.threads[context.caller.slot];
    if (test->operation == TOKEN_CALL) {
        REQUIRE(caller->ipc_reply_token == context.token
                && caller->ipc_outbound_message.reply_token == context.token
                && fixture->registry.last_reply_token == context.token
                && caller->ipc_reply_phase == MICROS_IPC_REPLY_PHASE_QUEUED
                && caller->ipc_request_receiver.slot == 0
                && caller->ipc_request_receiver.generation == 0);
    } else if (!preserved) {
        REQUIRE(caller->ipc_reply_token == 0 && caller->ipc_delivery_pending
                && caller->ipc_inbound_message.reply_token == 0
                && caller->ipc_inbound_message.type == message.type
                && caller->ipc_inbound_message.source == fixture->endpoints[1]
                && caller->ipc_reply_phase == MICROS_IPC_REPLY_PHASE_NONE
                && caller->ipc_request_receiver.slot == 0
                && caller->ipc_request_receiver.generation == 0);
    }
    REQUIRE(micros_endpoint_registry_validate_objects(&fixture->registry, &fixture->objects)
            == diagnostic);
    return selected_ipc_report_case(index, test->name, (unsigned)error, context.token, preserved);
}

size_t selected_ipc_token_case_count(void)
{
    return sizeof(token_cases) / sizeof(token_cases[0]);
}

bool selected_ipc_token_cases(size_t first_case)
{
    for (size_t index = 0; index < selected_ipc_token_case_count(); ++index) {
        if (!run_token_case(first_case + index, &token_cases[index])) {
            return false;
        }
    }
    return true;
}
