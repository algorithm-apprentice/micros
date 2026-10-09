#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap.h"
#include "micros/bootstrap_control.h"
#include "micros/tty.h"
#include "servers/tty/tty_control.h"
#include "servers/tty/tty_service_core.h"
#include "servers/tty/tty_uart.h"

#ifndef MICROS_TTY_CONFIGURED_VFS_SERVICE_ID
#define MICROS_TTY_CONFIGURED_VFS_SERVICE_ID UINT32_C(6)
#endif

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;

static struct micros_tty_service_state tty_service;
static struct micros_tty_uart_state tty_uart;
static volatile uint64_t tty_service_data =
    UINT64_C(0x5454595345525643);

static void tty_service_clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool tty_service_bytes_are_zero(
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

static void tty_service_write_u32_le(
    uint8_t *bytes,
    uint32_t value
)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t tty_service_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static uint64_t tty_service_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool validate_configuration(
    micros_endpoint_t *vfs_endpoint
)
{
    size_t index;
    micros_endpoint_t selected = MICROS_ENDPOINT_NONE;

    if (
        vfs_endpoint == NULL
        || micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.service_id
            != MICROS_TTY_SERVICE_ID
        || micros_bootstrap_service_config.service_count
            < MICROS_TTY_SERVICE_ID
        || micros_bootstrap_service_config.service_count
            > MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || micros_bootstrap_service_config.self_endpoint
            != micros_bootstrap_service_config
                .services[MICROS_TTY_SERVICE_ID - 1].endpoint
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
            || service->endpoint == MICROS_ENDPOINT_NONE
            || service->endpoint == MICROS_ENDPOINT_ANY
        ) {
            return false;
        }
        if (
            service->service_id
                == MICROS_TTY_CONFIGURED_VFS_SERVICE_ID
        ) {
            selected = service->endpoint;
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
    *vfs_endpoint = selected;
    return true;
}

static bool send_ready(void)
{
    struct micros_ipc_message message;

    tty_service_clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    tty_service_write_u32_le(
        &message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    tty_service_write_u32_le(
        &message.payload[4],
        micros_bootstrap_service_config.service_id
    );
    tty_service_write_u32_le(
        &message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    tty_service_write_u32_le(
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
        && tty_service_read_u32_le(&message.payload[0])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && tty_service_read_u32_le(&message.payload[4])
            == micros_bootstrap_service_config.service_id
        && tty_service_read_u32_le(&message.payload[8])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && tty_service_read_u32_le(&message.payload[12]) == 0
        && tty_service_read_u32_le(&message.payload[16])
            == micros_bootstrap_service_config.self_endpoint
        && tty_service_bytes_are_zero(
            &message.payload[20],
            sizeof(message.payload) - 20
        )
    );
}

static uint8_t uart_read(void *context, uint8_t offset)
{
    volatile uint8_t *registers = context;

    return registers[offset];
}

static void uart_write(
    void *context,
    uint8_t offset,
    uint8_t value
)
{
    volatile uint8_t *registers = context;

    registers[offset] = value;
}

static enum micros_tty_service_copy_result classify_copy_result(
    micros_runtime_result_t result
)
{
    switch (result) {
    case MICROS_SYSCALL_ABI_OK:
        return MICROS_TTY_SERVICE_COPY_OK;
    case MICROS_SYSCALL_ABI_ARGUMENT:
    case MICROS_SYSCALL_ABI_DEAD_ENDPOINT:
    case MICROS_SYSCALL_ABI_UNAUTHORIZED:
    case MICROS_SYSCALL_ABI_STATE:
    case MICROS_SYSCALL_ABI_MEMORY_FAULT:
    case MICROS_SYSCALL_ABI_STALE_GRANT:
    case MICROS_SYSCALL_ABI_RANGE:
        return MICROS_TTY_SERVICE_COPY_REJECTED;
    case MICROS_SYSCALL_ABI_DEADLOCK:
    case MICROS_SYSCALL_ABI_REPLY_TOKEN:
    case MICROS_SYSCALL_ABI_REPLY_TOKEN_EXHAUSTED:
    case MICROS_SYSCALL_ABI_ENDPOINT_CLOSING:
    case MICROS_SYSCALL_ABI_CAPACITY:
    default:
        return MICROS_TTY_SERVICE_COPY_INVARIANT;
    }
}

static enum micros_tty_service_copy_result service_copy(
    void *context,
    enum micros_tty_service_copy_direction direction,
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
        return MICROS_TTY_SERVICE_COPY_INVARIANT;
    }
    if (direction == MICROS_TTY_SERVICE_COPY_FROM_VFS) {
        result = micros_runtime_grant_copy_from(
            vfs_endpoint,
            grant,
            (size_t)grant_offset,
            (uintptr_t)local,
            length
        );
    } else if (direction == MICROS_TTY_SERVICE_COPY_TO_VFS) {
        result = micros_runtime_grant_copy_to(
            vfs_endpoint,
            grant,
            (size_t)grant_offset,
            (uintptr_t)local,
            length
        );
    } else {
        return MICROS_TTY_SERVICE_COPY_INVARIANT;
    }
    return classify_copy_result(result);
}

static bool notification_is_tty_irq(
    const struct micros_ipc_message *message
)
{
    return (
        message->source == MICROS_ENDPOINT_NONE
        && message->type == MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        && message->reply_token == 0
        && tty_service_read_u64_le(&message->payload[0])
            == MICROS_KERNEL_EVENT_TTY_IRQ
        && tty_service_bytes_are_zero(
            &message->payload[8],
            sizeof(message->payload) - 8
        )
    );
}

static bool notify_vfs(uint64_t events)
{
    return (
        events == 0
        || (
            tty_service.vfs_endpoint != MICROS_ENDPOINT_NONE
            && (
                events
                & ~(
                    MICROS_TTY_EVENT_COMPLETION
                    | MICROS_TTY_EVENT_WRITABLE
                )
            ) == 0
            && micros_runtime_notify(
                tty_service.vfs_endpoint,
                events
            ) == MICROS_SYSCALL_ABI_OK
        )
    );
}

static bool handle_irq(
    const struct micros_tty_uart_bus *bus,
    const struct micros_tty_service_io *io
)
{
    struct micros_tty_uart_drain_effects drain_effects;
    uint64_t events = 0;
    uint64_t completion_event;

    if (
        micros_tty_uart_drain(
            &tty_uart,
            bus,
            &tty_service.terminal,
            &drain_effects
        ) != MICROS_TTY_UART_OK
        || micros_tty_service_complete_read(
            &tty_service,
            io,
            &completion_event
        ) != MICROS_TTY_SERVICE_OK
    ) {
        return false;
    }
    events |= completion_event;
    if (drain_effects.writable) {
        events |= MICROS_TTY_EVENT_WRITABLE;
    }
    return (
        notify_vfs(events)
        && micros_tty_control_irq_complete()
            == MICROS_SYSCALL_ABI_OK
        && micros_tty_uart_validate(
            &tty_uart,
            &tty_service.terminal
        ) == MICROS_TTY_UART_OK
        && micros_tty_service_validate(&tty_service)
            == MICROS_TTY_SERVICE_OK
    );
}

static bool handle_call(
    const struct micros_tty_uart_bus *bus,
    const struct micros_tty_service_io *io,
    const struct micros_ipc_message *message
)
{
    struct micros_tty_service_reply_action action;
    uint64_t notification;

    if (
        micros_tty_service_handle_call(
            &tty_service,
            message,
            io,
            &action
        ) != MICROS_TTY_SERVICE_OK
        || micros_tty_uart_apply_effects(
            &tty_uart,
            bus,
            &tty_service.terminal,
            &action.uart_effects
        ) != MICROS_TTY_UART_OK
        || micros_runtime_reply(
            action.reply_token,
            &action.message
        ) != MICROS_SYSCALL_ABI_OK
        || micros_tty_service_commit_reply(
            &tty_service,
            &action,
            &notification
        ) != MICROS_TTY_SERVICE_OK
    ) {
        return false;
    }
    return notify_vfs(notification);
}

void micros_service_main(void)
{
    micros_endpoint_t vfs_endpoint;
    struct micros_tty_uart_bus bus = {
        .read = uart_read,
        .write = uart_write,
        .context = (void *)(uintptr_t)MICROS_TTY_UART_VIRTUAL_BASE,
    };
    const struct micros_tty_service_io io = {
        .copy = service_copy,
        .context = NULL,
    };
    struct micros_ipc_message message;

    if (
        tty_service_data != UINT64_C(0x5454595345525643)
        || !validate_configuration(&vfs_endpoint)
        || micros_tty_service_initialize(
            &tty_service,
            micros_bootstrap_service_config.self_endpoint,
            vfs_endpoint
        ) != MICROS_TTY_SERVICE_OK
        || micros_tty_uart_initialize(
            &tty_uart,
            &bus,
            &tty_service.terminal
        ) != MICROS_TTY_UART_OK
        || micros_tty_control_commit(
            micros_bootstrap_service_config.self_endpoint
        ) != MICROS_SYSCALL_ABI_OK
        || micros_tty_service_commit_ownership(&tty_service)
            != MICROS_TTY_SERVICE_OK
        || !send_ready()
    ) {
        __builtin_trap();
    }

    for (;;) {
        tty_service_clear_bytes(&message, sizeof(message));
        if (
            micros_runtime_receive(
                MICROS_ENDPOINT_ANY,
                &message
            ) != MICROS_SYSCALL_ABI_OK
        ) {
            __builtin_trap();
        }
        if (message.type == MICROS_IPC_TYPE_KERNEL_NOTIFICATION) {
            if (
                !notification_is_tty_irq(&message)
                || !handle_irq(&bus, &io)
            ) {
                __builtin_trap();
            }
        } else if (!handle_call(&bus, &io, &message)) {
            __builtin_trap();
        }
    }
}
