#include "vm_bootstrap_test_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

bool vm_bootstrap_test_test_abi_contract(
    struct vm_bootstrap_test_context *context
)
{
    (void)context;

    return (
        sizeof(struct micros_vm_boot_header) == 192
        && sizeof(struct micros_vm_physical_range) == 16
        && sizeof(struct micros_vm_managed_range) == 24
        && sizeof(struct micros_vm_address_space) == 32
        && sizeof(struct micros_vm_mapping) == 24
        && sizeof(struct micros_vm_boot_info) == 364704
        && offsetof(struct micros_vm_boot_header, managed_frame_count)
            == 64
        && offsetof(struct micros_vm_boot_header, digest) == 112
        && offsetof(struct micros_vm_boot_header, ownership_phase)
            == 120
        && offsetof(
            struct micros_vm_boot_header,
            address_space_entry_size
        )
            == 128
        && offsetof(struct micros_vm_boot_header, reserved) == 152
        && offsetof(struct micros_vm_physical_range, base) == 0
        && offsetof(struct micros_vm_physical_range, size) == 8
        && offsetof(struct micros_vm_managed_range, base) == 0
        && offsetof(struct micros_vm_managed_range, frame_count) == 8
        && offsetof(struct micros_vm_managed_range, frame_index) == 16
        && offsetof(struct micros_vm_address_space, process_slot) == 12
        && offsetof(struct micros_vm_address_space, mapping_count) == 14
        && offsetof(struct micros_vm_address_space, mapping_index) == 16
        && offsetof(struct micros_vm_address_space, flags) == 20
        && offsetof(
            struct micros_vm_address_space,
            root_physical_address
        ) == 24
        && offsetof(struct micros_vm_boot_info, memory_ranges) == 192
        && offsetof(struct micros_vm_boot_info, reserved_ranges) == 448
        && offsetof(struct micros_vm_boot_info, managed_ranges) == 1728
        && offsetof(struct micros_vm_boot_info, address_spaces) == 4032
        && offsetof(struct micros_vm_boot_info, mappings) == 4256
        && offsetof(struct micros_vm_boot_info, frame_states) == 102560
        && sizeof(struct micros_vm_boot_info)
            <= 90 * MICROS_FRAME_SIZE
    );
}

bool vm_bootstrap_test_test_valid_snapshot(
    struct vm_bootstrap_test_context *context
)
{
    struct micros_vm_boot_summary summary;
    uint64_t digest;

    vm_bootstrap_test_initialize_valid_info(context);
    EXPECT_TRUE(
        micros_vm_boot_validate(&context->info, &summary)
            == MICROS_VM_BOOT_OK
        && summary.version == 1
        && summary.managed_range_count == 1
        && summary.address_space_count == 2
        && summary.mapping_count == 3
        && summary.managed_frame_count == 8
        && summary.free_frame_count == 2
        && summary.vm_self_wired_frame_count == 2
        && summary.digest == context->info.header.digest
    );
    context->info.header.digest ^= UINT64_C(0xffff);
    EXPECT_TRUE(
        micros_vm_boot_digest(&context->info, &digest) == MICROS_VM_BOOT_OK
        && digest == summary.digest
        && vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_DIGEST)
    );
    return true;
}

bool vm_bootstrap_test_test_finalize_is_failure_atomic(
    struct vm_bootstrap_test_context *context
)
{
    struct micros_vm_boot_summary summary;
    struct micros_vm_boot_summary sentinel;
    uint64_t valid_digest;

    vm_bootstrap_test_initialize_valid_info(context);
    valid_digest = context->info.header.digest;
    EXPECT_TRUE(
        micros_vm_boot_finalize(&context->info, &summary)
            == MICROS_VM_BOOT_ERROR_STATE
        && context->info.header.digest == valid_digest
    );
    vm_bootstrap_test_initialize_valid_info(context);
    context->info.header.digest = 0;
    context->info.frame_states[4] = MICROS_VM_FRAME_KERNEL;
    memset(&sentinel, 0xa5, sizeof(sentinel));
    summary = sentinel;
    EXPECT_TRUE(
        micros_vm_boot_finalize(&context->info, &summary)
            == MICROS_VM_BOOT_ERROR_MAPPING
        && context->info.header.digest == 0
        && memcmp(&summary, &sentinel, sizeof(summary)) == 0
    );
    return true;
}

bool vm_bootstrap_test_test_rejections(
    struct vm_bootstrap_test_context *context
)
{
    vm_bootstrap_test_initialize_valid_info(context);
    context->info.header.total_size = 0;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_SHAPE));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.memory_ranges[0].size += MICROS_FRAME_SIZE;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_RANGE));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.memory_ranges[0].base += 1;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_RANGE));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.reserved_ranges[0].base += 1;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_RANGE));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.managed_ranges[0].frame_index = 1;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_RANGE));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.address_spaces[1].endpoint = UINT32_C(0x00001002);
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_IDENTITY));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.address_spaces[1].service_id = 64;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_IDENTITY));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.header.service_id = 64;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_SHAPE));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.address_spaces[1].endpoint = context->info.address_spaces[0].endpoint;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_IDENTITY));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.address_spaces[1].endpoint = UINT32_C(0x00002001);
    context->info.address_spaces[1].process_slot = 1;
    context->info.address_spaces[1].process_generation = 2;
    context->info.mappings[2].process_slot = 1;
    context->info.mappings[2].process_generation = 2;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_IDENTITY));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.address_spaces[1].mapping_index = 1;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_IDENTITY));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.mappings[1].process_generation = 2;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_MAPPING));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.mappings[2].physical_address =
        context->info.mappings[1].physical_address;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_MAPPING));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.frame_states[4] = MICROS_VM_FRAME_KERNEL;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_MAPPING));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.header.free_frame_count = 3;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_COUNT));

    vm_bootstrap_test_initialize_valid_info(context);
    context->info.frame_states[8] = MICROS_VM_FRAME_FREE;
    EXPECT_TRUE(vm_bootstrap_test_expect_error(context, MICROS_VM_BOOT_ERROR_COUNT));
    return true;
}

bool vm_bootstrap_test_test_misaligned_storage(
    struct vm_bootstrap_test_context *context
)
{
    (void)context;

    _Alignas(MICROS_VM_BOOT_INFO_ALIGNMENT)
    static uint8_t storage[
        sizeof(struct micros_vm_boot_info)
        + MICROS_VM_BOOT_INFO_ALIGNMENT
    ];
    struct micros_vm_boot_summary summary;
    struct micros_vm_boot_info *misaligned =
        (struct micros_vm_boot_info *)(void *)(storage + 1);
    struct micros_vm_boot_summary sentinel;
    uint64_t digest = UINT64_C(0xa5a5a5a5a5a5a5a5);
    uint8_t first_byte = storage[1];

    memset(&sentinel, 0xa5, sizeof(sentinel));
    summary = sentinel;
    EXPECT_TRUE(
        micros_vm_boot_validate(misaligned, &summary)
            == MICROS_VM_BOOT_ERROR_ARGUMENT
        && micros_vm_boot_digest(misaligned, &digest)
            == MICROS_VM_BOOT_ERROR_ARGUMENT
        && digest == UINT64_C(0xa5a5a5a5a5a5a5a5)
        && micros_vm_boot_finalize(misaligned, &summary)
            == MICROS_VM_BOOT_ERROR_ARGUMENT
        && storage[1] == first_byte
        && memcmp(&summary, &sentinel, sizeof(summary)) == 0
    );
    return true;
}
