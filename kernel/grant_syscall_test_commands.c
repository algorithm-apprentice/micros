#include "grant_syscall_test_internal.h"

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

    switch (command) {
    case GRANT_COMMAND_IPC_NOTIFY:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_CREATE_READ:
    case GRANT_COMMAND_CREATE_WRITE:
    case GRANT_COMMAND_CREATE_REMOTE_READ_ONLY:
    case GRANT_COMMAND_CREATE_UNMAPPED:
    case GRANT_COMMAND_CREATE_STALE:
    {
        uint64_t base = TEST_DATA_SECOND_ADDRESS - 64;
        uint64_t length = 256;
        uint64_t permissions =
            command == GRANT_COMMAND_CREATE_WRITE
                ? MICROS_GRANT_PERMISSION_WRITE
                : MICROS_GRANT_PERMISSION_READ;

        if (command == GRANT_COMMAND_CREATE_REMOTE_READ_ONLY) {
            base = TEST_READ_ONLY_ADDRESS;
            length = 64;
            permissions = MICROS_GRANT_PERMISSION_WRITE;
        } else if (command == GRANT_COMMAND_CREATE_UNMAPPED) {
            base = TEST_UNMAPPED_ADDRESS;
            length = 64;
        } else if (command == GRANT_COMMAND_CREATE_STALE) {
            base = TEST_DATA_ADDRESS;
            length = 64;
        }
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_CREATE_UPPER_ENDPOINT:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_CREATE_UPPER_PERMISSION:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_CREATE_UNUSED:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_REVOKE_STALE:
    case GRANT_COMMAND_REVOKE_READ:
    case GRANT_COMMAND_REVOKE_WRITE:
    {
        micros_grant_t token = fixture->stale_grant;

        if (command == GRANT_COMMAND_REVOKE_READ) {
            token = fixture->read_grant;
        } else if (command == GRANT_COMMAND_REVOKE_WRITE) {
            token = fixture->write_grant;
        }
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_REVOKE_UPPER_TOKEN:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_REVOKE_UNUSED:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_VALIDATE_READ:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_VALIDATE_ZERO:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_VALIDATE_WRONG_DIRECTION:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_VALIDATE_UNMAPPED:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_VALIDATE_OVERFLOW_STALE:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_VALIDATE_UPPER_PERMISSION:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_FROM_CROSS:
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
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_TO_CROSS:
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
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_FROM_LOCAL:
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
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_TO_LOCAL:
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
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_FROM_ZERO:
    case GRANT_COMMAND_COPY_TO_ZERO:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
            command == GRANT_COMMAND_COPY_FROM_ZERO
                ? MICROS_SYSCALL_ABI_GRANT_COPY_FROM
                : MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            fixture->endpoints[GRANT_SYSCALL_GRANTOR],
            command == GRANT_COMMAND_COPY_FROM_ZERO
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
    case GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION:
    case GRANT_COMMAND_COPY_TO_WRONG_DIRECTION:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
            command == GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION
                ? MICROS_SYSCALL_ABI_GRANT_COPY_FROM
                : MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            fixture->endpoints[GRANT_SYSCALL_GRANTOR],
            command == GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION
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
    case GRANT_COMMAND_COPY_WRONG_GRANTOR:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_STALE_ENDPOINT:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            stale_endpoint,
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
    case GRANT_COMMAND_COPY_STALE_TOKEN:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_RANGE:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_OVERFLOW:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_OVERSIZE:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_LOCAL_UNMAPPED:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_REMOTE_UNMAPPED:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_LOCAL_PERMISSION:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_REMOTE_PERMISSION:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_UPPER_ENDPOINT:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_UPPER_TOKEN:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_UNUSED:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_UNKNOWN_OPERATION:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_BOOTSTRAP_UNAVAILABLE:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_WRONG_GRANTEE:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_REVOKED:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_COPY_REUSED_ENDPOINT:
        grant_syscall_test_arm_raw(
            fixture,
            actor,
            context,
            command,
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
    case GRANT_COMMAND_NONE:
        return false;
    }
    return false;
}
