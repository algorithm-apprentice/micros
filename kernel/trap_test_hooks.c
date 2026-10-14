#include "kernel/trap_internal.h"

#ifdef MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST
#include "kernel/address_space_handoff_test.h"
#endif
#ifdef MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST
#include "kernel/bootstrap_test.h"
#endif
#ifdef MICROS_BUILD_IPC_ECALL_CORE_TEST
#include "kernel/ipc_ecall_test.h"
#endif
#ifdef MICROS_BUILD_IPC_SYSCALL_TEST
#include "kernel/ipc_syscall_test.h"
#endif
#ifdef MICROS_BUILD_IPC_SYSCALL_PANIC_TEST
#include "kernel/ipc_syscall_panic_test.h"
#endif
#ifdef MICROS_BUILD_GRANT_SYSCALL_TEST
#include "kernel/grant_syscall_test.h"
#endif
#ifdef MICROS_BUILD_PM_SERVICE_TEST
#include "kernel/pm_service_test.h"
#endif
#ifdef MICROS_BUILD_RAMFS_SERVICE_TEST
#include "kernel/ramfs_service_test.h"
#endif
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
#include "kernel/tty_service_test.h"
#endif
#ifdef MICROS_BUILD_USER_RUNTIME_TEST
#include "kernel/user_runtime_test.h"
#endif
#ifdef MICROS_BUILD_VFS_SERVICE_TEST
#include "kernel/vfs_service_test.h"
#endif
#if defined(MICROS_BUILD_VM_HANDOFF_TEST) \
    || defined(MICROS_BUILD_VM_SELF_FAULT_TEST) \
    || defined(MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST)
#include "kernel/vm_handoff_test.h"
#endif
#ifdef MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST
#include "kernel/vm_scratch_preemption_test.h"
#endif
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

void trap_test_hooks_pre_capture(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
#ifdef MICROS_BUILD_USER_EXECUTION_TEST
    if (!micros_user_execution_test_pre_capture(hart, frame)) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "user-execution-pre-capture",
            frame
        );
    }
#endif
    (void)hart;
    (void)frame;
}

bool trap_test_hooks_scratch_fault(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer,
    bool user_external,
    uint64_t cause_code
)
{
#ifdef MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST
    if (
        !user_timer
        && !user_external
        && cause_code != MICROS_EXCEPTION_USER_ECALL
    ) {
        enum micros_vm_scratch_preemption_test_trap_result
            result =
                micros_vm_scratch_preemption_test_handle_trap(
                    hart,
                    frame
                );

        if (
            result
                == MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_USER_RETURN
        ) {
            if (
                micros_scheduler_select_user_return(hart, frame)
                    != MICROS_SCHEDULER_OK
            ) {
                MICROS_TRAP_PANIC(
                    hart->hardware_id,
                    "vm-scratch-preemption-return",
                    frame
                );
            }
            return true;
        }
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "vm-scratch-preemption-trap",
            frame
        );
    }
#endif
    (void)hart;
    (void)frame;
    (void)user_timer;
    (void)user_external;
    (void)cause_code;
    return false;
}

bool trap_test_hooks_ramfs_fault(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer,
    bool user_external,
    uint64_t cause_code
)
{
#ifdef MICROS_BUILD_RAMFS_SERVICE_TEST
    if (
        !user_timer
        && !user_external
        && cause_code != MICROS_EXCEPTION_USER_ECALL
    ) {
        enum micros_ramfs_service_test_trap_result result =
            micros_ramfs_service_test_handle_trap(hart, frame);

        if (
            result
                == MICROS_RAMFS_SERVICE_TEST_TRAP_USER_RETURN
        ) {
            if (
                micros_scheduler_select_user_return(hart, frame)
                    != MICROS_SCHEDULER_OK
            ) {
                MICROS_TRAP_PANIC(
                    hart->hardware_id,
                    "ramfs-service-test-return",
                    frame
                );
            }
            return true;
        }
    }
#endif
    (void)hart;
    (void)frame;
    (void)user_timer;
    (void)user_external;
    (void)cause_code;
    return false;
}

void trap_test_hooks_vfs_fault(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer,
    bool user_external,
    uint64_t cause_code
)
{
#ifdef MICROS_BUILD_VFS_SERVICE_TEST
    if (
        !user_timer
        && !user_external
        && cause_code != MICROS_EXCEPTION_USER_ECALL
    ) {
        (void)micros_vfs_service_test_handle_trap(hart, frame);
    }
#endif
    (void)hart;
    (void)frame;
    (void)user_timer;
    (void)user_external;
    (void)cause_code;
}

bool trap_test_hooks_user_faults(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer,
    uint64_t cause_code
)
{
#ifdef MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST
    if (
        !user_timer
        && cause_code != MICROS_EXCEPTION_USER_ECALL
    ) {
        micros_bootstrap_test_handle_trap(hart, frame);
    }
#endif
#ifdef MICROS_BUILD_VM_HANDOFF_TEST
    if (
        !user_timer
        && cause_code != MICROS_EXCEPTION_USER_ECALL
    ) {
        micros_vm_handoff_test_handle_trap(hart, frame);
    }
#endif
#ifdef MICROS_BUILD_PM_SERVICE_TEST
    if (
        !user_timer
        && cause_code != MICROS_EXCEPTION_USER_ECALL
    ) {
        micros_pm_service_test_handle_trap(hart, frame);
    }
#endif
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    if (
        !user_timer
        && cause_code != MICROS_EXCEPTION_USER_ECALL
    ) {
        micros_tty_service_test_handle_trap(hart, frame);
    }
#endif
#ifdef MICROS_BUILD_USER_RUNTIME_TEST
    if (
        !user_timer
        && cause_code != MICROS_EXCEPTION_USER_ECALL
    ) {
        enum micros_user_runtime_test_trap_result result =
            micros_user_runtime_test_handle_trap(hart, frame);

        if (
            result
                == MICROS_USER_RUNTIME_TEST_TRAP_USER_RETURN
        ) {
            if (
                micros_scheduler_select_user_return(hart, frame)
                    != MICROS_SCHEDULER_OK
            ) {
                MICROS_TRAP_PANIC(
                    hart->hardware_id,
                    "user-runtime-test-return",
                    frame
                );
            }
            return true;
        }
        if (
            result
                == MICROS_USER_RUNTIME_TEST_TRAP_SUPERVISOR_RETURN
        ) {
            return true;
        }
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "user-runtime-test-mismatch",
            frame
        );
    }
#endif
    (void)hart;
    (void)frame;
    (void)user_timer;
    (void)cause_code;
    return false;
}

bool trap_test_hooks_control_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer,
    uint64_t cause_code
)
{
#ifdef MICROS_BUILD_IPC_ECALL_CORE_TEST
    if (
        !user_timer
        && cause_code == MICROS_EXCEPTION_USER_ECALL
        && frame->a7 == UINT64_MAX
    ) {
        if (
            !micros_ipc_ecall_core_test_finish_trap(
                hart,
                frame
            )
        ) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "ipc-ecall-core-finish",
                frame
            );
        }
        return true;
    }
#endif
#ifdef MICROS_BUILD_GRANT_SYSCALL_TEST
    if (
        !user_timer
        && cause_code == MICROS_EXCEPTION_USER_ECALL
        && frame->a7 == UINT64_MAX
    ) {
        enum micros_grant_syscall_test_control_result result =
            micros_grant_syscall_test_handle_control_trap(
                hart,
                frame
            );

        if (
            result
                == MICROS_GRANT_SYSCALL_TEST_CONTROL_USER_RETURN
            || result
                == MICROS_GRANT_SYSCALL_TEST_CONTROL_SUPERVISOR_RETURN
        ) {
            return true;
        }
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "grant-syscall-test-control",
            frame
        );
    }
#endif
    (void)hart;
    (void)frame;
    (void)user_timer;
    (void)cause_code;
    return false;
}

bool trap_test_hooks_address_space_handoff(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer,
    uint64_t cause_code
)
{
#ifdef MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST
    if (!user_timer) {
        if (cause_code == MICROS_EXCEPTION_USER_ECALL) {
            if (frame->a7 == UINT64_MAX) {
                enum micros_address_space_handoff_test_trap_result
                    result =
                        micros_address_space_handoff_test_handle_trap(
                            hart,
                            frame
                        );

                if (
                    result
                        == MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_USER_RETURN
                    || result
                        == MICROS_ADDRESS_SPACE_HANDOFF_TEST_TRAP_SUPERVISOR_RETURN
                ) {
                    return true;
                }
            } else {
                enum micros_syscall_return syscall_return;

                if (
                    !micros_address_space_handoff_test_before_ecall(
                        hart,
                        frame
                    )
                ) {
                    MICROS_TRAP_PANIC(
                        hart->hardware_id,
                        "address-space-handoff-syscall-before",
                        frame
                    );
                }
                syscall_return =
                    micros_syscall_handle_user_ecall(hart, frame);
                if (
                    !micros_address_space_handoff_test_after_dispatch(
                        hart,
                        frame,
                        syscall_return
                    )
                ) {
                    MICROS_TRAP_PANIC(
                        hart->hardware_id,
                        "address-space-handoff-syscall-dispatch",
                        frame
                    );
                }
                if (
                    (
                        syscall_return
                            == MICROS_SYSCALL_RETURN_CAPTURED
                            ? micros_scheduler_select_captured_user_return(
                                hart,
                                frame
                            )
                            : micros_scheduler_select_user_return(
                                hart,
                                frame
                            )
                    ) != MICROS_SCHEDULER_OK
                ) {
                    MICROS_TRAP_PANIC(
                        hart->hardware_id,
                        "address-space-handoff-syscall-return",
                        frame
                    );
                }
                if (
                    !micros_address_space_handoff_test_after_return(
                        hart,
                        frame
                    )
                ) {
                    MICROS_TRAP_PANIC(
                        hart->hardware_id,
                        "address-space-handoff-syscall-after",
                        frame
                    );
                }
                return true;
            }
        }
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "address-space-handoff-test-mismatch",
            frame
        );
    }
#endif
    (void)hart;
    (void)frame;
    (void)user_timer;
    (void)cause_code;
    return false;
}

void trap_test_hooks_before_user_timer(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
#ifdef MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST
    if (
        !micros_vm_scratch_preemption_test_before_user_timer(
            hart,
            frame
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "vm-scratch-preemption-timer-entry",
            frame
        );
    }
#endif
#if defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
    if (
        !micros_scheduler_invalid_test_before_user_timer(
            hart,
            frame
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "scheduler-invalid-timer-snapshot",
            frame
        );
    }
#endif
    (void)hart;
    (void)frame;
}

void trap_test_hooks_after_user_timer(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
#ifdef MICROS_BUILD_IPC_SYSCALL_TEST
    if (
        !micros_ipc_syscall_test_before_timer_return(
            hart
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "ipc-syscall-test-timer-prepare",
            frame
        );
    }
#endif
#if defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
    if (
        !micros_scheduler_invalid_test_after_user_timer(
            hart,
            frame
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "scheduler-invalid-timer-mutation",
            frame
        );
    }
#endif
    (void)hart;
    (void)frame;
}

void trap_test_hooks_before_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
#ifdef MICROS_BUILD_USER_RUNTIME_TEST
    if (
        !micros_user_runtime_test_before_ecall(
            hart,
            frame
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "user-runtime-syscall-before",
            frame
        );
    }
#endif
#ifdef MICROS_BUILD_IPC_SYSCALL_TEST
    if (
        !micros_ipc_syscall_test_before_ecall(
            hart,
            frame
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "ipc-syscall-test-before",
            frame
        );
    }
#endif
#ifdef MICROS_BUILD_GRANT_SYSCALL_TEST
    if (
        !micros_grant_syscall_test_before_ecall(
            hart,
            frame
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "grant-syscall-test-before",
            frame
        );
    }
#endif
    (void)hart;
    (void)frame;
}

void trap_test_hooks_after_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
)
{
#ifdef MICROS_BUILD_USER_RUNTIME_TEST
    if (
        !micros_user_runtime_test_after_dispatch(
            hart,
            frame,
            syscall_return
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "user-runtime-syscall-dispatch",
            frame
        );
    }
#endif
#ifdef MICROS_BUILD_GRANT_SYSCALL_TEST
    if (
        !micros_grant_syscall_test_after_dispatch(
            hart,
            frame,
            syscall_return
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "grant-syscall-test-dispatch",
            frame
        );
    }
#endif
#ifdef MICROS_BUILD_IPC_SYSCALL_PANIC_TEST
    if (
        !micros_ipc_syscall_panic_test_before_return(
            hart,
            frame,
            syscall_return
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "ipc-syscall-panic-test",
            frame
        );
    }
#endif
    (void)hart;
    (void)frame;
    (void)syscall_return;
}

void trap_test_hooks_after_syscall_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
)
{
#ifdef MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST
    if (
        !micros_vm_scratch_preemption_test_after_syscall_return(
            hart,
            frame,
            syscall_return
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "vm-scratch-preemption-syscall-return",
            frame
        );
    }
#endif
#ifdef MICROS_BUILD_IPC_SYSCALL_TEST
    if (
        !micros_ipc_syscall_test_after_return(
            hart,
            frame
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "ipc-syscall-test-after",
            frame
        );
    }
#endif
#ifdef MICROS_BUILD_GRANT_SYSCALL_TEST
    if (
        !micros_grant_syscall_test_after_return(
            hart,
            frame
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "grant-syscall-test-after",
            frame
        );
    }
#endif
    (void)hart;
    (void)frame;
    (void)syscall_return;
}

void trap_test_hooks_after_user_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer
)
{
#ifdef MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST
    if (
        user_timer
        && !micros_vm_scratch_preemption_test_after_user_timer(
            hart,
            frame
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "vm-scratch-preemption-timer-return",
            frame
        );
    }
#endif
#ifdef MICROS_BUILD_IPC_SYSCALL_TEST
    if (
        user_timer
        && !micros_ipc_syscall_test_after_timer_return(
            hart,
            frame
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "ipc-syscall-test-timer",
            frame
        );
    }
#endif
    (void)hart;
    (void)frame;
    (void)user_timer;
}

#if defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
void trap_test_hooks_invalid_user_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer
)
{
    if (
        !micros_scheduler_invalid_test_handle_user_trap(
            hart,
            frame,
            user_timer
        )
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "scheduler-invalid-test-mismatch",
            frame
        );
    }
}
#elif defined(MICROS_BUILD_SCHEDULER_TEST)
bool trap_test_hooks_scheduler_user_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer
)
{
    enum micros_scheduler_test_trap_action action =
        micros_scheduler_test_handle_user_trap(
            hart,
            frame,
            user_timer
        );

    if (
        action
            == MICROS_SCHEDULER_TEST_RETURN_SUPERVISOR
    ) {
        return true;
    }
    if (
        action
            == MICROS_SCHEDULER_TEST_CAPTURED_USER_RETURN
    ) {
        if (
            micros_scheduler_select_captured_user_return(
                hart,
                frame
            ) != MICROS_SCHEDULER_OK
        ) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "scheduler-captured-return",
                frame
            );
        }
        return true;
    }
    if (
        action != MICROS_SCHEDULER_TEST_CONTINUE
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "scheduler-test-mismatch",
            frame
        );
    }
    return false;
}
#elif defined(MICROS_BUILD_USER_EXECUTION_TEST)
void trap_test_hooks_user_execution_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    enum micros_user_execution_test_trap_result result;

    result = micros_user_execution_handle_test_trap(
        hart,
        frame
    );
    if (
        result
            == MICROS_USER_EXECUTION_TEST_TRAP_USER_RETURN
    ) {
        if (
            micros_scheduler_select_user_return(hart, frame)
                != MICROS_SCHEDULER_OK
        ) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "scheduler-user-return",
                frame
            );
        }
        return;
    }
    if (
        result
            == MICROS_USER_EXECUTION_TEST_TRAP_SUPERVISOR_RETURN
    ) {
        return;
    }
    MICROS_TRAP_PANIC(
        hart->hardware_id,
        "user-execution-test-mismatch",
        frame
    );
}
#endif

bool trap_test_hooks_supervisor_faults(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
    {
        enum micros_user_address_space_test_trap_result result =
            micros_user_address_space_handle_test_trap(frame);

        if (
            result
                == MICROS_USER_ADDRESS_SPACE_TEST_TRAP_HANDLED
        ) {
            return true;
        }
        if (
            result
                == MICROS_USER_ADDRESS_SPACE_TEST_TRAP_MISMATCH
        ) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "user-address-space-test-mismatch",
                frame
            );
        }
    }
#endif

#ifdef MICROS_BUILD_MMU_TEST
    {
        enum micros_mmu_test_trap_result result =
            micros_kernel_address_space_handle_test_trap(frame);

        if (result == MICROS_MMU_TEST_TRAP_HANDLED) {
            return true;
        }
        if (result == MICROS_MMU_TEST_TRAP_MISMATCH) {
            MICROS_TRAP_PANIC(
                hart->hardware_id,
                "mmu-test-mismatch",
                frame
            );
        }
    }
#endif
    (void)hart;
    (void)frame;
    return false;
}
