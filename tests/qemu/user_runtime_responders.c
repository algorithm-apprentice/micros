#include "tests/qemu/user_runtime_service_internal.h"

struct user_runtime_server_context {
    struct micros_ipc_message message;
    struct micros_ipc_message reply;
    micros_grant_t read_grant;
    micros_grant_t write_grant;
    uint64_t token;
    size_t index;
};

static uint64_t user_runtime_service_server_peer_exchange(
    struct user_runtime_server_context *role,
    const uint8_t *read_expected
);
static uint64_t user_runtime_service_server_grant_validation(
    struct user_runtime_server_context *role,
    const uint8_t *read_expected
);
static uint64_t user_runtime_service_server_grant_copies(
    struct user_runtime_server_context *role,
    const uint8_t *read_expected
);
static uint64_t user_runtime_service_server_stale_complete(
    struct user_runtime_server_context *role
);

uint64_t user_runtime_service_run_peer(void)
{
    struct micros_ipc_message message;

    if (
        micros_runtime_receive(
            micros_user_runtime_test_config.server_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || !user_runtime_service_message_matches(
            &message,
            micros_user_runtime_test_config.server_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_PEER_WAKE,
            UINT8_C(0xa0),
            false
        )
        || micros_runtime_notify(
            micros_user_runtime_test_config.server_endpoint,
            MICROS_USER_RUNTIME_TEST_NOTIFY_MASK
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x301);
    }
    micros_user_runtime_test_config.status |=
        MICROS_USER_RUNTIME_TEST_STATUS_IPC
        | MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE;
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
            == MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x302);
    }
    return UINT64_C(0x303);
}

static uint64_t user_runtime_service_server_calls(
    struct user_runtime_server_context *role,
    const uint8_t *read_expected
)
{
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &role->message)
            != MICROS_SYSCALL_ABI_OK
        || !user_runtime_service_message_matches(
            &role->message,
            micros_user_runtime_test_config.client_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_INITIAL,
            UINT8_C(0x10),
            false
        )
        || micros_runtime_receive(
            micros_user_runtime_test_config.client_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
        || !user_runtime_service_message_matches(
            &role->message,
            micros_user_runtime_test_config.client_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_ONE,
            UINT8_C(0x20),
            true
        )
    ) {
        return UINT64_C(0x401);
    }
    role->token = role->message.reply_token;
    user_runtime_service_fill_message(
        &role->reply,
        MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_ONE_REPLY,
        UINT8_C(0x30)
    );
    if (
        micros_runtime_reply(role->token, &role->reply)
            != MICROS_SYSCALL_ABI_OK
        || micros_runtime_receive(
            micros_user_runtime_test_config.client_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
        || !user_runtime_service_message_matches(
            &role->message,
            micros_user_runtime_test_config.client_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_TWO,
            UINT8_C(0x40),
            true
        )
    ) {
        return UINT64_C(0x402);
    }
    role->token = role->message.reply_token;
    return user_runtime_service_server_peer_exchange(role, read_expected);
}

static uint64_t user_runtime_service_server_peer_exchange(
    struct user_runtime_server_context *role,
    const uint8_t *read_expected
)
{
    user_runtime_service_fill_message(
        &role->message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_PEER_WAKE,
        UINT8_C(0xa0)
    );
    if (
        micros_runtime_send(
            micros_user_runtime_test_config.peer_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x403);
    }
    user_runtime_service_fill_message(
        &role->reply,
        MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_TWO_REPLY,
        UINT8_C(0x50)
    );
    if (
        micros_runtime_reply_receive(
            role->token,
            &role->reply,
            MICROS_ENDPOINT_ANY,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
        || !user_runtime_service_notification_matches(
            &role->message,
            micros_user_runtime_test_config.peer_endpoint
        )
        || micros_runtime_receive(
            micros_user_runtime_test_config.client_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
        || role->message.source
            != micros_user_runtime_test_config.client_endpoint
        || role->message.type != MICROS_USER_RUNTIME_TEST_MESSAGE_GRANTS
        || role->message.reply_token != 0
    ) {
        return UINT64_C(0x404);
    }
    role->read_grant = user_runtime_service_get_u32(role->message.payload);
    role->write_grant = user_runtime_service_get_u32(role->message.payload + 4);
    return user_runtime_service_server_grant_validation(role, read_expected);
}

static uint64_t user_runtime_service_server_grant_validation(
    struct user_runtime_server_context *role,
    const uint8_t *read_expected
)
{
    if (
        micros_runtime_grant_validate(
            micros_user_runtime_test_config.client_endpoint,
            role->read_grant,
            0,
            TEST_TRANSFER_SIZE,
            MICROS_GRANT_PERMISSION_READ
        ) != MICROS_SYSCALL_ABI_OK
        || micros_runtime_grant_validate(
            micros_user_runtime_test_config.client_endpoint,
            role->write_grant,
            0,
            TEST_TRANSFER_SIZE,
            MICROS_GRANT_PERMISSION_WRITE
        ) != MICROS_SYSCALL_ABI_OK
        || micros_runtime_grant_validate(
            micros_user_runtime_test_config.client_endpoint,
            role->read_grant,
            0,
            0,
            MICROS_GRANT_PERMISSION_WRITE
        ) != MICROS_SYSCALL_ABI_UNAUTHORIZED
    ) {
        return UINT64_C(0x40a);
    }
    return user_runtime_service_server_grant_copies(role, read_expected);
}

static uint64_t user_runtime_service_server_grant_copies(
    struct user_runtime_server_context *role,
    const uint8_t *read_expected
)
{
    micros_user_runtime_test_server_region.before =
        MICROS_USER_RUNTIME_TEST_CANARY_BEFORE;
    micros_user_runtime_test_server_region.after =
        MICROS_USER_RUNTIME_TEST_CANARY_AFTER;
    for (role->index = 0; role->index < TEST_TRANSFER_SIZE; ++role->index) {
        micros_user_runtime_test_server_region.bytes[role->index] = 0;
    }
    if (
        micros_runtime_grant_copy_from(
            micros_user_runtime_test_config.client_endpoint,
            role->read_grant,
            0,
            (uintptr_t)micros_user_runtime_test_server_region.bytes,
            TEST_TRANSFER_SIZE
        ) != MICROS_SYSCALL_ABI_OK
        || micros_user_runtime_test_server_region.before
            != MICROS_USER_RUNTIME_TEST_CANARY_BEFORE
        || micros_user_runtime_test_server_region.after
            != MICROS_USER_RUNTIME_TEST_CANARY_AFTER
        || !user_runtime_service_bytes_equal(
            micros_user_runtime_test_server_region.bytes,
            read_expected,
            TEST_TRANSFER_SIZE
        )
    ) {
        return UINT64_C(0x405);
    }
    for (role->index = 0; role->index < TEST_TRANSFER_SIZE; ++role->index) {
        micros_user_runtime_test_server_region.bytes[role->index] =
            (uint8_t)(UINT8_C(0xd0) + role->index);
    }
    if (
        micros_runtime_grant_copy_to(
            micros_user_runtime_test_config.client_endpoint,
            role->write_grant,
            0,
            (uintptr_t)micros_user_runtime_test_server_region.bytes,
            TEST_TRANSFER_SIZE
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x406);
    }
    return user_runtime_service_server_stale_complete(role);
}

static uint64_t user_runtime_service_server_stale_complete(
    struct user_runtime_server_context *role
)
{
    user_runtime_service_fill_message(
        &role->message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_COPY_DONE,
        UINT8_C(0x70)
    );
    if (
        micros_runtime_send(
            micros_user_runtime_test_config.client_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
        || micros_runtime_receive(
            micros_user_runtime_test_config.client_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
        || role->message.source
            != micros_user_runtime_test_config.client_endpoint
        || role->message.type != MICROS_USER_RUNTIME_TEST_MESSAGE_RETRY
        || role->message.reply_token != 0
    ) {
        return UINT64_C(0x407);
    }
    role->read_grant = user_runtime_service_get_u32(role->message.payload);
    if (
        micros_runtime_grant_validate(
            micros_user_runtime_test_config.client_endpoint,
            role->read_grant,
            0,
            TEST_TRANSFER_SIZE,
            MICROS_GRANT_PERMISSION_READ
        ) != MICROS_SYSCALL_ABI_STALE_GRANT
        || micros_runtime_grant_copy_from(
            micros_user_runtime_test_config.client_endpoint,
            role->read_grant,
            0,
            (uintptr_t)micros_user_runtime_test_server_region.bytes,
            TEST_TRANSFER_SIZE
        ) != MICROS_SYSCALL_ABI_STALE_GRANT
    ) {
        return UINT64_C(0x408);
    }
    user_runtime_service_fill_message(
        &role->message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_STALE_DONE,
        UINT8_C(0x90)
    );
    if (
        micros_runtime_send(
            micros_user_runtime_test_config.client_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
        || micros_runtime_receive(
            micros_user_runtime_test_config.client_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
        || !user_runtime_service_message_matches(
            &role->message,
            micros_user_runtime_test_config.client_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_CLIENT_DONE,
            UINT8_C(0xb0),
            false
        )
    ) {
        return UINT64_C(0x409);
    }
    micros_user_runtime_test_config.status |=
        MICROS_USER_RUNTIME_TEST_STATUS_IPC
        | MICROS_USER_RUNTIME_TEST_STATUS_GRANTS
        | MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE;
    return 0;
}

uint64_t user_runtime_service_run_server(void)
{
    static const uint8_t read_expected[TEST_TRANSFER_SIZE] = {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    };
    struct user_runtime_server_context role;

    return user_runtime_service_server_calls(&role, read_expected);
}
