#include "tests/qemu/ramfs_service_vfs_internal.h"

static bool vfs_tty_result_matches(
    struct vfs_service_fixture *fixture,
    const struct micros_ipc_message *message,
    uint32_t request_type,
    struct vfs_tty_result *result
)
{
    if (
        message == NULL
        || result == NULL
        || message->source != fixture->vfs_tty_endpoint
        || message->type != MICROS_TTY_MESSAGE_RESULT
        || message->reply_token != 0
        || vfs_read_u32_le(&message->payload[0])
            != MICROS_TTY_PROTOCOL_VERSION
        || vfs_read_u32_le(&message->payload[4]) != request_type
        || vfs_read_u32_le(&message->payload[12]) != 0
        || !vfs_bytes_are_zero(
            &message->payload[32],
            sizeof(message->payload) - 32
        )
    ) {
        return false;
    }
    result->result = (enum micros_tty_result)(
        (int32_t)vfs_read_u32_le(&message->payload[8])
    );
    result->request_id = vfs_read_u64_le(&message->payload[16]);
    result->transferred_count =
        vfs_read_u64_le(&message->payload[24]);
    return true;
}

static bool vfs_call_tty(
    struct vfs_service_fixture *fixture,
    uint32_t type,
    uint64_t request_id,
    micros_grant_t grant,
    uint64_t count,
    struct vfs_tty_result *result
)
{
    struct micros_ipc_message message;

    vfs_clear_bytes(&message, sizeof(message));
    message.type = type;
    vfs_write_u32_le(
        &message.payload[0],
        MICROS_TTY_PROTOCOL_VERSION
    );
    vfs_write_u64_le(&message.payload[8], request_id);
    if (
        type == MICROS_TTY_MESSAGE_SUBMIT_READ
        || type == MICROS_TTY_MESSAGE_SUBMIT_WRITE
    ) {
        vfs_write_u32_le(&message.payload[16], grant);
        vfs_write_u64_le(&message.payload[24], 0);
        vfs_write_u64_le(&message.payload[32], count);
    }
    return (
        micros_runtime_call(fixture->vfs_tty_endpoint, &message)
            == MICROS_SYSCALL_ABI_OK
        && vfs_tty_result_matches(fixture, &message, type, result)
        && result->request_id == request_id
    );
}

static bool vfs_receive_tty_events(
    struct vfs_service_fixture *fixture,
    uint64_t *events
)
{
    struct micros_ipc_message message;
    uint64_t observed;

    vfs_clear_bytes(&message, sizeof(message));
    if (
        events == NULL
        || micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    observed = vfs_read_u64_le(&message.payload[0]);
    if (
        message.source != fixture->vfs_tty_endpoint
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
        || !vfs_bytes_are_zero(
            &message.payload[8],
            sizeof(message.payload) - 8
        )
    ) {
        return false;
    }
    *events = observed;
    return true;
}

static bool vfs_collect_tty_write(
    struct vfs_service_fixture *fixture,
    uint64_t request_id,
    uint64_t expected_count
)
{
    struct vfs_tty_result result;
    uint64_t events;

    for (;;) {
        if (
            !vfs_call_tty(
                fixture,
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
            || !vfs_receive_tty_events(fixture, &events)
            || (events & MICROS_TTY_EVENT_COMPLETION) == 0
        ) {
            return false;
        }
    }
}

static bool vfs_wait_for_tty_drain(
    struct vfs_service_fixture *fixture
)
{
    struct vfs_tty_result result;
    uint64_t events;

    if (
        !vfs_call_tty(
            fixture,
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            UINT64_C(2),
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
        if (!vfs_receive_tty_events(fixture, &events)) {
            return false;
        }
    } while ((events & MICROS_TTY_EVENT_WRITABLE) == 0);
    return (
        vfs_call_tty(
            fixture,
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            UINT64_C(2),
            MICROS_GRANT_NONE,
            1,
            &result
        )
        && result.result == MICROS_TTY_RESULT_GRANT
        && result.transferred_count == 0
    );
}

bool vfs_write_pass_marker(
    struct vfs_service_fixture *fixture
)
{
    static const uint8_t marker[] = MICROS_RAMFS_TEST_PASS;
    struct vfs_tty_result result;
    micros_grant_t grant = MICROS_GRANT_NONE;
    bool write_ok;
    bool revoke_ok;

    if (sizeof(marker) - 1 > sizeof(fixture->vfs_tty_buffer)) {
        return false;
    }
    vfs_clear_bytes(fixture->vfs_tty_buffer, sizeof(fixture->vfs_tty_buffer));
    vfs_copy_bytes(fixture->vfs_tty_buffer, marker, sizeof(marker) - 1);
    if (
        micros_runtime_grant_create(
            fixture->vfs_tty_endpoint,
            (uintptr_t)fixture->vfs_tty_buffer,
            sizeof(fixture->vfs_tty_buffer),
            MICROS_GRANT_PERMISSION_READ,
            &grant
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    write_ok = (
        vfs_call_tty(
            fixture,
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            UINT64_C(1),
            grant,
            sizeof(marker) - 1,
            &result
        )
        && result.result == MICROS_TTY_RESULT_OK
        && result.transferred_count == 0
        && vfs_collect_tty_write(
            fixture,
            UINT64_C(1),
            sizeof(marker) - 1
        )
        && vfs_wait_for_tty_drain(fixture)
    );
    revoke_ok = (
        micros_runtime_grant_revoke(grant) == MICROS_SYSCALL_ABI_OK
    );
    return write_ok && revoke_ok;
}
