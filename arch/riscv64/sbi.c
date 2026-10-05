#include "arch/riscv64/platform.h"

enum {
    SBI_EXT_TIME = 0x54494d45,
    SBI_EXT_SYSTEM_RESET = 0x53525354,
    SBI_SET_TIMER = 0,
    SBI_SYSTEM_RESET = 0,
};

intptr_t sbi_set_timer(uint64_t absolute_time)
{
    register uintptr_t argument0 __asm__("a0") = absolute_time;
    register uintptr_t value __asm__("a1");
    register uintptr_t function_id __asm__("a6") = SBI_SET_TIMER;
    register uintptr_t extension_id __asm__("a7") = SBI_EXT_TIME;

    __asm__ volatile(
        "ecall"
        : "+r"(argument0), "=r"(value)
        : "r"(function_id), "r"(extension_id)
        : "memory");

    (void)value;
    return (intptr_t)argument0;
}

intptr_t sbi_system_reset(uint32_t reset_type, uint32_t reset_reason)
{
    register uintptr_t argument0 __asm__("a0") = reset_type;
    register uintptr_t argument1 __asm__("a1") = reset_reason;
    register uintptr_t function_id __asm__("a6") = SBI_SYSTEM_RESET;
    register uintptr_t extension_id __asm__("a7") = SBI_EXT_SYSTEM_RESET;

    __asm__ volatile(
        "ecall"
        : "+r"(argument0), "+r"(argument1)
        : "r"(function_id), "r"(extension_id)
        : "memory");

    return (intptr_t)argument0;
}

uint64_t riscv_read_time(void)
{
    uint64_t time;

    __asm__ volatile("rdtime %0" : "=r"(time));
    return time;
}
