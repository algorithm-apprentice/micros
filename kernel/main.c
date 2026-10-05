#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "micros/fdt.h"

#ifndef MICROS_VERSION
#error "MICROS_VERSION must be defined by the build"
#endif

void kernel_main(uintptr_t hart_id, uintptr_t fdt_address);

static void write_range(
    const char *event,
    const struct micros_fdt_range *range
)
{
    uart_write(event);
    uart_write(" base=");
    uart_write_hex64(range->base);
    uart_write(" size=");
    uart_write_hex64(range->size);
    uart_write("\n");
}

static void stop_after_reset_failure(void)
{
    uart_write("MICROS_TEST_FAILURE sbi-system-reset-returned\n");
    uart_flush();
    for (;;) {
        __asm__ volatile("wfi");
    }
}

void kernel_main(uintptr_t hart_id, uintptr_t fdt_address)
{
    struct micros_fdt_memory_map memory_map;
    enum micros_fdt_error error;
    size_t index;

    (void)hart_id;

    uart_write("MICROS_BOOT " MICROS_VERSION "\n");
    uart_flush();

    error = micros_fdt_parse_memory_map(
        (const void *)fdt_address,
        MICROS_FDT_MAX_BLOB_SIZE,
        &memory_map
    );
    if (error != MICROS_FDT_OK) {
        uart_write("MICROS_TEST_FAILURE fdt-");
        uart_write(micros_fdt_error_name(error));
        uart_write("\n");
        uart_flush();
        (void)sbi_system_reset(
            SBI_RESET_TYPE_SHUTDOWN,
            SBI_RESET_REASON_SYSTEM_FAILURE
        );
        stop_after_reset_failure();
    }

    for (index = 0; index < memory_map.memory_range_count; ++index) {
        write_range(
            "MICROS_FDT_MEMORY",
            &memory_map.memory_ranges[index]
        );
    }
    for (index = 0; index < memory_map.reservation_range_count; ++index) {
        write_range(
            "MICROS_FDT_RESERVATION",
            &memory_map.reservation_ranges[index]
        );
    }
    for (
        index = 0;
        index < memory_map.reserved_memory_range_count;
        ++index
    ) {
        write_range(
            "MICROS_FDT_RESERVED_MEMORY",
            &memory_map.reserved_memory_ranges[index]
        );
    }
    uart_write("MICROS_FDT_READY\n");
    uart_flush();

    (void)sbi_system_reset(SBI_RESET_TYPE_SHUTDOWN, SBI_RESET_REASON_NONE);
    stop_after_reset_failure();
}
