#include "micros/vm_bootstrap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

_Alignas(MICROS_VM_BOOT_INFO_ALIGNMENT)
static struct micros_vm_boot_info info;

static void initialize_valid_info(void)
{
    struct micros_vm_boot_summary summary;

    memset(&info, 0, sizeof(info));
    info.header.version = MICROS_VM_BOOT_INFO_VERSION;
    info.header.header_size = MICROS_VM_BOOT_HEADER_SIZE;
    info.header.total_size = MICROS_VM_BOOT_INFO_SIZE;
    info.header.physical_range_size = MICROS_VM_PHYSICAL_RANGE_SIZE;
    info.header.managed_range_size = MICROS_VM_MANAGED_RANGE_SIZE;
    info.header.address_space_entry_size =
        MICROS_VM_ADDRESS_SPACE_SIZE;
    info.header.mapping_entry_size = MICROS_VM_MAPPING_SIZE;
    info.header.memory_range_capacity = MICROS_VM_MAX_MEMORY_RANGES;
    info.header.memory_range_count = 1;
    info.header.reserved_range_capacity =
        MICROS_VM_MAX_RESERVED_RANGES;
    info.header.reserved_range_count = 1;
    info.header.managed_range_capacity = MICROS_VM_MAX_MANAGED_RANGES;
    info.header.managed_range_count = 1;
    info.header.address_space_capacity =
        MICROS_VM_MAX_STATIC_ADDRESS_SPACES;
    info.header.address_space_count = 2;
    info.header.mapping_capacity = MICROS_VM_MAX_STATIC_MAPPINGS;
    info.header.mapping_count = 3;
    info.header.frame_state_capacity = MICROS_VM_MAX_MANAGED_FRAMES;
    info.header.frame_state_size = 1;
    info.header.service_id = 2;
    info.header.self_endpoint = UINT32_C(0x00001001);
    info.header.ownership_phase =
        MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP;
    info.header.managed_frame_count = 8;
    info.header.free_frame_count = 2;
    info.header.kernel_frame_count = 3;
    info.header.vm_self_wired_frame_count = 2;
    info.header.service_wired_frame_count = 1;

    info.memory_ranges[0] = (struct micros_vm_physical_range){
        .base = UINT64_C(0x80000000),
        .size = UINT64_C(0x00009000),
    };
    info.reserved_ranges[0] = (struct micros_vm_physical_range){
        .base = UINT64_C(0x80000000),
        .size = UINT64_C(0x00001000),
    };
    info.managed_ranges[0] = (struct micros_vm_managed_range){
        .base = UINT64_C(0x80001000),
        .frame_count = 8,
        .frame_index = 0,
    };
    info.address_spaces[0] = (struct micros_vm_address_space){
        .service_id = 2,
        .endpoint = UINT32_C(0x00001001),
        .process_generation = 1,
        .mapping_index = 0,
        .root_physical_address = UINT64_C(0x80001000),
        .process_slot = 1,
        .mapping_count = 2,
    };
    info.address_spaces[1] = (struct micros_vm_address_space){
        .service_id = 1,
        .endpoint = UINT32_C(0x00001000),
        .process_generation = 1,
        .mapping_index = 2,
        .root_physical_address = UINT64_C(0x80004000),
        .process_slot = 0,
        .mapping_count = 1,
    };
    info.mappings[0] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 1,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_EXECUTE,
        .role = MICROS_VM_MAPPING_SELF_WIRED,
        .virtual_address = MICROS_USER_VIRTUAL_BASE,
        .physical_address = UINT64_C(0x80002000),
    };
    info.mappings[1] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 1,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_WRITE,
        .role = MICROS_VM_MAPPING_SELF_WIRED,
        .virtual_address = MICROS_USER_VIRTUAL_BASE + MICROS_FRAME_SIZE,
        .physical_address = UINT64_C(0x80003000),
    };
    info.mappings[2] = (struct micros_vm_mapping){
        .process_generation = 1,
        .process_slot = 0,
        .permissions =
            MICROS_VM_PERMISSION_READ | MICROS_VM_PERMISSION_EXECUTE,
        .role = MICROS_VM_MAPPING_SERVICE_WIRED,
        .virtual_address = MICROS_USER_VIRTUAL_BASE,
        .physical_address = UINT64_C(0x80005000),
    };
    info.frame_states[0] = MICROS_VM_FRAME_KERNEL;
    info.frame_states[1] = MICROS_VM_FRAME_SELF_WIRED;
    info.frame_states[2] = MICROS_VM_FRAME_SELF_WIRED;
    info.frame_states[3] = MICROS_VM_FRAME_KERNEL;
    info.frame_states[4] = MICROS_VM_FRAME_SERVICE_WIRED;
    info.frame_states[5] = MICROS_VM_FRAME_FREE;
    info.frame_states[6] = MICROS_VM_FRAME_FREE;
    info.frame_states[7] = MICROS_VM_FRAME_KERNEL;
    if (
        micros_vm_boot_finalize(&info, &summary)
        != MICROS_VM_BOOT_OK
    ) {
        abort();
    }
}

static bool expect_error(enum micros_vm_boot_error expected)
{
    struct micros_vm_boot_summary summary;
    struct micros_vm_boot_summary sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    summary = sentinel;
    return (
        micros_vm_boot_validate(&info, &summary) == expected
        && memcmp(&summary, &sentinel, sizeof(summary)) == 0
    );
}

static bool test_abi_contract(void)
{
    return (
        sizeof(struct micros_vm_boot_header) == 192
        && sizeof(struct micros_vm_physical_range) == 16
        && sizeof(struct micros_vm_managed_range) == 24
        && sizeof(struct micros_vm_address_space) == 32
        && sizeof(struct micros_vm_mapping) == 24
        && sizeof(struct micros_vm_boot_info) == 364672
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
        && offsetof(struct micros_vm_boot_info, mappings) == 4224
        && offsetof(struct micros_vm_boot_info, frame_states) == 102528
        && sizeof(struct micros_vm_boot_info)
            <= 90 * MICROS_FRAME_SIZE
    );
}

static bool test_valid_snapshot(void)
{
    struct micros_vm_boot_summary summary;
    uint64_t digest;

    initialize_valid_info();
    EXPECT_TRUE(
        micros_vm_boot_validate(&info, &summary)
            == MICROS_VM_BOOT_OK
        && summary.version == 1
        && summary.managed_range_count == 1
        && summary.address_space_count == 2
        && summary.mapping_count == 3
        && summary.managed_frame_count == 8
        && summary.free_frame_count == 2
        && summary.vm_self_wired_frame_count == 2
        && summary.digest == info.header.digest
    );
    info.header.digest ^= UINT64_C(0xffff);
    EXPECT_TRUE(
        micros_vm_boot_digest(&info, &digest) == MICROS_VM_BOOT_OK
        && digest == summary.digest
        && expect_error(MICROS_VM_BOOT_ERROR_DIGEST)
    );
    return true;
}

static bool test_finalize_is_failure_atomic(void)
{
    struct micros_vm_boot_summary summary;
    struct micros_vm_boot_summary sentinel;
    uint64_t valid_digest;

    initialize_valid_info();
    valid_digest = info.header.digest;
    EXPECT_TRUE(
        micros_vm_boot_finalize(&info, &summary)
            == MICROS_VM_BOOT_ERROR_STATE
        && info.header.digest == valid_digest
    );
    initialize_valid_info();
    info.header.digest = 0;
    info.frame_states[4] = MICROS_VM_FRAME_KERNEL;
    memset(&sentinel, 0xa5, sizeof(sentinel));
    summary = sentinel;
    EXPECT_TRUE(
        micros_vm_boot_finalize(&info, &summary)
            == MICROS_VM_BOOT_ERROR_MAPPING
        && info.header.digest == 0
        && memcmp(&summary, &sentinel, sizeof(summary)) == 0
    );
    return true;
}

static bool test_rejections(void)
{
    initialize_valid_info();
    info.header.total_size = 0;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_SHAPE));

    initialize_valid_info();
    info.memory_ranges[0].size += MICROS_FRAME_SIZE;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_RANGE));

    initialize_valid_info();
    info.memory_ranges[0].base += 1;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_RANGE));

    initialize_valid_info();
    info.reserved_ranges[0].base += 1;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_RANGE));

    initialize_valid_info();
    info.managed_ranges[0].frame_index = 1;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_RANGE));

    initialize_valid_info();
    info.address_spaces[1].endpoint = UINT32_C(0x00001002);
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_IDENTITY));

    initialize_valid_info();
    info.address_spaces[1].service_id = 64;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_IDENTITY));

    initialize_valid_info();
    info.header.service_id = 64;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_SHAPE));

    initialize_valid_info();
    info.address_spaces[1].endpoint = info.address_spaces[0].endpoint;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_IDENTITY));

    initialize_valid_info();
    info.address_spaces[1].endpoint = UINT32_C(0x00002001);
    info.address_spaces[1].process_slot = 1;
    info.address_spaces[1].process_generation = 2;
    info.mappings[2].process_slot = 1;
    info.mappings[2].process_generation = 2;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_IDENTITY));

    initialize_valid_info();
    info.address_spaces[1].mapping_index = 1;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_IDENTITY));

    initialize_valid_info();
    info.mappings[1].process_generation = 2;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_MAPPING));

    initialize_valid_info();
    info.mappings[2].physical_address =
        info.mappings[1].physical_address;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_MAPPING));

    initialize_valid_info();
    info.frame_states[4] = MICROS_VM_FRAME_KERNEL;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_MAPPING));

    initialize_valid_info();
    info.header.free_frame_count = 3;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_COUNT));

    initialize_valid_info();
    info.frame_states[8] = MICROS_VM_FRAME_FREE;
    EXPECT_TRUE(expect_error(MICROS_VM_BOOT_ERROR_COUNT));
    return true;
}

static bool test_misaligned_storage(void)
{
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

int main(void)
{
    return (
        test_abi_contract()
        && test_valid_snapshot()
        && test_finalize_is_failure_atomic()
        && test_rejections()
        && test_misaligned_storage()
    )
        ? 0
        : 1;
}
