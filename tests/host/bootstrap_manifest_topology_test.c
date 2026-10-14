#include "bootstrap_manifest_test_internal.h"

#include "micros/vm_bootstrap.h"

struct topology_fixture {
    struct validation_fixture base;
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_manifest valid_manifest;
    struct micros_bootstrap_manifest_entry temporary;
    struct micros_bootstrap_expected_service expected[4];
    struct micros_bootstrap_expected_service valid_expected[4];
    struct micros_bootstrap_image_info images[4];
    struct micros_privilege_profile profiles[8];
    struct micros_privilege_profile valid_profiles[8];
    struct micros_bootstrap_manifest_plan plan;
    struct micros_bootstrap_manifest_plan sentinel;
    struct micros_bootstrap_diagnostic diagnostic;
};

bool test_vm_mapping_capacity(void)
{
    struct validation_fixture fixture;
    struct micros_bootstrap_manifest_plan plan;

    bootstrap_manifest_test_initialize_validation_fixture(&fixture);
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
        bootstrap_manifest_test_expect_validation_error(
            &fixture,
            4097,
            MICROS_BOOTSTRAP_ERROR_RANGE
        )
    );
    return true;
}

static void initialize_topology_inputs(struct topology_fixture *fixture)
{
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

    bootstrap_manifest_test_initialize_validation_fixture(&fixture->base);
    fixture->manifest = fixture->base.manifest;
    fixture->manifest.header.entry_count = 3;
    fixture->manifest.header.total_user_page_limit = 10;
    fixture->manifest.entries[2] =
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
    bootstrap_manifest_test_set_name(fixture->manifest.entries[2].service_name, "pm");
    bootstrap_manifest_test_set_name(fixture->manifest.entries[2].profile_name, "PM");
    fixture->expected[0] = fixture->base.expected[0];
    fixture->expected[1] = fixture->base.expected[1];
    fixture->expected[2] = (struct micros_bootstrap_expected_service){
        .service_id = 3,
        .image_id = 103,
        .process_slot = 2,
        .profile_id = 3,
        .prerequisites = UINT64_C(1) << 1,
        .call_targets =
            (UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER)
            | (UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VM)
            | (UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS),
        .role_flags = MICROS_BOOTSTRAP_ROLE_PM,
    };
    bootstrap_manifest_test_set_name(fixture->expected[2].service_name, "pm");
    bootstrap_manifest_test_set_name(fixture->expected[2].profile_name, "PM");
    fixture->images[0] = fixture->base.images[0];
    fixture->images[1] = fixture->base.images[1];
    fixture->images[2] = fixture->base.images[1];
    fixture->images[2].image_id = 103;
    fixture->images[2].vm_boot_info_address = 0;
    fixture->images[2].vm_boot_info_size = 0;
    fixture->images[2].vm_boot_info_initially_zero = false;
    fixture->profiles[0] = fixture->base.profiles[0];
    fixture->profiles[1] = fixture->base.profiles[1];
    for (index = 2; index < 7; ++index) {
        fixture->profiles[index] = bootstrap_manifest_test_profile(
            (uint8_t)(index + 1),
            profile_names[index]
        );
    }
}

static bool accept_permuted_topology(struct topology_fixture *fixture)
{
    fixture->temporary = fixture->manifest.entries[2];
    fixture->manifest.entries[2] = fixture->manifest.entries[0];
    fixture->manifest.entries[0] = fixture->temporary;
    fixture->valid_manifest = fixture->manifest;
    memcpy(fixture->valid_expected, fixture->expected, sizeof(fixture->expected));
    memcpy(fixture->valid_profiles, fixture->profiles, sizeof(fixture->profiles));
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            3,
            fixture->images,
            3,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_OK
        && fixture->plan.pm_service_id == 3
        && fixture->plan.ordered_service_ids[0] == 1
        && fixture->plan.ordered_service_ids[1] == 2
        && fixture->plan.ordered_service_ids[2] == 3
    );
    return true;
}

static bool reject_application_profile_changes(struct topology_fixture *fixture)
{
    memset(&fixture->sentinel, 0xa5, sizeof(fixture->sentinel));
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->valid_manifest,
            fixture->valid_expected,
            3,
            fixture->images,
            3,
            fixture->valid_profiles,
            6,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->profiles[6].call_targets = UINT32_C(1) << 3;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->valid_manifest,
            fixture->valid_expected,
            3,
            fixture->images,
            3,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    memcpy(fixture->profiles, fixture->valid_profiles, sizeof(fixture->profiles));
    fixture->profiles[6].operations |= MICROS_PRIVILEGE_OPERATION_RECEIVE;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->valid_manifest,
            fixture->valid_expected,
            3,
            fixture->images,
            3,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    return true;
}

static bool reject_standard_profile_changes(struct topology_fixture *fixture)
{
    memcpy(fixture->profiles, fixture->valid_profiles, sizeof(fixture->profiles));
    bootstrap_manifest_test_set_name(fixture->profiles[3].name, "TTY_BAD");
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->valid_manifest,
            fixture->valid_expected,
            3,
            fixture->images,
            3,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    memcpy(fixture->profiles, fixture->valid_profiles, sizeof(fixture->profiles));
    fixture->profiles[2].operations &= ~MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->valid_manifest,
            fixture->valid_expected,
            3,
            fixture->images,
            3,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    memcpy(fixture->profiles, fixture->valid_profiles, sizeof(fixture->profiles));
    fixture->profiles[2].call_targets |=
        UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_RAMFS;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->valid_manifest,
            fixture->valid_expected,
            3,
            fixture->images,
            3,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    memcpy(fixture->profiles, fixture->valid_profiles, sizeof(fixture->profiles));
    fixture->profiles[2].kernel_operations = 0;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->valid_manifest,
            fixture->valid_expected,
            3,
            fixture->images,
            3,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    return true;
}

static bool reject_pm_role_changes(struct topology_fixture *fixture)
{
    fixture->manifest = fixture->valid_manifest;
    memcpy(fixture->expected, fixture->valid_expected, sizeof(fixture->expected));
    fixture->manifest.entries[0].prerequisites = UINT64_C(1);
    fixture->expected[2].prerequisites = UINT64_C(1);
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            3,
            fixture->images,
            3,
            fixture->valid_profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_ROLE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->manifest = fixture->valid_manifest;
    memcpy(fixture->expected, fixture->valid_expected, sizeof(fixture->expected));
    fixture->manifest.entries[0].role_flags = 0;
    fixture->expected[2].role_flags = 0;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            3,
            fixture->images,
            3,
            fixture->valid_profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_ROLE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->manifest = fixture->valid_manifest;
    fixture->manifest.entries[0].role_flags |= MICROS_BOOTSTRAP_ROLE_VM;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->valid_expected,
            3,
            fixture->images,
            3,
            fixture->valid_profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_ROLE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->manifest = fixture->valid_manifest;
    memcpy(fixture->expected, fixture->valid_expected, sizeof(fixture->expected));
    fixture->manifest.entries[0].process_slot = 3;
    fixture->expected[2].process_slot = 3;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            3,
            fixture->images,
            3,
            fixture->valid_profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_ROLE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    return true;
}

static bool reject_prerequisite_cycles(struct topology_fixture *fixture)
{
    fixture->manifest = fixture->valid_manifest;
    memcpy(fixture->expected, fixture->valid_expected, sizeof(fixture->expected));
    fixture->manifest.entries[2].prerequisites = UINT64_C(1) << 2;
    fixture->expected[1].prerequisites = UINT64_C(1) << 2;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate_detailed(
            &fixture->manifest,
            fixture->expected,
            3,
            fixture->images,
            3,
            fixture->valid_profiles,
            7,
            32,
            &fixture->plan,
            &fixture->diagnostic
        ) == MICROS_BOOTSTRAP_ERROR_TOPOLOGY
        && fixture->diagnostic.reason
            == MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_CYCLE
        && fixture->diagnostic.service_id == 2
        && fixture->diagnostic.endpoint == MICROS_ENDPOINT_NONE
        && fixture->diagnostic.detail == UINT64_C(6)
    );
    fixture->manifest.entries[2].prerequisites = UINT64_C(1) << 3;
    fixture->expected[1].prerequisites = UINT64_C(1) << 3;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate_detailed(
            &fixture->manifest,
            fixture->expected,
            3,
            fixture->images,
            3,
            fixture->valid_profiles,
            7,
            32,
            &fixture->plan,
            &fixture->diagnostic
        ) == MICROS_BOOTSTRAP_ERROR_TOPOLOGY
        && fixture->diagnostic.reason
            == MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_CYCLE
        && fixture->diagnostic.service_id == 2
        && fixture->diagnostic.detail == UINT64_C(6)
    );
    return true;
}

static bool accept_probe_configuration(struct topology_fixture *fixture)
{
    fixture->manifest = fixture->valid_manifest;
    memcpy(fixture->expected, fixture->valid_expected, sizeof(fixture->expected));
    memcpy(fixture->profiles, fixture->valid_profiles, sizeof(fixture->profiles));
    fixture->manifest.header.entry_count = 4;
    fixture->manifest.header.total_user_page_limit = 13;
    fixture->manifest.entries[3] =
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
    bootstrap_manifest_test_set_name(fixture->manifest.entries[3].service_name, "probe");
    bootstrap_manifest_test_set_name(fixture->manifest.entries[3].profile_name, "PROBE");
    fixture->expected[3] = (struct micros_bootstrap_expected_service){
        .service_id = 4,
        .image_id = 104,
        .process_slot = 3,
        .profile_id = 8,
        .prerequisites = UINT64_C(1) << 2,
        .call_targets = UINT32_C(1) << 1,
    };
    bootstrap_manifest_test_set_name(fixture->expected[3].service_name, "probe");
    bootstrap_manifest_test_set_name(fixture->expected[3].profile_name, "PROBE");
    fixture->images[3] = fixture->images[2];
    fixture->images[3].image_id = 104;
    fixture->profiles[7] = bootstrap_manifest_test_profile(8, "PROBE");
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            4,
            fixture->images,
            4,
            fixture->profiles,
            8,
            40,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_OK
        && fixture->plan.pm_service_id == 3
        && fixture->plan.ordered_service_ids[3] == 4
    );
    return true;
}

static bool reject_probe_role_and_application_profile(struct topology_fixture *fixture)
{
    {
        struct micros_bootstrap_manifest probe_manifest = fixture->manifest;
        struct micros_bootstrap_expected_service probe_expected[4];

        memcpy(probe_expected, fixture->expected, sizeof(probe_expected));
        probe_manifest.entries[3].role_flags =
            MICROS_BOOTSTRAP_ROLE_PM;
        probe_expected[3].role_flags = MICROS_BOOTSTRAP_ROLE_PM;
        fixture->plan = fixture->sentinel;
        EXPECT_TRUE(
            micros_bootstrap_manifest_validate(
                &probe_manifest,
                probe_expected,
                4,
                fixture->images,
                4,
                fixture->profiles,
                8,
                40,
                &fixture->plan
            ) == MICROS_BOOTSTRAP_ERROR_ROLE
            && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
        );
    }
    {
        struct micros_bootstrap_manifest probe_manifest = fixture->manifest;
        struct micros_bootstrap_expected_service probe_expected[4];

        memcpy(probe_expected, fixture->expected, sizeof(probe_expected));
        probe_manifest.entries[3].profile_id =
            MICROS_PRIVILEGE_PROFILE_APPLICATION;
        bootstrap_manifest_test_set_name(
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
        bootstrap_manifest_test_set_name(probe_expected[3].profile_name, "APPLICATION");
        fixture->plan = fixture->sentinel;
        EXPECT_TRUE(
            micros_bootstrap_manifest_validate(
                &probe_manifest,
                probe_expected,
                4,
                fixture->images,
                4,
                fixture->profiles,
                8,
                40,
                &fixture->plan
            ) == MICROS_BOOTSTRAP_ERROR_PROFILE
            && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
        );
    }
    return true;
}

static bool reject_probe_authority(struct topology_fixture *fixture)
{
    {
        struct micros_bootstrap_expected_service probe_expected[4];

        memcpy(probe_expected, fixture->expected, sizeof(probe_expected));
        fixture->profiles[7].call_targets |= UINT32_C(1) << 9;
        probe_expected[3].call_targets = fixture->profiles[7].call_targets;
        fixture->plan = fixture->sentinel;
        EXPECT_TRUE(
            micros_bootstrap_manifest_validate(
                &fixture->manifest,
                probe_expected,
                4,
                fixture->images,
                4,
                fixture->profiles,
                8,
                40,
                &fixture->plan
            ) == MICROS_BOOTSTRAP_ERROR_PROFILE
            && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
        );
    }
    fixture->profiles[7] = bootstrap_manifest_test_profile(8, "PROBE");
    fixture->profiles[7].kernel_operations = MICROS_KERNEL_OPERATION_PM_CONTROL;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            4,
            fixture->images,
            4,
            fixture->profiles,
            8,
            40,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    return true;
}

static bool check_handoff_probe_configuration(struct topology_fixture *fixture)
{
    fixture->manifest = fixture->valid_manifest;
    memcpy(fixture->expected, fixture->valid_expected, sizeof(fixture->expected));
    memset(fixture->profiles, 0, sizeof(fixture->profiles));
    fixture->profiles[0] = fixture->valid_profiles[0];
    fixture->profiles[1] = fixture->valid_profiles[1];
    fixture->profiles[2] = (struct micros_privilege_profile){
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
    bootstrap_manifest_test_set_name(fixture->profiles[2].name, "VM_HANDOFF_PROBE");
    fixture->manifest.entries[0].role_flags = 0;
    bootstrap_manifest_test_set_name(
        fixture->manifest.entries[0].service_name,
        "vm-handoff-probe"
    );
    bootstrap_manifest_test_set_name(
        fixture->manifest.entries[0].profile_name,
        "VM_HANDOFF_PROBE"
    );
    fixture->expected[2].role_flags = 0;
    fixture->expected[2].call_targets = fixture->profiles[2].call_targets;
    bootstrap_manifest_test_set_name(fixture->expected[2].service_name, "vm-handoff-probe");
    bootstrap_manifest_test_set_name(fixture->expected[2].profile_name, "VM_HANDOFF_PROBE");
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            3,
            fixture->images,
            3,
            fixture->profiles,
            3,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_OK
        && fixture->plan.pm_service_id == 0
    );
    fixture->manifest.entries[2].prerequisites = UINT64_C(1) << 2;
    fixture->expected[1].prerequisites = UINT64_C(1) << 2;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate_detailed(
            &fixture->manifest,
            fixture->expected,
            3,
            fixture->images,
            3,
            fixture->profiles,
            3,
            32,
            &fixture->plan,
            &fixture->diagnostic
        ) == MICROS_BOOTSTRAP_ERROR_TOPOLOGY
        && fixture->diagnostic.reason
            == MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_CYCLE
        && fixture->diagnostic.service_id == 2
        && fixture->diagnostic.detail == UINT64_C(6)
    );
    return true;
}

bool test_three_service_topology(void)
{
    struct topology_fixture fixture;

    memset(fixture.expected, 0, sizeof(fixture.expected));
    memset(fixture.valid_expected, 0, sizeof(fixture.valid_expected));
    memset(fixture.images, 0, sizeof(fixture.images));
    memset(fixture.profiles, 0, sizeof(fixture.profiles));
    memset(fixture.valid_profiles, 0, sizeof(fixture.valid_profiles));
    initialize_topology_inputs(&fixture);
    return (
        accept_permuted_topology(&fixture)
        && reject_application_profile_changes(&fixture)
        && reject_standard_profile_changes(&fixture)
        && reject_pm_role_changes(&fixture)
        && reject_prerequisite_cycles(&fixture)
        && accept_probe_configuration(&fixture)
        && reject_probe_role_and_application_profile(&fixture)
        && reject_probe_authority(&fixture)
        && check_handoff_probe_configuration(&fixture)
    );
}
