#include "kernel/tests/grant/grant_syscall_test_internal.h"

bool grant_syscall_test_arm_copy_from_cross(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_write_pattern(
        fixture,
        GRANT_SYSCALL_GRANTOR,
        TEST_DATA_SECOND_ADDRESS - 64,
        128,
        UINT8_C(0x10)
    );
    grant_syscall_test_fill_bytes(
        (void *)(uintptr_t)
            fixture->user_physical[GRANT_SYSCALL_GRANTEE][2],
        UINT8_C(0xcc),
        MICROS_SV39_PAGE_SIZE
    );
    grant_syscall_test_fill_bytes(
        (void *)(uintptr_t)
            fixture->user_physical[GRANT_SYSCALL_GRANTEE][3],
        UINT8_C(0xcc),
        MICROS_SV39_PAGE_SIZE
    );
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->read_grant,
        0,
        TEST_DATA_SECOND_ADDRESS - 32,
        128,
        0,
        0,
        MICROS_SYSCALL_ABI_OK,
        false,
        false,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_to_cross(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_write_pattern(
        fixture,
        GRANT_SYSCALL_GRANTEE,
        TEST_DATA_SECOND_ADDRESS - 48,
        96,
        UINT8_C(0x50)
    );
    grant_syscall_test_fill_bytes(
        (void *)(uintptr_t)
            fixture->user_physical[GRANT_SYSCALL_GRANTOR][2],
        UINT8_C(0xdd),
        MICROS_SV39_PAGE_SIZE
    );
    grant_syscall_test_fill_bytes(
        (void *)(uintptr_t)
            fixture->user_physical[GRANT_SYSCALL_GRANTOR][3],
        UINT8_C(0xdd),
        MICROS_SV39_PAGE_SIZE
    );
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_TO,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->write_grant,
        16,
        TEST_DATA_SECOND_ADDRESS - 48,
        96,
        0,
        0,
        MICROS_SYSCALL_ABI_OK,
        false,
        false,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_from_local(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_write_pattern(
        fixture,
        GRANT_SYSCALL_GRANTOR,
        TEST_DATA_SECOND_ADDRESS,
        32,
        UINT8_C(0x90)
    );
    grant_syscall_test_fill_bytes(
        (void *)(uintptr_t)grant_syscall_test_physical_for(
            fixture,
            GRANT_SYSCALL_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x0ff)
        ),
        UINT8_C(0xcc),
        34
    );
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->read_grant,
        64,
        TEST_DATA_ADDRESS + UINT64_C(0x100),
        32,
        0,
        0,
        MICROS_SYSCALL_ABI_OK,
        false,
        false,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_to_local(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_write_pattern(
        fixture,
        GRANT_SYSCALL_GRANTEE,
        TEST_DATA_ADDRESS + UINT64_C(0x180),
        32,
        UINT8_C(0xa0)
    );
    grant_syscall_test_fill_bytes(
        (void *)(uintptr_t)grant_syscall_test_physical_for(
            fixture,
            GRANT_SYSCALL_GRANTOR,
            TEST_DATA_SECOND_ADDRESS + UINT64_C(0x3f)
        ),
        UINT8_C(0xee),
        34
    );
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_TO,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->write_grant,
        128,
        TEST_DATA_ADDRESS + UINT64_C(0x180),
        32,
        0,
        0,
        MICROS_SYSCALL_ABI_OK,
        false,
        false,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_from_zero(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        input->command == GRANT_COMMAND_COPY_FROM_ZERO
            ? MICROS_SYSCALL_ABI_GRANT_COPY_FROM
            : MICROS_SYSCALL_ABI_GRANT_COPY_TO,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        input->command == GRANT_COMMAND_COPY_FROM_ZERO
            ? fixture->read_grant
            : fixture->write_grant,
        0,
        0,
        0,
        0,
        0,
        MICROS_SYSCALL_ABI_OK,
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_from_wrong_direction(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        input->command == GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION
            ? MICROS_SYSCALL_ABI_GRANT_COPY_FROM
            : MICROS_SYSCALL_ABI_GRANT_COPY_TO,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        input->command == GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION
            ? fixture->write_grant
            : fixture->read_grant,
        0,
        TEST_DATA_ADDRESS,
        1,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_UNAUTHORIZED),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_wrong_grantor(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_WRONG],
        fixture->read_grant,
        0,
        TEST_DATA_ADDRESS,
        1,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_UNAUTHORIZED),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_stale_endpoint(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        input->stale_endpoint,
        fixture->read_grant,
        0,
        TEST_DATA_ADDRESS,
        1,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_DEAD_ENDPOINT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_stale_token(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->stale_grant,
        0,
        TEST_DATA_ADDRESS,
        1,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_STALE_GRANT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_range(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->read_grant,
        257,
        TEST_DATA_ADDRESS,
        1,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_RANGE),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_overflow(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->read_grant,
        UINT64_MAX,
        TEST_DATA_ADDRESS,
        2,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_RANGE),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_oversize(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->read_grant,
        0,
        TEST_DATA_ADDRESS,
        MICROS_GRANT_COPY_MAX + 1,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_RANGE),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_local_unmapped(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->read_grant,
        0,
        TEST_UNMAPPED_ADDRESS,
        1,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_MEMORY_FAULT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_remote_unmapped(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->unmapped_grant,
        0,
        TEST_DATA_ADDRESS,
        1,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_MEMORY_FAULT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_local_permission(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->read_grant,
        0,
        TEST_READ_ONLY_ADDRESS,
        1,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_MEMORY_FAULT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_remote_permission(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_TO,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->remote_read_only_grant,
        0,
        TEST_DATA_ADDRESS,
        1,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_MEMORY_FAULT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_upper_endpoint(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        (UINT64_C(1) << 32)
            | fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->read_grant,
        0,
        TEST_DATA_ADDRESS,
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

bool grant_syscall_test_arm_copy_upper_token(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        (UINT64_C(1) << 32) | fixture->read_grant,
        0,
        TEST_DATA_ADDRESS,
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

bool grant_syscall_test_arm_copy_unused(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->read_grant,
        0,
        TEST_DATA_ADDRESS,
        1,
        1,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_unknown_operation(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        UINT64_C(12),
        UINT64_C(0x1111),
        UINT64_C(0x2222),
        UINT64_C(0x3333),
        UINT64_C(0x4444),
        UINT64_C(0x5555),
        UINT64_C(0x6666),
        UINT64_C(0x7777),
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_ARGUMENT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_bootstrap_unavailable(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL,
        MICROS_BOOTSTRAP_COMMAND_RELEASE,
        2,
        0,
        0,
        0,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_UNAUTHORIZED),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_wrong_grantee(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->read_grant,
        0,
        TEST_DATA_ADDRESS,
        1,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_UNAUTHORIZED),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_revoked(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->read_grant,
        0,
        TEST_DATA_ADDRESS,
        1,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_STALE_GRANT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}

bool grant_syscall_test_arm_copy_reused_endpoint(
    struct grant_syscall_test_fixture *fixture,
    const struct grant_syscall_arming_input *input
)
{
    grant_syscall_test_arm_raw(
        fixture,
        input->actor,
        input->context,
        input->command,
        MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
        fixture->endpoints[GRANT_SYSCALL_GRANTOR],
        fixture->unmapped_grant,
        0,
        0,
        0,
        0,
        0,
        grant_syscall_test_abi_result(MICROS_SYSCALL_ABI_STALE_GRANT),
        false,
        true,
        MICROS_SYSCALL_RETURN_NORMAL
    );
    return true;
}
