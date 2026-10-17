#include "kernel/tests/ipc/ipc_syscall_test_internal.h"

uintptr_t micros_ipc_syscall_test_saved_sp;
uintptr_t micros_ipc_syscall_test_saved_gp;
uintptr_t micros_ipc_syscall_test_saved_tp;
static struct ipc_syscall_test_context test_context;

const size_t ipc_syscall_test_scenario_threads[21] = {
    IPC_SYSCALL_CLIENT,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_CLIENT,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_CLIENT,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_CLIENT,
    IPC_SYSCALL_PEER,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_PEER,
    IPC_SYSCALL_PEER,
    IPC_SYSCALL_PEER,
    IPC_SYSCALL_CLIENT,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_CLIENT,
    IPC_SYSCALL_CLIENT,
    IPC_SYSCALL_CLIENT,
};

const enum ipc_syscall_command ipc_syscall_test_scenario_commands[21] = {
    IPC_COMMAND_CLIENT_SEND_ONE,
    IPC_COMMAND_SERVER_RECEIVE_SEND_ONE,
    IPC_COMMAND_SERVER_WAIT_CALL_ONE,
    IPC_COMMAND_CLIENT_CALL_ONE,
    IPC_COMMAND_SERVER_REPLY_ONE,
    IPC_COMMAND_SERVER_WAIT_CALL_TWO,
    IPC_COMMAND_CLIENT_CALL_TWO,
    IPC_COMMAND_SERVER_REPLY_RECEIVE_TWO,
    IPC_COMMAND_CLIENT_WAIT_SERVER_CALL,
    IPC_COMMAND_PEER_SEND,
    IPC_COMMAND_SERVER_CALL_CLIENT,
    IPC_COMMAND_PEER_NOTIFY_ONE,
    IPC_COMMAND_PEER_NOTIFY_TWO,
    IPC_COMMAND_PEER_BLOCK,
    IPC_COMMAND_CLIENT_REPLY_THREE,
    IPC_COMMAND_SERVER_RECEIVE_NOTIFY,
    IPC_COMMAND_SERVER_WAKE_PEER,
    IPC_COMMAND_SERVER_BLOCK_FINAL,
    IPC_COMMAND_CLIENT_ERROR_UNAUTHORIZED,
    IPC_COMMAND_CLIENT_ERROR_ARGUMENT,
    IPC_COMMAND_CLIENT_ERROR_FAULT,
};

_Static_assert(
    sizeof(ipc_syscall_test_scenario_threads) / sizeof(ipc_syscall_test_scenario_threads[0])
        == sizeof(ipc_syscall_test_scenario_commands) / sizeof(ipc_syscall_test_scenario_commands[0]),
    "IPC syscall scenario arrays must stay aligned"
);

struct ipc_syscall_test_context *ipc_syscall_test_context_get(void)
{
    return &test_context;
}

bool ipc_syscall_test_test_mismatch(
    uint64_t stage,
    size_t thread_index,
    enum ipc_syscall_command command
)
{
    uart_write("MICROS_TEST_FAILURE ipc-syscall-stage=");
    uart_write_hex64(stage);
    uart_write(" thread=");
    uart_write_hex64(thread_index);
    uart_write(" command=");
    uart_write_hex64((uint64_t)command);
    uart_write("\n");
    uart_flush();
    return false;
}

void ipc_syscall_test_clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void ipc_syscall_test_copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

bool ipc_syscall_test_bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
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

void ipc_syscall_test_fill_context_pattern(
    struct micros_user_context *context,
    uint64_t base
)
{
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    for (index = 0; index < sizeof(words) / sizeof(words[0]); ++index) {
        words[index] = base + index;
    }
    ipc_syscall_test_copy_bytes(context, words, sizeof(*context));
}

uint64_t ipc_syscall_test_user_address_of(const unsigned char *symbol)
{
    return IPC_SYSCALL_TEST_CODE_VIRTUAL_ADDRESS
        + (
            (uintptr_t)symbol
            - (uintptr_t)micros_ipc_syscall_test_payload_start
        );
}

size_t ipc_syscall_test_current_thread_index(struct ipc_syscall_test_context *fixture, const struct micros_hart *hart)
{
    size_t index;

    if (hart == NULL) {
        return IPC_SYSCALL_PROCESS_COUNT;
    }
    for (index = 0; index < IPC_SYSCALL_PROCESS_COUNT; ++index) {
        if (
            hart->current_thread.slot == fixture->threads[index].slot
            && hart->current_thread.generation
                == fixture->threads[index].generation
        ) {
            return index;
        }
    }
    return IPC_SYSCALL_PROCESS_COUNT;
}

void ipc_syscall_test_copy_to_user_buffer(
    struct ipc_syscall_test_context *fixture,
    size_t process_index,
    uint64_t user_address,
    const struct micros_ipc_message *message
)
{
    const unsigned char *input = (const unsigned char *)message;
    size_t index;

    for (index = 0; index < sizeof(*message); ++index) {
        uint64_t offset =
            user_address - IPC_SYSCALL_TEST_DATA_VIRTUAL_ADDRESS + index;
        uint64_t physical =
            offset < MICROS_SV39_PAGE_SIZE
                ? fixture->data_physical[process_index][0] + offset
                : fixture->data_physical[process_index][1]
                    + offset - MICROS_SV39_PAGE_SIZE;

        *(unsigned char *)(uintptr_t)physical = input[index];
    }
}

void ipc_syscall_test_copy_from_user_buffer(
    struct ipc_syscall_test_context *fixture,
    size_t process_index,
    uint64_t user_address,
    struct micros_ipc_message *message
)
{
    unsigned char *output = (unsigned char *)message;
    size_t index;

    for (index = 0; index < sizeof(*message); ++index) {
        uint64_t offset =
            user_address - IPC_SYSCALL_TEST_DATA_VIRTUAL_ADDRESS + index;
        uint64_t physical =
            offset < MICROS_SV39_PAGE_SIZE
                ? fixture->data_physical[process_index][0] + offset
                : fixture->data_physical[process_index][1]
                    + offset - MICROS_SV39_PAGE_SIZE;

        output[index] =
            *(const unsigned char *)(uintptr_t)physical;
    }
}

bool ipc_syscall_test_command_has_stable_error(
    enum ipc_syscall_command command
)
{
    return (
        command == IPC_COMMAND_CLIENT_ERROR_UNAUTHORIZED
        || command == IPC_COMMAND_CLIENT_ERROR_ARGUMENT
        || command == IPC_COMMAND_CLIENT_ERROR_FAULT
    );
}

bool ipc_syscall_test_snapshot_negative_state(
    struct ipc_syscall_test_context *fixture,
    const struct micros_kernel_objects *objects,
    enum ipc_syscall_command command
)
{
    size_t index;

    if (
        objects == NULL
        || fixture->registry == NULL
        || !ipc_syscall_test_command_has_stable_error(command)
    ) {
        return false;
    }
    ipc_syscall_test_copy_bytes(
        &fixture->negative_registry_snapshot,
        fixture->registry,
        sizeof(fixture->negative_registry_snapshot)
    );
    ipc_syscall_test_copy_bytes(
        &fixture->negative_objects_snapshot,
        objects,
        sizeof(fixture->negative_objects_snapshot)
    );
    for (index = 0; index < IPC_SYSCALL_PROCESS_COUNT; ++index) {
        ipc_syscall_test_copy_from_user_buffer(
            fixture,
            index,
            IPC_SYSCALL_TEST_INBOUND_ADDRESS,
            &fixture->negative_buffer_snapshots[index].inbound
        );
        ipc_syscall_test_copy_from_user_buffer(
            fixture,
            index,
            IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
            &fixture->negative_buffer_snapshots[index].outbound
        );
    }
    fixture->negative_snapshot_command = command;
    fixture->negative_snapshot_active = true;
    return true;
}

bool ipc_syscall_test_negative_state_matches(
    struct ipc_syscall_test_context *fixture,
    const struct micros_kernel_objects *objects,
    enum ipc_syscall_command command
)
{
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct ipc_negative_buffer_snapshot observed;
    size_t index;
    bool matches;

    if (
        !fixture->negative_snapshot_active
        || fixture->negative_snapshot_command != command
        || objects == NULL
        || fixture->registry == NULL
        || hart_handle.slot >= MICROS_HART_CAPACITY
    ) {
        return false;
    }
    ipc_syscall_test_copy_bytes(
        &fixture->negative_objects_candidate,
        objects,
        sizeof(fixture->negative_objects_candidate)
    );
    ipc_syscall_test_copy_bytes(
        &fixture->negative_objects_candidate.threads[
            fixture->threads[IPC_SYSCALL_CLIENT].slot
        ].user_context,
        &fixture->negative_objects_snapshot.threads[
            fixture->threads[IPC_SYSCALL_CLIENT].slot
        ].user_context,
        sizeof(struct micros_user_context)
    );
    fixture->negative_objects_candidate.harts[
        hart_handle.slot
    ].accounting_owner =
        fixture->negative_objects_snapshot.harts[
            hart_handle.slot
        ].accounting_owner;
    fixture->negative_objects_candidate.harts[
        hart_handle.slot
    ].accounting_started_at =
        fixture->negative_objects_snapshot.harts[
            hart_handle.slot
        ].accounting_started_at;
    ipc_syscall_test_copy_bytes(
        &fixture->negative_objects_candidate.harts[
            hart_handle.slot
        ].accounted_thread,
        &fixture->negative_objects_snapshot.harts[
            hart_handle.slot
        ].accounted_thread,
        sizeof(struct micros_thread_handle)
    );
    fixture->negative_objects_candidate.harts[
        hart_handle.slot
    ].kernel_counter_ticks =
        fixture->negative_objects_snapshot.harts[
            hart_handle.slot
        ].kernel_counter_ticks;
    matches = (
        ipc_syscall_test_bytes_equal(
            &fixture->negative_registry_snapshot,
            fixture->registry,
            sizeof(fixture->negative_registry_snapshot)
        )
        && ipc_syscall_test_bytes_equal(
            &fixture->negative_objects_snapshot,
            &fixture->negative_objects_candidate,
            sizeof(fixture->negative_objects_snapshot)
        )
    );
    for (
        index = 0;
        matches && index < IPC_SYSCALL_PROCESS_COUNT;
        ++index
    ) {
        ipc_syscall_test_copy_from_user_buffer(
            fixture,
            index,
            IPC_SYSCALL_TEST_INBOUND_ADDRESS,
            &observed.inbound
        );
        ipc_syscall_test_copy_from_user_buffer(
            fixture,
            index,
            IPC_SYSCALL_TEST_OUTBOUND_ADDRESS,
            &observed.outbound
        );
        matches = ipc_syscall_test_bytes_equal(
            &fixture->negative_buffer_snapshots[index],
            &observed,
            sizeof(observed)
        );
    }
    fixture->negative_snapshot_active = false;
    return matches;
}

void ipc_syscall_test_fill_message(
    struct ipc_syscall_test_context *fixture,
    size_t process_index,
    uint64_t user_address,
    uint32_t type,
    uint8_t seed
)
{
    struct micros_ipc_message message;
    size_t index;

    ipc_syscall_test_clear_bytes(&message, sizeof(message));
    message.source = UINT32_C(0xaaaaaaaa);
    message.type = type;
    message.reply_token = UINT64_C(0xbbbbbbbbbbbbbbbb);
    for (index = 0; index < sizeof(message.payload); ++index) {
        message.payload[index] = (uint8_t)(seed + index);
    }
    ipc_syscall_test_copy_to_user_buffer(fixture, process_index, user_address, &message);
}

bool ipc_syscall_test_message_matches(
    struct ipc_syscall_test_context *fixture,
    size_t process_index,
    uint64_t user_address,
    micros_endpoint_t source,
    uint32_t type,
    uint8_t seed,
    enum ipc_token_expectation token_expectation,
    uint64_t *token
)
{
    struct micros_ipc_message message;
    size_t index;

    ipc_syscall_test_copy_from_user_buffer(
        fixture,
        process_index,
        user_address,
        &message
    );
    if (
        message.source != source
        || message.type != type
        || (
            token_expectation == IPC_TOKEN_ZERO
                ? message.reply_token != 0
                : message.reply_token == 0
        )
    ) {
        return false;
    }
    for (index = 0; index < sizeof(message.payload); ++index) {
        if (message.payload[index] != (uint8_t)(seed + index)) {
            return false;
        }
    }
    if (token != NULL) {
        *token = message.reply_token;
    }
    return true;
}

static uint64_t notification_mask(
    const struct micros_ipc_message *message
)
{
    uint64_t mask = 0;
    size_t index;

    for (index = 0; index < sizeof(mask); ++index) {
        mask |= (uint64_t)message->payload[index] << (index * 8);
    }
    return mask;
}

bool ipc_syscall_test_notification_matches(
    struct ipc_syscall_test_context *fixture,
    size_t process_index,
    micros_endpoint_t source,
    uint64_t mask
)
{
    struct micros_ipc_message message;
    size_t index;

    ipc_syscall_test_copy_from_user_buffer(
        fixture,
        process_index,
        IPC_SYSCALL_TEST_INBOUND_ADDRESS,
        &message
    );
    if (
        message.source != source
        || message.type != MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        || message.reply_token != 0
        || notification_mask(&message) != mask
    ) {
        return false;
    }
    for (index = sizeof(mask); index < sizeof(message.payload); ++index) {
        if (message.payload[index] != 0) {
            return false;
        }
    }
    return true;
}
