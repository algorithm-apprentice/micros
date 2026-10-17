#ifndef MICROS_KERNEL_USER_RUNTIME_IMAGE_H
#define MICROS_KERNEL_USER_RUNTIME_IMAGE_H

#include <stddef.h>
#include <stdint.h>

enum {
    MICROS_USER_RUNTIME_IMAGE_SEGMENT_COUNT = 3,
};

struct micros_user_runtime_image_segment {
    uint64_t virtual_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint32_t flags;
    const unsigned char *file_bytes;
};

struct micros_user_runtime_image {
    uint64_t entry;
    size_t segment_count;
    struct micros_user_runtime_image_segment
        segments[MICROS_USER_RUNTIME_IMAGE_SEGMENT_COUNT];
    uint64_t config_address;
    uint64_t rodata_address;
    uint64_t rodata_fault_address;
    uint64_t rodata_resume_address;
    uint64_t service_returned_address;
    uint64_t data_start;
    uint64_t data_end;
    uint64_t bss_start;
    uint64_t bss_end;
    uint64_t image_end;
};

extern const struct micros_user_runtime_image
    micros_user_runtime_test_image;

#endif
