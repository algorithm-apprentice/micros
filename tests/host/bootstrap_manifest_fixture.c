#include "bootstrap_manifest_test_internal.h"

#include "micros/vm_bootstrap.h"

void bootstrap_manifest_test_set_name(
    char output[MICROS_BOOTSTRAP_NAME_SIZE],
    const char *name
)
{
    size_t length = strlen(name);

    memset(output, 0, MICROS_BOOTSTRAP_NAME_SIZE);
    memcpy(output, name, length);
}

struct micros_privilege_profile bootstrap_manifest_test_profile(
    uint8_t id,
    const char *name
)
{
    struct micros_privilege_profile result;

    memset(&result, 0, sizeof(result));
    result.id = id;
    bootstrap_manifest_test_set_name(result.name, name);
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
            result.call_targets |=
                (
                    UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VM
                ) | (
                    UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS
                );
            result.kernel_operations =
                MICROS_KERNEL_OPERATION_PM_CONTROL;
        } else if (id == MICROS_PRIVILEGE_PROFILE_TTY) {
            result.operations |=
                MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_NOTIFY;
            result.notify_targets =
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS;
            result.kernel_operations =
                MICROS_KERNEL_OPERATION_TTY_CONTROL;
        } else if (id == MICROS_PRIVILEGE_PROFILE_RAMFS) {
            result.operations |=
                MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE;
        } else if (id == MICROS_PRIVILEGE_PROFILE_VFS) {
            result.operations |=
                MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE;
            result.call_targets |=
                (
                    UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_TTY
                ) | (
                    UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_RAMFS
                );
        }
    }
    return result;
}

void bootstrap_manifest_test_initialize_manifest(
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
    bootstrap_manifest_test_set_name(vm->service_name, "vm");
    bootstrap_manifest_test_set_name(vm->profile_name, "VM");
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
    bootstrap_manifest_test_set_name(launcher->service_name, "bootstrap-launcher");
    bootstrap_manifest_test_set_name(launcher->profile_name, "BOOTSTRAP_LAUNCHER");
    launcher->user_page_limit = 4;
    launcher->role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER;
}

void bootstrap_manifest_test_initialize_validation_fixture(
    struct validation_fixture *fixture
)
{
    bootstrap_manifest_test_initialize_manifest(&fixture->manifest);
    memset(fixture->expected, 0, sizeof(fixture->expected));
    fixture->expected[0] =
        (struct micros_bootstrap_expected_service){
            .service_id = 1,
            .image_id = 101,
            .process_slot = 0,
            .profile_id = 1,
            .role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER,
        };
    bootstrap_manifest_test_set_name(
        fixture->expected[0].service_name,
        "bootstrap-launcher"
    );
    bootstrap_manifest_test_set_name(
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
    bootstrap_manifest_test_set_name(fixture->expected[1].service_name, "vm");
    bootstrap_manifest_test_set_name(fixture->expected[1].profile_name, "VM");
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
    fixture->profiles[0] =
        bootstrap_manifest_test_profile(1, "BOOTSTRAP_LAUNCHER");
    fixture->profiles[1] = bootstrap_manifest_test_profile(2, "VM");
}

bool bootstrap_manifest_test_expect_validation_error(
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
