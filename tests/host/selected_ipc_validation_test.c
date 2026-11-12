#include "tests/host/selected_ipc_validation_test.h"
#include "kernel/endpoint/endpoint_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(MICROS_TEST_WORK_COUNTS)
#include "tests/work/work_counts.h"
#endif

#if defined(MICROS_SELECTED_IPC_LAUNCHER)
#include <unistd.h>
#endif

static struct selected_ipc_fixture fixture;
static const char *case_name;

struct selected_ipc_fixture *selected_ipc_fixture_state(void)
{
    return &fixture;
}

void selected_ipc_test_name(const char *name)
{
    case_name = name;
}

#if defined(MICROS_TEST_WORK_COUNTS)
static struct micros_test_work_snapshot snapshot;
#endif

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "IPC01_FIXTURE_FAILURE case=%s line=%d expression=%s\n", \
                    case_name, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

bool selected_ipc_setup(size_t max_threads)
{
    struct micros_privilege_profile profiles[2] = {0};

    case_name = "setup";
    memset(&fixture, 0, sizeof(fixture));
    for (size_t index = 0; index < 2; ++index) {
        profiles[index].id = index == 0 ? 1 : 31;
        memset(profiles[index].name, index == 0 ? 'A' : 'B', 31);
        profiles[index].operations = MICROS_PRIVILEGE_OPERATION_DEFINED_MASK;
        profiles[index].call_targets = (UINT32_C(1) << 1) | (UINT32_C(1) << 31);
        profiles[index].send_targets = profiles[index].call_targets;
        profiles[index].notify_targets = profiles[index].call_targets;
        profiles[index].kernel_operations = UINT64_C(1) << 3;
    }
    REQUIRE(micros_kernel_objects_initialize(&fixture.objects, max_threads, 1)
            == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_endpoint_registry_initialize(&fixture.registry, profiles, 2)
            == MICROS_ENDPOINT_OK);
    for (size_t index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        REQUIRE(micros_process_create(&fixture.objects, &fixture.processes[index])
                == MICROS_KERNEL_OBJECT_OK);
    }
    for (size_t index = 0; index < 2; ++index) {
        size_t slot = index == 0 ? 0 : MICROS_PROCESS_CAPACITY - 1;

        REQUIRE(micros_endpoint_reserve(
            &fixture.registry, &fixture.objects, fixture.processes[slot],
            &fixture.endpoints[index]
        ) == MICROS_ENDPOINT_OK);
        REQUIRE(micros_endpoint_install_profile(
            &fixture.registry, &fixture.objects, fixture.processes[slot],
            profiles[index].id
        ) == MICROS_ENDPOINT_OK);
        REQUIRE(micros_endpoint_activate(
            &fixture.registry, &fixture.objects, fixture.endpoints[index]
        ) == MICROS_ENDPOINT_OK);
    }
    REQUIRE(micros_endpoint_registry_validate_objects(
        &fixture.registry, &fixture.objects
    ) == MICROS_ENDPOINT_OK);
    fixture.before_objects = fixture.objects;
    fixture.before_registry = fixture.registry;
    return true;
}

static enum micros_endpoint_error query(size_t operation, uint64_t *value)
{
    const struct micros_privilege_profile *profile = NULL;
    const struct micros_endpoint_record *endpoint = NULL;
    enum micros_endpoint_error error;

    *value = 0;
    switch (operation) {
    case 0:
    case 1:
        error = micros_privilege_profile_resolve(
            &fixture.registry, operation == 0 ? 1 : 31, &profile
        );
        if (error == MICROS_ENDPOINT_OK) {
            *value = profile->id;
        }
        return error;
    case 2:
        return micros_privilege_profile_allows_operation(
            &fixture.registry, 1, MICROS_PRIVILEGE_OPERATION_RECEIVE
        );
    case 3:
        return micros_privilege_profile_allows_target(
            &fixture.registry, 1, MICROS_PRIVILEGE_OPERATION_SEND, 31
        );
    case 4:
        return micros_privilege_profile_allows_kernel_operation(
            &fixture.registry, 31, 3
        );
    case 5:
    case 6:
        error = micros_endpoint_resolve_active(
            &fixture.registry, &fixture.objects,
            fixture.endpoints[operation - 5], &endpoint
        );
        if (error == MICROS_ENDPOINT_OK) {
            *value = endpoint->value;
        }
        return error;
    case 7:
        return micros_endpoint_authorize_operation(
            &fixture.registry, &fixture.objects, fixture.endpoints[0],
            MICROS_PRIVILEGE_OPERATION_RECEIVE
        );
    case 8:
        return micros_endpoint_authorize_target(
            &fixture.registry, &fixture.objects, fixture.endpoints[0],
            MICROS_PRIVILEGE_OPERATION_SEND, fixture.endpoints[1]
        );
    case 9:
        return micros_endpoint_authorize_kernel_operation(
            &fixture.registry, &fixture.objects, fixture.endpoints[0], 3
        );
    default:
        return MICROS_ENDPOINT_ERROR_ARGUMENT;
    }
}

bool selected_ipc_begin_capture(size_t index)
{
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(micros_test_work_begin(
        UINT64_C(0x4950433031000001), index + 1,
        (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_OBJECTS)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_SCHEDULER)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_ENDPOINT)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_IPC)
    ) == MICROS_TEST_WORK_OK);
#else
    (void)index;
#endif
    return true;
}

bool selected_ipc_finish_capture(void)
{
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
#endif
    return true;
}

static uint64_t digest_bytes(uint64_t digest, const volatile void *storage, size_t size)
{
    const volatile unsigned char *bytes = storage;

    for (size_t index = 0; index < size; ++index) {
        digest ^= bytes[index];
        digest *= UINT64_C(1099511628211);
    }
    return digest;
}

#define MIX_FIELD(field) digest = digest_bytes(digest, &(field), sizeof(field))
#define MIX_HANDLE(handle) \
    do { MIX_FIELD((handle).slot); MIX_FIELD((handle).generation); } while (false)

static uint64_t thread_digest(uint64_t digest, const struct micros_thread *thread)
{
    MIX_FIELD(thread->slot_state);
    MIX_FIELD(thread->generation);
    MIX_HANDLE(thread->owner);
    MIX_FIELD(thread->context_attached);
    MIX_FIELD(thread->kernel_stack_bottom);
    MIX_FIELD(thread->kernel_stack_top);
    MIX_FIELD(thread->user_context);
    MIX_FIELD(thread->runtime_flags);
    MIX_FIELD(thread->scheduler_assigned);
    MIX_FIELD(thread->scheduler_preemptible);
    MIX_FIELD(thread->scheduler_priority);
    MIX_HANDLE(thread->scheduler_hart);
    MIX_FIELD(thread->quantum_counter_ticks);
    MIX_FIELD(thread->remaining_counter_ticks);
    MIX_FIELD(thread->ready_linked);
    MIX_HANDLE(thread->ready_next);
    MIX_FIELD(thread->ipc_queue_kind);
    MIX_HANDLE(thread->ipc_next);
    MIX_FIELD(thread->ipc_outbound_message);
    MIX_FIELD(thread->ipc_send_destination);
    MIX_FIELD(thread->ipc_receive_source);
    MIX_FIELD(thread->ipc_receive_buffer);
    MIX_FIELD(thread->ipc_delivery_pending);
    MIX_FIELD(thread->ipc_inbound_message);
    MIX_FIELD(thread->ipc_staged_result);
    MIX_FIELD(thread->ipc_reply_token);
    MIX_FIELD(thread->ipc_reply_callee);
    MIX_FIELD(thread->ipc_reply_phase);
    MIX_HANDLE(thread->ipc_request_receiver);
    return digest;
}

static uint64_t hart_digest(uint64_t digest, const struct micros_hart *hart)
{
    MIX_FIELD(hart->trap);
    MIX_FIELD(hart->slot_state);
    MIX_FIELD(hart->generation);
    MIX_FIELD(hart->hardware_id);
    MIX_FIELD(hart->trap_installed);
    MIX_FIELD(hart->idle_primary_stack_bottom);
    MIX_FIELD(hart->idle_primary_stack_top);
    MIX_HANDLE(hart->current_thread);
    for (size_t index = 0; index < MICROS_SCHEDULER_PRIORITY_COUNT; ++index) {
        MIX_HANDLE(hart->ready_head[index]);
        MIX_HANDLE(hart->ready_tail[index]);
    }
    MIX_FIELD(hart->accounting_owner);
    MIX_FIELD(hart->accounting_started_at);
    MIX_HANDLE(hart->accounted_thread);
    MIX_FIELD(hart->kernel_counter_ticks);
    MIX_FIELD(hart->idle_counter_ticks);
    MIX_FIELD(hart->interrupt_depth);
    MIX_FIELD(hart->preempt_disable_count);
    MIX_FIELD(hart->reschedule_pending);
    MIX_FIELD(hart->timer.initialized);
    MIX_FIELD(hart->timer.active);
    MIX_FIELD(hart->timer.interval);
    MIX_FIELD(hart->timer.deadline);
    MIX_FIELD(hart->timer.ticks);
    MIX_FIELD(hart->timer.test_stop_after);
    return digest;
}

/* Struct stores may change handle padding, but not any defined state. */
static uint64_t state_digest(
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry
)
{
    uint64_t digest = UINT64_C(14695981039346656037);

    MIX_FIELD(objects->initialization_magic);
    MIX_FIELD(objects->max_threads_per_process);
    MIX_FIELD(objects->max_harts);
    MIX_FIELD(objects->live_process_count);
    MIX_FIELD(objects->live_thread_count);
    MIX_FIELD(objects->registered_hart_count);
    for (size_t index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_process *process = &objects->processes[index];
        const struct micros_endpoint_record *endpoint = &registry->endpoints[index];

        MIX_FIELD(process->slot_state);
        MIX_FIELD(process->generation);
        MIX_FIELD(process->live_thread_count);
        MIX_FIELD(process->address_space_root);
        MIX_FIELD(process->primary_endpoint);
        MIX_FIELD(process->privilege_profile);
        MIX_FIELD(process->endpoint_lifecycle_consumed);
        MIX_HANDLE(process->sole_thread);
        MIX_FIELD(endpoint->state);
        MIX_HANDLE(endpoint->owner);
        MIX_FIELD(endpoint->value);
        MIX_HANDLE(endpoint->sender_head);
        MIX_HANDLE(endpoint->sender_tail);
        MIX_HANDLE(endpoint->receiver_head);
        MIX_HANDLE(endpoint->receiver_tail);
        MIX_FIELD(endpoint->pending_notification_sources);
        MIX_FIELD(endpoint->pending_kernel_events);
        MIX_FIELD(endpoint->pending_events);
    }
    for (size_t index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        digest = thread_digest(digest, &objects->threads[index]);
    }
    for (size_t index = 0; index < MICROS_HART_CAPACITY; ++index) {
        digest = hart_digest(digest, &objects->harts[index]);
    }
    MIX_FIELD(registry->initialization_magic);
    MIX_FIELD(registry->profile_count);
    MIX_FIELD(registry->last_reply_token);
    MIX_FIELD(registry->profiles);
    return digest;
}

#undef MIX_HANDLE
#undef MIX_FIELD

bool selected_ipc_report_case(size_t index, const char *name, unsigned result, uint64_t value, bool preserved)
{
    REQUIRE((memcmp(&fixture.objects, &fixture.before_objects, sizeof(fixture.objects)) == 0
        && memcmp(&fixture.registry, &fixture.before_registry, sizeof(fixture.registry)) == 0)
        == preserved);
    printf("MICROS_IPC01_CASE {\"version\":1,\"case\":%zu,\"name\":\"%s\","
           "\"result\":%u,\"value\":%" PRIu64 ",\"preserved\":%u,"
           "\"before_state\":%" PRIu64 ",\"after_state\":%" PRIu64 ","
           "\"lookups\":%" PRIu64 ",\"translations\":%" PRIu64 ",\"plan_bytes\":%zu}\n",
           index + 1, name, result, value, (unsigned)preserved,
           state_digest(&fixture.before_objects, &fixture.before_registry),
           state_digest(&fixture.objects, &fixture.registry),
           fixture.lookups, fixture.translations, fixture.plan_bytes);
#if defined(MICROS_TEST_WORK_COUNTS)
    printf("MICROS_IPC01_WORK {\"version\":%u,\"reserved0\":%u,"
           "\"producer\":%" PRIu64 ",\"case\":%" PRIu64 ","
           "\"owner_mask\":%u,\"reserved1\":%u,\"counts\":[",
           snapshot.version, snapshot.reserved0, snapshot.producer_label,
           snapshot.capture_label, snapshot.owner_mask, snapshot.reserved1);
    for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        printf("%s[", owner == 0 ? "" : ",");
        for (size_t kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            printf("%s%" PRIu64, kind == 0 ? "" : ",",
                   snapshot.counts[owner][kind]);
        }
        printf("]");
    }
    printf("]}\n");
#endif
    return true;
}

static bool run_case(size_t index, const char *name)
{
    enum micros_endpoint_error result;
    uint64_t value;

    case_name = name;
    REQUIRE(selected_ipc_begin_capture(index));
    result = query(index, &value);
    REQUIRE(selected_ipc_finish_capture());
    REQUIRE(result == MICROS_ENDPOINT_OK);
    return selected_ipc_report_case(index, name, (unsigned)result, value, true);
}

bool selected_ipc_prepare_thread(size_t process_slot)
{
    struct micros_user_context context = {
        .sp = UINT64_C(0x70000000), .sepc = UINT64_C(0x40000000),
    };
    struct micros_thread_handle *handle;
    uintptr_t bottom;

    REQUIRE(fixture.thread_count < MICROS_THREAD_CAPACITY);
    handle = &fixture.threads[fixture.thread_count++];
    REQUIRE(micros_thread_create(
        &fixture.objects, fixture.processes[process_slot], handle
    ) == MICROS_KERNEL_OBJECT_OK);
    bottom = UINT64_C(0x10000000) + (uintptr_t)handle->slot * MICROS_THREAD_KERNEL_STACK_SIZE;
    REQUIRE(micros_thread_attach_execution_context(
        &fixture.objects, *handle, bottom, bottom + MICROS_THREAD_KERNEL_STACK_SIZE, &context
    ) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_thread_scheduler_admit(
        &fixture.objects, fixture.hart, *handle, 7, 100, true
    ) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_thread_scheduler_hold(&fixture.objects, *handle) == MICROS_KERNEL_OBJECT_OK);
    return true;
}

static bool prepare_third_endpoint(void)
{
    size_t slot = MICROS_PROCESS_CAPACITY - 2;

    REQUIRE(micros_endpoint_reserve(
        &fixture.registry, &fixture.objects, fixture.processes[slot], &fixture.endpoints[2]
    ) == MICROS_ENDPOINT_OK);
    REQUIRE(micros_endpoint_install_profile(
        &fixture.registry, &fixture.objects, fixture.processes[slot], 1
    ) == MICROS_ENDPOINT_OK);
    REQUIRE(micros_endpoint_activate(
        &fixture.registry, &fixture.objects, fixture.endpoints[2]
    ) == MICROS_ENDPOINT_OK);
    return selected_ipc_prepare_thread(slot);
}

enum ipc_case {
    SEND_WAIT,
    SEND_DELIVERY,
    SEND_LAST,
    SEND_UNMATCHED,
    RECEIVE_WAIT,
    RECEIVE_DELIVERY,
    RECEIVE_LAST,
    CALL_WAIT,
    CALL_DELIVERY,
    REPLY,
    REPLY_RECEIVE_WAIT,
    REPLY_RECEIVE_SENDER,
    NOTIFY_COALESCE,
    NOTIFY_DELIVERY,
    KERNEL_COALESCE,
    KERNEL_DELIVERY,
    KERNEL_LAST,
    STAGE_NO_MESSAGE,
    CANCEL_CLOSE,
    KERNEL_BACKEDGE,
    RECEIVE_PENDING_CORRUPT,
    REPLY_PENDING_CORRUPT,
    CALL_TOKEN_COLLISION,
    IPC_CASE_COUNT,
};

static bool prepare_ipc_case(enum ipc_case operation, const struct micros_ipc_message *message)
{
    REQUIRE(selected_ipc_setup(MICROS_THREAD_CAPACITY));
    REQUIRE(micros_hart_register(&fixture.objects, 0, &fixture.hart) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_hart_install_trap_stacks(
        &fixture.objects, fixture.hart,
        UINT64_C(0x20000000), UINT64_C(0x20000000) + MICROS_PRIMARY_TRAP_STACK_MIN_SIZE,
        UINT64_C(0x30000000), UINT64_C(0x30000000) + MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE
    ) == MICROS_KERNEL_OBJECT_OK);
    if (operation == KERNEL_LAST || operation == KERNEL_BACKEDGE) {
        size_t count = operation == KERNEL_LAST ? MICROS_THREAD_CAPACITY : 3;
        size_t matched = operation == KERNEL_LAST ? count - 1 : 1;

        for (size_t index = 0; index < count; ++index) {
            REQUIRE(selected_ipc_prepare_thread(MICROS_PROCESS_CAPACITY - 1));
            REQUIRE(micros_ipc_receiver_enqueue(
                &fixture.registry, &fixture.objects, fixture.threads[index],
                index == matched ? MICROS_ENDPOINT_ANY : fixture.endpoints[0],
                UINT64_C(0x50000000)
            ) == MICROS_IPC_OK);
        }
        if (operation == KERNEL_BACKEDGE) {
            fixture.objects.threads[fixture.threads[1].slot].ipc_next = fixture.threads[0];
        }
        return true;
    }
    REQUIRE(selected_ipc_prepare_thread(0));
    REQUIRE(selected_ipc_prepare_thread(MICROS_PROCESS_CAPACITY - 1));
    if (operation == RECEIVE_PENDING_CORRUPT) {
        fixture.registry.endpoints[MICROS_PROCESS_CAPACITY - 1].pending_events[0] = 1;
    }
    if (operation == CALL_TOKEN_COLLISION) {
        REQUIRE(micros_ipc_call(
            &fixture.registry, &fixture.objects, fixture.threads[0], fixture.endpoints[1],
            message, UINT64_C(0x50000100)
        ) == MICROS_IPC_OK);
        REQUIRE(selected_ipc_prepare_thread(0));
        fixture.registry.last_reply_token = 0;
        return true;
    }
    if (operation == SEND_LAST || operation == SEND_UNMATCHED
        || operation == RECEIVE_LAST || operation == REPLY_RECEIVE_SENDER) {
        REQUIRE(prepare_third_endpoint());
    }
    if (operation == SEND_LAST) {
        for (size_t index = 0; index < 16; ++index) {
            struct micros_thread_handle receiver = fixture.threads[1];

            if (index != 0) {
                REQUIRE(selected_ipc_prepare_thread(MICROS_PROCESS_CAPACITY - 1));
                receiver = fixture.threads[fixture.thread_count - 1];
            }
            REQUIRE(micros_ipc_receiver_enqueue(
                &fixture.registry, &fixture.objects, receiver,
                index == 15 ? MICROS_ENDPOINT_ANY : fixture.endpoints[2], UINT64_C(0x50000000)
            ) == MICROS_IPC_OK);
        }
    } else if (operation == SEND_UNMATCHED) {
        REQUIRE(micros_ipc_receiver_enqueue(
            &fixture.registry, &fixture.objects, fixture.threads[1],
            fixture.endpoints[2], UINT64_C(0x50000000)
        ) == MICROS_IPC_OK);
    } else if (operation == SEND_DELIVERY || operation == CALL_DELIVERY
        || operation == NOTIFY_DELIVERY || operation == KERNEL_DELIVERY
        || operation == REPLY || operation == REPLY_RECEIVE_WAIT
        || operation == REPLY_RECEIVE_SENDER || operation == REPLY_PENDING_CORRUPT) {
        REQUIRE(micros_ipc_receiver_enqueue(
            &fixture.registry, &fixture.objects, fixture.threads[1],
            MICROS_ENDPOINT_ANY, UINT64_C(0x50000000)
        ) == MICROS_IPC_OK);
    }
    if (operation == RECEIVE_DELIVERY || operation == RECEIVE_LAST || operation == CANCEL_CLOSE) {
        size_t count = operation == RECEIVE_LAST ? 16 : 1;

        for (size_t index = 0; index < count; ++index) {
            struct micros_thread_handle sender = fixture.threads[0];

            if (index + 1 == count && operation == RECEIVE_LAST) {
                sender = fixture.threads[2];
            } else if (index != 0) {
                REQUIRE(selected_ipc_prepare_thread(0));
                sender = fixture.threads[fixture.thread_count - 1];
            }
            REQUIRE(micros_ipc_sender_enqueue(
                &fixture.registry, &fixture.objects, sender, fixture.endpoints[1], message, 0, 0
            ) == MICROS_IPC_OK);
        }
    }
    if (operation == REPLY || operation == REPLY_RECEIVE_WAIT
        || operation == REPLY_RECEIVE_SENDER || operation == REPLY_PENDING_CORRUPT) {
        struct micros_thread *receiver = &fixture.objects.threads[fixture.threads[1].slot];

        REQUIRE(micros_ipc_call(
            &fixture.registry, &fixture.objects, fixture.threads[0], fixture.endpoints[1],
            message, UINT64_C(0x50000100)
        ) == MICROS_IPC_OK);
        REQUIRE(receiver->ipc_delivery_pending && receiver->ipc_inbound_message.reply_token == 128);
        if (operation == REPLY_PENDING_CORRUPT) {
            REQUIRE(selected_ipc_prepare_thread(MICROS_PROCESS_CAPACITY - 1));
            receiver->ipc_delivery_pending = false;
            return true;
        }
        receiver->ipc_delivery_pending = false;
        receiver->ipc_receive_buffer = 0;
        memset(&receiver->ipc_inbound_message, 0, sizeof(receiver->ipc_inbound_message));
        fixture.objects.threads[fixture.threads[0].slot].ipc_reply_phase = MICROS_IPC_REPLY_PHASE_RETURNED;
        fixture.objects.threads[fixture.threads[0].slot].ipc_request_receiver =
            (struct micros_thread_handle){0, 0};
        REQUIRE(micros_thread_scheduler_hold(&fixture.objects, fixture.threads[1])
                == MICROS_KERNEL_OBJECT_OK);
        if (operation == REPLY_RECEIVE_SENDER) {
            REQUIRE(micros_ipc_sender_enqueue(
                &fixture.registry, &fixture.objects, fixture.threads[2],
                fixture.endpoints[1], message, 0, 0
            ) == MICROS_IPC_OK);
        }
    }
    return true;
}

static enum micros_ipc_error execute_ipc_case(
    enum ipc_case operation,
    const struct micros_ipc_message *message
)
{
    switch (operation) {
    case SEND_WAIT:
    case SEND_DELIVERY:
    case SEND_LAST:
    case SEND_UNMATCHED:
        return micros_ipc_send(
            &fixture.registry, &fixture.objects, fixture.threads[0], fixture.endpoints[1], message
        );
    case RECEIVE_WAIT:
    case RECEIVE_DELIVERY:
    case RECEIVE_LAST:
    case RECEIVE_PENDING_CORRUPT:
        return micros_ipc_receive(
            &fixture.registry, &fixture.objects, fixture.threads[1],
            operation == RECEIVE_LAST ? fixture.endpoints[2]
                : operation == RECEIVE_PENDING_CORRUPT ? fixture.endpoints[0] : MICROS_ENDPOINT_ANY,
            UINT64_C(0x50000000)
        );
    case CALL_WAIT:
    case CALL_DELIVERY:
        return micros_ipc_call(
            &fixture.registry, &fixture.objects, fixture.threads[0], fixture.endpoints[1],
            message, UINT64_C(0x50000100)
        );
    case CALL_TOKEN_COLLISION:
        return micros_ipc_call(
            &fixture.registry, &fixture.objects, fixture.threads[2], fixture.endpoints[1],
            message, UINT64_C(0x50000200)
        );
    case REPLY:
        return micros_ipc_reply(
            &fixture.registry, &fixture.objects, fixture.threads[1], 128, message
        );
    case REPLY_PENDING_CORRUPT:
        return micros_ipc_reply(
            &fixture.registry, &fixture.objects, fixture.threads[2], 128, message
        );
    case REPLY_RECEIVE_WAIT:
    case REPLY_RECEIVE_SENDER:
        return micros_ipc_reply_receive(
            &fixture.registry, &fixture.objects, fixture.threads[1], 128, message,
            MICROS_ENDPOINT_ANY, UINT64_C(0x50000000)
        );
    case NOTIFY_COALESCE:
    case NOTIFY_DELIVERY:
        return micros_ipc_notify(
            &fixture.registry, &fixture.objects, fixture.threads[0], fixture.endpoints[1], UINT64_C(0x80)
        );
    case KERNEL_COALESCE:
    case KERNEL_DELIVERY:
    case KERNEL_LAST:
    case KERNEL_BACKEDGE:
        return micros_ipc_inject_kernel_notification(
            &fixture.registry, &fixture.objects, fixture.endpoints[1], UINT64_C(0x40)
        );
    case STAGE_NO_MESSAGE:
        return micros_ipc_stage_no_message_completion(
            &fixture.registry, &fixture.objects, fixture.threads[0], MICROS_IPC_OK
        );
    case CANCEL_CLOSE:
        return micros_ipc_endpoint_close(
            &fixture.registry, &fixture.objects, fixture.endpoints[1]
        );
    default:
        return MICROS_IPC_ERROR_ARGUMENT;
    }
}

static bool run_ipc_case(size_t index, enum ipc_case operation, const char *name)
{
    struct micros_ipc_message message = {
        .source = MICROS_ENDPOINT_ANY, .type = UINT32_C(0x1234),
        .reply_token = UINT64_MAX, .payload = {UINT8_C(0x5a)},
    };
    enum micros_ipc_error error;
    bool invalid = operation >= KERNEL_BACKEDGE;

    case_name = name;
    REQUIRE(prepare_ipc_case(operation, &message));
    case_name = name;
    REQUIRE(micros_endpoint_registry_validate_objects(
        &fixture.registry, &fixture.objects
    ) == (invalid ? MICROS_ENDPOINT_ERROR_INVARIANT : MICROS_ENDPOINT_OK));
    fixture.before_objects = fixture.objects;
    fixture.before_registry = fixture.registry;
    REQUIRE(selected_ipc_begin_capture(index));
    error = execute_ipc_case(operation, &message);
    REQUIRE(selected_ipc_finish_capture());
    REQUIRE(error == (invalid ? MICROS_IPC_ERROR_INVARIANT : MICROS_IPC_OK));
    REQUIRE(micros_endpoint_registry_validate_objects(
        &fixture.registry, &fixture.objects
    ) == (invalid ? MICROS_ENDPOINT_ERROR_INVARIANT : MICROS_ENDPOINT_OK));
    if (operation == CALL_WAIT || operation == CALL_DELIVERY) {
        REQUIRE(fixture.objects.threads[fixture.threads[0].slot].ipc_reply_token == 128);
    } else if (operation == REPLY || operation == REPLY_RECEIVE_WAIT || operation == REPLY_RECEIVE_SENDER) {
        const struct micros_thread *caller = &fixture.objects.threads[fixture.threads[0].slot];

        REQUIRE(caller->ipc_reply_token == 0 && caller->ipc_delivery_pending);
        REQUIRE(caller->ipc_inbound_message.source == fixture.endpoints[1]);
        REQUIRE(caller->ipc_inbound_message.reply_token == 0);
    } else if (operation == CANCEL_CLOSE) {
        REQUIRE(fixture.objects.threads[fixture.threads[0].slot].ipc_staged_result
                == MICROS_IPC_ERROR_DEAD_ENDPOINT);
        REQUIRE(fixture.registry.endpoints[MICROS_PROCESS_CAPACITY - 1].state == MICROS_ENDPOINT_STATE_FREE);
    } else if (operation == KERNEL_LAST) {
        REQUIRE(fixture.objects.threads[fixture.threads[MICROS_THREAD_CAPACITY - 1].slot]
                .ipc_delivery_pending);
        REQUIRE(!fixture.objects.threads[fixture.threads[0].slot].ipc_delivery_pending);
    }
    return selected_ipc_report_case(index, name, (unsigned)error, fixture.registry.last_reply_token, invalid);
}

static bool run_lifecycle_case(size_t index, size_t operation, const char *name)
{
    const size_t slot = MICROS_PROCESS_CAPACITY - 2;
    struct micros_thread_handle thread;
    micros_endpoint_t endpoint = 0;
    uint64_t value = 0;
    enum micros_endpoint_error error;
    bool invalid = operation >= 6;
    bool preserved = operation == 3 || operation == 4 || invalid;

    REQUIRE(selected_ipc_setup(1));
    case_name = name;
    REQUIRE(micros_thread_create(
        &fixture.objects, fixture.processes[slot], &thread
    ) == MICROS_KERNEL_OBJECT_OK);
    if (operation != 0 && operation != 7) {
        REQUIRE(micros_endpoint_reserve(
            &fixture.registry, &fixture.objects, fixture.processes[slot], &endpoint
        ) == MICROS_ENDPOINT_OK);
    }
    if (operation == 2 || operation == 4) {
        REQUIRE(micros_endpoint_install_profile(
            &fixture.registry, &fixture.objects, fixture.processes[slot], 1
        ) == MICROS_ENDPOINT_OK);
    }
    if (operation == 4) {
        REQUIRE(micros_endpoint_activate(&fixture.registry, &fixture.objects, endpoint)
                == MICROS_ENDPOINT_OK);
    } else if (operation == 6) {
        REQUIRE(micros_hart_register(&fixture.objects, 0, &fixture.hart) == MICROS_KERNEL_OBJECT_OK);
        fixture.objects.harts[fixture.hart.slot].current_thread =
            (struct micros_thread_handle){MICROS_THREAD_CAPACITY, 1};
    } else if (operation == 7) {
        fixture.registry.endpoints[slot].pending_events[MICROS_PROCESS_CAPACITY - 1] = 1;
    }
    REQUIRE(micros_endpoint_registry_validate_objects(
        &fixture.registry, &fixture.objects
    ) == (invalid ? MICROS_ENDPOINT_ERROR_INVARIANT : MICROS_ENDPOINT_OK));
    fixture.before_objects = fixture.objects;
    fixture.before_registry = fixture.registry;
    REQUIRE(selected_ipc_begin_capture(index));
    switch (operation) {
    case 0:
    case 7:
        error = micros_endpoint_reserve(
            &fixture.registry, &fixture.objects, fixture.processes[slot], &endpoint
        );
        value = endpoint;
        break;
    case 1:
        error = micros_endpoint_install_profile(
            &fixture.registry, &fixture.objects, fixture.processes[slot], 1
        );
        value = fixture.objects.processes[slot].privilege_profile;
        break;
    case 2:
        error = micros_endpoint_activate(&fixture.registry, &fixture.objects, endpoint);
        value = fixture.registry.endpoints[slot].state;
        break;
    case 3:
    case 6:
        error = micros_endpoint_preflight_publish(&fixture.registry, &fixture.objects, endpoint, 1);
        break;
    case 4:
        error = micros_endpoint_preflight_source_only_for_teardown(
            &fixture.registry, &fixture.objects, endpoint
        );
        break;
    default:
        error = micros_endpoint_close(&fixture.registry, &fixture.objects, endpoint);
        break;
    }
    REQUIRE(selected_ipc_finish_capture());
    REQUIRE(error == (invalid ? MICROS_ENDPOINT_ERROR_INVARIANT : MICROS_ENDPOINT_OK));
    REQUIRE(micros_endpoint_registry_validate_objects(
        &fixture.registry, &fixture.objects
    ) == (invalid ? MICROS_ENDPOINT_ERROR_INVARIANT : MICROS_ENDPOINT_OK));
    return selected_ipc_report_case(index, name, (unsigned)error, value, preserved);
}

int main(int argc, char **argv)
{
    static const char *const names[] = {
        "profile-first", "profile-last", "profile-operation", "profile-target",
        "profile-kernel", "endpoint-first", "endpoint-last",
        "endpoint-operation", "endpoint-target", "endpoint-kernel",
    };
    static const char *const ipc_names[] = {
        "send-wait", "send-delivery", "send-last", "send-unmatched",
        "receive-wait", "receive-delivery", "receive-last",
        "call-wait", "call-delivery", "reply", "reply-receive-wait", "reply-receive-sender",
        "notify-coalesce", "notify-delivery", "kernel-coalesce", "kernel-delivery", "kernel-last",
        "stage-no-message", "cancel-close",
        "kernel-backedge", "receive-pending-corrupt", "reply-pending-corrupt", "call-token-collision",
    };
    static const char *const lifecycle_names[] = {
        "reserve", "install-profile", "activate", "preflight-publish",
        "preflight-source-only", "substrate-close", "publish-bad-current", "reserve-dirty-record",
    };
    size_t query_count = sizeof(names) / sizeof(names[0]);
    size_t lifecycle_count = sizeof(lifecycle_names) / sizeof(lifecycle_names[0]);

    _Static_assert(sizeof(ipc_names) / sizeof(ipc_names[0]) == IPC_CASE_COUNT,
                   "every native IPC operation has a case identity");

#if defined(MICROS_SELECTED_IPC_LAUNCHER)
    if (argc == 2) {
        const char *launcher = getenv("__PYVENV_LAUNCHER__");
        char *const arguments[] = {
            MICROS_SELECTED_IPC_PYTHON, "-E", "-S", "-B",
            MICROS_SELECTED_IPC_DRIVER, "--executable", argv[1],
            "--executable", argv[0], NULL,
        };

        if (launcher != NULL && launcher[0] != '\0') {
            fputs("__PYVENV_LAUNCHER__ is unsupported\n", stderr);
            return 2;
        }
        if (chdir(MICROS_SELECTED_IPC_ROOT) != 0) {
            return 2;
        }
        execv(MICROS_SELECTED_IPC_PYTHON, arguments);
        perror("IPC01 launcher");
        return 2;
    }
#endif
    if (argc != 3 || strcmp(argv[1], "--run") != 0
#if defined(MICROS_TEST_WORK_COUNTS)
        || strcmp(argv[2], "work") != 0
#else
        || strcmp(argv[2], "ordinary") != 0
#endif
    ) {
        return 2;
    }
    if (!selected_ipc_setup(1)) {
        return 1;
    }
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (!run_case(index, names[index])) {
            return 1;
        }
    }
    for (size_t index = 0; index < IPC_CASE_COUNT; ++index) {
        if (!run_ipc_case(query_count + index, (enum ipc_case)index, ipc_names[index])) {
            return 1;
        }
    }
    for (size_t index = 0; index < lifecycle_count; ++index) {
        if (!run_lifecycle_case(query_count + IPC_CASE_COUNT + index, index, lifecycle_names[index])) {
            return 1;
        }
    }
    if (!selected_ipc_completion_cases(query_count + IPC_CASE_COUNT + lifecycle_count)) {
        return 1;
    }
    if (!selected_ipc_deadlock_cases(query_count + IPC_CASE_COUNT + lifecycle_count
                                    + selected_ipc_completion_case_count())) {
        return 1;
    }
    if (!selected_ipc_token_cases(query_count + IPC_CASE_COUNT + lifecycle_count
                                 + selected_ipc_completion_case_count()
                                 + selected_ipc_deadlock_case_count())) {
        return 1;
    }
    printf("MICROS_IPC01_NATIVE_PASS cases=%zu\n", query_count + IPC_CASE_COUNT + lifecycle_count
           + selected_ipc_completion_case_count() + selected_ipc_deadlock_case_count()
           + selected_ipc_token_case_count());
    return 0;
}
