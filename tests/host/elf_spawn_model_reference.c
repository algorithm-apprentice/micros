#include "tests/host/elf_spawn_model_internal.h"

static size_t reference_running_count(const struct elf_reference_state *reference)
{
    size_t count = 0;
    size_t slot;

    for (slot = 0; slot < MICROS_PM_PROCESS_CAPACITY; ++slot) {
        count += reference->processes[slot].state == ELF_REFERENCE_RUNNING;
    }
    return count;
}

struct elf_model_spawn_plan elf_reference_plan_spawn(
    const struct elf_spawn_model_context *context, enum elf_model_operation operation
)
{
    struct elf_model_spawn_plan plan = {.child_slot = ELF_MODEL_NO_SLOT};
    size_t slot;

    if (operation == ELF_MODEL_BAD_PARENT_GRANT) {
        plan.result = MICROS_PM_RESULT_GRANT;
        return plan;
    }
    for (slot = 0; slot < MICROS_PM_PROCESS_CAPACITY; ++slot) {
        if (context->reference.processes[slot].state == ELF_REFERENCE_FREE) {
            plan.child_slot = slot;
            break;
        }
    }
    plan.result = MICROS_PM_RESULT_NO_SPACE;
    if (plan.child_slot == ELF_MODEL_NO_SLOT) {
        return plan;
    }
    plan.consumed = true;
    plan.forward_count = 1;
    if (operation == ELF_MODEL_RESERVE_FAILURE) {
        return plan;
    }
    plan.reserved = true;
    plan.forward_count = 2;
    if (operation == ELF_MODEL_GRANT_FAILURE) {
        return plan;
    }
    plan.forward_count = 4;
    if (operation == ELF_MODEL_MISSING_PATH) {
        plan.result = MICROS_PM_RESULT_NOT_FOUND;
        return plan;
    }
    plan.image = true;
    plan.forward_count = 5;
    if (operation == ELF_MODEL_BAD_EXECUTABLE) {
        plan.result = MICROS_PM_RESULT_EXECUTABLE;
        return plan;
    }
    if (reference_running_count(&context->reference) * 8 + 8
        > TEST_MANAGED_FRAME_COUNT - TEST_FIRST_FREE_FRAME) {
        return plan;
    }
    plan.frozen = true;
    if (operation == ELF_MODEL_CANCEL_FROZEN) {
        plan.parent_lost = true;
        return plan;
    }
    plan.forward_count = 6;
    if (operation == ELF_MODEL_BAD_DESCRIPTOR) {
        plan.result = MICROS_PM_RESULT_DESCRIPTOR;
        return plan;
    }
    plan.descriptors = true;
    plan.forward_count = 7;
    if (operation == ELF_MODEL_PREPARE_FAILURE) {
        return plan;
    }
    plan.prepared = true;
    plan.hidden = true;
    plan.forward_count = 9;
    if (operation == ELF_MODEL_ACTIVATE_FAILURE) {
        return plan;
    }
    plan.active = true;
    plan.parent_lost = operation == ELF_MODEL_CANCEL_ACTIVE;
    plan.forward_count = 11;
    plan.result = MICROS_PM_RESULT_OK;
    return plan;
}

void elf_reference_spawn_trace(struct elf_spawn_model_context *context)
{
    static const uint32_t forward[] = {
        ELF_STAGE_RESERVE, ELF_STAGE_REQUEST_GRANT, ELF_STAGE_IMAGE,
        ELF_STAGE_REVOKE_REQUEST, ELF_STAGE_LOAD, ELF_STAGE_DESCRIPTORS,
        ELF_STAGE_PREPARE, ELF_STAGE_HIDDEN, ELF_STAGE_ACTIVATE,
        ELF_STAGE_COMMIT_LOAD, ELF_STAGE_RELEASE_IMAGE,
    };
    const struct elf_model_spawn_plan *plan = &context->spawn_plan;
    size_t index;

    for (index = 0; index < plan->forward_count; ++index) {
        context->expected_stages[context->expected_stage_count++] = forward[index];
    }
    if (plan->active) {
        return;
    }
    if (plan->hidden) {
        context->expected_stages[context->expected_stage_count++] = ELF_STAGE_REVERT_HIDDEN;
    }
    if (plan->prepared) {
        context->expected_stages[context->expected_stage_count++] = ELF_STAGE_ABORT_PREPARED;
    }
    if (plan->descriptors) {
        context->expected_stages[context->expected_stage_count++] = ELF_STAGE_ABORT_DESCRIPTORS;
    }
    if (plan->frozen) {
        context->expected_stages[context->expected_stage_count++] = ELF_STAGE_ABORT_LOAD;
    }
    if (plan->image) {
        context->expected_stages[context->expected_stage_count++] = ELF_STAGE_RELEASE_IMAGE;
    }
    if (plan->reserved) {
        context->expected_stages[context->expected_stage_count++] = ELF_STAGE_ABORT_RESERVED;
    }
}

static void reference_clear_process(struct elf_reference_process *process)
{
    uint32_t generation = process->generation;

    memset(process, 0, sizeof(*process));
    process->generation = generation;
}

static bool reference_allocate_frames(
    const struct elf_reference_state *reference, uint32_t selected[8]
)
{
    bool used[TEST_MANAGED_FRAME_COUNT] = {false};
    size_t slot;
    size_t index;
    size_t count = 0;

    for (slot = 0; slot < MICROS_PM_PROCESS_CAPACITY; ++slot) {
        const struct elf_reference_process *process = &reference->processes[slot];

        if (process->state == ELF_REFERENCE_RUNNING) {
            for (index = 0; index < 8; ++index) {
                used[process->frames[index]] = true;
            }
        }
    }
    for (index = TEST_FIRST_FREE_FRAME; index < TEST_MANAGED_FRAME_COUNT && count < 8; ++index) {
        if (!used[index]) {
            selected[count++] = (uint32_t)index;
        }
    }
    return count == 8;
}

static size_t reference_allocate_vfs_slot(const struct elf_reference_state *reference)
{
    size_t candidate;
    size_t slot;

    for (candidate = 0; candidate < MICROS_VFS_PROCESS_CAPACITY; ++candidate) {
        bool used = false;

        for (slot = 0; slot < MICROS_PM_PROCESS_CAPACITY; ++slot) {
            if (reference->processes[slot].state == ELF_REFERENCE_RUNNING
                && reference->processes[slot].vfs_slot == candidate) {
                used = true;
            }
        }
        if (!used) {
            return candidate;
        }
    }
    return MICROS_VFS_PROCESS_CAPACITY;
}

bool elf_reference_apply_spawn(struct elf_spawn_model_context *context)
{
    struct elf_reference_state *reference = &context->reference;
    const struct elf_model_spawn_plan *plan = &context->spawn_plan;
    struct elf_reference_process *process;
    uint32_t frames[8];
    size_t vfs_slot;

    elf_reference_spawn_trace(context);
    if (!plan->consumed) {
        return true;
    }
    process = &reference->processes[plan->child_slot];
    ++process->generation;
    ++reference->last_pid;
    ++reference->spawn_tokens;
    reference->image_tokens += plan->image;
    reference->descriptor_tokens += plan->descriptors;
    if (plan->image && !plan->frozen && plan->result == MICROS_PM_RESULT_NO_SPACE) {
        ++context->vm_capacity_rejections;
    }
    if (!plan->active) {
        reference_clear_process(process);
        return true;
    }
    vfs_slot = reference_allocate_vfs_slot(reference);
    if (!reference_allocate_frames(reference, frames)
        || vfs_slot == MICROS_VFS_PROCESS_CAPACITY || context->supplied_slot == ELF_MODEL_NO_SLOT
        || context->supplied_load == 0 || context->supplied_mapping == 0) {
        return false;
    }
    process->state = ELF_REFERENCE_RUNNING;
    process->pid = reference->last_pid;
    process->parent = context->operation == ELF_MODEL_INIT ? ELF_MODEL_NO_SLOT : (uint16_t)context->parent;
    process->kernel_slot = (uint16_t)context->supplied_slot;
    process->kernel_generation = context->supplied_generation;
    process->vfs_slot = (uint16_t)vfs_slot;
    process->load = context->supplied_load;
    process->mapping = context->supplied_mapping;
    memcpy(process->frames, frames, sizeof(frames));
    return true;
}

static size_t reference_lowest_zombie(
    const struct elf_reference_state *reference, size_t parent, uint64_t selector
)
{
    size_t selected = ELF_MODEL_NO_SLOT;
    size_t slot;
    uint64_t lowest = UINT64_MAX;

    for (slot = 0; slot < MICROS_PM_PROCESS_CAPACITY; ++slot) {
        const struct elf_reference_process *process = &reference->processes[slot];

        if (process->state == ELF_REFERENCE_ZOMBIE && process->parent == parent
            && (selector == 0 || selector == process->pid) && process->pid < lowest) {
            selected = slot;
            lowest = process->pid;
        }
    }
    return selected;
}

static void reference_clear_wait(struct elf_reference_process *parent)
{
    parent->waiting = false;
    parent->wait_flags = 0;
    parent->wait_selector = 0;
    parent->wait_token = 0;
}

static void reference_complete_wait(
    struct elf_spawn_model_context *context, size_t parent_slot, size_t child_slot, uint64_t token
)
{
    struct elf_reference_process *parent = &context->reference.processes[parent_slot];
    struct elf_reference_process *child = &context->reference.processes[child_slot];

    context->expected_replies[context->expected_reply_count++] = (struct elf_model_wait_reply){
        .token = token, .pid = child->pid, .status = child->status,
    };
    reference_clear_process(child);
    reference_clear_wait(parent);
}

void elf_reference_apply_exit(struct elf_spawn_model_context *context)
{
    struct elf_reference_process *process = &context->reference.processes[context->parent];
    size_t original_parent = process->parent;
    size_t affected[2] = {0, original_parent};
    size_t count = original_parent == 0 ? 1 : 2;
    size_t slot;
    static const uint32_t stages[] = {
        ELF_STAGE_EXIT_BEGIN, ELF_STAGE_EXIT_VFS, ELF_STAGE_EXIT_DETACH,
        ELF_STAGE_EXIT_VM, ELF_STAGE_EXIT_RELEASE,
    };

    for (slot = 0; slot < sizeof(stages) / sizeof(stages[0]); ++slot) {
        context->expected_stages[context->expected_stage_count++] = stages[slot];
    }
    for (slot = 0; slot < MICROS_PM_PROCESS_CAPACITY; ++slot) {
        struct elf_reference_process *child = &context->reference.processes[slot];

        if (child->state != ELF_REFERENCE_FREE && child->parent == context->parent) {
            child->parent = 0;
        }
    }
    process->state = ELF_REFERENCE_ZOMBIE;
    process->status = (int32_t)context->argument;
    process->load = 0;
    process->mapping = 0;
    reference_clear_wait(process);
    for (slot = 0; slot < count; ++slot) {
        struct elf_reference_process *parent = &context->reference.processes[affected[slot]];
        size_t zombie = reference_lowest_zombie(&context->reference, affected[slot], parent->wait_selector);

        if (parent->waiting && zombie != ELF_MODEL_NO_SLOT) {
            reference_complete_wait(context, affected[slot], zombie, parent->wait_token);
        }
    }
    if (context->expected_reply_count == 2) {
        ++context->double_wait_completions;
    }
}

enum micros_pm_result elf_reference_apply_wait(
    struct elf_spawn_model_context *context, bool *blocked, bool *completed
)
{
    struct elf_reference_process *parent = &context->reference.processes[context->parent];
    size_t zombie;
    size_t slot;
    bool has_child = false;

    *blocked = false;
    *completed = false;
    if (parent->waiting) {
        return MICROS_PM_RESULT_BUSY;
    }
    zombie = reference_lowest_zombie(&context->reference, context->parent, context->argument);
    if (zombie != ELF_MODEL_NO_SLOT) {
        reference_complete_wait(context, context->parent, zombie, context->operation_reply);
        *completed = true;
        return MICROS_PM_RESULT_OK;
    }
    for (slot = 0; slot < MICROS_PM_PROCESS_CAPACITY; ++slot) {
        const struct elf_reference_process *child = &context->reference.processes[slot];

        has_child |= child->state != ELF_REFERENCE_FREE && child->parent == context->parent
            && (context->argument == 0 || context->argument == child->pid);
    }
    if (!has_child) {
        return MICROS_PM_RESULT_NO_CHILD;
    }
    if ((context->wait_flags & MICROS_PM_WAIT_NOHANG) == 0) {
        parent->waiting = true;
        parent->wait_flags = context->wait_flags;
        parent->wait_selector = context->argument;
        parent->wait_token = context->operation_reply;
        *blocked = true;
    }
    return MICROS_PM_RESULT_OK;
}
