#ifndef MICROS_SCHEDULER_H
#define MICROS_SCHEDULER_H

#include "micros/scheduler_core.h"

struct micros_hart;
struct micros_trap_frame;

enum micros_scheduler_error {
    MICROS_SCHEDULER_OK = 0,
    MICROS_SCHEDULER_ERROR_NOT_INITIALIZED,
    MICROS_SCHEDULER_ERROR_STATE,
    MICROS_SCHEDULER_ERROR_CONTEXT,
    MICROS_SCHEDULER_ERROR_IDLE,
    MICROS_SCHEDULER_ERROR_INVARIANT,
};

enum micros_scheduler_error micros_scheduler_user_trap_enter(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
);

enum micros_scheduler_error micros_scheduler_select_user_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

#ifdef MICROS_BUILD_USER_EXECUTION_TEST
_Noreturn void micros_scheduler_test_enter_without_timer(
    struct micros_thread_handle thread
);

enum micros_scheduler_error micros_scheduler_test_prepare_supervisor_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);
#endif

#endif
