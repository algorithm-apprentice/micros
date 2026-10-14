#include "bootstrap_manifest_test_internal.h"

#include "micros/tty.h"
#include "micros/vm_bootstrap.h"

struct tty_contract_fixture {
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_expected_service expected[6];
    struct micros_bootstrap_image_info images[6];
    struct micros_privilege_profile profiles[7];
    struct micros_bootstrap_manifest_plan plan;
    struct micros_bootstrap_manifest_plan sentinel;
};

static void initialize_tty_entries(struct tty_contract_fixture *fixture)
{
    size_t index;

    memset(&fixture->manifest, 0, sizeof(fixture->manifest));
    fixture->manifest.header.magic = MICROS_BOOTSTRAP_MANIFEST_MAGIC;
    fixture->manifest.header.version = MICROS_BOOTSTRAP_MANIFEST_VERSION;
    fixture->manifest.header.header_size =
        MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE;
    fixture->manifest.header.entry_size =
        MICROS_BOOTSTRAP_MANIFEST_ENTRY_SIZE;
    fixture->manifest.header.entry_capacity =
        MICROS_BOOTSTRAP_SERVICE_CAPACITY;
    fixture->manifest.header.entry_count = 4;
    fixture->manifest.header.total_user_page_limit = 13;
    fixture->manifest.header.manifest_size = MICROS_BOOTSTRAP_MANIFEST_SIZE;

    for (index = 0; index < 4; ++index) {
        struct micros_bootstrap_manifest_entry *entry =
            &fixture->manifest.entries[index];

        entry->service_id = (uint32_t)index + 1;
        entry->image_id = (uint32_t)index + 101;
        entry->process_slot = (uint16_t)index;
        entry->stack_page_count = 1;
        entry->profile_id = (uint8_t)index + 1;
        entry->ready_timeout_counter_ticks = index == 0 ? 0 : 100;
        entry->user_page_limit = index == 0 ? 4 : 3;
        if (index != 0) {
            entry->prerequisites =
                UINT64_C(1) << (index - 1);
        }

        fixture->expected[index] =
            (struct micros_bootstrap_expected_service){
                .service_id = entry->service_id,
                .image_id = entry->image_id,
                .process_slot = entry->process_slot,
                .profile_id = entry->profile_id,
                .prerequisites = entry->prerequisites,
            };
        fixture->images[index] =
            (struct micros_bootstrap_image_info){
                .version = MICROS_BOOTSTRAP_IMAGE_VERSION,
                .image_id = entry->image_id,
                .entry = MICROS_USER_VIRTUAL_BASE,
                .config_address =
                    MICROS_USER_VIRTUAL_BASE + 0x1000,
                .config_size =
                    sizeof(struct micros_bootstrap_service_config),
                .page_count = 2,
                .image_end = MICROS_USER_VIRTUAL_BASE + 0x2000,
                .config_initially_zero = true,
            };
    }
}

static void initialize_tty_roles(struct tty_contract_fixture *fixture)
{
    bootstrap_manifest_test_set_name(
        fixture->manifest.entries[0].service_name, "bootstrap-launcher"
    );
    bootstrap_manifest_test_set_name(
        fixture->manifest.entries[0].profile_name, "BOOTSTRAP_LAUNCHER"
    );
    fixture->manifest.entries[0].role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER;
    bootstrap_manifest_test_set_name(
        fixture->expected[0].service_name, "bootstrap-launcher"
    );
    bootstrap_manifest_test_set_name(
        fixture->expected[0].profile_name, "BOOTSTRAP_LAUNCHER"
    );
    fixture->expected[0].role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER;

    bootstrap_manifest_test_set_name(fixture->manifest.entries[1].service_name, "vm");
    bootstrap_manifest_test_set_name(fixture->manifest.entries[1].profile_name, "VM");
    fixture->manifest.entries[1].role_flags = MICROS_BOOTSTRAP_ROLE_VM;
    bootstrap_manifest_test_set_name(fixture->expected[1].service_name, "vm");
    bootstrap_manifest_test_set_name(fixture->expected[1].profile_name, "VM");
    fixture->expected[1].role_flags = MICROS_BOOTSTRAP_ROLE_VM;
    fixture->expected[1].call_targets =
        UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER;
    fixture->images[1].vm_boot_info_address =
        MICROS_USER_VIRTUAL_BASE + 0x2000;
    fixture->images[1].vm_boot_info_size = MICROS_VM_BOOT_INFO_SIZE;
    fixture->images[1].vm_boot_info_initially_zero = true;

    bootstrap_manifest_test_set_name(fixture->manifest.entries[2].service_name, "pm");
    bootstrap_manifest_test_set_name(fixture->manifest.entries[2].profile_name, "PM");
    fixture->manifest.entries[2].role_flags = MICROS_BOOTSTRAP_ROLE_PM;
    bootstrap_manifest_test_set_name(fixture->expected[2].service_name, "pm");
    bootstrap_manifest_test_set_name(fixture->expected[2].profile_name, "PM");
    fixture->expected[2].role_flags = MICROS_BOOTSTRAP_ROLE_PM;
    fixture->expected[2].call_targets =
        (UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER)
        | (UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VM)
        | (UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS);

    bootstrap_manifest_test_set_name(fixture->manifest.entries[3].service_name, "tty");
    bootstrap_manifest_test_set_name(fixture->manifest.entries[3].profile_name, "TTY");
    fixture->manifest.entries[3].role_flags =
        MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER;
    fixture->manifest.entries[3].device_base =
        MICROS_TTY_UART_PHYSICAL_BASE;
    fixture->manifest.entries[3].device_length =
        MICROS_TTY_UART_MAPPED_LENGTH;
    fixture->manifest.entries[3].irq_source =
        MICROS_TTY_UART_IRQ_SOURCE;
    bootstrap_manifest_test_set_name(fixture->expected[3].service_name, "tty");
    bootstrap_manifest_test_set_name(fixture->expected[3].profile_name, "TTY");
    fixture->expected[3].role_flags =
        MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER;
    fixture->expected[3].call_targets =
        UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER;
    fixture->expected[3].device_base =
        MICROS_TTY_UART_PHYSICAL_BASE;
    fixture->expected[3].device_length =
        MICROS_TTY_UART_MAPPED_LENGTH;
    fixture->expected[3].irq_source = MICROS_TTY_UART_IRQ_SOURCE;
}

static bool accept_standard_profiles(struct tty_contract_fixture *fixture)
{
    fixture->profiles[0] = bootstrap_manifest_test_profile(1, "BOOTSTRAP_LAUNCHER");
    fixture->profiles[1] = bootstrap_manifest_test_profile(2, "VM");
    fixture->profiles[2] = bootstrap_manifest_test_profile(3, "PM");
    fixture->profiles[3] = bootstrap_manifest_test_profile(4, "TTY");
    fixture->profiles[4] = bootstrap_manifest_test_profile(5, "RAMFS");
    fixture->profiles[5] = bootstrap_manifest_test_profile(6, "VFS");
    fixture->profiles[6] = bootstrap_manifest_test_profile(7, "APPLICATION");

    EXPECT_TRUE(
        fixture->profiles[3].operations
            == (
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_CALL
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_NOTIFY
            )
        && fixture->profiles[3].call_targets
            == (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
            )
        && fixture->profiles[3].notify_targets
            == (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS
            )
        && fixture->profiles[3].kernel_operations
            == MICROS_KERNEL_OPERATION_TTY_CONTROL
        && fixture->profiles[4].operations
            == (
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_CALL
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE
            )
        && fixture->profiles[4].call_targets
            == (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
            )
        && fixture->profiles[5].operations
            == (
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_CALL
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE
            )
        && fixture->profiles[5].call_targets
            == (
                (
                    UINT32_C(1)
                    << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
                ) | (
                    UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_TTY
                ) | (
                    UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_RAMFS
                )
            )
        && micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            4,
            fixture->images,
            4,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_OK
        && fixture->plan.console_service_id == 4
        && fixture->plan.ordered_service_ids[3] == 4
    );
    return true;
}

static bool reject_io_profiles(struct tty_contract_fixture *fixture)
{
    memset(&fixture->sentinel, 0xa5, sizeof(fixture->sentinel));
    fixture->plan = fixture->sentinel;
    fixture->profiles[4].operations &=
        ~MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            4,
            fixture->images,
            4,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->profiles[4] = bootstrap_manifest_test_profile(5, "RAMFS");
    fixture->plan = fixture->sentinel;
    fixture->profiles[5].call_targets &=
        ~(UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_RAMFS);
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            4,
            fixture->images,
            4,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->profiles[5] = bootstrap_manifest_test_profile(6, "VFS");
    return true;
}

static bool reject_console_role_and_image(struct tty_contract_fixture *fixture)
{
    memset(&fixture->sentinel, 0xa5, sizeof(fixture->sentinel));
    fixture->plan = fixture->sentinel;
    fixture->manifest.entries[3].stack_page_count = 2;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            4,
            fixture->images,
            4,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_ROLE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->manifest.entries[3].stack_page_count = 1;

    fixture->images[3].image_end = MICROS_TTY_UART_VIRTUAL_BASE + 0x1000;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            4,
            fixture->images,
            4,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_IMAGE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->images[3].image_end = MICROS_USER_VIRTUAL_BASE + 0x2000;
    return true;
}

static bool reject_console_targets_and_prerequisites(
    struct tty_contract_fixture *fixture
)
{
    fixture->profiles[3].notify_targets = 0;
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            4,
            fixture->images,
            4,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->profiles[3] = bootstrap_manifest_test_profile(4, "TTY");

    fixture->profiles[5].call_targets &=
        ~(UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_TTY);
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            4,
            fixture->images,
            4,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_PROFILE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->profiles[5] = bootstrap_manifest_test_profile(6, "VFS");

    fixture->manifest.entries[3].prerequisites = UINT64_C(1);
    fixture->expected[3].prerequisites = UINT64_C(1);
    fixture->plan = fixture->sentinel;
    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            4,
            fixture->images,
            4,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_ROLE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
    );
    fixture->manifest.entries[3].prerequisites = UINT64_C(1) << 2;
    fixture->expected[3].prerequisites = UINT64_C(1) << 2;
    return true;
}

static bool accept_six_service_configuration(struct tty_contract_fixture *fixture)
{
    fixture->manifest.header.entry_count = 6;
    fixture->manifest.header.total_user_page_limit = 19;
    fixture->manifest.entries[4] =
        (struct micros_bootstrap_manifest_entry){
            .service_id = 5,
            .image_id = 105,
            .process_slot = 4,
            .stack_page_count = 1,
            .profile_id = MICROS_PRIVILEGE_PROFILE_RAMFS,
            .prerequisites = UINT64_C(1) << 3,
            .ready_timeout_counter_ticks = 100,
            .user_page_limit = 3,
        };
    bootstrap_manifest_test_set_name(fixture->manifest.entries[4].service_name, "ramfs");
    bootstrap_manifest_test_set_name(fixture->manifest.entries[4].profile_name, "RAMFS");
    fixture->expected[4] = (struct micros_bootstrap_expected_service){
        .service_id = 5,
        .image_id = 105,
        .process_slot = 4,
        .profile_id = MICROS_PRIVILEGE_PROFILE_RAMFS,
        .prerequisites = UINT64_C(1) << 3,
        .call_targets =
            UINT32_C(1)
            << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
    };
    bootstrap_manifest_test_set_name(fixture->expected[4].service_name, "ramfs");
    bootstrap_manifest_test_set_name(fixture->expected[4].profile_name, "RAMFS");
    fixture->images[4] = fixture->images[3];
    fixture->images[4].image_id = 105;

    fixture->manifest.entries[5] =
        (struct micros_bootstrap_manifest_entry){
            .service_id = 6,
            .image_id = 106,
            .process_slot = 5,
            .stack_page_count = 1,
            .profile_id = MICROS_PRIVILEGE_PROFILE_VFS,
            .prerequisites = UINT64_C(1) << 4,
            .ready_timeout_counter_ticks = 100,
            .user_page_limit = 3,
        };
    bootstrap_manifest_test_set_name(fixture->manifest.entries[5].service_name, "vfs");
    bootstrap_manifest_test_set_name(fixture->manifest.entries[5].profile_name, "VFS");
    fixture->expected[5] = (struct micros_bootstrap_expected_service){
        .service_id = 6,
        .image_id = 106,
        .process_slot = 5,
        .profile_id = MICROS_PRIVILEGE_PROFILE_VFS,
        .prerequisites = UINT64_C(1) << 4,
        .call_targets =
            (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
            ) | (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_TTY
            ) | (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_RAMFS
            ),
    };
    bootstrap_manifest_test_set_name(fixture->expected[5].service_name, "vfs");
    bootstrap_manifest_test_set_name(fixture->expected[5].profile_name, "VFS");
    fixture->images[5] = fixture->images[3];
    fixture->images[5].image_id = 106;

    EXPECT_TRUE(
        micros_bootstrap_manifest_validate(
            &fixture->manifest,
            fixture->expected,
            6,
            fixture->images,
            6,
            fixture->profiles,
            7,
            32,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_OK
        && fixture->plan.ordered_service_ids[4] == 5
        && fixture->plan.ordered_service_ids[5] == 6
    );
    return true;
}

bool test_tty_service_contract(void)
{
    struct tty_contract_fixture fixture;

    memset(fixture.expected, 0, sizeof(fixture.expected));
    memset(fixture.images, 0, sizeof(fixture.images));
    memset(fixture.profiles, 0, sizeof(fixture.profiles));
    initialize_tty_entries(&fixture);
    initialize_tty_roles(&fixture);
    return (
        accept_standard_profiles(&fixture)
        && reject_io_profiles(&fixture)
        && reject_console_role_and_image(&fixture)
        && reject_console_targets_and_prerequisites(&fixture)
        && accept_six_service_configuration(&fixture)
    );
}
