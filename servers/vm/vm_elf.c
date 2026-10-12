#include "servers/vm/vm_image_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/sv39.h"
#include "micros/user_address_space.h"

enum {
    ELF_HEADER_SIZE = 64,
    ELF_PROGRAM_HEADER_SIZE = 56,
    ELF_PROGRAM_HEADER_OFFSET = 64,
    ELF_CLASS_64 = 2,
    ELF_DATA_LITTLE_ENDIAN = 1,
    ELF_VERSION_CURRENT = 1,
    ELF_OS_ABI_SYSV = 0,
    ELF_TYPE_EXECUTABLE = 2,
    ELF_MACHINE_RISCV = 243,
    ELF_PROGRAM_TYPE_LOAD = 1,
    ELF_FLAG_RISCV_RVC = 1,
    ELF_PROGRAM_FLAG_EXECUTE = 1,
    ELF_PROGRAM_FLAG_WRITE = 2,
    ELF_PROGRAM_FLAG_READ = 4,
};

static bool elf_header_is_valid(
    const uint8_t *file_bytes,
    size_t file_size
)
{
    return (
        file_size >= ELF_HEADER_SIZE
        && file_bytes[0] == 0x7f
        && file_bytes[1] == 'E'
        && file_bytes[2] == 'L'
        && file_bytes[3] == 'F'
        && file_bytes[4] == ELF_CLASS_64
        && file_bytes[5] == ELF_DATA_LITTLE_ENDIAN
        && file_bytes[6] == ELF_VERSION_CURRENT
        && file_bytes[7] == ELF_OS_ABI_SYSV
        && file_bytes[8] == 0
        && vm_image_bytes_are_zero(&file_bytes[9], 7)
        && vm_image_read_u16_le(&file_bytes[16])
            == ELF_TYPE_EXECUTABLE
        && vm_image_read_u16_le(&file_bytes[18])
            == ELF_MACHINE_RISCV
        && vm_image_read_u32_le(&file_bytes[20])
            == ELF_VERSION_CURRENT
        && vm_image_read_u64_le(&file_bytes[24])
            == MICROS_APPLICATION_ENTRY
        && vm_image_read_u64_le(&file_bytes[32])
            == ELF_PROGRAM_HEADER_OFFSET
        && (
            vm_image_read_u32_le(&file_bytes[48])
            & ~(uint32_t)ELF_FLAG_RISCV_RVC
        ) == 0
        && vm_image_read_u16_le(&file_bytes[52])
            == ELF_HEADER_SIZE
        && vm_image_read_u16_le(&file_bytes[54])
            == ELF_PROGRAM_HEADER_SIZE
        && vm_image_read_u16_le(&file_bytes[56])
            == MICROS_APPLICATION_ELF_SEGMENT_COUNT
        && file_size
            >= ELF_PROGRAM_HEADER_OFFSET
                + MICROS_APPLICATION_ELF_SEGMENT_COUNT
                    * ELF_PROGRAM_HEADER_SIZE
    );
}

static bool decode_segment(
    const uint8_t *file_bytes,
    size_t file_size,
    size_t index,
    uint64_t previous_virtual_end,
    uint64_t previous_file_end,
    struct micros_vm_elf_segment *segment,
    uint64_t *virtual_end,
    uint64_t *file_end
)
{
    static const uint32_t expected_flags[
        MICROS_APPLICATION_ELF_SEGMENT_COUNT
    ] = {
        ELF_PROGRAM_FLAG_READ | ELF_PROGRAM_FLAG_EXECUTE,
        ELF_PROGRAM_FLAG_READ,
        ELF_PROGRAM_FLAG_READ | ELF_PROGRAM_FLAG_WRITE,
    };
    static const uint32_t permissions[
        MICROS_APPLICATION_ELF_SEGMENT_COUNT
    ] = {
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_EXECUTE,
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ,
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE,
    };
    size_t offset =
        ELF_PROGRAM_HEADER_OFFSET + index * ELF_PROGRAM_HEADER_SIZE;
    uint32_t flags = vm_image_read_u32_le(&file_bytes[offset + 4]);
    uint64_t physical_address =
        vm_image_read_u64_le(&file_bytes[offset + 24]);
    uint64_t alignment =
        vm_image_read_u64_le(&file_bytes[offset + 48]);

    segment->file_offset =
        vm_image_read_u64_le(&file_bytes[offset + 8]);
    segment->virtual_address =
        vm_image_read_u64_le(&file_bytes[offset + 16]);
    segment->file_size =
        vm_image_read_u64_le(&file_bytes[offset + 32]);
    segment->memory_size =
        vm_image_read_u64_le(&file_bytes[offset + 40]);
    segment->permissions = permissions[index];
    return (
        vm_image_read_u32_le(&file_bytes[offset])
            == ELF_PROGRAM_TYPE_LOAD
        && flags == expected_flags[index]
        && alignment == MICROS_APPLICATION_STACK_PAGE_SIZE
        && segment->file_offset
            % MICROS_APPLICATION_STACK_PAGE_SIZE == 0
        && segment->virtual_address
            % MICROS_APPLICATION_STACK_PAGE_SIZE == 0
        && physical_address
            % MICROS_APPLICATION_STACK_PAGE_SIZE == 0
        && segment->memory_size
            % MICROS_APPLICATION_STACK_PAGE_SIZE == 0
        && physical_address == segment->virtual_address
        && segment->memory_size != 0
        && segment->file_size <= segment->memory_size
        && segment->file_offset
            % MICROS_APPLICATION_STACK_PAGE_SIZE
            == segment->virtual_address
                % MICROS_APPLICATION_STACK_PAGE_SIZE
        && vm_image_add_u64(
            segment->file_offset,
            segment->file_size,
            file_end
        )
        && *file_end <= file_size
        && vm_image_add_u64(
            segment->virtual_address,
            segment->memory_size,
            virtual_end
        )
        && segment->virtual_address >= MICROS_USER_VIRTUAL_BASE
        && *virtual_end <= MICROS_USER_VIRTUAL_END
        && (
            index != 0
            || segment->virtual_address == MICROS_APPLICATION_ENTRY
        )
        && (
            index == 0
            || segment->virtual_address == previous_virtual_end
        )
        && (index == 0 || segment->file_offset >= previous_file_end)
        && !vm_image_address_ranges_overlap(
            segment->virtual_address,
            *virtual_end,
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
                + MICROS_APPLICATION_STACK_PAGE_SIZE
        )
        && !vm_image_address_ranges_overlap(
            segment->virtual_address,
            *virtual_end,
            MICROS_APPLICATION_STACK_BASE,
            MICROS_APPLICATION_STACK_TOP
        )
    );
}

static void build_page_plan(struct micros_vm_elf_plan *candidate)
{
    size_t page_index = 0;
    size_t index;

    for (index = 0; index < candidate->segment_count; ++index) {
        const struct micros_vm_elf_segment *segment =
            &candidate->segments[index];
        uint64_t segment_page_count = segment->memory_size
            / MICROS_APPLICATION_STACK_PAGE_SIZE;
        uint64_t segment_page;

        for (
            segment_page = 0;
            segment_page < segment_page_count;
            ++segment_page
        ) {
            struct micros_vm_image_page *page =
                &candidate->pages[page_index];
            uint64_t page_offset = segment_page
                * MICROS_APPLICATION_STACK_PAGE_SIZE;

            page->virtual_address =
                segment->virtual_address + page_offset;
            page->permissions = segment->permissions;
            if (page_offset < segment->file_size) {
                uint64_t remaining =
                    segment->file_size - page_offset;

                page->file_offset =
                    segment->file_offset + page_offset;
                page->file_size = (uint32_t)(
                    remaining < MICROS_APPLICATION_STACK_PAGE_SIZE
                        ? remaining
                        : MICROS_APPLICATION_STACK_PAGE_SIZE
                );
            }
            ++page_index;
        }
    }
}

enum micros_vm_elf_error micros_vm_elf_plan(
    const uint8_t *file_bytes,
    size_t file_size,
    struct micros_vm_elf_plan *plan
)
{
    struct micros_vm_elf_plan candidate = {0};
    uint64_t previous_virtual_end = 0;
    uint64_t previous_file_end = 0;
    uint64_t total_pages = 0;
    size_t index;

    if (file_bytes == NULL || plan == NULL) {
        return MICROS_VM_ELF_ERROR_ARGUMENT;
    }
    if (file_size > MICROS_APPLICATION_EXECUTABLE_SIZE_MAX) {
        return MICROS_VM_ELF_ERROR_TOO_BIG;
    }
    if (!elf_header_is_valid(file_bytes, file_size)) {
        return MICROS_VM_ELF_ERROR_EXECUTABLE;
    }
    candidate.entry = MICROS_APPLICATION_ENTRY;
    candidate.segment_count = MICROS_APPLICATION_ELF_SEGMENT_COUNT;
    for (index = 0; index < candidate.segment_count; ++index) {
        uint64_t virtual_end;
        uint64_t file_end;

        if (!decode_segment(
            file_bytes,
            file_size,
            index,
            previous_virtual_end,
            previous_file_end,
            &candidate.segments[index],
            &virtual_end,
            &file_end
        )) {
            return MICROS_VM_ELF_ERROR_EXECUTABLE;
        }
        previous_virtual_end = virtual_end;
        previous_file_end = file_end;
        total_pages += candidate.segments[index].memory_size
            / MICROS_APPLICATION_STACK_PAGE_SIZE;
    }
    if (
        MICROS_APPLICATION_ENTRY
            >= candidate.segments[0].virtual_address
                + candidate.segments[0].memory_size
    ) {
        return MICROS_VM_ELF_ERROR_EXECUTABLE;
    }
    if (
        total_pages < MICROS_APPLICATION_IMAGE_PAGE_MIN
        || total_pages > MICROS_APPLICATION_IMAGE_PAGE_MAX
    ) {
        return MICROS_VM_ELF_ERROR_TOO_BIG;
    }

    candidate.page_count = (uint32_t)total_pages;
    build_page_plan(&candidate);
    *plan = candidate;
    return MICROS_VM_ELF_OK;
}
