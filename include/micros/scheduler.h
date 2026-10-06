#ifndef MICROS_SCHEDULER_H
#define MICROS_SCHEDULER_H

#include <stdint.h>

#include "micros/scheduler_core.h"

struct micros_hart;
struct micros_trap_frame;

enum micros_scheduler_error {
    MICROS_SCHEDULER_OK = 0,
    MICROS_SCHEDULER_ERROR_ARGUMENT,
    MICROS_SCHEDULER_ERROR_NOT_INITIALIZED,
    MICROS_SCHEDULER_ERROR_STATE,
    MICROS_SCHEDULER_ERROR_EMPTY,
    MICROS_SCHEDULER_ERROR_TIMER,
    MICROS_SCHEDULER_ERROR_TIMER_INACTIVE,
    MICROS_SCHEDULER_ERROR_TIMER_TICK_OVERFLOW,
    MICROS_SCHEDULER_ERROR_TIMER_REARM_FAILED,
    MICROS_SCHEDULER_ERROR_CONTEXT,
    MICROS_SCHEDULER_ERROR_IDLE,
    MICROS_SCHEDULER_ERROR_INVARIANT,
};

enum micros_scheduler_error micros_scheduler_initialize(
    uint64_t preemption_interval
);

bool micros_scheduler_is_initialized(void);

enum micros_scheduler_error micros_scheduler_admit(
    struct micros_thread_handle thread,
    uint8_t priority,
    uint64_t quantum_counter_ticks
);

enum micros_scheduler_error micros_scheduler_start(void);

enum micros_scheduler_error micros_scheduler_user_trap_enter(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
);

enum micros_scheduler_error micros_scheduler_handle_user_timer(
    struct micros_hart *hart
);

enum micros_scheduler_error micros_scheduler_select_user_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

#ifdef MICROS_BUILD_USER_EXECUTION_TEST
_Noreturn void micros_scheduler_test_enter_without_timer(
    struct micros_thread_handle thread
);
#endif

#if defined(MICROS_BUILD_USER_EXECUTION_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_SWITCH_TEST)
enum micros_scheduler_error micros_scheduler_test_prepare_supervisor_return(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);
#endif

#ifdef MICROS_BUILD_SCHEDULER_SWITCH_TEST
enum micros_scheduler_switch_test_trap_action {
    MICROS_SCHEDULER_SWITCH_TEST_CONTINUE = 0,
    MICROS_SCHEDULER_SWITCH_TEST_RETURN_SUPERVISOR,
    MICROS_SCHEDULER_SWITCH_TEST_MISMATCH,
};

bool micros_scheduler_switch_test_after_start(
    const struct micros_hart *hart
);

enum micros_scheduler_switch_test_trap_action
micros_scheduler_switch_test_handle_user_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer
);

bool micros_scheduler_switch_test_after_user_return(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
);

_Noreturn void micros_scheduler_switch_runtime_run_self_test(void);
#endif

#endif
