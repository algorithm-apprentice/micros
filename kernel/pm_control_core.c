#include "kernel/pm_control_core.h"

#include <stddef.h>
#include <stdint.h>

#include "micros/endpoint.h"

#define MICROS_PM_CONTROL_MAGIC UINT64_C(0x4d4943524f535043)

static bool bytes_are_zero(const void *storage, size_t size)
{
    const unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static bool pointer_is_aligned(const void *pointer, size_t alignment)
{
    return (uintptr_t)pointer % alignment == 0;
}

static bool process_handle_is_valid(
    struct micros_process_handle process
)
{
    return (
        process.slot < MICROS_PROCESS_CAPACITY
        && process.generation != 0
        && process.generation <= MICROS_PROCESS_GENERATION_MAX
    );
}

static bool process_handle_is_zero(
    struct micros_process_handle process
)
{
    return process.slot == 0 && process.generation == 0;
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

static bool thread_handle_is_valid(
    struct micros_thread_handle thread
)
{
    return (
        thread.slot < MICROS_THREAD_CAPACITY
        && thread.generation != 0
    );
}

static bool endpoint_matches_process(
    uint32_t endpoint,
    struct micros_process_handle process
)
{
    return (
        process_handle_is_valid(process)
        && endpoint
            == (
                (process.generation << MICROS_ENDPOINT_SLOT_BITS)
                | process.slot
            )
        && endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
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

enum micros_syscall_abi_result micros_pm_control_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_pm_control_request *request
)
{
    struct micros_pm_control_request candidate;

    if (
        arguments == NULL
        || request == NULL
        || !pointer_is_aligned(
            arguments,
            _Alignof(struct micros_syscall_arguments)
        )
        || !pointer_is_aligned(
            request,
            _Alignof(struct micros_pm_control_request)
        )
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    if (arguments->a7 != MICROS_SYSCALL_ABI_PM_CONTROL) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    if (arguments->a0 == MICROS_PM_CONTROL_RESERVE) {
        if (
            arguments->a1 % 8 != 0
            || arguments->a2 != MICROS_PM_RESERVATION_SIZE
            || arguments->a3 != 0
            || arguments->a4 != 0
            || arguments->a5 != 0
            || arguments->a6 != 0
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate.command = MICROS_PM_CONTROL_RESERVE;
        candidate.output_address = arguments->a1;
    } else if (
        arguments->a0 == MICROS_PM_CONTROL_ABORT_RESERVED
    ) {
        if (
            arguments->a1 == 0
            || arguments->a2 != 0
            || arguments->a3 != 0
            || arguments->a4 != 0
            || arguments->a5 != 0
            || arguments->a6 != 0
        ) {
            return MICROS_SYSCALL_ABI_ARGUMENT;
        }
        candidate.command = MICROS_PM_CONTROL_ABORT_RESERVED;
        candidate.transaction = arguments->a1;
    } else {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_pm_control_error micros_pm_control_state_prepare(
    struct micros_pm_control_state *state,
    uint32_t service_id,
    uint32_t endpoint,
    uint32_t profile_id,
    struct micros_process_handle process,
    struct micros_thread_handle thread
)
{
    struct micros_pm_control_state candidate;

    if (
        state == NULL
        || !pointer_is_aligned(
            state,
            _Alignof(struct micros_pm_control_state)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (!bytes_are_zero(state, sizeof(*state))) {
        return MICROS_PM_CONTROL_ERROR_STORAGE;
    }
    if (
        service_id != 3
        || process.slot != 2
        || profile_id != MICROS_PRIVILEGE_PROFILE_PM
        || !endpoint_matches_process(endpoint, process)
        || !thread_handle_is_valid(thread)
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    candidate.initialization_magic = MICROS_PM_CONTROL_MAGIC;
    candidate.service_id = service_id;
    candidate.endpoint = endpoint;
    candidate.profile_id = profile_id;
    candidate.process.slot = process.slot;
    candidate.process.generation = process.generation;
    candidate.thread.slot = thread.slot;
    candidate.thread.generation = thread.generation;
    copy_bytes(state, &candidate, sizeof(candidate));
    return MICROS_PM_CONTROL_OK;
}

enum micros_pm_control_error micros_pm_control_state_validate(
    const struct micros_pm_control_state *state
)
{
    if (
        state == NULL
        || !pointer_is_aligned(
            state,
            _Alignof(struct micros_pm_control_state)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (
        state->initialization_magic != MICROS_PM_CONTROL_MAGIC
        || state->service_id != 3
        || state->process.slot != 2
        || state->profile_id != MICROS_PRIVILEGE_PROFILE_PM
        || state->reserved != 0
        || !endpoint_matches_process(
            state->endpoint,
            state->process
        )
        || !thread_handle_is_valid(state->thread)
        || (
            state->active_transaction == 0
            && !process_handle_is_zero(state->reserved_process)
        )
        || (
            state->active_transaction != 0
            && (
                state->active_transaction
                    != state->last_transaction
                || !process_handle_is_valid(
                    state->reserved_process
                )
                || process_handles_equal(
                    state->reserved_process,
                    state->process
                )
            )
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    return MICROS_PM_CONTROL_OK;
}

enum micros_pm_control_error micros_pm_control_state_validate_objects(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_process *process;

    if (
        state == NULL
        || objects == NULL
        || !pointer_is_aligned(
            objects,
            _Alignof(struct micros_kernel_objects)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (
        micros_pm_control_state_validate(state)
            != MICROS_PM_CONTROL_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (state->active_transaction == 0) {
        return MICROS_PM_CONTROL_OK;
    }
    if (
        micros_process_resolve(
            objects,
            state->reserved_process,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || !process_is_empty(process)
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    return MICROS_PM_CONTROL_OK;
}

enum micros_pm_control_error micros_pm_control_reserve_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    struct micros_pm_control_reserve_plan *plan
)
{
    struct micros_pm_control_reserve_plan candidate;
    size_t index;

    if (
        plan == NULL
        || !pointer_is_aligned(
            plan,
            _Alignof(struct micros_pm_control_reserve_plan)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (
        micros_pm_control_state_validate_objects(state, objects)
            != MICROS_PM_CONTROL_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (state->active_transaction != 0) {
        return MICROS_PM_CONTROL_ERROR_STATE;
    }
    if (state->last_transaction == UINT64_MAX) {
        return MICROS_PM_CONTROL_ERROR_CAPACITY;
    }
    clear_bytes(&candidate, sizeof(candidate));
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        uint32_t generation;

        if (
            objects->processes[index].slot_state
                != MICROS_KERNEL_OBJECT_SLOT_FREE
        ) {
            continue;
        }
        if (
            micros_process_next_generation(
                (uint16_t)index,
                objects->processes[index].generation,
                &generation
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            continue;
        }
        candidate.process.slot = (uint16_t)index;
        candidate.process.generation = generation;
        break;
    }
    if (candidate.process.generation == 0) {
        return MICROS_PM_CONTROL_ERROR_CAPACITY;
    }
    candidate.transaction = state->last_transaction + 1;
    candidate.result.version = MICROS_PM_RESERVATION_VERSION;
    candidate.result.size = MICROS_PM_RESERVATION_SIZE;
    candidate.result.transaction = candidate.transaction;
    copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_PM_CONTROL_OK;
}

void micros_pm_control_reserve_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    const struct micros_pm_control_reserve_plan *plan
)
{
    struct micros_process *process =
        &objects->processes[plan->process.slot];

    process->slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE;
    process->generation = plan->process.generation;
    process->live_thread_count = 0;
    process->address_space_root = 0;
    process->primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
    process->privilege_profile = 0;
    process->endpoint_lifecycle_consumed = false;
    ++objects->live_process_count;
    state->last_transaction = plan->transaction;
    state->active_transaction = plan->transaction;
    state->reserved_process = plan->process;
}

enum micros_pm_control_error micros_pm_control_abort_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    uint64_t transaction,
    struct micros_pm_control_abort_plan *plan
)
{
    struct micros_pm_control_abort_plan candidate;
    uint32_t unused_generation;
    enum micros_kernel_object_error generation_error;

    if (
        transaction == 0
        || plan == NULL
        || !pointer_is_aligned(
            plan,
            _Alignof(struct micros_pm_control_abort_plan)
        )
    ) {
        return MICROS_PM_CONTROL_ERROR_ARGUMENT;
    }
    if (
        micros_pm_control_state_validate_objects(state, objects)
            != MICROS_PM_CONTROL_OK
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    if (state->active_transaction != transaction) {
        return MICROS_PM_CONTROL_ERROR_STATE;
    }
    generation_error = micros_process_next_generation(
        state->reserved_process.slot,
        state->reserved_process.generation,
        &unused_generation
    );
    if (
        generation_error != MICROS_KERNEL_OBJECT_OK
        && generation_error
            != MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED
    ) {
        return MICROS_PM_CONTROL_ERROR_INVARIANT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    candidate.process = state->reserved_process;
    candidate.transaction = transaction;
    candidate.quarantine = generation_error
        == MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED;
    copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_PM_CONTROL_OK;
}

void micros_pm_control_abort_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    const struct micros_pm_control_abort_plan *plan
)
{
    struct micros_process *process =
        &objects->processes[plan->process.slot];

    process->slot_state = plan->quarantine
        ? MICROS_KERNEL_OBJECT_SLOT_QUARANTINED
        : MICROS_KERNEL_OBJECT_SLOT_FREE;
    process->live_thread_count = 0;
    process->address_space_root = 0;
    process->primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE;
    process->privilege_profile = 0;
    process->endpoint_lifecycle_consumed = false;
    --objects->live_process_count;
    state->active_transaction = 0;
    state->reserved_process.slot = 0;
    state->reserved_process.generation = 0;
}
