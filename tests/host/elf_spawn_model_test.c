#include "tests/host/elf_spawn_model_internal.h"

#include <inttypes.h>

void elf_model_report_failure(
    const struct elf_spawn_model_context *context, const char *file, int line,
    const char *expression
)
{
    size_t index;

    fprintf(stderr, "%s:%d: %s; seed=%016" PRIx64 " trace=%016" PRIx64
                    " operation=%zu violation=%u\n",
            file, line, expression, context->seed, context->trace_digest,
            context->trace_count, (unsigned int)context->violation);
    fprintf(stderr, "actual stages:");
    for (index = 0; index < context->stage_count; ++index) {
        fprintf(stderr, " %u", context->stages[index]);
    }
    fprintf(stderr, "\nexpected stages:");
    for (index = 0; index < context->expected_stage_count; ++index) {
        fprintf(stderr, " %u", context->expected_stages[index]);
    }
    fprintf(stderr, "\nPM pid=%" PRIu64 "/%" PRIu64 " VM phase=%u VFS image=%" PRIu64
                    "/%" PRIu64 " descriptor=%" PRIu64 "/%" PRIu64 "\n",
            context->pm.last_pid, context->reference.last_pid, context->vm->state.phase,
            context->vfs.last_image_token, context->reference.image_tokens,
            context->vfs.last_descriptor_token, context->reference.descriptor_tokens);
    for (index = 0; index < context->trace_count; ++index) {
        const struct elf_model_trace_entry *entry = &context->trace[index];

        fprintf(stderr, "%zu: op=%u parent=%zu argument=%016" PRIx64 " random=%016" PRIx64 "\n",
                index, (unsigned int)entry->operation, entry->parent, entry->argument, entry->random);
    }
}

static uint64_t next_random(struct elf_spawn_model_context *context)
{
    uint64_t value = context->random;

    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    context->random = value;
    return value;
}

static size_t select_running(
    const struct elf_reference_state *reference, uint64_t draw, bool allow_init, bool unblocked
)
{
    size_t choices[MICROS_PM_PROCESS_CAPACITY];
    size_t count = 0;
    size_t slot;

    for (slot = allow_init ? 0 : 1; slot < MICROS_PM_PROCESS_CAPACITY; ++slot) {
        if (reference->processes[slot].state == ELF_REFERENCE_RUNNING
            && (!unblocked || !reference->processes[slot].waiting)) {
            choices[count++] = slot;
        }
    }
    return count == 0 ? ELF_MODEL_NO_SLOT : choices[draw % count];
}

static uint64_t select_wait_child(
    const struct elf_reference_state *reference, size_t parent, uint64_t draw
)
{
    size_t choices[MICROS_PM_PROCESS_CAPACITY];
    size_t count = 0;
    size_t slot;

    if (draw % 3 == 0) {
        return 0;
    }
    if (draw % 3 == 1) {
        return reference->last_pid + 1;
    }
    for (slot = 1; slot < MICROS_PM_PROCESS_CAPACITY; ++slot) {
        if (reference->processes[slot].state != ELF_REFERENCE_FREE
            && reference->processes[slot].parent == parent) {
            choices[count++] = slot;
        }
    }
    return count == 0 ? 0 : reference->processes[choices[(draw >> 8) % count]].pid;
}

static bool nested_wait_prelude(struct elf_spawn_model_context *context)
{
    ELF_MODEL_EXPECT(context,
        elf_model_run_operation(context, ELF_MODEL_SPAWN, 0, 0, 0)
        && elf_model_run_operation(context, ELF_MODEL_SPAWN, 1, 0, 0)
        && elf_model_run_operation(context, ELF_MODEL_SPAWN, 2, 0, 0)
        && elf_model_run_operation(context, ELF_MODEL_EXIT, 3, 23, 0)
        && elf_model_run_operation(context, ELF_MODEL_WAIT, 0, 0, 0)
        && elf_model_run_operation(context, ELF_MODEL_WAIT, 1, 3, 0)
        && elf_model_run_operation(context, ELF_MODEL_EXIT, 2, 17, 0)
        && context->double_wait_completions == 1
    );
    return true;
}

static bool run_mixed_operation(struct elf_spawn_model_context *context)
{
    uint64_t draw = next_random(context);
    enum elf_model_operation operation =
        (enum elf_model_operation)(1 + draw % (ELF_MODEL_OPERATION_COUNT - 1));
    size_t parent;
    uint64_t argument = draw >> 16;

    parent = select_running(&context->reference, draw >> 32,
        operation != ELF_MODEL_EXIT, operation <= ELF_MODEL_BAD_PARENT_GRANT || operation == ELF_MODEL_EXIT);
    if (parent == ELF_MODEL_NO_SLOT) {
        operation = ELF_MODEL_SPAWN;
        parent = select_running(&context->reference, draw >> 32, true, true);
    }
    ELF_MODEL_EXPECT(context, parent != ELF_MODEL_NO_SLOT);
    if (operation == ELF_MODEL_WAIT) {
        argument = select_wait_child(&context->reference, parent, draw >> 8);
    }
    return elf_model_run_operation(context, operation, parent, argument, draw);
}

static bool drain_children(struct elf_spawn_model_context *context)
{
    size_t step;
    size_t slot;

    for (step = 0; step < MICROS_PM_PROCESS_CAPACITY * 2; ++step) {
        slot = select_running(&context->reference, 0, false, true);
        if (slot != ELF_MODEL_NO_SLOT) {
            ELF_MODEL_EXPECT(context, elf_model_run_operation(context, ELF_MODEL_EXIT, slot, 31, 0));
            continue;
        }
        for (slot = 1; slot < MICROS_PM_PROCESS_CAPACITY; ++slot) {
            if (context->reference.processes[slot].state != ELF_REFERENCE_FREE) {
                break;
            }
        }
        if (slot == MICROS_PM_PROCESS_CAPACITY) {
            return elf_model_run_operation(context, ELF_MODEL_WAIT, 0, 0, 1);
        }
        ELF_MODEL_EXPECT(context,
            elf_model_run_operation(context, ELF_MODEL_WAIT, 0, 0, 1)
            && !context->reference.processes[0].waiting);
    }
    ELF_MODEL_EXPECT(context, false);
    return false;
}

int main(void)
{
    struct elf_spawn_model_context *context = elf_model_context_get();
    size_t index;

    if (!elf_model_initialize(context)) {
        return 1;
    }
    printf("elf-spawn model seed=%016" PRIx64 " mixed=8192\n", context->seed);
    if (!elf_model_run_operation(context, ELF_MODEL_INIT, 0, 0, 0)
        || !elf_model_oracle_self_test(context) || !nested_wait_prelude(context)) {
        return 1;
    }
    for (index = 0; index < 8192; ++index) {
        if (!run_mixed_operation(context)) {
            return 1;
        }
    }
    if (!drain_children(context)) {
        return 1;
    }
    for (index = 0; index < ELF_MODEL_OPERATION_COUNT; ++index) {
        if (context->coverage[index] == 0) {
            elf_model_report_failure(context, __FILE__, __LINE__, "missing operation coverage");
            return 1;
        }
    }
    for (index = ELF_STAGE_RESERVE; index <= ELF_STAGE_EXIT_RELEASE; ++index) {
        if (context->stage_coverage[index] == 0) {
            elf_model_report_failure(context, __FILE__, __LINE__, "missing collaborator stage coverage");
            return 1;
        }
    }
    if (context->vm_capacity_rejections == 0 || context->double_wait_completions == 0) {
        elf_model_report_failure(context, __FILE__, __LINE__, "missing capacity or two-parent coverage");
        return 1;
    }
    printf("elf-spawn model passed mixed=8192 total=%zu seed=%016" PRIx64
           " trace=%016" PRIx64 " vm-capacity=%zu two-parent=%zu\n",
           context->trace_count, context->seed, context->trace_digest,
           context->vm_capacity_rejections, context->double_wait_completions);
    return 0;
}
