#include "kernel/bootstrap_runtime_internal.h"

#include "arch/riscv64/platform.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/kernel_object_runtime_internal.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

enum micros_bootstrap_error micros_bootstrap_runtime_validate(void)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    struct micros_endpoint_registry *registry =
        micros_ipc_runtime_authoritative_registry();
    const struct micros_tty_handoff_runtime_state *tty =
        micros_tty_handoff_runtime_state();
    const struct bootstrap_runtime_context *runtime =
        bootstrap_runtime_context();
    size_t index;

    if (
        objects == NULL
        || registry == NULL
        || runtime->bootstrap_state.phase
            == MICROS_BOOTSTRAP_PHASE_UNINITIALIZED
        || micros_bootstrap_control_validate(
            &runtime->bootstrap_state,
            registry,
            objects
        ) != MICROS_BOOTSTRAP_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || (
            runtime->bootstrap_state.plan.console_service_id == 0
                ? micros_tty_handoff_runtime_state() != NULL
                : micros_tty_handoff_runtime_validate(
                    &runtime->bootstrap_state,
                    registry,
                    objects
                ) != MICROS_TTY_HANDOFF_OK
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    if (
        runtime->bootstrap_state.plan.console_service_id != 0
        && (
            tty == NULL
            || (
                tty->handoff.console_phase
                    == MICROS_TTY_CONSOLE_EARLY
                    ? !uart_console_begin_preflight()
                    : !uart_console_handoff_is_active()
            )
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    for (index = 0; index < runtime->bootstrap_state.entry_count; ++index) {
        struct micros_user_context context;

        if (
            micros_user_address_space_validate(
                runtime->bootstrap_state.bindings[index].process
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || micros_user_execution_inspect(
                runtime->bootstrap_state.bindings[index].thread,
                &context
            ) != MICROS_USER_EXECUTION_OK
            || micros_user_execution_validate_context(
                runtime->bootstrap_state.bindings[index].thread,
                &context
            ) != MICROS_USER_EXECUTION_OK
        ) {
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
    }
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error
micros_bootstrap_runtime_validate_vm_prepared(void)
{
    struct micros_vm_boot_summary observed_summary;
    const struct micros_vm_handoff_state *handoff =
        micros_vm_handoff_runtime_state();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    struct micros_endpoint_registry *registry =
        micros_ipc_runtime_authoritative_registry();
    const struct bootstrap_runtime_context *runtime =
        bootstrap_runtime_context();

    if (
        handoff == NULL
        || handoff->phase != MICROS_VM_HANDOFF_PHASE_PREPARED
        || runtime->prepared_vm_snapshot.info == NULL
        || objects == NULL
        || registry == NULL
        || micros_vm_handoff_runtime_validate(
            &runtime->bootstrap_state,
            registry,
            objects
        ) != MICROS_VM_HANDOFF_OK
        || micros_vm_boot_validate(
            runtime->prepared_vm_snapshot.info,
            &observed_summary
        ) != MICROS_VM_BOOT_OK
        || micros_vm_snapshot_validate_current(
            &runtime->bootstrap_state.manifest,
            runtime->bootstrap_state.bindings,
            runtime->bootstrap_state.entry_count,
            runtime->prepared_vm_snapshot.info
        ) != MICROS_BOOTSTRAP_OK
        || !bootstrap_runtime_bytes_equal(
            &observed_summary,
            &handoff->summary,
            sizeof(observed_summary)
        )
        || !bootstrap_preparation_user_range_matches(
            handoff->process,
            handoff->boot_info_address,
            runtime->prepared_vm_snapshot.info,
            handoff->boot_info_size
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    return MICROS_BOOTSTRAP_OK;
}
