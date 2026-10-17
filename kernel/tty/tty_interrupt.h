#ifndef MICROS_KERNEL_TTY_INTERRUPT_H
#define MICROS_KERNEL_TTY_INTERRUPT_H

struct micros_hart;
struct micros_trap_frame;

enum micros_tty_interrupt_result {
    MICROS_TTY_INTERRUPT_SPURIOUS = 0,
    MICROS_TTY_INTERRUPT_HANDLED,
};

enum micros_tty_interrupt_result micros_tty_interrupt_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
);

#endif
