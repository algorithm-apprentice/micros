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

bool vm_bootstrap_test_test_builds_from_authoritative_state(
    struct vm_bootstrap_test_context *context
)
{
    const struct micros_process_handle vm_process = {1, 1};
    struct micros_vm_boot_summary summary;

    EXPECT_TRUE(vm_bootstrap_test_initialize_build_fixture(context));
    EXPECT_TRUE(
        micros_vm_boot_build(
            &context->info,
            2,
            3,
            &context->build_allocator,
            &context->build_ownership,
            2,
            UINT32_C(0x00001001),
            vm_process,
            &summary
        ) == MICROS_VM_BOOT_OK
        && summary.managed_frame_count == 8
        && summary.free_frame_count == 2
        && summary.vm_self_wired_frame_count == 2
        && summary.address_space_count == 2
        && summary.mapping_count == 3
        && context->info.header.kernel_frame_count == 3
        && context->info.header.service_wired_frame_count == 1
        && context->info.mappings[0].role
            == MICROS_VM_MAPPING_SELF_WIRED
        && context->info.mappings[1].role
            == MICROS_VM_MAPPING_SELF_WIRED
        && context->info.mappings[2].role
            == MICROS_VM_MAPPING_SERVICE_WIRED
        && micros_vm_boot_validate(&context->info, &summary)
            == MICROS_VM_BOOT_OK
        && micros_vm_boot_validate_authority(
            &context->info,
            &context->build_allocator,
            &context->build_ownership,
            vm_process
        ) == MICROS_VM_BOOT_OK
    );
    return true;
}

static bool missing_wired_target_clears_candidate(
    struct vm_bootstrap_test_context *context,
    const struct micros_vm_boot_summary *sentinel
)
{
    const struct micros_process_handle vm_process = {1, 1};
    const struct micros_process_handle service_process = {0, 1};
    struct micros_frame_owner service_user;
    struct micros_vm_boot_summary summary;

    EXPECT_TRUE(vm_bootstrap_test_initialize_build_fixture(context));
    EXPECT_TRUE(
        micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_USER,
            service_process,
            &service_user
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_prepare_handoff(
            &context->build_ownership,
            context->build_frames[4],
            service_user,
            MICROS_FRAME_HANDOFF_NONE
        ) == MICROS_FRAME_OWNERSHIP_OK
    );
    summary = *sentinel;
    EXPECT_TRUE(
        micros_vm_boot_build(
            &context->info,
            2,
            3,
            &context->build_allocator,
            &context->build_ownership,
            2,
            UINT32_C(0x00001001),
            vm_process,
            &summary
        ) == MICROS_VM_BOOT_ERROR_STATE
        && vm_bootstrap_test_storage_is_zero(&context->info, sizeof(context->info))
        && memcmp(&summary, sentinel, sizeof(summary)) == 0
    );
    return true;
}

static bool duplicate_mapping_clears_candidate(
    struct vm_bootstrap_test_context *context,
    const struct micros_vm_boot_summary *sentinel
)
{
    const struct micros_process_handle vm_process = {1, 1};
    struct micros_vm_boot_summary summary;

    EXPECT_TRUE(vm_bootstrap_test_initialize_build_fixture(context));
    context->info.mappings[2].physical_address = context->build_frames[2];
    summary = *sentinel;
    EXPECT_TRUE(
        micros_vm_boot_build(
            &context->info,
            2,
            3,
            &context->build_allocator,
            &context->build_ownership,
            2,
            UINT32_C(0x00001001),
            vm_process,
            &summary
        ) == MICROS_VM_BOOT_ERROR_MAPPING
        && vm_bootstrap_test_storage_is_zero(&context->info, sizeof(context->info))
        && memcmp(&summary, sentinel, sizeof(summary)) == 0
    );
    return true;
}

static bool temporary_owner_clears_candidate(
    struct vm_bootstrap_test_context *context,
    const struct micros_vm_boot_summary *sentinel
)
{
    const struct micros_process_handle vm_process = {1, 1};
    struct micros_frame_owner temporary;
    struct micros_vm_boot_summary summary;
    uint64_t temporary_frame;

    EXPECT_TRUE(
        vm_bootstrap_test_initialize_build_fixture(context)
        && micros_frame_owner_make_kernel(
            MICROS_FRAME_OWNER_KERNEL_TEMPORARY,
            &temporary
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_allocate(
            &context->build_ownership,
            temporary,
            &temporary_frame
        ) == MICROS_FRAME_OWNERSHIP_OK
    );
    summary = *sentinel;
    EXPECT_TRUE(
        micros_vm_boot_build(
            &context->info,
            2,
            3,
            &context->build_allocator,
            &context->build_ownership,
            2,
            UINT32_C(0x00001001),
            vm_process,
            &summary
        ) == MICROS_VM_BOOT_ERROR_STATE
        && vm_bootstrap_test_storage_is_zero(&context->info, sizeof(context->info))
        && memcmp(&summary, sentinel, sizeof(summary)) == 0
    );
    return true;
}

static bool stale_authority_is_rejected(
    struct vm_bootstrap_test_context *context
)
{
    const struct micros_process_handle vm_process = {1, 1};
    struct micros_frame_owner retained;
    struct micros_vm_boot_summary summary;
    uint64_t retained_frame;

    EXPECT_TRUE(
        vm_bootstrap_test_initialize_build_fixture(context)
        && micros_vm_boot_build(
            &context->info,
            2,
            3,
            &context->build_allocator,
            &context->build_ownership,
            2,
            UINT32_C(0x00001001),
            vm_process,
            &summary
        ) == MICROS_VM_BOOT_OK
        && micros_frame_owner_make_kernel(
            MICROS_FRAME_OWNER_KERNEL_RETAINED,
            &retained
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_allocate(
            &context->build_ownership,
            retained,
            &retained_frame
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_vm_boot_validate(&context->info, &summary)
            == MICROS_VM_BOOT_OK
        && micros_vm_boot_validate_authority(
            &context->info,
            &context->build_allocator,
            &context->build_ownership,
            vm_process
        ) == MICROS_VM_BOOT_ERROR_STATE
    );
    return true;
}

bool vm_bootstrap_test_test_build_failure_clears_candidate(
    struct vm_bootstrap_test_context *context
)
{
    struct micros_vm_boot_summary sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    return (
        missing_wired_target_clears_candidate(context, &sentinel)
        && duplicate_mapping_clears_candidate(context, &sentinel)
        && temporary_owner_clears_candidate(context, &sentinel)
        && stale_authority_is_rejected(context)
    );
}
