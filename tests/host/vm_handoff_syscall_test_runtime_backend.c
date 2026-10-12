#include "vm_handoff_syscall_test_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

const struct micros_bootstrap_control_state *
micros_bootstrap_runtime_state(void)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return &fixture->bootstrap;
}

struct micros_bootstrap_control_state *
micros_bootstrap_runtime_authoritative_state(void)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return &fixture->bootstrap;
}

enum micros_bootstrap_error micros_bootstrap_runtime_validate(void)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error
micros_bootstrap_runtime_validate_vm_prepared(void)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return MICROS_BOOTSTRAP_OK;
}

_Noreturn void micros_bootstrap_runtime_fail(
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t detail
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    (void)service_id;
    (void)endpoint;
    (void)detail;
    fixture->failure_reason = reason;
    if (fixture->panic_armed) {
        longjmp(fixture->panic_target, 1);
    }
    abort();
}

const struct micros_bootstrap_binding *
micros_bootstrap_control_find_binding(
    const struct micros_bootstrap_control_state *state,
    uint32_t service_id
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return micros_bootstrap_control_find_binding_bounded(
        state,
        service_id
    );
}

struct micros_vm_handoff_state *
micros_vm_handoff_runtime_authoritative_state(void)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return &fixture->vm_state;
}

const struct micros_vm_handoff_state *
micros_vm_handoff_runtime_state(void)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return &fixture->vm_state;
}

enum micros_vm_handoff_error micros_vm_handoff_runtime_validate(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return (
        state == &fixture->bootstrap
        && endpoint_registry == &fixture->registry
        && kernel_objects == &fixture->objects
    ) ? MICROS_VM_HANDOFF_OK : MICROS_VM_HANDOFF_ERROR_INVARIANT;
}

const struct micros_pm_control_state *
micros_pm_control_runtime_state(void)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return &fixture->pm_state;
}

enum micros_pm_control_error micros_pm_control_state_validate(
    const struct micros_pm_control_state *state
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return (
        state == &fixture->pm_state
        && state->service_id == 3
        && (
            state->active_transaction == 0
                ? state->reserved_process.slot == 0
                    && state->reserved_process.generation == 0
                : state->active_transaction
                    == state->last_transaction
                    && vm_handoff_syscall_test_process_handles_equal(
                        state->reserved_process,
                        fixture->child_process
                    )
        )
    ) ? MICROS_PM_CONTROL_OK
      : MICROS_PM_CONTROL_ERROR_INVARIANT;
}

enum micros_pm_control_error micros_pm_control_runtime_validate(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return (
        state == &fixture->bootstrap
        && endpoint_registry == &fixture->registry
        && kernel_objects == &fixture->objects
    ) ? MICROS_PM_CONTROL_OK
      : MICROS_PM_CONTROL_ERROR_INVARIANT;
}

struct micros_endpoint_registry *
micros_ipc_runtime_authoritative_registry(void)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return &fixture->registry;
}

const struct micros_frame_ownership *
micros_frame_ownership_runtime_ledger(void)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return &fixture->ownership;
}

enum micros_kernel_object_error micros_process_resolve(
    const struct micros_kernel_objects *kernel_objects,
    struct micros_process_handle handle,
    const struct micros_process **process
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (kernel_objects != &fixture->objects || process == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    if (vm_handoff_syscall_test_process_handles_equal(handle, fixture->vm_process)) {
        *process = &fixture->vm_process_record;
        return MICROS_KERNEL_OBJECT_OK;
    }
    if (vm_handoff_syscall_test_process_handles_equal(handle, fixture->foreign_process)) {
        *process = &fixture->foreign_process_record;
        return MICROS_KERNEL_OBJECT_OK;
    }
    if (vm_handoff_syscall_test_process_handles_equal(handle, fixture->child_process)) {
        *process = &fixture->child_process_record;
        return MICROS_KERNEL_OBJECT_OK;
    }
    return MICROS_KERNEL_OBJECT_ERROR_STALE;
}

enum micros_kernel_object_error micros_thread_resolve(
    const struct micros_kernel_objects *kernel_objects,
    struct micros_thread_handle handle,
    const struct micros_thread **thread
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (kernel_objects != &fixture->objects || thread == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    if (vm_handoff_syscall_test_thread_handles_equal(handle, fixture->vm_thread)) {
        *thread = &fixture->vm_thread_record;
        return MICROS_KERNEL_OBJECT_OK;
    }
    if (vm_handoff_syscall_test_thread_handles_equal(handle, fixture->launcher_thread)) {
        *thread = &fixture->launcher_thread_record;
        return MICROS_KERNEL_OBJECT_OK;
    }
    if (vm_handoff_syscall_test_thread_handles_equal(handle, fixture->foreign_thread)) {
        *thread = &fixture->foreign_thread_record;
        return MICROS_KERNEL_OBJECT_OK;
    }
    return MICROS_KERNEL_OBJECT_ERROR_STALE;
}

bool micros_thread_ipc_state_is_clear(
    const struct micros_thread *thread
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return (
        thread != NULL
        && thread->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && thread->ipc_next.slot == 0
        && thread->ipc_next.generation == 0
        && vm_handoff_syscall_test_bytes_are_zero(
            &thread->ipc_outbound_message,
            sizeof(thread->ipc_outbound_message)
        )
        && thread->ipc_send_destination == 0
        && thread->ipc_receive_source == 0
        && thread->ipc_receive_buffer == 0
        && !thread->ipc_delivery_pending
        && vm_handoff_syscall_test_bytes_are_zero(
            &thread->ipc_inbound_message,
            sizeof(thread->ipc_inbound_message)
        )
        && thread->ipc_staged_result == MICROS_IPC_OK
        && thread->ipc_reply_token == 0
        && thread->ipc_reply_callee == 0
    );
}

bool micros_ipc_thread_has_staged_kernel_notification(
    const struct micros_thread *thread
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return (
        thread != NULL
        && thread->ipc_delivery_pending
        && thread->ipc_staged_result == MICROS_IPC_OK
        && thread->ipc_inbound_message.source
            == MICROS_ENDPOINT_NONE
        && thread->ipc_inbound_message.type
            == MICROS_IPC_TYPE_KERNEL_NOTIFICATION
    );
}

enum micros_endpoint_error micros_endpoint_resolve_active(
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects,
    micros_endpoint_t endpoint,
    const struct micros_endpoint_record **record
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    if (
        endpoint_registry != &fixture->registry
        || kernel_objects != &fixture->objects
        || record == NULL
    ) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    if (endpoint == fixture->vm_endpoint) {
        *record = &fixture->vm_endpoint_record;
    } else if (endpoint == fixture->launcher_endpoint) {
        *record = &fixture->launcher_endpoint_record;
    } else {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error
micros_privilege_profile_allows_kernel_operation(
    const struct micros_endpoint_registry *endpoint_registry,
    uint8_t profile_id,
    uint8_t operation
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    return (
        endpoint_registry == &fixture->registry
        && profile_id == TEST_VM_PROFILE_ID
        && operation == 1
    ) ? MICROS_ENDPOINT_OK : MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
}

_Noreturn void micros_trap_panic_entry(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line,
    const struct micros_trap_frame *trap_frame
)
{
    struct vm_handoff_syscall_test_context *fixture =
        vm_handoff_syscall_test_context();

    (void)fixture;

    (void)hart_id;
    (void)reason;
    (void)file;
    (void)line;
    (void)trap_frame;
    if (fixture->panic_armed) {
        longjmp(fixture->panic_target, 1);
    }
    abort();
}
