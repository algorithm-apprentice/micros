#include "tests/host/elf_spawn_model_internal.h"

static struct elf_model_kernel_process *current_process(struct elf_spawn_model_context *context)
{
    return context->kernel_slot < MICROS_PROCESS_CAPACITY
        ? &context->kernel[context->kernel_slot] : NULL;
}

static micros_runtime_result_t model_reserve(
    void *opaque, struct micros_pm_reservation_result *result
)
{
    struct elf_spawn_model_context *context = opaque;
    size_t slot;

    if (!elf_model_record_stage(context, ELF_STAGE_RESERVE)
        || !elf_model_kernel_require(context, context->kernel_slot == ELF_MODEL_NO_SLOT)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (context->operation == ELF_MODEL_RESERVE_FAILURE) {
        return MICROS_SYSCALL_ABI_CAPACITY;
    }
    for (slot = ELF_MODEL_FIRST_PROCESS; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        if (context->kernel[slot].phase == ELF_KERNEL_FREE) {
            context->kernel[slot].phase = ELF_KERNEL_RESERVED;
            context->kernel[slot].reservation = ++context->last_reservation;
            context->kernel_slot = slot;
            context->supplied_slot = slot;
            context->supplied_generation = context->kernel[slot].generation;
            memset(result, 0, sizeof(*result));
            result->version = MICROS_PM_RESERVATION_VERSION;
            result->size = MICROS_PM_RESERVATION_SIZE;
            result->transaction = context->last_reservation;
            return MICROS_SYSCALL_ABI_OK;
        }
    }
    return MICROS_SYSCALL_ABI_CAPACITY;
}

static micros_runtime_result_t model_abort_reserved(void *opaque, uint64_t reservation)
{
    struct elf_spawn_model_context *context = opaque;
    struct elf_model_kernel_process *process = current_process(context);
    uint32_t generation;

    if (!elf_model_record_stage(context, ELF_STAGE_ABORT_RESERVED)
        || !elf_model_kernel_require(context, process != NULL
            && process->phase == ELF_KERNEL_RESERVED && process->reservation == reservation)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    generation = process->generation + 1;
    memset(process, 0, sizeof(*process));
    process->generation = generation;
    context->kernel_slot = ELF_MODEL_NO_SLOT;
    return MICROS_SYSCALL_ABI_OK;
}

static bool prepared_identity_matches(
    const struct elf_model_kernel_process *process, uint64_t reservation,
    uint64_t load, uint64_t mapping, uint64_t prepared
)
{
    return process != NULL && process->phase == ELF_KERNEL_PREPARED
        && process->reservation == reservation && process->load == load
        && process->mapping == mapping && process->prepared == prepared;
}

static micros_runtime_result_t model_prepare(
    void *opaque, uint64_t reservation, uint64_t load, uint64_t mapping,
    struct micros_pm_prepared_result *result
)
{
    struct elf_spawn_model_context *context = opaque;
    struct elf_model_kernel_process *process = current_process(context);

    if (!elf_model_record_stage(context, ELF_STAGE_PREPARE)
        || !elf_model_kernel_require(context, process != NULL
            && process->phase == ELF_KERNEL_FROZEN && process->reservation == reservation
            && process->load == load && process->mapping == mapping)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (context->operation == ELF_MODEL_PREPARE_FAILURE) {
        return MICROS_SYSCALL_ABI_CAPACITY;
    }
    process->phase = ELF_KERNEL_PREPARED;
    process->prepared = ++context->last_prepared;
    memset(result, 0, sizeof(*result));
    result->version = MICROS_PM_PREPARED_VERSION;
    result->size = MICROS_PM_PREPARED_SIZE;
    result->prepared_token = process->prepared;
    result->endpoint = process->generation << MICROS_ENDPOINT_SLOT_BITS | (uint32_t)context->kernel_slot;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t model_abort_prepared(
    void *opaque, uint64_t reservation, uint64_t load, uint64_t mapping, uint64_t prepared
)
{
    struct elf_spawn_model_context *context = opaque;
    struct elf_model_kernel_process *process = current_process(context);

    if (!elf_model_record_stage(context, ELF_STAGE_ABORT_PREPARED)
        || !elf_model_kernel_require(context,
            prepared_identity_matches(process, reservation, load, mapping, prepared))) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    process->phase = ELF_KERNEL_FROZEN;
    process->prepared = 0;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t model_activate(
    void *opaque, uint64_t reservation, uint64_t load, uint64_t mapping, uint64_t prepared
)
{
    struct elf_spawn_model_context *context = opaque;
    struct elf_model_kernel_process *process = current_process(context);

    if (!elf_model_record_stage(context, ELF_STAGE_ACTIVATE)
        || !elf_model_kernel_require(context,
            prepared_identity_matches(process, reservation, load, mapping, prepared))) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (context->operation == ELF_MODEL_ACTIVATE_FAILURE) {
        return MICROS_SYSCALL_ABI_CAPACITY;
    }
    process->phase = ELF_KERNEL_ACTIVE;
    process->prepared = 0;
    process->reservation = 0;
    context->kernel_slot = ELF_MODEL_NO_SLOT;
    elf_model_cancel_at(context, ELF_STAGE_ACTIVATE);
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t model_exit_begin(
    void *opaque, micros_endpoint_t endpoint, struct micros_pm_exit_begin_result *result
)
{
    struct elf_spawn_model_context *context = opaque;
    size_t slot = endpoint & UINT32_C(0xfff);
    struct elf_model_kernel_process *process;

    if (!elf_model_record_stage(context, ELF_STAGE_EXIT_BEGIN)
        || !elf_model_kernel_require(context, slot < MICROS_PROCESS_CAPACITY
            && context->kernel_slot == ELF_MODEL_NO_SLOT)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    process = &context->kernel[slot];
    if (!elf_model_kernel_require(context, process->phase == ELF_KERNEL_ACTIVE
        && process->generation == endpoint >> MICROS_ENDPOINT_SLOT_BITS)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    process->phase = ELF_KERNEL_STOPPED;
    process->exit = ++context->last_exit;
    context->kernel_slot = slot;
    memset(result, 0, sizeof(*result));
    result->version = MICROS_PM_EXIT_BEGIN_VERSION;
    result->size = MICROS_PM_EXIT_BEGIN_SIZE;
    result->exit_token = process->exit;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t model_exit_detach(
    void *opaque, uint64_t exit_token, micros_endpoint_t endpoint
)
{
    struct elf_spawn_model_context *context = opaque;
    struct elf_model_kernel_process *process = current_process(context);

    if (!elf_model_record_stage(context, ELF_STAGE_EXIT_DETACH)
        || !elf_model_kernel_require(context, process != NULL
            && process->phase == ELF_KERNEL_STOPPED && process->exit == exit_token
            && endpoint == (process->generation << MICROS_ENDPOINT_SLOT_BITS | context->kernel_slot))) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    process->phase = ELF_KERNEL_DETACHED;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t model_exit_release(void *opaque, uint64_t exit_token)
{
    struct elf_spawn_model_context *context = opaque;
    struct elf_model_kernel_process *process = current_process(context);
    uint32_t generation;

    if (!elf_model_record_stage(context, ELF_STAGE_EXIT_RELEASE)
        || !elf_model_kernel_require(context, process != NULL
            && process->phase == ELF_KERNEL_EMPTY && process->exit == exit_token
            && process->page_count == 0 && process->load == 0 && process->mapping == 0)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    generation = process->generation + 1;
    memset(process, 0, sizeof(*process));
    process->generation = generation;
    context->kernel_slot = ELF_MODEL_NO_SLOT;
    return MICROS_SYSCALL_ABI_OK;
}

const struct micros_pm_coordinator_backend *elf_model_pm_backend(void)
{
    static const struct micros_pm_coordinator_backend backend = {
        .reserve = model_reserve,
        .abort_reserved = model_abort_reserved,
        .prepare_execution = model_prepare,
        .abort_prepared = model_abort_prepared,
        .activate = model_activate,
        .exit_begin = model_exit_begin,
        .exit_detach = model_exit_detach,
        .release_exited_process = model_exit_release,
        .grant_create = elf_model_grant_create,
        .grant_revoke = elf_model_grant_revoke,
        .grant_copy_from = elf_model_parent_copy,
        .call = elf_model_call,
        .reply = elf_model_reply,
    };

    return &backend;
}
