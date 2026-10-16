#include "grant_syscall_test_internal.h"

static bool grant_syscall_test_arm_ipc_notify(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_NOTIFY,
        fixture->endpoints[GRANT_SYSCALL_GRANTEE],
        UINT64_C(5),
        0,
        0,
        UINT64_C(0x4444444444444444),
        UINT64_C(0x5555555555555555),
        UINT64_C(0x6666666666666666),
        MICROS_SYSCALL_ABI_OK,
        false,
        false,
        MICROS_SYSCALL_RETURN_CAPTURED
    );
    return true;
}

static bool grant_syscall_test_arm_create_read(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    {
        uint64_t base = TEST_DATA_SECOND_ADDRESS - 64;
        uint64_t length = 256;
        uint64_t permissions =
            input->command == GRANT_COMMAND_CREATE_WRITE
                ? MICROS_GRANT_PERMISSION_WRITE
                : MICROS_GRANT_PERMISSION_READ;

        if (input->command == GRANT_COMMAND_CREATE_REMOTE_READ_ONLY) {
            base = TEST_READ_ONLY_ADDRESS;
            length = 64;
            permissions = MICROS_GRANT_PERMISSION_WRITE;
        } else if (input->command == GRANT_COMMAND_CREATE_UNMAPPED) {
            base = TEST_UNMAPPED_ADDRESS;
            length = 64;
        } else if (input->command == GRANT_COMMAND_CREATE_STALE) {
            base = TEST_DATA_ADDRESS;
            length = 64;
        }
        grant_syscall_test_arm_raw(
            fixture,
            input->actor,
            input->context,
            input->command,
            MICROS_SYSCALL_ABI_GRANT_CREATE,
            fixture->endpoints[GRANT_SYSCALL_GRANTEE],
            base,
            length,
            permissions,
            0,
            0,
            0,
            0,
            true,
            false,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    }
}

static bool grant_syscall_test_arm_create_upper_endpoint(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_CREATE,
        (UINT64_C(1) << 32)
            | fixture->endpoints[GRANT_SYSCALL_GRANTEE],
        TEST_DATA_ADDRESS,
        64,
        MICROS_GRANT_PERMISSION_READ,
        0,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

static bool grant_syscall_test_arm_create_upper_permission(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_CREATE,
        fixture->endpoints[GRANT_SYSCALL_GRANTEE],
        TEST_DATA_ADDRESS,
        64,
        (UINT64_C(1) << 32)
            | MICROS_GRANT_PERMISSION_READ,
        0,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

static bool grant_syscall_test_arm_create_unused(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_CREATE,
        fixture->endpoints[GRANT_SYSCALL_GRANTEE],
        TEST_DATA_ADDRESS,
        64,
        MICROS_GRANT_PERMISSION_READ,
        1,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

static bool grant_syscall_test_arm_revoke_stale(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    {
        micros_grant_t token = fixture->stale_grant;

        if (input->command == GRANT_COMMAND_REVOKE_READ) {
            token = fixture->read_grant;
        } else if (input->command == GRANT_COMMAND_REVOKE_WRITE) {
            token = fixture->write_grant;
        }
        grant_syscall_test_arm_raw(
            fixture,
            input->actor,
            input->context,
            input->command,
            MICROS_SYSCALL_ABI_GRANT_REVOKE,
            token,
            0,
            0,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_OK,
            false,
            false,
            MICROS_SYSCALL_RETURN_NORMAL
        );
        return true;
    }
}

static bool grant_syscall_test_arm_revoke_upper_token(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_REVOKE,
        (UINT64_C(1) << 32) | fixture->stale_grant,
        0,
        0,
        0,
        0,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

static bool grant_syscall_test_arm_revoke_unused(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_REVOKE,
        fixture->stale_grant,
        1,
        0,
        0,
        0,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

static bool grant_syscall_test_arm_validate_read(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_VALIDATE,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->read_grant,
        32,
        64,
        MICROS_GRANT_PERMISSION_READ,
        0,
        0,
        MICROS_SYSCALL_ABI_OK,
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

static bool grant_syscall_test_arm_validate_zero(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_VALIDATE,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->write_grant,
        256,
        0,
        MICROS_GRANT_PERMISSION_WRITE,
        0,
        0,
        MICROS_SYSCALL_ABI_OK,
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

static bool grant_syscall_test_arm_validate_wrong_direction(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_VALIDATE,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->read_grant,
        0,
        1,
        MICROS_GRANT_PERMISSION_WRITE,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_UNAUTHORIZED),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

static bool grant_syscall_test_arm_validate_unmapped(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_VALIDATE,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->unmapped_grant,
        0,
        1,
        MICROS_GRANT_PERMISSION_READ,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_MEMORY_FAULT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

static bool grant_syscall_test_arm_validate_overflow_stale(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_VALIDATE,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->stale_grant,
        UINT64_MAX,
        2,
        MICROS_GRANT_PERMISSION_READ,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_RANGE),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

static bool grant_syscall_test_arm_validate_upper_permission(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_VALIDATE,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->read_grant,
        0,
        1,
        (UINT64_C(1) << 32)
            | MICROS_GRANT_PERMISSION_READ,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

struct grant_syscall_command_binding {
    enum grant_syscall_command command;
    bool (*arm)(
        struct grant_syscall_test_fixture *fixture,
        const struct grant_syscall_arming_input *input
    );
};

static const struct grant_syscall_command_binding command_bindings[] = {
    {GRANT_COMMAND_IPC_NOTIFY, grant_syscall_test_arm_ipc_notify},
    {GRANT_COMMAND_CREATE_READ, grant_syscall_test_arm_create_read},
    {GRANT_COMMAND_CREATE_WRITE, grant_syscall_test_arm_create_read},
    {GRANT_COMMAND_CREATE_REMOTE_READ_ONLY, grant_syscall_test_arm_create_read},
    {GRANT_COMMAND_CREATE_UNMAPPED, grant_syscall_test_arm_create_read},
    {GRANT_COMMAND_CREATE_STALE, grant_syscall_test_arm_create_read},
    {GRANT_COMMAND_CREATE_UPPER_ENDPOINT, grant_syscall_test_arm_create_upper_endpoint},
    {GRANT_COMMAND_CREATE_UPPER_PERMISSION, grant_syscall_test_arm_create_upper_permission},
    {GRANT_COMMAND_CREATE_UNUSED, grant_syscall_test_arm_create_unused},
    {GRANT_COMMAND_REVOKE_STALE, grant_syscall_test_arm_revoke_stale},
    {GRANT_COMMAND_REVOKE_READ, grant_syscall_test_arm_revoke_stale},
    {GRANT_COMMAND_REVOKE_WRITE, grant_syscall_test_arm_revoke_stale},
    {GRANT_COMMAND_REVOKE_UPPER_TOKEN, grant_syscall_test_arm_revoke_upper_token},
    {GRANT_COMMAND_REVOKE_UNUSED, grant_syscall_test_arm_revoke_unused},
    {GRANT_COMMAND_VALIDATE_READ, grant_syscall_test_arm_validate_read},
    {GRANT_COMMAND_VALIDATE_ZERO, grant_syscall_test_arm_validate_zero},
    {GRANT_COMMAND_VALIDATE_WRONG_DIRECTION, grant_syscall_test_arm_validate_wrong_direction},
    {GRANT_COMMAND_VALIDATE_UNMAPPED, grant_syscall_test_arm_validate_unmapped},
    {GRANT_COMMAND_VALIDATE_OVERFLOW_STALE, grant_syscall_test_arm_validate_overflow_stale},
    {GRANT_COMMAND_VALIDATE_UPPER_PERMISSION, grant_syscall_test_arm_validate_upper_permission},
    {GRANT_COMMAND_COPY_FROM_CROSS, grant_syscall_test_arm_copy_from_cross},
    {GRANT_COMMAND_COPY_TO_CROSS, grant_syscall_test_arm_copy_to_cross},
    {GRANT_COMMAND_COPY_FROM_LOCAL, grant_syscall_test_arm_copy_from_local},
    {GRANT_COMMAND_COPY_TO_LOCAL, grant_syscall_test_arm_copy_to_local},
    {GRANT_COMMAND_COPY_FROM_ZERO, grant_syscall_test_arm_copy_from_zero},
    {GRANT_COMMAND_COPY_TO_ZERO, grant_syscall_test_arm_copy_from_zero},
    {GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION, grant_syscall_test_arm_copy_from_wrong_direction},
    {GRANT_COMMAND_COPY_TO_WRONG_DIRECTION, grant_syscall_test_arm_copy_from_wrong_direction},
    {GRANT_COMMAND_COPY_WRONG_GRANTOR, grant_syscall_test_arm_copy_wrong_grantor},
    {GRANT_COMMAND_COPY_STALE_ENDPOINT, grant_syscall_test_arm_copy_stale_endpoint},
    {GRANT_COMMAND_COPY_STALE_TOKEN, grant_syscall_test_arm_copy_stale_token},
    {GRANT_COMMAND_COPY_RANGE, grant_syscall_test_arm_copy_range},
    {GRANT_COMMAND_COPY_OVERFLOW, grant_syscall_test_arm_copy_overflow},
    {GRANT_COMMAND_COPY_OVERSIZE, grant_syscall_test_arm_copy_oversize},
    {GRANT_COMMAND_COPY_LOCAL_UNMAPPED, grant_syscall_test_arm_copy_local_unmapped},
    {GRANT_COMMAND_COPY_REMOTE_UNMAPPED, grant_syscall_test_arm_copy_remote_unmapped},
    {GRANT_COMMAND_COPY_LOCAL_PERMISSION, grant_syscall_test_arm_copy_local_permission},
    {GRANT_COMMAND_COPY_REMOTE_PERMISSION, grant_syscall_test_arm_copy_remote_permission},
    {GRANT_COMMAND_COPY_UPPER_ENDPOINT, grant_syscall_test_arm_copy_upper_endpoint},
    {GRANT_COMMAND_COPY_UPPER_TOKEN, grant_syscall_test_arm_copy_upper_token},
    {GRANT_COMMAND_COPY_UNUSED, grant_syscall_test_arm_copy_unused},
    {GRANT_COMMAND_UNKNOWN_OPERATION, grant_syscall_test_arm_unknown_operation},
    {GRANT_COMMAND_BOOTSTRAP_UNAVAILABLE, grant_syscall_test_arm_bootstrap_unavailable},
    {GRANT_COMMAND_COPY_WRONG_GRANTEE, grant_syscall_test_arm_copy_wrong_grantee},
    {GRANT_COMMAND_COPY_REVOKED, grant_syscall_test_arm_copy_revoked},
    {GRANT_COMMAND_COPY_REUSED_ENDPOINT, grant_syscall_test_arm_copy_reused_endpoint},
};

bool grant_syscall_test_arm_command(
    struct grant_syscall_test_fixture *fixture,
    size_t actor,
    struct micros_user_context *context,
    enum grant_syscall_command command
)
{
    uint64_t stale_endpoint =
        (uint64_t)fixture->endpoints[GRANT_SYSCALL_GRANTOR]
        + (UINT64_C(1) << MICROS_ENDPOINT_SLOT_BITS);

    const struct grant_syscall_arming_input input = {
        .actor = actor,
        .context = context,
        .command = command,
        .stale_endpoint = stale_endpoint,
    };
    size_t index;

    for (index = 0; index < sizeof(command_bindings) / sizeof(command_bindings[0]); ++index) {
        if (command_bindings[index].command == command) {
            return command_bindings[index].arm(fixture, &input);
        }
    }
    return false;
}
