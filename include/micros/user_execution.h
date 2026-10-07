#ifndef MICROS_USER_EXECUTION_H
#define MICROS_USER_EXECUTION_H

#include <stdbool.h>

#include "micros/kernel_objects.h"
#include "micros/user_context.h"

struct micros_hart;
struct micros_trap_frame;

enum micros_user_execution_error {
    MICROS_USER_EXECUTION_OK = 0,
    MICROS_USER_EXECUTION_ERROR_ARGUMENT,
    MICROS_USER_EXECUTION_ERROR_NOT_INITIALIZED,
    MICROS_USER_EXECUTION_ERROR_PHASE,
    MICROS_USER_EXECUTION_ERROR_STALE,
    MICROS_USER_EXECUTION_ERROR_STATE,
    MICROS_USER_EXECUTION_ERROR_CONTEXT,
    MICROS_USER_EXECUTION_ERROR_STACK,
    MICROS_USER_EXECUTION_ERROR_MAPPING,
    MICROS_USER_EXECUTION_ERROR_BUSY,
    MICROS_USER_EXECUTION_ERROR_INVARIANT,
    MICROS_USER_EXECUTION_ERROR_SATP,
};

enum micros_user_execution_error micros_user_execution_prepare(
    struct micros_thread_handle thread,
    const struct micros_user_context *initial_context
);

enum micros_user_execution_error micros_user_execution_inspect(
    struct micros_thread_handle thread,
    struct micros_user_context *context
);

enum micros_user_execution_error micros_user_execution_detach(
    struct micros_thread_handle thread
);

enum micros_user_execution_error
micros_user_execution_capture_trap(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
);

enum micros_user_execution_error
micros_user_execution_validate_return(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
);

enum micros_user_execution_error
micros_user_execution_validate_context(
    struct micros_thread_handle thread,
    const struct micros_user_context *context
);

enum micros_user_execution_error
micros_user_execution_store_context(
    struct micros_thread_handle thread,
    const struct micros_user_context *context
);

void micros_user_execution_install_return_frame(
    struct micros_trap_frame *frame,
    const struct micros_user_context *context,
    struct micros_hart *hart
);

#if defined(MICROS_BUILD_USER_EXECUTION_TEST) \
    || defined(MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_TEST) \
    || defined(MICROS_BUILD_IPC_ECALL_CORE_TEST) \
    || defined(MICROS_BUILD_IPC_SYSCALL_TEST) \
    || defined(MICROS_BUILD_IPC_SYSCALL_PANIC_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
bool micros_user_execution_test_stack_bounds(
    struct micros_thread_handle thread,
    uintptr_t *stack_bottom,
    uintptr_t *stack_top
);
#endif

#ifdef MICROS_BUILD_USER_EXECUTION_TEST
enum micros_user_execution_test_trap_result {
    MICROS_USER_EXECUTION_TEST_TRAP_INACTIVE = 0,
    MICROS_USER_EXECUTION_TEST_TRAP_USER_RETURN,
    MICROS_USER_EXECUTION_TEST_TRAP_SUPERVISOR_RETURN,
    MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH,
};

bool micros_user_execution_test_pre_capture(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
);

_Noreturn void micros_user_execution_runtime_run_self_test(void);

enum micros_user_execution_test_trap_result
micros_user_execution_handle_test_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);
#endif

#endif
