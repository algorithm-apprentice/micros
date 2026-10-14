#include "kernel/bootstrap_runtime_internal.h"

#include "arch/riscv64/platform.h"
#include "kernel/tty_handoff_runtime.h"
#include "micros/panic.h"

static const char *diagnostic_reason_name(
    enum micros_bootstrap_diagnostic_reason reason
)
{
    switch (reason) {
    case MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_HEADER:
        return "manifest-header";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY:
        return "manifest-entry";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_PROFILE:
        return "manifest-profile";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_IMAGE:
        return "manifest-image";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_CYCLE:
        return "manifest-cycle";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE:
        return "prepare";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_ORDER:
        return "release-order";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION:
        return "release-transition";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_READY_MALFORMED:
        return "ready-malformed";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_READY_FOREIGN:
        return "ready-foreign";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_READY_EARLY:
        return "ready-early";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_READY_DUPLICATE:
        return "ready-duplicate";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE:
        return "ready-role-gate";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_READY_TIMEOUT:
        return "ready-timeout";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT:
        return "service-fault";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY:
        return "authority";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION:
        return "completion";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_CONSOLE_MAP_GATE:
        return "console-map-gate";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_CONSOLE_PROTOCOL:
        return "console-protocol";
    }
    return "authority";
}

static const char *diagnostic_phase_name(
    enum micros_bootstrap_phase phase
)
{
    switch (phase) {
    case MICROS_BOOTSTRAP_PHASE_UNINITIALIZED:
        return "uninitialized";
    case MICROS_BOOTSTRAP_PHASE_PREPARING:
        return "preparing";
    case MICROS_BOOTSTRAP_PHASE_RUNNING:
        return "running";
    case MICROS_BOOTSTRAP_PHASE_SEALED:
        return "sealed";
    case MICROS_BOOTSTRAP_PHASE_FAILED:
        return "failed";
    }
    return "failed";
}

static const char *diagnostic_state_name(
    const struct bootstrap_runtime_context *runtime,
    uint32_t service_id
)
{
    const struct micros_bootstrap_binding *binding;
    size_t index;

    if (service_id == 0) {
        return "none";
    }
    binding = micros_bootstrap_control_find_binding(
        &runtime->bootstrap_state,
        service_id
    );
    if (binding == NULL) {
        return "none";
    }
    if (
        runtime->bootstrap_state.plan.controller_service_id == service_id
    ) {
        return "controller";
    }
    for (
        index = 0;
        index < runtime->bootstrap_state.transitions.entry_count;
        ++index
    ) {
        const struct micros_bootstrap_runtime_entry *entry =
            &runtime->bootstrap_state.transitions.entries[index];

        if (entry->service_id != service_id) {
            continue;
        }
        switch (entry->state) {
        case MICROS_BOOTSTRAP_SERVICE_PREPARED:
            return "prepared";
        case MICROS_BOOTSTRAP_SERVICE_STARTING:
            return "starting";
        case MICROS_BOOTSTRAP_SERVICE_READY:
            return "ready";
        }
    }
    return "none";
}

void micros_bootstrap_runtime_record_failure(
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t detail
)
{
    struct bootstrap_runtime_context *runtime = bootstrap_runtime_context();
    const char *state_name = diagnostic_state_name(runtime, service_id);

    if (!runtime->failure_record_emitted) {
        runtime->bootstrap_state.phase = MICROS_BOOTSTRAP_PHASE_FAILED;
        runtime->bootstrap_state.transitions.phase =
            MICROS_BOOTSTRAP_PHASE_FAILED;
        micros_panic_seize();
        uart_write("MICROS_BOOTSTRAP_FAILURE reason=");
        uart_write(diagnostic_reason_name(reason));
        uart_write(" service=");
        uart_write_hex64(service_id);
        uart_write(" endpoint=");
        uart_write_hex64(endpoint);
        uart_write(" phase=");
        uart_write(diagnostic_phase_name(runtime->bootstrap_state.phase));
        uart_write(" state=");
        uart_write(state_name);
        uart_write(" detail=");
        uart_write_hex64(detail);
        uart_write("\n");
        uart_flush();
        runtime->failure_record_emitted = true;
    }
}

_Noreturn void micros_bootstrap_runtime_fail(
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t detail
)
{
    micros_bootstrap_runtime_record_failure(
        reason,
        service_id,
        endpoint,
        detail
    );
    bootstrap_runtime_panic_runtime("bootstrap-failure");
}

void micros_bootstrap_runtime_check_deadline(uint64_t now)
{
    const struct bootstrap_runtime_context *runtime =
        bootstrap_runtime_context();
    uint32_t service_id;
    const struct micros_bootstrap_binding *binding;
    const struct micros_tty_handoff_runtime_state *tty;
    size_t index;

    if (runtime->bootstrap_state.phase != MICROS_BOOTSTRAP_PHASE_RUNNING) {
        return;
    }
    tty = micros_tty_handoff_runtime_state();
    if (
        tty != NULL
        && micros_tty_handoff_runtime_deadline_expired(now)
    ) {
        binding = micros_bootstrap_control_find_binding(
            &runtime->bootstrap_state,
            tty->service_id
        );
        micros_bootstrap_runtime_fail(
            MICROS_BOOTSTRAP_DIAGNOSTIC_READY_TIMEOUT,
            tty->service_id,
            binding == NULL
                ? MICROS_ENDPOINT_NONE
                : binding->endpoint,
            (uint64_t)tty->handoff.console_phase
        );
    }
    if (runtime->bootstrap_state.transitions.starting_service_id == 0) {
        return;
    }
    service_id = runtime->bootstrap_state.transitions.starting_service_id;
    for (
        index = 0;
        index < runtime->bootstrap_state.transitions.entry_count;
        ++index
    ) {
        const struct micros_bootstrap_runtime_entry *entry =
            &runtime->bootstrap_state.transitions.entries[index];

        if (
            entry->service_id == service_id
            && entry->state == MICROS_BOOTSTRAP_SERVICE_STARTING
            && entry->ready_deadline != 0
            && now >= entry->ready_deadline
        ) {
            binding = micros_bootstrap_control_find_binding(
                &runtime->bootstrap_state,
                service_id
            );
            micros_bootstrap_runtime_fail(
                MICROS_BOOTSTRAP_DIAGNOSTIC_READY_TIMEOUT,
                service_id,
                binding == NULL
                    ? MICROS_ENDPOINT_NONE
                    : binding->endpoint,
                entry->ready_deadline
            );
        }
    }
}
