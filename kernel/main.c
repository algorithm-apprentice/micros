#include <stdint.h>

void kernel_main(uintptr_t hart_id, uintptr_t fdt_address);

void kernel_main(uintptr_t hart_id, uintptr_t fdt_address)
{
    (void)hart_id;
    (void)fdt_address;

    for (;;) {
        __asm__ volatile("wfi");
    }
}
