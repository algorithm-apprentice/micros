#ifndef MICROS_ARCH_RISCV64_PLATFORM_H
#define MICROS_ARCH_RISCV64_PLATFORM_H

#include <stdint.h>

enum {
    MICROS_RISCV_PLIC_PRIORITY_PAGE_BASE = 0x0c000000,
    MICROS_RISCV_PLIC_UART0_PRIORITY = 0x0c000028,
    MICROS_RISCV_PLIC_SUPERVISOR_ENABLE_PAGE_BASE = 0x0c002000,
    MICROS_RISCV_PLIC_SUPERVISOR_ENABLE_WORD = 0x0c002080,
    MICROS_RISCV_PLIC_SUPERVISOR_CONTEXT_PAGE_BASE = 0x0c201000,
    MICROS_RISCV_PLIC_SUPERVISOR_THRESHOLD = 0x0c201000,
    MICROS_RISCV_PLIC_SUPERVISOR_CLAIM_COMPLETE = 0x0c201004,
    MICROS_RISCV_UART0_BASE = 0x10000000,
    SBI_RESET_TYPE_SHUTDOWN = 0,
    SBI_RESET_REASON_NONE = 0,
    SBI_RESET_REASON_SYSTEM_FAILURE = 1,
};

void uart_write(const char *text);
void uart_write_hex64(uint64_t value);
void uart_flush(void);
void uart_panic_seize(void);
intptr_t sbi_set_timer(uint64_t absolute_time);
intptr_t sbi_system_reset(uint32_t reset_type, uint32_t reset_reason);
uint64_t riscv_read_time(void);

#endif
