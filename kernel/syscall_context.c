#include "kernel/syscall_internal.h"

#include "kernel/boot/bootstrap_runtime.h"
#include "kernel/objects/kernel_object_runtime_internal.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"

_Noreturn void syscall_panic_syscall(
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

void syscall_resolve_context(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_arguments *arguments,
    struct micros_syscall_context *context
)
{
    const struct micros_thread *thread;
    const struct micros_process *process;

    context->objects =
        micros_kernel_object_runtime_authoritative_registry();
    context->hart = micros_kernel_object_runtime_boot_hart_handle();
    if (
        context->objects == NULL
        || context->hart.slot >= MICROS_HART_CAPACITY
        || hart != &context->objects->harts[context->hart.slot]
        || micros_hart_current_thread(
            context->objects,
            context->hart,
            &context->current
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_resolve(
            context->objects,
            context->current,
            &thread
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_resolve(
            context->objects,
            thread->owner,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        const struct micros_bootstrap_control_state *state =
            micros_bootstrap_runtime_state();

        if (
            (
                arguments->a7
                    == MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL
                || arguments->a7
                    == MICROS_SYSCALL_ABI_VM_HANDOFF
                || arguments->a7
                    == MICROS_SYSCALL_ABI_PM_CONTROL
                || arguments->a7
                    == MICROS_SYSCALL_ABI_TTY_CONTROL
            )
            && state != NULL
            && state->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
        ) {
            micros_bootstrap_runtime_fail(
                MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                0,
                MICROS_ENDPOINT_NONE,
                0
            );
        }
        syscall_panic_syscall(hart, frame, "syscall-current-invariant");
    }
    context->process = thread->owner;
    (void)process;
}
