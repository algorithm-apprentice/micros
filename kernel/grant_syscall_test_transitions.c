#include "grant_syscall_test_internal.h"

static bool grant_syscall_test_schedule_next(
    struct grant_syscall_test_fixture *fixture,
    size_t current,
    size_t next,
    enum grant_syscall_command command,
    struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_user_context *context;

    if (
        objects == NULL
        || next >= GRANT_SYSCALL_PROCESS_COUNT
        || !fixture->thread_live[next]
    ) {
        return false;
    }
    context = current == next
        ? (struct micros_user_context *)frame
        : &objects->threads[fixture->threads[next].slot].user_context;
    if (!grant_syscall_test_arm_command(fixture, next, context, command)) {
        return false;
    }
    if (current == next) {
        return true;
    }
    fixture->control_action = GRANT_CONTROL_SWITCH;
    fixture->control_next_actor = next;
    fixture->control_next_command = command;
    grant_syscall_test_arm_control((struct micros_user_context *)frame);
    return true;
}

static bool grant_syscall_test_schedule_reuse(
    struct grant_syscall_test_fixture *fixture,
    struct micros_trap_frame *frame
)
{
    fixture->control_action = GRANT_CONTROL_REUSE_GRANTOR;
    fixture->control_next_actor = GRANT_SYSCALL_GRANTEE;
    fixture->control_next_command = GRANT_COMMAND_COPY_REUSED_ENDPOINT;
    grant_syscall_test_arm_control((struct micros_user_context *)frame);
    return true;
}

static bool grant_syscall_test_schedule_finish(
    struct grant_syscall_test_fixture *fixture,
    struct micros_trap_frame *frame
)
{
    fixture->control_action = GRANT_CONTROL_FINISH;
    fixture->control_next_actor = GRANT_SYSCALL_PROCESS_COUNT;
    fixture->control_next_command = GRANT_COMMAND_NONE;
    grant_syscall_test_arm_control((struct micros_user_context *)frame);
    return true;
}

bool grant_syscall_test_transition_after_return(
    struct grant_syscall_test_fixture *fixture,
    size_t actor,
    enum grant_syscall_command command,
    struct micros_trap_frame *frame
)
{
    switch (command) {
    case GRANT_COMMAND_IPC_NOTIFY:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_CREATE_READ,
            frame
        );
    case GRANT_COMMAND_CREATE_READ:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_CREATE_WRITE,
            frame
        );
    case GRANT_COMMAND_CREATE_WRITE:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_CREATE_REMOTE_READ_ONLY,
            frame
        );
    case GRANT_COMMAND_CREATE_REMOTE_READ_ONLY:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_CREATE_UNMAPPED,
            frame
        );
    case GRANT_COMMAND_CREATE_UNMAPPED:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_CREATE_STALE,
            frame
        );
    case GRANT_COMMAND_CREATE_STALE:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_CREATE_UPPER_ENDPOINT,
            frame
        );
    case GRANT_COMMAND_CREATE_UPPER_ENDPOINT:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_CREATE_UPPER_PERMISSION,
            frame
        );
    case GRANT_COMMAND_CREATE_UPPER_PERMISSION:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_CREATE_UNUSED,
            frame
        );
    case GRANT_COMMAND_CREATE_UNUSED:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_REVOKE_STALE,
            frame
        );
    case GRANT_COMMAND_REVOKE_STALE:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_REVOKE_UPPER_TOKEN,
            frame
        );
    case GRANT_COMMAND_REVOKE_UPPER_TOKEN:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_REVOKE_UNUSED,
            frame
        );
    case GRANT_COMMAND_REVOKE_UNUSED:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            GRANT_SYSCALL_GRANTEE,
            GRANT_COMMAND_VALIDATE_READ,
            frame
        );
    case GRANT_COMMAND_VALIDATE_READ:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_VALIDATE_ZERO,
            frame
        );
    case GRANT_COMMAND_VALIDATE_ZERO:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_VALIDATE_WRONG_DIRECTION,
            frame
        );
    case GRANT_COMMAND_VALIDATE_WRONG_DIRECTION:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_VALIDATE_UNMAPPED,
            frame
        );
    case GRANT_COMMAND_VALIDATE_UNMAPPED:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_VALIDATE_OVERFLOW_STALE,
            frame
        );
    case GRANT_COMMAND_VALIDATE_OVERFLOW_STALE:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_VALIDATE_UPPER_PERMISSION,
            frame
        );
    case GRANT_COMMAND_VALIDATE_UPPER_PERMISSION:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_FROM_CROSS,
            frame
        );
    case GRANT_COMMAND_COPY_FROM_CROSS:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_TO_CROSS,
            frame
        );
    case GRANT_COMMAND_COPY_TO_CROSS:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_FROM_LOCAL,
            frame
        );
    case GRANT_COMMAND_COPY_FROM_LOCAL:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_TO_LOCAL,
            frame
        );
    case GRANT_COMMAND_COPY_TO_LOCAL:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_FROM_ZERO,
            frame
        );
    case GRANT_COMMAND_COPY_FROM_ZERO:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_TO_ZERO,
            frame
        );
    case GRANT_COMMAND_COPY_TO_ZERO:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION,
            frame
        );
    case GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_TO_WRONG_DIRECTION,
            frame
        );
    case GRANT_COMMAND_COPY_TO_WRONG_DIRECTION:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_WRONG_GRANTOR,
            frame
        );
    case GRANT_COMMAND_COPY_WRONG_GRANTOR:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_STALE_ENDPOINT,
            frame
        );
    case GRANT_COMMAND_COPY_STALE_ENDPOINT:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_STALE_TOKEN,
            frame
        );
    case GRANT_COMMAND_COPY_STALE_TOKEN:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_RANGE,
            frame
        );
    case GRANT_COMMAND_COPY_RANGE:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_OVERFLOW,
            frame
        );
    case GRANT_COMMAND_COPY_OVERFLOW:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_OVERSIZE,
            frame
        );
    case GRANT_COMMAND_COPY_OVERSIZE:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_LOCAL_UNMAPPED,
            frame
        );
    case GRANT_COMMAND_COPY_LOCAL_UNMAPPED:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_REMOTE_UNMAPPED,
            frame
        );
    case GRANT_COMMAND_COPY_REMOTE_UNMAPPED:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_LOCAL_PERMISSION,
            frame
        );
    case GRANT_COMMAND_COPY_LOCAL_PERMISSION:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_REMOTE_PERMISSION,
            frame
        );
    case GRANT_COMMAND_COPY_REMOTE_PERMISSION:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_UPPER_ENDPOINT,
            frame
        );
    case GRANT_COMMAND_COPY_UPPER_ENDPOINT:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_UPPER_TOKEN,
            frame
        );
    case GRANT_COMMAND_COPY_UPPER_TOKEN:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_COPY_UNUSED,
            frame
        );
    case GRANT_COMMAND_COPY_UNUSED:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_UNKNOWN_OPERATION,
            frame
        );
    case GRANT_COMMAND_UNKNOWN_OPERATION:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_BOOTSTRAP_UNAVAILABLE,
            frame
        );
    case GRANT_COMMAND_BOOTSTRAP_UNAVAILABLE:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            GRANT_SYSCALL_WRONG,
            GRANT_COMMAND_COPY_WRONG_GRANTEE,
            frame
        );
    case GRANT_COMMAND_COPY_WRONG_GRANTEE:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            GRANT_SYSCALL_GRANTOR,
            GRANT_COMMAND_REVOKE_READ,
            frame
        );
    case GRANT_COMMAND_REVOKE_READ:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            actor,
            GRANT_COMMAND_REVOKE_WRITE,
            frame
        );
    case GRANT_COMMAND_REVOKE_WRITE:
        return grant_syscall_test_schedule_next(
            fixture,
            actor,
            GRANT_SYSCALL_GRANTEE,
            GRANT_COMMAND_COPY_REVOKED,
            frame
        );
    case GRANT_COMMAND_COPY_REVOKED:
        return grant_syscall_test_schedule_reuse(fixture, frame);
    case GRANT_COMMAND_COPY_REUSED_ENDPOINT:
        return grant_syscall_test_schedule_finish(fixture, frame);
    case GRANT_COMMAND_NONE:
        return false;
    }
    return false;
}
