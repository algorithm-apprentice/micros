#include "tests/qemu/bootstrap_launcher_internal.h"

bool bootstrap_launcher_console_mapped_notification_is_canonical(
    const struct micros_ipc_message *message
)
{
    return (
        message->source == MICROS_ENDPOINT_NONE
        && message->type == MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        && message->reply_token == 0
        && bootstrap_launcher_read_u64_le(&message->payload[0])
            == MICROS_KERNEL_EVENT_CONSOLE_MAPPED
        && bootstrap_launcher_bytes_are_zero(
            &message->payload[8],
            sizeof(message->payload) - 8
        )
    );
}

static uint32_t message_shape_detail(
    const struct micros_ipc_message *message
)
{
    size_t index;

    if (
        (
            message->type & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
    ) {
        return 9;
    }
    if (message->type != MICROS_BOOTSTRAP_MESSAGE_READY) {
        return 1;
    }
    if (message->reply_token == 0) {
        return 2;
    }
    if (
        bootstrap_launcher_read_u32_le(&message->payload[0])
        != MICROS_BOOTSTRAP_MANIFEST_VERSION
    ) {
        return 3;
    }
    if (
        bootstrap_launcher_read_u32_le(&message->payload[8])
        != MICROS_BOOTSTRAP_MANIFEST_VERSION
    ) {
        return 5;
    }
    if (bootstrap_launcher_read_u32_le(&message->payload[12]) != 0) {
        return 6;
    }
    for (index = 20; index < sizeof(message->payload); ++index) {
        if (message->payload[index] != 0) {
            return 8;
        }
    }
    return 0;
}

static int resolve_ready_source(
    const struct micros_bootstrap_manifest *manifest,
    uint32_t service_id,
    const struct micros_ipc_message *message
)
{
    uint32_t detail = message_shape_detail(message);
    int source_config_index =
        bootstrap_launcher_find_config_endpoint(message->source);
    int source_manifest_index = source_config_index < 0
        ? -1
        : bootstrap_launcher_find_manifest_service(
            manifest,
            micros_bootstrap_service_config.services[
                source_config_index
            ].service_id
        );

    if (detail != 0) {
        bootstrap_launcher_fail(
            source_manifest_index < 0
                ? service_id
                : manifest->entries[source_manifest_index].service_id,
            message->source,
            MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED,
            detail
        );
    }
    if (source_manifest_index < 0) {
        bootstrap_launcher_fail(
            service_id,
            message->source,
            MICROS_BOOTSTRAP_FAILURE_READY_FOREIGN,
            0
        );
    }
    return source_manifest_index;
}

static void classify_ready_source(
    const struct bootstrap_launcher_context *launcher,
    uint32_t service_id,
    const struct micros_ipc_message *message,
    int source_manifest_index
)
{
    const struct micros_bootstrap_manifest *manifest = launcher->manifest;

    if (
        launcher->state[source_manifest_index]
            == LAUNCHER_SERVICE_STARTING
        && manifest->entries[source_manifest_index].service_id
            == service_id
    ) {
        if (bootstrap_launcher_read_u32_le(&message->payload[4]) != service_id) {
            bootstrap_launcher_fail(
                service_id,
                message->source,
                MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED,
                4
            );
        }
        if (
            bootstrap_launcher_read_u32_le(&message->payload[16])
            != message->source
        ) {
            bootstrap_launcher_fail(
                service_id,
                message->source,
                MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED,
                7
            );
        }
    } else if (
        launcher->state[source_manifest_index]
            == LAUNCHER_SERVICE_READY
    ) {
        bootstrap_launcher_fail(
            manifest->entries[source_manifest_index].service_id,
            message->source,
            MICROS_BOOTSTRAP_FAILURE_READY_DUPLICATE,
            0
        );
    } else if (
        launcher->state[source_manifest_index]
            == LAUNCHER_SERVICE_PREPARED
    ) {
        bootstrap_launcher_fail(
            manifest->entries[source_manifest_index].service_id,
            message->source,
            MICROS_BOOTSTRAP_FAILURE_READY_EARLY,
            0
        );
    } else {
        bootstrap_launcher_fail(
            service_id,
            message->source,
            MICROS_BOOTSTRAP_FAILURE_READY_FOREIGN,
            0
        );
    }
}

void bootstrap_launcher_validate_ready_message(
    const struct bootstrap_launcher_context *launcher,
    uint32_t service_id,
    const struct micros_ipc_message *message
)
{
    int source_manifest_index =
        resolve_ready_source(launcher->manifest, service_id, message);

    classify_ready_source(launcher, service_id, message, source_manifest_index);
}
