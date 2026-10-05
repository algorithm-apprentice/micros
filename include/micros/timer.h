#ifndef MICROS_TIMER_H
#define MICROS_TIMER_H

#include <stdbool.h>
#include <stdint.h>

enum micros_timer_interrupt_result {
    MICROS_TIMER_INTERRUPT_HANDLED,
    MICROS_TIMER_INTERRUPT_HANDLED_SPURIOUS,
    MICROS_TIMER_INTERRUPT_INACTIVE,
    MICROS_TIMER_INTERRUPT_TICK_OVERFLOW,
    MICROS_TIMER_INTERRUPT_REARM_FAILED,
};

bool micros_timer_initialize(void);
bool micros_timer_start(uint64_t interval);
bool micros_timer_stop(void);
uint64_t micros_timer_ticks(void);
enum micros_timer_interrupt_result micros_timer_handle_interrupt(void);

#ifdef MICROS_BUILD_TIMER_TEST
bool micros_timer_run_self_test(uint64_t interval, uint64_t stop_after);
#endif

#endif
