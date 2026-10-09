#include "kernel/tty_fault.h"

#include <stdbool.h>

#include "arch/riscv64/platform.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/tty_handoff_runtime.h"
#include "micros/panic.h"

static bool owner_fault_emitted;

static const char *console_phase_name(
    enum micros_tty_console_phase phase
)
{
    switch (phase) {
    case MICROS_TTY_CONSOLE_EARLY:
        return "early";
    case MICROS_TTY_CONSOLE_MAP_REQUESTED:
        return "map-requested";
    case MICROS_TTY_CONSOLE_MAPPED:
        return "mapped";
    case MICROS_TTY_CONSOLE_STARTING:
        return "starting";
    case MICROS_TTY_CONSOLE_OWNED:
        return "owned";
    case MICROS_TTY_CONSOLE_PANIC:
        return "panic";
    }
    return "invalid";
}

static const char *route_phase_name(
    enum micros_tty_route_phase phase
)
{
    switch (phase) {
    case MICROS_TTY_ROUTE_DISABLED:
        return "disabled";
    case MICROS_TTY_ROUTE_IDLE:
        return "idle";
    case MICROS_TTY_ROUTE_IN_SERVICE:
        return "in-service";
    case MICROS_TTY_ROUTE_PANIC:
        return "panic";
    }
    return "invalid";
}

void micros_tty_owner_fault_record(void)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();
    const struct micros_tty_handoff_runtime_state *state =
        micros_tty_handoff_runtime_state();
    struct micros_tty_handoff snapshot;

    if (
        owner_fault_emitted
        || bootstrap == NULL
        || bootstrap->phase != MICROS_BOOTSTRAP_PHASE_SEALED
        || state == NULL
        || state->handoff.console_phase == MICROS_TTY_CONSOLE_EARLY
    ) {
        return;
    }
    snapshot = state->handoff;
    micros_panic_seize();
    uart_write("MICROS_TTY_OWNER_FAULT service=");
    uart_write_hex64(state->service_id);
    uart_write(" process-slot=");
    uart_write_hex64(state->process.slot);
    uart_write(" process-generation=");
    uart_write_hex64(state->process.generation);
    uart_write(" endpoint=");
    uart_write_hex64(state->endpoint);
    uart_write(" console=");
    uart_write(console_phase_name(snapshot.console_phase));
    uart_write(" route=");
    uart_write(route_phase_name(snapshot.route_phase));
    uart_write(" source=");
    uart_write_hex64(snapshot.claimed_source);
    uart_write("\n");
    uart_flush();
    owner_fault_emitted = true;
}
