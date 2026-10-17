#include "kernel/tests/ipc/ipc_syscall_test_internal.h"

static bool transition_client_send_one(
    struct ipc_syscall_test_context *fixture,
    struct micros_kernel_objects *objects,
    struct micros_user_context *context,
    size_t thread_index
)
{
    if (
        micros_thread_install_policy(
            objects,
            fixture->threads[IPC_SYSCALL_SERVER],
            IPC_SYSCALL_TEST_SERVER_PRIORITY,
            UINT64_MAX
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    ipc_syscall_test_arm_call(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_CLIENT_CALL_ONE,
        fixture->endpoints[IPC_SYSCALL_SERVER],
        UINT32_C(0x1101),
        UINT8_C(0x20)
    );
    return true;
}

static bool transition_server_receive_send_one(
    struct ipc_syscall_test_context *fixture,
    struct micros_user_context *context,
    size_t thread_index,
    enum ipc_syscall_command command
)
{
    if (
        !ipc_syscall_test_message_matches(
            fixture,
            thread_index,
            IPC_SYSCALL_TEST_INBOUND_ADDRESS,
            fixture->endpoints[IPC_SYSCALL_CLIENT],
            UINT32_C(0x1001),
            UINT8_C(0x10),
            IPC_TOKEN_ZERO,
            NULL
        )
    ) {
        (void)ipc_syscall_test_test_mismatch(
            UINT64_C(0x51),
            thread_index,
            command
        );
        return false;
    }
    ipc_syscall_test_arm_receive(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_SERVER_WAIT_CALL_ONE,
        MICROS_ENDPOINT_ANY
    );
    return true;
}

static bool transition_server_wait_call_one(
    struct ipc_syscall_test_context *fixture,
    struct micros_user_context *context,
    size_t thread_index
)
{
    if (
        !ipc_syscall_test_message_matches(
            fixture,
            thread_index,
            IPC_SYSCALL_TEST_INBOUND_ADDRESS,
            fixture->endpoints[IPC_SYSCALL_CLIENT],
            UINT32_C(0x1101),
            UINT8_C(0x20),
            IPC_TOKEN_NONZERO,
            &fixture->token_one
        )
    ) {
        return false;
    }
    ipc_syscall_test_arm_reply(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_SERVER_REPLY_ONE,
        fixture->token_one,
        UINT32_C(0x1201),
        UINT8_C(0x30)
    );
    return true;
}

static bool transition_server_reply_one(
    struct ipc_syscall_test_context *fixture,
    struct micros_user_context *context,
    size_t thread_index
)
{
    ipc_syscall_test_arm_receive(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_SERVER_WAIT_CALL_TWO,
        MICROS_ENDPOINT_ANY
    );
    return true;
}

static bool transition_client_call_one(
    struct ipc_syscall_test_context *fixture,
    struct micros_user_context *context,
    size_t thread_index
)
{
    if (
        !ipc_syscall_test_message_matches(
            fixture,
            thread_index,
            IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
            fixture->endpoints[IPC_SYSCALL_SERVER],
            UINT32_C(0x1201),
            UINT8_C(0x30),
            IPC_TOKEN_ZERO,
            NULL
        )
    ) {
        return false;
    }
    ipc_syscall_test_arm_call(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_CLIENT_CALL_TWO,
        fixture->endpoints[IPC_SYSCALL_SERVER],
        UINT32_C(0x1301),
        UINT8_C(0x40)
    );
    return true;
}

static bool transition_server_wait_call_two(
    struct ipc_syscall_test_context *fixture,
    struct micros_user_context *context,
    size_t thread_index
)
{
    if (
        !ipc_syscall_test_message_matches(
            fixture,
            thread_index,
            IPC_SYSCALL_TEST_INBOUND_ADDRESS,
            fixture->endpoints[IPC_SYSCALL_CLIENT],
            UINT32_C(0x1301),
            UINT8_C(0x40),
            IPC_TOKEN_NONZERO,
            &fixture->token_two
        )
    ) {
        return false;
    }
    ipc_syscall_test_arm_reply_receive(
        fixture,
        thread_index,
        context,
        fixture->token_two,
        UINT32_C(0x1401),
        UINT8_C(0x50)
    );
    return true;
}

static bool transition_client_call_two(
    struct ipc_syscall_test_context *fixture,
    struct micros_kernel_objects *objects,
    struct micros_user_context *context,
    size_t thread_index
)
{
    if (
        !ipc_syscall_test_message_matches(
            fixture,
            thread_index,
            IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
            fixture->endpoints[IPC_SYSCALL_SERVER],
            UINT32_C(0x1401),
            UINT8_C(0x50),
            IPC_TOKEN_ZERO,
            NULL
        )
    ) {
        struct micros_ipc_message observed;

        ipc_syscall_test_copy_from_user_buffer(
            fixture,
            thread_index,
            IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
            &observed
        );

        uart_write("MICROS_TEST_FAILURE call-two source=");
        uart_write_hex64(observed.source);
        uart_write(" type=");
        uart_write_hex64(observed.type);
        uart_write(" token=");
        uart_write_hex64(observed.reply_token);
        uart_write("\n");
        uart_flush();
        return false;
    }
    ipc_syscall_test_arm_receive(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_CLIENT_WAIT_SERVER_CALL,
        MICROS_ENDPOINT_ANY
    );
    {
        enum micros_kernel_object_error admit_error =
            micros_thread_scheduler_admit(
                objects,
                micros_kernel_object_runtime_boot_hart_handle(),
                fixture->threads[IPC_SYSCALL_PEER],
                MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
                UINT64_MAX,
                true
            );

        if (admit_error != MICROS_KERNEL_OBJECT_OK) {
            const struct micros_thread *peer =
                &objects->threads[
                    fixture->threads[IPC_SYSCALL_PEER].slot
                ];

            uart_write("MICROS_TEST_FAILURE peer-admit=");
            uart_write_hex64((uint64_t)admit_error);
            uart_write(" assigned=");
            uart_write_hex64(peer->scheduler_assigned);
            uart_write(" context=");
            uart_write_hex64(peer->context_attached);
            uart_write(" flags=");
            uart_write_hex64(peer->runtime_flags);
            uart_write(" pending=");
            uart_write_hex64(peer->ipc_delivery_pending);
            uart_write(" kind=");
            uart_write_hex64(peer->ipc_queue_kind);
            uart_write(" result=");
            uart_write_hex64(peer->ipc_staged_result);
            uart_write(" buffer=");
            uart_write_hex64(peer->ipc_receive_buffer);
            uart_write(" token=");
            uart_write_hex64(peer->ipc_reply_token);
            uart_write(" ready=");
            uart_write_hex64(peer->ready_linked);
            uart_write(" next=");
            uart_write_hex64(peer->ready_next.slot);
            uart_write(" validate=");
            uart_write_hex64(
                (uint64_t)micros_scheduler_core_validate(
                    objects
                )
            );
            uart_write("\n");
            uart_flush();
            return false;
        }
    }
    return true;
}

static bool transition_server_reply_receive_two(
    struct ipc_syscall_test_context *fixture,
    struct micros_kernel_objects *objects,
    struct micros_user_context *context,
    size_t thread_index
)
{
    if (
        !ipc_syscall_test_message_matches(
            fixture,
            thread_index,
            IPC_SYSCALL_TEST_INBOUND_ADDRESS,
            fixture->endpoints[IPC_SYSCALL_PEER],
            UINT32_C(0x1501),
            UINT8_C(0x60),
            IPC_TOKEN_ZERO,
            NULL
        )
    ) {
        return false;
    }
    if (
        micros_thread_install_policy(
            objects,
            fixture->threads[IPC_SYSCALL_PEER],
            IPC_SYSCALL_TEST_PEER_PRIORITY,
            UINT64_MAX
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    ipc_syscall_test_arm_call(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_SERVER_CALL_CLIENT,
        fixture->endpoints[IPC_SYSCALL_CLIENT],
        UINT32_C(0x1601),
        UINT8_C(0x70)
    );
    return true;
}

static bool transition_peer_send(
    struct ipc_syscall_test_context *fixture,
    struct micros_user_context *context,
    size_t thread_index
)
{
    ipc_syscall_test_arm_notify(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_PEER_NOTIFY_ONE,
        fixture->endpoints[IPC_SYSCALL_SERVER],
        UINT64_C(1)
    );
    return true;
}

static bool transition_peer_notify_one(
    struct ipc_syscall_test_context *fixture,
    struct micros_user_context *context,
    size_t thread_index
)
{
    ipc_syscall_test_arm_notify(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_PEER_NOTIFY_TWO,
        fixture->endpoints[IPC_SYSCALL_SERVER],
        UINT64_C(4)
    );
    return true;
}

static bool transition_peer_notify_two(
    struct ipc_syscall_test_context *fixture,
    struct micros_user_context *context,
    size_t thread_index
)
{
    ipc_syscall_test_arm_receive(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_PEER_BLOCK,
        MICROS_ENDPOINT_ANY
    );
    return true;
}

static bool transition_client_wait_server_call(
    struct ipc_syscall_test_context *fixture,
    struct micros_user_context *context,
    size_t thread_index
)
{
    if (
        !ipc_syscall_test_message_matches(
            fixture,
            thread_index,
            IPC_SYSCALL_TEST_INBOUND_ADDRESS,
            fixture->endpoints[IPC_SYSCALL_SERVER],
            UINT32_C(0x1601),
            UINT8_C(0x70),
            IPC_TOKEN_NONZERO,
            &fixture->token_three
        )
    ) {
        return false;
    }
    ipc_syscall_test_arm_reply(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_CLIENT_REPLY_THREE,
        fixture->token_three,
        UINT32_C(0x1701),
        UINT8_C(0x80)
    );
    return true;
}

static bool transition_server_call_client(
    struct ipc_syscall_test_context *fixture,
    struct micros_user_context *context,
    size_t thread_index
)
{
    if (
        !ipc_syscall_test_message_matches(
            fixture,
            thread_index,
            IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
            fixture->endpoints[IPC_SYSCALL_CLIENT],
            UINT32_C(0x1701),
            UINT8_C(0x80),
            IPC_TOKEN_ZERO,
            NULL
        )
    ) {
        return false;
    }
    ipc_syscall_test_arm_receive(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_SERVER_RECEIVE_NOTIFY,
        MICROS_ENDPOINT_ANY
    );
    return true;
}

static bool transition_server_receive_notify(
    struct ipc_syscall_test_context *fixture,
    struct micros_kernel_objects *objects,
    struct micros_user_context *context,
    size_t thread_index
)
{
    if (
        !ipc_syscall_test_notification_matches(
            fixture,
            thread_index,
            fixture->endpoints[IPC_SYSCALL_PEER],
            UINT64_C(5)
        )
    ) {
        return false;
    }
    if (
        micros_thread_install_policy(
            objects,
            fixture->threads[IPC_SYSCALL_PEER],
            IPC_SYSCALL_TEST_BACKGROUND_PRIORITY,
            UINT64_MAX
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    ipc_syscall_test_arm_send(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_SERVER_WAKE_PEER,
        fixture->endpoints[IPC_SYSCALL_PEER],
        UINT32_C(0x1801),
        UINT8_C(0x90)
    );
    return true;
}

static bool transition_server_wake_peer(
    struct ipc_syscall_test_context *fixture,
    struct micros_kernel_objects *objects,
    struct micros_user_context *context,
    size_t thread_index
)
{
    fixture->finish_ready = true;
    objects->threads[
        fixture->threads[IPC_SYSCALL_PEER].slot
    ].user_context.sepc =
        ipc_syscall_test_user_address_of(
            micros_ipc_syscall_test_payload_spin
        );
    ipc_syscall_test_arm_receive(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_SERVER_BLOCK_FINAL,
        MICROS_ENDPOINT_ANY
    );
    return true;
}

static bool transition_client_reply_three(
    struct ipc_syscall_test_context *fixture,
    struct micros_user_context *context,
    size_t thread_index,
    enum ipc_syscall_command command
)
{
    if (!fixture->finish_ready) {
        (void)ipc_syscall_test_test_mismatch(
            UINT64_C(0xf1),
            thread_index,
            command
        );
        return false;
    }
    ipc_syscall_test_arm_notify(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_CLIENT_ERROR_UNAUTHORIZED,
        fixture->endpoints[IPC_SYSCALL_SERVER],
        UINT64_C(8)
    );
    fixture->scripts[thread_index].expected_result =
        (uint64_t)(int64_t)MICROS_IPC_ABI_UNAUTHORIZED;
    return true;
}

static bool transition_client_error_unauthorized(
    struct ipc_syscall_test_context *fixture,
    struct micros_user_context *context,
    size_t thread_index
)
{
    ipc_syscall_test_arm_command(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_CLIENT_ERROR_ARGUMENT,
        MICROS_IPC_ABI_SEND,
        UINT64_C(1) << 32,
        IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
        0,
        0
    );
    fixture->scripts[thread_index].expected_result =
        (uint64_t)(int64_t)MICROS_IPC_ABI_ARGUMENT;
    return true;
}

static bool transition_client_error_argument(
    struct ipc_syscall_test_context *fixture,
    struct micros_user_context *context,
    size_t thread_index
)
{
    ipc_syscall_test_arm_command(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_CLIENT_ERROR_FAULT,
        MICROS_IPC_ABI_SEND,
        fixture->endpoints[IPC_SYSCALL_SERVER],
        IPC_SYSCALL_TEST_STACK_VIRTUAL_ADDRESS
            + MICROS_SV39_PAGE_SIZE,
        0,
        0
    );
    fixture->scripts[thread_index].expected_result =
        (uint64_t)(int64_t)MICROS_IPC_ABI_MESSAGE_FAULT;
    return true;
}

static bool transition_client_error_fault(
    struct ipc_syscall_test_context *fixture,
    struct micros_hart *hart,
    struct micros_kernel_objects *objects,
    struct micros_trap_frame *frame,
    size_t thread_index,
    enum ipc_syscall_command command
)
{
    if (
        fixture->scenario_phase
            != sizeof(ipc_syscall_test_scenario_commands)
                / sizeof(ipc_syscall_test_scenario_commands[0])
    ) {
        return ipc_syscall_test_test_mismatch(
            UINT64_C(0xf30),
            thread_index,
            command
        );
    }
    if (
        micros_ipc_stage_no_message_completion(
            fixture->registry,
            objects,
            fixture->threads[IPC_SYSCALL_CLIENT],
            MICROS_IPC_ERROR_DEAD_ENDPOINT
        ) != MICROS_IPC_OK
    ) {
        return ipc_syscall_test_test_mismatch(
            UINT64_C(0xf31),
            thread_index,
            command
        );
    }
    ipc_syscall_test_copy_bytes(
        &fixture->timer_expected_context,
        frame,
        sizeof(fixture->timer_expected_context)
    );
    fixture->timer_expected_context.a0 =
        (uint64_t)(int64_t)MICROS_IPC_ABI_DEAD_ENDPOINT;
    fixture->timer_expected_context.sepc =
        ipc_syscall_test_user_address_of(
            micros_ipc_syscall_test_payload_spin
        );
    frame->sepc = fixture->timer_expected_context.sepc;
    if (
        !micros_timer_stop(hart)
        || !micros_timer_start(
            hart,
            IPC_SYSCALL_TEST_DEFERRED_TIMER_INTERVAL
        )
    ) {
        return ipc_syscall_test_test_mismatch(
            UINT64_C(0xf32),
            thread_index,
            command
        );
    }
    fixture->timer_completion_started = true;
    return true;
}

bool ipc_syscall_test_transition_after_return(
    struct ipc_syscall_test_context *fixture,
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    size_t thread_index,
    enum ipc_syscall_command command
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_user_context *context =
        (struct micros_user_context *)frame;

    switch (command) {
    case IPC_COMMAND_CLIENT_SEND_ONE:
        return transition_client_send_one(fixture, objects, context, thread_index);
    case IPC_COMMAND_SERVER_RECEIVE_SEND_ONE:
        return transition_server_receive_send_one(fixture, context, thread_index, command);
    case IPC_COMMAND_SERVER_WAIT_CALL_ONE:
        return transition_server_wait_call_one(fixture, context, thread_index);
    case IPC_COMMAND_SERVER_REPLY_ONE:
        return transition_server_reply_one(fixture, context, thread_index);
    case IPC_COMMAND_CLIENT_CALL_ONE:
        return transition_client_call_one(fixture, context, thread_index);
    case IPC_COMMAND_SERVER_WAIT_CALL_TWO:
        return transition_server_wait_call_two(fixture, context, thread_index);
    case IPC_COMMAND_CLIENT_CALL_TWO:
        return transition_client_call_two(fixture, objects, context, thread_index);
    case IPC_COMMAND_SERVER_REPLY_RECEIVE_TWO:
        return transition_server_reply_receive_two(fixture, objects, context, thread_index);
    case IPC_COMMAND_PEER_SEND:
        return transition_peer_send(fixture, context, thread_index);
    case IPC_COMMAND_PEER_NOTIFY_ONE:
        return transition_peer_notify_one(fixture, context, thread_index);
    case IPC_COMMAND_PEER_NOTIFY_TWO:
        return transition_peer_notify_two(fixture, context, thread_index);
    case IPC_COMMAND_CLIENT_WAIT_SERVER_CALL:
        return transition_client_wait_server_call(fixture, context, thread_index);
    case IPC_COMMAND_SERVER_CALL_CLIENT:
        return transition_server_call_client(fixture, context, thread_index);
    case IPC_COMMAND_SERVER_RECEIVE_NOTIFY:
        return transition_server_receive_notify(fixture, objects, context, thread_index);
    case IPC_COMMAND_SERVER_WAKE_PEER:
        return transition_server_wake_peer(fixture, objects, context, thread_index);
    case IPC_COMMAND_CLIENT_REPLY_THREE:
        return transition_client_reply_three(fixture, context, thread_index, command);
    case IPC_COMMAND_CLIENT_ERROR_UNAUTHORIZED:
        return transition_client_error_unauthorized(fixture, context, thread_index);
    case IPC_COMMAND_CLIENT_ERROR_ARGUMENT:
        return transition_client_error_argument(fixture, context, thread_index);
    case IPC_COMMAND_CLIENT_ERROR_FAULT:
        return transition_client_error_fault(fixture, hart, objects, frame, thread_index, command);
    case IPC_COMMAND_NONE:
    case IPC_COMMAND_PEER_BLOCK:
    case IPC_COMMAND_SERVER_BLOCK_FINAL:
        return false;
    }
    return false;
}
