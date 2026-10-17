#ifndef MICROS_KERNEL_VM_HANDOFF_SYSCALL_INTERNAL_H
#define MICROS_KERNEL_VM_HANDOFF_SYSCALL_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/endpoint/endpoint_internal.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/pm_control_runtime.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/memory/user_address_space_internal.h"
#include "kernel/vm/vm_handoff_core.h"
#include "kernel/vm/vm_handoff_runtime.h"
#include "kernel/vm/vm_handoff_syscall.h"
#include "micros/frame_ownership_runtime.h"

enum {
    VM_FIXED_OBJECT_CHUNK_CAPACITY = 2,
};

enum vm_fixed_object_error {
    VM_FIXED_OBJECT_OK = 0,
    VM_FIXED_OBJECT_ERROR_MEMORY_FAULT,
    VM_FIXED_OBJECT_ERROR_INVARIANT,
};

struct vm_fixed_object_chunk {
    uintptr_t physical_address;
    size_t size;
};

struct vm_fixed_object_plan {
    size_t chunk_count;
    size_t size;
    struct vm_fixed_object_chunk
        chunks[VM_FIXED_OBJECT_CHUNK_CAPACITY];
};

union vm_decoded_request {
    struct micros_vm_handoff_request ready;
    struct micros_vm_tty_mapping_request tty_mapping;
    struct micros_vm_load_begin_request load_begin;
    struct micros_vm_scratch_map_request scratch_map;
    struct micros_vm_scratch_commit_request scratch_commit;
    struct micros_vm_load_freeze_request load_freeze;
    struct micros_vm_load_abort_request load_abort;
    struct micros_vm_load_commit_request load_commit;
    struct micros_vm_exit_release_request exit_release;
};

enum reservation_check_result {
    RESERVATION_CHECK_OK = 0,
    RESERVATION_CHECK_STATE,
    RESERVATION_CHECK_INVARIANT,
};

enum tty_map_phase_result {
    TTY_MAP_PHASE_READY = 0,
    TTY_MAP_PHASE_STATE,
    TTY_MAP_PHASE_INVARIANT,
};

void micros_vm_handoff_syscall_clear_bytes(
    void *storage,
    size_t size
);

void micros_vm_handoff_syscall_copy_bytes(
    void *destination,
    const void *source,
    size_t size
);

bool micros_vm_handoff_syscall_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
);

bool micros_vm_handoff_syscall_process_is_empty(
    const struct micros_process *process
);

_Noreturn void micros_vm_handoff_syscall_panic(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
);

enum micros_syscall_return micros_vm_handoff_syscall_return_result(
    struct micros_trap_frame *frame,
    enum micros_syscall_abi_result result
);

bool micros_vm_handoff_syscall_map_error(
    enum micros_vm_handoff_error error,
    enum micros_syscall_abi_result *result
);

_Noreturn void micros_vm_handoff_syscall_fail(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_bootstrap_diagnostic_reason reason,
    const struct micros_vm_handoff_state *state
);

bool micros_vm_handoff_syscall_vm_is_authorized(
    const struct micros_vm_handoff_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_syscall_context *context
);

bool micros_vm_handoff_syscall_vm_phase_is_ready(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_vm_handoff_state *state,
    const struct micros_frame_ownership *ownership
);

bool micros_vm_handoff_syscall_post_handoff_phase_is_ready(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_vm_handoff_state *state,
    const struct micros_frame_ownership *ownership
);

enum reservation_check_result
micros_vm_handoff_syscall_check_reservation(
    const struct micros_pm_control_state *pm,
    const struct micros_kernel_objects *objects,
    uint64_t token,
    const struct micros_process_handle *expected_process,
    bool require_empty,
    struct micros_process_handle *process_handle
);

enum vm_fixed_object_error micros_vm_handoff_syscall_prepare_fixed_object(
    struct micros_process_handle process,
    uint64_t user_address,
    size_t size,
    uint32_t required_permission,
    struct vm_fixed_object_plan *plan
);

void micros_vm_handoff_syscall_read_fixed_object_prevalidated(
    const struct vm_fixed_object_plan *plan,
    void *output
);

void micros_vm_handoff_syscall_write_fixed_object_prevalidated(
    const struct vm_fixed_object_plan *plan,
    const void *input
);

enum micros_syscall_return micros_vm_handoff_syscall_handle_ready(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    struct micros_bootstrap_control_state *bootstrap,
    const struct micros_frame_ownership *ownership,
    const struct micros_vm_handoff_request *request
);

enum micros_syscall_return micros_vm_handoff_syscall_handle_tty_mapping(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_vm_handoff_state *vm,
    struct micros_bootstrap_control_state *bootstrap,
    struct micros_endpoint_registry *registry,
    const struct micros_frame_ownership *ownership,
    const struct micros_vm_tty_mapping_request *request
);

enum micros_syscall_return micros_vm_handoff_syscall_handle_load_begin(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_vm_handoff_state *state,
    const struct micros_pm_control_state *pm,
    const struct micros_vm_load_begin_request *request
);

enum micros_syscall_return micros_vm_handoff_syscall_handle_load_commit(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    const struct micros_vm_load_commit_request *request
);

enum micros_syscall_return micros_vm_handoff_syscall_handle_scratch_map(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    const struct micros_vm_scratch_map_request *request
);

enum micros_syscall_return micros_vm_handoff_syscall_handle_scratch_commit(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    const struct micros_vm_scratch_commit_request *request
);

enum micros_syscall_return micros_vm_handoff_syscall_handle_load_freeze(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_vm_handoff_state *state,
    const struct micros_pm_control_state *pm,
    const struct micros_vm_load_freeze_request *request
);

enum micros_syscall_return micros_vm_handoff_syscall_handle_load_abort(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_vm_handoff_state *state,
    const struct micros_pm_control_state *pm,
    const struct micros_vm_load_abort_request *request
);

enum micros_syscall_return micros_vm_handoff_syscall_handle_exit_release(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    const struct micros_vm_exit_release_request *request
);

#endif
