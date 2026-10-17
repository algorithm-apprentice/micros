#include "kernel/trap/trap_internal.h"

#include <stddef.h>

#include "arch/riscv64/platform.h"
#include "kernel/boot/bootstrap_runtime.h"
#include "kernel/tty/tty_fault.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/vm/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"

void trap_fail_active_bootstrap_service_trap(
    struct micros_hart *hart
)
{
    const struct micros_bootstrap_control_state *state =
        micros_bootstrap_runtime_state();
    struct micros_thread_handle current;
    size_t index;

    if (
        state == NULL
        || (
            state->phase != MICROS_BOOTSTRAP_PHASE_RUNNING
            && state->phase != MICROS_BOOTSTRAP_PHASE_SEALED
        )
        || micros_hart_current_thread(
            micros_kernel_object_runtime_registry(),
            micros_kernel_object_runtime_boot_hart_handle(),
            &current
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return;
    }
    for (index = 0; index < state->entry_count; ++index) {
        if (
            state->bindings[index].thread.slot == current.slot
            && state->bindings[index].thread.generation
                == current.generation
        ) {
            if (state->phase == MICROS_BOOTSTRAP_PHASE_RUNNING) {
                micros_bootstrap_runtime_fail(
                    MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT,
                    state->bindings[index].service_id,
                    state->bindings[index].endpoint,
                    0
                );
            }
            if (
                state->phase == MICROS_BOOTSTRAP_PHASE_SEALED
                && state->bindings[index].service_id
                    == MICROS_TTY_SERVICE_ID
            ) {
                micros_tty_owner_fault_record();
            }
            return;
        }
    }
    (void)hart;
}

void trap_record_tty_owner_failure(void)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();
    const struct micros_tty_handoff_runtime_state *state =
        micros_tty_handoff_runtime_state();

    if (
        bootstrap != NULL
        && bootstrap->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
        && state != NULL
    ) {
        micros_bootstrap_runtime_fail(
            MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT,
            state->service_id,
            state->endpoint,
            0
        );
    }
    micros_tty_owner_fault_record();
}

static bool trap_vm_fault_is_current(
    struct micros_trap_frame *frame,
    uint64_t cause_code,
    const struct micros_vm_handoff_state *handoff,
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle *current
)
{
    if (
        handoff == NULL
        || objects == NULL
        || (frame->scause & MICROS_SCAUSE_INTERRUPT) != 0
        || (
            cause_code != MICROS_EXCEPTION_INSTRUCTION_PAGE_FAULT
            && cause_code != MICROS_EXCEPTION_LOAD_PAGE_FAULT
            && cause_code != MICROS_EXCEPTION_STORE_PAGE_FAULT
        )
        || micros_hart_current_thread(
            objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            current
        ) != MICROS_KERNEL_OBJECT_OK
        || current->slot != handoff->thread.slot
        || current->generation != handoff->thread.generation
    ) {
        return false;
    }
    return true;
}

static void trap_validate_vm_fault_runtime(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_frame_ownership *ownership,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    if (
        bootstrap == NULL
        || ownership == NULL
        || registry == NULL
        || micros_vm_handoff_runtime_validate(
            bootstrap,
            registry,
            objects
        ) != MICROS_VM_HANDOFF_OK
    ) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "vm-self-fault-invariant",
            frame
        );
    }
}

static void trap_classify_vm_fault(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_vm_handoff_state *handoff,
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_frame_ownership *ownership,
    enum micros_bootstrap_service_state *service_state,
    enum micros_vm_self_fault_action *action,
    const char **ownership_name
)
{
    size_t index;

    for (index = 0; index < bootstrap->transitions.entry_count; ++index) {
        if (
            bootstrap->transitions.entries[index].service_id
                == handoff->service_id
        ) {
            *service_state =
                bootstrap->transitions.entries[index].state;
            break;
        }
    }
    *action = micros_vm_self_fault_classify(
        bootstrap->phase,
        *service_state
    );
    if (*action == MICROS_VM_SELF_FAULT_INVALID) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "vm-self-fault-state",
            frame
        );
    }
    if (
        ownership->phase == MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
    ) {
        *ownership_name = "bootstrap";
    } else if (
        ownership->phase
            == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    ) {
        *ownership_name = "handed-off";
    } else {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "vm-self-fault-ownership",
            frame
        );
    }
}

static void trap_record_vm_self_fault(
    const struct micros_vm_handoff_state *handoff,
    const struct micros_trap_frame *frame,
    const char *ownership_name
)
{
    micros_panic_seize();
    uart_write("MICROS_VM_SELF_FAULT service=");
    uart_write_hex64(handoff->service_id);
    uart_write(" process-slot=");
    uart_write_hex64(handoff->process.slot);
    uart_write(" process-generation=");
    uart_write_hex64(handoff->process.generation);
    uart_write(" endpoint=");
    uart_write_hex64(handoff->endpoint);
    uart_write(" scause=");
    uart_write_hex64(frame->scause);
    uart_write(" stval=");
    uart_write_hex64(frame->stval);
    uart_write(" sepc=");
    uart_write_hex64(frame->sepc);
    uart_write(" ownership=");
    uart_write(ownership_name);
    uart_write("\n");
    uart_flush();
}

static _Noreturn void trap_fail_vm_self_fault(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_vm_handoff_state *handoff,
    enum micros_vm_self_fault_action action
)
{
    if (
        action == MICROS_VM_SELF_FAULT_RUNNING_STARTING
        || action == MICROS_VM_SELF_FAULT_RUNNING_READY
    ) {
        micros_bootstrap_runtime_record_failure(
            MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT,
            handoff->service_id,
            handoff->endpoint,
            0
        );
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "bootstrap-failure",
            frame
        );
    }
    if (action == MICROS_VM_SELF_FAULT_SEALED) {
        MICROS_TRAP_PANIC(
            hart->hardware_id,
            "vm-self-fault",
            frame
        );
    }
    MICROS_TRAP_PANIC(
        hart->hardware_id,
        "vm-self-fault-phase",
        frame
    );
}

bool trap_handle_vm_self_fault(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    uint64_t cause_code
)
{
    const struct micros_vm_handoff_state *handoff =
        micros_vm_handoff_runtime_state();
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();
    const struct micros_frame_ownership *ownership =
        micros_frame_ownership_runtime_ledger();
    const struct micros_endpoint_registry *registry =
        micros_ipc_runtime_registry();
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    struct micros_thread_handle current;
    const char *ownership_name;
    enum micros_bootstrap_service_state service_state = 0;
    enum micros_vm_self_fault_action action;

    if (!trap_vm_fault_is_current(
        frame, cause_code, handoff, objects, &current
    )) {
        return false;
    }
    trap_validate_vm_fault_runtime(
        hart, frame, bootstrap, ownership, registry, objects
    );
    trap_classify_vm_fault(
        hart, frame, handoff, bootstrap, ownership,
        &service_state, &action, &ownership_name
    );
    trap_record_vm_self_fault(handoff, frame, ownership_name);
    trap_fail_vm_self_fault(hart, frame, handoff, action);
}
