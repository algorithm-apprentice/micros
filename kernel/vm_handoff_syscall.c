#include "kernel/vm_handoff_syscall.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/endpoint_internal.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/pm_control_runtime.h"
#include "kernel/plic.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/user_address_space_internal.h"
#include "kernel/vm_handoff_core.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_core.h"
#include "micros/panic.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

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

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size)
{
    uint8_t *output = destination;
    const uint8_t *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static bool process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool process_is_empty(const struct micros_process *process)
{
    return (
        process->live_thread_count == 0
        && process->address_space_root == 0
        && process->primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        && process->privilege_profile == 0
        && !process->endpoint_lifecycle_consumed
    );
}

static _Noreturn void panic_vm_handoff(
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

static enum micros_syscall_return return_result(
    struct micros_trap_frame *frame,
    enum micros_syscall_abi_result result
)
{
    frame->a0 = (uint64_t)(int64_t)result;
    return MICROS_SYSCALL_RETURN_NORMAL;
}

static enum micros_syscall_abi_result decode_request(
    const struct micros_syscall_arguments *arguments,
    uint32_t command,
    union vm_decoded_request *request
)
{
    switch (command) {
    case MICROS_VM_HANDOFF_READY:
        return micros_vm_handoff_decode(arguments, &request->ready);
    case MICROS_VM_HANDOFF_MAP_TTY_UART:
        return micros_vm_handoff_decode_tty_mapping(
            arguments,
            &request->tty_mapping
        );
    case MICROS_VM_LOAD_BEGIN:
        return micros_vm_load_begin_decode(
            arguments,
            &request->load_begin
        );
    case MICROS_VM_SCRATCH_MAP:
        return micros_vm_scratch_map_decode(
            arguments,
            &request->scratch_map
        );
    case MICROS_VM_SCRATCH_COMMIT:
        return micros_vm_scratch_commit_decode(
            arguments,
            &request->scratch_commit
        );
    case MICROS_VM_LOAD_FREEZE:
        return micros_vm_load_freeze_decode(
            arguments,
            &request->load_freeze
        );
    case MICROS_VM_LOAD_ABORT:
        return micros_vm_load_abort_decode(
            arguments,
            &request->load_abort
        );
    case MICROS_VM_LOAD_COMMIT:
        return micros_vm_load_commit_decode(
            arguments,
            &request->load_commit
        );
    case MICROS_VM_EXIT_RELEASE:
        return micros_vm_exit_release_decode(
            arguments,
            &request->exit_release
        );
    default:
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
}

static enum vm_fixed_object_error prepare_fixed_object(
    struct micros_process_handle process,
    uint64_t user_address,
    size_t size,
    uint32_t required_permission,
    struct vm_fixed_object_plan *plan
)
{
    struct vm_fixed_object_plan candidate = {0};
    uint64_t current = user_address;
    size_t remaining = size;

    if (
        plan == NULL
        || size == 0
        || user_address < MICROS_USER_VIRTUAL_BASE
        || size > MICROS_USER_VIRTUAL_END - MICROS_USER_VIRTUAL_BASE
        || user_address > MICROS_USER_VIRTUAL_END - size
    ) {
        return VM_FIXED_OBJECT_ERROR_MEMORY_FAULT;
    }
    while (remaining != 0) {
        uint64_t physical_address = 0;
        uint32_t permissions = 0;
        size_t contiguous = 0;
        size_t chunk;
        enum micros_user_address_space_error error;

        if (
            candidate.chunk_count
                >= VM_FIXED_OBJECT_CHUNK_CAPACITY
        ) {
            return VM_FIXED_OBJECT_ERROR_INVARIANT;
        }
        error = micros_user_address_space_translate(
            process,
            current,
            &physical_address,
            &permissions,
            &contiguous
        );
        if (
            error == MICROS_USER_ADDRESS_SPACE_ERROR_RANGE
            || error == MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION
            || error == MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED
            || error == MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
            || (
                error == MICROS_USER_ADDRESS_SPACE_OK
                && (
                    permissions & required_permission
                ) != required_permission
            )
        ) {
            return VM_FIXED_OBJECT_ERROR_MEMORY_FAULT;
        }
        if (
            error != MICROS_USER_ADDRESS_SPACE_OK
            || physical_address == 0
            || contiguous == 0
            || physical_address > UINTPTR_MAX
        ) {
            return VM_FIXED_OBJECT_ERROR_INVARIANT;
        }
        chunk = contiguous < remaining ? contiguous : remaining;
        if (
            physical_address > UINTPTR_MAX - (chunk - 1)
            || current > UINT64_MAX - chunk
        ) {
            return VM_FIXED_OBJECT_ERROR_INVARIANT;
        }
        candidate.chunks[candidate.chunk_count].physical_address =
            (uintptr_t)physical_address;
        candidate.chunks[candidate.chunk_count].size = chunk;
        ++candidate.chunk_count;
        current += chunk;
        remaining -= chunk;
    }
    candidate.size = size;
    *plan = candidate;
    return VM_FIXED_OBJECT_OK;
}

static void read_fixed_object_prevalidated(
    const struct vm_fixed_object_plan *plan,
    void *output
)
{
    uint8_t *destination = output;
    size_t offset = 0;
    size_t index;

    for (index = 0; index < plan->chunk_count; ++index) {
        copy_bytes(
            &destination[offset],
            (const void *)plan->chunks[index].physical_address,
            plan->chunks[index].size
        );
        offset += plan->chunks[index].size;
    }
}

static void write_fixed_object_prevalidated(
    const struct vm_fixed_object_plan *plan,
    const void *input
)
{
    const uint8_t *source = input;
    size_t offset = 0;
    size_t index;

    for (index = 0; index < plan->chunk_count; ++index) {
        copy_bytes(
            (void *)plan->chunks[index].physical_address,
            &source[offset],
            plan->chunks[index].size
        );
        offset += plan->chunks[index].size;
    }
}

static bool map_vm_error(
    enum micros_vm_handoff_error error,
    enum micros_syscall_abi_result *result
)
{
    switch (error) {
    case MICROS_VM_HANDOFF_ERROR_ARGUMENT:
    case MICROS_VM_HANDOFF_ERROR_SUMMARY:
        *result = MICROS_SYSCALL_ABI_ARGUMENT;
        return true;
    case MICROS_VM_HANDOFF_ERROR_STATE:
        *result = MICROS_SYSCALL_ABI_STATE;
        return true;
    default:
        return false;
    }
}

static _Noreturn void fail_vm_handoff(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_bootstrap_diagnostic_reason reason,
    const struct micros_vm_handoff_state *state
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();

    if (
        state != NULL
        && bootstrap != NULL
        && bootstrap->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
    ) {
        micros_bootstrap_runtime_fail(
            reason,
            state->service_id,
            state->endpoint,
            0
        );
    }
    panic_vm_handoff(hart, frame, "vm-handoff-invariant");
}

static bool vm_is_authorized(
    const struct micros_vm_handoff_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_syscall_context *context
)
{
    const struct micros_process *process;
    const struct micros_endpoint_record *endpoint;

    return (
        state != NULL
        && registry != NULL
        && objects != NULL
        && context->process.slot == state->process.slot
        && context->process.generation == state->process.generation
        && context->current.slot == state->thread.slot
        && context->current.generation == state->thread.generation
        && micros_process_resolve(
            objects,
            context->process,
            &process
        ) == MICROS_KERNEL_OBJECT_OK
        && process->primary_endpoint == state->endpoint
        && process->privilege_profile == state->profile_id
        && micros_endpoint_resolve_active(
            registry,
            objects,
            state->endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint->owner.slot == context->process.slot
        && endpoint->owner.generation
            == context->process.generation
        && micros_privilege_profile_allows_kernel_operation(
            registry,
            (uint8_t)state->profile_id,
            1
        ) == MICROS_ENDPOINT_OK
    );
}

static bool vm_phase_is_ready(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_vm_handoff_state *state,
    const struct micros_frame_ownership *ownership
)
{
    size_t index;

    if (
        bootstrap == NULL
        || state == NULL
        || ownership == NULL
        || bootstrap->phase != MICROS_BOOTSTRAP_PHASE_RUNNING
        || bootstrap->transitions.starting_service_id
            != state->service_id
        || state->phase != MICROS_VM_HANDOFF_PHASE_PREPARED
        || ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
    ) {
        return false;
    }
    for (
        index = 0;
        index < bootstrap->transitions.entry_count;
        ++index
    ) {
        const struct micros_bootstrap_runtime_entry *entry =
            &bootstrap->transitions.entries[index];

        if (entry->service_id == state->service_id) {
            return entry->state
                == MICROS_BOOTSTRAP_SERVICE_STARTING;
        }
    }
    return false;
}

static bool vm_post_handoff_phase_is_ready(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_vm_handoff_state *state,
    const struct micros_frame_ownership *ownership
)
{
    return (
        bootstrap != NULL
        && state != NULL
        && ownership != NULL
        && bootstrap->phase == MICROS_BOOTSTRAP_PHASE_SEALED
        && state->phase == MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        && ownership->phase
            == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    );
}

enum reservation_check_result {
    RESERVATION_CHECK_OK = 0,
    RESERVATION_CHECK_STATE,
    RESERVATION_CHECK_INVARIANT,
};

static enum reservation_check_result check_reservation(
    const struct micros_pm_control_state *pm,
    const struct micros_kernel_objects *objects,
    uint64_t token,
    const struct micros_process_handle *expected_process,
    bool require_empty,
    struct micros_process_handle *process_handle
)
{
    const struct micros_process *process;

    if (
        pm == NULL
        || objects == NULL
        || process_handle == NULL
        || token == 0
        || micros_pm_control_state_validate(pm)
            != MICROS_PM_CONTROL_OK
    ) {
        return RESERVATION_CHECK_INVARIANT;
    }
    if (
        pm->active_transaction != token
        || (
            expected_process != NULL
            && !process_handles_equal(
                pm->reserved_process,
                *expected_process
            )
        )
    ) {
        return RESERVATION_CHECK_STATE;
    }
    if (
        micros_process_resolve(
            objects,
            pm->reserved_process,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return RESERVATION_CHECK_INVARIANT;
    }
    if (require_empty && !process_is_empty(process)) {
        return RESERVATION_CHECK_INVARIANT;
    }
    *process_handle = pm->reserved_process;
    return RESERVATION_CHECK_OK;
}

enum tty_map_phase_result {
    TTY_MAP_PHASE_READY = 0,
    TTY_MAP_PHASE_STATE,
    TTY_MAP_PHASE_INVARIANT,
};

static const struct micros_bootstrap_runtime_entry *
runtime_entry(
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

static enum tty_map_phase_result tty_map_phase(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_vm_handoff_state *vm,
    const struct micros_tty_handoff_runtime_state *tty,
    const struct micros_frame_ownership *ownership
)
{
    const struct micros_bootstrap_runtime_entry *launcher_entry;
    const struct micros_bootstrap_runtime_entry *tty_entry;

    if (
        bootstrap == NULL
        || vm == NULL
        || tty == NULL
        || ownership == NULL
    ) {
        return TTY_MAP_PHASE_STATE;
    }
    if (
        bootstrap->transitions.entry_count
            > MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || bootstrap->transitions.next_order_index
            > bootstrap->transitions.entry_count
        || micros_tty_handoff_validate(&tty->handoff)
            != MICROS_TTY_HANDOFF_OK
        || tty->service_id != MICROS_TTY_SERVICE_ID
    ) {
        return TTY_MAP_PHASE_INVARIANT;
    }
    if (
        vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        || bootstrap->phase != MICROS_BOOTSTRAP_PHASE_RUNNING
        || tty->handoff.console_phase
            != MICROS_TTY_CONSOLE_MAP_REQUESTED
        || tty->handoff.route_phase != MICROS_TTY_ROUTE_DISABLED
        || bootstrap->transitions.starting_service_id != 0
        || bootstrap->transitions.next_order_index
            >= bootstrap->transitions.entry_count
        || bootstrap->transitions.ordered_service_ids[
            bootstrap->transitions.next_order_index
        ] != tty->service_id
    ) {
        return TTY_MAP_PHASE_STATE;
    }
    launcher_entry = runtime_entry(
        &bootstrap->transitions,
        bootstrap->plan.controller_service_id
    );
    tty_entry = runtime_entry(
        &bootstrap->transitions,
        tty->service_id
    );
    if (launcher_entry == NULL || tty_entry == NULL) {
        return TTY_MAP_PHASE_INVARIANT;
    }
    if (
        launcher_entry->state != MICROS_BOOTSTRAP_SERVICE_READY
        || launcher_entry->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        || !launcher_entry->profile_installed
        || !launcher_entry->scheduler_assigned
        || tty_entry->state != MICROS_BOOTSTRAP_SERVICE_PREPARED
        || tty_entry->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_RESERVED
        || tty_entry->profile_installed
        || tty_entry->scheduler_assigned
        || tty_entry->ready_deadline != 0
    ) {
        return TTY_MAP_PHASE_STATE;
    }
    return TTY_MAP_PHASE_READY;
}

static bool vm_console_event_state_is_clear(
    const struct micros_vm_handoff_state *vm,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_endpoint_record *endpoint;
    const struct micros_thread *thread;

    return (
        vm != NULL
        && micros_endpoint_resolve_active(
            registry,
            objects,
            vm->endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && micros_thread_resolve(
            objects,
            vm->thread,
            &thread
        ) == MICROS_KERNEL_OBJECT_OK
        && endpoint->pending_kernel_events == 0
        && micros_thread_ipc_state_is_clear(thread)
    );
}

static bool launcher_console_event_state_is_clear(
    const struct micros_bootstrap_binding *launcher,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_endpoint_record *endpoint;
    const struct micros_thread *thread;

    return (
        launcher != NULL
        && micros_endpoint_resolve_active(
            registry,
            objects,
            launcher->endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint->pending_kernel_events == 0
        && micros_thread_resolve(
            objects,
            launcher->thread,
            &thread
        ) == MICROS_KERNEL_OBJECT_OK
        && !micros_ipc_thread_has_staged_kernel_notification(
            thread
        )
    );
}

static _Noreturn void fail_console_map(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_tty_handoff_runtime_state *tty,
    uint64_t detail
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();

    if (
        tty != NULL
        && bootstrap != NULL
        && bootstrap->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
    ) {
        micros_bootstrap_runtime_fail(
            MICROS_BOOTSTRAP_DIAGNOSTIC_CONSOLE_MAP_GATE,
            tty->service_id,
            tty->endpoint,
            detail
        );
    }
    panic_vm_handoff(hart, frame, "console-map-gate");
}

static enum micros_syscall_return handle_ready_command(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    struct micros_bootstrap_control_state *bootstrap,
    const struct micros_frame_ownership *ownership,
    const struct micros_vm_handoff_request *request
)
{
    if (!vm_phase_is_ready(bootstrap, state, ownership)) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (
        micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_bootstrap_runtime_validate_vm_prepared()
            != MICROS_BOOTSTRAP_OK
    ) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    if (!micros_vm_handoff_summary_matches(state, request)) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE,
            state
        );
    }
    if (
        micros_user_address_space_complete_wired_handoff()
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE,
            state
        );
    }
    micros_vm_handoff_commit_prevalidated(state);
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}

static enum micros_syscall_return handle_tty_mapping_command(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_vm_handoff_state *vm,
    struct micros_bootstrap_control_state *bootstrap,
    struct micros_endpoint_registry *registry,
    const struct micros_frame_ownership *ownership,
    const struct micros_vm_tty_mapping_request *request
)
{
    struct micros_tty_handoff_runtime_state *tty =
        micros_tty_handoff_runtime_authoritative_state();
    const struct micros_bootstrap_binding *launcher;
    struct micros_user_address_space_tty_uart_plan mapping_plan;
    struct micros_tty_handoff mapped_handoff;
    struct micros_ipc_kernel_notification_plan notification;
    enum micros_user_address_space_error address_error;
    enum micros_tty_handoff_error tty_error;
    enum tty_map_phase_result phase;

    clear_bytes(&mapping_plan, sizeof(mapping_plan));
    clear_bytes(&notification, sizeof(notification));
    phase = tty_map_phase(bootstrap, vm, tty, ownership);
    if (phase == TTY_MAP_PHASE_STATE) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (phase != TTY_MAP_PHASE_READY) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            vm
        );
    }
    if (
        micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_vm_handoff_runtime_validate(
            bootstrap,
            registry,
            context->objects
        ) != MICROS_VM_HANDOFF_OK
        || micros_tty_handoff_runtime_validate(
            bootstrap,
            registry,
            context->objects
        ) != MICROS_TTY_HANDOFF_OK
        || !micros_plic_validate(MICROS_PLIC_DISABLED)
        || !uart_console_handoff_is_quiesced()
        || micros_user_address_space_validate(tty->process)
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            vm
        );
    }
    launcher = micros_bootstrap_control_find_binding(
        bootstrap,
        bootstrap->plan.controller_service_id
    );
    if (launcher == NULL) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            vm
        );
    }
    if (
        !launcher_console_event_state_is_clear(
            launcher,
            registry,
            context->objects
        )
    ) {
        fail_console_map(hart, frame, tty, UINT64_C(0x301));
    }
    if (
        !vm_console_event_state_is_clear(
            vm,
            registry,
            context->objects
        )
    ) {
        fail_console_map(hart, frame, tty, UINT64_C(0x300));
    }
    if (!micros_vm_tty_mapping_matches(request, tty->endpoint)) {
        fail_console_map(hart, frame, tty, UINT64_C(1));
    }
    address_error =
        micros_user_address_space_prepare_tty_uart_mapping(
            tty->process,
            tty->root_physical_address,
            &mapping_plan
        );
    if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
        fail_console_map(
            hart,
            frame,
            tty,
            (uint64_t)address_error
        );
    }
    tty_error = micros_tty_handoff_runtime_prepare_mapped(
        &mapped_handoff
    );
    if (tty_error != MICROS_TTY_HANDOFF_OK) {
        fail_console_map(
            hart,
            frame,
            tty,
            UINT64_C(0x100) | (uint64_t)tty_error
        );
    }
    if (
        micros_ipc_prepare_kernel_notification(
            registry,
            context->objects,
            launcher->endpoint,
            MICROS_KERNEL_EVENT_CONSOLE_MAPPED,
            &notification
        ) != MICROS_IPC_OK
    ) {
        fail_console_map(hart, frame, tty, UINT64_C(0x200));
    }

    micros_user_address_space_commit_tty_uart_mapping(
        &mapping_plan
    );
    micros_tty_handoff_runtime_commit_mapped_prevalidated(
        &mapped_handoff
    );
    micros_ipc_commit_kernel_notification_prevalidated(
        registry,
        context->objects,
        &notification
    );
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}

static enum micros_syscall_return handle_load_begin_command(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_vm_handoff_state *state,
    const struct micros_pm_control_state *pm,
    const struct micros_vm_load_begin_request *request
)
{
    struct micros_process_handle process;
    struct micros_vm_load_begin_plan load_plan;
    struct vm_fixed_object_plan output_plan;
    enum reservation_check_result reservation;
    enum vm_fixed_object_error object_error;
    enum micros_vm_handoff_error vm_error;
    enum micros_syscall_abi_result result;

    clear_bytes(&load_plan, sizeof(load_plan));
    clear_bytes(&output_plan, sizeof(output_plan));
    object_error = prepare_fixed_object(
        context->process,
        request->output_address,
        sizeof(struct micros_vm_load_begin_result),
        MICROS_SV39_PERMISSION_WRITE,
        &output_plan
    );
    if (object_error == VM_FIXED_OBJECT_ERROR_MEMORY_FAULT) {
        return return_result(
            frame,
            MICROS_SYSCALL_ABI_MEMORY_FAULT
        );
    }
    if (object_error != VM_FIXED_OBJECT_OK) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    reservation = check_reservation(
        pm,
        context->objects,
        request->process_reservation_token,
        NULL,
        true,
        &process
    );
    if (reservation == RESERVATION_CHECK_STATE) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (reservation != RESERVATION_CHECK_OK) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    vm_error = micros_vm_load_begin_preflight(
        state,
        request->process_reservation_token,
        process,
        true,
        &load_plan
    );
    if (vm_error != MICROS_VM_HANDOFF_OK) {
        if (map_vm_error(vm_error, &result)) {
            return return_result(frame, result);
        }
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }

    micros_vm_load_begin_commit_prevalidated(state, &load_plan);
    write_fixed_object_prevalidated(
        &output_plan,
        &load_plan.result
    );
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}

static enum micros_syscall_return handle_scratch_map_command(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    const struct micros_vm_scratch_map_request *request
)
{
    struct micros_vm_scratch_plan scratch;
    struct micros_frame_vm_claim_plan ownership_plan;
    struct micros_user_address_space_vm_scratch_plan address_plan;
    enum micros_vm_handoff_error vm_error;
    enum micros_frame_ownership_error ownership_error;
    enum micros_user_address_space_error address_error;
    enum micros_syscall_abi_result result;

    clear_bytes(&scratch, sizeof(scratch));
    clear_bytes(&ownership_plan, sizeof(ownership_plan));
    clear_bytes(&address_plan, sizeof(address_plan));
    vm_error = micros_vm_scratch_map_preflight(
        state,
        request,
        &scratch
    );
    if (vm_error != MICROS_VM_HANDOFF_OK) {
        if (map_vm_error(vm_error, &result)) {
            return return_result(frame, result);
        }
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    ownership_error =
        micros_frame_ownership_runtime_vm_claim_preflight(
            request->physical_address,
            &ownership_plan
        );
        if (ownership_error != MICROS_FRAME_OWNERSHIP_OK) {
            fail_vm_handoff(
                hart,
                frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    address_error =
        micros_user_address_space_prepare_vm_scratch_mapping(
            state->process,
            state->root_physical_address,
            request->physical_address,
            &address_plan
        );
        if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
            fail_vm_handoff(
                hart,
                frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }

    micros_vm_scratch_map_bind_prevalidated(state, &scratch);
    micros_frame_ownership_runtime_vm_claim_commit_prevalidated(
        &ownership_plan
    );
    micros_user_address_space_commit_vm_scratch_mapping(
        &address_plan
    );
    micros_vm_scratch_map_finish_prevalidated(state);
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}

static enum micros_syscall_return handle_scratch_commit_command(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    const struct micros_vm_scratch_commit_request *request
)
{
    struct micros_vm_scratch_plan scratch;
    struct micros_user_address_space_vm_scratch_plan address_plan;
    enum micros_vm_handoff_error vm_error;
    enum micros_user_address_space_error address_error;
    enum micros_syscall_abi_result result;

    clear_bytes(&scratch, sizeof(scratch));
    clear_bytes(&address_plan, sizeof(address_plan));
    vm_error = micros_vm_scratch_commit_preflight(
        state,
        request,
        &scratch
    );
    if (vm_error != MICROS_VM_HANDOFF_OK) {
        if (map_vm_error(vm_error, &result)) {
            return return_result(frame, result);
        }
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    address_error =
        micros_user_address_space_prepare_vm_scratch_removal(
            state->process,
            state->root_physical_address,
            request->physical_address,
            &address_plan
        );
        if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
            fail_vm_handoff(
                hart,
                frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }

    micros_user_address_space_commit_vm_scratch_removal(
        &address_plan
    );
    micros_vm_scratch_commit_commit_prevalidated(
        state,
        &scratch
    );
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}

static enum micros_syscall_return handle_load_freeze_command(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_vm_handoff_state *state,
    const struct micros_pm_control_state *pm,
    const struct micros_vm_load_freeze_request *request
)
{
    struct micros_vm_load_freeze_plan freeze_request;
    struct micros_vm_load_freeze_state_plan state_plan;
    struct micros_frame_vm_freeze_plan ownership_plan;
    struct micros_user_address_space_vm_freeze_plan address_plan;
    struct vm_fixed_object_plan input_plan;
    struct vm_fixed_object_plan output_plan;
    struct micros_process_handle reserved_process;
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    enum vm_fixed_object_error object_error;
    enum micros_vm_handoff_error vm_error;
    enum micros_frame_ownership_error ownership_error;
    enum micros_user_address_space_error address_error;
    enum reservation_check_result reservation;
    enum micros_syscall_abi_result result;
    size_t index;

    clear_bytes(&freeze_request, sizeof(freeze_request));
    clear_bytes(&state_plan, sizeof(state_plan));
    clear_bytes(&ownership_plan, sizeof(ownership_plan));
    clear_bytes(&address_plan, sizeof(address_plan));
    clear_bytes(&input_plan, sizeof(input_plan));
    clear_bytes(&output_plan, sizeof(output_plan));
    clear_bytes(data_frames, sizeof(data_frames));
    object_error = prepare_fixed_object(
        context->process,
        request->plan_address,
        sizeof(freeze_request),
        MICROS_SV39_PERMISSION_READ,
        &input_plan
    );
    if (object_error == VM_FIXED_OBJECT_ERROR_MEMORY_FAULT) {
        return return_result(
            frame,
            MICROS_SYSCALL_ABI_MEMORY_FAULT
        );
    }
    if (object_error != VM_FIXED_OBJECT_OK) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    read_fixed_object_prevalidated(&input_plan, &freeze_request);
    object_error = prepare_fixed_object(
        context->process,
        request->output_address,
        sizeof(struct micros_vm_load_freeze_result),
        MICROS_SV39_PERMISSION_WRITE,
        &output_plan
    );
    if (object_error == VM_FIXED_OBJECT_ERROR_MEMORY_FAULT) {
        return return_result(
            frame,
            MICROS_SYSCALL_ABI_MEMORY_FAULT
        );
    }
    if (object_error != VM_FIXED_OBJECT_OK) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    vm_error = micros_vm_load_freeze_preflight(
        state,
        &freeze_request,
        &state_plan
    );
    if (vm_error != MICROS_VM_HANDOFF_OK) {
        if (map_vm_error(vm_error, &result)) {
            return return_result(frame, result);
        }
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    reservation = check_reservation(
        pm,
        context->objects,
        state->load.process_reservation_token,
        &state_plan.action.process,
        true,
        &reserved_process
    );
    if (reservation == RESERVATION_CHECK_STATE) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (reservation != RESERVATION_CHECK_OK) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    for (index = 0; index < state_plan.action.entry_count; ++index) {
        data_frames[index] =
            state_plan.action.entries[index].physical_address;
    }
    ownership_error =
        micros_frame_ownership_runtime_vm_freeze_preflight(
            reserved_process,
            data_frames,
            state_plan.action.entry_count,
            state_plan.action.table_frames,
            &ownership_plan
        );
        if (ownership_error != MICROS_FRAME_OWNERSHIP_OK) {
            fail_vm_handoff(
                hart,
                frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    address_error = micros_user_address_space_prepare_vm_freeze(
        &state_plan.action,
        &address_plan
    );
    if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }

    micros_user_address_space_construct_vm_freeze_prevalidated(
        &address_plan
    );
    micros_frame_ownership_runtime_vm_freeze_commit_prevalidated(
        &ownership_plan
    );
    micros_user_address_space_install_vm_freeze_prevalidated(
        &address_plan
    );
    micros_vm_load_freeze_commit_prevalidated(state, &state_plan);
    write_fixed_object_prevalidated(
        &output_plan,
        &state_plan.result
    );
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}

static enum micros_syscall_return handle_load_abort_command(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_vm_handoff_state *state,
    const struct micros_pm_control_state *pm,
    const struct micros_vm_load_abort_request *request
)
{
    struct micros_vm_load_abort_action action;
    struct micros_user_address_space_vm_scratch_plan scratch_plan;
    struct micros_user_address_space_vm_teardown_plan teardown_plan;
    struct micros_frame_vm_transferable_release_plan
        transferable_plan;
    struct micros_frame_vm_process_release_plan process_plan;
    struct micros_process_handle reserved_process;
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    enum micros_vm_handoff_error vm_error;
    enum micros_frame_ownership_error ownership_error;
    enum micros_user_address_space_error address_error;
    enum reservation_check_result reservation;
    enum micros_syscall_abi_result result;
    size_t data_frame_count;
    size_t index;

    clear_bytes(&action, sizeof(action));
    clear_bytes(&scratch_plan, sizeof(scratch_plan));
    clear_bytes(&teardown_plan, sizeof(teardown_plan));
    clear_bytes(&transferable_plan, sizeof(transferable_plan));
    clear_bytes(&process_plan, sizeof(process_plan));
    clear_bytes(data_frames, sizeof(data_frames));
    vm_error = micros_vm_load_abort_preflight(
        state,
        request->load_token,
        request->mapping_generation,
        &action
    );
    if (vm_error != MICROS_VM_HANDOFF_OK) {
        if (map_vm_error(vm_error, &result)) {
            return return_result(frame, result);
        }
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    reservation = check_reservation(
        pm,
        context->objects,
        state->load.process_reservation_token,
        &action.process,
        !action.frozen,
        &reserved_process
    );
    if (reservation == RESERVATION_CHECK_STATE) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (reservation != RESERVATION_CHECK_OK) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    if (action.frozen) {
        address_error = micros_user_address_space_prepare_vm_teardown(
            reserved_process,
            action.root_physical_address,
            action.table_frames,
            action.entries,
            action.entry_count,
            &teardown_plan
        );
        if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
            fail_vm_handoff(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                state
            );
        }
        for (index = 0; index < action.entry_count; ++index) {
            data_frames[index] =
                action.entries[index].physical_address;
        }
        ownership_error =
            micros_frame_ownership_runtime_vm_release_process_preflight(
                reserved_process,
                data_frames,
                action.entry_count,
                action.table_frames,
                &process_plan
            );
            if (ownership_error != MICROS_FRAME_OWNERSHIP_OK) {
                fail_vm_handoff(
                    hart,
                    frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                state
            );
        }
    } else {
        data_frame_count = action.entry_count;
        for (index = 0; index < action.entry_count; ++index) {
            data_frames[index] =
                action.entries[index].physical_address;
        }
        if (action.scratch_mapped) {
            if (
                data_frame_count
                    >= MICROS_APPLICATION_RESIDENT_PAGE_MAX
            ) {
                fail_vm_handoff(
                    hart,
                    frame,
                    MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                    state
                );
            }
            data_frames[data_frame_count] =
                action.scratch_physical_address;
            ++data_frame_count;
            address_error =
                micros_user_address_space_prepare_vm_scratch_removal(
                    state->process,
                    state->root_physical_address,
                    action.scratch_physical_address,
                    &scratch_plan
                );
                if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
                    fail_vm_handoff(
                        hart,
                        frame,
                    MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                    state
                );
            }
        }
        ownership_error =
            micros_frame_ownership_runtime_vm_release_transferable_preflight(
                data_frames,
                data_frame_count,
                &transferable_plan
            );
            if (ownership_error != MICROS_FRAME_OWNERSHIP_OK) {
                fail_vm_handoff(
                    hart,
                    frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
                state
            );
        }
    }

    if (action.frozen) {
        micros_user_address_space_commit_vm_teardown_prevalidated(
            &teardown_plan
        );
        micros_frame_ownership_runtime_vm_release_process_commit_prevalidated(
            &process_plan
        );
    } else {
        if (action.scratch_mapped) {
            micros_user_address_space_commit_vm_scratch_removal(
                &scratch_plan
            );
        }
        micros_frame_ownership_runtime_vm_release_transferable_commit_prevalidated(
            &transferable_plan
        );
    }
    micros_vm_load_abort_commit_prevalidated(state);
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}

static enum micros_syscall_return handle_load_commit_command(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    const struct micros_vm_load_commit_request *request
)
{
    enum micros_vm_handoff_error vm_error;
    enum micros_syscall_abi_result result;

    vm_error = micros_vm_load_commit_preflight(
        state,
        request->load_token,
        request->mapping_generation
    );
    if (vm_error != MICROS_VM_HANDOFF_OK) {
        if (map_vm_error(vm_error, &result)) {
            return return_result(frame, result);
        }
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    micros_vm_load_commit_commit_prevalidated(state);
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}

static enum micros_syscall_return handle_exit_release_command(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    const struct micros_vm_exit_release_request *request
)
{
    struct micros_vm_exit_release_action action;
    struct micros_user_address_space_vm_teardown_plan address_plan;
    struct micros_frame_vm_process_release_plan ownership_plan;
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    enum micros_vm_handoff_error vm_error;
    enum micros_user_address_space_error address_error;
    enum micros_frame_ownership_error ownership_error;
    enum micros_syscall_abi_result result;
    size_t index;

    clear_bytes(&action, sizeof(action));
    clear_bytes(&address_plan, sizeof(address_plan));
    clear_bytes(&ownership_plan, sizeof(ownership_plan));
    clear_bytes(data_frames, sizeof(data_frames));
    vm_error = micros_vm_exit_release_preflight(
        state,
        request->exit_token,
        request->load_token,
        request->mapping_generation,
        &action
    );
    if (vm_error != MICROS_VM_HANDOFF_OK) {
        if (map_vm_error(vm_error, &result)) {
            return return_result(frame, result);
        }
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    address_error = micros_user_address_space_prepare_vm_teardown(
        action.process,
        action.root_physical_address,
        action.table_frames,
        action.entries,
        action.entry_count,
        &address_plan
    );
    if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    for (index = 0; index < action.entry_count; ++index) {
        data_frames[index] =
            action.entries[index].physical_address;
    }
    ownership_error =
        micros_frame_ownership_runtime_vm_release_process_preflight(
            action.process,
            data_frames,
            action.entry_count,
            action.table_frames,
            &ownership_plan
        );
        if (ownership_error != MICROS_FRAME_OWNERSHIP_OK) {
            fail_vm_handoff(
                hart,
                frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }

    micros_user_address_space_commit_vm_teardown_prevalidated(
        &address_plan
    );
    micros_frame_ownership_runtime_vm_release_process_commit_prevalidated(
        &ownership_plan
    );
    micros_vm_exit_release_commit_prevalidated(
        state,
        action.process
    );
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}

enum micros_syscall_return
micros_vm_handoff_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    union vm_decoded_request request;
    struct micros_vm_handoff_state *state;
    struct micros_bootstrap_control_state *bootstrap;
    struct micros_endpoint_registry *registry;
    const struct micros_frame_ownership *ownership;
    const struct micros_pm_control_state *pm;
    const struct micros_thread *current_thread;
    uint32_t command;

    if (
        hart == NULL
        || frame == NULL
        || context == NULL
        || arguments == NULL
        || context->objects == NULL
    ) {
        panic_vm_handoff(hart, frame, "vm-handoff-argument");
    }
    if (
        micros_vm_handoff_decode_command(arguments, &command)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return return_result(frame, MICROS_SYSCALL_ABI_ARGUMENT);
    }
    if (
        decode_request(arguments, command, &request)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return return_result(frame, MICROS_SYSCALL_ABI_ARGUMENT);
    }
    if (
        micros_thread_resolve(
            context->objects,
            context->current,
            &current_thread
        ) != MICROS_KERNEL_OBJECT_OK
        || current_thread->owner.slot != context->process.slot
        || current_thread->owner.generation
            != context->process.generation
    ) {
        panic_vm_handoff(hart, frame, "vm-handoff-current");
    }
    state = micros_vm_handoff_runtime_authoritative_state();
    bootstrap = micros_bootstrap_runtime_authoritative_state();
    registry = micros_ipc_runtime_authoritative_registry();
    if (
        !vm_is_authorized(
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
    ownership = micros_frame_ownership_runtime_ledger();
    if (ownership == NULL) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    if (
        bootstrap != NULL
        && bootstrap->transitions.entry_count
            > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    if (command == MICROS_VM_HANDOFF_READY) {
        return handle_ready_command(
            hart,
            frame,
            state,
            bootstrap,
            ownership,
            &request.ready
        );
    }
    if (command == MICROS_VM_HANDOFF_MAP_TTY_UART) {
        return handle_tty_mapping_command(
            hart,
            frame,
            context,
            state,
            bootstrap,
            registry,
            ownership,
            &request.tty_mapping
        );
    }
    if (
        !vm_post_handoff_phase_is_ready(
            bootstrap,
            state,
            ownership
        )
    ) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    pm = micros_pm_control_runtime_state();
    if (
        micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_vm_handoff_runtime_validate(
            bootstrap,
            registry,
            context->objects
        ) != MICROS_VM_HANDOFF_OK
        || micros_pm_control_runtime_validate(
            bootstrap,
            registry,
            context->objects
        ) != MICROS_PM_CONTROL_OK
        || micros_user_address_space_validate(state->process)
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    switch (command) {
    case MICROS_VM_LOAD_BEGIN:
        return handle_load_begin_command(
            hart,
            frame,
            context,
            state,
            pm,
            &request.load_begin
        );
    case MICROS_VM_SCRATCH_MAP:
        return handle_scratch_map_command(
            hart,
            frame,
            state,
            &request.scratch_map
        );
    case MICROS_VM_SCRATCH_COMMIT:
        return handle_scratch_commit_command(
            hart,
            frame,
            state,
            &request.scratch_commit
        );
    case MICROS_VM_LOAD_FREEZE:
        return handle_load_freeze_command(
            hart,
            frame,
            context,
            state,
            pm,
            &request.load_freeze
        );
    case MICROS_VM_LOAD_ABORT:
        return handle_load_abort_command(
            hart,
            frame,
            context,
            state,
            pm,
            &request.load_abort
        );
    case MICROS_VM_LOAD_COMMIT:
        return handle_load_commit_command(
            hart,
            frame,
            state,
            &request.load_commit
        );
    case MICROS_VM_EXIT_RELEASE:
        return handle_exit_release_command(
            hart,
            frame,
            state,
            &request.exit_release
        );
    default:
        return return_result(frame, MICROS_SYSCALL_ABI_ARGUMENT);
    }
}
