#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap_control.h"
#include "micros/tty.h"
#include "tests/qemu/tty_handoff_protocol.h"

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;

static uint8_t tty_read_buffer[64];
static uint8_t tty_write_buffer[256];
static volatile uint64_t tty_service_vfs_data =
    UINT64_C(0x5454595646534441);

struct tty_call_result {
    enum micros_tty_result result;
    uint64_t request_id;
    uint64_t transferred_count;
};

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(
    uint8_t *destination,
    const uint8_t *source,
    size_t size
)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        destination[index] = source[index];
    }
}

static bool bytes_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size
)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

static bool bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static uint64_t read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool validate_configuration(void)
{
    size_t index;

    if (
        micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.service_id
            != MICROS_TTY_HANDOFF_TEST_VFS_SERVICE_ID
        || micros_bootstrap_service_config.service_count
            != MICROS_TTY_HANDOFF_TEST_SERVICE_COUNT
        || micros_bootstrap_service_config.self_endpoint
            != micros_bootstrap_service_config.services[
                MICROS_TTY_HANDOFF_TEST_VFS_SERVICE_ID - 1
            ].endpoint
        || micros_bootstrap_service_config.launcher_endpoint
            != micros_bootstrap_service_config.services[
                MICROS_TTY_HANDOFF_TEST_LAUNCHER_SERVICE_ID - 1
            ].endpoint
        || micros_bootstrap_service_config.manifest_view_address != 0
    ) {
        return false;
    }
    for (
        index = 0;
        index < MICROS_TTY_HANDOFF_TEST_SERVICE_COUNT;
        ++index
    ) {
        if (
            micros_bootstrap_service_config.services[index].service_id
                != index + 1
            || micros_bootstrap_service_config.services[index].endpoint
                == MICROS_ENDPOINT_NONE
            || micros_bootstrap_service_config.services[index].endpoint
                == MICROS_ENDPOINT_ANY
        ) {
            return false;
        }
    }
    for (
        index = 0;
        index < sizeof(micros_bootstrap_service_config.reserved)
                / sizeof(micros_bootstrap_service_config.reserved[0]);
        ++index
    ) {
        if (micros_bootstrap_service_config.reserved[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool send_ready(void)
{
    struct micros_ipc_message message;

    clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    write_u32_le(
        &message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    write_u32_le(
        &message.payload[4],
        MICROS_TTY_HANDOFF_TEST_VFS_SERVICE_ID
    );
    write_u32_le(
        &message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    write_u32_le(
        &message.payload[16],
        micros_bootstrap_service_config.self_endpoint
    );
    if (
        micros_runtime_call(
            micros_bootstrap_service_config.launcher_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    return (
        message.source
            == micros_bootstrap_service_config.launcher_endpoint
        && message.type == MICROS_BOOTSTRAP_MESSAGE_READY_ACK
        && message.reply_token == 0
        && read_u32_le(&message.payload[0])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && read_u32_le(&message.payload[4])
            == MICROS_TTY_HANDOFF_TEST_VFS_SERVICE_ID
        && read_u32_le(&message.payload[8])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && read_u32_le(&message.payload[12]) == 0
        && read_u32_le(&message.payload[16])
            == micros_bootstrap_service_config.self_endpoint
        && bytes_are_zero(
            &message.payload[20],
            sizeof(message.payload) - 20
        )
    );
}

static bool tty_result_matches(
    const struct micros_ipc_message *message,
    micros_endpoint_t tty_endpoint,
    uint32_t request_type,
    struct tty_call_result *result
)
{
    if (
        message == NULL
        || result == NULL
        || message->source != tty_endpoint
        || message->type != MICROS_TTY_MESSAGE_RESULT
        || message->reply_token != 0
        || read_u32_le(&message->payload[0])
            != MICROS_TTY_PROTOCOL_VERSION
        || read_u32_le(&message->payload[4]) != request_type
        || read_u32_le(&message->payload[12]) != 0
        || !bytes_are_zero(
            &message->payload[32],
            sizeof(message->payload) - 32
        )
    ) {
        return false;
    }
    result->result = (enum micros_tty_result)(
        (int32_t)read_u32_le(&message->payload[8])
    );
    result->request_id = read_u64_le(&message->payload[16]);
    result->transferred_count =
        read_u64_le(&message->payload[24]);
    return true;
}

static bool call_tty(
    uint32_t type,
    uint64_t request_id,
    micros_grant_t grant,
    uint64_t count,
    struct tty_call_result *result
)
{
    struct micros_ipc_message message;
    micros_endpoint_t tty_endpoint =
        micros_bootstrap_service_config.services[
            MICROS_TTY_HANDOFF_TEST_TTY_SERVICE_ID - 1
        ].endpoint;

    clear_bytes(&message, sizeof(message));
    message.type = type;
    write_u32_le(
        &message.payload[0],
        MICROS_TTY_PROTOCOL_VERSION
    );
    write_u64_le(&message.payload[8], request_id);
    if (
        type == MICROS_TTY_MESSAGE_SUBMIT_READ
        || type == MICROS_TTY_MESSAGE_SUBMIT_WRITE
    ) {
        write_u32_le(&message.payload[16], grant);
        write_u64_le(&message.payload[24], 0);
        write_u64_le(&message.payload[32], count);
    }
    return (
        micros_runtime_call(tty_endpoint, &message)
            == MICROS_SYSCALL_ABI_OK
        && tty_result_matches(
            &message,
            tty_endpoint,
            type,
            result
        )
        && result->request_id == request_id
    );
}

static bool receive_tty_events(uint64_t *events)
{
    struct micros_ipc_message message;
    micros_endpoint_t tty_endpoint =
        micros_bootstrap_service_config.services[
            MICROS_TTY_HANDOFF_TEST_TTY_SERVICE_ID - 1
        ].endpoint;
    uint64_t observed;

    clear_bytes(&message, sizeof(message));
    if (
        events == NULL
        || micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    observed = read_u64_le(&message.payload[0]);
    if (
        message.source != tty_endpoint
        || message.type != MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        || message.reply_token != 0
        || observed == 0
        || (
            observed
            & ~(
                MICROS_TTY_EVENT_COMPLETION
                | MICROS_TTY_EVENT_WRITABLE
            )
        ) != 0
        || !bytes_are_zero(
            &message.payload[8],
            sizeof(message.payload) - 8
        )
    ) {
        return false;
    }
    *events = observed;
    return true;
}

static bool collect_completed(
    uint64_t request_id,
    uint64_t expected_count
)
{
    struct tty_call_result result;
    uint64_t events;

    for (;;) {
        if (
            !call_tty(
                MICROS_TTY_MESSAGE_COLLECT,
                request_id,
                MICROS_GRANT_NONE,
                0,
                &result
            )
        ) {
            return false;
        }
        if (result.result == MICROS_TTY_RESULT_OK) {
            return result.transferred_count == expected_count;
        }
        if (
            result.result != MICROS_TTY_RESULT_PENDING
            || result.transferred_count != 0
            || !receive_tty_events(&events)
            || (events & MICROS_TTY_EVENT_COMPLETION) == 0
        ) {
            return false;
        }
    }
}

static bool submit_write(
    uint64_t request_id,
    micros_grant_t grant,
    const uint8_t *bytes,
    size_t count
)
{
    struct tty_call_result result;

    if (
        count == 0
        || count > sizeof(tty_write_buffer)
    ) {
        return false;
    }
    copy_bytes(tty_write_buffer, bytes, count);
    return (
        call_tty(
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            request_id,
            grant,
            count,
            &result
        )
        && result.result == MICROS_TTY_RESULT_OK
        && result.transferred_count == 0
        && collect_completed(request_id, count)
    );
}

static bool wait_for_physical_drain(void)
{
    struct tty_call_result result;
    uint64_t events;

    if (
        !call_tty(
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            UINT64_C(4),
            MICROS_GRANT_NONE,
            1,
            &result
        )
    ) {
        return false;
    }
    if (result.result == MICROS_TTY_RESULT_GRANT) {
        return result.transferred_count == 0;
    }
    if (
        result.result != MICROS_TTY_RESULT_BUSY
        || result.transferred_count != 0
    ) {
        return false;
    }
    do {
        if (!receive_tty_events(&events)) {
            return false;
        }
    } while ((events & MICROS_TTY_EVENT_WRITABLE) == 0);
    return (
        call_tty(
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            UINT64_C(4),
            MICROS_GRANT_NONE,
            1,
            &result
        )
        && result.result == MICROS_TTY_RESULT_GRANT
        && result.transferred_count == 0
    );
}

_Noreturn void micros_tty_service_test_report(
    uint64_t magic,
    uint64_t read_count,
    uint64_t trigger_count,
    uint64_t pass_count
);

void micros_service_main(void)
{
    static const uint8_t input_trigger[] =
        MICROS_TTY_HANDOFF_TEST_INPUT_TRIGGER;
    static const uint8_t expected_input[] =
        MICROS_TTY_HANDOFF_TEST_EXPECTED_INPUT;
    static const uint8_t pass_marker[] =
        MICROS_TTY_HANDOFF_TEST_PASS;
    struct tty_call_result result;
    micros_endpoint_t tty_endpoint;
    micros_grant_t read_grant = MICROS_GRANT_NONE;
    micros_grant_t write_grant = MICROS_GRANT_NONE;

    if (
        tty_service_vfs_data != UINT64_C(0x5454595646534441)
        || !validate_configuration()
        || !send_ready()
    ) {
        __builtin_trap();
    }
    tty_endpoint = micros_bootstrap_service_config.services[
        MICROS_TTY_HANDOFF_TEST_TTY_SERVICE_ID - 1
    ].endpoint;
    if (
        micros_runtime_grant_create(
            tty_endpoint,
            (uintptr_t)tty_read_buffer,
            sizeof(tty_read_buffer),
            MICROS_GRANT_PERMISSION_WRITE,
            &read_grant
        ) != MICROS_SYSCALL_ABI_OK
        || micros_runtime_grant_create(
            tty_endpoint,
            (uintptr_t)tty_write_buffer,
            sizeof(tty_write_buffer),
            MICROS_GRANT_PERMISSION_READ,
            &write_grant
        ) != MICROS_SYSCALL_ABI_OK
        || !call_tty(
            MICROS_TTY_MESSAGE_SUBMIT_READ,
            UINT64_C(1),
            read_grant,
            sizeof(tty_read_buffer),
            &result
        )
        || result.result != MICROS_TTY_RESULT_OK
        || result.transferred_count != 0
        || !submit_write(
            UINT64_C(2),
            write_grant,
            input_trigger,
            sizeof(input_trigger) - 1
        )
        || !collect_completed(
            UINT64_C(1),
            MICROS_TTY_HANDOFF_TEST_READ_COUNT
        )
        || !bytes_equal(
            tty_read_buffer,
            expected_input,
            sizeof(expected_input) - 1
        )
        || !submit_write(
            UINT64_C(3),
            write_grant,
            pass_marker,
            sizeof(pass_marker) - 1
        )
        || !wait_for_physical_drain()
        || micros_runtime_grant_revoke(read_grant)
            != MICROS_SYSCALL_ABI_OK
        || micros_runtime_grant_revoke(write_grant)
            != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    micros_tty_service_test_report(
        MICROS_TTY_HANDOFF_TEST_REPORT_MAGIC,
        MICROS_TTY_HANDOFF_TEST_READ_COUNT,
        sizeof(input_trigger) - 1,
        sizeof(pass_marker) - 1
    );
}
