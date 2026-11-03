#include "tests/qemu/diagnostic_history_kernel.h"

#include "arch/riscv64/platform.h"

#ifdef MICROS_BUILD_PANIC_TEST
void micros_diagnostic_test_seed_panic(void)
{
    char text[2] = {0, 0};
    unsigned index;

    for (index = 0; index < 2000; ++index) {
        text[0] = index == 1999 ? '\n' : (char)('a' + index % 23);
        uart_write(text);
    }
}
#endif

#ifdef MICROS_BUILD_TTY_SERVICE_TEST
#include <stdbool.h>
#include <stddef.h>

#include "arch/riscv64/interrupt.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/panic.h"

static uint64_t diagnostic_thr_writes;

static struct {
    uint64_t registered_thr_writes;
    uint32_t cursor;
    unsigned phase;
} diagnostic_fixture;

void micros_diagnostic_test_observe_thr(void)
{
    if (diagnostic_thr_writes != UINT64_MAX) {
        ++diagnostic_thr_writes;
    }
}

static bool buffered_without_kernel_output(void)
{
    const struct micros_tty_handoff_runtime_state *state =
        micros_tty_handoff_runtime_state();

    return (
        state != NULL
        && state->service_id == 4
        && state->handoff.console_phase == MICROS_TTY_CONSOLE_OWNED
        && uart_console_handoff_is_active()
        && !riscv_irq_is_enabled()
        && diagnostic_thr_writes != UINT64_MAX
        && diagnostic_thr_writes == diagnostic_fixture.registered_thr_writes
    );
}

void micros_diagnostic_test_registered(uint32_t cursor)
{
    if (diagnostic_fixture.phase != 0 || cursor >= 10000) {
        MICROS_PANIC(0, "diagnostic-test-registration");
    }
    diagnostic_fixture.registered_thr_writes = diagnostic_thr_writes;
    diagnostic_fixture.cursor = cursor;
    diagnostic_fixture.phase = 1;
    if (!buffered_without_kernel_output()) {
        MICROS_PANIC(0, "diagnostic-test-registration");
    }
}

bool micros_diagnostic_test_before_read(uint32_t cursor)
{
    const struct micros_tty_handoff_runtime_state *state =
        micros_tty_handoff_runtime_state();
    char text[2] = {0, 0};
    unsigned index;

    if (!buffered_without_kernel_output() || diagnostic_fixture.phase == 0) {
        MICROS_PANIC(0, "diagnostic-test-reader");
    }
    if (diagnostic_fixture.phase == 3) {
        return true;
    }
    if (
        diagnostic_fixture.phase != 1 || cursor != diagnostic_fixture.cursor
        || state->handoff.deadline_armed || state->handoff.deadline != 0
    ) {
        MICROS_PANIC(0, "diagnostic-test-reader");
    }
    if (state->handoff.route_phase == MICROS_TTY_ROUTE_IN_SERVICE) {
        const struct micros_endpoint_registry *registry = micros_ipc_runtime_registry();
        const struct micros_endpoint_record *endpoint;

        if (registry == NULL || state->process.slot != MICROS_TTY_PROCESS_SLOT) {
            MICROS_PANIC(0, "diagnostic-test-deferred-irq");
        }
        endpoint = &registry->endpoints[MICROS_TTY_PROCESS_SLOT];
        if (
            state->handoff.claimed_source != 10
            || endpoint->state != MICROS_ENDPOINT_STATE_ACTIVE || endpoint->value != state->endpoint
            || endpoint->owner.slot != state->process.slot
            || endpoint->owner.generation != state->process.generation
            || endpoint->pending_kernel_events != UINT64_C(0x8)
        ) {
            MICROS_PANIC(0, "diagnostic-test-deferred-irq");
        }
        return false;
    }
    if (state->handoff.route_phase != MICROS_TTY_ROUTE_IDLE || state->handoff.claimed_source != 0) {
        MICROS_PANIC(0, "diagnostic-test-reader");
    }
    diagnostic_fixture.phase = 2;
    for (index = 0; index < 8193; ++index) {
        text[0] = (char)(1 + index % 251);
        uart_write(text);
    }
    if (!buffered_without_kernel_output()) {
        MICROS_PANIC(0, "diagnostic-test-reader");
    }
    return true;
}

void micros_diagnostic_test_after_read(void)
{
    if (
        !buffered_without_kernel_output()
        || (diagnostic_fixture.phase != 2 && diagnostic_fixture.phase != 3)
    ) {
        MICROS_PANIC(0, "diagnostic-test-copy");
    }
    if (diagnostic_fixture.phase == 2) {
        diagnostic_fixture.phase = 3;
        uart_write(
            "MICROS_DIAGNOSTIC_TEST_PASS transfer=three-pages envelope=irq-diagnostic "
            "output=tty early=drained\n"
        );
    }
}

void micros_diagnostic_test_finish(void)
{
    if (diagnostic_fixture.phase != 3 || !buffered_without_kernel_output()) {
        MICROS_PANIC(0, "diagnostic-test-final");
    }
}
#endif
