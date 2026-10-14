#ifndef MICROS_TESTS_QEMU_APPLICATION_RUNTIME_FIXTURE_H
#define MICROS_TESTS_QEMU_APPLICATION_RUNTIME_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/endpoint.h"
#include "micros/spawn.h"

struct micros_application_test_segment {
    const uint8_t *bytes;
    uint64_t virtual_address;
    uint64_t memory_size;
    size_t file_size;
    uint32_t elf_flags;
};

struct micros_application_test_image {
    uint64_t entry;
    struct micros_application_test_segment
        segments[MICROS_APPLICATION_ELF_SEGMENT_COUNT];
    const uint8_t *request_bytes;
    size_t request_size;
};

extern const struct micros_application_test_image
    micros_application_runtime_test_image;

bool vm_application_test_fill_page(
    size_t page_index,
    uint8_t *page,
    micros_endpoint_t pm_endpoint,
    micros_endpoint_t metadata_peer
);
bool vm_application_test_stack_pointer(uint64_t *stack_pointer);

#endif
