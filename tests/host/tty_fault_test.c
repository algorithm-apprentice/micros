#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "arch/riscv64/platform.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/tty_fault.h"
#include "kernel/tty_handoff_runtime.h"
#include "micros/panic.h"

static struct micros_bootstrap_control_state bootstrap;
static struct micros_tty_handoff_runtime_state tty_state;
static char output[512];
static size_t output_length;
static unsigned int seizure_count;
static bool order_violation;

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
            return 1; \
        } \
    } while (false)

static void append_character(char character)
{
    if (output_length + 1 >= sizeof(output)) {
        order_violation = true;
        return;
    }
    output[output_length] = character;
    output_length += 1;
    output[output_length] = '\0';
}

int main(void)
{
    static const char expected[] =
        "MICROS_TTY_OWNER_FAULT "
        "service=0x0000000000000004 "
        "process-slot=0x0000000000000003 "
        "process-generation=0x0000000000000007 "
        "endpoint=0x0000000000007003 "
        "console=owned route=in-service "
        "source=0x000000000000000a\n";

    bootstrap.phase = MICROS_BOOTSTRAP_PHASE_RUNNING;
    tty_state = (struct micros_tty_handoff_runtime_state){
        .service_id = MICROS_TTY_SERVICE_ID,
        .endpoint = UINT32_C(0x00007003),
        .process = {
            .slot = MICROS_TTY_PROCESS_SLOT,
            .generation = 7,
        },
    };
    tty_state.handoff = (struct micros_tty_handoff){
        .console_phase = MICROS_TTY_CONSOLE_OWNED,
        .route_phase = MICROS_TTY_ROUTE_IN_SERVICE,
        .claimed_source = MICROS_TTY_UART_IRQ_SOURCE,
    };
    micros_tty_owner_fault_record();
    EXPECT_TRUE(output_length == 0 && seizure_count == 0);

    bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    micros_tty_owner_fault_record();
    EXPECT_TRUE(
        seizure_count == 1
        && tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_PANIC
        && tty_state.handoff.route_phase == MICROS_TTY_ROUTE_PANIC
        && strcmp(output, expected) == 0
        && !order_violation
    );

    micros_tty_owner_fault_record();
    EXPECT_TRUE(
        seizure_count == 1
        && strcmp(output, expected) == 0
        && !order_violation
    );
    return 0;
}

const struct micros_bootstrap_control_state *
micros_bootstrap_runtime_state(void)
{
    return &bootstrap;
}

const struct micros_tty_handoff_runtime_state *
micros_tty_handoff_runtime_state(void)
{
    return &tty_state;
}

void micros_panic_seize(void)
{
    seizure_count += 1;
    tty_state.handoff.console_phase = MICROS_TTY_CONSOLE_PANIC;
    tty_state.handoff.route_phase = MICROS_TTY_ROUTE_PANIC;
    tty_state.handoff.claimed_source = 0;
}

void uart_write(const char *text)
{
    if (seizure_count == 0 || text == NULL) {
        order_violation = true;
        return;
    }
    while (*text != '\0') {
        append_character(*text);
        text += 1;
    }
}

void uart_write_hex64(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    uart_write("0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        append_character(
            digits[(value >> (unsigned int)shift) & UINT64_C(0xf)]
        );
    }
}

void uart_flush(void)
{
    if (seizure_count == 0) {
        order_violation = true;
    }
}
