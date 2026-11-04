#ifndef MICROS_KERNEL_PANIC_TRACE_H
#define MICROS_KERNEL_PANIC_TRACE_H

#include <stdint.h>

void micros_panic_trace_report(void);
uintptr_t micros_panic_trace_frame_pointer(void);
uint64_t micros_panic_trace_load_word(uintptr_t address);

#endif
