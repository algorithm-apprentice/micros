#ifndef MICROS_KERNEL_BOOTSTRAP_IMAGE_H
#define MICROS_KERNEL_BOOTSTRAP_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap.h"

enum {
    MICROS_BOOTSTRAP_IMAGE_SEGMENT_COUNT = 3,
};

#define MICROS_BOOTSTRAP_IMAGE_EXECUTE UINT32_C(0x1)
#define MICROS_BOOTSTRAP_IMAGE_WRITE UINT32_C(0x2)
#define MICROS_BOOTSTRAP_IMAGE_READ UINT32_C(0x4)

struct micros_bootstrap_image_segment {
    uint64_t virtual_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint32_t flags;
    const unsigned char *file_bytes;
};

struct micros_bootstrap_image {
    uint32_t version;
    uint32_t image_id;
    uint64_t entry;
    size_t segment_count;
    struct micros_bootstrap_image_segment
        segments[MICROS_BOOTSTRAP_IMAGE_SEGMENT_COUNT];
    uint64_t config_address;
    uint32_t config_size;
    uint32_t page_count;
    uint64_t image_end;
};

enum micros_bootstrap_error micros_bootstrap_image_catalog_validate(
    const struct micros_bootstrap_image *images,
    size_t image_count,
    struct micros_bootstrap_image_info *infos,
    size_t info_capacity
);

#endif
