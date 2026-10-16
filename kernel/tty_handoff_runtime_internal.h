#ifndef MICROS_KERNEL_TTY_HANDOFF_RUNTIME_INTERNAL_H
#define MICROS_KERNEL_TTY_HANDOFF_RUNTIME_INTERNAL_H

#include "kernel/tty_handoff_runtime.h"

void tty_handoff_clear_bytes(void *storage, size_t size);

void tty_handoff_copy_bytes(void *destination, const void *source, size_t size);

#endif
