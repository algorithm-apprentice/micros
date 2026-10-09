#include "kernel/bootstrap_syscall.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/endpoint_internal.h"
#include "kernel/grant_runtime_internal.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/plic.h"
#include "kernel/scheduler_core_internal.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/user_address_space_internal.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_core.h"
#include "micros/ipc_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler_core.h"
#include "micros/user_execution.h"

static _Noreturn void panic_bootstrap_syscall(
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

static _Noreturn void fail_active_bootstrap(
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
    panic_bootstrap_syscall(
        hart,
        frame,
        "bootstrap-invariant"
    );
}

static enum micros_syscall_return return_result(
    struct micros_trap_frame *frame,
    enum micros_syscall_abi_result result
)
{
    frame->a0 = (uint64_t)(int64_t)result;
    return MICROS_SYSCALL_RETURN_NORMAL;
}

static enum micros_syscall_abi_result map_bootstrap_error(
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

static enum micros_syscall_abi_result map_ipc_error(
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

static bool controller_is_authorized(
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

static bool role_gate_ready(
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

static enum micros_bootstrap_diagnostic_reason map_failure_reason(
    enum micros_bootstrap_failure_reason reason
)
{
    switch (reason) {
    case MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_MALFORMED;
    case MICROS_BOOTSTRAP_FAILURE_READY_FOREIGN:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_FOREIGN;
    case MICROS_BOOTSTRAP_FAILURE_READY_EARLY:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_EARLY;
    case MICROS_BOOTSTRAP_FAILURE_READY_DUPLICATE:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_DUPLICATE;
    case MICROS_BOOTSTRAP_FAILURE_RELEASE_ORDER:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_ORDER;
    case MICROS_BOOTSTRAP_FAILURE_RELEASE_TRANSITION:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION;
    case MICROS_BOOTSTRAP_FAILURE_AUTHORITY:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY;
    case MICROS_BOOTSTRAP_FAILURE_COMPLETION:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION;
    case MICROS_BOOTSTRAP_FAILURE_READY_ROLE_GATE:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE;
    case MICROS_BOOTSTRAP_FAILURE_CONSOLE_PROTOCOL:
        return MICROS_BOOTSTRAP_DIAGNOSTIC_CONSOLE_PROTOCOL;
    }
    return MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY;
}

static uint64_t failure_detail(
    const struct micros_bootstrap_control_state *state,
    const struct micros_bootstrap_control_request *request
)
{
    if (
        request->failure_reason
        == MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED
    ) {
        return request->failure_detail;
    }
    if (
        request->failure_reason
            == MICROS_BOOTSTRAP_FAILURE_RELEASE_ORDER
        && state->transitions.next_order_index
            < state->transitions.entry_count
    ) {
        return state->transitions.ordered_service_ids[
            state->transitions.next_order_index
        ];
    }
    return 0;
}

static enum micros_bootstrap_error validate_failure_request(
    const struct micros_bootstrap_control_state *state,
    const struct micros_bootstrap_control_request *request
)
{
    const struct micros_bootstrap_binding *binding =
        micros_bootstrap_control_find_binding(
            state,
            request->service_id
        );

    if (
        micros_bootstrap_control_validate_failure_detail(request)
        != MICROS_SYSCALL_ABI_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    switch (request->failure_reason) {
    case MICROS_BOOTSTRAP_FAILURE_READY_FOREIGN:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return (
            state->transitions.starting_service_id != 0
            && request->service_id
                == state->transitions.starting_service_id
            && request->endpoint != MICROS_ENDPOINT_NONE
            && request->endpoint != MICROS_ENDPOINT_ANY
        )
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_READY_EARLY:
    case MICROS_BOOTSTRAP_FAILURE_READY_DUPLICATE:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return request->endpoint == binding->endpoint
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return (
            request->service_id
                == state->transitions.starting_service_id
            || request->endpoint == binding->endpoint
        )
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_RELEASE_ORDER:
    case MICROS_BOOTSTRAP_FAILURE_RELEASE_TRANSITION:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return request->endpoint == MICROS_ENDPOINT_NONE
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_AUTHORITY:
    case MICROS_BOOTSTRAP_FAILURE_COMPLETION:
        if (request->service_id != 0 && binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return request->endpoint == MICROS_ENDPOINT_NONE
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_READY_ROLE_GATE:
        if (binding == NULL) {
            return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
        }
        return (
            request->service_id
                == state->transitions.starting_service_id
            && request->endpoint == binding->endpoint
        )
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    case MICROS_BOOTSTRAP_FAILURE_CONSOLE_PROTOCOL:
        return (
            state->plan.console_service_id != 0
            && request->service_id
                == state->plan.console_service_id
            && request->endpoint == MICROS_ENDPOINT_NONE
        )
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_STATE;
    }
    return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
}

static const struct micros_bootstrap_runtime_entry *
find_runtime_entry(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t service_id
)
{
    size_t index;

    if (
        runtime == NULL
        || runtime->entry_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        return NULL;
    }
    for (index = 0; index < runtime->entry_count; ++index) {
        if (runtime->entries[index].service_id == service_id) {
            return &runtime->entries[index];
        }
    }
    return NULL;
}

static bool service_is_ready(
    const struct micros_bootstrap_control_state *state,
    uint32_t service_id
)
{
    const struct micros_bootstrap_runtime_entry *entry =
        find_runtime_entry(&state->transitions, service_id);

    return (
        service_id != 0
        && entry != NULL
        && entry->state == MICROS_BOOTSTRAP_SERVICE_READY
        && entry->endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        && entry->profile_installed
        && entry->scheduler_assigned
        && entry->ready_deadline == 0
    );
}

static bool no_bootstrap_deadline_is_armed(
    const struct micros_bootstrap_runtime *runtime
)
{
    size_t index;

    if (
        runtime == NULL
        || runtime->entry_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || runtime->starting_service_id != 0
    ) {
        return false;
    }
    for (index = 0; index < runtime->entry_count; ++index) {
        if (runtime->entries[index].ready_deadline != 0) {
            return false;
        }
    }
    return true;
}

static bool console_event_state_is_clear(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_endpoint_record *endpoint;
    const struct micros_thread *thread;

    return (
        micros_endpoint_resolve_active(
            registry,
            objects,
            state->controller_endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && micros_thread_resolve(
            objects,
            state->controller_thread,
            &thread
        ) == MICROS_KERNEL_OBJECT_OK
        && endpoint->pending_kernel_events == 0
        && micros_thread_ipc_state_is_clear(thread)
    );
}

static bool kernel_notification_destination_is_clear(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint_value,
    struct micros_thread_handle thread_handle
)
{
    const struct micros_endpoint_record *endpoint;
    const struct micros_thread *thread;

    return (
        micros_endpoint_resolve_active(
            registry,
            objects,
            endpoint_value,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint->pending_kernel_events == 0
        && micros_thread_resolve(
            objects,
            thread_handle,
            &thread
        ) == MICROS_KERNEL_OBJECT_OK
        && !micros_ipc_thread_has_staged_kernel_notification(
            thread
        )
    );
}

static enum micros_syscall_return handle_console_begin(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry
)
{
    struct micros_user_address_space_tty_uart_plan mapping_plan;
    struct micros_ipc_kernel_notification_plan notification;
    struct micros_tty_handoff begin;
    const struct micros_frame_ownership *ownership =
        micros_frame_ownership_runtime_ledger();
    const struct micros_vm_handoff_state *vm =
        micros_vm_handoff_runtime_state();
    const struct micros_tty_handoff_runtime_state *tty =
        micros_tty_handoff_runtime_state();
    const struct micros_bootstrap_binding *tty_binding;
    const struct micros_bootstrap_binding *vm_binding;
    const struct micros_bootstrap_manifest_entry *tty_entry;
    const struct micros_bootstrap_runtime_entry *tty_transition;
    enum micros_user_address_space_error address_error;
    enum micros_tty_handoff_error tty_error;
    enum micros_ipc_error ipc_error;
    uint64_t now = riscv_read_time();

    if (
        request->service_id != state->plan.console_service_id
        || request->service_id != MICROS_TTY_SERVICE_ID
    ) {
        return return_result(frame, MICROS_SYSCALL_ABI_ARGUMENT);
    }
    tty_binding = micros_bootstrap_control_find_binding(
        state,
        request->service_id
    );
    vm_binding = micros_bootstrap_control_find_binding(
        state,
        state->plan.vm_service_id
    );
    tty_transition = find_runtime_entry(
        &state->transitions,
        request->service_id
    );
    if (
        tty == NULL
        || tty_binding == NULL
        || vm_binding == NULL
        || tty_binding->manifest_index >= state->entry_count
        || ownership == NULL
        || vm == NULL
    ) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }
    tty_entry =
        &state->manifest.entries[tty_binding->manifest_index];
    if (
        ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        || vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || !service_is_ready(state, state->plan.vm_service_id)
        || !service_is_ready(state, state->plan.pm_service_id)
        || state->transitions.next_order_index
            >= state->transitions.entry_count
        || state->transitions.ordered_service_ids[
            state->transitions.next_order_index
        ] != request->service_id
        || tty_transition == NULL
        || tty_transition->state
            != MICROS_BOOTSTRAP_SERVICE_PREPARED
        || tty_transition->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_RESERVED
        || tty_transition->profile_installed
        || tty_transition->scheduler_assigned
        || tty_transition->ready_deadline != 0
        || tty_entry->stack_page_count != 1
        || tty_entry->ready_timeout_counter_ticks == 0
        || tty->handoff.console_phase
            != MICROS_TTY_CONSOLE_EARLY
        || tty->handoff.route_phase
            != MICROS_TTY_ROUTE_DISABLED
        || tty->handoff.deadline_armed
        || !no_bootstrap_deadline_is_armed(
            &state->transitions
        )
    ) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (
        micros_vm_handoff_runtime_validate(
            state,
            registry,
            context->objects
        ) != MICROS_VM_HANDOFF_OK
        || micros_tty_handoff_runtime_validate(
            state,
            registry,
            context->objects
        ) != MICROS_TTY_HANDOFF_OK
        || micros_frame_ownership_runtime_validate(
            context->objects
        ) != MICROS_FRAME_OWNERSHIP_OK
    ) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }
    address_error =
        micros_user_address_space_prepare_tty_uart_mapping(
            tty->process,
            tty->root_physical_address,
            &mapping_plan
        );
    if (
        address_error == MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED
        || address_error == MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT
        || address_error == MICROS_USER_ADDRESS_SPACE_ERROR_STATE
    ) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }
    tty_error = micros_tty_handoff_runtime_prepare_begin(
        now,
        tty_entry->ready_timeout_counter_ticks,
        &begin
    );
    if (tty_error == MICROS_TTY_HANDOFF_ERROR_RANGE) {
        return return_result(frame, MICROS_SYSCALL_ABI_RANGE);
    }
    if (tty_error == MICROS_TTY_HANDOFF_ERROR_STATE) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (
        tty_error != MICROS_TTY_HANDOFF_OK
        || !uart_console_begin_preflight()
        || !micros_plic_validate(MICROS_PLIC_DISABLED)
        || !kernel_notification_destination_is_clear(
            registry,
            context->objects,
            vm_binding->endpoint,
            vm_binding->thread
        )
    ) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }
    ipc_error = micros_ipc_prepare_kernel_notification(
        registry,
        context->objects,
        vm_binding->endpoint,
        MICROS_KERNEL_EVENT_CONSOLE_MAP_REQUEST,
        &notification
    );
    if (ipc_error != MICROS_IPC_OK) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }

    uart_console_begin_quiesce_prevalidated();
    micros_tty_handoff_runtime_commit_begin_deadline_prevalidated(
        &begin
    );
    uart_console_handoff_commit_prevalidated();
    micros_tty_handoff_runtime_commit_begin_phase_prevalidated(
        &begin
    );
    micros_ipc_commit_kernel_notification_prevalidated(
        registry,
        context->objects,
        &notification
    );
    if (
        !uart_console_handoff_is_quiesced()
        || micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
    ) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}

static enum micros_syscall_return handle_release(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry
)
{
    const struct micros_bootstrap_binding *binding =
        micros_bootstrap_control_find_binding(
            state,
            request->service_id
        );
    const struct micros_bootstrap_manifest_entry *entry =
        binding == NULL
            ? NULL
            : &state->manifest.entries[binding->manifest_index];
    const struct micros_tty_handoff_runtime_state *tty;
    struct micros_tty_handoff released;
    enum micros_bootstrap_error error;
    enum micros_tty_handoff_error tty_error;
    bool console = (
        entry != NULL
        && (
            entry->role_flags
            & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
        ) != 0
    );
    uint64_t now = riscv_read_time();

    micros_bootstrap_runtime_check_deadline(now);
    if (!console) {
        error = micros_bootstrap_control_release(
            state,
            registry,
            context->objects,
            context->hart,
            request->service_id,
            now,
            role_gate_ready(
                state,
                registry,
                context->objects,
                request->service_id,
                true
            )
        );
    } else {
        tty = micros_tty_handoff_runtime_state();
        tty_error =
            micros_tty_handoff_runtime_prepare_release(&released);
        if (tty_error == MICROS_TTY_HANDOFF_ERROR_STATE) {
            return return_result(frame, MICROS_SYSCALL_ABI_STATE);
        }
        if (
            tty == NULL
            || tty_error != MICROS_TTY_HANDOFF_OK
            || tty->service_id != request->service_id
            || !uart_console_handoff_is_quiesced()
            || !micros_plic_validate(MICROS_PLIC_DISABLED)
            || micros_user_address_space_validate_tty_uart_mapping(
                tty->process,
                tty->root_physical_address
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || !console_event_state_is_clear(
                state,
                registry,
                context->objects
            )
        ) {
            fail_active_bootstrap(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
                request->service_id,
                MICROS_ENDPOINT_NONE
            );
        }
        error = micros_bootstrap_control_release_console(
            state,
            registry,
            context->objects,
            context->hart,
            request->service_id,
            now,
            released.deadline,
            true
        );
        if (error == MICROS_BOOTSTRAP_OK) {
            micros_tty_handoff_runtime_commit_release_prevalidated(
                &released
            );
        }
    }
    if (
        error != MICROS_BOOTSTRAP_OK
        && error != MICROS_BOOTSTRAP_ERROR_ARGUMENT
        && error != MICROS_BOOTSTRAP_ERROR_RANGE
        && error != MICROS_BOOTSTRAP_ERROR_STATE
    ) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }
    if (
        error == MICROS_BOOTSTRAP_OK
        && micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
    ) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }
    return return_result(
        frame,
        map_bootstrap_error(error, false)
    );
}

static enum micros_syscall_return handle_accept_ready(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry
)
{
    struct micros_bootstrap_ready_plan plan;
    struct micros_tty_handoff tty_ready;
    struct micros_scheduler_current_ipc_guard guard = {0};
    const struct micros_bootstrap_binding *binding;
    const struct micros_bootstrap_manifest_entry *entry;
    const struct micros_thread *thread;
    enum micros_bootstrap_error error;
    enum micros_ipc_error ipc_error;
    enum micros_tty_handoff_error tty_error;
    bool tty_ready_active = false;
    bool gate_ready;
    uint64_t now = riscv_read_time();

    micros_bootstrap_runtime_check_deadline(now);
    binding = micros_bootstrap_control_find_binding(
        state,
        request->service_id
    );
    entry = binding == NULL
        ? NULL
        : &state->manifest.entries[binding->manifest_index];
    gate_ready = role_gate_ready(
        state,
        registry,
        context->objects,
        request->service_id,
        false
    );
    if (
        entry != NULL
        && (
            entry->role_flags
            & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
        ) != 0
        && gate_ready
    ) {
        tty_error = micros_tty_handoff_runtime_prepare_ready(
            now,
            &tty_ready
        );
        if (tty_error != MICROS_TTY_HANDOFF_OK) {
            fail_active_bootstrap(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE,
                request->service_id,
                request->endpoint
            );
        }
        tty_ready_active = true;
    }
    error = micros_bootstrap_control_prepare_ready(
        state,
        registry,
        context->objects,
        request->service_id,
        request->endpoint,
        now,
        gate_ready,
        &plan
    );
    if (error == MICROS_BOOTSTRAP_ERROR_ROLE) {
        if (binding == NULL) {
            fail_active_bootstrap(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                request->service_id,
                request->endpoint
            );
        }
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE,
            binding->service_id,
            binding->endpoint
        );
    }
    if (error != MICROS_BOOTSTRAP_OK) {
        if (
            error != MICROS_BOOTSTRAP_ERROR_ARGUMENT
            && error != MICROS_BOOTSTRAP_ERROR_IDENTITY
            && error != MICROS_BOOTSTRAP_ERROR_RANGE
            && error != MICROS_BOOTSTRAP_ERROR_STATE
        ) {
            fail_active_bootstrap(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                request->service_id,
                request->endpoint
            );
        }
        return return_result(
            frame,
            map_bootstrap_error(error, true)
        );
    }
    if (
        micros_user_execution_store_context(
            context->current,
            (const struct micros_user_context *)frame
        ) != MICROS_USER_EXECUTION_OK
        || micros_scheduler_begin_current_ipc(
            context->objects,
            context->hart,
            &guard
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            request->service_id,
            request->endpoint
        );
    }
    ipc_error = micros_ipc_reply_expected_caller(
        registry,
        context->objects,
        context->current,
        request->reply_token,
        request->endpoint,
        &plan.acknowledgment
    );
    if (ipc_error != MICROS_IPC_OK) {
        if (
            micros_scheduler_rollback_current_ipc(
                context->objects,
                &guard
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            fail_active_bootstrap(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                request->service_id,
                request->endpoint
            );
        }
        return return_result(frame, map_ipc_error(ipc_error));
    }
    micros_bootstrap_control_commit_ready_prevalidated(state, &plan);
    if (tty_ready_active) {
        micros_tty_handoff_runtime_commit_ready_prevalidated(
            &tty_ready
        );
    }
    if (
        micros_thread_resolve(
            context->objects,
            context->current,
            &thread
        ) != MICROS_KERNEL_OBJECT_OK
        || thread->runtime_flags != 0
        || thread->ipc_delivery_pending
        || micros_ipc_stage_no_message_completion(
            registry,
            context->objects,
            context->current,
            MICROS_IPC_OK
        ) != MICROS_IPC_OK
        || micros_scheduler_commit_current_ipc(
            context->objects,
            &guard
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
    ) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            request->service_id,
            request->endpoint
        );
    }
    return MICROS_SYSCALL_RETURN_CAPTURED;
}

static enum micros_syscall_return handle_complete(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    const struct micros_grant_registry *grants
)
{
    struct micros_bootstrap_complete_plan plan;
    struct micros_scheduler_current_ipc_guard guard = {0};
    const struct micros_bootstrap_binding *controller;
    enum micros_bootstrap_error error;

    error = micros_bootstrap_control_prepare_complete(
        state,
        registry,
        context->objects,
        grants,
        &plan
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        if (error != MICROS_BOOTSTRAP_ERROR_STATE) {
            fail_active_bootstrap(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION,
                0,
                MICROS_ENDPOINT_NONE
            );
        }
        return return_result(
            frame,
            map_bootstrap_error(error, false)
        );
    }
    if (
        micros_scheduler_preflight_current_ipc(
            context->objects,
            context->hart
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    controller = &state->bindings[
        plan.controller_binding_index
    ];
    if (
        micros_scheduler_begin_current_ipc(
            context->objects,
            context->hart,
            &guard
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION,
            0,
            MICROS_ENDPOINT_NONE
        );
    }
    if (
        micros_thread_scheduler_remove(
            context->objects,
            controller->thread
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION,
            0,
            MICROS_ENDPOINT_NONE
        );
    }
    micros_endpoint_commit_source_only_prevalidated(
        registry,
        controller->endpoint
    );
    micros_bootstrap_control_commit_complete_prevalidated(
        state,
        registry,
        context->objects,
        &plan
    );
    if (
        micros_bootstrap_control_validate(
            state,
            registry,
            context->objects
        ) != MICROS_BOOTSTRAP_OK
        || micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
    ) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION,
            0,
            MICROS_ENDPOINT_NONE
        );
    }
    return MICROS_SYSCALL_RETURN_CAPTURED;
}

enum micros_syscall_return
micros_bootstrap_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    struct micros_bootstrap_control_request request;
    struct micros_bootstrap_control_state *state;
    struct micros_endpoint_registry *registry;
    struct micros_grant_registry *grants;
    enum micros_syscall_abi_result decode_result;
    enum micros_bootstrap_error error;

    if (
        hart == NULL
        || frame == NULL
        || context == NULL
        || arguments == NULL
        || context->objects == NULL
    ) {
        panic_bootstrap_syscall(
            hart,
            frame,
            "bootstrap-syscall-argument"
        );
    }
    decode_result = micros_bootstrap_control_decode(
        arguments,
        &request
    );
    if (decode_result != MICROS_SYSCALL_ABI_OK) {
        return return_result(frame, decode_result);
    }
    state = micros_bootstrap_runtime_authoritative_state();
    if (state == NULL) {
        return return_result(
            frame,
            MICROS_SYSCALL_ABI_UNAUTHORIZED
        );
    }
    registry = micros_ipc_runtime_authoritative_registry();
    if (registry == NULL) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            0,
            MICROS_ENDPOINT_NONE
        );
    }
    if (
        !controller_is_authorized(
            state,
            registry,
            context->objects,
            context
        )
    ) {
        return return_result(
            frame,
            MICROS_SYSCALL_ABI_UNAUTHORIZED
        );
    }
    if (
        micros_bootstrap_runtime_validate() != MICROS_BOOTSTRAP_OK
    ) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            0,
            MICROS_ENDPOINT_NONE
        );
    }
    grants = micros_grant_runtime_authoritative_registry();
    if (grants == NULL) {
        fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            0,
            MICROS_ENDPOINT_NONE
        );
    }
    switch (request.command) {
    case MICROS_BOOTSTRAP_COMMAND_CONSOLE_BEGIN:
        return handle_console_begin(
            hart,
            frame,
            context,
            &request,
            state,
            registry
        );
    case MICROS_BOOTSTRAP_COMMAND_RELEASE:
        return handle_release(
            hart,
            frame,
            context,
            &request,
            state,
            registry
        );
    case MICROS_BOOTSTRAP_COMMAND_ACCEPT_READY:
        return handle_accept_ready(
            hart,
            frame,
            context,
            &request,
            state,
            registry
        );
    case MICROS_BOOTSTRAP_COMMAND_FAIL:
        error = validate_failure_request(state, &request);
        if (error != MICROS_BOOTSTRAP_OK) {
            return return_result(
                frame,
                map_bootstrap_error(error, false)
            );
        }
        micros_bootstrap_runtime_fail(
            map_failure_reason(request.failure_reason),
            request.service_id,
            request.endpoint,
            failure_detail(state, &request)
        );
    case MICROS_BOOTSTRAP_COMMAND_COMPLETE:
        return handle_complete(
            hart,
            frame,
            context,
            state,
            registry,
            grants
        );
    }
    fail_active_bootstrap(
        hart,
        frame,
        MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
        0,
        MICROS_ENDPOINT_NONE
    );
}
