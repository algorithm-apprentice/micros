#include "grant_syscall_test_internal.h"

static bool grant_syscall_test_effect_copy_from_cross(
    struct grant_syscall_test_fixture *fixture
)
{
    return (
        grant_syscall_test_pattern_matches(
            fixture,
            GRANT_SYSCALL_GRANTEE,
            TEST_DATA_SECOND_ADDRESS - 32,
            128,
            UINT8_C(0x10)
        )
        && grant_syscall_test_range_is_value(
            fixture,
            GRANT_SYSCALL_GRANTEE,
            TEST_DATA_SECOND_ADDRESS - 33,
            1,
            UINT8_C(0xcc)
        )
        && grant_syscall_test_range_is_value(
            fixture,
            GRANT_SYSCALL_GRANTEE,
            TEST_DATA_SECOND_ADDRESS + 96,
            1,
            UINT8_C(0xcc)
        )
    );
}

static bool grant_syscall_test_effect_copy_to_cross(
    struct grant_syscall_test_fixture *fixture
)
{
    return (
        grant_syscall_test_pattern_matches(
            fixture,
            GRANT_SYSCALL_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 48,
            96,
            UINT8_C(0x50)
        )
        && grant_syscall_test_range_is_value(
            fixture,
            GRANT_SYSCALL_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 49,
            1,
            UINT8_C(0xdd)
        )
        && grant_syscall_test_range_is_value(
            fixture,
            GRANT_SYSCALL_GRANTOR,
            TEST_DATA_SECOND_ADDRESS + 48,
            1,
            UINT8_C(0xdd)
        )
    );
}

static bool grant_syscall_test_effect_copy_from_local(
    struct grant_syscall_test_fixture *fixture
)
{
    return (
        grant_syscall_test_pattern_matches(
            fixture,
            GRANT_SYSCALL_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x100),
            32,
            UINT8_C(0x90)
        )
        && grant_syscall_test_range_is_value(
            fixture,
            GRANT_SYSCALL_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x0ff),
            1,
            UINT8_C(0xcc)
        )
        && grant_syscall_test_range_is_value(
            fixture,
            GRANT_SYSCALL_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x120),
            1,
            UINT8_C(0xcc)
        )
    );
}

static bool grant_syscall_test_effect_copy_to_local(
    struct grant_syscall_test_fixture *fixture
)
{
    return (
        grant_syscall_test_pattern_matches(
            fixture,
            GRANT_SYSCALL_GRANTOR,
            TEST_DATA_SECOND_ADDRESS + UINT64_C(0x40),
            32,
            UINT8_C(0xa0)
        )
        && grant_syscall_test_range_is_value(
            fixture,
            GRANT_SYSCALL_GRANTOR,
            TEST_DATA_SECOND_ADDRESS + UINT64_C(0x3f),
            1,
            UINT8_C(0xee)
        )
        && grant_syscall_test_range_is_value(
            fixture,
            GRANT_SYSCALL_GRANTOR,
            TEST_DATA_SECOND_ADDRESS + UINT64_C(0x60),
            1,
            UINT8_C(0xee)
        )
    );
}

bool grant_syscall_test_validate_command_effect(
    struct grant_syscall_test_fixture *fixture,
    size_t actor,
    enum grant_syscall_command command,
    const struct micros_trap_frame *frame
)
{
    const struct micros_endpoint_record *grantee_endpoint;

    switch (command) {
    case GRANT_COMMAND_IPC_NOTIFY:
        grantee_endpoint =
            &fixture->endpoint_registry->endpoints[
                fixture->processes[GRANT_SYSCALL_GRANTEE].slot
            ];
        return (
            grantee_endpoint->pending_notification_sources
                == (
                    UINT64_C(1)
                    << fixture->processes[GRANT_SYSCALL_GRANTOR].slot
                )
            && grantee_endpoint->pending_events[
                fixture->processes[GRANT_SYSCALL_GRANTOR].slot
            ] == UINT64_C(5)
        );
    case GRANT_COMMAND_CREATE_READ:
        fixture->read_grant = (micros_grant_t)frame->a0;
        return true;
    case GRANT_COMMAND_CREATE_WRITE:
        fixture->write_grant = (micros_grant_t)frame->a0;
        return fixture->write_grant != fixture->read_grant;
    case GRANT_COMMAND_CREATE_REMOTE_READ_ONLY:
        fixture->remote_read_only_grant = (micros_grant_t)frame->a0;
        return (
            fixture->remote_read_only_grant != fixture->read_grant
            && fixture->remote_read_only_grant != fixture->write_grant
        );
    case GRANT_COMMAND_CREATE_UNMAPPED:
        fixture->unmapped_grant = (micros_grant_t)frame->a0;
        return (
            fixture->unmapped_grant != fixture->read_grant
            && fixture->unmapped_grant != fixture->write_grant
            && fixture->unmapped_grant != fixture->remote_read_only_grant
        );
    case GRANT_COMMAND_CREATE_STALE:
        fixture->stale_grant = (micros_grant_t)frame->a0;
        return (
            fixture->stale_grant != fixture->read_grant
            && fixture->stale_grant != fixture->write_grant
            && fixture->stale_grant != fixture->remote_read_only_grant
            && fixture->stale_grant != fixture->unmapped_grant
        );
    case GRANT_COMMAND_COPY_FROM_CROSS:
        return grant_syscall_test_effect_copy_from_cross(fixture);
    case GRANT_COMMAND_COPY_TO_CROSS:
        return grant_syscall_test_effect_copy_to_cross(fixture);
    case GRANT_COMMAND_COPY_FROM_LOCAL:
        return grant_syscall_test_effect_copy_from_local(fixture);
    case GRANT_COMMAND_COPY_TO_LOCAL:
        return grant_syscall_test_effect_copy_to_local(fixture);
    case GRANT_COMMAND_REVOKE_STALE:
    case GRANT_COMMAND_REVOKE_READ:
    case GRANT_COMMAND_REVOKE_WRITE:
        return frame->a0 == MICROS_SYSCALL_ABI_OK;
    case GRANT_COMMAND_COPY_REUSED_ENDPOINT:
        return (
            actor == GRANT_SYSCALL_GRANTEE
            && fixture->endpoints[GRANT_SYSCALL_GRANTOR]
                != fixture->old_grantor_endpoint
        );
    default:
        return true;
    }
}
