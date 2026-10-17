#include "kernel/boot/boot_internal.h"

void kernel_main(uintptr_t hart_id, uintptr_t fdt_address);

void kernel_main(uintptr_t hart_id, uintptr_t fdt_address)
{
    struct boot_context boot;

    boot.hart_id = hart_id;
    boot_initialize_objects(&boot);
    boot_discover_memory(&boot, fdt_address);
    boot_initialize_memory(&boot);
    boot_test_foundation(&boot);
    boot_test_address_spaces(&boot);
    boot_test_execution(&boot);
    boot_test_services(&boot);
    boot_test_objects_and_ipc(&boot);
    boot_test_nested_trap(&boot);
    boot_shutdown();
}
