#ifndef MICROS_KERNEL_IPC_SYSCALL_INTERNAL_H
#define MICROS_KERNEL_IPC_SYSCALL_INTERNAL_H

#include "kernel/ipc_syscall.h"
#include "micros/ipc_abi.h"
#include "micros/ipc_buffer.h"
#include "micros/ipc_core.h"

struct ipc_syscall_request {
    uint64_t operation;
    micros_endpoint_t endpoint;
    micros_endpoint_t source;
    uint64_t token;
    uint64_t event_mask;
    uintptr_t primary_buffer;
    uintptr_t receive_buffer;
    struct micros_ipc_message message;
};

_Noreturn void ipc_syscall_panic(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
);
enum micros_syscall_return ipc_syscall_return_abi_error(
    struct micros_trap_frame *frame,
    enum micros_ipc_abi_result result
);
enum micros_ipc_abi_result ipc_syscall_map_buffer_error(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_ipc_buffer_error error
);
enum micros_ipc_abi_result ipc_syscall_preflight_request(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_process_handle process,
    const struct micros_syscall_arguments *arguments,
    struct ipc_syscall_request *request
);
enum micros_ipc_error ipc_syscall_execute_portable(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_thread_handle thread,
    const struct ipc_syscall_request *request
);

#endif
