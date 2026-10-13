#include "tests/host/pm_model_test_internal.h"

static uint64_t pm_model_random(uint64_t *state)
{
    uint64_t value = *state;

    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

bool pm_model_failure(
    const char *expression,
    int line,
    uint64_t seed,
    size_t step,
    const uint32_t *trace
)
{
    size_t index;

    fprintf(
        stderr,
        "%s:%d: PM model failure: %s seed=0x%016llx step=%zu\n",
        __FILE__,
        line,
        expression,
        (unsigned long long)seed,
        step
    );
    fprintf(stderr, "operation trace:");
    for (index = 0; index <= step; ++index) {
        if ((index % 8) == 0) {
            fputc('\n', stderr);
        }
        fprintf(stderr, " %08x", trace[index]);
    }
    fputc('\n', stderr);
    return false;
}

bool test_replayable_transition_model(void)
{
    struct pm_model_context state;
    struct pm_model_context *context = &state;

    context->seed = UINT64_C(0x4d4943524f53504d);
    context->random_state = context->seed;
    memset(&context->reference, 0, sizeof(context->reference));
    memset(context->operation_trace, 0, sizeof(context->operation_trace));
    context->reference.wait.pending_slot = PM_MODEL_NO_SLOT;
    context->step = 0;
    PM_MODEL_EXPECT(context, pm_test_initialize_running_init(&context->table, &context->init));
    context->reference.last_pid = 1;
    context->reference.generations[context->init.slot] = context->init.generation;
    context->reference.records[context->init.slot].present = true;
    context->reference.records[context->init.slot].reaper = true;
    context->reference.records[context->init.slot].state = MICROS_PM_PROCESS_RUNNING;
    context->reference.records[context->init.slot].handle = context->init;
    context->reference.records[context->init.slot].pid = 1;
    context->reference.records[context->init.slot].endpoint = UINT32_C(0x00001000);

    for (context->step = 0; context->step < PM_MODEL_STEP_COUNT;
         ++context->step) {
        uint64_t choice = pm_model_random(&context->random_state);
        uint32_t operation = (uint32_t)(choice % 8);

        context->operation_trace[context->step] = operation << 24;
        PM_MODEL_EXPECT(context,
            pm_model_run_operation(context, operation, choice)
        );
        PM_MODEL_EXPECT(context,
            micros_pm_table_validate(&context->table) == MICROS_PM_MODEL_OK
            && pm_model_compare(&context->table, &context->reference, context->init)
        );
    }
    return true;
}
