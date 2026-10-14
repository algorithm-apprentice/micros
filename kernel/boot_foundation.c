#include "kernel/boot_internal.h"

#include "arch/riscv64/platform.h"
#include "kernel/plic.h"
#include "micros/bootstrap_memory.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/trap.h"

#ifndef MICROS_VERSION
#error "MICROS_VERSION must be defined by the build"
#endif

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

void boot_initialize_objects(struct boot_context *boot)
{
    if (
        micros_kernel_object_runtime_initialize(boot->hart_id)
        != MICROS_KERNEL_OBJECT_OK
    ) {
        MICROS_PANIC(boot->hart_id, "objects-init");
    }
    if (!micros_trap_install()) {
        MICROS_PANIC(boot->hart_id, "trap-install");
    }
    boot->kernel_objects = micros_kernel_object_runtime_registry();
    boot->boot_hart = micros_kernel_object_runtime_boot_hart();
    if (boot->kernel_objects == NULL || boot->boot_hart == NULL) {
        MICROS_PANIC(boot->hart_id, "objects-state");
    }

    uart_write("MICROS_BOOT " MICROS_VERSION "\n");
    uart_write("MICROS_OBJECTS_READY processes=");
    uart_write_hex64(boot->kernel_objects->live_process_count);
    uart_write(" threads=");
    uart_write_hex64(boot->kernel_objects->live_thread_count);
    uart_write(" harts=");
    uart_write_hex64(boot->kernel_objects->registered_hart_count);
    uart_write(" max-threads=");
    uart_write_hex64(boot->kernel_objects->max_threads_per_process);
    uart_write(" max-harts=");
    uart_write_hex64(boot->kernel_objects->max_harts);
    uart_write(" boot-hart=");
    uart_write_hex64(boot->boot_hart->hardware_id);
    uart_write("\n");
    uart_write("MICROS_TRAP_READY\n");
    uart_flush();
}

void boot_discover_memory(struct boot_context *boot, uintptr_t fdt_address)
{
    enum micros_fdt_error error;
    size_t index;

    error = micros_fdt_parse_memory_map(
        (const void *)fdt_address,
        MICROS_FDT_MAX_BLOB_SIZE,
        &boot->memory_map
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

    for (index = 0; index < boot->memory_map.memory_range_count; ++index) {
        write_range(
            "MICROS_FDT_MEMORY",
            &boot->memory_map.memory_ranges[index]
        );
    }
    for (index = 0; index < boot->memory_map.reservation_range_count; ++index) {
        write_range(
            "MICROS_FDT_RESERVATION",
            &boot->memory_map.reservation_ranges[index]
        );
    }
    for (
        index = 0;
        index < boot->memory_map.reserved_memory_range_count;
        ++index
    ) {
        write_range(
            "MICROS_FDT_RESERVED_MEMORY",
            &boot->memory_map.reserved_memory_ranges[index]
        );
    }
    write_fdt_counts(&boot->memory_map);
    uart_write("MICROS_FDT_READY\n");
    uart_flush();
}

void boot_initialize_memory(struct boot_context *boot)
{
    if (
        micros_bootstrap_memory_initialize(&boot->memory_map)
        != MICROS_FRAME_ALLOCATOR_OK
    ) {
        MICROS_PANIC(boot->hart_id, "frame-allocator-init");
    }
    boot->frame_allocator = micros_bootstrap_frame_allocator();
    if (boot->frame_allocator == NULL) {
        MICROS_PANIC(boot->hart_id, "frame-allocator-state");
    }
    uart_write("MICROS_FRAME_ALLOCATOR_READY managed=");
    uart_write_hex64(boot->frame_allocator->managed_frame_count);
    uart_write(" free=");
    uart_write_hex64(boot->frame_allocator->free_frame_count);
    uart_write("\n");
    uart_flush();

    if (
        micros_frame_ownership_runtime_initialize()
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        MICROS_PANIC(boot->hart_id, "frame-ownership-init");
    }

    if (
        micros_kernel_address_space_initialize()
        != MICROS_KERNEL_ADDRESS_SPACE_OK
    ) {
        MICROS_PANIC(boot->hart_id, "mmu-init");
    }
    if (!micros_plic_initialize()) {
        MICROS_PANIC(boot->hart_id, "plic-init");
    }
    boot->address_space = micros_kernel_address_space_report();
    if (boot->address_space == NULL) {
        MICROS_PANIC(boot->hart_id, "mmu-state");
    }
    uart_write("MICROS_MMU_READY mode=sv39 root=");
    uart_write_hex64(boot->address_space->root_physical_address);
    uart_write(" tables=");
    uart_write_hex64(boot->address_space->table_count);
    uart_write("\n");
    uart_flush();

    boot->frame_ownership = micros_frame_ownership_runtime_ledger();
    if (
        boot->frame_ownership == NULL
        || micros_frame_ownership_runtime_validate(boot->kernel_objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || boot->frame_ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || boot->frame_ownership->owned_frame_count == 0
        || boot->frame_ownership->owned_frame_count
            != boot->address_space->table_count
        || boot->frame_ownership->owner_counts[
            MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
        ] != boot->address_space->table_count
    ) {
        MICROS_PANIC(boot->hart_id, "frame-ownership-state");
    }
    uart_write("MICROS_FRAME_OWNERSHIP_READY owned=");
    uart_write_hex64(boot->frame_ownership->owned_frame_count);
    uart_write(" kernel-tables=");
    uart_write_hex64(
        boot->frame_ownership->owner_counts[
            MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
        ]
    );
    uart_write(" phase=bootstrap\n");
    uart_flush();
}

void boot_shutdown(void)
{
    (void)sbi_system_reset(SBI_RESET_TYPE_SHUTDOWN, SBI_RESET_REASON_NONE);
    stop_after_reset_failure();
}
