#include "tests/qemu/user_runtime_service_internal.h"

struct user_runtime_client_context {
    struct micros_ipc_message message;
    volatile uint64_t canary_before;
    volatile uint64_t canary_after;
    micros_grant_t read_grant;
    micros_grant_t write_grant;
    uint64_t probe_result;
    size_t index;
};

static uint64_t user_runtime_service_client_calls(
    struct user_runtime_client_context *role,
    const uint8_t *read_expected
);
static uint64_t user_runtime_service_client_grants(
    struct user_runtime_client_context *role,
    const uint8_t *read_expected
);
static uint64_t user_runtime_service_client_revoke_stale(
    struct user_runtime_client_context *role
);
static uint64_t user_runtime_service_client_complete(
    struct user_runtime_client_context *role
);

static uint64_t user_runtime_service_client_raw_send(
    struct user_runtime_client_context *role,
    const uint8_t *read_expected
)
{
    user_runtime_service_fill_message(
        &role->message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_INITIAL,
        UINT8_C(0x10)
    );
    role->probe_result = micros_user_runtime_test_raw_send_probe(
        micros_user_runtime_test_config.server_endpoint,
        &role->message
    );
    if (role->probe_result != 0) {
        return UINT64_C(0x2100) + role->probe_result;
    }
    micros_user_runtime_test_config.status |=
        MICROS_USER_RUNTIME_TEST_STATUS_REGISTERS;
    return user_runtime_service_client_calls(role, read_expected);
}

static uint64_t user_runtime_service_client_calls(
    struct user_runtime_client_context *role,
    const uint8_t *read_expected
)
{
    user_runtime_service_fill_message(
        &role->message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_ONE,
        UINT8_C(0x20)
    );
    if (
        micros_runtime_call(
            micros_user_runtime_test_config.server_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
        || role->canary_before != MICROS_USER_RUNTIME_TEST_CANARY_BEFORE
        || role->canary_after != MICROS_USER_RUNTIME_TEST_CANARY_AFTER
        || !user_runtime_service_message_matches(
            &role->message,
            micros_user_runtime_test_config.server_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_ONE_REPLY,
            UINT8_C(0x30),
            false
        )
    ) {
        return UINT64_C(0x202);
    }
    user_runtime_service_fill_message(
        &role->message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_TWO,
        UINT8_C(0x40)
    );
    if (
        micros_runtime_call(
            micros_user_runtime_test_config.server_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
        || !user_runtime_service_message_matches(
            &role->message,
            micros_user_runtime_test_config.server_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_TWO_REPLY,
            UINT8_C(0x50),
            false
        )
    ) {
        return UINT64_C(0x203);
    }
    micros_user_runtime_test_config.status |=
        MICROS_USER_RUNTIME_TEST_STATUS_IPC;
    return user_runtime_service_client_grants(role, read_expected);
}

static uint64_t user_runtime_service_client_grants(
    struct user_runtime_client_context *role,
    const uint8_t *read_expected
)
{
    micros_user_runtime_test_write_region.before =
        MICROS_USER_RUNTIME_TEST_CANARY_BEFORE;
    micros_user_runtime_test_write_region.after =
        MICROS_USER_RUNTIME_TEST_CANARY_AFTER;
    for (role->index = 0; role->index < TEST_TRANSFER_SIZE; ++role->index) {
        micros_user_runtime_test_write_region.bytes[role->index] = 0;
    }
    if (
        micros_runtime_grant_create(
            micros_user_runtime_test_config.server_endpoint,
            (uintptr_t)micros_user_runtime_test_read_region.bytes,
            TEST_TRANSFER_SIZE,
            MICROS_GRANT_PERMISSION_READ,
            &role->read_grant
        ) != MICROS_SYSCALL_ABI_OK
        || micros_runtime_grant_create(
            micros_user_runtime_test_config.server_endpoint,
            (uintptr_t)micros_user_runtime_test_write_region.bytes,
            TEST_TRANSFER_SIZE,
            MICROS_GRANT_PERMISSION_WRITE,
            &role->write_grant
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x204);
    }
    user_runtime_service_fill_message(
        &role->message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_GRANTS,
        UINT8_C(0x60)
    );
    user_runtime_service_put_u32(role->message.payload, role->read_grant);
    user_runtime_service_put_u32(role->message.payload + 4, role->write_grant);
    if (
        micros_runtime_send(
            micros_user_runtime_test_config.server_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
        || micros_runtime_receive(
            micros_user_runtime_test_config.server_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
        || !user_runtime_service_message_matches(
            &role->message,
            micros_user_runtime_test_config.server_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_COPY_DONE,
            UINT8_C(0x70),
            false
        )
        || micros_user_runtime_test_write_region.before
            != MICROS_USER_RUNTIME_TEST_CANARY_BEFORE
        || micros_user_runtime_test_write_region.after
            != MICROS_USER_RUNTIME_TEST_CANARY_AFTER
        || !user_runtime_service_bytes_equal(
            micros_user_runtime_test_read_region.bytes,
            read_expected,
            TEST_TRANSFER_SIZE
        )
    ) {
        return UINT64_C(0x205);
    }
    for (role->index = 0; role->index < TEST_TRANSFER_SIZE; ++role->index) {
        if (
            micros_user_runtime_test_write_region.bytes[role->index]
                != (uint8_t)(UINT8_C(0xd0) + role->index)
        ) {
            return UINT64_C(0x206);
        }
    }
    return user_runtime_service_client_revoke_stale(role);
}

static uint64_t user_runtime_service_client_revoke_stale(
    struct user_runtime_client_context *role
)
{
    if (
        micros_runtime_grant_revoke(role->read_grant)
            != MICROS_SYSCALL_ABI_OK
        || micros_runtime_grant_revoke(role->write_grant)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x207);
    }
    user_runtime_service_fill_message(
        &role->message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_RETRY,
        UINT8_C(0x80)
    );
    user_runtime_service_put_u32(role->message.payload, role->read_grant);
    if (
        micros_runtime_send(
            micros_user_runtime_test_config.server_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
        || micros_runtime_receive(
            micros_user_runtime_test_config.server_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
        || !user_runtime_service_message_matches(
            &role->message,
            micros_user_runtime_test_config.server_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_STALE_DONE,
            UINT8_C(0x90),
            false
        )
    ) {
        return UINT64_C(0x208);
    }
    return user_runtime_service_client_complete(role);
}

static uint64_t user_runtime_service_client_complete(
    struct user_runtime_client_context *role
)
{
    micros_user_runtime_test_config.status |=
        MICROS_USER_RUNTIME_TEST_STATUS_GRANTS
        | MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE;
    user_runtime_service_fill_message(
        &role->message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_CLIENT_DONE,
        UINT8_C(0xb0)
    );
    if (
        micros_runtime_send(
            micros_user_runtime_test_config.server_endpoint,
            &role->message
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x209);
    }
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &role->message)
            == MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x20a);
    }
    return UINT64_C(0x20b);
}

uint64_t user_runtime_service_run_client(void)
{
    static const uint8_t read_expected[TEST_TRANSFER_SIZE] = {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    };
    struct user_runtime_client_context role;

    role.canary_before = MICROS_USER_RUNTIME_TEST_CANARY_BEFORE;
    role.canary_after = MICROS_USER_RUNTIME_TEST_CANARY_AFTER;
    role.read_grant = MICROS_GRANT_NONE;
    role.write_grant = MICROS_GRANT_NONE;
    return user_runtime_service_client_raw_send(&role, read_expected);
}
