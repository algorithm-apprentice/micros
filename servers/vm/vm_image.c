#include "servers/vm/vm_image.h"

#include <stdbool.h>
#include <stdint.h>

#include "lib/spawn/spawn_request.h"
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
    APPLICATION_AUXILIARY_BYTES =
        MICROS_APPLICATION_AUXV_COUNT * 2 * sizeof(uint64_t),
};

static uint16_t vm_image_read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)(
        (uint16_t)bytes[0]
        | (uint16_t)((uint16_t)bytes[1] << 8)
    );
}

static uint32_t vm_image_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static uint64_t vm_image_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static void vm_image_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static void vm_image_zero_bytes(uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void vm_image_copy_bytes(
    uint8_t *destination,
    const uint8_t *source,
    size_t size
)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        destination[index] = source[index];
    }
}

static bool vm_image_bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool vm_image_add_size(size_t left, size_t right, size_t *result)
{
    if (right > SIZE_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool vm_image_add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool vm_image_align_size_16(size_t value, size_t *result)
{
    size_t added;

    if (!vm_image_add_size(value, 15, &added)) {
        return false;
    }
    *result = added & ~(size_t)15;
    return true;
}

static bool vm_image_ranges_overlap(
    const void *left,
    size_t left_size,
    const void *right,
    size_t right_size
)
{
    uintptr_t left_start = (uintptr_t)left;
    uintptr_t right_start = (uintptr_t)right;
    uintptr_t left_end;
    uintptr_t right_end;

    if (
        left_size > UINTPTR_MAX - left_start
        || right_size > UINTPTR_MAX - right_start
    ) {
        return true;
    }
    left_end = left_start + left_size;
    right_end = right_start + right_size;
    return left_start < right_end && right_start < left_end;
}

static bool vm_image_address_ranges_overlap(
    uint64_t left_start,
    uint64_t left_end,
    uint64_t right_start,
    uint64_t right_end
)
{
    return left_start < right_end && right_start < left_end;
}

static enum micros_application_stack_error vm_image_plan_decoded_stack(
    const struct micros_spawn_request *request,
    struct micros_application_stack_plan *plan
)
{
    struct micros_application_stack_plan candidate = {0};
    size_t relative_offset = sizeof(uint64_t);
    size_t strings_size = 0;
    size_t occupied_size;
    size_t frame_offset;
    size_t index;

    if (
        !vm_image_add_size(
            relative_offset,
            ((size_t)request->argument_count + 1) * sizeof(uint64_t),
            &relative_offset
        )
    ) {
        return MICROS_APPLICATION_STACK_ERROR_TOO_BIG;
    }
    candidate.environment_offset = (uint32_t)relative_offset;
    if (
        !vm_image_add_size(
            relative_offset,
            ((size_t)request->environment_count + 1)
                * sizeof(uint64_t),
            &relative_offset
        )
        || !vm_image_align_size_16(relative_offset, &relative_offset)
    ) {
        return MICROS_APPLICATION_STACK_ERROR_TOO_BIG;
    }
    candidate.auxiliary_offset = (uint32_t)relative_offset;
    if (
        !vm_image_add_size(
            relative_offset,
            APPLICATION_AUXILIARY_BYTES,
            &relative_offset
        )
    ) {
        return MICROS_APPLICATION_STACK_ERROR_TOO_BIG;
    }
    candidate.strings_offset = (uint32_t)relative_offset;

    for (index = 0; index < request->argument_count; ++index) {
        if (!vm_image_add_size(
            strings_size,
            request->arguments[index].size,
            &strings_size
        )) {
            return MICROS_APPLICATION_STACK_ERROR_TOO_BIG;
        }
    }
    for (index = 0; index < request->environment_count; ++index) {
        if (!vm_image_add_size(
            strings_size,
            request->environment[index].size,
            &strings_size
        )) {
            return MICROS_APPLICATION_STACK_ERROR_TOO_BIG;
        }
    }
    if (
        !vm_image_add_size(relative_offset, strings_size, &relative_offset)
        || !vm_image_align_size_16(relative_offset, &occupied_size)
        || occupied_size > MICROS_APPLICATION_STACK_OCCUPIED_MAX
        || occupied_size > MICROS_APPLICATION_STACK_PAGE_SIZE
    ) {
        return MICROS_APPLICATION_STACK_ERROR_TOO_BIG;
    }

    frame_offset = MICROS_APPLICATION_STACK_PAGE_SIZE - occupied_size;
    candidate.stack_pointer =
        MICROS_APPLICATION_STACK_TOP - occupied_size;
    candidate.frame_size = (uint32_t)relative_offset;
    candidate.occupied_size = (uint32_t)occupied_size;
    candidate.argc_offset = (uint32_t)frame_offset;
    candidate.arguments_offset =
        (uint32_t)(frame_offset + sizeof(uint64_t));
    candidate.environment_offset += (uint32_t)frame_offset;
    candidate.auxiliary_offset += (uint32_t)frame_offset;
    candidate.strings_offset += (uint32_t)frame_offset;
    *plan = candidate;
    return MICROS_APPLICATION_STACK_OK;
}

static enum micros_application_stack_error vm_image_decode_and_plan_stack(
    const uint8_t *request_bytes,
    size_t request_size,
    struct micros_spawn_request *request,
    struct micros_application_stack_plan *plan
)
{
    enum micros_spawn_request_error error =
        micros_spawn_request_decode(
            request_bytes,
            request_size,
            request
        );

    if (error == MICROS_SPAWN_REQUEST_ERROR_ARGUMENT) {
        return MICROS_APPLICATION_STACK_ERROR_ARGUMENT;
    }
    if (error != MICROS_SPAWN_REQUEST_OK) {
        return MICROS_APPLICATION_STACK_ERROR_MALFORMED;
    }
    return vm_image_plan_decoded_stack(request, plan);
}

enum micros_application_stack_error micros_application_stack_plan(
    const uint8_t *request_bytes,
    size_t request_size,
    struct micros_application_stack_plan *plan
)
{
    struct micros_spawn_request request;
    struct micros_application_stack_plan candidate;
    enum micros_application_stack_error error;

    if (plan == NULL) {
        return MICROS_APPLICATION_STACK_ERROR_ARGUMENT;
    }
    error = vm_image_decode_and_plan_stack(
        request_bytes,
        request_size,
        &request,
        &candidate
    );
    if (error != MICROS_APPLICATION_STACK_OK) {
        return error;
    }
    *plan = candidate;
    return MICROS_APPLICATION_STACK_OK;
}

enum micros_application_stack_error micros_application_stack_build(
    const uint8_t *request_bytes,
    size_t request_size,
    const struct micros_application_stack_values *values,
    uint8_t *stack_page,
    size_t stack_page_size,
    struct micros_application_stack_plan *plan
)
{
    static const uint64_t auxiliary_types[MICROS_APPLICATION_AUXV_COUNT] = {
        MICROS_AT_PM_ENDPOINT,
        MICROS_AT_VFS_ENDPOINT,
        MICROS_AT_PID,
        MICROS_AT_PAGE_SIZE,
        MICROS_AT_NULL,
    };
    struct micros_spawn_request request;
    struct micros_application_stack_plan candidate;
    uint64_t auxiliary_values[MICROS_APPLICATION_AUXV_COUNT];
    enum micros_application_stack_error error;
    size_t string_offset;
    size_t index;

    if (
        request_bytes == NULL
        || values == NULL
        || stack_page == NULL
        || plan == NULL
        || stack_page_size != MICROS_APPLICATION_STACK_PAGE_SIZE
        || values->pm_endpoint == MICROS_ENDPOINT_NONE
        || values->pm_endpoint == MICROS_ENDPOINT_ANY
        || values->vfs_endpoint == MICROS_ENDPOINT_NONE
        || values->vfs_endpoint == MICROS_ENDPOINT_ANY
        || values->pid == 0
        || vm_image_ranges_overlap(
            request_bytes,
            request_size,
            stack_page,
            stack_page_size
        )
    ) {
        return MICROS_APPLICATION_STACK_ERROR_ARGUMENT;
    }
    error = vm_image_decode_and_plan_stack(
        request_bytes,
        request_size,
        &request,
        &candidate
    );
    if (error != MICROS_APPLICATION_STACK_OK) {
        return error;
    }

    auxiliary_values[0] = values->pm_endpoint;
    auxiliary_values[1] = values->vfs_endpoint;
    auxiliary_values[2] = values->pid;
    auxiliary_values[3] = MICROS_APPLICATION_STACK_PAGE_SIZE;
    auxiliary_values[4] = 0;

    vm_image_zero_bytes(stack_page, stack_page_size);
    vm_image_write_u64_le(
        &stack_page[candidate.argc_offset],
        request.argument_count
    );
    string_offset = candidate.strings_offset;
    for (index = 0; index < request.argument_count; ++index) {
        const struct micros_spawn_string_reference *reference =
            &request.arguments[index];

        vm_image_write_u64_le(
            &stack_page[candidate.arguments_offset
                + index * sizeof(uint64_t)],
            MICROS_APPLICATION_STACK_BASE + string_offset
        );
        vm_image_copy_bytes(
            &stack_page[string_offset],
            &request_bytes[reference->offset],
            reference->size
        );
        string_offset += reference->size;
    }
    for (index = 0; index < request.environment_count; ++index) {
        const struct micros_spawn_string_reference *reference =
            &request.environment[index];

        vm_image_write_u64_le(
            &stack_page[candidate.environment_offset
                + index * sizeof(uint64_t)],
            MICROS_APPLICATION_STACK_BASE + string_offset
        );
        vm_image_copy_bytes(
            &stack_page[string_offset],
            &request_bytes[reference->offset],
            reference->size
        );
        string_offset += reference->size;
    }
    for (index = 0; index < MICROS_APPLICATION_AUXV_COUNT; ++index) {
        size_t offset = candidate.auxiliary_offset
            + index * 2 * sizeof(uint64_t);

        vm_image_write_u64_le(&stack_page[offset], auxiliary_types[index]);
        vm_image_write_u64_le(
            &stack_page[offset + sizeof(uint64_t)],
            auxiliary_values[index]
        );
    }

    *plan = candidate;
    return MICROS_APPLICATION_STACK_OK;
}

enum micros_vm_elf_error micros_vm_elf_plan(
    const uint8_t *file_bytes,
    size_t file_size,
    struct micros_vm_elf_plan *plan
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
    struct micros_vm_elf_plan candidate = {0};
    uint64_t previous_virtual_end = 0;
    uint64_t previous_file_end = 0;
    uint64_t total_pages = 0;
    size_t page_index = 0;
    size_t index;

    if (file_bytes == NULL || plan == NULL) {
        return MICROS_VM_ELF_ERROR_ARGUMENT;
    }
    if (file_size > MICROS_APPLICATION_EXECUTABLE_SIZE_MAX) {
        return MICROS_VM_ELF_ERROR_TOO_BIG;
    }
    if (
        file_size < ELF_HEADER_SIZE
        || file_bytes[0] != 0x7f
        || file_bytes[1] != 'E'
        || file_bytes[2] != 'L'
        || file_bytes[3] != 'F'
        || file_bytes[4] != ELF_CLASS_64
        || file_bytes[5] != ELF_DATA_LITTLE_ENDIAN
        || file_bytes[6] != ELF_VERSION_CURRENT
        || file_bytes[7] != ELF_OS_ABI_SYSV
        || file_bytes[8] != 0
        || !vm_image_bytes_are_zero(&file_bytes[9], 7)
        || vm_image_read_u16_le(&file_bytes[16]) != ELF_TYPE_EXECUTABLE
        || vm_image_read_u16_le(&file_bytes[18]) != ELF_MACHINE_RISCV
        || vm_image_read_u32_le(&file_bytes[20]) != ELF_VERSION_CURRENT
        || vm_image_read_u64_le(&file_bytes[24]) != MICROS_APPLICATION_ENTRY
        || vm_image_read_u64_le(&file_bytes[32]) != ELF_PROGRAM_HEADER_OFFSET
        || (
            vm_image_read_u32_le(&file_bytes[48])
            & ~(uint32_t)ELF_FLAG_RISCV_RVC
        ) != 0
        || vm_image_read_u16_le(&file_bytes[52]) != ELF_HEADER_SIZE
        || vm_image_read_u16_le(&file_bytes[54]) != ELF_PROGRAM_HEADER_SIZE
        || vm_image_read_u16_le(&file_bytes[56])
            != MICROS_APPLICATION_ELF_SEGMENT_COUNT
        || file_size
            < ELF_PROGRAM_HEADER_OFFSET
                + MICROS_APPLICATION_ELF_SEGMENT_COUNT
                    * ELF_PROGRAM_HEADER_SIZE
    ) {
        return MICROS_VM_ELF_ERROR_EXECUTABLE;
    }

    candidate.entry = MICROS_APPLICATION_ENTRY;
    candidate.segment_count = MICROS_APPLICATION_ELF_SEGMENT_COUNT;
    for (index = 0; index < MICROS_APPLICATION_ELF_SEGMENT_COUNT; ++index) {
        struct micros_vm_elf_segment *segment =
            &candidate.segments[index];
        size_t offset = ELF_PROGRAM_HEADER_OFFSET
            + index * ELF_PROGRAM_HEADER_SIZE;
        uint32_t flags = vm_image_read_u32_le(&file_bytes[offset + 4]);
        uint64_t physical_address =
            vm_image_read_u64_le(&file_bytes[offset + 24]);
        uint64_t alignment = vm_image_read_u64_le(&file_bytes[offset + 48]);
        uint64_t virtual_end;
        uint64_t file_end;

        segment->file_offset = vm_image_read_u64_le(&file_bytes[offset + 8]);
        segment->virtual_address =
            vm_image_read_u64_le(&file_bytes[offset + 16]);
        segment->file_size = vm_image_read_u64_le(&file_bytes[offset + 32]);
        segment->memory_size = vm_image_read_u64_le(&file_bytes[offset + 40]);
        segment->permissions = permissions[index];
        if (
            vm_image_read_u32_le(&file_bytes[offset]) != ELF_PROGRAM_TYPE_LOAD
            || flags != expected_flags[index]
            || alignment != MICROS_APPLICATION_STACK_PAGE_SIZE
            || segment->file_offset
                % MICROS_APPLICATION_STACK_PAGE_SIZE != 0
            || segment->virtual_address
                % MICROS_APPLICATION_STACK_PAGE_SIZE != 0
            || physical_address
                % MICROS_APPLICATION_STACK_PAGE_SIZE != 0
            || segment->memory_size
                % MICROS_APPLICATION_STACK_PAGE_SIZE != 0
            || physical_address != segment->virtual_address
            || segment->memory_size == 0
            || segment->file_size > segment->memory_size
            || segment->file_offset
                % MICROS_APPLICATION_STACK_PAGE_SIZE
                != segment->virtual_address
                    % MICROS_APPLICATION_STACK_PAGE_SIZE
            || !vm_image_add_u64(
                segment->file_offset,
                segment->file_size,
                &file_end
            )
            || file_end > file_size
            || !vm_image_add_u64(
                segment->virtual_address,
                segment->memory_size,
                &virtual_end
            )
            || segment->virtual_address < MICROS_USER_VIRTUAL_BASE
            || virtual_end > MICROS_USER_VIRTUAL_END
            || (
                index == 0
                && segment->virtual_address
                    != MICROS_APPLICATION_ENTRY
            )
            || (
                index != 0
                && segment->virtual_address != previous_virtual_end
            )
            || (index != 0 && segment->file_offset < previous_file_end)
            || vm_image_address_ranges_overlap(
                segment->virtual_address,
                virtual_end,
                MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
                MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
                    + MICROS_APPLICATION_STACK_PAGE_SIZE
            )
            || vm_image_address_ranges_overlap(
                segment->virtual_address,
                virtual_end,
                MICROS_APPLICATION_STACK_BASE,
                MICROS_APPLICATION_STACK_TOP
            )
        ) {
            return MICROS_VM_ELF_ERROR_EXECUTABLE;
        }
        previous_virtual_end = virtual_end;
        previous_file_end = file_end;
        total_pages += segment->memory_size
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
    for (index = 0; index < MICROS_APPLICATION_ELF_SEGMENT_COUNT; ++index) {
        const struct micros_vm_elf_segment *segment =
            &candidate.segments[index];
        uint64_t segment_page_count = segment->memory_size
            / MICROS_APPLICATION_STACK_PAGE_SIZE;
        uint64_t segment_page;

        for (
            segment_page = 0;
            segment_page < segment_page_count;
            ++segment_page
        ) {
            struct micros_vm_image_page *page =
                &candidate.pages[page_index];
            uint64_t page_offset = segment_page
                * MICROS_APPLICATION_STACK_PAGE_SIZE;

            page->virtual_address =
                segment->virtual_address + page_offset;
            page->permissions = segment->permissions;
            if (page_offset < segment->file_size) {
                uint64_t remaining = segment->file_size - page_offset;

                page->file_offset = segment->file_offset + page_offset;
                page->file_size = (uint32_t)(
                    remaining < MICROS_APPLICATION_STACK_PAGE_SIZE
                        ? remaining
                        : MICROS_APPLICATION_STACK_PAGE_SIZE
                );
            }
            ++page_index;
        }
    }

    *plan = candidate;
    return MICROS_VM_ELF_OK;
}
