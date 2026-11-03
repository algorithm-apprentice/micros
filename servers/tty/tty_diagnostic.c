#include "servers/tty/tty_diagnostic.h"

#include "servers/tty/tty_control.h"
#include "micros/bootstrap.h"

_Static_assert(sizeof(struct micros_tty_diagnostic) == 12288, "one static three-page snapshot owner");

static uint32_t tty_diagnostic_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

bool micros_tty_diagnostic_initialize(
    struct micros_tty_diagnostic *diagnostic,
    micros_endpoint_t self_endpoint
)
{
    micros_runtime_result_t result;

    if (
        diagnostic == NULL || (uintptr_t)diagnostic % _Alignof(struct micros_tty_diagnostic) != 0
        || diagnostic->initialized || diagnostic->pending
        || diagnostic->previous != 0 || diagnostic->length != 0 || diagnostic->offset != 0
    ) {
        return false;
    }
    result = micros_tty_control_diagnostic_register(self_endpoint, diagnostic->snapshot);
    if (result < 0 || (uint64_t)result >= MICROS_DIAGNOSTIC_RING_CAPACITY) {
        return false;
    }
    diagnostic->previous = (uint32_t)result;
    diagnostic->initialized = true;
    return true;
}

static bool accept_snapshot(struct micros_tty_diagnostic *diagnostic)
{
    uint32_t next;
    uint32_t length;

    if (micros_tty_control_diagnostic_read(diagnostic->previous) != 0) {
        return false;
    }
    next = tty_diagnostic_read_u32(diagnostic->snapshot + 4);
    length = tty_diagnostic_read_u32(diagnostic->snapshot + 8);
    if (
        tty_diagnostic_read_u32(diagnostic->snapshot) != MICROS_DIAGNOSTIC_VERSION
        || tty_diagnostic_read_u32(diagnostic->snapshot + 12) != 0
        || next >= MICROS_DIAGNOSTIC_RING_CAPACITY
        || length >= MICROS_DIAGNOSTIC_RING_CAPACITY
        || (diagnostic->previous + length) % MICROS_DIAGNOSTIC_RING_CAPACITY != next
    ) {
        return false;
    }
    diagnostic->previous = next;
    diagnostic->length = length;
    diagnostic->offset = 0;
    diagnostic->pending = false;
    return true;
}

bool micros_tty_diagnostic_progress(
    struct micros_tty_diagnostic *diagnostic,
    struct micros_tty_state *terminal,
    bool notified,
    struct micros_tty_effects *effects
)
{
    uint32_t count;

    if (
        diagnostic == NULL || effects == NULL
        || (uintptr_t)diagnostic % _Alignof(struct micros_tty_diagnostic) != 0
        || !diagnostic->initialized || diagnostic->previous >= MICROS_DIAGNOSTIC_RING_CAPACITY
        || diagnostic->length >= MICROS_DIAGNOSTIC_RING_CAPACITY
        || diagnostic->offset > diagnostic->length
        || micros_tty_state_validate(terminal) != MICROS_TTY_CORE_OK
    ) {
        return false;
    }
    *effects = (struct micros_tty_effects){0};
    diagnostic->pending = diagnostic->pending || notified;
    if (
        diagnostic->pending && diagnostic->offset == diagnostic->length
        && !accept_snapshot(diagnostic)
    ) {
        return false;
    }
    if (diagnostic->offset == diagnostic->length || terminal->write_resident) {
        return true;
    }
    count = diagnostic->length - diagnostic->offset;
    if (count > MICROS_TTY_WRITE_CAPACITY) {
        count = MICROS_TTY_WRITE_CAPACITY;
    }
    if (micros_tty_write_accept(
        terminal, diagnostic->snapshot + MICROS_DIAGNOSTIC_HEADER_SIZE + diagnostic->offset, count, effects
    ) != MICROS_TTY_CORE_OK) {
        return false;
    }
    diagnostic->offset += count;
    return true;
}

bool micros_tty_diagnostic_notification(
    const struct micros_ipc_message *message,
    uint64_t *events
)
{
    uint64_t mask = 0;
    size_t index;

    if (
        message == NULL || events == NULL
        || message->source != MICROS_ENDPOINT_NONE
        || message->type != MICROS_IPC_TYPE_KERNEL_NOTIFICATION || message->reply_token != 0
    ) {
        return false;
    }
    for (index = 0; index < 8; ++index) {
        mask |= (uint64_t)message->payload[index] << (index * 8);
    }
    if (mask == 0 || (mask & ~(MICROS_KERNEL_EVENT_TTY_IRQ | MICROS_KERNEL_EVENT_DIAGNOSTIC)) != 0) {
        return false;
    }
    for (index = 8; index < sizeof(message->payload); ++index) {
        if (message->payload[index] != 0) {
            return false;
        }
    }
    *events = mask;
    return true;
}
