#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap.h"
#include "micros/bootstrap_control.h"
#include "micros/ramfs.h"
#include "servers/ramfs/ramfs_core.h"
#include "servers/ramfs/ramfs_embedded_seed.h"

#define MICROS_RAMFS_CONFIGURED_VFS_SERVICE_ID UINT32_C(6)
#define MICROS_RAMFS_SERVICE_DATA_MAGIC \
    UINT64_C(0x52414d4653535256)

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;

static struct micros_ramfs_state ramfs_state;
static volatile uint64_t ramfs_service_data =
    MICROS_RAMFS_SERVICE_DATA_MAGIC;

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool service_bytes_are_zero(
    const uint8_t *bytes,
    size_t size
)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static void service_write_u32_le(
    uint8_t *bytes,
    uint32_t value
)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t service_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static bool endpoint_is_canonical(micros_endpoint_t endpoint)
{
    const uint32_t slot_mask =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1;
    uint32_t generation;

    if (
        endpoint == MICROS_ENDPOINT_NONE
        || endpoint == MICROS_ENDPOINT_ANY
        || (endpoint & slot_mask) >= MICROS_PROCESS_CAPACITY
    ) {
        return false;
    }
    generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;
    return (
        generation != 0
        && generation <= MICROS_ENDPOINT_GENERATION_MAX
    );
}

static bool validate_configuration(
    micros_endpoint_t *vfs_endpoint
)
{
    size_t index;
    size_t other;

    if (
        vfs_endpoint == NULL
        || micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.service_id
            != MICROS_RAMFS_SERVICE_ID
        || micros_bootstrap_service_config.service_count
            != MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || micros_bootstrap_service_config.self_endpoint
            != micros_bootstrap_service_config
                .services[MICROS_RAMFS_SERVICE_ID - 1].endpoint
        || micros_bootstrap_service_config.launcher_endpoint
            != micros_bootstrap_service_config.services[0].endpoint
        || micros_bootstrap_service_config.manifest_view_address != 0
    ) {
        return false;
    }
    for (
        index = 0;
        index < micros_bootstrap_service_config.service_count;
        ++index
    ) {
        const volatile struct micros_bootstrap_service_endpoint *service =
            &micros_bootstrap_service_config.services[index];

        if (
            service->service_id != index + 1
            || !endpoint_is_canonical(service->endpoint)
        ) {
            return false;
        }
        for (other = 0; other < index; ++other) {
            if (
                service->endpoint
                == micros_bootstrap_service_config
                    .services[other].endpoint
            ) {
                return false;
            }
        }
    }
    for (
        index = 0;
        index < sizeof(micros_bootstrap_service_config.reserved)
                / sizeof(micros_bootstrap_service_config.reserved[0]);
        ++index
    ) {
        if (micros_bootstrap_service_config.reserved[index] != 0) {
            return false;
        }
    }
    *vfs_endpoint = micros_bootstrap_service_config.services[
        MICROS_RAMFS_CONFIGURED_VFS_SERVICE_ID - 1
    ].endpoint;
    return endpoint_is_canonical(*vfs_endpoint);
}

static bool send_ready(void)
{
    struct micros_ipc_message message;

    clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    service_write_u32_le(
        &message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    service_write_u32_le(
        &message.payload[4],
        micros_bootstrap_service_config.service_id
    );
    service_write_u32_le(
        &message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    service_write_u32_le(
        &message.payload[16],
        micros_bootstrap_service_config.self_endpoint
    );
    if (
        micros_runtime_call(
            micros_bootstrap_service_config.launcher_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    return (
        message.source
            == micros_bootstrap_service_config.launcher_endpoint
        && message.type == MICROS_BOOTSTRAP_MESSAGE_READY_ACK
        && message.reply_token == 0
        && service_read_u32_le(&message.payload[0])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && service_read_u32_le(&message.payload[4])
            == micros_bootstrap_service_config.service_id
        && service_read_u32_le(&message.payload[8])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && service_read_u32_le(&message.payload[12]) == 0
        && service_read_u32_le(&message.payload[16])
            == micros_bootstrap_service_config.self_endpoint
        && service_bytes_are_zero(
            &message.payload[20],
            sizeof(message.payload) - 20
        )
    );
}

static enum micros_ramfs_copy_result classify_copy_result(
    micros_runtime_result_t result
)
{
    switch (result) {
    case MICROS_SYSCALL_ABI_OK:
        return MICROS_RAMFS_COPY_OK;
    case MICROS_SYSCALL_ABI_ARGUMENT:
    case MICROS_SYSCALL_ABI_DEAD_ENDPOINT:
    case MICROS_SYSCALL_ABI_UNAUTHORIZED:
    case MICROS_SYSCALL_ABI_STATE:
    case MICROS_SYSCALL_ABI_MEMORY_FAULT:
    case MICROS_SYSCALL_ABI_STALE_GRANT:
    case MICROS_SYSCALL_ABI_RANGE:
        return MICROS_RAMFS_COPY_REJECTED;
    case MICROS_SYSCALL_ABI_DEADLOCK:
    case MICROS_SYSCALL_ABI_REPLY_TOKEN:
    case MICROS_SYSCALL_ABI_REPLY_TOKEN_EXHAUSTED:
    case MICROS_SYSCALL_ABI_ENDPOINT_CLOSING:
    case MICROS_SYSCALL_ABI_CAPACITY:
    default:
        return MICROS_RAMFS_COPY_INVARIANT;
    }
}

static enum micros_ramfs_copy_result service_copy(
    void *context,
    enum micros_ramfs_copy_direction direction,
    micros_endpoint_t vfs_endpoint,
    micros_grant_t grant,
    uint64_t grant_offset,
    uint8_t *local,
    size_t length
)
{
    micros_runtime_result_t result;

    (void)context;
    if (
        grant_offset > SIZE_MAX
        || (
            length != 0
            && local == NULL
        )
    ) {
        return MICROS_RAMFS_COPY_INVARIANT;
    }
    if (direction == MICROS_RAMFS_COPY_FROM_VFS) {
        result = micros_runtime_grant_copy_from(
            vfs_endpoint,
            grant,
            (size_t)grant_offset,
            (uintptr_t)local,
            length
        );
    } else if (direction == MICROS_RAMFS_COPY_TO_VFS) {
        result = micros_runtime_grant_copy_to(
            vfs_endpoint,
            grant,
            (size_t)grant_offset,
            (uintptr_t)local,
            length
        );
    } else {
        return MICROS_RAMFS_COPY_INVARIANT;
    }
    return classify_copy_result(result);
}

void micros_service_main(void)
{
    micros_endpoint_t vfs_endpoint;
    const struct micros_ramfs_io io = {
        .copy = service_copy,
        .context = NULL,
    };
    struct micros_ipc_message message;

    if (
        ramfs_service_data != MICROS_RAMFS_SERVICE_DATA_MAGIC
        || !validate_configuration(&vfs_endpoint)
        || micros_ramfs_state_initialize(
            &ramfs_state,
            vfs_endpoint,
            micros_ramfs_embedded_seed,
            micros_ramfs_embedded_seed_size
        ) != MICROS_RAMFS_CORE_OK
        || !send_ready()
    ) {
        __builtin_trap();
    }

    clear_bytes(&message, sizeof(message));
    if (
        micros_runtime_receive(
            MICROS_ENDPOINT_ANY,
            &message
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    for (;;) {
        struct micros_ramfs_reply_action action;
        struct micros_ipc_message next;

        if (
            micros_ramfs_handle_call(
                &ramfs_state,
                &message,
                &io,
                &action
            ) != MICROS_RAMFS_CORE_OK
            || !action.active
        ) {
            __builtin_trap();
        }
        clear_bytes(&next, sizeof(next));
        if (
            micros_runtime_reply_receive(
                action.reply_token,
                &action.message,
                MICROS_ENDPOINT_ANY,
                &next
            ) != MICROS_SYSCALL_ABI_OK
        ) {
            __builtin_trap();
        }
        message = next;
    }
}
