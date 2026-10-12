#include "tests/host/vm_core_test_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/sv39.h"

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

static struct vm_core_test_context test_context;

struct vm_core_test_context *vm_core_test_context_get(void)
{
    return &test_context;
}

void vm_core_test_write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

void vm_core_test_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void vm_core_test_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

bool vm_core_test_bytes_are_zero(const void *storage, size_t size)
{
    const uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

uint64_t vm_core_test_frame_address(size_t frame_index)
{
    return UINT64_C(0x80001000)
        + frame_index * MICROS_FRAME_SIZE;
}

static void initialize_boot_header(
    struct micros_vm_boot_info *info,
    size_t managed_frame_count
)
{
    info->header.version = MICROS_VM_BOOT_INFO_VERSION;
    info->header.header_size = MICROS_VM_BOOT_HEADER_SIZE;
    info->header.total_size = MICROS_VM_BOOT_INFO_SIZE;
    info->header.physical_range_size =
        MICROS_VM_PHYSICAL_RANGE_SIZE;
    info->header.managed_range_size =
        MICROS_VM_MANAGED_RANGE_SIZE;
    info->header.address_space_entry_size =
        MICROS_VM_ADDRESS_SPACE_SIZE;
    info->header.mapping_entry_size = MICROS_VM_MAPPING_SIZE;
    info->header.memory_range_capacity =
        MICROS_VM_MAX_MEMORY_RANGES;
    info->header.memory_range_count = 1;
    info->header.reserved_range_capacity =
        MICROS_VM_MAX_RESERVED_RANGES;
    info->header.reserved_range_count = 1;
    info->header.managed_range_capacity =
        MICROS_VM_MAX_MANAGED_RANGES;
    info->header.managed_range_count = 1;
    info->header.address_space_capacity =
        MICROS_VM_MAX_STATIC_ADDRESS_SPACES;
    info->header.address_space_count =
        TEST_BOOT_ADDRESS_SPACE_COUNT;
    info->header.mapping_capacity = MICROS_VM_MAX_STATIC_MAPPINGS;
    info->header.mapping_count = 3;
    info->header.frame_state_capacity =
        MICROS_VM_MAX_MANAGED_FRAMES;
    info->header.frame_state_size = 1;
    info->header.service_id = TEST_VM_SERVICE_ID;
    info->header.self_endpoint = TEST_VM_ENDPOINT;
    info->header.ownership_phase =
        MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP;
    info->header.managed_frame_count = managed_frame_count;
    info->header.free_frame_count =
        managed_frame_count - TEST_FIRST_FREE_FRAME;
    info->header.kernel_frame_count = 3;
    info->header.vm_self_wired_frame_count = 2;
    info->header.service_wired_frame_count = 1;
    info->memory_ranges[0] = (struct micros_vm_physical_range){
        .base = UINT64_C(0x80000000),
        .size = (managed_frame_count + 1) * MICROS_FRAME_SIZE,
    };
    info->reserved_ranges[0] = (struct micros_vm_physical_range){
        .base = UINT64_C(0x80000000),
        .size = MICROS_FRAME_SIZE,
    };
    info->managed_ranges[0] = (struct micros_vm_managed_range){
        .base = UINT64_C(0x80001000),
        .frame_count = managed_frame_count,
        .frame_index = 0,
    };
}

static void initialize_boot_mappings(struct micros_vm_boot_info *info)
{
    info->address_spaces[0] =
        (struct micros_vm_address_space){
            .service_id = TEST_VM_SERVICE_ID,
            .endpoint = TEST_VM_ENDPOINT,
            .process_generation = 1,
            .process_slot = 1,
            .mapping_count = 2,
            .mapping_index = 0,
            .root_physical_address = vm_core_test_frame_address(0),
        };
    info->address_spaces[1] =
        (struct micros_vm_address_space){
            .service_id = 1,
            .endpoint = UINT32_C(0x00001000),
            .process_generation = 1,
            .process_slot = 0,
            .mapping_count = 1,
            .mapping_index = 2,
            .root_physical_address = vm_core_test_frame_address(3),
        };
    info->mappings[0] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 1,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_EXECUTE,
        .role = MICROS_VM_MAPPING_SELF_WIRED,
        .virtual_address = MICROS_USER_VIRTUAL_BASE,
        .physical_address = vm_core_test_frame_address(1),
    };
    info->mappings[1] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 1,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_WRITE,
        .role = MICROS_VM_MAPPING_SELF_WIRED,
        .virtual_address = MICROS_USER_VIRTUAL_BASE + MICROS_FRAME_SIZE,
        .physical_address = vm_core_test_frame_address(2),
    };
    info->mappings[2] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 0,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_EXECUTE,
        .role = MICROS_VM_MAPPING_SERVICE_WIRED,
        .virtual_address = MICROS_USER_VIRTUAL_BASE,
        .physical_address = vm_core_test_frame_address(4),
    };
}

static void initialize_boot_frame_states(
    struct micros_vm_boot_info *info,
    size_t managed_frame_count
)
{
    size_t index;

    info->frame_states[0] = MICROS_VM_FRAME_KERNEL;
    info->frame_states[1] = MICROS_VM_FRAME_SELF_WIRED;
    info->frame_states[2] = MICROS_VM_FRAME_SELF_WIRED;
    info->frame_states[3] = MICROS_VM_FRAME_KERNEL;
    info->frame_states[4] = MICROS_VM_FRAME_SERVICE_WIRED;
    info->frame_states[5] = MICROS_VM_FRAME_KERNEL;
    for (
        index = TEST_FIRST_FREE_FRAME;
        index < managed_frame_count;
        ++index
    ) {
        info->frame_states[index] = MICROS_VM_FRAME_FREE;
    }
}

bool vm_core_test_initialize_boot_info(
    struct vm_core_test_context *context,
    size_t managed_frame_count
)
{
    struct micros_vm_boot_summary summary;

    if (
        managed_frame_count < TEST_FIRST_FREE_FRAME
        || managed_frame_count > MICROS_VM_MAX_MANAGED_FRAMES
    ) {
        return false;
    }
    memset(&context->boot_info, 0, sizeof(context->boot_info));
    initialize_boot_header(&context->boot_info, managed_frame_count);
    initialize_boot_mappings(&context->boot_info);
    initialize_boot_frame_states(
        &context->boot_info,
        managed_frame_count
    );
    return micros_vm_boot_finalize(&context->boot_info, &summary)
        == MICROS_VM_BOOT_OK;
}

enum micros_vm_core_error vm_core_test_initialize_state(
    struct vm_core_test_context *context
)
{
    return micros_vm_state_initialize(
        &context->state,
        &context->boot_info,
        TEST_VM_SERVICE_ID,
        TEST_VM_ENDPOINT,
        TEST_BOOT_ADDRESS_SPACE_COUNT
    );
}

size_t vm_core_test_build_request(
    struct vm_core_test_context *context
)
{
    static const uint8_t path[] = "/bin/app";
    static const uint8_t argument[] = "app";
    static const uint8_t environment[] = "A=B";
    size_t cursor = MICROS_SPAWN_REQUEST_HEADER_SIZE;

    memset(context->request, 0, sizeof(context->request));
    vm_core_test_write_u32_le(&context->request[0], MICROS_SPAWN_REQUEST_VERSION);
    vm_core_test_write_u32_le(&context->request[4], MICROS_SPAWN_REQUEST_HEADER_SIZE);
    vm_core_test_write_u16_le(
        &context->request[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET],
        1
    );
    vm_core_test_write_u16_le(
        &context->request[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET],
        1
    );
    vm_core_test_write_u16_le(
        &context->request[MICROS_SPAWN_PATH_REFERENCE_OFFSET],
        (uint16_t)cursor
    );
    vm_core_test_write_u16_le(
        &context->request[MICROS_SPAWN_PATH_REFERENCE_OFFSET + 2],
        sizeof(path)
    );
    memcpy(&context->request[cursor], path, sizeof(path));
    cursor += sizeof(path);
    vm_core_test_write_u16_le(
        &context->request[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET],
        (uint16_t)cursor
    );
    vm_core_test_write_u16_le(
        &context->request[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + 2],
        sizeof(argument)
    );
    memcpy(&context->request[cursor], argument, sizeof(argument));
    cursor += sizeof(argument);
    vm_core_test_write_u16_le(
        &context->request[MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET],
        (uint16_t)cursor
    );
    vm_core_test_write_u16_le(
        &context->request[MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET + 2],
        sizeof(environment)
    );
    memcpy(&context->request[cursor], environment, sizeof(environment));
    cursor += sizeof(environment);
    vm_core_test_write_u32_le(&context->request[8], (uint32_t)cursor);
    return cursor;
}

size_t vm_core_test_build_elf(
    struct vm_core_test_context *context,
    uint64_t text_pages,
    uint64_t rodata_pages,
    uint64_t data_pages
)
{
    static const uint32_t flags[3] = {
        UINT32_C(0x5),
        UINT32_C(0x4),
        UINT32_C(0x6),
    };
    uint64_t page_counts[3] = {
        text_pages,
        rodata_pages,
        data_pages,
    };
    uint64_t virtual_address = MICROS_APPLICATION_ENTRY;
    size_t index;

    memset(context->executable, 0, sizeof(context->executable));
    context->executable[0] = 0x7f;
    context->executable[1] = 'E';
    context->executable[2] = 'L';
    context->executable[3] = 'F';
    context->executable[4] = 2;
    context->executable[5] = 1;
    context->executable[6] = 1;
    vm_core_test_write_u16_le(&context->executable[16], 2);
    vm_core_test_write_u16_le(&context->executable[18], 243);
    vm_core_test_write_u32_le(&context->executable[20], 1);
    vm_core_test_write_u64_le(&context->executable[24], MICROS_APPLICATION_ENTRY);
    vm_core_test_write_u64_le(&context->executable[32], 64);
    vm_core_test_write_u16_le(&context->executable[52], 64);
    vm_core_test_write_u16_le(&context->executable[54], 56);
    vm_core_test_write_u16_le(&context->executable[56], 3);

    for (index = 0; index < 3; ++index) {
        size_t offset = 64 + index * 56;
        uint64_t file_offset = index * UINT64_C(4096);
        uint64_t file_size = index == 2
            ? UINT64_C(1024)
            : UINT64_C(4096);
        uint64_t memory_size =
            page_counts[index] * UINT64_C(4096);

        vm_core_test_write_u32_le(&context->executable[offset], 1);
        vm_core_test_write_u32_le(&context->executable[offset + 4], flags[index]);
        vm_core_test_write_u64_le(&context->executable[offset + 8], file_offset);
        vm_core_test_write_u64_le(&context->executable[offset + 16], virtual_address);
        vm_core_test_write_u64_le(&context->executable[offset + 24], virtual_address);
        vm_core_test_write_u64_le(&context->executable[offset + 32], file_size);
        vm_core_test_write_u64_le(&context->executable[offset + 40], memory_size);
        vm_core_test_write_u64_le(&context->executable[offset + 48], 4096);
        virtual_address += memory_size;
    }
    for (index = 232; index < 8192 + 1024; ++index) {
        context->executable[index] = (uint8_t)(index * 17 + 3);
    }
    return 8192 + 1024;
}

struct micros_application_stack_values vm_core_test_stack_values(
    uint64_t pid
)
{
    const struct micros_application_stack_values values = {
        .pm_endpoint = UINT32_C(0x00001003),
        .vfs_endpoint = UINT32_C(0x00001004),
        .pid = pid,
    };

    return values;
}

bool vm_core_test_prepare_minimal_load(
    struct vm_core_test_context *context
)
{
    struct micros_application_stack_values values = vm_core_test_stack_values(7);
    size_t executable_size = vm_core_test_build_elf(context, 1, 1, 1);
    size_t request_size = vm_core_test_build_request(context);

    return micros_vm_load_prepare(
        &context->state,
        UINT64_C(0x1111),
        values.pid,
        context->executable,
        executable_size,
        context->request,
        request_size,
        &values
    ) == MICROS_VM_CORE_OK;
}

static uint64_t digest_byte(uint64_t digest, uint8_t byte)
{
    digest ^= byte;
    return digest * UINT64_C(1099511628211);
}

static uint64_t digest_u32(uint64_t digest, uint32_t value)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        digest = digest_byte(
            digest,
            (uint8_t)(value >> (index * 8))
        );
    }
    return digest;
}

static uint64_t digest_u64(uint64_t digest, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        digest = digest_byte(
            digest,
            (uint8_t)(value >> (index * 8))
        );
    }
    return digest;
}

uint64_t vm_core_test_expected_page_digest(
    const struct vm_core_test_context *context
)
{
    const struct micros_vm_load_transaction *transaction =
        &context->state.transaction;
    uint64_t digest = UINT64_C(14695981039346656037);
    size_t index;

    digest = digest_u32(digest, transaction->elf_plan.page_count);
    digest = digest_u32(digest, transaction->page_count);
    for (index = 0; index < transaction->page_count; ++index) {
        uint64_t virtual_address = index
                < transaction->elf_plan.page_count
            ? transaction->elf_plan.pages[index].virtual_address
            : MICROS_APPLICATION_STACK_BASE;
        uint32_t permissions = index
                < transaction->elf_plan.page_count
            ? transaction->elf_plan.pages[index].permissions
            : MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE;

        digest = digest_u64(digest, virtual_address);
        digest = digest_u64(
            digest,
            transaction->data_frames[index]
        );
        digest = digest_u32(digest, permissions);
        digest = digest_u32(digest, 0);
    }
    return digest;
}

bool vm_core_test_run_to_frozen(
    struct vm_core_test_context *context,
    uint64_t mapping_generation
)
{
    struct micros_vm_load_page_action action;
    uint32_t page_index;

    if (
        !vm_core_test_prepare_minimal_load(context)
        || micros_vm_load_bind(
            &context->state,
            (struct micros_process_handle){2, 7},
            UINT64_C(0x2222)
        ) != MICROS_VM_CORE_OK
    ) {
        return false;
    }
    for (
        page_index = 0;
        page_index < MICROS_APPLICATION_RESIDENT_PAGE_MAX;
        ++page_index
    ) {
        if (
            micros_vm_load_next_page(&context->state, &action)
                != MICROS_VM_CORE_OK
        ) {
            break;
        }
        if (
            action.page_index != page_index
            || micros_vm_load_scratch_mapped(&context->state, page_index)
                != MICROS_VM_CORE_OK
            || micros_vm_load_fill_scratch(
                &context->state,
                page_index,
                context->scratch_page,
                sizeof(context->scratch_page)
            ) != MICROS_VM_CORE_OK
            || micros_vm_load_scratch_committed(&context->state, page_index)
                != MICROS_VM_CORE_OK
        ) {
            return false;
        }
    }
    return (
        page_index == context->state.transaction.page_count
        && micros_vm_load_frozen(&context->state, mapping_generation)
            == MICROS_VM_CORE_OK
    );
}
