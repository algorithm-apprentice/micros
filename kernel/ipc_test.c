#include "micros/ipc_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "kernel/ipc_runtime_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler_core.h"
#include "micros/user_address_space.h"

enum {
    IPC_TEST_CLIENT = 0,
    IPC_TEST_SERVER,
    IPC_TEST_PEER,
    IPC_TEST_PROCESS_COUNT,
    IPC_TEST_CLIENT_PROFILE = 1,
    IPC_TEST_SERVER_PROFILE = 2,
    IPC_TEST_PEER_PROFILE = 3,
};

_Alignas(4096)
static unsigned char
    ipc_test_stacks[IPC_TEST_PROCESS_COUNT][
        MICROS_THREAD_KERNEL_STACK_SIZE
    ];
static struct micros_endpoint_registry *registry;
static struct micros_endpoint_registry registry_snapshot;
static struct micros_kernel_objects objects_snapshot;
static struct micros_hart hart_snapshot;
static struct micros_ipc_message receive_buffers[IPC_TEST_PROCESS_COUNT];
static struct micros_ipc_message send_messages[8];
static const struct micros_ipc_message zero_message;
static struct micros_user_context
    context_snapshots[IPC_TEST_PROCESS_COUNT];
static uintptr_t root_snapshots[IPC_TEST_PROCESS_COUNT];

bool micros_ipc_runtime_run_self_test(void);

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static bool bytes_equal(const void *left, const void *right, size_t size)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

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
    copy_bytes(&context, words, sizeof(context));
    return context;
}

static void fill_stack_pattern(size_t stack_index)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_KERNEL_STACK_SIZE; ++index) {
        ipc_test_stacks[stack_index][index] =
            (unsigned char)(UINT8_C(0x31) + stack_index + index);
    }
}

static bool stack_pattern_matches(size_t stack_index)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_KERNEL_STACK_SIZE; ++index) {
        if (
            ipc_test_stacks[stack_index][index]
                != (unsigned char)(
                    UINT8_C(0x31) + stack_index + index
                )
        ) {
            return false;
        }
    }
    return true;
}

static bool snapshot_execution_state(
    const struct micros_kernel_objects *objects,
    const struct micros_process_handle processes[IPC_TEST_PROCESS_COUNT],
    const struct micros_thread_handle threads[IPC_TEST_PROCESS_COUNT]
)
{
    size_t index;

    for (index = 0; index < IPC_TEST_PROCESS_COUNT; ++index) {
        uintptr_t stack_bottom;
        uintptr_t stack_top;

        root_snapshots[index] =
            objects->processes[processes[index].slot]
                .address_space_root;
        if (
            root_snapshots[index] == 0
            || micros_user_address_space_validate(processes[index])
                != MICROS_USER_ADDRESS_SPACE_OK
            || micros_thread_inspect_execution_context(
                objects,
                threads[index],
                &context_snapshots[index],
                &stack_bottom,
                &stack_top
            ) != MICROS_KERNEL_OBJECT_OK
            || stack_bottom
                != (uintptr_t)&ipc_test_stacks[index][0]
            || stack_top
                != (uintptr_t)&ipc_test_stacks[index][
                    MICROS_THREAD_KERNEL_STACK_SIZE
                ]
        ) {
            return false;
        }
        fill_stack_pattern(index);
    }
    return (
        root_snapshots[0] != root_snapshots[1]
        && root_snapshots[0] != root_snapshots[2]
        && root_snapshots[1] != root_snapshots[2]
    );
}

static bool execution_state_matches(
    const struct micros_kernel_objects *objects,
    const struct micros_process_handle processes[IPC_TEST_PROCESS_COUNT],
    const struct micros_thread_handle threads[IPC_TEST_PROCESS_COUNT]
)
{
    size_t index;

    for (index = 0; index < IPC_TEST_PROCESS_COUNT; ++index) {
        struct micros_user_context observed;
        uintptr_t stack_bottom;
        uintptr_t stack_top;

        if (
            objects->processes[processes[index].slot]
                .address_space_root != root_snapshots[index]
            || micros_user_address_space_validate(processes[index])
                != MICROS_USER_ADDRESS_SPACE_OK
            || micros_thread_inspect_execution_context(
                objects,
                threads[index],
                &observed,
                &stack_bottom,
                &stack_top
            ) != MICROS_KERNEL_OBJECT_OK
            || !bytes_equal(
                &observed,
                &context_snapshots[index],
                sizeof(observed)
            )
            || stack_bottom
                != (uintptr_t)&ipc_test_stacks[index][0]
            || stack_top
                != (uintptr_t)&ipc_test_stacks[index][
                    MICROS_THREAD_KERNEL_STACK_SIZE
                ]
            || !stack_pattern_matches(index)
        ) {
            return false;
        }
    }
    return true;
}

static void fill_message(
    struct micros_ipc_message *message,
    uint32_t type,
    uint8_t seed
)
{
    size_t index;

    clear_bytes(message, sizeof(*message));
    message->source = MICROS_ENDPOINT_ANY;
    message->type = type;
    message->reply_token = UINT64_C(0xa5a5a5a5a5a5a5a5);
    for (index = 0; index < sizeof(message->payload); ++index) {
        message->payload[index] =
            (uint8_t)(seed + (uint8_t)index);
    }
}

static struct micros_ipc_message canonical_message(
    const struct micros_ipc_message *source,
    micros_endpoint_t source_endpoint,
    uint64_t reply_token
)
{
    struct micros_ipc_message result = *source;

    result.source = source_endpoint;
    result.reply_token = reply_token;
    return result;
}

static uint64_t notification_event_mask(
    const struct micros_ipc_message *message
)
{
    uint64_t event_mask = 0;
    size_t index;

    for (index = 0; index < sizeof(event_mask); ++index) {
        event_mask |=
            (uint64_t)message->payload[index] << (index * 8);
    }
    return event_mask;
}

static bool notification_matches(
    const struct micros_ipc_message *message,
    micros_endpoint_t source,
    uint64_t event_mask
)
{
    size_t index;

    if (
        message->source != source
        || message->type != MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        || message->reply_token != 0
        || notification_event_mask(message) != event_mask
    ) {
        return false;
    }
    for (index = sizeof(event_mask); index < sizeof(message->payload); ++index) {
        if (message->payload[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool prepare_thread(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    struct micros_process_handle process,
    size_t stack_index,
    struct micros_thread_handle *thread
)
{
    struct micros_user_context context =
        context_pattern(UINT64_C(0x1000) + stack_index * UINT64_C(0x100));
    uintptr_t stack_bottom =
        (uintptr_t)&ipc_test_stacks[stack_index][0];
    uintptr_t stack_top =
        (uintptr_t)&ipc_test_stacks[stack_index][
            MICROS_THREAD_KERNEL_STACK_SIZE
        ];

    return (
        micros_thread_create(objects, process, thread)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_attach_execution_context(
            objects,
            *thread,
            stack_bottom,
            stack_top,
            &context
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_admit(
            objects,
            hart,
            *thread,
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            100,
            true
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(objects, *thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool hold_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
)
{
    const struct micros_thread *resolved =
        &objects->threads[thread.slot];

    return (
        resolved->runtime_flags == 0
        && resolved->ready_linked
        && !resolved->ipc_delivery_pending
        && micros_thread_scheduler_hold(objects, thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool consume_staged(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread,
    enum micros_ipc_error expected_result,
    const struct micros_ipc_message *expected_message
)
{
    struct micros_thread *resolved = &objects->threads[thread.slot];

    if (
        !resolved->ipc_delivery_pending
        || resolved->ipc_staged_result != expected_result
        || (
            expected_message != NULL
            && !bytes_equal(
                &resolved->ipc_inbound_message,
                expected_message,
                sizeof(*expected_message)
            )
        )
        || (
            expected_message == NULL
            && !bytes_equal(
                &resolved->ipc_inbound_message,
                &zero_message,
                sizeof(resolved->ipc_inbound_message)
            )
        )
    ) {
        return false;
    }
    resolved->ipc_receive_buffer = 0;
    resolved->ipc_delivery_pending = false;
    clear_bytes(
        &resolved->ipc_inbound_message,
        sizeof(resolved->ipc_inbound_message)
    );
    resolved->ipc_staged_result = MICROS_IPC_OK;
    return true;
}

static void snapshot_state(const struct micros_kernel_objects *objects)
{
    copy_bytes(&registry_snapshot, registry, sizeof(*registry));
    copy_bytes(&objects_snapshot, objects, sizeof(*objects));
}

static bool state_matches_snapshot(
    const struct micros_kernel_objects *objects
)
{
    return (
        bytes_equal(&registry_snapshot, registry, sizeof(*registry))
        && bytes_equal(&objects_snapshot, objects, sizeof(*objects))
    );
}

static bool ready_queue_matches(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    const struct micros_thread_handle *threads,
    size_t thread_count
)
{
    const struct micros_hart *resolved = &objects->harts[hart.slot];
    struct micros_thread_handle current =
        resolved->ready_head[MICROS_SCHEDULER_PRIORITY_DEFAULT_USER];
    size_t index;

    for (index = 0; index < thread_count; ++index) {
        if (!thread_handles_equal(current, threads[index])) {
            return false;
        }
        current = objects->threads[current.slot].ready_next;
    }
    return (
        thread_handle_is_zero(current)
        && (
            thread_count == 0
                ? thread_handle_is_zero(
                    resolved->ready_tail[
                        MICROS_SCHEDULER_PRIORITY_DEFAULT_USER
                    ]
                )
                : thread_handles_equal(
                    resolved->ready_tail[
                        MICROS_SCHEDULER_PRIORITY_DEFAULT_USER
                    ],
                    threads[thread_count - 1]
                )
        )
    );
}

static bool endpoint_records_are_clear(void)
{
    size_t index;
    size_t source;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_endpoint_record *record =
            &registry->endpoints[index];

        if (
            record->state != MICROS_ENDPOINT_STATE_FREE
            || record->owner.slot != 0
            || record->owner.generation != 0
            || record->value != 0
            || !thread_handle_is_zero(record->sender_head)
            || !thread_handle_is_zero(record->sender_tail)
            || !thread_handle_is_zero(record->receiver_head)
            || !thread_handle_is_zero(record->receiver_tail)
            || record->pending_notification_sources != 0
        ) {
            return false;
        }
        for (source = 0; source < MICROS_PROCESS_CAPACITY; ++source) {
            if (record->pending_events[source] != 0) {
                return false;
            }
        }
    }
    return true;
}

static bool cleanup_thread(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
)
{
    return (
        micros_thread_scheduler_remove(objects, thread)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_detach_execution_context(objects, thread)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_release(objects, thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

bool micros_ipc_runtime_run_self_test(void)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = IPC_TEST_CLIENT_PROFILE,
            .name = "IPC_CLIENT",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_CALL
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .call_targets = UINT32_C(1) << IPC_TEST_SERVER_PROFILE,
            .send_targets = UINT32_C(1) << IPC_TEST_SERVER_PROFILE,
            .notify_targets = UINT32_C(1) << IPC_TEST_SERVER_PROFILE,
        },
        {
            .id = IPC_TEST_SERVER_PROFILE,
            .name = "IPC_SERVER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .send_targets = UINT32_C(1) << IPC_TEST_CLIENT_PROFILE,
            .notify_targets = UINT32_C(1) << IPC_TEST_CLIENT_PROFILE,
        },
        {
            .id = IPC_TEST_PEER_PROFILE,
            .name = "IPC_PEER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_CALL
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .call_targets = UINT32_C(1) << IPC_TEST_SERVER_PROFILE,
            .send_targets = UINT32_C(1) << IPC_TEST_SERVER_PROFILE,
            .notify_targets = UINT32_C(1) << IPC_TEST_CLIENT_PROFILE,
        },
    };
    static const uint8_t profile_ids[IPC_TEST_PROCESS_COUNT] = {
        IPC_TEST_CLIENT_PROFILE,
        IPC_TEST_SERVER_PROFILE,
        IPC_TEST_PEER_PROFILE,
    };
    struct micros_kernel_objects *objects;
    const struct micros_frame_ownership *ledger;
    struct micros_hart *boot_hart;
    struct micros_hart_handle hart;
    struct micros_process_handle processes[IPC_TEST_PROCESS_COUNT];
    struct micros_thread_handle threads[IPC_TEST_PROCESS_COUNT];
    micros_endpoint_t endpoints[IPC_TEST_PROCESS_COUNT];
    struct micros_process_handle replacement_process;
    struct micros_thread_handle replacement_thread;
    micros_endpoint_t replacement_endpoint;
    struct micros_ipc_message expected;
    struct micros_ipc_message reply;
    struct micros_ipc_message *request;
    uint64_t reply_token;
    uint64_t close_token;
    size_t baseline_processes;
    size_t baseline_threads;
    size_t baseline_harts;
    uint64_t baseline_owned;
    uint64_t baseline_free;
    uintptr_t saved_status;
    bool passed = false;
    size_t index;
    uint64_t failure_stage = 1;

    saved_status = riscv_irq_save();
    objects = micros_kernel_object_runtime_test_registry();
    ledger = micros_frame_ownership_runtime_ledger();
    boot_hart = micros_kernel_object_runtime_boot_hart();
    hart = micros_kernel_object_runtime_boot_hart_handle();
    if (objects == NULL || boot_hart == NULL || ledger == NULL) {
        goto done;
    }
    baseline_processes = objects->live_process_count;
    baseline_threads = objects->live_thread_count;
    baseline_harts = objects->registered_hart_count;
    baseline_owned = ledger->owned_frame_count;
    baseline_free = ledger->allocator->free_frame_count;
    copy_bytes(&hart_snapshot, boot_hart, sizeof(hart_snapshot));
    if (
        baseline_processes != 0
        || baseline_threads != 0
        || baseline_harts != 1
        || !thread_handle_is_zero(boot_hart->current_thread)
        || micros_ipc_runtime_registry() != NULL
        || micros_ipc_runtime_initialize(
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || (
            registry =
                micros_ipc_runtime_authoritative_registry()
        ) == NULL
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        goto done;
    }
    for (index = 0; index < IPC_TEST_PROCESS_COUNT; ++index) {
        if (
            micros_process_create(objects, &processes[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_user_address_space_create(processes[index])
                != MICROS_USER_ADDRESS_SPACE_OK
            || micros_endpoint_reserve(
                registry,
                objects,
                processes[index],
                &endpoints[index]
            ) != MICROS_ENDPOINT_OK
            || micros_thread_create(
                objects,
                processes[index],
                &threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_install_profile(
                registry,
                objects,
                processes[index],
                profile_ids[index]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                registry,
                objects,
                endpoints[index]
            ) != MICROS_ENDPOINT_OK
        ) {
            goto done;
        }
    }
    for (index = 0; index < IPC_TEST_PROCESS_COUNT; ++index) {
        struct micros_user_context context =
            context_pattern(UINT64_C(0x2000) + index * UINT64_C(0x100));
        uintptr_t stack_bottom =
            (uintptr_t)&ipc_test_stacks[index][0];
        uintptr_t stack_top =
            (uintptr_t)&ipc_test_stacks[index][
                MICROS_THREAD_KERNEL_STACK_SIZE
            ];

        if (
            micros_thread_attach_execution_context(
                objects,
                threads[index],
                stack_bottom,
                stack_top,
                &context
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_admit(
                objects,
                hart,
                threads[index],
                MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
                100,
                true
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_hold(
                objects,
                threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            goto done;
        }
    }
    if (
        micros_endpoint_registry_validate_objects(registry, objects)
            != MICROS_ENDPOINT_OK
        || !ready_queue_matches(objects, hart, NULL, 0)
        || !snapshot_execution_state(objects, processes, threads)
    ) {
        goto done;
    }
    failure_stage = 2;

    fill_message(&send_messages[0], UINT32_C(0x1001), UINT8_C(0x10));
    if (
        micros_ipc_receive(
            registry,
            objects,
            threads[IPC_TEST_SERVER],
            endpoints[IPC_TEST_CLIENT],
            (uintptr_t)&receive_buffers[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
        || micros_ipc_send(
            registry,
            objects,
            threads[IPC_TEST_CLIENT],
            endpoints[IPC_TEST_SERVER],
            &send_messages[0]
        ) != MICROS_IPC_OK
    ) {
        goto done;
    }
    expected = canonical_message(
        &send_messages[0],
        endpoints[IPC_TEST_CLIENT],
        0
    );
    {
        const struct micros_thread_handle ready[] = {
            threads[IPC_TEST_CLIENT],
            threads[IPC_TEST_SERVER],
        };

        if (
            !ready_queue_matches(
                objects,
                hart,
                ready,
                sizeof(ready) / sizeof(ready[0])
            )
            || !consume_staged(
                objects,
                threads[IPC_TEST_SERVER],
                MICROS_IPC_OK,
                &expected
            )
            || !hold_thread(objects, threads[IPC_TEST_CLIENT])
            || !hold_thread(objects, threads[IPC_TEST_SERVER])
        ) {
            goto done;
        }
    }
    failure_stage = 3;

    fill_message(&send_messages[1], UINT32_C(0x1002), UINT8_C(0x20));
    if (
        micros_ipc_send(
            registry,
            objects,
            threads[IPC_TEST_PEER],
            endpoints[IPC_TEST_SERVER],
            &send_messages[1]
        ) != MICROS_IPC_OK
        || objects->threads[threads[IPC_TEST_PEER].slot].runtime_flags
            != MICROS_THREAD_RTS_IPC_SEND
        || !thread_handles_equal(
            registry->endpoints[processes[IPC_TEST_SERVER].slot]
                .sender_head,
            threads[IPC_TEST_PEER]
        )
        || micros_ipc_receive(
            registry,
            objects,
            threads[IPC_TEST_SERVER],
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&receive_buffers[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
    ) {
        goto done;
    }
    expected = canonical_message(
        &send_messages[1],
        endpoints[IPC_TEST_PEER],
        0
    );
    {
        const struct micros_thread_handle ready[] = {
            threads[IPC_TEST_SERVER],
            threads[IPC_TEST_PEER],
        };

        if (
            !ready_queue_matches(
                objects,
                hart,
                ready,
                sizeof(ready) / sizeof(ready[0])
            )
            || !consume_staged(
                objects,
                threads[IPC_TEST_SERVER],
                MICROS_IPC_OK,
                &expected
            )
            || !hold_thread(objects, threads[IPC_TEST_SERVER])
            || !hold_thread(objects, threads[IPC_TEST_PEER])
        ) {
            goto done;
        }
    }
    failure_stage = 4;

    fill_message(&send_messages[2], UINT32_C(0x2001), UINT8_C(0x30));
    if (
        micros_ipc_receive(
            registry,
            objects,
            threads[IPC_TEST_SERVER],
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&receive_buffers[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
        || micros_ipc_call(
            registry,
            objects,
            threads[IPC_TEST_CLIENT],
            endpoints[IPC_TEST_SERVER],
            &send_messages[2]
        ) != MICROS_IPC_OK
    ) {
        goto done;
    }
    request = &objects->threads[
        threads[IPC_TEST_SERVER].slot
    ].ipc_inbound_message;
    reply_token = request->reply_token;
    expected = canonical_message(
        &send_messages[2],
        endpoints[IPC_TEST_CLIENT],
        reply_token
    );
    if (
        reply_token == 0
        || !bytes_equal(request, &expected, sizeof(expected))
        || objects->threads[
            threads[IPC_TEST_CLIENT].slot
        ].runtime_flags != MICROS_THREAD_RTS_IPC_REPLY
        || !consume_staged(
            objects,
            threads[IPC_TEST_SERVER],
            MICROS_IPC_OK,
            &expected
        )
        || !hold_thread(objects, threads[IPC_TEST_SERVER])
    ) {
        goto done;
    }
    fill_message(&reply, UINT32_C(0x2002), UINT8_C(0x40));
    if (
        micros_ipc_reply(
            registry,
            objects,
            threads[IPC_TEST_SERVER],
            reply_token,
            &reply
        ) != MICROS_IPC_OK
    ) {
        goto done;
    }
    expected = canonical_message(
        &reply,
        endpoints[IPC_TEST_SERVER],
        0
    );
    if (
        !consume_staged(
            objects,
            threads[IPC_TEST_CLIENT],
            MICROS_IPC_OK,
            &expected
        )
        || !hold_thread(objects, threads[IPC_TEST_SERVER])
        || !hold_thread(objects, threads[IPC_TEST_CLIENT])
    ) {
        goto done;
    }
    snapshot_state(objects);
    if (
        micros_ipc_reply(
            registry,
            objects,
            threads[IPC_TEST_SERVER],
            reply_token,
            &reply
        ) != MICROS_IPC_ERROR_REPLY_TOKEN
        || !state_matches_snapshot(objects)
    ) {
        goto done;
    }
    failure_stage = 5;

    fill_message(&send_messages[3], UINT32_C(0x3001), UINT8_C(0x50));
    if (
        micros_ipc_receive(
            registry,
            objects,
            threads[IPC_TEST_SERVER],
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&receive_buffers[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
        || micros_ipc_call(
            registry,
            objects,
            threads[IPC_TEST_CLIENT],
            endpoints[IPC_TEST_SERVER],
            &send_messages[3]
        ) != MICROS_IPC_OK
    ) {
        goto done;
    }
    request = &objects->threads[
        threads[IPC_TEST_SERVER].slot
    ].ipc_inbound_message;
    reply_token = request->reply_token;
    expected = canonical_message(
        &send_messages[3],
        endpoints[IPC_TEST_CLIENT],
        reply_token
    );
    if (
        !consume_staged(
            objects,
            threads[IPC_TEST_SERVER],
            MICROS_IPC_OK,
            &expected
        )
        || !hold_thread(objects, threads[IPC_TEST_SERVER])
    ) {
        goto done;
    }
    failure_stage = 6;
    fill_message(&reply, UINT32_C(0x3002), UINT8_C(0x60));
    if (
        micros_ipc_reply_receive(
            registry,
            objects,
            threads[IPC_TEST_SERVER],
            reply_token,
            &reply,
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&receive_buffers[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
        || objects->threads[
            threads[IPC_TEST_SERVER].slot
        ].runtime_flags != MICROS_THREAD_RTS_IPC_RECEIVE
        || !thread_handles_equal(
            registry->endpoints[processes[IPC_TEST_SERVER].slot]
                .receiver_head,
            threads[IPC_TEST_SERVER]
        )
    ) {
        goto done;
    }
    fill_message(&send_messages[4], UINT32_C(0x3003), UINT8_C(0x70));
    if (
        micros_ipc_send(
            registry,
            objects,
            threads[IPC_TEST_PEER],
            endpoints[IPC_TEST_SERVER],
            &send_messages[4]
        ) != MICROS_IPC_OK
    ) {
        goto done;
    }
    expected = canonical_message(
        &reply,
        endpoints[IPC_TEST_SERVER],
        0
    );
    if (
        !consume_staged(
            objects,
            threads[IPC_TEST_CLIENT],
            MICROS_IPC_OK,
            &expected
        )
        || !hold_thread(objects, threads[IPC_TEST_CLIENT])
    ) {
        goto done;
    }
    failure_stage = 7;
    expected = canonical_message(
        &send_messages[4],
        endpoints[IPC_TEST_PEER],
        0
    );
    if (
        !consume_staged(
            objects,
            threads[IPC_TEST_SERVER],
            MICROS_IPC_OK,
            &expected
        )
        || !hold_thread(objects, threads[IPC_TEST_PEER])
        || !hold_thread(objects, threads[IPC_TEST_SERVER])
    ) {
        goto done;
    }

    fill_message(&send_messages[5], UINT32_C(0x4001), UINT8_C(0x80));
    if (
        micros_ipc_receive(
            registry,
            objects,
            threads[IPC_TEST_SERVER],
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&receive_buffers[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
        || micros_ipc_call(
            registry,
            objects,
            threads[IPC_TEST_CLIENT],
            endpoints[IPC_TEST_SERVER],
            &send_messages[5]
        ) != MICROS_IPC_OK
    ) {
        goto done;
    }
    request = &objects->threads[
        threads[IPC_TEST_SERVER].slot
    ].ipc_inbound_message;
    reply_token = request->reply_token;
    expected = canonical_message(
        &send_messages[5],
        endpoints[IPC_TEST_CLIENT],
        reply_token
    );
    if (
        !consume_staged(
            objects,
            threads[IPC_TEST_SERVER],
            MICROS_IPC_OK,
            &expected
        )
        || !hold_thread(objects, threads[IPC_TEST_SERVER])
        || micros_ipc_notify(
            registry,
            objects,
            threads[IPC_TEST_PEER],
            endpoints[IPC_TEST_CLIENT],
            UINT64_C(0x01)
        ) != MICROS_IPC_OK
        || !hold_thread(objects, threads[IPC_TEST_PEER])
        || micros_ipc_notify(
            registry,
            objects,
            threads[IPC_TEST_PEER],
            endpoints[IPC_TEST_CLIENT],
            UINT64_C(0x04)
        ) != MICROS_IPC_OK
        || !hold_thread(objects, threads[IPC_TEST_PEER])
        || objects->threads[
            threads[IPC_TEST_CLIENT].slot
        ].runtime_flags != MICROS_THREAD_RTS_IPC_REPLY
        || objects->threads[
            threads[IPC_TEST_CLIENT].slot
        ].ipc_delivery_pending
        || registry->endpoints[processes[IPC_TEST_CLIENT].slot]
            .pending_events[processes[IPC_TEST_PEER].slot]
            != UINT64_C(0x05)
    ) {
        goto done;
    }
    fill_message(&reply, UINT32_C(0x4002), UINT8_C(0x90));
    if (
        micros_ipc_reply(
            registry,
            objects,
            threads[IPC_TEST_SERVER],
            reply_token,
            &reply
        ) != MICROS_IPC_OK
    ) {
        goto done;
    }
    expected = canonical_message(
        &reply,
        endpoints[IPC_TEST_SERVER],
        0
    );
    if (
        !consume_staged(
            objects,
            threads[IPC_TEST_CLIENT],
            MICROS_IPC_OK,
            &expected
        )
        || !hold_thread(objects, threads[IPC_TEST_SERVER])
        || !hold_thread(objects, threads[IPC_TEST_CLIENT])
        || micros_ipc_receive(
            registry,
            objects,
            threads[IPC_TEST_CLIENT],
            MICROS_ENDPOINT_ANY,
            (uintptr_t)&receive_buffers[IPC_TEST_CLIENT]
        ) != MICROS_IPC_OK
    ) {
        goto done;
    }
    if (
        !notification_matches(
            &objects->threads[
                threads[IPC_TEST_CLIENT].slot
            ].ipc_inbound_message,
            endpoints[IPC_TEST_PEER],
            UINT64_C(0x05)
        )
        || !consume_staged(
            objects,
            threads[IPC_TEST_CLIENT],
            MICROS_IPC_OK,
            &objects->threads[
                threads[IPC_TEST_CLIENT].slot
            ].ipc_inbound_message
        )
        || !hold_thread(objects, threads[IPC_TEST_CLIENT])
    ) {
        goto done;
    }
    failure_stage = 8;

    fill_message(&send_messages[6], UINT32_C(0x5001), UINT8_C(0xa0));
    if (
        micros_ipc_send(
            registry,
            objects,
            threads[IPC_TEST_CLIENT],
            endpoints[IPC_TEST_SERVER],
            &send_messages[6]
        ) != MICROS_IPC_OK
    ) {
        goto done;
    }
    snapshot_state(objects);
    fill_message(&send_messages[7], UINT32_C(0x5002), UINT8_C(0xb0));
    if (
        micros_ipc_send(
            registry,
            objects,
            threads[IPC_TEST_SERVER],
            endpoints[IPC_TEST_CLIENT],
            &send_messages[7]
        ) != MICROS_IPC_ERROR_DEADLOCK
        || !state_matches_snapshot(objects)
        || micros_ipc_receive(
            registry,
            objects,
            threads[IPC_TEST_SERVER],
            endpoints[IPC_TEST_CLIENT],
            (uintptr_t)&receive_buffers[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
    ) {
        goto done;
    }
    expected = canonical_message(
        &send_messages[6],
        endpoints[IPC_TEST_CLIENT],
        0
    );
    if (
        !consume_staged(
            objects,
            threads[IPC_TEST_SERVER],
            MICROS_IPC_OK,
            &expected
        )
        || !hold_thread(objects, threads[IPC_TEST_SERVER])
        || !hold_thread(objects, threads[IPC_TEST_CLIENT])
    ) {
        goto done;
    }

    fill_message(&send_messages[0], UINT32_C(0x6001), UINT8_C(0xc0));
    if (
        micros_ipc_call(
            registry,
            objects,
            threads[IPC_TEST_CLIENT],
            endpoints[IPC_TEST_SERVER],
            &send_messages[0]
        ) != MICROS_IPC_OK
    ) {
        goto done;
    }
    close_token = objects->threads[
        threads[IPC_TEST_CLIENT].slot
    ].ipc_reply_token;
    if (
        close_token == 0
        || micros_ipc_receive(
            registry,
            objects,
            threads[IPC_TEST_PEER],
            endpoints[IPC_TEST_SERVER],
            (uintptr_t)&receive_buffers[IPC_TEST_PEER]
        ) != MICROS_IPC_OK
        || micros_ipc_notify(
            registry,
            objects,
            threads[IPC_TEST_SERVER],
            endpoints[IPC_TEST_CLIENT],
            UINT64_C(0x20)
        ) != MICROS_IPC_OK
        || !hold_thread(objects, threads[IPC_TEST_SERVER])
        || micros_ipc_endpoint_close(
            registry,
            objects,
            endpoints[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
        || objects->threads[
            threads[IPC_TEST_CLIENT].slot
        ].ipc_reply_token != 0
        || registry->endpoints[processes[IPC_TEST_CLIENT].slot]
            .pending_events[processes[IPC_TEST_SERVER].slot] != 0
        || !consume_staged(
            objects,
            threads[IPC_TEST_CLIENT],
            MICROS_IPC_ERROR_DEAD_ENDPOINT,
            NULL
        )
        || !consume_staged(
            objects,
            threads[IPC_TEST_PEER],
            MICROS_IPC_ERROR_DEAD_ENDPOINT,
            NULL
        )
        || !hold_thread(objects, threads[IPC_TEST_CLIENT])
        || !hold_thread(objects, threads[IPC_TEST_PEER])
    ) {
        goto done;
    }
    failure_stage = 9;

    if (
        !execution_state_matches(objects, processes, threads)
        || !cleanup_thread(objects, threads[IPC_TEST_SERVER])
        || micros_user_address_space_destroy(
            processes[IPC_TEST_SERVER]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_process_release(
            objects,
            processes[IPC_TEST_SERVER]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(objects, &replacement_process)
            != MICROS_KERNEL_OBJECT_OK
        || replacement_process.slot
            != processes[IPC_TEST_SERVER].slot
        || replacement_process.generation
            != processes[IPC_TEST_SERVER].generation + 1
        || micros_user_address_space_create(replacement_process)
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_endpoint_reserve(
            registry,
            objects,
            replacement_process,
            &replacement_endpoint
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_install_profile(
            registry,
            objects,
            replacement_process,
            IPC_TEST_SERVER_PROFILE
        ) != MICROS_ENDPOINT_OK
        || micros_endpoint_activate(
            registry,
            objects,
            replacement_endpoint
        ) != MICROS_ENDPOINT_OK
        || !prepare_thread(
            objects,
            hart,
            replacement_process,
            IPC_TEST_SERVER,
            &replacement_thread
        )
        || replacement_thread.slot != threads[IPC_TEST_SERVER].slot
        || replacement_thread.generation
            != threads[IPC_TEST_SERVER].generation + 1
    ) {
        goto done;
    }
    threads[IPC_TEST_SERVER] = replacement_thread;
    processes[IPC_TEST_SERVER] = replacement_process;
    if (!snapshot_execution_state(objects, processes, threads)) {
        goto done;
    }
    snapshot_state(objects);
    if (
        micros_ipc_send(
            registry,
            objects,
            threads[IPC_TEST_CLIENT],
            endpoints[IPC_TEST_SERVER],
            &send_messages[0]
        ) != MICROS_IPC_ERROR_DEAD_ENDPOINT
        || !state_matches_snapshot(objects)
    ) {
        goto done;
    }
    failure_stage = 10;
    endpoints[IPC_TEST_SERVER] = replacement_endpoint;
    snapshot_state(objects);
    if (
        micros_ipc_send(
            registry,
            objects,
            threads[IPC_TEST_CLIENT],
            endpoints[IPC_TEST_PEER],
            &send_messages[0]
        ) != MICROS_IPC_ERROR_UNAUTHORIZED
        || !state_matches_snapshot(objects)
    ) {
        goto done;
    }

    if (
        !execution_state_matches(objects, processes, threads)
        || micros_ipc_endpoint_close(
            registry,
            objects,
            endpoints[IPC_TEST_CLIENT]
        ) != MICROS_IPC_OK
        || micros_ipc_endpoint_close(
            registry,
            objects,
            endpoints[IPC_TEST_PEER]
        ) != MICROS_IPC_OK
        || micros_ipc_endpoint_close(
            registry,
            objects,
            endpoints[IPC_TEST_SERVER]
        ) != MICROS_IPC_OK
    ) {
        goto done;
    }
    for (index = 0; index < IPC_TEST_PROCESS_COUNT; ++index) {
        if (
            !cleanup_thread(objects, threads[index])
            || micros_user_address_space_destroy(processes[index])
                != MICROS_USER_ADDRESS_SPACE_OK
            || micros_process_release(objects, processes[index])
                != MICROS_KERNEL_OBJECT_OK
        ) {
            goto done;
        }
    }
    failure_stage = 11;
    if (
        objects->live_process_count != baseline_processes
        || objects->live_thread_count != baseline_threads
        || objects->registered_hart_count != baseline_harts
        || ledger->owned_frame_count != baseline_owned
        || ledger->allocator->free_frame_count != baseline_free
    ) {
        failure_stage = UINT64_C(0x1101);
        goto done;
    }
    if (registry->last_reply_token < close_token) {
        failure_stage = UINT64_C(0x1102);
        goto done;
    }
    if (!endpoint_records_are_clear()) {
        failure_stage = UINT64_C(0x1103);
        goto done;
    }
    if (!bytes_equal(boot_hart, &hart_snapshot, sizeof(*boot_hart))) {
        failure_stage = UINT64_C(0x1104);
        goto done;
    }
    if (
        micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        failure_stage = UINT64_C(0x1108);
        goto done;
    }
    if (
        micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        failure_stage = UINT64_C(0x1105);
        goto done;
    }
    if (
        micros_scheduler_core_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        failure_stage = UINT64_C(0x1106);
        goto done;
    }
    if (
        micros_endpoint_registry_validate_objects(registry, objects)
            != MICROS_ENDPOINT_OK
    ) {
        failure_stage = UINT64_C(0x1107);
        goto done;
    }
    passed = true;

done:
    if (!passed) {
        uart_write("MICROS_TEST_FAILURE ipc-stage=");
        uart_write_hex64(failure_stage);
        uart_write("\n");
        uart_flush();
    }
    riscv_irq_restore(saved_status);
    return passed;
}
