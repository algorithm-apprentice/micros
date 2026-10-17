#include "kernel/boot/bootstrap_syscall_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/endpoint/endpoint_internal.h"
#include "kernel/platform/plic.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/memory/user_address_space_internal.h"
#include "kernel/vm/vm_handoff_runtime.h"
#include "micros/panic.h"

_Noreturn void bootstrap_syscall_panic_bootstrap_syscall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
)
{
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        reason,
        frame
    );
}

_Noreturn void bootstrap_syscall_fail_active_bootstrap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    micros_endpoint_t endpoint
)
{
    const struct micros_bootstrap_control_state *state =
        micros_bootstrap_runtime_state();

    if (
        state != NULL
        && (
            state->phase == MICROS_BOOTSTRAP_PHASE_PREPARING
            || state->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
            || state->phase == MICROS_BOOTSTRAP_PHASE_SEALED
        )
    ) {
        micros_bootstrap_runtime_fail(
            reason,
            service_id,
            endpoint,
            0
        );
    }
    bootstrap_syscall_panic_bootstrap_syscall(
        hart,
        frame,
        "bootstrap-invariant"
    );
}

enum micros_syscall_return bootstrap_syscall_return_result(
    struct micros_trap_frame *frame,
    enum micros_syscall_abi_result result
)
{
    frame->a0 = (uint64_t)(int64_t)result;
    return MICROS_SYSCALL_RETURN_NORMAL;
}

enum micros_syscall_abi_result bootstrap_syscall_map_bootstrap_error(
    enum micros_bootstrap_error error,
    bool endpoint_identity
)
{
    switch (error) {
    case MICROS_BOOTSTRAP_OK:
        return MICROS_SYSCALL_ABI_OK;
    case MICROS_BOOTSTRAP_ERROR_ARGUMENT:
        return MICROS_SYSCALL_ABI_ARGUMENT;
    case MICROS_BOOTSTRAP_ERROR_IDENTITY:
        return endpoint_identity
            ? MICROS_SYSCALL_ABI_DEAD_ENDPOINT
            : MICROS_SYSCALL_ABI_ARGUMENT;
    case MICROS_BOOTSTRAP_ERROR_RANGE:
        return MICROS_SYSCALL_ABI_RANGE;
    case MICROS_BOOTSTRAP_ERROR_STATE:
        return MICROS_SYSCALL_ABI_STATE;
    default:
        return MICROS_SYSCALL_ABI_STATE;
    }
}

enum micros_syscall_abi_result bootstrap_syscall_map_ipc_error(
    enum micros_ipc_error error
)
{
    switch (error) {
    case MICROS_IPC_OK:
        return MICROS_SYSCALL_ABI_OK;
    case MICROS_IPC_ERROR_ARGUMENT:
        return MICROS_SYSCALL_ABI_ARGUMENT;
    case MICROS_IPC_ERROR_DEAD_ENDPOINT:
        return MICROS_SYSCALL_ABI_DEAD_ENDPOINT;
    case MICROS_IPC_ERROR_UNAUTHORIZED:
        return MICROS_SYSCALL_ABI_UNAUTHORIZED;
    case MICROS_IPC_ERROR_STATE:
    case MICROS_IPC_ERROR_NOT_READY:
        return MICROS_SYSCALL_ABI_STATE;
    case MICROS_IPC_ERROR_REPLY_TOKEN:
        return MICROS_SYSCALL_ABI_REPLY_TOKEN;
    case MICROS_IPC_ERROR_ENDPOINT_CLOSING:
        return MICROS_SYSCALL_ABI_ENDPOINT_CLOSING;
    default:
        return MICROS_SYSCALL_ABI_STATE;
    }
}

bool bootstrap_syscall_controller_is_authorized(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_syscall_context *context
)
{
    const struct micros_process *process;
    const struct micros_endpoint_record *endpoint;
    const struct micros_privilege_profile *profile;

    return (
        state != NULL
        && state->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
        && context->process.slot == state->controller_process.slot
        && context->process.generation
            == state->controller_process.generation
        && context->current.slot == state->controller_thread.slot
        && context->current.generation
            == state->controller_thread.generation
        && micros_process_resolve(
            objects,
            context->process,
            &process
        ) == MICROS_KERNEL_OBJECT_OK
        && process->primary_endpoint == state->controller_endpoint
        && micros_endpoint_resolve_active(
            registry,
            objects,
            state->controller_endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint->owner.slot == context->process.slot
        && endpoint->owner.generation
            == context->process.generation
        && micros_privilege_profile_resolve(
            registry,
            (uint8_t)process->privilege_profile,
            &profile
        ) == MICROS_ENDPOINT_OK
        && (
            profile->kernel_operations
            & MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL
        ) != 0
    );
}

bool bootstrap_syscall_role_gate_ready(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    uint32_t service_id,
    bool release
)
{
    const struct micros_bootstrap_binding *binding =
        micros_bootstrap_control_find_binding(state, service_id);
    const struct micros_bootstrap_manifest_entry *entry;

    if (binding == NULL) {
        return true;
    }
    entry = &state->manifest.entries[binding->manifest_index];
    if (
        (
            entry->role_flags
            & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
        ) != 0
    ) {
        const struct micros_tty_handoff_runtime_state *tty =
            micros_tty_handoff_runtime_state();

        return (
            !release
            && tty != NULL
            && tty->service_id == service_id
            && micros_tty_handoff_runtime_role_ready(
                registry,
                objects
            )
            && micros_user_address_space_validate_tty_uart_mapping(
                tty->process,
                tty->root_physical_address
            ) == MICROS_USER_ADDRESS_SPACE_OK
            && uart_console_handoff_is_active()
            && micros_plic_validate(MICROS_PLIC_ENABLED)
            && riscv_external_interrupt_is_enabled()
        );
    }
    if (
        !release
        && (entry->role_flags & MICROS_BOOTSTRAP_ROLE_VM) != 0
    ) {
        return micros_vm_handoff_runtime_role_ready(
            state,
            registry,
            objects,
            service_id,
            binding->process,
            binding->endpoint
        );
    }
    return true;
}
