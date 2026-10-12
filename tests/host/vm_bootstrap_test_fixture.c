#include "vm_bootstrap_test_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static struct vm_bootstrap_test_context test_context;

struct vm_bootstrap_test_context *vm_bootstrap_test_context(void)
{
    return &test_context;
}

void vm_bootstrap_test_initialize_valid_info(
    struct vm_bootstrap_test_context *context
)
{
    struct micros_vm_boot_summary summary;

    memset(&context->info, 0, sizeof(context->info));
    context->info.header.version = MICROS_VM_BOOT_INFO_VERSION;
    context->info.header.header_size = MICROS_VM_BOOT_HEADER_SIZE;
    context->info.header.total_size = MICROS_VM_BOOT_INFO_SIZE;
    context->info.header.physical_range_size = MICROS_VM_PHYSICAL_RANGE_SIZE;
    context->info.header.managed_range_size = MICROS_VM_MANAGED_RANGE_SIZE;
    context->info.header.address_space_entry_size =
        MICROS_VM_ADDRESS_SPACE_SIZE;
    context->info.header.mapping_entry_size = MICROS_VM_MAPPING_SIZE;
    context->info.header.memory_range_capacity = MICROS_VM_MAX_MEMORY_RANGES;
    context->info.header.memory_range_count = 1;
    context->info.header.reserved_range_capacity =
        MICROS_VM_MAX_RESERVED_RANGES;
    context->info.header.reserved_range_count = 1;
    context->info.header.managed_range_capacity = MICROS_VM_MAX_MANAGED_RANGES;
    context->info.header.managed_range_count = 1;
    context->info.header.address_space_capacity =
        MICROS_VM_MAX_STATIC_ADDRESS_SPACES;
    context->info.header.address_space_count = 2;
    context->info.header.mapping_capacity = MICROS_VM_MAX_STATIC_MAPPINGS;
    context->info.header.mapping_count = 3;
    context->info.header.frame_state_capacity = MICROS_VM_MAX_MANAGED_FRAMES;
    context->info.header.frame_state_size = 1;
    context->info.header.service_id = 2;
    context->info.header.self_endpoint = UINT32_C(0x00001001);
    context->info.header.ownership_phase =
        MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP;
    context->info.header.managed_frame_count = 8;
    context->info.header.free_frame_count = 2;
    context->info.header.kernel_frame_count = 3;
    context->info.header.vm_self_wired_frame_count = 2;
    context->info.header.service_wired_frame_count = 1;

    context->info.memory_ranges[0] = (struct micros_vm_physical_range){
        .base = UINT64_C(0x80000000),
        .size = UINT64_C(0x00009000),
    };
    context->info.reserved_ranges[0] = (struct micros_vm_physical_range){
        .base = UINT64_C(0x80000000),
        .size = UINT64_C(0x00001000),
    };
    context->info.managed_ranges[0] = (struct micros_vm_managed_range){
        .base = UINT64_C(0x80001000),
        .frame_count = 8,
        .frame_index = 0,
    };
    context->info.address_spaces[0] = (struct micros_vm_address_space){
        .service_id = 2,
        .endpoint = UINT32_C(0x00001001),
        .process_generation = 1,
        .mapping_index = 0,
        .root_physical_address = UINT64_C(0x80001000),
        .process_slot = 1,
        .mapping_count = 2,
    };
    context->info.address_spaces[1] = (struct micros_vm_address_space){
        .service_id = 1,
        .endpoint = UINT32_C(0x00001000),
        .process_generation = 1,
        .mapping_index = 2,
        .root_physical_address = UINT64_C(0x80004000),
        .process_slot = 0,
        .mapping_count = 1,
    };
    context->info.mappings[0] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 1,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_EXECUTE,
        .role = MICROS_VM_MAPPING_SELF_WIRED,
        .virtual_address = MICROS_USER_VIRTUAL_BASE,
        .physical_address = UINT64_C(0x80002000),
    };
    context->info.mappings[1] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 1,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_WRITE,
        .role = MICROS_VM_MAPPING_SELF_WIRED,
        .virtual_address = MICROS_USER_VIRTUAL_BASE + MICROS_FRAME_SIZE,
        .physical_address = UINT64_C(0x80003000),
    };
    context->info.mappings[2] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 0,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_EXECUTE,
        .role = MICROS_VM_MAPPING_SERVICE_WIRED,
        .virtual_address = MICROS_USER_VIRTUAL_BASE,
        .physical_address = UINT64_C(0x80005000),
    };
    context->info.frame_states[0] = MICROS_VM_FRAME_KERNEL;
    context->info.frame_states[1] = MICROS_VM_FRAME_SELF_WIRED;
    context->info.frame_states[2] = MICROS_VM_FRAME_SELF_WIRED;
    context->info.frame_states[3] = MICROS_VM_FRAME_KERNEL;
    context->info.frame_states[4] = MICROS_VM_FRAME_SERVICE_WIRED;
    context->info.frame_states[5] = MICROS_VM_FRAME_FREE;
    context->info.frame_states[6] = MICROS_VM_FRAME_FREE;
    context->info.frame_states[7] = MICROS_VM_FRAME_KERNEL;
    if (
        micros_vm_boot_finalize(&context->info, &summary)
        != MICROS_VM_BOOT_OK
    ) {
        abort();
    }
}

bool vm_bootstrap_test_expect_error(
    struct vm_bootstrap_test_context *context,
    enum micros_vm_boot_error expected
)
{
    struct micros_vm_boot_summary summary;
    struct micros_vm_boot_summary sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    summary = sentinel;
    return (
        micros_vm_boot_validate(&context->info, &summary) == expected
        && memcmp(&summary, &sentinel, sizeof(summary)) == 0
    );
}

bool vm_bootstrap_test_storage_is_zero(const void *storage, size_t size)
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

static bool initialize_build_allocator_and_owners(
    struct vm_bootstrap_test_context *context,
    struct micros_frame_owner owners[8]
)
{
    const struct micros_physical_range memory = {
        .base = UINT64_C(0x80000000),
        .size = UINT64_C(0x00009000),
    };
    const struct micros_physical_range reserved = {
        .base = UINT64_C(0x80000000),
        .size = UINT64_C(0x00001000),
    };
    const struct micros_process_handle vm_process = {1, 1};
    const struct micros_process_handle service_process = {0, 1};

    memset(&context->info, 0, sizeof(context->info));
    memset(&context->build_allocator, 0, sizeof(context->build_allocator));
    memset(&context->build_ownership, 0, sizeof(context->build_ownership));
    memset(context->build_bitmap, 0, sizeof(context->build_bitmap));
    memset(context->build_frames, 0, sizeof(context->build_frames));
    if (
        micros_frame_allocator_initialize(
            &context->build_allocator,
            &memory,
            1,
            &reserved,
            1
        ) != MICROS_FRAME_ALLOCATOR_OK
        || micros_frame_ownership_initialize(
            &context->build_ownership,
            &context->build_allocator
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
            vm_process,
            &owners[0]
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_USER,
            vm_process,
            &owners[1]
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
            service_process,
            &owners[3]
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_USER,
            service_process,
            &owners[4]
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_owner_make_kernel(
            MICROS_FRAME_OWNER_KERNEL_RETAINED,
            &owners[7]
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_owner_make_kernel(
            MICROS_FRAME_OWNER_KERNEL_TEMPORARY,
            &owners[5]
        ) != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return false;
    }
    owners[2] = owners[1];
    owners[6] = owners[5];
    return true;
}

static bool allocate_and_stage_build_frames(
    struct vm_bootstrap_test_context *context,
    const struct micros_frame_owner owners[8]
)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        if (
            micros_frame_ownership_allocate(
                &context->build_ownership,
                owners[index],
                &context->build_frames[index]
            ) != MICROS_FRAME_OWNERSHIP_OK
        ) {
            return false;
        }
    }
    if (
        micros_frame_ownership_release(
            &context->build_ownership,
            owners[5],
            context->build_frames[5]
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_release(
            &context->build_ownership,
            owners[6],
            context->build_frames[6]
        ) != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return false;
    }
    context->build_bitmap[0] =
        (UINT64_C(1) << 1)
        | (UINT64_C(1) << 2)
        | (UINT64_C(1) << 4);
    if (
        micros_frame_ownership_prepare_wired_process_user_set(
            &context->build_ownership,
            context->build_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        ) != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return false;
    }
    return true;
}

static void populate_build_snapshot(
    struct vm_bootstrap_test_context *context
)
{
    context->info.address_spaces[0] = (struct micros_vm_address_space){
        .service_id = 2,
        .endpoint = UINT32_C(0x00001001),
        .process_generation = 1,
        .process_slot = 1,
        .mapping_count = 2,
        .mapping_index = 0,
        .root_physical_address = context->build_frames[0],
    };
    context->info.address_spaces[1] = (struct micros_vm_address_space){
        .service_id = 1,
        .endpoint = UINT32_C(0x00001000),
        .process_generation = 1,
        .process_slot = 0,
        .mapping_count = 1,
        .mapping_index = 2,
        .root_physical_address = context->build_frames[3],
    };
    context->info.mappings[0] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 1,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_EXECUTE,
        .virtual_address = MICROS_USER_VIRTUAL_BASE,
        .physical_address = context->build_frames[1],
    };
    context->info.mappings[1] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 1,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_WRITE,
        .virtual_address = MICROS_USER_VIRTUAL_BASE + MICROS_FRAME_SIZE,
        .physical_address = context->build_frames[2],
    };
    context->info.mappings[2] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 0,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_EXECUTE,
        .virtual_address = MICROS_USER_VIRTUAL_BASE,
        .physical_address = context->build_frames[4],
    };
}

bool vm_bootstrap_test_initialize_build_fixture(
    struct vm_bootstrap_test_context *context
)
{
    struct micros_frame_owner owners[8];

    if (
        !initialize_build_allocator_and_owners(context, owners)
        || !allocate_and_stage_build_frames(context, owners)
    ) {
        return false;
    }
    populate_build_snapshot(context);
    return true;
}
