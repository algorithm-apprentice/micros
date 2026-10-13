#include "ipc_syscall_test_internal.h"

void ipc_syscall_test_arm_command(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    uint64_t operation,
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3
)
{
    fixture->scripts[thread_index].armed = true;
    fixture->scripts[thread_index].awaiting = false;
    fixture->scripts[thread_index].command = command;
    fixture->scripts[thread_index].expected_result = MICROS_IPC_ABI_OK;
    context->a0 = a0;
    context->a1 = a1;
    context->a2 = a2;
    context->a3 = a3;
    context->a7 = operation;
    context->sepc =
        ipc_syscall_test_user_address_of(micros_ipc_syscall_test_payload_ecall);
}

void ipc_syscall_test_arm_send(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    micros_endpoint_t destination,
    uint32_t type,
    uint8_t seed
)
{
    ipc_syscall_test_fill_message(
        fixture,
        thread_index,
        IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
        type,
        seed
    );
    ipc_syscall_test_arm_command(
        fixture,
        thread_index,
        context,
        command,
        MICROS_IPC_ABI_SEND,
        destination,
        IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
        0,
        0
    );
}

void ipc_syscall_test_arm_receive(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    micros_endpoint_t source
)
{
    {
        struct micros_ipc_message zero_message;

        ipc_syscall_test_clear_bytes(&zero_message, sizeof(zero_message));
        ipc_syscall_test_copy_to_user_buffer(
            fixture,
            thread_index,
            IPC_SYSCALL_TEST_INBOUND_ADDRESS,
            &zero_message
        );
    }
    ipc_syscall_test_arm_command(
        fixture,
        thread_index,
        context,
        command,
        MICROS_IPC_ABI_RECEIVE,
        source,
        IPC_SYSCALL_TEST_INBOUND_ADDRESS,
        0,
        0
    );
}

void ipc_syscall_test_arm_call(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    micros_endpoint_t destination,
    uint32_t type,
    uint8_t seed
)
{
    ipc_syscall_test_fill_message(
        fixture,
        thread_index,
        IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
        type,
        seed
    );
    ipc_syscall_test_arm_command(
        fixture,
        thread_index,
        context,
        command,
        MICROS_IPC_ABI_CALL,
        destination,
        IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
        0,
        0
    );
}

void ipc_syscall_test_arm_reply(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    uint64_t token,
    uint32_t type,
    uint8_t seed
)
{
    ipc_syscall_test_fill_message(
        fixture,
        thread_index,
        IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
        type,
        seed
    );
    ipc_syscall_test_arm_command(
        fixture,
        thread_index,
        context,
        command,
        MICROS_IPC_ABI_REPLY,
        token,
        IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
        0,
        0
    );
}

void ipc_syscall_test_arm_reply_receive(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    struct micros_user_context *context,
    uint64_t token,
    uint32_t type,
    uint8_t seed
)
{
    ipc_syscall_test_fill_message(
        fixture,
        thread_index,
        IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
        type,
        seed
    );
    {
        struct micros_ipc_message zero_message;

        ipc_syscall_test_clear_bytes(&zero_message, sizeof(zero_message));
        ipc_syscall_test_copy_to_user_buffer(
            fixture,
            thread_index,
            IPC_SYSCALL_TEST_INBOUND_ADDRESS,
            &zero_message
        );
    }
    ipc_syscall_test_arm_command(
        fixture,
        thread_index,
        context,
        IPC_COMMAND_SERVER_REPLY_RECEIVE_TWO,
        MICROS_IPC_ABI_REPLY_RECEIVE,
        token,
        IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
        MICROS_ENDPOINT_ANY,
        IPC_SYSCALL_TEST_INBOUND_ADDRESS
    );
}

void ipc_syscall_test_arm_notify(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    micros_endpoint_t destination,
    uint64_t mask
)
{
    ipc_syscall_test_arm_command(
        fixture,
        thread_index,
        context,
        command,
        MICROS_IPC_ABI_NOTIFY,
        destination,
        mask,
        0,
        0
    );
}
