#include "micros/timer.h"

#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"

struct micros_timer_state {
    bool initialized;
    volatile bool active;
    uint64_t interval;
    uint64_t deadline;
    volatile uint64_t ticks;
#ifdef MICROS_BUILD_TIMER_TEST
    uint64_t stop_after;
#endif
};

static struct micros_timer_state timer_state;

bool micros_timer_initialize(void)
{
    uintptr_t saved_status = riscv_irq_save();
    bool timer_interrupt_was_enabled =
        riscv_timer_interrupt_is_enabled();

    riscv_timer_interrupt_disable();
    if (sbi_set_timer(UINT64_MAX) != 0) {
        if (timer_interrupt_was_enabled) {
            riscv_timer_interrupt_enable();
        }
        riscv_irq_restore(saved_status);
        return false;
    }

    timer_state.initialized = true;
    timer_state.active = false;
    timer_state.interval = 0;
    timer_state.deadline = UINT64_MAX;
    timer_state.ticks = 0;
#ifdef MICROS_BUILD_TIMER_TEST
    timer_state.stop_after = 0;
#endif

    riscv_irq_restore(saved_status);
    return true;
}

bool micros_timer_start(uint64_t interval)
{
    uintptr_t saved_status = riscv_irq_save();
    uint64_t current_time;
    uint64_t deadline;

    if (
        !timer_state.initialized
        || timer_state.active
        || interval == 0
    ) {
        riscv_irq_restore(saved_status);
        return false;
    }

    current_time = riscv_read_time();
    if (UINT64_MAX - current_time < interval) {
        riscv_irq_restore(saved_status);
        return false;
    }
    deadline = current_time + interval;
    if (sbi_set_timer(deadline) != 0) {
        riscv_irq_restore(saved_status);
        return false;
    }

    timer_state.interval = interval;
    timer_state.deadline = deadline;
    timer_state.active = true;
    riscv_timer_interrupt_enable();
    riscv_irq_restore(saved_status);
    return true;
}

bool micros_timer_stop(void)
{
    uintptr_t saved_status = riscv_irq_save();

    if (!timer_state.initialized || !timer_state.active) {
        riscv_irq_restore(saved_status);
        return false;
    }

    riscv_timer_interrupt_disable();
    if (sbi_set_timer(UINT64_MAX) != 0) {
        riscv_timer_interrupt_enable();
        riscv_irq_restore(saved_status);
        return false;
    }

    timer_state.deadline = UINT64_MAX;
    timer_state.active = false;
    riscv_irq_restore(saved_status);
    return true;
}

uint64_t micros_timer_ticks(void)
{
    uintptr_t saved_status = riscv_irq_save();
    uint64_t ticks = timer_state.ticks;

    riscv_irq_restore(saved_status);
    return ticks;
}

enum micros_timer_interrupt_result micros_timer_handle_interrupt(void)
{
    uint64_t current_time;
    uint64_t next_deadline;
    uint64_t next_ticks;

    if (!timer_state.initialized || !timer_state.active) {
        return MICROS_TIMER_INTERRUPT_INACTIVE;
    }

    current_time = riscv_read_time();
    if (current_time < timer_state.deadline) {
        return MICROS_TIMER_INTERRUPT_HANDLED_SPURIOUS;
    }
    if (timer_state.ticks == UINT64_MAX) {
        return MICROS_TIMER_INTERRUPT_TICK_OVERFLOW;
    }

    next_ticks = timer_state.ticks + 1;
    timer_state.ticks = next_ticks;

#ifdef MICROS_BUILD_TIMER_TEST
    if (
        timer_state.stop_after != 0
        && next_ticks == timer_state.stop_after
    ) {
        riscv_timer_interrupt_disable();
        if (sbi_set_timer(UINT64_MAX) != 0) {
            return MICROS_TIMER_INTERRUPT_REARM_FAILED;
        }
        timer_state.deadline = UINT64_MAX;
        timer_state.active = false;
        return MICROS_TIMER_INTERRUPT_HANDLED;
    }
#endif

    if (UINT64_MAX - current_time < timer_state.interval) {
        return MICROS_TIMER_INTERRUPT_REARM_FAILED;
    }
    next_deadline = current_time + timer_state.interval;
    if (sbi_set_timer(next_deadline) != 0) {
        return MICROS_TIMER_INTERRUPT_REARM_FAILED;
    }
    timer_state.deadline = next_deadline;
    return MICROS_TIMER_INTERRUPT_HANDLED;
}

#ifdef MICROS_BUILD_TIMER_TEST
bool micros_timer_run_self_test(uint64_t interval, uint64_t stop_after)
{
    uintptr_t saved_status;
    bool configured;

    if (
        interval == 0
        || stop_after == 0
        || riscv_irq_is_enabled()
        || !micros_timer_initialize()
    ) {
        return false;
    }

    saved_status = riscv_irq_save();
    configured = timer_state.initialized && !timer_state.active;
    if (configured) {
        timer_state.stop_after = stop_after;
    }
    riscv_irq_restore(saved_status);
    if (!configured || !micros_timer_start(interval)) {
        return false;
    }

    while (timer_state.active) {
        riscv_wait_for_interrupt_with_irq_window();
    }

    return (
        !riscv_irq_is_enabled()
        && !riscv_timer_interrupt_is_enabled()
        && micros_timer_ticks() == stop_after
    );
}
#endif
