#include "servers/tty/tty_uart.h"

#include <stddef.h>
#include <stdint.h>

#define MICROS_TTY_UART_INITIALIZATION_MAGIC \
    UINT64_C(0x4d49435541525431)

enum {
    UART_IER_RECEIVED_DATA = 0x01,
    UART_IER_TRANSMIT_EMPTY = 0x02,
    UART_IER_LINE_STATUS = 0x04,
    UART_IIR_NO_PENDING = 0x01,
    UART_IIR_CAUSE_MASK = 0x0e,
    UART_IIR_MODEM_STATUS = 0x00,
    UART_IIR_TRANSMIT_EMPTY = 0x02,
    UART_IIR_RECEIVED_DATA = 0x04,
    UART_IIR_LINE_STATUS = 0x06,
    UART_IIR_RECEIVE_TIMEOUT = 0x0c,
    UART_FCR_ENABLE_CLEAR = 0x07,
    UART_LCR_8N1 = 0x03,
    UART_LCR_DLAB = 0x80,
    UART_MCR_DTR_RTS = 0x03,
    UART_LSR_DATA_READY = 0x01,
    UART_LSR_ERROR_MASK = 0x1e,
    UART_LSR_TRANSMIT_EMPTY = 0x20,
    UART_DRAIN_LIMIT = 256,
};

static bool bus_is_valid(const struct micros_tty_uart_bus *bus)
{
    return bus != NULL && bus->read != NULL && bus->write != NULL;
}

static uint8_t bus_read(
    const struct micros_tty_uart_bus *bus,
    uint8_t offset
)
{
    return bus->read(bus->context, offset);
}

static void bus_write(
    const struct micros_tty_uart_bus *bus,
    uint8_t offset,
    uint8_t value
)
{
    bus->write(bus->context, offset, value);
}

static enum micros_tty_uart_error discard_stale_receive(
    const struct micros_tty_uart_bus *bus
)
{
    size_t count;

    for (
        count = 0;
        count < MICROS_TTY_UART_FIFO_CAPACITY;
        ++count
    ) {
        uint8_t line_status = bus_read(
            bus,
            MICROS_TTY_UART_REGISTER_LSR
        );

        if ((line_status & UART_LSR_DATA_READY) == 0) {
            return MICROS_TTY_UART_OK;
        }
        (void)bus_read(
            bus,
            MICROS_TTY_UART_REGISTER_RBR_THR_DLL
        );
    }
    return MICROS_TTY_UART_OK;
}

static enum micros_tty_uart_error discard_stale_interrupts(
    const struct micros_tty_uart_bus *bus
)
{
    size_t iteration;

    for (iteration = 0; iteration < UART_DRAIN_LIMIT; ++iteration) {
        uint8_t interrupt = bus_read(
            bus,
            MICROS_TTY_UART_REGISTER_IIR_FCR
        );

        if ((interrupt & UART_IIR_NO_PENDING) != 0) {
            return MICROS_TTY_UART_OK;
        }
        switch (interrupt & UART_IIR_CAUSE_MASK) {
        case UART_IIR_LINE_STATUS: {
            uint8_t line_status = bus_read(
                bus,
                MICROS_TTY_UART_REGISTER_LSR
            );

            if ((line_status & UART_LSR_DATA_READY) != 0) {
                (void)bus_read(
                    bus,
                    MICROS_TTY_UART_REGISTER_RBR_THR_DLL
                );
            }
            break;
        }
        case UART_IIR_RECEIVED_DATA:
        case UART_IIR_RECEIVE_TIMEOUT:
            if (
                discard_stale_receive(bus)
                    != MICROS_TTY_UART_OK
            ) {
                return MICROS_TTY_UART_ERROR_INVARIANT;
            }
            break;
        case UART_IIR_MODEM_STATUS:
            (void)bus_read(
                bus,
                MICROS_TTY_UART_REGISTER_MSR
            );
            break;
        case UART_IIR_TRANSMIT_EMPTY:
        default:
            return MICROS_TTY_UART_ERROR_INVARIANT;
        }
    }
    return MICROS_TTY_UART_ERROR_INVARIANT;
}

enum micros_tty_uart_error micros_tty_uart_validate(
    const struct micros_tty_uart_state *uart,
    const struct micros_tty_state *terminal
)
{
    uint8_t expected;

    if (uart == NULL || terminal == NULL) {
        return MICROS_TTY_UART_ERROR_ARGUMENT;
    }
    if (
        uart->initialization_magic
            != MICROS_TTY_UART_INITIALIZATION_MAGIC
        || micros_tty_state_validate(terminal)
            != MICROS_TTY_CORE_OK
    ) {
        return MICROS_TTY_UART_ERROR_INVARIANT;
    }
    expected = UART_IER_RECEIVED_DATA | UART_IER_LINE_STATUS;
    if (terminal->transmit_interrupt_enabled) {
        expected |= UART_IER_TRANSMIT_EMPTY;
    }
    return uart->interrupt_enable == expected
        ? MICROS_TTY_UART_OK
        : MICROS_TTY_UART_ERROR_INVARIANT;
}

enum micros_tty_uart_error micros_tty_uart_initialize(
    struct micros_tty_uart_state *uart,
    const struct micros_tty_uart_bus *bus,
    const struct micros_tty_state *terminal
)
{
    if (
        uart == NULL
        || !bus_is_valid(bus)
        || terminal == NULL
        || micros_tty_state_validate(terminal)
            != MICROS_TTY_CORE_OK
        || micros_tty_output_pending(terminal)
        || terminal->transmit_interrupt_enabled
    ) {
        return MICROS_TTY_UART_ERROR_ARGUMENT;
    }
    uart->initialization_magic = 0;
    uart->interrupt_enable = 0;
    bus_write(bus, MICROS_TTY_UART_REGISTER_IER_DLM, 0);
    bus_write(
        bus,
        MICROS_TTY_UART_REGISTER_LCR,
        UART_LCR_DLAB
    );
    bus_write(bus, MICROS_TTY_UART_REGISTER_RBR_THR_DLL, 2);
    bus_write(bus, MICROS_TTY_UART_REGISTER_IER_DLM, 0);
    bus_write(
        bus,
        MICROS_TTY_UART_REGISTER_LCR,
        UART_LCR_8N1
    );
    bus_write(
        bus,
        MICROS_TTY_UART_REGISTER_IIR_FCR,
        UART_FCR_ENABLE_CLEAR
    );
    bus_write(
        bus,
        MICROS_TTY_UART_REGISTER_MCR,
        UART_MCR_DTR_RTS
    );
    uart->interrupt_enable =
        UART_IER_RECEIVED_DATA | UART_IER_LINE_STATUS;
    bus_write(
        bus,
        MICROS_TTY_UART_REGISTER_IER_DLM,
        uart->interrupt_enable
    );
    if (
        discard_stale_interrupts(bus)
            != MICROS_TTY_UART_OK
    ) {
        return MICROS_TTY_UART_ERROR_INVARIANT;
    }
    uart->initialization_magic =
        MICROS_TTY_UART_INITIALIZATION_MAGIC;
    return micros_tty_uart_validate(uart, terminal);
}

enum micros_tty_uart_error micros_tty_uart_apply_effects(
    struct micros_tty_uart_state *uart,
    const struct micros_tty_uart_bus *bus,
    const struct micros_tty_state *terminal,
    const struct micros_tty_effects *effects
)
{
    if (
        uart == NULL
        || !bus_is_valid(bus)
        || terminal == NULL
        || effects == NULL
        || (effects->enable_transmit && effects->disable_transmit)
        || effects->writable
    ) {
        return MICROS_TTY_UART_ERROR_ARGUMENT;
    }
    if (
        uart->initialization_magic
            != MICROS_TTY_UART_INITIALIZATION_MAGIC
        || micros_tty_state_validate(terminal)
            != MICROS_TTY_CORE_OK
        || (
            uart->interrupt_enable
            & (
                UART_IER_RECEIVED_DATA
                | UART_IER_LINE_STATUS
            )
        ) != (
            UART_IER_RECEIVED_DATA
            | UART_IER_LINE_STATUS
        )
        || (
            uart->interrupt_enable
            & ~(
                UART_IER_RECEIVED_DATA
                | UART_IER_TRANSMIT_EMPTY
                | UART_IER_LINE_STATUS
            )
        ) != 0
    ) {
        return MICROS_TTY_UART_ERROR_INVARIANT;
    }
    if (effects->enable_transmit) {
        if (
            !terminal->transmit_interrupt_enabled
            || (
                uart->interrupt_enable
                & UART_IER_TRANSMIT_EMPTY
            ) != 0
        ) {
            return MICROS_TTY_UART_ERROR_INVARIANT;
        }
        uart->interrupt_enable |= UART_IER_TRANSMIT_EMPTY;
        bus_write(
            bus,
            MICROS_TTY_UART_REGISTER_IER_DLM,
            uart->interrupt_enable
        );
    } else if (effects->disable_transmit) {
        if (
            terminal->transmit_interrupt_enabled
            || (
                uart->interrupt_enable
                & UART_IER_TRANSMIT_EMPTY
            ) == 0
        ) {
            return MICROS_TTY_UART_ERROR_INVARIANT;
        }
        uart->interrupt_enable &=
            (uint8_t)~UART_IER_TRANSMIT_EMPTY;
        bus_write(
            bus,
            MICROS_TTY_UART_REGISTER_IER_DLM,
            uart->interrupt_enable
        );
    }
    return micros_tty_uart_validate(uart, terminal);
}

static enum micros_tty_uart_error receive_available(
    struct micros_tty_uart_state *uart,
    const struct micros_tty_uart_bus *bus,
    struct micros_tty_state *terminal
)
{
    bool observed = false;

    for (;;) {
        struct micros_tty_effects effects;
        uint8_t line_status = bus_read(
            bus,
            MICROS_TTY_UART_REGISTER_LSR
        );
        uint8_t byte;

        if ((line_status & UART_LSR_DATA_READY) == 0) {
            return observed
                ? MICROS_TTY_UART_OK
                : MICROS_TTY_UART_ERROR_INVARIANT;
        }
        observed = true;
        byte = bus_read(
            bus,
            MICROS_TTY_UART_REGISTER_RBR_THR_DLL
        );
        if ((line_status & UART_LSR_ERROR_MASK) != 0) {
            if (
                micros_tty_record_receive_error(terminal)
                    != MICROS_TTY_CORE_OK
            ) {
                return MICROS_TTY_UART_ERROR_INVARIANT;
            }
        } else {
            if (
                micros_tty_receive_byte(
                    terminal,
                    byte,
                    &effects
                ) != MICROS_TTY_CORE_OK
                || micros_tty_uart_apply_effects(
                    uart,
                    bus,
                    terminal,
                    &effects
                ) != MICROS_TTY_UART_OK
            ) {
                return MICROS_TTY_UART_ERROR_INVARIANT;
            }
        }
    }
}

static enum micros_tty_uart_error handle_line_status(
    const struct micros_tty_uart_bus *bus,
    struct micros_tty_state *terminal
)
{
    uint8_t line_status = bus_read(
        bus,
        MICROS_TTY_UART_REGISTER_LSR
    );

    if ((line_status & UART_LSR_ERROR_MASK) == 0) {
        return MICROS_TTY_UART_ERROR_INVARIANT;
    }
    if (
        micros_tty_record_receive_error(terminal)
            != MICROS_TTY_CORE_OK
    ) {
        return MICROS_TTY_UART_ERROR_INVARIANT;
    }
    if ((line_status & UART_LSR_DATA_READY) != 0) {
        (void)bus_read(
            bus,
            MICROS_TTY_UART_REGISTER_RBR_THR_DLL
        );
    }
    return MICROS_TTY_UART_OK;
}

static enum micros_tty_uart_error transmit_available(
    struct micros_tty_uart_state *uart,
    const struct micros_tty_uart_bus *bus,
    struct micros_tty_state *terminal,
    struct micros_tty_uart_drain_effects *drain_effects
)
{
    uint8_t line_status = bus_read(
        bus,
        MICROS_TTY_UART_REGISTER_LSR
    );
    size_t count;

    if (
        (line_status & UART_LSR_TRANSMIT_EMPTY) == 0
        || !micros_tty_output_pending(terminal)
    ) {
        return MICROS_TTY_UART_ERROR_INVARIANT;
    }
    for (
        count = 0;
        count < MICROS_TTY_UART_FIFO_CAPACITY
            && micros_tty_output_pending(terminal);
        ++count
    ) {
        struct micros_tty_effects effects;
        uint8_t byte;

        if (
            micros_tty_output_take(
                terminal,
                &byte,
                &effects
            ) != MICROS_TTY_CORE_OK
        ) {
            return MICROS_TTY_UART_ERROR_INVARIANT;
        }
        bus_write(
            bus,
            MICROS_TTY_UART_REGISTER_RBR_THR_DLL,
            byte
        );
        if (effects.writable) {
            drain_effects->writable = true;
        }
        effects.writable = false;
        if (
            micros_tty_uart_apply_effects(
                uart,
                bus,
                terminal,
                &effects
            ) != MICROS_TTY_UART_OK
        ) {
            return MICROS_TTY_UART_ERROR_INVARIANT;
        }
    }
    return MICROS_TTY_UART_OK;
}

enum micros_tty_uart_error micros_tty_uart_drain(
    struct micros_tty_uart_state *uart,
    const struct micros_tty_uart_bus *bus,
    struct micros_tty_state *terminal,
    struct micros_tty_uart_drain_effects *effects
)
{
    struct micros_tty_uart_drain_effects committed = {0};
    size_t iteration;

    if (
        uart == NULL
        || !bus_is_valid(bus)
        || terminal == NULL
        || effects == NULL
    ) {
        return MICROS_TTY_UART_ERROR_ARGUMENT;
    }
    if (
        micros_tty_uart_validate(uart, terminal)
            != MICROS_TTY_UART_OK
    ) {
        return MICROS_TTY_UART_ERROR_INVARIANT;
    }
    for (iteration = 0; iteration < UART_DRAIN_LIMIT; ++iteration) {
        uint8_t interrupt = bus_read(
            bus,
            MICROS_TTY_UART_REGISTER_IIR_FCR
        );
        enum micros_tty_uart_error error;

        if ((interrupt & UART_IIR_NO_PENDING) != 0) {
            *effects = committed;
            return micros_tty_uart_validate(uart, terminal);
        }
        switch (interrupt & UART_IIR_CAUSE_MASK) {
        case UART_IIR_LINE_STATUS:
            error = handle_line_status(bus, terminal);
            break;
        case UART_IIR_RECEIVED_DATA:
        case UART_IIR_RECEIVE_TIMEOUT:
            error = receive_available(uart, bus, terminal);
            break;
        case UART_IIR_TRANSMIT_EMPTY:
            error = transmit_available(
                uart,
                bus,
                terminal,
                &committed
            );
            break;
        case UART_IIR_MODEM_STATUS:
            (void)bus_read(
                bus,
                MICROS_TTY_UART_REGISTER_MSR
            );
            error = MICROS_TTY_UART_OK;
            break;
        default:
            return MICROS_TTY_UART_ERROR_INVARIANT;
        }
        if (error != MICROS_TTY_UART_OK) {
            return error;
        }
    }
    return MICROS_TTY_UART_ERROR_INVARIANT;
}
