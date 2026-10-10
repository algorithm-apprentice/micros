#ifndef MICROS_SERVERS_VM_IMAGE_H
#define MICROS_SERVERS_VM_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "micros/endpoint.h"
#include "micros/spawn.h"

#define MICROS_VM_SCRATCH_VIRTUAL_ADDRESS \
    UINT64_C(0x000000007fffe000)

enum micros_application_stack_error {
    MICROS_APPLICATION_STACK_OK = 0,
    MICROS_APPLICATION_STACK_ERROR_ARGUMENT,
    MICROS_APPLICATION_STACK_ERROR_MALFORMED,
    MICROS_APPLICATION_STACK_ERROR_TOO_BIG,
};

enum micros_vm_elf_error {
    MICROS_VM_ELF_OK = 0,
    MICROS_VM_ELF_ERROR_ARGUMENT,
    MICROS_VM_ELF_ERROR_EXECUTABLE,
    MICROS_VM_ELF_ERROR_TOO_BIG,
};

struct micros_application_stack_values {
    micros_endpoint_t pm_endpoint;
    micros_endpoint_t vfs_endpoint;
    uint64_t pid;
};

struct micros_application_stack_plan {
    uint64_t stack_pointer;
    uint32_t frame_size;
    uint32_t occupied_size;
    uint32_t argc_offset;
    uint32_t arguments_offset;
    uint32_t environment_offset;
    uint32_t auxiliary_offset;
    uint32_t strings_offset;
};

struct micros_vm_elf_segment {
    uint64_t file_offset;
    uint64_t virtual_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint32_t permissions;
    uint32_t reserved;
};

struct micros_vm_image_page {
    uint64_t virtual_address;
    uint64_t file_offset;
    uint32_t file_size;
    uint32_t permissions;
};

struct micros_vm_elf_plan {
    uint64_t entry;
    uint32_t segment_count;
    uint32_t page_count;
    struct micros_vm_elf_segment
        segments[MICROS_APPLICATION_ELF_SEGMENT_COUNT];
    struct micros_vm_image_page
        pages[MICROS_APPLICATION_IMAGE_PAGE_MAX];
};

enum micros_application_stack_error micros_application_stack_plan(
    const uint8_t *request_bytes,
    size_t request_size,
    struct micros_application_stack_plan *plan
);

enum micros_application_stack_error micros_application_stack_build(
    const uint8_t *request_bytes,
    size_t request_size,
    const struct micros_application_stack_values *values,
    uint8_t *stack_page,
    size_t stack_page_size,
    struct micros_application_stack_plan *plan
);

enum micros_vm_elf_error micros_vm_elf_plan(
    const uint8_t *file_bytes,
    size_t file_size,
    struct micros_vm_elf_plan *plan
);

#endif
