#include "bootstrap_manifest_test_internal.h"

#include "micros/tty.h"
#include "micros/vm_bootstrap.h"

struct manifest_validation_case {
    struct validation_fixture inputs;
    struct micros_bootstrap_manifest_plan plan;
    struct micros_bootstrap_manifest_plan sentinel;
    unsigned char misaligned_storage[
        sizeof(struct micros_bootstrap_manifest)
        + _Alignof(struct micros_bootstrap_manifest)
    ];
    unsigned char *misaligned_manifest;
};

bool test_manifest_contract(void)
{
    return (
        MICROS_BOOTSTRAP_MANIFEST_VERSION == 1
        && MICROS_BOOTSTRAP_MANIFEST_MAGIC
            == UINT32_C(0x3153424d)
        && MICROS_BOOTSTRAP_SERVICE_CAPACITY == 7
        && MICROS_BOOTSTRAP_ROLE_PM == UINT32_C(0x8)
        && MICROS_BOOTSTRAP_ROLE_DEFINED_MASK == UINT32_C(0xf)
        && MICROS_KERNEL_OPERATION_PM_CONTROL == UINT64_C(0x4)
        && MICROS_KERNEL_OPERATION_TTY_CONTROL == UINT64_C(0x8)
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
        && sizeof(struct micros_bootstrap_manifest) == 1408
        && sizeof(struct micros_bootstrap_manifest_plan) == 68
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
            struct micros_bootstrap_service_config,
            manifest_view_address
        ) == 80
        && offsetof(
            struct micros_bootstrap_service_config,
            reserved
        ) == 88
        && sizeof(
            ((struct micros_bootstrap_service_config *)0)->reserved
        ) == 40
        && offsetof(
            struct micros_bootstrap_manifest_plan,
            pm_service_id
        ) == 20
        && MICROS_BOOTSTRAP_MANIFEST_VIEW
            == UINT64_C(0x000000007fffe000)
    );
}

static bool accept_baseline_plan(struct manifest_validation_case *fixture)
{
    memset(&fixture->plan, 0, sizeof(fixture->plan));
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->inputs.manifest,
            fixture->inputs.expected,
            2,
            fixture->inputs.images,
            2,
            fixture->inputs.profiles,
            2,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_OK
        && fixture->plan.entry_count == 2
        && fixture->plan.controller_service_id == 1
        && fixture->plan.vm_service_id == 2
        && fixture->plan.ordered_service_ids[0] == 1
        && fixture->plan.ordered_service_ids[1] == 2
        && fixture->plan.ordered_manifest_indices[0] == 1
        && fixture->plan.ordered_manifest_indices[1] == 0
    );
    return true;
}

static bool reject_shape_and_alignment(struct manifest_validation_case *fixture)
{
    memset(&fixture->sentinel, 0xa5, sizeof(fixture->sentinel));
    fixture->plan = fixture->sentinel;
    fixture->inputs.manifest.header.magic = 0;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->inputs.manifest,
            fixture->inputs.expected,
            2,
            fixture->inputs.images,
            2,
            fixture->inputs.profiles,
            2,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_SHAPE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    bootstrap_manifest_test_initialize_manifest(&fixture->inputs.manifest);
    fixture->misaligned_manifest = (unsigned char *)(
        (
            (uintptr_t)fixture->misaligned_storage
            + _Alignof(struct micros_bootstrap_manifest) - 1
        )
        & ~(
            (uintptr_t)_Alignof(struct micros_bootstrap_manifest)
            - 1
        )
    ) + 1;
    memcpy(
        fixture->misaligned_manifest,
        &fixture->inputs.manifest,
        sizeof(fixture->inputs.manifest)
    );
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            (const struct micros_bootstrap_manifest *)(
                const void *
            )fixture->misaligned_manifest,
            fixture->inputs.expected,
            2,
            fixture->inputs.images,
            2,
            fixture->inputs.profiles,
            2,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_ARGUMENT
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    return true;
}

static bool reject_controller_profiles(struct manifest_validation_case *fixture)
{
    fixture->inputs.profiles[0].operations = MICROS_PRIVILEGE_OPERATION_RECEIVE;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->inputs.manifest,
            fixture->inputs.expected,
            2,
            fixture->inputs.images,
            2,
            fixture->inputs.profiles,
            2,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->inputs.profiles[0] =
        bootstrap_manifest_test_profile(1, "BOOTSTRAP_LAUNCHER");
    fixture->inputs.profiles[0].kernel_operations |=
        MICROS_KERNEL_OPERATION_VM_HANDOFF;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->inputs.manifest,
            fixture->inputs.expected,
            2,
            fixture->inputs.images,
            2,
            fixture->inputs.profiles,
            2,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    return true;
}

static bool reject_vm_profiles(struct manifest_validation_case *fixture)
{
    fixture->inputs.profiles[0] =
        bootstrap_manifest_test_profile(1, "BOOTSTRAP_LAUNCHER");
    fixture->inputs.profiles[1].call_targets = 0;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->inputs.manifest,
            fixture->inputs.expected,
            2,
            fixture->inputs.images,
            2,
            fixture->inputs.profiles,
            2,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->inputs.profiles[1] = bootstrap_manifest_test_profile(2, "VM");
    fixture->inputs.profiles[1].kernel_operations = 0;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->inputs.manifest,
            fixture->inputs.expected,
            2,
            fixture->inputs.images,
            2,
            fixture->inputs.profiles,
            2,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->inputs.profiles[1] = bootstrap_manifest_test_profile(2, "VM");
    fixture->inputs.profiles[1].kernel_operations =
        MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->inputs.manifest,
            fixture->inputs.expected,
            2,
            fixture->inputs.images,
            2,
            fixture->inputs.profiles,
            2,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->inputs.profiles[1] =
        bootstrap_manifest_test_profile(2, "BOOTSTRAP_LAUNCHER");
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->inputs.manifest,
            fixture->inputs.expected,
            2,
            fixture->inputs.images,
            2,
            fixture->inputs.profiles,
            2,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    return true;
}

bool test_manifest_validation(void)
{
    struct manifest_validation_case fixture;

    bootstrap_manifest_test_initialize_validation_fixture(&fixture.inputs);
    return (
        accept_baseline_plan(&fixture)
        && reject_shape_and_alignment(&fixture)
        && reject_controller_profiles(&fixture)
        && reject_vm_profiles(&fixture)
    );
}

static bool reject_headers(
    struct validation_fixture *fixture,
    struct micros_bootstrap_manifest_plan *plan,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->manifest.header.version = 2;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_SHAPE
        )
    );
    memset(diagnostic, 0xa5, sizeof(*diagnostic));
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate_detailed(
            &fixture->manifest,
            fixture->expected,
            2,
            fixture->images,
            2,
            fixture->profiles,
            2,
            32,
            plan,
            diagnostic
        ) == MICROS_BOOTSTRAP_ERROR_SHAPE
        && diagnostic->reason
            == MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_HEADER
        && diagnostic->service_id == 0
        && diagnostic->endpoint == MICROS_ENDPOINT_NONE
        && diagnostic->detail == 0
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->manifest.header.reserved2[3] = 1;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_SHAPE
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->manifest.entries[2].reserved2[0] = 1;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_SHAPE
        )
    );
    return true;
}

static bool reject_entry_identities(struct validation_fixture *fixture)
{
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->manifest.entries[1].service_id = 2;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IDENTITY
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->manifest.entries[1].image_id = 102;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IDENTITY
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->manifest.entries[1].process_slot = 1;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IDENTITY
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    bootstrap_manifest_test_set_name(fixture->manifest.entries[1].service_name, "vm");
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IDENTITY
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    bootstrap_manifest_test_set_name(fixture->manifest.entries[1].profile_name, "VM");
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IDENTITY
        )
    );
    return true;
}

static bool reject_roles_topology_and_accounting(struct validation_fixture *fixture)
{
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->manifest.entries[0].role_flags |=
        MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_ROLE
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->manifest.entries[0].device_base =
        MICROS_BOOTSTRAP_UART_BASE;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_ROLE
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->manifest.entries[0].ready_timeout_counter_ticks = 0;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_TOPOLOGY
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->manifest.entries[0].prerequisites = UINT64_C(1) << 1;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_TOPOLOGY
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->manifest.entries[0].prerequisites = UINT64_C(1) << 2;
    fixture->expected[1].prerequisites = UINT64_C(1) << 2;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_TOPOLOGY
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->manifest.header.total_user_page_limit = 6;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_RANGE
        )
    );
    return true;
}

static bool reject_profile_inputs(
    struct validation_fixture *fixture,
    struct micros_bootstrap_manifest_plan *plan,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->profiles[1].call_targets = 0;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_PROFILE
        )
    );
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate_detailed(
            &fixture->manifest,
            fixture->expected,
            2,
            fixture->images,
            2,
            fixture->profiles,
            2,
            32,
            plan,
            diagnostic
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && diagnostic->reason
            == MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_PROFILE
        && diagnostic->service_id == 2
        && diagnostic->detail == 0
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    bootstrap_manifest_test_set_name(
        fixture->profiles[1].name,
        "BOOTSTRAP_LAUNCHER"
    );
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_PROFILE
        )
    );
    return true;
}

static bool reject_images_and_resources(struct validation_fixture *fixture)
{
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->images[1].vm_boot_info_size = 0;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IMAGE
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->images[1].vm_boot_info_initially_zero = false;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IMAGE
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->images[0].vm_boot_info_address =
        MICROS_USER_VIRTUAL_BASE + 0x2000;
    fixture->images[0].vm_boot_info_size = MICROS_VM_BOOT_INFO_SIZE;
    fixture->images[0].vm_boot_info_initially_zero = true;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IMAGE
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->images[1].config_address += 1;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IMAGE
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->images[1].page_count = 1;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_RANGE
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    fixture->expected[1].image_id = 999;
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            32,
            MICROS_BOOTSTRAP_ERROR_IDENTITY
        )
    );
    bootstrap_manifest_test_initialize_validation_fixture(fixture);
    EXPECT_TRUE(
        bootstrap_manifest_test_expect_validation_error(
            fixture,
            6,
            MICROS_BOOTSTRAP_ERROR_RANGE
        )
    );
    return true;
}

bool test_manifest_rejections(void)
{
    struct validation_fixture fixture;
    struct micros_bootstrap_manifest_plan plan;
    struct micros_bootstrap_diagnostic diagnostic;

    return (
        reject_headers(&fixture, &plan, &diagnostic)
        && reject_entry_identities(&fixture)
        && reject_roles_topology_and_accounting(&fixture)
        && reject_profile_inputs(&fixture, &plan, &diagnostic)
        && reject_images_and_resources(&fixture)
    );
}
