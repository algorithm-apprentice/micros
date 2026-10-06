#ifndef MICROS_TIMER_H
#define MICROS_TIMER_H

#include <stdbool.h>
#include <stdint.h>

struct micros_hart;

enum micros_timer_interrupt_result {
    MICROS_TIMER_INTERRUPT_HANDLED,
    MICROS_TIMER_INTERRUPT_HANDLED_SPURIOUS,
    MICROS_TIMER_INTERRUPT_INACTIVE,
    MICROS_TIMER_INTERRUPT_TICK_OVERFLOW,
    MICROS_TIMER_INTERRUPT_REARM_FAILED,
};

bool micros_timer_initialize(struct micros_hart *hart);
bool micros_timer_start(struct micros_hart *hart, uint64_t interval);
bool micros_timer_stop(struct micros_hart *hart);
bool micros_timer_prepare_return(struct micros_hart *hart);
uint64_t micros_timer_ticks(const struct micros_hart *hart);
enum micros_timer_interrupt_result micros_timer_handle_interrupt(
    struct micros_hart *hart
);

#ifdef MICROS_BUILD_TIMER_TEST
bool micros_timer_run_self_test(
    struct micros_hart *hart,
    uint64_t interval,
    uint64_t stop_after
);
#endif

#endif
