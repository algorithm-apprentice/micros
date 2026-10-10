#include "kernel/tty_control_core.h"

#include <stddef.h>
#include <stdint.h>

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size)
{
    uint8_t *output = destination;
    const uint8_t *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static bool pointer_is_aligned(const void *pointer, size_t alignment)
{
    return (uintptr_t)pointer % alignment == 0;
}

enum micros_syscall_abi_result micros_tty_control_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_tty_control_request *request
)
{
    struct micros_tty_control_request candidate;

    if (
        arguments == NULL
        || request == NULL
        || !pointer_is_aligned(
            arguments,
            _Alignof(struct micros_syscall_arguments)
        )
        || !pointer_is_aligned(
            request,
            _Alignof(struct micros_tty_control_request)
        )
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    if (
        arguments->a7 != MICROS_SYSCALL_ABI_TTY_CONTROL
        || arguments->a0 > UINT32_MAX
        || arguments->a1 > UINT32_MAX
        || arguments->a2 > UINT32_MAX
        || arguments->a3 > UINT32_MAX
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    candidate.command = (uint32_t)arguments->a0;
    candidate.version = (uint32_t)arguments->a1;
    if (candidate.command == MICROS_TTY_CONTROL_COMMIT) {
        candidate.service_id = (uint32_t)arguments->a2;
        candidate.endpoint = (micros_endpoint_t)arguments->a3;
        candidate.virtual_base = arguments->a4;
        candidate.physical_base = arguments->a5;
        candidate.mapped_length = (uint32_t)arguments->a6;
        candidate.irq_source = (uint32_t)(arguments->a6 >> 32);
    } else if (
        candidate.command == MICROS_TTY_CONTROL_IRQ_COMPLETE
    ) {
        if (
            arguments->a3 != 0
            || arguments->a4 != 0
            || arguments->a5 != 0
            || arguments->a6 != 0
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate.irq_source = (uint32_t)arguments->a2;
    } else {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

bool micros_tty_control_commit_tuple_matches(
    const struct micros_tty_control_request *request,
    uint32_t service_id,
    micros_endpoint_t endpoint
)
{
    return (
        request != NULL
        && request->command == MICROS_TTY_CONTROL_COMMIT
        && request->version == MICROS_TTY_CONTROL_VERSION
        && request->service_id == service_id
        && request->endpoint == endpoint
        && request->virtual_base == MICROS_TTY_UART_VIRTUAL_BASE
        && request->physical_base
            == MICROS_TTY_UART_PHYSICAL_BASE
        && request->mapped_length
            == MICROS_TTY_UART_MAPPED_LENGTH
        && request->irq_source == MICROS_TTY_UART_IRQ_SOURCE
    );
}

bool micros_tty_control_complete_tuple_matches(
    const struct micros_tty_control_request *request
)
{
    return (
        request != NULL
        && request->command == MICROS_TTY_CONTROL_IRQ_COMPLETE
        && request->version == MICROS_TTY_CONTROL_VERSION
        && request->service_id == 0
        && request->endpoint == 0
        && request->virtual_base == 0
        && request->physical_base == 0
        && request->mapped_length == 0
        && request->irq_source == MICROS_TTY_UART_IRQ_SOURCE
    );
}
