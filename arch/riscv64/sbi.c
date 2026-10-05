#include "arch/riscv64/platform.h"

enum {
    SBI_EXT_SYSTEM_RESET = 0x53525354,
    SBI_SYSTEM_RESET = 0,
};

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
