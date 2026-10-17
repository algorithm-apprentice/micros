#ifndef MICROS_KERNEL_BOOT_INTERNAL_H
#define MICROS_KERNEL_BOOT_INTERNAL_H

#include "micros/fdt.h"

struct micros_frame_allocator;
struct micros_frame_ownership;
struct micros_kernel_address_space_report;
struct micros_kernel_objects;
struct micros_hart;

struct boot_context {
    const struct micros_frame_allocator *frame_allocator;
    const struct micros_frame_ownership *frame_ownership;
    const struct micros_kernel_address_space_report *address_space;
    const struct micros_kernel_objects *kernel_objects;
    struct micros_hart *boot_hart;
    struct micros_fdt_memory_map memory_map;
    uintptr_t hart_id;
};

void boot_initialize_objects(struct boot_context *boot);
void boot_discover_memory(struct boot_context *boot, uintptr_t fdt_address);
void boot_initialize_memory(struct boot_context *boot);
void boot_test_foundation(const struct boot_context *boot);
void boot_test_address_spaces(const struct boot_context *boot);
void boot_test_execution(const struct boot_context *boot);
void boot_test_services(const struct boot_context *boot);
void boot_test_objects_and_ipc(const struct boot_context *boot);
void boot_test_nested_trap(const struct boot_context *boot);
void boot_shutdown(void);

#endif
