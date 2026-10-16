#include "kernel_objects_test_internal.h"

bool kernel_object_test_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

size_t kernel_object_test_model_lowest_free_process(
    const struct lifecycle_model *model
)
{
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (!model->processes[index].live) {
            return index;
        }
    }
    return MICROS_PROCESS_CAPACITY;
}

size_t kernel_object_test_model_lowest_free_thread(
    const struct lifecycle_model *model
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        if (!model->threads[index].live) {
            return index;
        }
    }
    return MICROS_THREAD_CAPACITY;
}

struct micros_process_handle kernel_object_test_model_process_handle(
    const struct lifecycle_model *model,
    size_t slot
)
{
    struct micros_process_handle handle = {
        (uint16_t)slot,
        model->processes[slot].generation,
    };

    if (handle.generation == 0) {
        handle.generation = 1;
    }
    return handle;
}

struct micros_thread_handle kernel_object_test_model_thread_handle(
    const struct lifecycle_model *model,
    size_t slot
)
{
    struct micros_thread_handle handle = {
        (uint16_t)slot,
        model->threads[slot].generation,
    };

    if (handle.generation == 0) {
        handle.generation = 1;
    }
    return handle;
}

bool kernel_object_test_model_reports_expected_error(
    enum micros_kernel_object_error expected,
    enum micros_kernel_object_error actual,
    size_t step,
    const char *operation
)
{
    if (expected == actual) {
        return true;
    }
    fprintf(
        stderr,
        "seeded model seed=0x%08x step=%zu operation=%s "
        "expected=%d actual=%d\n",
        UINT32_C(0x7a31d4c9),
        step,
        operation,
        (int)expected,
        (int)actual
    );
    return false;
}
