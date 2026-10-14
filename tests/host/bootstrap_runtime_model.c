#include "bootstrap_runtime_model_internal.h"

#include "bootstrap_manifest_test_internal.h"

struct model_transition {
    enum model_operation operation;
    uint32_t service_id;
    uint64_t now;
    enum micros_bootstrap_error expected_error;
    enum micros_bootstrap_error actual_error;
    bool compare_error;
};

static uint64_t model_random(uint64_t *state)
{
    uint64_t value = *state;

    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

static void print_model_failure(
    uint64_t seed,
    size_t step,
    const struct model_trace_entry trace[16],
    size_t trace_count,
    const struct reference_runtime *reference,
    const struct micros_bootstrap_runtime *runtime
)
{
    size_t index;
    size_t start = trace_count > 16 ? trace_count - 16 : 0;

    fprintf(
        stderr,
        "bootstrap model seed=0x%016llx step=%zu "
        "manifest={1,2} order={1,2} "
        "reference={phase=%d,next=%u,starting=%u} "
        "production={phase=%d,next=%u,starting=%u}\n",
        (unsigned long long)seed,
        step,
        (int)reference->phase,
        (unsigned int)reference->next_order_index,
        reference->starting_service_id,
        (int)runtime->phase,
        (unsigned int)runtime->next_order_index,
        runtime->starting_service_id
    );
    for (index = start; index < trace_count; ++index) {
        const struct model_trace_entry *entry =
            &trace[index % 16];

        fprintf(
            stderr,
            "trace step=%zu op=%d service=%u now=%llu "
            "error=%d/%d class=%d/%d\n",
            entry->step,
            (int)entry->operation,
            entry->service_id,
            (unsigned long long)entry->now,
            (int)entry->expected_error,
            (int)entry->actual_error,
            (int)entry->expected_class,
            (int)entry->actual_class
        );
    }
}

static bool dispatch_reset_initialize_and_release(
    size_t step,
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_manifest_plan *plan,
    struct reference_runtime *reference,
    struct micros_bootstrap_runtime *runtime,
    struct model_transition *transition
)
{
    switch (step % 16) {
    case 0:
        memset(reference, 0, sizeof(*reference));
        memset(runtime, 0, sizeof(*runtime));
        transition->operation = MODEL_RESET;
        transition->compare_error = false;
        break;
    case 1:
    case 2:
    case 13:
        transition->operation = MODEL_INITIALIZE;
        transition->expected_error =
            bootstrap_runtime_reference_initialize(reference);
        transition->actual_error = micros_bootstrap_runtime_initialize(
            manifest,
            plan,
            runtime
        );
        break;
    case 3:
        transition->operation = MODEL_RELEASE;
        transition->service_id = 99;
        transition->expected_error = bootstrap_runtime_reference_release(
            reference,
            transition->service_id,
            10
        );
        transition->actual_error = micros_bootstrap_runtime_release(
            runtime,
            transition->service_id,
            10
        );
        break;
    case 4:
        transition->operation = MODEL_RELEASE;
        transition->service_id = 1;
        transition->expected_error = bootstrap_runtime_reference_release(
            reference,
            transition->service_id,
            10
        );
        transition->actual_error = micros_bootstrap_runtime_release(
            runtime,
            transition->service_id,
            10
        );
        break;
    case 5:
        transition->operation = MODEL_RELEASE;
        transition->service_id = 2;
        transition->now = ((step / 16) & 1) != 0
            ? UINT64_MAX - UINT64_C(50)
            : UINT64_C(10);
        transition->expected_error = bootstrap_runtime_reference_release(
            reference,
            transition->service_id,
            transition->now
        );
        transition->actual_error = micros_bootstrap_runtime_release(
            runtime,
            transition->service_id,
            transition->now
        );
        break;
    default:
        return false;
    }
    return true;
}

static bool dispatch_repeated_release_and_foreign_ready(
    size_t cycle_step,
    struct reference_runtime *reference,
    struct micros_bootstrap_runtime *runtime,
    struct model_transition *transition
)
{
    switch (cycle_step) {
    case 6:
        transition->operation = MODEL_RELEASE;
        transition->service_id = 2;
        transition->now = 10;
        transition->expected_error = bootstrap_runtime_reference_release(
            reference,
            transition->service_id,
            transition->now
        );
        transition->actual_error = micros_bootstrap_runtime_release(
            runtime,
            transition->service_id,
            transition->now
        );
        break;
    case 7:
        transition->operation = MODEL_ACCEPT_READY;
        transition->service_id = 99;
        transition->now = 20;
        transition->expected_error = bootstrap_runtime_reference_accept_ready(
            reference,
            transition->service_id,
            transition->now
        );
        transition->actual_error = micros_bootstrap_runtime_accept_ready(
            runtime,
            transition->service_id,
            transition->now
        );
        break;
    case 8:
        transition->operation = MODEL_ACCEPT_READY;
        transition->service_id = 1;
        transition->now = 20;
        transition->expected_error = bootstrap_runtime_reference_accept_ready(
            reference,
            transition->service_id,
            transition->now
        );
        transition->actual_error = micros_bootstrap_runtime_accept_ready(
            runtime,
            transition->service_id,
            transition->now
        );
        break;
    default:
        return false;
    }
    return true;
}

static bool dispatch_deadline_ready_and_sealing(
    size_t cycle_step,
    struct reference_runtime *reference,
    struct micros_bootstrap_runtime *runtime,
    struct model_transition *transition
)
{
    switch (cycle_step) {
    case 9:
    case 10:
    case 11:
        transition->operation = MODEL_ACCEPT_READY;
        transition->service_id = 2;
        transition->now = cycle_step == 9 ? 110 : 109;
        transition->expected_error = bootstrap_runtime_reference_accept_ready(
            reference,
            transition->service_id,
            transition->now
        );
        transition->actual_error = micros_bootstrap_runtime_accept_ready(
            runtime,
            transition->service_id,
            transition->now
        );
        break;
    case 12:
    case 15:
        transition->operation = MODEL_COMPLETE;
        transition->expected_error =
            bootstrap_runtime_reference_complete(reference);
        transition->actual_error =
            micros_bootstrap_runtime_complete(runtime);
        break;
    case 14:
        transition->operation = MODEL_RELEASE;
        transition->service_id = 2;
        transition->expected_error = bootstrap_runtime_reference_release(
            reference,
            transition->service_id,
            0
        );
        transition->actual_error = micros_bootstrap_runtime_release(
            runtime,
            transition->service_id,
            0
        );
        break;
    default:
        return false;
    }
    return true;
}

static bool classify_trace_and_compare(
    uint64_t seed,
    size_t step,
    uint64_t random_value,
    const struct model_transition *transition,
    const struct reference_runtime *reference,
    const struct micros_bootstrap_runtime *runtime,
    struct micros_bootstrap_runtime *expected_runtime,
    struct model_trace_entry trace[16]
)
{
    uint32_t ready_source;
    uint32_t ready_payload;
    bool ready_shape;
    enum micros_bootstrap_ready_class expected_class;
    enum micros_bootstrap_ready_class actual_class;
    enum micros_bootstrap_error expected_error = transition->expected_error;
    enum micros_bootstrap_error actual_error = transition->actual_error;
    bool compare_error = transition->compare_error;

    switch (random_value & UINT64_C(3)) {
    case 0:
        ready_source = 0;
        break;
    case 1:
        ready_source = 1;
        break;
    case 2:
        ready_source = 2;
        break;
    default:
        ready_source = 99;
        break;
    }
    ready_payload = (random_value & UINT64_C(4)) != 0
        ? ready_source
        : (uint32_t)(random_value >> 32);
    ready_shape = (random_value & UINT64_C(8)) != 0;
    expected_class = bootstrap_runtime_reference_classify_ready(
        reference,
        ready_source,
        ready_payload,
        ready_shape
    );
    actual_class = micros_bootstrap_classify_ready(
        runtime,
        ready_source,
        ready_payload,
        ready_shape
    );
    trace[step % 16] = (struct model_trace_entry){
        .step = step,
        .operation = transition->operation,
        .service_id = transition->service_id,
        .now = transition->now,
        .expected_error = expected_error,
        .actual_error = actual_error,
        .expected_class = expected_class,
        .actual_class = actual_class,
    };
    bootstrap_runtime_project_reference(reference, expected_runtime);
    if (
        (compare_error && actual_error != expected_error)
        || actual_class != expected_class
        || memcmp(
            runtime,
            expected_runtime,
            sizeof(*runtime)
        ) != 0
    ) {
        print_model_failure(
            seed,
            step,
            trace,
            step + 1,
            reference,
            runtime
        );
        return false;
    }
    return true;
}

bool test_replayable_runtime_model(void)
{
    static const uint64_t seed = UINT64_C(0x42535452504c4159);
    struct micros_bootstrap_manifest manifest;
    const struct micros_bootstrap_manifest_plan plan = {
        .entry_count = 2,
        .total_user_page_limit = 7,
        .controller_service_id = 1,
        .vm_service_id = 2,
        .ordered_service_ids = {1, 2},
        .ordered_manifest_indices = {1, 0},
    };
    struct reference_runtime reference = {0};
    struct micros_bootstrap_runtime runtime = {0};
    struct micros_bootstrap_runtime expected_runtime;
    struct model_trace_entry trace[16] = {0};
    uint64_t random_state = seed;
    size_t step;

    bootstrap_manifest_test_initialize_manifest(&manifest);
    for (step = 0; step < 4096; ++step) {
        uint64_t random_value = model_random(&random_state);
        size_t cycle_step = step % 16;
        struct model_transition transition = {
            .operation = MODEL_RESET,
            .service_id = 0,
            .now = 0,
            .expected_error = MICROS_BOOTSTRAP_OK,
            .actual_error = MICROS_BOOTSTRAP_OK,
            .compare_error = true,
        };

        if (cycle_step < 6 || cycle_step == 13) {
            if (!dispatch_reset_initialize_and_release(
                step,
                &manifest,
                &plan,
                &reference,
                &runtime,
                &transition
            )) {
                return false;
            }
        } else if (cycle_step < 9) {
            if (!dispatch_repeated_release_and_foreign_ready(
                cycle_step,
                &reference,
                &runtime,
                &transition
            )) {
                return false;
            }
        } else if (!dispatch_deadline_ready_and_sealing(
            cycle_step,
            &reference,
            &runtime,
            &transition
        )) {
            return false;
        }
        if (!classify_trace_and_compare(
            seed,
            step,
            random_value,
            &transition,
            &reference,
            &runtime,
            &expected_runtime,
            trace
        )) {
            return false;
        }
    }
    return true;
}
