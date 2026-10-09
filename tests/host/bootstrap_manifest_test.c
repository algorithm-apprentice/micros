#include "micros/bootstrap.h"
#include "micros/vm_bootstrap.h"

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

static void set_name(
    char output[MICROS_BOOTSTRAP_NAME_SIZE],
    const char *name
)
{
    size_t length = strlen(name);

    memset(output, 0, MICROS_BOOTSTRAP_NAME_SIZE);
    memcpy(output, name, length);
}

static struct micros_privilege_profile profile(
    uint8_t id,
    const char *name
)
{
    struct micros_privilege_profile result;

    memset(&result, 0, sizeof(result));
    result.id = id;
    set_name(result.name, name);
    if (id == MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER) {
        result.operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_REPLY;
        result.kernel_operations =
            MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL;
    } else if (id == MICROS_PRIVILEGE_PROFILE_APPLICATION) {
        result.operations = MICROS_PRIVILEGE_OPERATION_CALL;
        result.call_targets =
            (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_PM
            )
            | (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS
            );
    } else {
        result.operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL;
        result.call_targets =
            UINT32_C(1)
            << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER;
        if (id == MICROS_PRIVILEGE_PROFILE_VM) {
            result.operations |= MICROS_PRIVILEGE_OPERATION_REPLY;
            result.kernel_operations =
                MICROS_KERNEL_OPERATION_VM_HANDOFF;
        } else if (id == MICROS_PRIVILEGE_PROFILE_PM) {
            result.operations |=
                MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE;
            result.kernel_operations =
                MICROS_KERNEL_OPERATION_PM_CONTROL;
        }
    }
    return result;
}

static void initialize_manifest(
    struct micros_bootstrap_manifest *manifest
)
{
    struct micros_bootstrap_manifest_entry *launcher;
    struct micros_bootstrap_manifest_entry *vm;

    memset(manifest, 0, sizeof(*manifest));
    manifest->header.magic = MICROS_BOOTSTRAP_MANIFEST_MAGIC;
    manifest->header.version = MICROS_BOOTSTRAP_MANIFEST_VERSION;
    manifest->header.header_size =
        MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE;
    manifest->header.entry_size =
        MICROS_BOOTSTRAP_MANIFEST_ENTRY_SIZE;
    manifest->header.entry_capacity =
        MICROS_BOOTSTRAP_SERVICE_CAPACITY;
    manifest->header.entry_count = 2;
    manifest->header.total_user_page_limit = 7;
    manifest->header.manifest_size = MICROS_BOOTSTRAP_MANIFEST_SIZE;

    vm = &manifest->entries[0];
    vm->service_id = 2;
    vm->image_id = 102;
    vm->process_slot = 1;
    vm->stack_page_count = 1;
    vm->profile_id = 2;
    set_name(vm->service_name, "vm");
    set_name(vm->profile_name, "VM");
    vm->prerequisites = UINT64_C(1);
    vm->ready_timeout_counter_ticks = 100;
    vm->user_page_limit = 3;
    vm->role_flags = MICROS_BOOTSTRAP_ROLE_VM;

    launcher = &manifest->entries[1];
    launcher->service_id = 1;
    launcher->image_id = 101;
    launcher->process_slot = 0;
    launcher->stack_page_count = 1;
    launcher->profile_id = 1;
    set_name(launcher->service_name, "bootstrap-launcher");
    set_name(launcher->profile_name, "BOOTSTRAP_LAUNCHER");
    launcher->user_page_limit = 4;
    launcher->role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER;
}

static bool test_manifest_contract(void)
{
    return (
        MICROS_BOOTSTRAP_MANIFEST_VERSION == 1
        && MICROS_BOOTSTRAP_MANIFEST_MAGIC
            == UINT32_C(0x3153424d)
        && MICROS_BOOTSTRAP_SERVICE_CAPACITY == 6
        && MICROS_BOOTSTRAP_ROLE_PM == UINT32_C(0x8)
        && MICROS_BOOTSTRAP_ROLE_DEFINED_MASK == UINT32_C(0xf)
        && MICROS_KERNEL_OPERATION_PM_CONTROL == UINT64_C(0x4)
        && MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER == 1
        && MICROS_PRIVILEGE_PROFILE_VM == 2
        && MICROS_PRIVILEGE_PROFILE_PM == 3
        && MICROS_PRIVILEGE_PROFILE_TTY == 4
        && MICROS_PRIVILEGE_PROFILE_RAMFS == 5
        && MICROS_PRIVILEGE_PROFILE_VFS == 6
        && MICROS_PRIVILEGE_PROFILE_APPLICATION == 7
        && MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED == UINT64_C(0x1)
        && sizeof(struct micros_bootstrap_manifest_header) == 64
        && sizeof(struct micros_bootstrap_manifest_entry) == 192
        && sizeof(struct micros_bootstrap_manifest) == 1216
        && sizeof(struct micros_bootstrap_manifest_plan) == 60
        && sizeof(struct micros_bootstrap_service_config) == 128
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            prerequisites
        ) == 80
        && offsetof(
            struct micros_bootstrap_service_config,
            services
        ) == 24
        && offsetof(
            struct micros_bootstrap_manifest_plan,
            pm_service_id
        ) == 20
        && MICROS_BOOTSTRAP_MANIFEST_VIEW
            == UINT64_C(0x000000007fffe000)
    );
}

static bool test_manifest_validation(void)
{
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_manifest_plan plan;
    struct micros_bootstrap_manifest_plan sentinel;
    struct micros_bootstrap_expected_service expected[2];
    struct micros_bootstrap_image_info images[2];
    struct micros_privilege_profile profiles[2];
    unsigned char misaligned_storage[
        sizeof(struct micros_bootstrap_manifest)
        + _Alignof(struct micros_bootstrap_manifest)
    ];
    unsigned char *misaligned_manifest;

    initialize_manifest(&manifest);
    memset(expected, 0, sizeof(expected));
    expected[0] = (struct micros_bootstrap_expected_service){
        .service_id = 1,
        .image_id = 101,
        .process_slot = 0,
        .profile_id = 1,
        .role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER,
    };
    set_name(expected[0].service_name, "bootstrap-launcher");
    set_name(expected[0].profile_name, "BOOTSTRAP_LAUNCHER");
    expected[1] = (struct micros_bootstrap_expected_service){
        .service_id = 2,
        .image_id = 102,
        .process_slot = 1,
        .profile_id = 2,
        .prerequisites = UINT64_C(1),
        .call_targets = UINT32_C(1) << 1,
        .role_flags = MICROS_BOOTSTRAP_ROLE_VM,
    };
    set_name(expected[1].service_name, "vm");
    set_name(expected[1].profile_name, "VM");
    images[0] = (struct micros_bootstrap_image_info){
        .version = MICROS_BOOTSTRAP_IMAGE_VERSION,
        .image_id = 101,
        .entry = MICROS_USER_VIRTUAL_BASE,
        .config_address = MICROS_USER_VIRTUAL_BASE + 0x1000,
        .config_size = sizeof(struct micros_bootstrap_service_config),
        .page_count = 2,
        .image_end = MICROS_USER_VIRTUAL_BASE + 0x2000,
        .config_initially_zero = true,
    };
    images[1] = images[0];
    images[1].image_id = 102;
    images[1].vm_boot_info_address =
        MICROS_USER_VIRTUAL_BASE + 0x2000;
    images[1].vm_boot_info_size = MICROS_VM_BOOT_INFO_SIZE;
    images[1].vm_boot_info_initially_zero = true;
    profiles[0] = profile(1, "BOOTSTRAP_LAUNCHER");
    profiles[1] = profile(2, "VM");

    memset(&plan, 0, sizeof(plan));
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            2,
            images,
            2,
            profiles,
            2,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_OK
        && plan.entry_count == 2
        && plan.controller_service_id == 1
        && plan.vm_service_id == 2
        && plan.ordered_service_ids[0] == 1
        && plan.ordered_service_ids[1] == 2
        && plan.ordered_manifest_indices[0] == 1
        && plan.ordered_manifest_indices[1] == 0
    );

    memset(&sentinel, 0xa5, sizeof(sentinel));
    plan = sentinel;
    manifest.header.magic = 0;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            2,
            images,
            2,
            profiles,
            2,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_SHAPE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    initialize_manifest(&manifest);
    misaligned_manifest = (unsigned char *)(
        (
            (uintptr_t)misaligned_storage
            + _Alignof(struct micros_bootstrap_manifest) - 1
        )
        & ~(
            (uintptr_t)_Alignof(struct micros_bootstrap_manifest)
            - 1
        )
    ) + 1;
    memcpy(misaligned_manifest, &manifest, sizeof(manifest));
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            (const struct micros_bootstrap_manifest *)(
                const void *
            )misaligned_manifest,
            expected,
            2,
            images,
            2,
            profiles,
            2,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_ARGUMENT
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    profiles[0].operations = MICROS_PRIVILEGE_OPERATION_RECEIVE;
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            2,
            images,
            2,
            profiles,
            2,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    profiles[0] = profile(1, "BOOTSTRAP_LAUNCHER");
    profiles[0].kernel_operations |=
        MICROS_KERNEL_OPERATION_VM_HANDOFF;
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            2,
            images,
            2,
            profiles,
            2,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    profiles[0] = profile(1, "BOOTSTRAP_LAUNCHER");
    profiles[1].call_targets = 0;
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            2,
            images,
            2,
            profiles,
            2,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    profiles[1] = profile(2, "VM");
    profiles[1].kernel_operations = 0;
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            2,
            images,
            2,
            profiles,
            2,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    profiles[1] = profile(2, "VM");
    profiles[1].kernel_operations =
        MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL;
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            2,
            images,
            2,
            profiles,
            2,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    profiles[1] = profile(2, "BOOTSTRAP_LAUNCHER");
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            2,
            images,
            2,
            profiles,
            2,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    return true;
}

struct validation_fixture {
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_expected_service expected[2];
    struct micros_bootstrap_image_info images[2];
    struct micros_privilege_profile profiles[2];
};

static void initialize_validation_fixture(
    struct validation_fixture *fixture
)
{
    initialize_manifest(&fixture->manifest);
    memset(fixture->expected, 0, sizeof(fixture->expected));
    fixture->expected[0] =
        (struct micros_bootstrap_expected_service){
            .service_id = 1,
            .image_id = 101,
            .process_slot = 0,
            .profile_id = 1,
            .role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER,
        };
    set_name(
        fixture->expected[0].service_name,
        "bootstrap-launcher"
    );
    set_name(
        fixture->expected[0].profile_name,
        "BOOTSTRAP_LAUNCHER"
    );
    fixture->expected[1] =
        (struct micros_bootstrap_expected_service){
            .service_id = 2,
            .image_id = 102,
            .process_slot = 1,
            .profile_id = 2,
            .prerequisites = UINT64_C(1),
            .call_targets = UINT32_C(1) << 1,
            .role_flags = MICROS_BOOTSTRAP_ROLE_VM,
        };
    set_name(fixture->expected[1].service_name, "vm");
    set_name(fixture->expected[1].profile_name, "VM");
    fixture->images[0] = (struct micros_bootstrap_image_info){
        .version = MICROS_BOOTSTRAP_IMAGE_VERSION,
        .image_id = 101,
        .entry = MICROS_USER_VIRTUAL_BASE,
        .config_address = MICROS_USER_VIRTUAL_BASE + 0x1000,
        .config_size = sizeof(struct micros_bootstrap_service_config),
        .page_count = 2,
        .image_end = MICROS_USER_VIRTUAL_BASE + 0x2000,
        .config_initially_zero = true,
    };
    fixture->images[1] = fixture->images[0];
    fixture->images[1].image_id = 102;
    fixture->images[1].vm_boot_info_address =
        MICROS_USER_VIRTUAL_BASE + 0x2000;
    fixture->images[1].vm_boot_info_size = MICROS_VM_BOOT_INFO_SIZE;
    fixture->images[1].vm_boot_info_initially_zero = true;
    fixture->profiles[0] = profile(1, "BOOTSTRAP_LAUNCHER");
    fixture->profiles[1] = profile(2, "VM");
}

static bool expect_validation_error(
    const struct validation_fixture *fixture,
    uint64_t available_pages,
    enum micros_bootstrap_error expected_error
)
{
    struct micros_bootstrap_manifest_plan plan;
    struct micros_bootstrap_manifest_plan sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    plan = sentinel;
    return (
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            2,
            fixture->images,
            2,
            fixture->profiles,
            2,
            available_pages,
            &plan
        ) == expected_error
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
}

static bool test_manifest_rejections(void)
{
    struct validation_fixture fixture;
    struct micros_bootstrap_manifest_plan plan;
    struct micros_bootstrap_diagnostic diagnostic;

    initialize_validation_fixture(&fixture);
    fixture.manifest.header.version = 2;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_SHAPE
        )
    );
    memset(&diagnostic, 0xa5, sizeof(diagnostic));
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate_detailed(
            &fixture.manifest,
            fixture.expected,
            2,
            fixture.images,
            2,
            fixture.profiles,
            2,
            32,
            &plan,
            &diagnostic
        ) == MICROS_BOOTSTRAP_ERROR_SHAPE
        && diagnostic.reason
            == MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_HEADER
        && diagnostic.service_id == 0
        && diagnostic.endpoint == MICROS_ENDPOINT_NONE
        && diagnostic.detail == 0
    );
    initialize_validation_fixture(&fixture);
    fixture.manifest.header.reserved2[3] = 1;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_SHAPE
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.manifest.entries[2].reserved2[0] = 1;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_SHAPE
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.manifest.entries[1].service_id = 2;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IDENTITY
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.manifest.entries[1].image_id = 102;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IDENTITY
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.manifest.entries[1].process_slot = 1;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IDENTITY
        )
    );
    initialize_validation_fixture(&fixture);
    set_name(fixture.manifest.entries[1].service_name, "vm");
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IDENTITY
        )
    );
    initialize_validation_fixture(&fixture);
    set_name(fixture.manifest.entries[1].profile_name, "VM");
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IDENTITY
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.manifest.entries[0].role_flags |=
        MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_ROLE
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.manifest.entries[0].device_base =
        MICROS_BOOTSTRAP_UART_BASE;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_ROLE
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.manifest.entries[0].ready_timeout_counter_ticks = 0;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_TOPOLOGY
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.manifest.entries[0].prerequisites = UINT64_C(1) << 1;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_TOPOLOGY
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.manifest.entries[0].prerequisites = UINT64_C(1) << 2;
    fixture.expected[1].prerequisites = UINT64_C(1) << 2;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_TOPOLOGY
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.manifest.header.total_user_page_limit = 6;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_RANGE
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.profiles[1].call_targets = 0;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_PROFILE
        )
    );
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate_detailed(
            &fixture.manifest,
            fixture.expected,
            2,
            fixture.images,
            2,
            fixture.profiles,
            2,
            32,
            &plan,
            &diagnostic
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && diagnostic.reason
            == MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_PROFILE
        && diagnostic.service_id == 2
        && diagnostic.detail == 0
    );
    initialize_validation_fixture(&fixture);
    set_name(
        fixture.profiles[1].name,
        "BOOTSTRAP_LAUNCHER"
    );
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_PROFILE
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.images[1].vm_boot_info_size = 0;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IMAGE
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.images[1].vm_boot_info_initially_zero = false;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IMAGE
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.images[0].vm_boot_info_address =
        MICROS_USER_VIRTUAL_BASE + 0x2000;
    fixture.images[0].vm_boot_info_size = MICROS_VM_BOOT_INFO_SIZE;
    fixture.images[0].vm_boot_info_initially_zero = true;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IMAGE
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.images[1].config_address += 1;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IMAGE
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.images[1].page_count = 1;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_RANGE
        )
    );
    initialize_validation_fixture(&fixture);
    fixture.expected[1].image_id = 999;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IDENTITY
        )
    );
    initialize_validation_fixture(&fixture);
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            6,
            MICROS_BOOTSTRAP_ERROR_RANGE
        )
    );
    return true;
}

static bool test_vm_mapping_capacity(void)
{
    struct validation_fixture fixture;
    struct micros_bootstrap_manifest_plan plan;

    initialize_validation_fixture(&fixture);
    fixture.images[1].page_count = 4091;
    fixture.images[1].image_end =
        MICROS_USER_VIRTUAL_BASE
        + UINT64_C(4091) * MICROS_FRAME_SIZE;
    fixture.manifest.entries[0].user_page_limit = 4092;
    fixture.manifest.header.total_user_page_limit = 4096;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture.manifest,
            fixture.expected,
            2,
            fixture.images,
            2,
            fixture.profiles,
            2,
            4096,
            &plan
        ) == MICROS_BOOTSTRAP_OK
    );

    fixture.images[1].page_count = 4092;
    fixture.images[1].image_end =
        MICROS_USER_VIRTUAL_BASE
        + UINT64_C(4092) * MICROS_FRAME_SIZE;
    fixture.manifest.entries[0].user_page_limit = 4093;
    fixture.manifest.header.total_user_page_limit = 4097;
    EXPECT_TRUE(
        expect_validation_error(
            &fixture,
            4097,
            MICROS_BOOTSTRAP_ERROR_RANGE
        )
    );
    return true;
}

static bool test_three_service_topology(void)
{
    struct validation_fixture base;
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_manifest valid_manifest;
    struct micros_bootstrap_manifest_entry temporary;
    struct micros_bootstrap_expected_service expected[4] = {0};
    struct micros_bootstrap_expected_service valid_expected[4] = {0};
    struct micros_bootstrap_image_info images[4] = {0};
    struct micros_privilege_profile profiles[8] = {0};
    struct micros_privilege_profile valid_profiles[8] = {0};
    struct micros_bootstrap_manifest_plan plan;
    struct micros_bootstrap_manifest_plan sentinel;
    struct micros_bootstrap_diagnostic diagnostic;
    static const char *const profile_names[7] = {
        "BOOTSTRAP_LAUNCHER",
        "VM",
        "PM",
        "TTY",
        "RAMFS",
        "VFS",
        "APPLICATION",
    };
    size_t index;

    initialize_validation_fixture(&base);
    manifest = base.manifest;
    manifest.header.entry_count = 3;
    manifest.header.total_user_page_limit = 10;
    manifest.entries[2] =
        (struct micros_bootstrap_manifest_entry){
            .service_id = 3,
            .image_id = 103,
            .process_slot = 2,
            .stack_page_count = 1,
            .profile_id = 3,
            .prerequisites = UINT64_C(1) << 1,
            .ready_timeout_counter_ticks = 100,
            .user_page_limit = 3,
            .role_flags = MICROS_BOOTSTRAP_ROLE_PM,
        };
    set_name(manifest.entries[2].service_name, "pm");
    set_name(manifest.entries[2].profile_name, "PM");
    expected[0] = base.expected[0];
    expected[1] = base.expected[1];
    expected[2] = (struct micros_bootstrap_expected_service){
        .service_id = 3,
        .image_id = 103,
        .process_slot = 2,
        .profile_id = 3,
        .prerequisites = UINT64_C(1) << 1,
        .call_targets = UINT32_C(1) << 1,
        .role_flags = MICROS_BOOTSTRAP_ROLE_PM,
    };
    set_name(expected[2].service_name, "pm");
    set_name(expected[2].profile_name, "PM");
    images[0] = base.images[0];
    images[1] = base.images[1];
    images[2] = base.images[1];
    images[2].image_id = 103;
    images[2].vm_boot_info_address = 0;
    images[2].vm_boot_info_size = 0;
    images[2].vm_boot_info_initially_zero = false;
    profiles[0] = base.profiles[0];
    profiles[1] = base.profiles[1];
    for (index = 2; index < 7; ++index) {
        profiles[index] = profile(
            (uint8_t)(index + 1),
            profile_names[index]
        );
    }

    temporary = manifest.entries[2];
    manifest.entries[2] = manifest.entries[0];
    manifest.entries[0] = temporary;
    valid_manifest = manifest;
    memcpy(valid_expected, expected, sizeof(expected));
    memcpy(valid_profiles, profiles, sizeof(profiles));
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            3,
            images,
            3,
            profiles,
            7,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_OK
        && plan.pm_service_id == 3
        && plan.ordered_service_ids[0] == 1
        && plan.ordered_service_ids[1] == 2
        && plan.ordered_service_ids[2] == 3
    );

    memset(&sentinel, 0xa5, sizeof(sentinel));
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &valid_manifest,
            valid_expected,
            3,
            images,
            3,
            valid_profiles,
            6,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    profiles[6].call_targets = UINT32_C(1) << 3;
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &valid_manifest,
            valid_expected,
            3,
            images,
            3,
            profiles,
            7,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    memcpy(profiles, valid_profiles, sizeof(profiles));
    profiles[6].operations |= MICROS_PRIVILEGE_OPERATION_RECEIVE;
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &valid_manifest,
            valid_expected,
            3,
            images,
            3,
            profiles,
            7,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    memcpy(profiles, valid_profiles, sizeof(profiles));
    set_name(profiles[3].name, "TTY_BAD");
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &valid_manifest,
            valid_expected,
            3,
            images,
            3,
            profiles,
            7,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    memcpy(profiles, valid_profiles, sizeof(profiles));
    profiles[2].operations &= ~MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE;
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &valid_manifest,
            valid_expected,
            3,
            images,
            3,
            profiles,
            7,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    memcpy(profiles, valid_profiles, sizeof(profiles));
    profiles[2].call_targets |=
        UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VM;
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &valid_manifest,
            valid_expected,
            3,
            images,
            3,
            profiles,
            7,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    memcpy(profiles, valid_profiles, sizeof(profiles));
    profiles[2].kernel_operations = 0;
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &valid_manifest,
            valid_expected,
            3,
            images,
            3,
            profiles,
            7,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    manifest = valid_manifest;
    memcpy(expected, valid_expected, sizeof(expected));
    manifest.entries[0].prerequisites = UINT64_C(1);
    expected[2].prerequisites = UINT64_C(1);
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            3,
            images,
            3,
            valid_profiles,
            7,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_ROLE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    manifest = valid_manifest;
    memcpy(expected, valid_expected, sizeof(expected));
    manifest.entries[0].role_flags = 0;
    expected[2].role_flags = 0;
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            3,
            images,
            3,
            valid_profiles,
            7,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_ROLE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    manifest = valid_manifest;
    manifest.entries[0].role_flags |= MICROS_BOOTSTRAP_ROLE_VM;
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            valid_expected,
            3,
            images,
            3,
            valid_profiles,
            7,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_ROLE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );
    manifest = valid_manifest;
    memcpy(expected, valid_expected, sizeof(expected));
    manifest.entries[0].process_slot = 3;
    expected[2].process_slot = 3;
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            3,
            images,
            3,
            valid_profiles,
            7,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_ROLE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );

    manifest = valid_manifest;
    memcpy(expected, valid_expected, sizeof(expected));
    manifest.entries[2].prerequisites = UINT64_C(1) << 2;
    expected[1].prerequisites = UINT64_C(1) << 2;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate_detailed(
            &manifest,
            expected,
            3,
            images,
            3,
            valid_profiles,
            7,
            32,
            &plan,
            &diagnostic
        ) == MICROS_BOOTSTRAP_ERROR_TOPOLOGY
        && diagnostic.reason
            == MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_CYCLE
        && diagnostic.service_id == 2
        && diagnostic.endpoint == MICROS_ENDPOINT_NONE
        && diagnostic.detail == UINT64_C(6)
    );
    manifest.entries[2].prerequisites = UINT64_C(1) << 3;
    expected[1].prerequisites = UINT64_C(1) << 3;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate_detailed(
            &manifest,
            expected,
            3,
            images,
            3,
            valid_profiles,
            7,
            32,
            &plan,
            &diagnostic
        ) == MICROS_BOOTSTRAP_ERROR_TOPOLOGY
        && diagnostic.reason
            == MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_CYCLE
        && diagnostic.service_id == 2
        && diagnostic.detail == UINT64_C(6)
    );

    manifest = valid_manifest;
    memcpy(expected, valid_expected, sizeof(expected));
    memcpy(profiles, valid_profiles, sizeof(profiles));
    manifest.header.entry_count = 4;
    manifest.header.total_user_page_limit = 13;
    manifest.entries[3] =
        (struct micros_bootstrap_manifest_entry){
            .service_id = 4,
            .image_id = 104,
            .process_slot = 3,
            .stack_page_count = 1,
            .profile_id = 8,
            .prerequisites = UINT64_C(1) << 2,
            .ready_timeout_counter_ticks = 100,
            .user_page_limit = 3,
        };
    set_name(manifest.entries[3].service_name, "probe");
    set_name(manifest.entries[3].profile_name, "PROBE");
    expected[3] = (struct micros_bootstrap_expected_service){
        .service_id = 4,
        .image_id = 104,
        .process_slot = 3,
        .profile_id = 8,
        .prerequisites = UINT64_C(1) << 2,
        .call_targets = UINT32_C(1) << 1,
    };
    set_name(expected[3].service_name, "probe");
    set_name(expected[3].profile_name, "PROBE");
    images[3] = images[2];
    images[3].image_id = 104;
    profiles[7] = profile(8, "PROBE");
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            4,
            images,
            4,
            profiles,
            8,
            40,
            &plan
        ) == MICROS_BOOTSTRAP_OK
        && plan.pm_service_id == 3
        && plan.ordered_service_ids[3] == 4
    );

    {
        struct micros_bootstrap_manifest probe_manifest = manifest;
        struct micros_bootstrap_expected_service probe_expected[4];

        memcpy(probe_expected, expected, sizeof(probe_expected));
        probe_manifest.entries[3].role_flags =
            MICROS_BOOTSTRAP_ROLE_PM;
        probe_expected[3].role_flags = MICROS_BOOTSTRAP_ROLE_PM;
        plan = sentinel;
        EXPECT_TRUE(
            micros_bootstrap_manifest_validate(
                &probe_manifest,
                probe_expected,
                4,
                images,
                4,
                profiles,
                8,
                40,
                &plan
            ) == MICROS_BOOTSTRAP_ERROR_ROLE
            && memcmp(&plan, &sentinel, sizeof(plan)) == 0
        );
    }
    {
        struct micros_bootstrap_manifest probe_manifest = manifest;
        struct micros_bootstrap_expected_service probe_expected[4];

        memcpy(probe_expected, expected, sizeof(probe_expected));
        probe_manifest.entries[3].profile_id =
            MICROS_PRIVILEGE_PROFILE_APPLICATION;
        set_name(
            probe_manifest.entries[3].profile_name,
            "APPLICATION"
        );
        probe_expected[3].profile_id =
            MICROS_PRIVILEGE_PROFILE_APPLICATION;
        probe_expected[3].call_targets =
            (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_PM
            ) | (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_VFS
            );
        set_name(probe_expected[3].profile_name, "APPLICATION");
        plan = sentinel;
        EXPECT_TRUE(
            micros_bootstrap_manifest_validate(
                &probe_manifest,
                probe_expected,
                4,
                images,
                4,
                profiles,
                8,
                40,
                &plan
            ) == MICROS_BOOTSTRAP_ERROR_PROFILE
            && memcmp(&plan, &sentinel, sizeof(plan)) == 0
        );
    }
    {
        struct micros_bootstrap_expected_service probe_expected[4];

        memcpy(probe_expected, expected, sizeof(probe_expected));
        profiles[7].call_targets |= UINT32_C(1) << 9;
        probe_expected[3].call_targets = profiles[7].call_targets;
        plan = sentinel;
        EXPECT_TRUE(
            micros_bootstrap_manifest_validate(
                &manifest,
                probe_expected,
                4,
                images,
                4,
                profiles,
                8,
                40,
                &plan
            ) == MICROS_BOOTSTRAP_ERROR_PROFILE
            && memcmp(&plan, &sentinel, sizeof(plan)) == 0
        );
    }
    profiles[7] = profile(8, "PROBE");
    profiles[7].kernel_operations = MICROS_KERNEL_OPERATION_PM_CONTROL;
    plan = sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            4,
            images,
            4,
            profiles,
            8,
            40,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
    );

    manifest = valid_manifest;
    memcpy(expected, valid_expected, sizeof(expected));
    memset(profiles, 0, sizeof(profiles));
    profiles[0] = valid_profiles[0];
    profiles[1] = valid_profiles[1];
    profiles[2] = (struct micros_privilege_profile){
        .id = 3,
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL,
        .call_targets =
            (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
            ) | (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_VM
            ),
    };
    set_name(profiles[2].name, "VM_HANDOFF_PROBE");
    manifest.entries[0].role_flags = 0;
    set_name(
        manifest.entries[0].service_name,
        "vm-handoff-probe"
    );
    set_name(
        manifest.entries[0].profile_name,
        "VM_HANDOFF_PROBE"
    );
    expected[2].role_flags = 0;
    expected[2].call_targets = profiles[2].call_targets;
    set_name(expected[2].service_name, "vm-handoff-probe");
    set_name(expected[2].profile_name, "VM_HANDOFF_PROBE");
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &manifest,
            expected,
            3,
            images,
            3,
            profiles,
            3,
            32,
            &plan
        ) == MICROS_BOOTSTRAP_OK
        && plan.pm_service_id == 0
    );
    manifest.entries[2].prerequisites = UINT64_C(1) << 2;
    expected[1].prerequisites = UINT64_C(1) << 2;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate_detailed(
            &manifest,
            expected,
            3,
            images,
            3,
            profiles,
            3,
            32,
            &plan,
            &diagnostic
        ) == MICROS_BOOTSTRAP_ERROR_TOPOLOGY
        && diagnostic.reason
            == MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_CYCLE
        && diagnostic.service_id == 2
        && diagnostic.detail == UINT64_C(6)
    );
    return true;
}

static bool test_runtime_transitions(void)
{
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_manifest_plan plan = {
        .entry_count = 2,
        .total_user_page_limit = 7,
        .controller_service_id = 1,
        .vm_service_id = 2,
        .ordered_service_ids = {1, 2},
        .ordered_manifest_indices = {1, 0},
    };
    struct micros_bootstrap_runtime runtime;
    struct micros_bootstrap_runtime snapshot;

    initialize_manifest(&manifest);
    memset(&runtime, 0, sizeof(runtime));
    EXPECT_TRUE(
        micros_bootstrap_runtime_initialize(
            &manifest,
            &plan,
            &runtime
        ) == MICROS_BOOTSTRAP_OK
        && runtime.phase == MICROS_BOOTSTRAP_PHASE_RUNNING
        && runtime.entries[1].state
            == MICROS_BOOTSTRAP_SERVICE_READY
        && runtime.entries[1].endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        && runtime.entries[1].scheduler_assigned
        && runtime.entries[0].state
            == MICROS_BOOTSTRAP_SERVICE_PREPARED
        && runtime.entries[0].endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_RESERVED
        && !runtime.entries[0].scheduler_assigned
    );
    snapshot = runtime;
    EXPECT_TRUE(
        micros_bootstrap_runtime_release(&runtime, 99, 10)
            == MICROS_BOOTSTRAP_ERROR_ARGUMENT
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
        && micros_bootstrap_runtime_accept_ready(&runtime, 99, 10)
            == MICROS_BOOTSTRAP_ERROR_ARGUMENT
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
        && micros_bootstrap_runtime_release(&runtime, 1, 10)
            == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
        && micros_bootstrap_classify_ready(&runtime, 1, 1, true)
            == MICROS_BOOTSTRAP_READY_DUPLICATE
        && micros_bootstrap_classify_ready(&runtime, 2, 2, true)
            == MICROS_BOOTSTRAP_READY_EARLY
        && micros_bootstrap_classify_ready(&runtime, 99, 99, true)
            == MICROS_BOOTSTRAP_READY_FOREIGN
        && micros_bootstrap_classify_ready(&runtime, 2, 2, false)
            == MICROS_BOOTSTRAP_READY_MALFORMED
    );
    EXPECT_TRUE(
        micros_bootstrap_runtime_release(&runtime, 2, 10)
            == MICROS_BOOTSTRAP_OK
        && runtime.starting_service_id == 2
        && runtime.entries[0].state
            == MICROS_BOOTSTRAP_SERVICE_STARTING
        && runtime.entries[0].ready_deadline == 110
        && runtime.entries[0].profile_installed
        && runtime.entries[0].scheduler_assigned
        && runtime.entries[0].endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        && micros_bootstrap_classify_ready(&runtime, 2, 2, true)
            == MICROS_BOOTSTRAP_READY_ACCEPT
        && micros_bootstrap_classify_ready(&runtime, 2, 1, true)
            == MICROS_BOOTSTRAP_READY_MALFORMED
        && micros_bootstrap_classify_ready(&runtime, 1, 1, true)
            == MICROS_BOOTSTRAP_READY_DUPLICATE
    );
    snapshot = runtime;
    EXPECT_TRUE(
        micros_bootstrap_runtime_accept_ready(&runtime, 2, 111)
            == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
        && micros_bootstrap_runtime_accept_ready(&runtime, 2, 110)
            == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
        && micros_bootstrap_runtime_accept_ready(&runtime, 2, 109)
            == MICROS_BOOTSTRAP_OK
        && runtime.entries[0].state
            == MICROS_BOOTSTRAP_SERVICE_READY
        && runtime.starting_service_id == 0
        && runtime.next_order_index == 2
        && micros_bootstrap_runtime_complete(&runtime)
            == MICROS_BOOTSTRAP_OK
        && runtime.phase == MICROS_BOOTSTRAP_PHASE_SEALED
        && runtime.controller_service_id == 0
        && runtime.entries[1].endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY
        && !runtime.entries[1].scheduler_assigned
        && micros_bootstrap_runtime_release(&runtime, 2, 0)
            == MICROS_BOOTSTRAP_ERROR_STATE
    );
    snapshot = runtime;
    EXPECT_TRUE(
        micros_bootstrap_runtime_initialize(
            &manifest,
            &plan,
            &runtime
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
    );
    return true;
}

struct reference_runtime_entry {
    uint32_t service_id;
    uint64_t prerequisites;
    uint64_t ready_timeout_counter_ticks;
    uint64_t ready_deadline;
    enum micros_bootstrap_service_state state;
    enum micros_bootstrap_endpoint_state endpoint_state;
    bool profile_installed;
    bool scheduler_assigned;
};

struct reference_runtime {
    enum micros_bootstrap_phase phase;
    uint16_t entry_count;
    uint16_t next_order_index;
    uint32_t controller_service_id;
    uint32_t starting_service_id;
    struct reference_runtime_entry entries[2];
    uint32_t ordered_service_ids[2];
};

enum model_operation {
    MODEL_RESET = 0,
    MODEL_INITIALIZE,
    MODEL_RELEASE,
    MODEL_ACCEPT_READY,
    MODEL_COMPLETE,
};

struct model_trace_entry {
    size_t step;
    enum model_operation operation;
    uint32_t service_id;
    uint64_t now;
    enum micros_bootstrap_error expected_error;
    enum micros_bootstrap_error actual_error;
    enum micros_bootstrap_ready_class expected_class;
    enum micros_bootstrap_ready_class actual_class;
};

static bool reference_is_zero(const struct reference_runtime *runtime)
{
    const struct reference_runtime zero = {0};

    return memcmp(runtime, &zero, sizeof(*runtime)) == 0;
}

static struct reference_runtime_entry *reference_entry(
    struct reference_runtime *runtime,
    uint32_t service_id
)
{
    size_t index;

    for (index = 0; index < runtime->entry_count; ++index) {
        if (runtime->entries[index].service_id == service_id) {
            return &runtime->entries[index];
        }
    }
    return NULL;
}

static const struct reference_runtime_entry *reference_entry_const(
    const struct reference_runtime *runtime,
    uint32_t service_id
)
{
    size_t index;

    for (index = 0; index < runtime->entry_count; ++index) {
        if (runtime->entries[index].service_id == service_id) {
            return &runtime->entries[index];
        }
    }
    return NULL;
}

static enum micros_bootstrap_error reference_initialize(
    struct reference_runtime *runtime
)
{
    if (!reference_is_zero(runtime)) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    runtime->phase = MICROS_BOOTSTRAP_PHASE_RUNNING;
    runtime->entry_count = 2;
    runtime->next_order_index = 1;
    runtime->controller_service_id = 1;
    runtime->entries[0] = (struct reference_runtime_entry){
        .service_id = 2,
        .prerequisites = UINT64_C(1),
        .ready_timeout_counter_ticks = 100,
        .state = MICROS_BOOTSTRAP_SERVICE_PREPARED,
        .endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_RESERVED,
    };
    runtime->entries[1] = (struct reference_runtime_entry){
        .service_id = 1,
        .state = MICROS_BOOTSTRAP_SERVICE_READY,
        .endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE,
        .profile_installed = true,
        .scheduler_assigned = true,
    };
    runtime->ordered_service_ids[0] = 1;
    runtime->ordered_service_ids[1] = 2;
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error reference_release(
    struct reference_runtime *runtime,
    uint32_t service_id,
    uint64_t now
)
{
    struct reference_runtime_entry *entry;

    if (service_id == 0) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (runtime->phase != MICROS_BOOTSTRAP_PHASE_RUNNING) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    entry = reference_entry(runtime, service_id);
    if (entry == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (
        runtime->starting_service_id != 0
        || runtime->next_order_index >= runtime->entry_count
        || runtime->ordered_service_ids[runtime->next_order_index]
            != service_id
        || entry->state != MICROS_BOOTSTRAP_SERVICE_PREPARED
        || entry->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_RESERVED
        || entry->profile_installed
        || entry->scheduler_assigned
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (
        entry->ready_timeout_counter_ticks == 0
        || UINT64_MAX - now < entry->ready_timeout_counter_ticks
    ) {
        return MICROS_BOOTSTRAP_ERROR_RANGE;
    }
    entry->profile_installed = true;
    entry->endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
    entry->scheduler_assigned = true;
    entry->state = MICROS_BOOTSTRAP_SERVICE_STARTING;
    entry->ready_deadline =
        now + entry->ready_timeout_counter_ticks;
    runtime->starting_service_id = service_id;
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error reference_accept_ready(
    struct reference_runtime *runtime,
    uint32_t service_id,
    uint64_t now
)
{
    struct reference_runtime_entry *entry;

    if (service_id == 0) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (runtime->phase != MICROS_BOOTSTRAP_PHASE_RUNNING) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    entry = reference_entry(runtime, service_id);
    if (entry == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (
        runtime->starting_service_id != service_id
        || entry->state != MICROS_BOOTSTRAP_SERVICE_STARTING
        || entry->ready_deadline == 0
        || now >= entry->ready_deadline
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    entry->state = MICROS_BOOTSTRAP_SERVICE_READY;
    entry->ready_deadline = 0;
    runtime->starting_service_id = 0;
    ++runtime->next_order_index;
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error reference_complete(
    struct reference_runtime *runtime
)
{
    struct reference_runtime_entry *controller;
    size_t index;

    if (
        runtime->phase != MICROS_BOOTSTRAP_PHASE_RUNNING
        || runtime->starting_service_id != 0
        || runtime->next_order_index != runtime->entry_count
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    for (index = 0; index < runtime->entry_count; ++index) {
        if (
            runtime->entries[index].state
                != MICROS_BOOTSTRAP_SERVICE_READY
            || runtime->entries[index].ready_deadline != 0
        ) {
            return MICROS_BOOTSTRAP_ERROR_STATE;
        }
    }
    controller = reference_entry(runtime, 1);
    if (
        controller == NULL
        || controller->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        || !controller->profile_installed
        || !controller->scheduler_assigned
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    controller->endpoint_state =
        MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY;
    controller->scheduler_assigned = false;
    runtime->phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    runtime->controller_service_id = 0;
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_ready_class reference_classify_ready(
    const struct reference_runtime *runtime,
    uint32_t source_service_id,
    uint32_t payload_service_id,
    bool message_shape_valid
)
{
    const struct reference_runtime_entry *entry;

    if (!message_shape_valid) {
        return MICROS_BOOTSTRAP_READY_MALFORMED;
    }
    if (source_service_id == 0) {
        return MICROS_BOOTSTRAP_READY_FOREIGN;
    }
    entry = reference_entry_const(runtime, source_service_id);
    if (entry == NULL) {
        return MICROS_BOOTSTRAP_READY_FOREIGN;
    }
    if (source_service_id == runtime->starting_service_id) {
        return payload_service_id == source_service_id
            ? MICROS_BOOTSTRAP_READY_ACCEPT
            : MICROS_BOOTSTRAP_READY_MALFORMED;
    }
    if (entry->state == MICROS_BOOTSTRAP_SERVICE_READY) {
        return MICROS_BOOTSTRAP_READY_DUPLICATE;
    }
    if (entry->state == MICROS_BOOTSTRAP_SERVICE_PREPARED) {
        return MICROS_BOOTSTRAP_READY_EARLY;
    }
    return MICROS_BOOTSTRAP_READY_FOREIGN;
}

static void project_reference(
    const struct reference_runtime *reference,
    struct micros_bootstrap_runtime *runtime
)
{
    size_t index;

    memset(runtime, 0, sizeof(*runtime));
    runtime->phase = reference->phase;
    runtime->entry_count = reference->entry_count;
    runtime->next_order_index = reference->next_order_index;
    runtime->controller_service_id =
        reference->controller_service_id;
    runtime->starting_service_id = reference->starting_service_id;
    for (index = 0; index < reference->entry_count; ++index) {
        runtime->entries[index].service_id =
            reference->entries[index].service_id;
        runtime->entries[index].prerequisites =
            reference->entries[index].prerequisites;
        runtime->entries[index].ready_timeout_counter_ticks =
            reference->entries[index].ready_timeout_counter_ticks;
        runtime->entries[index].ready_deadline =
            reference->entries[index].ready_deadline;
        runtime->entries[index].state =
            reference->entries[index].state;
        runtime->entries[index].endpoint_state =
            reference->entries[index].endpoint_state;
        runtime->entries[index].profile_installed =
            reference->entries[index].profile_installed;
        runtime->entries[index].scheduler_assigned =
            reference->entries[index].scheduler_assigned;
        runtime->ordered_service_ids[index] =
            reference->ordered_service_ids[index];
    }
}

static uint64_t model_random(uint64_t *state)
{
    uint64_t value = *state;

    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

static void print_model_failure(
    uint64_t seed,
    size_t step,
    const struct model_trace_entry trace[16],
    size_t trace_count,
    const struct reference_runtime *reference,
    const struct micros_bootstrap_runtime *runtime
)
{
    size_t index;
    size_t start = trace_count > 16 ? trace_count - 16 : 0;

    fprintf(
        stderr,
        "bootstrap model seed=0x%016llx step=%zu "
        "manifest={1,2} order={1,2} "
        "reference={phase=%d,next=%u,starting=%u} "
        "production={phase=%d,next=%u,starting=%u}\n",
        (unsigned long long)seed,
        step,
        (int)reference->phase,
        (unsigned int)reference->next_order_index,
        reference->starting_service_id,
        (int)runtime->phase,
        (unsigned int)runtime->next_order_index,
        runtime->starting_service_id
    );
    for (index = start; index < trace_count; ++index) {
        const struct model_trace_entry *entry =
            &trace[index % 16];

        fprintf(
            stderr,
            "trace step=%zu op=%d service=%u now=%llu "
            "error=%d/%d class=%d/%d\n",
            entry->step,
            (int)entry->operation,
            entry->service_id,
            (unsigned long long)entry->now,
            (int)entry->expected_error,
            (int)entry->actual_error,
            (int)entry->expected_class,
            (int)entry->actual_class
        );
    }
}

static bool test_replayable_runtime_model(void)
{
    static const uint64_t seed = UINT64_C(0x42535452504c4159);
    struct micros_bootstrap_manifest manifest;
    const struct micros_bootstrap_manifest_plan plan = {
        .entry_count = 2,
        .total_user_page_limit = 7,
        .controller_service_id = 1,
        .vm_service_id = 2,
        .ordered_service_ids = {1, 2},
        .ordered_manifest_indices = {1, 0},
    };
    struct reference_runtime reference = {0};
    struct micros_bootstrap_runtime runtime = {0};
    struct micros_bootstrap_runtime expected_runtime;
    struct model_trace_entry trace[16] = {0};
    uint64_t random_state = seed;
    size_t step;

    initialize_manifest(&manifest);
    for (step = 0; step < 4096; ++step) {
        uint64_t random_value = model_random(&random_state);
        size_t cycle_step = step % 16;
        enum model_operation operation = MODEL_RESET;
        uint32_t service_id = 0;
        uint64_t now = 0;
        enum micros_bootstrap_error expected_error =
            MICROS_BOOTSTRAP_OK;
        enum micros_bootstrap_error actual_error =
            MICROS_BOOTSTRAP_OK;
        bool compare_error = true;
        uint32_t ready_source;
        uint32_t ready_payload;
        bool ready_shape;
        enum micros_bootstrap_ready_class expected_class;
        enum micros_bootstrap_ready_class actual_class;

        switch (cycle_step) {
        case 0:
            memset(&reference, 0, sizeof(reference));
            memset(&runtime, 0, sizeof(runtime));
            operation = MODEL_RESET;
            compare_error = false;
            break;
        case 1:
        case 2:
        case 13:
            operation = MODEL_INITIALIZE;
            expected_error = reference_initialize(&reference);
            actual_error = micros_bootstrap_runtime_initialize(
                &manifest,
                &plan,
                &runtime
            );
            break;
        case 3:
            operation = MODEL_RELEASE;
            service_id = 99;
            expected_error = reference_release(
                &reference,
                service_id,
                10
            );
            actual_error = micros_bootstrap_runtime_release(
                &runtime,
                service_id,
                10
            );
            break;
        case 4:
            operation = MODEL_RELEASE;
            service_id = 1;
            expected_error = reference_release(
                &reference,
                service_id,
                10
            );
            actual_error = micros_bootstrap_runtime_release(
                &runtime,
                service_id,
                10
            );
            break;
        case 5:
            operation = MODEL_RELEASE;
            service_id = 2;
            now = ((step / 16) & 1) != 0
                ? UINT64_MAX - UINT64_C(50)
                : UINT64_C(10);
            expected_error = reference_release(
                &reference,
                service_id,
                now
            );
            actual_error = micros_bootstrap_runtime_release(
                &runtime,
                service_id,
                now
            );
            break;
        case 6:
            operation = MODEL_RELEASE;
            service_id = 2;
            now = 10;
            expected_error = reference_release(
                &reference,
                service_id,
                now
            );
            actual_error = micros_bootstrap_runtime_release(
                &runtime,
                service_id,
                now
            );
            break;
        case 7:
            operation = MODEL_ACCEPT_READY;
            service_id = 99;
            now = 20;
            expected_error = reference_accept_ready(
                &reference,
                service_id,
                now
            );
            actual_error = micros_bootstrap_runtime_accept_ready(
                &runtime,
                service_id,
                now
            );
            break;
        case 8:
            operation = MODEL_ACCEPT_READY;
            service_id = 1;
            now = 20;
            expected_error = reference_accept_ready(
                &reference,
                service_id,
                now
            );
            actual_error = micros_bootstrap_runtime_accept_ready(
                &runtime,
                service_id,
                now
            );
            break;
        case 9:
        case 10:
        case 11:
            operation = MODEL_ACCEPT_READY;
            service_id = 2;
            now = cycle_step == 9 ? 110 : 109;
            expected_error = reference_accept_ready(
                &reference,
                service_id,
                now
            );
            actual_error = micros_bootstrap_runtime_accept_ready(
                &runtime,
                service_id,
                now
            );
            break;
        case 12:
        case 15:
            operation = MODEL_COMPLETE;
            expected_error = reference_complete(&reference);
            actual_error =
                micros_bootstrap_runtime_complete(&runtime);
            break;
        case 14:
            operation = MODEL_RELEASE;
            service_id = 2;
            expected_error = reference_release(
                &reference,
                service_id,
                0
            );
            actual_error = micros_bootstrap_runtime_release(
                &runtime,
                service_id,
                0
            );
            break;
        default:
            return false;
        }

        switch (random_value & UINT64_C(3)) {
        case 0:
            ready_source = 0;
            break;
        case 1:
            ready_source = 1;
            break;
        case 2:
            ready_source = 2;
            break;
        default:
            ready_source = 99;
            break;
        }
        ready_payload = (random_value & UINT64_C(4)) != 0
            ? ready_source
            : (uint32_t)(random_value >> 32);
        ready_shape = (random_value & UINT64_C(8)) != 0;
        expected_class = reference_classify_ready(
            &reference,
            ready_source,
            ready_payload,
            ready_shape
        );
        actual_class = micros_bootstrap_classify_ready(
            &runtime,
            ready_source,
            ready_payload,
            ready_shape
        );
        trace[step % 16] = (struct model_trace_entry){
            .step = step,
            .operation = operation,
            .service_id = service_id,
            .now = now,
            .expected_error = expected_error,
            .actual_error = actual_error,
            .expected_class = expected_class,
            .actual_class = actual_class,
        };
        project_reference(&reference, &expected_runtime);
        if (
            (compare_error && actual_error != expected_error)
            || actual_class != expected_class
            || memcmp(
                &runtime,
                &expected_runtime,
                sizeof(runtime)
            ) != 0
        ) {
            print_model_failure(
                seed,
                step,
                trace,
                step + 1,
                &reference,
                &runtime
            );
            return false;
        }
    }
    return true;
}

int main(void)
{
    if (!test_manifest_contract()) {
        fprintf(stderr, "manifest contract failed\n");
        return 1;
    }
    if (!test_manifest_validation()) {
        fprintf(stderr, "manifest validation failed\n");
        return 1;
    }
    if (!test_manifest_rejections()) {
        fprintf(stderr, "manifest rejections failed\n");
        return 1;
    }
    if (!test_vm_mapping_capacity()) {
        fprintf(stderr, "VM mapping capacity failed\n");
        return 1;
    }
    if (!test_three_service_topology()) {
        fprintf(stderr, "three-service topology failed\n");
        return 1;
    }
    if (!test_runtime_transitions()) {
        fprintf(stderr, "runtime transitions failed\n");
        return 1;
    }
    if (!test_replayable_runtime_model()) {
        fprintf(stderr, "runtime model failed\n");
        return 1;
    }
    return 0;
}
