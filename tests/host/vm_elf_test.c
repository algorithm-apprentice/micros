#include "tests/host/spawn_abi_test_internal.h"

static bool elf_plan_is_unchanged_on_error(
    struct spawn_abi_test_context *context,
    size_t file_size,
    enum micros_vm_elf_error expected
)
{
    struct micros_vm_elf_plan plan;
    struct micros_vm_elf_plan sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    plan = sentinel;
    return (
        micros_vm_elf_plan(context->elf_bytes, file_size, &plan) == expected
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
}

bool test_elf_plan(struct spawn_abi_test_context *context)
{
    struct micros_vm_elf_plan plan;
    size_t file_size = spawn_abi_test_build_valid_elf(context, 1, 1, 1);

    EXPECT_TRUE(
        micros_vm_elf_plan(context->elf_bytes, file_size, &plan)
            == MICROS_VM_ELF_OK
        && plan.entry == MICROS_APPLICATION_ENTRY
        && plan.segment_count == MICROS_APPLICATION_ELF_SEGMENT_COUNT
        && plan.page_count == MICROS_APPLICATION_IMAGE_PAGE_MIN
        && plan.segments[0].permissions
            == (
                MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
            )
        && plan.segments[1].permissions
            == (
                MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
            )
        && plan.segments[2].permissions
            == (
                MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
            )
        && plan.pages[0].virtual_address == MICROS_APPLICATION_ENTRY
        && plan.pages[0].file_offset == 0
        && plan.pages[0].file_size == 4096
        && plan.pages[2].file_offset == 8192
        && plan.pages[2].file_size == 1024
        && spawn_abi_test_bytes_are_zero(
            &plan.pages[plan.page_count],
            sizeof(plan.pages)
                - plan.page_count * sizeof(plan.pages[0])
        )
    );

    file_size = spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u32_le(&context->elf_bytes[48], 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[64 + 112 + 32], 0);
    EXPECT_TRUE(
        micros_vm_elf_plan(context->elf_bytes, file_size, &plan)
            == MICROS_VM_ELF_OK
        && plan.pages[2].file_offset == 0
        && plan.pages[2].file_size == 0
    );

    file_size = spawn_abi_test_build_valid_elf(context, 5, 5, 5);
    EXPECT_TRUE(
        micros_vm_elf_plan(context->elf_bytes, file_size, &plan)
            == MICROS_VM_ELF_OK
        && plan.page_count == MICROS_APPLICATION_IMAGE_PAGE_MAX
        && plan.pages[14].virtual_address
            == MICROS_APPLICATION_ENTRY + 14 * UINT64_C(4096)
        && plan.pages[14].file_size == 0
    );
    file_size = spawn_abi_test_build_valid_elf(context, 5, 5, 6);
    EXPECT_TRUE(
        elf_plan_is_unchanged_on_error(context,
            file_size,
            MICROS_VM_ELF_ERROR_TOO_BIG
        )
    );

    file_size = spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    EXPECT_TRUE(
        micros_vm_elf_plan(
            context->elf_bytes,
            MICROS_APPLICATION_EXECUTABLE_SIZE_MAX,
            &plan
        ) == MICROS_VM_ELF_OK
    );
    EXPECT_TRUE(
        elf_plan_is_unchanged_on_error(context,
            MICROS_APPLICATION_EXECUTABLE_SIZE_MAX + 1,
            MICROS_VM_ELF_ERROR_TOO_BIG
        )
    );
    return true;
}

static bool reject_elf_truncation_and_identity(struct spawn_abi_test_context *context, size_t file_size)
{
    size_t index;

    for (index = 0; index < 232; ++index) {
        EXPECT_TRUE(
            elf_plan_is_unchanged_on_error(context,
                index,
                MICROS_VM_ELF_ERROR_EXECUTABLE
            )
        );
    }

    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    context->elf_bytes[0] = 0;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    context->elf_bytes[4] = 1;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    context->elf_bytes[5] = 2;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    context->elf_bytes[6] = 0;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    context->elf_bytes[7] = 1;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    context->elf_bytes[8] = 1;
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    for (index = 9; index < 16; ++index) {
        spawn_abi_test_build_valid_elf(context, 1, 1, 1);
        context->elf_bytes[index] = 1;
        EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
            file_size,
            MICROS_VM_ELF_ERROR_EXECUTABLE
        ));
    }
    return true;
}

static bool reject_elf_fixed_header_values(struct spawn_abi_test_context *context, size_t file_size)
{
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u16_le(&context->elf_bytes[16], 3);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u16_le(&context->elf_bytes[18], 62);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u32_le(&context->elf_bytes[20], 0);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[24], MICROS_APPLICATION_ENTRY + 4);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[32], 128);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u32_le(&context->elf_bytes[48], 2);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u32_le(&context->elf_bytes[48], 1);
    {
        struct micros_vm_elf_plan plan;

        EXPECT_TRUE(
            micros_vm_elf_plan(context->elf_bytes, file_size, &plan)
                == MICROS_VM_ELF_OK
        );
    }
    return true;
}

static bool check_elf_header_sizes_and_ignored_sections(struct spawn_abi_test_context *context, size_t file_size)
{
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u16_le(&context->elf_bytes[52], 63);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u16_le(&context->elf_bytes[54], 55);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u16_le(&context->elf_bytes[56], 4);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));

    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[40], UINT64_MAX);
    spawn_abi_test_write_u16_le(&context->elf_bytes[58], UINT16_MAX);
    spawn_abi_test_write_u16_le(&context->elf_bytes[60], UINT16_MAX);
    spawn_abi_test_write_u16_le(&context->elf_bytes[62], UINT16_MAX);
    {
        struct micros_vm_elf_plan plan;

        EXPECT_TRUE(
            micros_vm_elf_plan(context->elf_bytes, file_size, &plan)
                == MICROS_VM_ELF_OK
        );
    }
    return true;
}

bool test_elf_header_rejections(struct spawn_abi_test_context *context)
{
    size_t file_size = spawn_abi_test_build_valid_elf(context, 1, 1, 1);

    return (
        reject_elf_truncation_and_identity(context, file_size)
        && reject_elf_fixed_header_values(context, file_size)
        && check_elf_header_sizes_and_ignored_sections(context, file_size)
    );
}

static bool reject_elf_segment_type_permissions_and_geometry(struct spawn_abi_test_context *context, size_t file_size)
{
    size_t ph0 = 64;
    size_t ph1 = 64 + 56;

    spawn_abi_test_write_u32_le(&context->elf_bytes[ph0], 0);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u32_le(&context->elf_bytes[ph0 + 4], 7);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph0 + 48], 2048);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph1 + 8], 4097);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 16],
        MICROS_APPLICATION_ENTRY + 4097
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 24],
        MICROS_APPLICATION_ENTRY + 4097
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 24],
        MICROS_APPLICATION_ENTRY + 8192
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    return true;
}

static bool reject_elf_segment_sizes_and_overflow(struct spawn_abi_test_context *context, size_t file_size)
{
    size_t ph0 = 64;
    size_t ph1 = 64 + 56;
    size_t ph2 = 64 + 112;

    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph1 + 40], 0);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph1 + 40], 4097);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph1 + 32], 8192);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph2 + 32], 4096);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph2 + 8], UINT64_MAX - 4095);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph2 + 32], 8192);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph2 + 40], 8192);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph0 + 16], UINT64_MAX - 4095);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph0 + 24], UINT64_MAX - 4095);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph0 + 40], 8192);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    return true;
}

static bool reject_elf_overlap_contiguity_and_scratch(struct spawn_abi_test_context *context, size_t file_size)
{
    size_t ph0 = 64;
    size_t ph1 = 64 + 56;

    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph0 + 16],
        MICROS_APPLICATION_ENTRY + 4096
    );
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph0 + 24],
        MICROS_APPLICATION_ENTRY + 4096
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 16],
        MICROS_APPLICATION_ENTRY + 8192
    );
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 24],
        MICROS_APPLICATION_ENTRY + 8192
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 16],
        MICROS_APPLICATION_ENTRY
    );
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 24],
        MICROS_APPLICATION_ENTRY
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(&context->elf_bytes[ph1 + 8], 0);
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));

    spawn_abi_test_build_valid_elf(context, 1, 1, 1);
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph0 + 40],
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
            - MICROS_APPLICATION_ENTRY
    );
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 16],
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
    );
    spawn_abi_test_write_u64_le(
        &context->elf_bytes[ph1 + 24],
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
    );
    EXPECT_TRUE(elf_plan_is_unchanged_on_error(context,
        file_size,
        MICROS_VM_ELF_ERROR_EXECUTABLE
    ));
    return true;
}

bool test_elf_segment_rejections(struct spawn_abi_test_context *context)
{
    size_t file_size = spawn_abi_test_build_valid_elf(context, 1, 1, 1);

    return (
        reject_elf_segment_type_permissions_and_geometry(context, file_size)
        && reject_elf_segment_sizes_and_overflow(context, file_size)
        && reject_elf_overlap_contiguity_and_scratch(context, file_size)
    );
}
