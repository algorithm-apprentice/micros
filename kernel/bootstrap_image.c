#include "kernel/bootstrap_image.h"

#include <stddef.h>
#include <stdint.h>

#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/vm_bootstrap.h"

static bool pointer_is_aligned(const void *pointer, size_t alignment)
{
    return (uintptr_t)pointer % alignment == 0;
}

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool range_bytes_are_zero(
    const struct micros_bootstrap_image_segment *segment,
    uint64_t address,
    uint32_t size
)
{
    uint64_t range_offset = address
        - segment->virtual_address;
    uint64_t index;

    for (index = 0; index < size; ++index) {
        uint64_t offset = range_offset + index;

        if (
            offset < segment->file_size
            && segment->file_bytes[offset] != 0
        ) {
            return false;
        }
    }
    return true;
}

static bool ranges_overlap(
    uint64_t left_base,
    uint64_t left_size,
    uint64_t right_base,
    uint64_t right_size
)
{
    return (
        left_base < right_base + right_size
        && right_base < left_base + left_size
    );
}

static enum micros_bootstrap_error validate_image(
    const struct micros_bootstrap_image *image,
    struct micros_bootstrap_image_info *info
)
{
    static const uint32_t expected_flags[
        MICROS_BOOTSTRAP_IMAGE_SEGMENT_COUNT
    ] = {
        MICROS_BOOTSTRAP_IMAGE_READ
            | MICROS_BOOTSTRAP_IMAGE_EXECUTE,
        MICROS_BOOTSTRAP_IMAGE_READ,
        MICROS_BOOTSTRAP_IMAGE_READ
            | MICROS_BOOTSTRAP_IMAGE_WRITE,
    };
    uint64_t next_address = MICROS_USER_VIRTUAL_BASE;
    uint64_t page_count = 0;
    bool entry_found = false;
    bool config_found = false;
    bool vm_boot_info_found = false;
    bool has_vm_boot_info =
        image->vm_boot_info_address != 0
        || image->vm_boot_info_size != 0;
    size_t index;

    if (
        image->version != MICROS_BOOTSTRAP_IMAGE_VERSION
        || image->image_id == 0
        || image->entry != MICROS_USER_VIRTUAL_BASE
        || image->segment_count
            != MICROS_BOOTSTRAP_IMAGE_SEGMENT_COUNT
        || image->config_address % 8 != 0
        || image->config_size
            != sizeof(struct micros_bootstrap_service_config)
        || UINT64_MAX - image->config_address
            < image->config_size
        || image->page_count == 0
        || (
            (image->vm_boot_info_address == 0)
            != (image->vm_boot_info_size == 0)
        )
        || (
            has_vm_boot_info
            && (
                image->vm_boot_info_address
                    % MICROS_VM_BOOT_INFO_ALIGNMENT
                    != 0
                || image->vm_boot_info_size
                    != MICROS_VM_BOOT_INFO_SIZE
                || UINT64_MAX - image->vm_boot_info_address
                    < image->vm_boot_info_size
                || ranges_overlap(
                    image->config_address,
                    image->config_size,
                    image->vm_boot_info_address,
                    image->vm_boot_info_size
                )
            )
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_IMAGE;
    }
    for (
        index = 0;
        index < MICROS_BOOTSTRAP_IMAGE_SEGMENT_COUNT;
        ++index
    ) {
        const struct micros_bootstrap_image_segment *segment =
            &image->segments[index];
        uint64_t segment_end;

        if (
            segment->virtual_address != next_address
            || segment->memory_size == 0
            || segment->memory_size % MICROS_SV39_PAGE_SIZE != 0
            || segment->file_size > segment->memory_size
            || segment->flags != expected_flags[index]
            || (
                segment->file_size != 0
                && segment->file_bytes == NULL
            )
            || UINT64_MAX - segment->virtual_address
                < segment->memory_size
        ) {
            return MICROS_BOOTSTRAP_ERROR_IMAGE;
        }
        segment_end =
            segment->virtual_address + segment->memory_size;
        if (
            image->entry >= segment->virtual_address
            && image->entry < segment_end
        ) {
            entry_found = (
                segment->flags
                & MICROS_BOOTSTRAP_IMAGE_EXECUTE
            ) != 0;
        }
        if (
            image->config_address >= segment->virtual_address
            && image->config_address < segment_end
        ) {
            if (
                config_found
                || UINT64_MAX - image->config_address
                    < image->config_size
                || image->config_address + image->config_size
                    > segment_end
                || (
                    segment->flags
                    & MICROS_BOOTSTRAP_IMAGE_WRITE
                ) == 0
                || !range_bytes_are_zero(
                    segment,
                    image->config_address,
                    image->config_size
                )
            ) {
                return MICROS_BOOTSTRAP_ERROR_IMAGE;
            }
            config_found = true;
        }
        if (
            has_vm_boot_info
            && image->vm_boot_info_address
                >= segment->virtual_address
            && image->vm_boot_info_address < segment_end
        ) {
            if (
                vm_boot_info_found
                || image->vm_boot_info_size
                    > segment_end - image->vm_boot_info_address
                || (
                    segment->flags
                    & MICROS_BOOTSTRAP_IMAGE_WRITE
                ) == 0
                || !range_bytes_are_zero(
                    segment,
                    image->vm_boot_info_address,
                    image->vm_boot_info_size
                )
            ) {
                return MICROS_BOOTSTRAP_ERROR_IMAGE;
            }
            vm_boot_info_found = true;
        }
        if (
            UINT64_MAX - page_count
                < segment->memory_size / MICROS_SV39_PAGE_SIZE
        ) {
            return MICROS_BOOTSTRAP_ERROR_RANGE;
        }
        page_count +=
            segment->memory_size / MICROS_SV39_PAGE_SIZE;
        next_address = segment_end;
    }
    if (
        !entry_found
        || !config_found
        || (has_vm_boot_info && !vm_boot_info_found)
        || image->image_end != next_address
        || image->page_count != page_count
    ) {
        return MICROS_BOOTSTRAP_ERROR_IMAGE;
    }
    *info = (struct micros_bootstrap_image_info){
        .version = image->version,
        .image_id = image->image_id,
        .entry = image->entry,
        .config_address = image->config_address,
        .config_size = image->config_size,
        .page_count = image->page_count,
        .image_end = image->image_end,
        .vm_boot_info_address = image->vm_boot_info_address,
        .vm_boot_info_size = image->vm_boot_info_size,
        .config_initially_zero = true,
        .vm_boot_info_initially_zero = has_vm_boot_info,
    };
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error micros_bootstrap_image_catalog_validate(
    const struct micros_bootstrap_image *images,
    size_t image_count,
    struct micros_bootstrap_image_info *infos,
    size_t info_capacity
)
{
    struct micros_bootstrap_image_info candidate[
        MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ];
    size_t index;

    if (
        images == NULL
        || infos == NULL
        || image_count == 0
        || image_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || info_capacity < image_count
        || !pointer_is_aligned(
            images,
            _Alignof(struct micros_bootstrap_image)
        )
        || !pointer_is_aligned(
            infos,
            _Alignof(struct micros_bootstrap_image_info)
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    clear_bytes(candidate, sizeof(candidate));
    for (index = 0; index < image_count; ++index) {
        size_t other;
        enum micros_bootstrap_error error;

        for (other = 0; other < index; ++other) {
            if (images[other].image_id == images[index].image_id) {
                return MICROS_BOOTSTRAP_ERROR_IMAGE;
            }
        }
        error = validate_image(&images[index], &candidate[index]);
        if (error != MICROS_BOOTSTRAP_OK) {
            return error;
        }
    }
    for (index = 0; index < image_count; ++index) {
        infos[index] = candidate[index];
    }
    return MICROS_BOOTSTRAP_OK;
}
