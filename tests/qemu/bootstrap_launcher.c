#include "tests/qemu/bootstrap_launcher_internal.h"

#include "tests/qemu/bootstrap_launcher_control.h"

static void initialize_service_states(
    struct bootstrap_launcher_context *launcher
)
{
    size_t order_index;

    for (
        order_index = 0;
        order_index < launcher->manifest->header.entry_count;
        ++order_index
    ) {
        int manifest_index = bootstrap_launcher_find_manifest_service(
            launcher->manifest,
            launcher->order[order_index]
        );

        if (manifest_index < 0) {
            bootstrap_launcher_fail(
                0,
                MICROS_ENDPOINT_NONE,
                MICROS_BOOTSTRAP_FAILURE_AUTHORITY,
                0
            );
        }
        launcher->state[manifest_index] = order_index == 0
            ? LAUNCHER_SERVICE_READY
            : LAUNCHER_SERVICE_PREPARED;
    }
}

static void begin_console_if_needed(
    const struct micros_bootstrap_manifest_entry *entry,
    uint32_t service_id
)
{
    if (
        (
            entry->role_flags
            & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
        ) != 0
    ) {
        struct micros_ipc_message mapped = {0};

        if (
            micros_bootstrap_launcher_console_begin(service_id)
                != MICROS_SYSCALL_ABI_OK
            || micros_runtime_receive(
                MICROS_ENDPOINT_ANY,
                &mapped
            ) != MICROS_SYSCALL_ABI_OK
            || !bootstrap_launcher_console_mapped_notification_is_canonical(
                &mapped
            )
        ) {
            bootstrap_launcher_fail(
                service_id,
                MICROS_ENDPOINT_NONE,
                MICROS_BOOTSTRAP_FAILURE_CONSOLE_PROTOCOL,
                0
            );
        }
    }
}

static void release_and_accept_service(
    struct bootstrap_launcher_context *launcher,
    uint32_t service_id
)
{
    int expected_index = bootstrap_launcher_find_manifest_service(
        launcher->manifest,
        service_id
    );
    struct micros_ipc_message message = {0};
    int64_t result;

    if (expected_index < 0) {
        bootstrap_launcher_fail(
            service_id,
            MICROS_ENDPOINT_NONE,
            MICROS_BOOTSTRAP_FAILURE_AUTHORITY,
            0
        );
    }
    begin_console_if_needed(
        &launcher->manifest->entries[expected_index],
        service_id
    );
    if (
        bootstrap_launcher_bootstrap_control(
            MICROS_BOOTSTRAP_COMMAND_RELEASE,
            service_id,
            0,
            0,
            0
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        bootstrap_launcher_fail(
            service_id,
            MICROS_ENDPOINT_NONE,
            MICROS_BOOTSTRAP_FAILURE_RELEASE_TRANSITION,
            0
        );
    }
    launcher->state[expected_index] = LAUNCHER_SERVICE_STARTING;
    result = micros_runtime_receive(MICROS_ENDPOINT_ANY, &message);
    if (result != MICROS_SYSCALL_ABI_OK) {
        bootstrap_launcher_fail(
            service_id,
            MICROS_ENDPOINT_NONE,
            MICROS_BOOTSTRAP_FAILURE_RELEASE_TRANSITION,
            0
        );
    }
    bootstrap_launcher_validate_ready_message(launcher, service_id, &message);
    if (
        bootstrap_launcher_bootstrap_control(
            MICROS_BOOTSTRAP_COMMAND_ACCEPT_READY,
            service_id,
            message.source,
            message.reply_token,
            0
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        bootstrap_launcher_fail(
            service_id,
            MICROS_ENDPOINT_NONE,
            MICROS_BOOTSTRAP_FAILURE_RELEASE_TRANSITION,
            0
        );
    }
    launcher->state[expected_index] = LAUNCHER_SERVICE_READY;
}

void micros_service_main(void)
{
    struct bootstrap_launcher_context launcher = {0};
    size_t order_index;

    launcher.manifest =
        (const struct micros_bootstrap_manifest *)(uintptr_t)
            MICROS_BOOTSTRAP_MANIFEST_VIEW;
    if (
        micros_bootstrap_launcher_rodata
            != UINT64_C(0x4c41554e43484552)
        || micros_bootstrap_launcher_data
            != UINT64_C(0x4253544c41554e43)
        || !bootstrap_launcher_validate_manifest(
            launcher.manifest,
            launcher.order
        )
        || !bootstrap_launcher_validate_configuration(launcher.manifest)
    ) {
        bootstrap_launcher_fail(
            0,
            MICROS_ENDPOINT_NONE,
            MICROS_BOOTSTRAP_FAILURE_AUTHORITY,
            0
        );
    }
    initialize_service_states(&launcher);
    for (
        order_index = 1;
        order_index < launcher.manifest->header.entry_count;
        ++order_index
    ) {
        release_and_accept_service(&launcher, launcher.order[order_index]);
    }
    (void)bootstrap_launcher_bootstrap_control(
        MICROS_BOOTSTRAP_COMMAND_COMPLETE,
        0,
        0,
        0,
        0
    );
    bootstrap_launcher_fail(
        0,
        MICROS_ENDPOINT_NONE,
        MICROS_BOOTSTRAP_FAILURE_COMPLETION,
        0
    );
}
