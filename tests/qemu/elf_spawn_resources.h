#ifndef MICROS_TESTS_QEMU_ELF_SPAWN_RESOURCES_H
#define MICROS_TESTS_QEMU_ELF_SPAWN_RESOURCES_H

#include <stdbool.h>
#include <stddef.h>

#include "kernel/pm/pm_control_core.h"
#include "kernel/vm/vm_handoff_core.h"
#include "micros/frame_ownership.h"
#include "micros/grant.h"
#include "servers/pm/pm_coordinator.h"
#include "servers/ramfs/ramfs_core.h"
#include "servers/tty/tty_service_core.h"
#include "servers/tty/tty_uart.h"
#include "servers/vfs/vfs_core.h"
#include "servers/vm/vm_core.h"
#include "tests/qemu/elf_spawn_protocol.h"

enum {
    MICROS_ELF_SPAWN_TABLE_CAPACITY =
        (MICROS_ELF_SPAWN_SERVICE_COUNT + 1) * MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT,
};

struct micros_elf_spawn_page_table {
    uint64_t physical_address;
    uint64_t entries[MICROS_FRAME_SIZE / sizeof(uint64_t)];
};

struct micros_elf_spawn_kernel_resources {
    struct micros_kernel_objects objects;
    struct micros_endpoint_registry endpoints;
    struct micros_grant_registry grants;
    struct micros_frame_allocator allocator;
    struct micros_frame_ownership ownership;
    struct micros_vm_handoff_state vm;
    struct micros_pm_control_state pm;
    uint32_t table_count;
    uint32_t reserved;
    struct micros_elf_spawn_page_table tables[MICROS_ELF_SPAWN_TABLE_CAPACITY];
};

struct micros_elf_spawn_service_resources {
    struct micros_pm_table pm;
    struct micros_pm_coordinator coordinator;
    struct micros_vm_state vm;
    struct micros_vm_boot_info boot_info;
    struct micros_vfs_state vfs;
    struct micros_ramfs_state ramfs;
    struct micros_tty_service_state tty;
    struct micros_tty_uart_state uart;
};

struct micros_elf_spawn_resources {
    struct micros_elf_spawn_kernel_resources kernel;
    struct micros_elf_spawn_service_resources services;
};

bool micros_elf_spawn_resource_bytes_equal(
    const void *left, const void *right, size_t size
);
void micros_elf_spawn_resource_copy(void *destination, const void *source, size_t size);
bool micros_elf_spawn_same_kernel_resources(
    const struct micros_elf_spawn_kernel_resources *baseline,
    const struct micros_elf_spawn_kernel_resources *current
);
bool micros_elf_spawn_same_service_resources(
    const struct micros_elf_spawn_service_resources *baseline,
    const struct micros_elf_spawn_service_resources *current
);

#endif
