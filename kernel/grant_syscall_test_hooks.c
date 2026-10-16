#include "grant_syscall_test_internal.h"

bool micros_grant_syscall_test_before_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct grant_syscall_test_fixture *fixture = grant_syscall_test_context();

    size_t actor = grant_syscall_test_current_actor(fixture, hart);
    struct grant_syscall_script *script;

    if (
        frame == NULL
        || actor >= GRANT_SYSCALL_PROCESS_COUNT
        || !grant_syscall_test_scheduler_state_valid(fixture, hart, actor, false)
    ) {
        return grant_syscall_test_test_mismatch(
            1,
            actor,
            GRANT_COMMAND_NONE,
            frame
        );
    }
    script = &fixture->scripts[actor];
    if (
        !script->armed
        || script->awaiting
        || frame->sepc
            != grant_syscall_test_user_address_of(
                micros_grant_syscall_payload_ecall
            )
    ) {
        return grant_syscall_test_test_mismatch(2, actor, script->command, frame);
    }
    if (
        script->preserve_state
        && !grant_syscall_test_snapshot_failure_state(fixture)
    ) {
        return grant_syscall_test_test_mismatch(
            UINT64_C(0x20),
            actor,
            script->command,
            frame
        );
    }
    grant_syscall_test_copy_bytes(
        &script->captured,
        (const struct micros_user_context *)frame,
        sizeof(script->captured)
    );
    script->armed = false;
    script->awaiting = true;
    fixture->dispatch_actor = actor;
    return true;
}

bool micros_grant_syscall_test_after_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
)
{
    struct grant_syscall_test_fixture *fixture = grant_syscall_test_context();

    size_t actor = fixture->dispatch_actor;
    uint64_t expected = actor < GRANT_SYSCALL_PROCESS_COUNT
        ? fixture->scripts[actor].expected_return
        : UINT64_MAX;

    if (
        frame == NULL
        || actor >= GRANT_SYSCALL_PROCESS_COUNT
        || !fixture->scripts[actor].awaiting
        || syscall_return != fixture->scripts[actor].expected_return
        || (
            syscall_return == MICROS_SYSCALL_RETURN_NORMAL
            && grant_syscall_test_current_actor(fixture, hart) != actor
        )
    ) {
        uart_write("MICROS_TEST_FAILURE grant-dispatch expected=");
        uart_write_hex64(expected);
        uart_write(" actual=");
        uart_write_hex64(syscall_return);
        uart_write(" actor=");
        uart_write_hex64(actor);
        uart_write(" current=");
        uart_write_hex64(grant_syscall_test_current_actor(fixture, hart));
        uart_write(" a0=");
        uart_write_hex64(frame == NULL ? 0 : frame->a0);
        uart_write("\n");
        uart_flush();
        return false;
    }
    return true;
}

bool micros_grant_syscall_test_after_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct grant_syscall_test_fixture *fixture = grant_syscall_test_context();

    size_t actor = grant_syscall_test_current_actor(fixture, hart);
    struct grant_syscall_script *script;
    struct micros_user_context expected;
    enum grant_syscall_command completed;

    if (
        frame == NULL
        || actor >= GRANT_SYSCALL_PROCESS_COUNT
        || !grant_syscall_test_scheduler_state_valid(fixture, hart, actor, true)
    ) {
        return grant_syscall_test_test_mismatch(
            3,
            actor,
            GRANT_COMMAND_NONE,
            frame
        );
    }
    script = &fixture->scripts[actor];
    if (!script->awaiting) {
        return grant_syscall_test_test_mismatch(4, actor, script->command, frame);
    }
    grant_syscall_test_copy_bytes(&expected, &script->captured, sizeof(expected));
    if (script->token_result) {
        if (
            frame->a0 >= MICROS_GRANT_NONE
            || (int64_t)frame->a0 < 0
        ) {
            return grant_syscall_test_test_mismatch(
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
    expected.sepc =
        grant_syscall_test_user_address_of(
            micros_grant_syscall_payload_after_ecall
        );
    if (!grant_syscall_test_bytes_equal(frame, &expected, sizeof(expected))) {
        return grant_syscall_test_test_mismatch(5, actor, script->command, frame);
    }
    if (
        script->preserve_state
        && !grant_syscall_test_failure_state_matches(fixture)
    ) {
        return grant_syscall_test_test_mismatch(
            UINT64_C(0x50),
            actor,
            script->command,
            frame
        );
    }
    completed = script->command;
    if (!grant_syscall_test_validate_command_effect(fixture, actor, completed, frame)) {
        return grant_syscall_test_test_mismatch(
            UINT64_C(0x60),
            actor,
            completed,
            frame
        );
    }
    script->awaiting = false;
    script->command = GRANT_COMMAND_NONE;
    fixture->dispatch_actor = GRANT_SYSCALL_PROCESS_COUNT;
    if (!grant_syscall_test_transition_after_return(fixture, actor, completed, frame)) {
        return grant_syscall_test_test_mismatch(6, actor, completed, frame);
    }
    return true;
}

enum micros_grant_syscall_test_control_result
micros_grant_syscall_test_handle_control_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct grant_syscall_test_fixture *fixture = grant_syscall_test_context();

    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t actor = grant_syscall_test_current_actor(fixture, hart);
    struct micros_user_context expected;
    const uint64_t control_mask =
        MICROS_RISCV_SSTATUS_SIE
        | MICROS_RISCV_SSTATUS_SPIE
        | MICROS_RISCV_SSTATUS_SPP
        | MICROS_RISCV_SSTATUS_SUM
        | TEST_SSTATUS_UBE
        | TEST_SSTATUS_VS
        | TEST_SSTATUS_FS
        | TEST_SSTATUS_XS
        | TEST_SSTATUS_MXR
        | TEST_SSTATUS_SD;

    if (
        objects == NULL
        || hart == NULL
        || frame == NULL
        || actor >= GRANT_SYSCALL_PROCESS_COUNT
        || frame->a0 != TEST_CONTROL_MAGIC
        || frame->a7 != UINT64_MAX
        || frame->sepc
            != grant_syscall_test_user_address_of(
                micros_grant_syscall_payload_ecall
            )
        || fixture->control_action == GRANT_CONTROL_NONE
    ) {
        return MICROS_GRANT_SYSCALL_TEST_CONTROL_MISMATCH;
    }
    if (fixture->control_action == GRANT_CONTROL_SWITCH) {
        const struct micros_thread *next =
            &objects->threads[fixture->threads[fixture->control_next_actor].slot];

        grant_syscall_test_copy_bytes(&expected, &next->user_context, sizeof(expected));
        if (
            (
                next->scheduler_assigned
                ? micros_thread_runtime_flags_unset(
                    objects,
                    fixture->threads[fixture->control_next_actor],
                    MICROS_THREAD_RTS_INACTIVE
                )
                : micros_thread_scheduler_admit(
                    objects,
                    micros_kernel_object_runtime_boot_hart_handle(),
                    fixture->threads[fixture->control_next_actor],
                    MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
                    UINT64_MAX,
                    true
                )
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_hold(
                objects,
                fixture->threads[actor]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_scheduler_select_user_return(hart, frame)
                != MICROS_SCHEDULER_OK
            || grant_syscall_test_current_actor(fixture, hart) != fixture->control_next_actor
            || !grant_syscall_test_bytes_equal(frame, &expected, sizeof(expected))
            || !grant_syscall_test_scheduler_state_valid(
                fixture,
                hart,
                fixture->control_next_actor,
                true
            )
        ) {
            return MICROS_GRANT_SYSCALL_TEST_CONTROL_MISMATCH;
        }
        fixture->control_action = GRANT_CONTROL_NONE;
        return MICROS_GRANT_SYSCALL_TEST_CONTROL_USER_RETURN;
    }
    if (fixture->control_action == GRANT_CONTROL_REUSE_GRANTOR) {
        if (
            actor != GRANT_SYSCALL_GRANTEE
            || !grant_syscall_test_reuse_grantor(fixture)
            || !grant_syscall_test_arm_command(
                fixture,
                actor,
                (struct micros_user_context *)frame,
                fixture->control_next_command
            )
            || micros_scheduler_select_user_return(hart, frame)
                != MICROS_SCHEDULER_OK
            || grant_syscall_test_current_actor(fixture, hart) != actor
            || !grant_syscall_test_scheduler_state_valid(fixture, hart, actor, true)
        ) {
            return MICROS_GRANT_SYSCALL_TEST_CONTROL_MISMATCH;
        }
        fixture->control_action = GRANT_CONTROL_NONE;
        return MICROS_GRANT_SYSCALL_TEST_CONTROL_USER_RETURN;
    }
    if (
        fixture->control_action != GRANT_CONTROL_FINISH
        || micros_scheduler_test_prepare_supervisor_return(
            hart,
            frame
        ) != MICROS_SCHEDULER_OK
    ) {
        return MICROS_GRANT_SYSCALL_TEST_CONTROL_MISMATCH;
    }
    frame->sp = micros_grant_syscall_test_saved_sp;
    frame->sepc =
        (uintptr_t)micros_grant_syscall_test_supervisor_resume;
    frame->sstatus &= ~control_mask;
    frame->sstatus |= MICROS_RISCV_SSTATUS_SPP;
    fixture->control_action = GRANT_CONTROL_NONE;
    return MICROS_GRANT_SYSCALL_TEST_CONTROL_SUPERVISOR_RETURN;
}
