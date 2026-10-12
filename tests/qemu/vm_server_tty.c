#include "tests/qemu/vm_server_internal.h"

#include "lib/runtime/raw_syscall.h"
#include "micros/tty.h"
#include "micros/vm.h"

#ifdef MICROS_VM_TTY_HANDOFF_TEST

static bool vm_server_notification_is_canonical(
    const struct micros_ipc_message *message,
    uint64_t event_mask
)
{
    size_t index;
    uint64_t observed = 0;

    if (
        message->source != MICROS_ENDPOINT_NONE
        || message->type != MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        || message->reply_token != 0
    ) {
        return false;
    }
    for (index = 0; index < 8; ++index) {
        observed |= (uint64_t)message->payload[index]
            << (index * 8);
    }
    return (
        observed == event_mask
        && vm_server_bytes_are_zero(
            &message->payload[8],
            sizeof(message->payload) - 8
        )
    );
}

bool vm_server_map_tty_uart(struct vm_server_context *context)
{
    struct micros_ipc_message message;
    int tty_index = vm_server_find_service(MICROS_TTY_SERVICE_ID);
    uint64_t irq_and_length =
        (uint64_t)MICROS_TTY_UART_IRQ_SOURCE << 32
        | MICROS_TTY_UART_MAPPED_LENGTH;

    if (tty_index < 0 || context->tty_mapping.mapped) {
        return false;
    }
    vm_server_clear_bytes(&message, sizeof(message));
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
            != MICROS_SYSCALL_ABI_OK
        || !vm_server_notification_is_canonical(
            &message,
            MICROS_KERNEL_EVENT_CONSOLE_MAP_REQUEST
        )
        || micros_runtime_raw_syscall(
            MICROS_VM_HANDOFF_MAP_TTY_UART,
            MICROS_TTY_MAPPING_VERSION,
            MICROS_TTY_SERVICE_ID,
            micros_bootstrap_service_config.services[tty_index]
                .endpoint,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_PHYSICAL_BASE,
            irq_and_length,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    context->tty_mapping.mapped = true;
    context->tty_mapping.endpoint =
        micros_bootstrap_service_config.services[tty_index].endpoint;
    context->tty_mapping.virtual_base = MICROS_TTY_UART_VIRTUAL_BASE;
    context->tty_mapping.physical_base = MICROS_TTY_UART_PHYSICAL_BASE;
    context->tty_mapping.irq_source = MICROS_TTY_UART_IRQ_SOURCE;
    context->tty_mapping.mapped_length = MICROS_TTY_UART_MAPPED_LENGTH;
    return true;
}

#endif
