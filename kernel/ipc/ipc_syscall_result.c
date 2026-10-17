#include "kernel/ipc/ipc_syscall_internal.h"

#include "arch/riscv64/trap_context.h"
#include "micros/ipc_buffer.h"
#include "micros/panic.h"

_Noreturn void ipc_syscall_panic(
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

enum micros_syscall_return ipc_syscall_return_abi_error(
    struct micros_trap_frame *frame,
    enum micros_ipc_abi_result result
)
{
    frame->a0 = (uint64_t)(int64_t)result;
    return MICROS_SYSCALL_RETURN_NORMAL;
}

enum micros_ipc_abi_result ipc_syscall_map_buffer_error(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_ipc_buffer_error error
)
{
    switch (error) {
    case MICROS_IPC_BUFFER_OK:
        return MICROS_IPC_ABI_OK;
    case MICROS_IPC_BUFFER_ERROR_ARGUMENT:
    case MICROS_IPC_BUFFER_ERROR_MESSAGE_FAULT:
        return MICROS_IPC_ABI_MESSAGE_FAULT;
    case MICROS_IPC_BUFFER_ERROR_INVARIANT:
        ipc_syscall_panic(hart, frame, "ipc-buffer-invariant");
    }
    ipc_syscall_panic(hart, frame, "ipc-buffer-result");
}
