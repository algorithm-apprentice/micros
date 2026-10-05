#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "micros/fdt.h"
#include "micros/panic.h"
#include "micros/trap.h"

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

static void write_fdt_counts(
    const struct micros_fdt_memory_map *memory_map
)
{
    uart_write("MICROS_FDT_COUNTS memory=");
    uart_write_hex64((uint64_t)memory_map->memory_range_count);
    uart_write(" reservation=");
    uart_write_hex64((uint64_t)memory_map->reservation_range_count);
    uart_write(" reserved-memory=");
    uart_write_hex64(
        (uint64_t)memory_map->reserved_memory_range_count
    );
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

    micros_trap_install(hart_id);

    uart_write("MICROS_BOOT " MICROS_VERSION "\n");
    uart_write("MICROS_TRAP_READY\n");
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
    write_fdt_counts(&memory_map);
    uart_write("MICROS_FDT_READY\n");
    uart_flush();

#ifdef MICROS_BUILD_PANIC_TEST
    MICROS_PANIC(hart_id, "intentional-test");
#endif

#ifdef MICROS_BUILD_TRAP_TEST
    if (!micros_trap_run_self_test()) {
        MICROS_PANIC(hart_id, "trap-test-restore");
    }
    uart_write(
        "MICROS_TRAP_TEST_PASS "
        "origin=S cause=illegal-instruction registers=preserved\n"
    );
    uart_flush();
#endif

#ifdef MICROS_BUILD_TRAP_PANIC_TEST
    micros_trap_run_panic_test();
#endif

    (void)sbi_system_reset(SBI_RESET_TYPE_SHUTDOWN, SBI_RESET_REASON_NONE);
    stop_after_reset_failure();
}
