#include <stdint.h>

#include "arch/riscv64/platform.h"

#ifndef MICROS_VERSION
#error "MICROS_VERSION must be defined by the build"
#endif

void kernel_main(uintptr_t hart_id, uintptr_t fdt_address);

void kernel_main(uintptr_t hart_id, uintptr_t fdt_address)
{
    (void)hart_id;
    (void)fdt_address;

    uart_write("MICROS_BOOT " MICROS_VERSION "\n");
    uart_flush();

    (void)sbi_system_reset(SBI_RESET_TYPE_SHUTDOWN, SBI_RESET_REASON_NONE);

    uart_write("MICROS_TEST_FAILURE sbi-system-reset-returned\n");
    uart_flush();
    for (;;) {
        __asm__ volatile("wfi");
    }
}
