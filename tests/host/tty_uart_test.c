#include "servers/tty/tty_uart.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum {
    BUS_CAPACITY = 64,
};

struct bus_read {
    uint8_t offset;
    uint8_t value;
};

struct bus_write {
    uint8_t offset;
    uint8_t value;
};

struct bus_fixture {
    struct bus_read reads[BUS_CAPACITY];
    size_t read_count;
    size_t read_cursor;
    struct bus_write writes[BUS_CAPACITY];
    size_t write_count;
    bool failed;
};

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

static uint8_t read_register(void *context, uint8_t offset)
{
    struct bus_fixture *fixture = context;

    if (
        fixture == NULL
        || fixture->read_cursor >= fixture->read_count
        || fixture->reads[fixture->read_cursor].offset != offset
    ) {
        if (fixture != NULL) {
            fixture->failed = true;
        }
        return 0xff;
    }
    return fixture->reads[fixture->read_cursor++].value;
}

static void write_register(
    void *context,
    uint8_t offset,
    uint8_t value
)
{
    struct bus_fixture *fixture = context;

    if (fixture == NULL || fixture->write_count >= BUS_CAPACITY) {
        if (fixture != NULL) {
            fixture->failed = true;
        }
        return;
    }
    fixture->writes[fixture->write_count++] =
        (struct bus_write){
            .offset = offset,
            .value = value,
        };
}

static struct micros_tty_uart_bus fixture_bus(
    struct bus_fixture *fixture
)
{
    return (struct micros_tty_uart_bus){
        .read = read_register,
        .write = write_register,
        .context = fixture,
    };
}

static bool initialize_uart(
    struct micros_tty_state *terminal,
    struct micros_tty_uart_state *uart,
    struct bus_fixture *fixture
)
{
    struct micros_tty_uart_bus bus = fixture_bus(fixture);

    fixture->reads[fixture->read_count++] =
        (struct bus_read){2, 0x01};
    return (
        micros_tty_state_initialize(terminal)
            == MICROS_TTY_CORE_OK
        && micros_tty_uart_initialize(uart, &bus, terminal)
            == MICROS_TTY_UART_OK
        && !fixture->failed
        && fixture->write_count == 8
        && fixture->writes[0].offset
            == MICROS_TTY_UART_REGISTER_IER_DLM
        && fixture->writes[0].value == 0
        && fixture->writes[1].offset
            == MICROS_TTY_UART_REGISTER_LCR
        && fixture->writes[1].value == 0x80
        && fixture->writes[2].offset
            == MICROS_TTY_UART_REGISTER_RBR_THR_DLL
        && fixture->writes[2].value == 2
        && fixture->writes[3].offset
            == MICROS_TTY_UART_REGISTER_IER_DLM
        && fixture->writes[3].value == 0
        && fixture->writes[4].offset
            == MICROS_TTY_UART_REGISTER_LCR
        && fixture->writes[4].value == 0x03
        && fixture->writes[5].offset
            == MICROS_TTY_UART_REGISTER_IIR_FCR
        && fixture->writes[5].value == 0x07
        && fixture->writes[6].offset
            == MICROS_TTY_UART_REGISTER_MCR
        && fixture->writes[6].value == 0x03
        && fixture->writes[7].offset
            == MICROS_TTY_UART_REGISTER_IER_DLM
        && fixture->writes[7].value == 0x05
        && fixture->read_cursor == fixture->read_count
    );
}

static bool test_initialization_drains_stale_line_status(void)
{
    struct micros_tty_state terminal;
    struct micros_tty_uart_state uart;
    struct bus_fixture fixture = {0};
    struct micros_tty_uart_bus bus = fixture_bus(&fixture);

    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x06};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){5, 0x03};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){0, 'z'};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x01};
    EXPECT_TRUE(
        micros_tty_state_initialize(&terminal)
            == MICROS_TTY_CORE_OK
        && micros_tty_uart_initialize(
            &uart,
            &bus,
            &terminal
        ) == MICROS_TTY_UART_OK
        && !fixture.failed
        && fixture.read_cursor == fixture.read_count
        && fixture.write_count == 8
        && terminal.receive_error_count == 0
        && terminal.input_count == 0
    );
    return true;
}

static bool test_initialization_discards_stale_receive(void)
{
    struct micros_tty_state terminal;
    struct micros_tty_uart_state uart;
    struct bus_fixture fixture = {0};
    struct micros_tty_uart_bus bus = fixture_bus(&fixture);

    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x04};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){5, 0x01};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){0, 'a'};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){5, 0x00};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x01};
    EXPECT_TRUE(
        micros_tty_state_initialize(&terminal)
            == MICROS_TTY_CORE_OK
        && micros_tty_uart_initialize(
            &uart,
            &bus,
            &terminal
        ) == MICROS_TTY_UART_OK
        && !fixture.failed
        && fixture.read_cursor == fixture.read_count
        && fixture.write_count == 8
        && terminal.receive_error_count == 0
        && terminal.input_count == 0
        && !micros_tty_output_pending(&terminal)
    );
    return true;
}

static bool test_receive_and_echo_drain(void)
{
    struct micros_tty_state terminal;
    struct micros_tty_uart_state uart;
    struct bus_fixture fixture = {0};
    struct micros_tty_uart_bus bus = fixture_bus(&fixture);
    struct micros_tty_uart_drain_effects effects;

    EXPECT_TRUE(initialize_uart(&terminal, &uart, &fixture));
    fixture.write_count = 0;
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x04};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){5, 0x01};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){0, 'x'};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){5, 0x00};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x02};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){5, 0x20};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x01};
    EXPECT_TRUE(
        micros_tty_uart_drain(
            &uart,
            &bus,
            &terminal,
            &effects
        ) == MICROS_TTY_UART_OK
        && !effects.writable
        && !fixture.failed
        && fixture.read_cursor == fixture.read_count
        && fixture.write_count == 3
        && fixture.writes[0].offset == 1
        && fixture.writes[0].value == 0x07
        && fixture.writes[1].offset == 0
        && fixture.writes[1].value == 'x'
        && fixture.writes[2].offset == 1
        && fixture.writes[2].value == 0x05
        && terminal.input_count == 1
        && terminal.input[terminal.input_head].byte == 'x'
        && !micros_tty_output_pending(&terminal)
    );
    return true;
}

static bool test_receive_error_is_discarded(void)
{
    struct micros_tty_state terminal;
    struct micros_tty_uart_state uart;
    struct bus_fixture fixture = {0};
    struct micros_tty_uart_bus bus = fixture_bus(&fixture);
    struct micros_tty_uart_drain_effects effects;

    EXPECT_TRUE(initialize_uart(&terminal, &uart, &fixture));
    fixture.write_count = 0;
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x06};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){5, 0x03};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){0, 'z'};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x01};
    EXPECT_TRUE(
        micros_tty_uart_drain(
            &uart,
            &bus,
            &terminal,
            &effects
        ) == MICROS_TTY_UART_OK
        && terminal.receive_error_count == 1
        && terminal.input_count == 0
        && fixture.write_count == 0
    );
    return true;
}

static bool test_receive_timeout_drains_all_bytes(void)
{
    struct micros_tty_state terminal;
    struct micros_tty_uart_state uart;
    struct bus_fixture fixture = {0};
    struct micros_tty_uart_bus bus = fixture_bus(&fixture);
    struct micros_tty_uart_drain_effects effects;

    EXPECT_TRUE(initialize_uart(&terminal, &uart, &fixture));
    fixture.write_count = 0;
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x0c};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){5, 0x01};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){0, 'a'};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){5, 0x01};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){0, 'b'};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){5, 0x00};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x01};
    EXPECT_TRUE(
        micros_tty_uart_drain(
            &uart,
            &bus,
            &terminal,
            &effects
        ) == MICROS_TTY_UART_OK
        && !effects.writable
        && !fixture.failed
        && fixture.read_cursor == fixture.read_count
        && fixture.write_count == 1
        && fixture.writes[0].offset == 1
        && fixture.writes[0].value == 0x07
        && terminal.input_count == 2
        && terminal.echo_count == 2
        && terminal.transmit_interrupt_enabled
    );
    return true;
}

static bool test_modem_status_is_acknowledged(void)
{
    struct micros_tty_state terminal;
    struct micros_tty_uart_state uart;
    struct bus_fixture fixture = {0};
    struct micros_tty_uart_bus bus = fixture_bus(&fixture);
    struct micros_tty_uart_drain_effects effects;

    EXPECT_TRUE(initialize_uart(&terminal, &uart, &fixture));
    fixture.write_count = 0;
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x00};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){6, 0x90};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x01};
    EXPECT_TRUE(
        micros_tty_uart_drain(
            &uart,
            &bus,
            &terminal,
            &effects
        ) == MICROS_TTY_UART_OK
        && !effects.writable
        && !fixture.failed
        && fixture.read_cursor == fixture.read_count
        && fixture.write_count == 0
    );
    return true;
}

static bool test_write_drain_reports_writable(void)
{
    static const uint8_t bytes[] = {'a', '\n'};
    struct micros_tty_state terminal;
    struct micros_tty_uart_state uart;
    struct bus_fixture fixture = {0};
    struct micros_tty_uart_bus bus = fixture_bus(&fixture);
    struct micros_tty_uart_drain_effects drain_effects;
    struct micros_tty_effects effects;

    EXPECT_TRUE(initialize_uart(&terminal, &uart, &fixture));
    fixture.write_count = 0;
    EXPECT_TRUE(
        micros_tty_write_accept(
            &terminal,
            bytes,
            sizeof(bytes),
            &effects
        ) == MICROS_TTY_CORE_OK
        && micros_tty_uart_apply_effects(
            &uart,
            &bus,
            &terminal,
            &effects
        ) == MICROS_TTY_UART_OK
        && micros_tty_write_arm_retry(&terminal)
            == MICROS_TTY_CORE_OK
    );
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x02};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){5, 0x20};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x01};
    EXPECT_TRUE(
        micros_tty_uart_drain(
            &uart,
            &bus,
            &terminal,
            &drain_effects
        ) == MICROS_TTY_UART_OK
        && drain_effects.writable
        && fixture.write_count == 5
        && fixture.writes[0].offset == 1
        && fixture.writes[0].value == 0x07
        && fixture.writes[1].offset == 0
        && fixture.writes[1].value == 'a'
        && fixture.writes[2].offset == 0
        && fixture.writes[2].value == '\r'
        && fixture.writes[3].offset == 0
        && fixture.writes[3].value == '\n'
        && fixture.writes[4].offset == 1
        && fixture.writes[4].value == 0x05
        && !terminal.write_resident
    );
    return true;
}

static bool test_transmit_fill_stops_at_fifo_capacity(void)
{
    uint8_t bytes[MICROS_TTY_UART_FIFO_CAPACITY + 1];
    struct micros_tty_state terminal;
    struct micros_tty_uart_state uart;
    struct bus_fixture fixture = {0};
    struct micros_tty_uart_bus bus = fixture_bus(&fixture);
    struct micros_tty_uart_drain_effects drain_effects;
    struct micros_tty_effects effects;
    size_t index;

    for (index = 0; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)('a' + index);
    }
    EXPECT_TRUE(initialize_uart(&terminal, &uart, &fixture));
    fixture.write_count = 0;
    EXPECT_TRUE(
        micros_tty_write_accept(
            &terminal,
            bytes,
            sizeof(bytes),
            &effects
        ) == MICROS_TTY_CORE_OK
        && micros_tty_uart_apply_effects(
            &uart,
            &bus,
            &terminal,
            &effects
        ) == MICROS_TTY_UART_OK
    );
    fixture.write_count = 0;
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x02};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){5, 0x20};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x01};
    EXPECT_TRUE(
        micros_tty_uart_drain(
            &uart,
            &bus,
            &terminal,
            &drain_effects
        ) == MICROS_TTY_UART_OK
        && !drain_effects.writable
        && fixture.write_count == MICROS_TTY_UART_FIFO_CAPACITY
        && terminal.write_cursor
            == MICROS_TTY_UART_FIFO_CAPACITY
        && terminal.write_resident
        && terminal.transmit_interrupt_enabled
    );
    for (
        index = 0;
        index < MICROS_TTY_UART_FIFO_CAPACITY;
        ++index
    ) {
        EXPECT_TRUE(
            fixture.writes[index].offset == 0
            && fixture.writes[index].value == bytes[index]
        );
    }
    return true;
}

static bool test_pending_lf_precedes_echo_at_fifo_boundary(void)
{
    uint8_t bytes[MICROS_TTY_UART_FIFO_CAPACITY];
    struct micros_tty_state terminal;
    struct micros_tty_uart_state uart;
    struct bus_fixture fixture = {0};
    struct micros_tty_uart_bus bus = fixture_bus(&fixture);
    struct micros_tty_uart_drain_effects drain_effects;
    struct micros_tty_effects effects;
    size_t index;

    for (index = 0; index + 1 < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)('a' + index);
    }
    bytes[sizeof(bytes) - 1] = '\n';
    EXPECT_TRUE(initialize_uart(&terminal, &uart, &fixture));
    fixture.write_count = 0;
    EXPECT_TRUE(
        micros_tty_write_accept(
            &terminal,
            bytes,
            sizeof(bytes),
            &effects
        ) == MICROS_TTY_CORE_OK
        && micros_tty_uart_apply_effects(
            &uart,
            &bus,
            &terminal,
            &effects
        ) == MICROS_TTY_UART_OK
    );
    fixture.write_count = 0;
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x02};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){5, 0x20};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x01};
    EXPECT_TRUE(
        micros_tty_uart_drain(
            &uart,
            &bus,
            &terminal,
            &drain_effects
        ) == MICROS_TTY_UART_OK
        && fixture.write_count == MICROS_TTY_UART_FIFO_CAPACITY
        && fixture.writes[MICROS_TTY_UART_FIFO_CAPACITY - 1].value
            == '\r'
        && terminal.write_pending_lf
        && terminal.write_resident
    );
    EXPECT_TRUE(
        micros_tty_receive_byte(&terminal, 'x', &effects)
            == MICROS_TTY_CORE_OK
        && !effects.enable_transmit
    );
    fixture.write_count = 0;
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x02};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){5, 0x20};
    fixture.reads[fixture.read_count++] =
        (struct bus_read){2, 0x01};
    EXPECT_TRUE(
        micros_tty_uart_drain(
            &uart,
            &bus,
            &terminal,
            &drain_effects
        ) == MICROS_TTY_UART_OK
        && fixture.write_count == 3
        && fixture.writes[0].offset == 0
        && fixture.writes[0].value == '\n'
        && fixture.writes[1].offset == 0
        && fixture.writes[1].value == 'x'
        && fixture.writes[2].offset == 1
        && fixture.writes[2].value == 0x05
        && !micros_tty_output_pending(&terminal)
    );
    return true;
}

static bool test_corrupt_uart_state_has_no_mmio_effect(void)
{
    static const uint8_t byte = 'q';
    struct micros_tty_state terminal;
    struct micros_tty_uart_state uart;
    struct bus_fixture fixture = {0};
    struct micros_tty_uart_bus bus = fixture_bus(&fixture);
    struct micros_tty_effects effects;

    EXPECT_TRUE(
        initialize_uart(&terminal, &uart, &fixture)
        && micros_tty_write_accept(
            &terminal,
            &byte,
            1,
            &effects
        ) == MICROS_TTY_CORE_OK
    );
    fixture.write_count = 0;
    uart.initialization_magic = 0;
    EXPECT_TRUE(
        micros_tty_uart_apply_effects(
            &uart,
            &bus,
            &terminal,
            &effects
        ) == MICROS_TTY_UART_ERROR_INVARIANT
        && fixture.write_count == 0
    );
    return true;
}

int main(void)
{
    return (
        test_initialization_drains_stale_line_status()
        && test_initialization_discards_stale_receive()
        && test_receive_and_echo_drain()
        && test_receive_error_is_discarded()
        && test_receive_timeout_drains_all_bytes()
        && test_modem_status_is_acknowledged()
        && test_write_drain_reports_writable()
        && test_transmit_fill_stops_at_fifo_capacity()
        && test_pending_lf_precedes_echo_at_fifo_boundary()
        && test_corrupt_uart_state_has_no_mmio_effect()
    ) ? 0 : 1;
}
