#ifndef MICROS_ARCH_RISCV64_PLATFORM_H
#define MICROS_ARCH_RISCV64_PLATFORM_H

#include <stdint.h>

enum {
    SBI_RESET_TYPE_SHUTDOWN = 0,
    SBI_RESET_REASON_NONE = 0,
    SBI_RESET_REASON_SYSTEM_FAILURE = 1,
};

void uart_write(const char *text);
void uart_write_hex64(uint64_t value);
void uart_flush(void);
void uart_panic_seize(void);
intptr_t sbi_system_reset(uint32_t reset_type, uint32_t reset_reason);

#endif
