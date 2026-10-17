#include "kernel/address_space_handoff_test_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "kernel/grant/grant_runtime_internal.h"
#include "kernel/grant/grant_syscall.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/memory/user_address_space_internal.h"
#include "kernel/vm_snapshot.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_copy.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_buffer.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/sv39.h"
#include "micros/syscall_abi.h"
#include "micros/tty.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

static void handoff_test_arm_syscall_raw(
    struct micros_address_space_handoff_test_context *fixture,
    size_t actor,
    struct micros_user_context *context,
    enum handoff_syscall_command command,
    uint64_t operation,
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t expected_result,
    bool token_result,
    bool preserve_state
)
{
    struct handoff_syscall_script *script =
        &fixture->syscall_scripts[actor];

    script->armed = true;
    script->awaiting = false;
    script->token_result = token_result;
    script->preserve_state = preserve_state;
    script->command = command;
    script->expected_result = expected_result;
    context->a0 = a0;
    context->a1 = a1;
    context->a2 = a2;
    context->a3 = a3;
    context->a4 = a4;
    context->a5 = 0;
    context->a6 = 0;
    context->a7 = operation;
    context->sepc = handoff_test_user_address_of(fixture,
        micros_address_space_handoff_payload_ecall
    );
}

static bool arm_create_commands(
    struct micros_address_space_handoff_test_context *fixture,
    size_t actor,
    struct micros_user_context *context,
    enum handoff_syscall_command command
)
{
    switch (command) {
    case HANDOFF_SYSCALL_CREATE_READ:
    case HANDOFF_SYSCALL_CREATE_WRITE:
        handoff_test_arm_syscall_raw(fixture,
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_CREATE,
            fixture->endpoints[HANDOFF_GRANTEE],
            TEST_DATA_SECOND_ADDRESS - 64,
            256,
            command == HANDOFF_SYSCALL_CREATE_READ
                ? MICROS_GRANT_PERMISSION_READ
                : MICROS_GRANT_PERMISSION_WRITE,
            0,
            0,
            true,
            false
        );
        return true;
    default:
        return false;
    }
}

static bool arm_copy_from_commands(
    struct micros_address_space_handoff_test_context *fixture,
    size_t actor,
    struct micros_user_context *context,
    enum handoff_syscall_command command
)
{
    switch (command) {
    case HANDOFF_SYSCALL_COPY_FROM_LOCAL:
        handoff_test_write_pattern(fixture,
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS,
            32,
            UINT8_C(0x31)
        );
        handoff_test_fill_bytes(fixture,
            (void *)(uintptr_t)handoff_test_physical_for(fixture,
                HANDOFF_GRANTEE,
                TEST_DATA_ADDRESS + UINT64_C(0x0ff)
            ),
            UINT8_C(0xcc),
            34
        );
        handoff_test_arm_syscall_raw(fixture,
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->syscall_read_grant,
            64,
            TEST_DATA_ADDRESS + UINT64_C(0x100),
            32,
            MICROS_SYSCALL_ABI_OK,
            false,
            false
        );
        return true;
    case HANDOFF_SYSCALL_COPY_FROM_CROSS:
        handoff_test_write_pattern(fixture,
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 64,
            128,
            UINT8_C(0x41)
        );
        handoff_test_fill_bytes(fixture,
            (void *)(uintptr_t)fixture->user_physical[HANDOFF_GRANTEE][2],
            UINT8_C(0xcc),
            MICROS_SV39_PAGE_SIZE
        );
        handoff_test_fill_bytes(fixture,
            (void *)(uintptr_t)fixture->user_physical[HANDOFF_GRANTEE][3],
            UINT8_C(0xcc),
            MICROS_SV39_PAGE_SIZE
        );
        handoff_test_arm_syscall_raw(fixture,
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->syscall_read_grant,
            0,
            TEST_DATA_SECOND_ADDRESS - 32,
            128,
            MICROS_SYSCALL_ABI_OK,
            false,
            false
        );
        return true;
    default:
        return false;
    }
}

static bool arm_copy_to_commands(
    struct micros_address_space_handoff_test_context *fixture,
    size_t actor,
    struct micros_user_context *context,
    enum handoff_syscall_command command
)
{
    switch (command) {
    case HANDOFF_SYSCALL_COPY_TO_LOCAL:
        handoff_test_write_pattern(fixture,
            HANDOFF_GRANTEE,
            TEST_DATA_ADDRESS + UINT64_C(0x180),
            32,
            UINT8_C(0x51)
        );
        handoff_test_fill_bytes(fixture,
            (void *)(uintptr_t)handoff_test_physical_for(fixture,
                HANDOFF_GRANTOR,
                TEST_DATA_SECOND_ADDRESS + UINT64_C(0x3f)
            ),
            UINT8_C(0xdd),
            34
        );
        handoff_test_arm_syscall_raw(fixture,
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->syscall_write_grant,
            128,
            TEST_DATA_ADDRESS + UINT64_C(0x180),
            32,
            MICROS_SYSCALL_ABI_OK,
            false,
            false
        );
        return true;
    case HANDOFF_SYSCALL_COPY_TO_CROSS:
        handoff_test_write_pattern(fixture,
            HANDOFF_GRANTEE,
            TEST_DATA_SECOND_ADDRESS - 48,
            96,
            UINT8_C(0x61)
        );
        handoff_test_fill_bytes(fixture,
            (void *)(uintptr_t)fixture->user_physical[HANDOFF_GRANTOR][2],
            UINT8_C(0xee),
            MICROS_SV39_PAGE_SIZE
        );
        handoff_test_fill_bytes(fixture,
            (void *)(uintptr_t)fixture->user_physical[HANDOFF_GRANTOR][3],
            UINT8_C(0xee),
            MICROS_SV39_PAGE_SIZE
        );
        handoff_test_arm_syscall_raw(fixture,
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->syscall_write_grant,
            16,
            TEST_DATA_SECOND_ADDRESS - 48,
            96,
            MICROS_SYSCALL_ABI_OK,
            false,
            false
        );
        return true;
    default:
        return false;
    }
}

static bool arm_validation_commands(
    struct micros_address_space_handoff_test_context *fixture,
    size_t actor,
    struct micros_user_context *context,
    enum handoff_syscall_command command
)
{
    switch (command) {
    case HANDOFF_SYSCALL_VALIDATE_READ:
        handoff_test_arm_syscall_raw(fixture,
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_VALIDATE,
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->syscall_read_grant,
            0,
            128,
            MICROS_GRANT_PERMISSION_READ,
            MICROS_SYSCALL_ABI_OK,
            false,
            true
        );
        return true;
    case HANDOFF_SYSCALL_COPY_FROM_ZERO:
    case HANDOFF_SYSCALL_COPY_TO_ZERO:
        handoff_test_arm_syscall_raw(fixture,
            actor,
            context,
            command,
            command == HANDOFF_SYSCALL_COPY_FROM_ZERO
                ? MICROS_SYSCALL_ABI_GRANT_COPY_FROM
                : MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            fixture->endpoints[HANDOFF_GRANTOR],
            command == HANDOFF_SYSCALL_COPY_FROM_ZERO
                ? fixture->syscall_read_grant
                : fixture->syscall_write_grant,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_OK,
            false,
            true
        );
        return true;
    default:
        return false;
    }
}

static bool arm_failure_commands(
    struct micros_address_space_handoff_test_context *fixture,
    size_t actor,
    struct micros_user_context *context,
    enum handoff_syscall_command command
)
{
    switch (command) {
    case HANDOFF_SYSCALL_WRONG_DIRECTION:
        handoff_test_arm_syscall_raw(fixture,
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->syscall_write_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            handoff_test_abi_result(MICROS_SYSCALL_ABI_UNAUTHORIZED),
            false,
            true
        );
        return true;
    case HANDOFF_SYSCALL_RANGE:
        handoff_test_arm_syscall_raw(fixture,
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->syscall_read_grant,
            257,
            TEST_DATA_ADDRESS,
            1,
            handoff_test_abi_result(MICROS_SYSCALL_ABI_RANGE),
            false,
            true
        );
        return true;
    case HANDOFF_SYSCALL_UNMAPPED:
        handoff_test_arm_syscall_raw(fixture,
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->syscall_read_grant,
            0,
            TEST_UNMAPPED_ADDRESS,
            1,
            handoff_test_abi_result(
                MICROS_SYSCALL_ABI_MEMORY_FAULT
            ),
            false,
            true
        );
        return true;
    case HANDOFF_SYSCALL_PERMISSION:
        handoff_test_arm_syscall_raw(fixture,
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            fixture->endpoints[HANDOFF_GRANTOR],
            fixture->syscall_read_grant,
            0,
            TEST_READ_ONLY_ADDRESS,
            1,
            handoff_test_abi_result(
                MICROS_SYSCALL_ABI_MEMORY_FAULT
            ),
            false,
            true
        );
        return true;
    default:
        return false;
    }
}

static bool arm_revoke_commands(
    struct micros_address_space_handoff_test_context *fixture,
    size_t actor,
    struct micros_user_context *context,
    enum handoff_syscall_command command
)
{
    switch (command) {
    case HANDOFF_SYSCALL_REVOKE_READ:
    case HANDOFF_SYSCALL_REVOKE_WRITE:
        handoff_test_arm_syscall_raw(fixture,
            actor,
            context,
            command,
            MICROS_SYSCALL_ABI_GRANT_REVOKE,
            command == HANDOFF_SYSCALL_REVOKE_READ
                ? fixture->syscall_read_grant
                : fixture->syscall_write_grant,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_OK,
            false,
            false
        );
        return true;
    default:
        return false;
    }
}

static bool arm_stale_commands(
    struct micros_address_space_handoff_test_context *fixture,
    size_t actor,
    struct micros_user_context *context,
    enum handoff_syscall_command command
)
{
    switch (command) {
    case HANDOFF_SYSCALL_STALE_READ:
    case HANDOFF_SYSCALL_STALE_WRITE:
        handoff_test_arm_syscall_raw(fixture,
            actor,
            context,
            command,
            command == HANDOFF_SYSCALL_STALE_READ
                ? MICROS_SYSCALL_ABI_GRANT_COPY_FROM
                : MICROS_SYSCALL_ABI_GRANT_COPY_TO,
            fixture->endpoints[HANDOFF_GRANTOR],
            command == HANDOFF_SYSCALL_STALE_READ
                ? fixture->syscall_read_grant
                : fixture->syscall_write_grant,
            0,
            TEST_DATA_ADDRESS,
            1,
            handoff_test_abi_result(
                MICROS_SYSCALL_ABI_STALE_GRANT
            ),
            false,
            true
        );
        return true;
    default:
        return false;
    }
}

static bool handoff_test_arm_syscall(
    struct micros_address_space_handoff_test_context *fixture,
    size_t actor,
    struct micros_user_context *context,
    enum handoff_syscall_command command
)
{
    switch (command) {
    case HANDOFF_SYSCALL_CREATE_READ:
    case HANDOFF_SYSCALL_CREATE_WRITE:
        return arm_create_commands(fixture, actor, context, command);
    case HANDOFF_SYSCALL_COPY_FROM_LOCAL:
    case HANDOFF_SYSCALL_COPY_FROM_CROSS:
        return arm_copy_from_commands(fixture, actor, context, command);
    case HANDOFF_SYSCALL_COPY_TO_LOCAL:
    case HANDOFF_SYSCALL_COPY_TO_CROSS:
        return arm_copy_to_commands(fixture, actor, context, command);
    case HANDOFF_SYSCALL_VALIDATE_READ:
    case HANDOFF_SYSCALL_COPY_FROM_ZERO:
    case HANDOFF_SYSCALL_COPY_TO_ZERO:
        return arm_validation_commands(fixture, actor, context, command);
    case HANDOFF_SYSCALL_WRONG_DIRECTION:
    case HANDOFF_SYSCALL_RANGE:
    case HANDOFF_SYSCALL_UNMAPPED:
    case HANDOFF_SYSCALL_PERMISSION:
        return arm_failure_commands(fixture, actor, context, command);
    case HANDOFF_SYSCALL_REVOKE_READ:
    case HANDOFF_SYSCALL_REVOKE_WRITE:
        return arm_revoke_commands(fixture, actor, context, command);
    case HANDOFF_SYSCALL_STALE_READ:
    case HANDOFF_SYSCALL_STALE_WRITE:
        return arm_stale_commands(fixture, actor, context, command);
    case HANDOFF_SYSCALL_NONE:
        return false;
    }
    return false;
}

static void handoff_test_arm_control(
    struct micros_address_space_handoff_test_context *fixture,
    struct micros_user_context *context
)
{
    context->a0 = TEST_SYSCALL_CONTROL_MAGIC;
    context->a7 = UINT64_MAX;
    context->sepc = handoff_test_user_address_of(fixture,
        micros_address_space_handoff_payload_ecall
    );
}

static bool handoff_test_schedule_syscall(
    struct micros_address_space_handoff_test_context *fixture,
    size_t current,
    size_t next,
    enum handoff_syscall_command command,
    struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_user_context *context;

    if (
        objects == NULL
        || next >= HANDOFF_PROCESS_COUNT
    ) {
        return false;
    }
    context = current == next
        ? (struct micros_user_context *)frame
        : &objects->threads[fixture->threads[next].slot].user_context;
    if (!handoff_test_arm_syscall(fixture, next, context, command)) {
        return false;
    }
    if (current == next) {
        return true;
    }
    fixture->syscall_control_action = HANDOFF_SYSCALL_CONTROL_SWITCH;
    fixture->syscall_control_next = next;
    handoff_test_arm_control(fixture, (struct micros_user_context *)frame);
    return true;
}

static bool handoff_test_finish_syscalls(
    struct micros_address_space_handoff_test_context *fixture,
    struct micros_trap_frame *frame
)
{
    fixture->syscall_control_action = HANDOFF_SYSCALL_CONTROL_FINISH;
    fixture->syscall_control_next = HANDOFF_PROCESS_COUNT;
    handoff_test_arm_control(fixture, (struct micros_user_context *)frame);
    return true;
}

enum next_actor_kind {
    NEXT_ACTOR_CURRENT = 0,
    NEXT_ACTOR_GRANTOR,
    NEXT_ACTOR_GRANTEE,
};

struct handoff_syscall_transition {
    enum handoff_syscall_command next_command;
    enum next_actor_kind next_actor;
};

static bool handoff_test_transition_syscall(
    struct micros_address_space_handoff_test_context *fixture,
    size_t actor,
    enum handoff_syscall_command command,
    struct micros_trap_frame *frame
)
{
    static const struct handoff_syscall_transition transitions[] = {
        [HANDOFF_SYSCALL_CREATE_READ] = {
            HANDOFF_SYSCALL_CREATE_WRITE, NEXT_ACTOR_CURRENT,
        },
        [HANDOFF_SYSCALL_CREATE_WRITE] = {
            HANDOFF_SYSCALL_COPY_FROM_LOCAL, NEXT_ACTOR_GRANTEE,
        },
        [HANDOFF_SYSCALL_COPY_FROM_LOCAL] = {
            HANDOFF_SYSCALL_COPY_FROM_CROSS, NEXT_ACTOR_CURRENT,
        },
        [HANDOFF_SYSCALL_COPY_FROM_CROSS] = {
            HANDOFF_SYSCALL_COPY_TO_LOCAL, NEXT_ACTOR_CURRENT,
        },
        [HANDOFF_SYSCALL_COPY_TO_LOCAL] = {
            HANDOFF_SYSCALL_COPY_TO_CROSS, NEXT_ACTOR_CURRENT,
        },
        [HANDOFF_SYSCALL_COPY_TO_CROSS] = {
            HANDOFF_SYSCALL_VALIDATE_READ, NEXT_ACTOR_CURRENT,
        },
        [HANDOFF_SYSCALL_VALIDATE_READ] = {
            HANDOFF_SYSCALL_COPY_FROM_ZERO, NEXT_ACTOR_CURRENT,
        },
        [HANDOFF_SYSCALL_COPY_FROM_ZERO] = {
            HANDOFF_SYSCALL_COPY_TO_ZERO, NEXT_ACTOR_CURRENT,
        },
        [HANDOFF_SYSCALL_COPY_TO_ZERO] = {
            HANDOFF_SYSCALL_WRONG_DIRECTION, NEXT_ACTOR_CURRENT,
        },
        [HANDOFF_SYSCALL_WRONG_DIRECTION] = {
            HANDOFF_SYSCALL_RANGE, NEXT_ACTOR_CURRENT,
        },
        [HANDOFF_SYSCALL_RANGE] = {
            HANDOFF_SYSCALL_UNMAPPED, NEXT_ACTOR_CURRENT,
        },
        [HANDOFF_SYSCALL_UNMAPPED] = {
            HANDOFF_SYSCALL_PERMISSION, NEXT_ACTOR_CURRENT,
        },
        [HANDOFF_SYSCALL_PERMISSION] = {
            HANDOFF_SYSCALL_REVOKE_READ, NEXT_ACTOR_GRANTOR,
        },
        [HANDOFF_SYSCALL_REVOKE_READ] = {
            HANDOFF_SYSCALL_REVOKE_WRITE, NEXT_ACTOR_CURRENT,
        },
        [HANDOFF_SYSCALL_REVOKE_WRITE] = {
            HANDOFF_SYSCALL_STALE_READ, NEXT_ACTOR_GRANTEE,
        },
        [HANDOFF_SYSCALL_STALE_READ] = {
            HANDOFF_SYSCALL_STALE_WRITE, NEXT_ACTOR_CURRENT,
        },
    };
    const struct handoff_syscall_transition *transition;
    size_t next_actor;

    if (command == HANDOFF_SYSCALL_STALE_WRITE) {
        return handoff_test_finish_syscalls(fixture, frame);
    }
    if (
        command <= HANDOFF_SYSCALL_NONE
        || command >= HANDOFF_SYSCALL_STALE_WRITE
    ) {
        return false;
    }
    transition = &transitions[command];
    next_actor = actor;
    if (transition->next_actor == NEXT_ACTOR_GRANTOR) {
        next_actor = HANDOFF_GRANTOR;
    } else if (transition->next_actor == NEXT_ACTOR_GRANTEE) {
        next_actor = HANDOFF_GRANTEE;
    }
    return handoff_test_schedule_syscall(
        fixture,
        actor,
        next_actor,
        transition->next_command,
        frame
    );
}

static bool handoff_test_validate_syscall_effect(
    struct micros_address_space_handoff_test_context *fixture,
    enum handoff_syscall_command command,
    const struct micros_trap_frame *frame
)
{
    switch (command) {
    case HANDOFF_SYSCALL_CREATE_READ:
        fixture->syscall_read_grant = (micros_grant_t)frame->a0;
        return true;
    case HANDOFF_SYSCALL_CREATE_WRITE:
        fixture->syscall_write_grant = (micros_grant_t)frame->a0;
        return fixture->syscall_write_grant != fixture->syscall_read_grant;
    case HANDOFF_SYSCALL_COPY_FROM_LOCAL:
        return (
            handoff_test_pattern_matches(fixture,
                HANDOFF_GRANTEE,
                TEST_DATA_ADDRESS + UINT64_C(0x100),
                32,
                UINT8_C(0x31)
            )
            && *(const unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
                HANDOFF_GRANTEE,
                TEST_DATA_ADDRESS + UINT64_C(0x0ff)
            ) == UINT8_C(0xcc)
            && *(const unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
                HANDOFF_GRANTEE,
                TEST_DATA_ADDRESS + UINT64_C(0x120)
            ) == UINT8_C(0xcc)
        );
    case HANDOFF_SYSCALL_COPY_FROM_CROSS:
        return handoff_test_pattern_matches(fixture,
            HANDOFF_GRANTEE,
            TEST_DATA_SECOND_ADDRESS - 32,
            128,
            UINT8_C(0x41)
        );
    case HANDOFF_SYSCALL_COPY_TO_LOCAL:
        return (
            handoff_test_pattern_matches(fixture,
                HANDOFF_GRANTOR,
                TEST_DATA_SECOND_ADDRESS + UINT64_C(0x40),
                32,
                UINT8_C(0x51)
            )
            && *(const unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
                HANDOFF_GRANTOR,
                TEST_DATA_SECOND_ADDRESS + UINT64_C(0x3f)
            ) == UINT8_C(0xdd)
            && *(const unsigned char *)(uintptr_t)handoff_test_physical_for(fixture,
                HANDOFF_GRANTOR,
                TEST_DATA_SECOND_ADDRESS + UINT64_C(0x60)
            ) == UINT8_C(0xdd)
        );
    case HANDOFF_SYSCALL_COPY_TO_CROSS:
        return handoff_test_pattern_matches(fixture,
            HANDOFF_GRANTOR,
            TEST_DATA_SECOND_ADDRESS - 48,
            96,
            UINT8_C(0x61)
        );
    default:
        return true;
    }
}

bool micros_address_space_handoff_test_before_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_address_space_handoff_test_context *fixture =
        micros_address_space_handoff_test_context();

    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_endpoint_registry *endpoint_registry =
        micros_ipc_runtime_authoritative_registry();
    struct micros_grant_registry *grant_registry =
        micros_grant_runtime_authoritative_registry();
    size_t actor = handoff_test_current_actor(fixture, hart);
    struct handoff_syscall_script *script;

    if (
        fixture->test_state != HANDOFF_TEST_USER_RUNNING
        || frame == NULL
        || actor >= HANDOFF_PROCESS_COUNT
        || !handoff_test_scheduler_state_valid(fixture, hart, actor, false)
    ) {
        return handoff_test_syscall_mismatch(fixture,
            1,
            actor,
            HANDOFF_SYSCALL_NONE,
            frame
        );
    }
    script = &fixture->syscall_scripts[actor];
    if (
        !script->armed
        || script->awaiting
        || frame->sepc
            != handoff_test_user_address_of(fixture,
                micros_address_space_handoff_payload_ecall
            )
    ) {
        return handoff_test_syscall_mismatch(fixture,
            2,
            actor,
            script->command,
            frame
        );
    }
    if (
        script->preserve_state
        && !handoff_test_snapshot_syscall_state(fixture,
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
    ) {
        return handoff_test_syscall_mismatch(fixture,
            UINT64_C(0x20),
            actor,
            script->command,
            frame
        );
    }
    handoff_test_copy_bytes(fixture,
        &script->captured,
        (const struct micros_user_context *)frame,
        sizeof(script->captured)
    );
    script->armed = false;
    script->awaiting = true;
    return true;
}

bool micros_address_space_handoff_test_after_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
)
{
    struct micros_address_space_handoff_test_context *fixture =
        micros_address_space_handoff_test_context();

    size_t actor = handoff_test_current_actor(fixture, hart);

    return (
        frame != NULL
        && actor < HANDOFF_PROCESS_COUNT
        && fixture->syscall_scripts[actor].awaiting
        && syscall_return == MICROS_SYSCALL_RETURN_NORMAL
    );
}

bool micros_address_space_handoff_test_after_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_address_space_handoff_test_context *fixture =
        micros_address_space_handoff_test_context();

    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_endpoint_registry *endpoint_registry =
        micros_ipc_runtime_authoritative_registry();
    struct micros_grant_registry *grant_registry =
        micros_grant_runtime_authoritative_registry();
    size_t actor = handoff_test_current_actor(fixture, hart);
    struct handoff_syscall_script *script;
    struct micros_user_context expected;
    enum handoff_syscall_command completed;

    if (
        frame == NULL
        || actor >= HANDOFF_PROCESS_COUNT
        || !handoff_test_scheduler_state_valid(fixture, hart, actor, true)
    ) {
        return handoff_test_syscall_mismatch(fixture,
            3,
            actor,
            HANDOFF_SYSCALL_NONE,
            frame
        );
    }
    script = &fixture->syscall_scripts[actor];
    if (!script->awaiting) {
        return handoff_test_syscall_mismatch(fixture,
            4,
            actor,
            script->command,
            frame
        );
    }
    handoff_test_copy_bytes(fixture, &expected, &script->captured, sizeof(expected));
    if (script->token_result) {
        if (
            frame->a0 >= MICROS_GRANT_NONE
            || (int64_t)frame->a0 < 0
        ) {
            return handoff_test_syscall_mismatch(fixture,
                UINT64_C(0x41),
                actor,
                script->command,
                frame
            );
        }
        expected.a0 = frame->a0;
    } else {
        expected.a0 = script->expected_result;
    }
    expected.sepc = handoff_test_user_address_of(fixture,
        micros_address_space_handoff_payload_after_ecall
    );
    if (!handoff_test_bytes_equal(fixture, frame, &expected, sizeof(expected))) {
        return handoff_test_syscall_mismatch(fixture,
            5,
            actor,
            script->command,
            frame
        );
    }
    if (
        script->preserve_state
        && !handoff_test_syscall_state_matches(fixture,
            ledger,
            objects,
            endpoint_registry,
            grant_registry
        )
    ) {
        return handoff_test_syscall_mismatch(fixture,
            UINT64_C(0x50),
            actor,
            script->command,
            frame
        );
    }
    completed = script->command;
    if (!handoff_test_validate_syscall_effect(fixture, completed, frame)) {
        return handoff_test_syscall_mismatch(fixture,
            UINT64_C(0x60),
            actor,
            completed,
            frame
        );
    }
    script->awaiting = false;
    script->command = HANDOFF_SYSCALL_NONE;
    if (!handoff_test_transition_syscall(fixture, actor, completed, frame)) {
        return handoff_test_syscall_mismatch(fixture,
            6,
            actor,
            completed,
            frame
        );
    }
    return true;
}

bool handoff_test_run_real_syscalls(
    struct micros_address_space_handoff_test_context *fixture
)
{
    fixture->failure_stage = 10;
    if (
        !handoff_test_arm_syscall(fixture,
            HANDOFF_GRANTOR,
            &fixture->objects->threads[
                fixture->threads[HANDOFF_GRANTOR].slot
            ].user_context,
            HANDOFF_SYSCALL_CREATE_READ
        )
    ) {
        return false;
    }
    fixture->test_state = HANDOFF_TEST_USER_RUNNING;
    micros_address_space_handoff_test_enter(
        fixture->threads[HANDOFF_GRANTOR].slot,
        fixture->threads[HANDOFF_GRANTOR].generation
    );
    if (
        fixture->test_state != HANDOFF_TEST_SUPERVISOR
        || !handoff_test_bytes_equal(fixture,
            micros_address_space_handoff_test_saved_state,
            micros_address_space_handoff_test_restored_state,
            sizeof(uint64_t) * 17
        )
        || micros_user_address_space_validate(
            fixture->processes[HANDOFF_GRANTOR]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_validate(
            fixture->processes[HANDOFF_GRANTEE]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || fixture->syscall_scripts[HANDOFF_GRANTOR].armed
        || fixture->syscall_scripts[HANDOFF_GRANTOR].awaiting
        || fixture->syscall_scripts[HANDOFF_GRANTEE].armed
        || fixture->syscall_scripts[HANDOFF_GRANTEE].awaiting
        || micros_grant_inspect(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTOR],
            fixture->syscall_read_grant,
            &fixture->grant_record
        ) != MICROS_GRANT_ERROR_STALE_GRANT
        || micros_grant_inspect(
            fixture->grant_registry,
            fixture->endpoint_registry,
            fixture->objects,
            fixture->processes[HANDOFF_GRANTOR],
            fixture->syscall_write_grant,
            &fixture->grant_record
        ) != MICROS_GRANT_ERROR_STALE_GRANT
        || fixture->objects->processes[fixture->empty_process.slot].slot_state
            != MICROS_KERNEL_OBJECT_SLOT_LIVE
        || fixture->objects->processes[fixture->empty_process.slot].generation
            != fixture->empty_process.generation
        || fixture->objects->processes[fixture->empty_process.slot].address_space_root != 0
        || fixture->objects->processes[fixture->empty_process.slot].live_thread_count != 0
        || fixture->objects->processes[fixture->empty_process.slot].primary_endpoint
            != MICROS_PROCESS_ENDPOINT_NONE
        || micros_frame_ownership_runtime_validate(fixture->objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        return false;
    }

    if (
        (
            fixture->objects->threads[
                fixture->threads[HANDOFF_GRANTEE].slot
            ].scheduler_assigned
            && micros_thread_scheduler_remove(
                fixture->objects,
                fixture->threads[HANDOFF_GRANTEE]
            ) != MICROS_KERNEL_OBJECT_OK
        )
        || fixture->objects->threads[
            fixture->threads[HANDOFF_GRANTEE].slot
        ].runtime_flags != MICROS_THREAD_RTS_INACTIVE
    ) {
        return false;
    }
    fixture->objects->processes[
        fixture->processes[HANDOFF_GRANTEE].slot
    ].privilege_profile = 0;
    fixture->endpoint_registry->endpoints[
        fixture->processes[HANDOFF_GRANTEE].slot
    ].state = MICROS_ENDPOINT_STATE_RESERVED;
    if (
        micros_tty_handoff_runtime_validate(
            &fixture->tty_bootstrap,
            fixture->endpoint_registry,
            fixture->objects
        ) != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_runtime_begin(100, 100)
            != MICROS_TTY_HANDOFF_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
    ) {
        return false;
    }


    return true;
}
