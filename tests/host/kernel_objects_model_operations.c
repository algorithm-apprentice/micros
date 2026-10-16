#include "kernel_objects_test_internal.h"

uint32_t kernel_object_test_model_random(uint32_t *state)
{
    uint32_t value = *state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

bool kernel_object_test_model_process_create(
    struct micros_kernel_objects *fixture,
    struct lifecycle_model *model,
    size_t step,
    struct kernel_object_test_model_iteration *iteration
)
{
    struct micros_process_handle output = {
        UINT16_MAX,
        UINT32_MAX,
    };

    iteration->slot = kernel_object_test_model_lowest_free_process(model);
    iteration->expected = (
        iteration->slot == MICROS_PROCESS_CAPACITY
            ? MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED
            : MICROS_KERNEL_OBJECT_OK
    );
    iteration->actual = micros_process_create(fixture, &output);
    if (
        !kernel_object_test_model_reports_expected_error(
            iteration->expected,
            iteration->actual,
            step,
            "process-create"
        )
    ) {
        return false;
    }
    if (iteration->expected == MICROS_KERNEL_OBJECT_OK) {
        ++model->processes[iteration->slot].generation;
        model->processes[iteration->slot].live = true;
        ++model->process_count;
        if (
            output.slot != iteration->slot
            || output.generation
                != model->processes[iteration->slot].generation
        ) {
            return false;
        }
    } else if (
        output.slot != UINT16_MAX
        || output.generation != UINT32_MAX
    ) {
        return false;
    }
    return true;
}

bool kernel_object_test_model_process_release(
    struct micros_kernel_objects *fixture,
    struct lifecycle_model *model,
    size_t step,
    struct kernel_object_test_model_iteration *iteration
)
{
    struct micros_process_handle handle;

    iteration->slot = (iteration->value >> 8) % MICROS_PROCESS_CAPACITY;
    handle = kernel_object_test_model_process_handle(model, iteration->slot);
    if (!model->processes[iteration->slot].live) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
    } else if (
        model->processes[iteration->slot].thread_count != 0
        || model->processes[iteration->slot].address_space_root != 0
    ) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
    } else {
        iteration->expected = MICROS_KERNEL_OBJECT_OK;
    }
    iteration->actual = micros_process_release(fixture, handle);
    if (
        !kernel_object_test_model_reports_expected_error(
            iteration->expected,
            iteration->actual,
            step,
            "process-release"
        )
    ) {
        return false;
    }
    if (iteration->expected == MICROS_KERNEL_OBJECT_OK) {
        model->processes[iteration->slot].live = false;
        model->processes[iteration->slot].address_space_root = 0;
        --model->process_count;
    }
    return true;
}

bool kernel_object_test_model_thread_create(
    struct micros_kernel_objects *fixture,
    struct lifecycle_model *model,
    size_t step,
    struct kernel_object_test_model_iteration *iteration
)
{
    size_t owner_slot =
        (iteration->value >> 8) % MICROS_PROCESS_CAPACITY;
    struct micros_process_handle owner =
        kernel_object_test_model_process_handle(model, owner_slot);
    struct micros_thread_handle output = {
        UINT16_MAX,
        UINT32_MAX,
    };

    iteration->slot = kernel_object_test_model_lowest_free_thread(model);
    if (!model->processes[owner_slot].live) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
    } else if (
        model->processes[owner_slot].thread_count >= 3
    ) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_THREAD_LIMIT;
    } else if (iteration->slot == MICROS_THREAD_CAPACITY) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED;
    } else {
        iteration->expected = MICROS_KERNEL_OBJECT_OK;
    }
    iteration->actual = micros_thread_create(fixture, owner, &output);
    if (
        !kernel_object_test_model_reports_expected_error(
            iteration->expected,
            iteration->actual,
            step,
            "thread-create"
        )
    ) {
        return false;
    }
    if (iteration->expected == MICROS_KERNEL_OBJECT_OK) {
        ++model->threads[iteration->slot].generation;
        model->threads[iteration->slot].live = true;
        model->threads[iteration->slot].owner = owner;
        ++model->processes[owner_slot].thread_count;
        ++model->thread_count;
        if (
            output.slot != iteration->slot
            || output.generation
                != model->threads[iteration->slot].generation
        ) {
            return false;
        }
    } else if (
        output.slot != UINT16_MAX
        || output.generation != UINT32_MAX
    ) {
        return false;
    }
    return true;
}

bool kernel_object_test_model_thread_release(
    struct micros_kernel_objects *fixture,
    struct lifecycle_model *model,
    size_t step,
    struct kernel_object_test_model_iteration *iteration
)
{
    struct micros_thread_handle handle;

    iteration->slot = (iteration->value >> 8) % MICROS_THREAD_CAPACITY;
    handle = kernel_object_test_model_thread_handle(model, iteration->slot);
    if (!model->threads[iteration->slot].live) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
    } else if (model->threads[iteration->slot].context_attached) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
    } else {
        iteration->expected = MICROS_KERNEL_OBJECT_OK;
    }
    iteration->actual = micros_thread_release(fixture, handle);
    if (
        !kernel_object_test_model_reports_expected_error(
            iteration->expected,
            iteration->actual,
            step,
            "thread-release"
        )
    ) {
        return false;
    }
    if (iteration->expected == MICROS_KERNEL_OBJECT_OK) {
        size_t owner_slot =
            model->threads[iteration->slot].owner.slot;

        model->threads[iteration->slot].live = false;
        model->threads[iteration->slot].owner.generation = 0;
        model->threads[iteration->slot].owner.slot = 0;
        model->threads[iteration->slot].context_attached = false;
        model->threads[iteration->slot].kernel_stack_bottom = 0;
        model->threads[iteration->slot].kernel_stack_top = 0;
        memset(
            &model->threads[iteration->slot].context,
            0,
            sizeof(model->threads[iteration->slot].context)
        );
        --model->processes[owner_slot].thread_count;
        --model->thread_count;
    }
    return true;
}

bool kernel_object_test_model_address_space_attach(
    struct micros_kernel_objects *fixture,
    struct lifecycle_model *model,
    size_t step,
    struct kernel_object_test_model_iteration *iteration
)
{
    struct micros_process_handle handle;
    uintptr_t root;

    iteration->slot = (iteration->value >> 8) % MICROS_PROCESS_CAPACITY;
    handle = kernel_object_test_model_process_handle(model, iteration->slot);
    root = ((uintptr_t)iteration->slot + 1) * UINT64_C(0x1000);
    if (!model->processes[iteration->slot].live) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
    } else if (
        model->processes[iteration->slot].address_space_root != 0
    ) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
    } else {
        iteration->expected = MICROS_KERNEL_OBJECT_OK;
    }
    iteration->actual = micros_process_attach_address_space(
        fixture,
        handle,
        root
    );
    if (
        !kernel_object_test_model_reports_expected_error(
            iteration->expected,
            iteration->actual,
            step,
            "address-space-attach"
        )
    ) {
        return false;
    }
    if (iteration->expected == MICROS_KERNEL_OBJECT_OK) {
        model->processes[iteration->slot].address_space_root = root;
    }
    return true;
}

bool kernel_object_test_model_address_space_detach(
    struct micros_kernel_objects *fixture,
    struct lifecycle_model *model,
    size_t step,
    struct kernel_object_test_model_iteration *iteration
)
{
    struct micros_process_handle handle;
    uintptr_t expected_root;

    iteration->slot = (iteration->value >> 8) % MICROS_PROCESS_CAPACITY;
    handle = kernel_object_test_model_process_handle(model, iteration->slot);
    expected_root =
        model->processes[iteration->slot].address_space_root;
    if (!model->processes[iteration->slot].live) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
    } else if (
        expected_root == 0
        || model->processes[iteration->slot].thread_count != 0
    ) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
    } else {
        if (((iteration->value >> 16) & 1U) != 0) {
            ++expected_root;
            iteration->expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
        } else {
            iteration->expected = MICROS_KERNEL_OBJECT_OK;
        }
    }
    iteration->actual = micros_process_detach_address_space(
        fixture,
        handle,
        expected_root
    );
    if (
        !kernel_object_test_model_reports_expected_error(
            iteration->expected,
            iteration->actual,
            step,
            "address-space-detach"
        )
    ) {
        return false;
    }
    if (iteration->expected == MICROS_KERNEL_OBJECT_OK) {
        model->processes[iteration->slot].address_space_root = 0;
    }
    return true;
}

bool kernel_object_test_model_context_attach(
    struct micros_kernel_objects *fixture,
    struct lifecycle_model *model,
    size_t step,
    struct kernel_object_test_model_iteration *iteration
)
{
    struct micros_thread_handle handle;
    struct micros_user_context context =
        kernel_object_test_context_pattern(UINT64_C(0x30000000) + step);
    uintptr_t stack_bottom;
    uintptr_t stack_top;

    iteration->slot = (iteration->value >> 8) % MICROS_THREAD_CAPACITY;
    handle = kernel_object_test_model_thread_handle(model, iteration->slot);
    stack_bottom = UINT64_C(0x01000000)
        + (iteration->slot * UINT64_C(0x00008000));
    stack_top = stack_bottom
        + MICROS_THREAD_KERNEL_STACK_SIZE;
    if (!model->threads[iteration->slot].live) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
    } else if (model->threads[iteration->slot].context_attached) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
    } else {
        iteration->expected = MICROS_KERNEL_OBJECT_OK;
    }
    iteration->actual = micros_thread_attach_execution_context(
        fixture,
        handle,
        stack_bottom,
        stack_top,
        &context
    );
    if (
        !kernel_object_test_model_reports_expected_error(
            iteration->expected,
            iteration->actual,
            step,
            "context-attach"
        )
    ) {
        return false;
    }
    if (iteration->expected == MICROS_KERNEL_OBJECT_OK) {
        model->threads[iteration->slot].context_attached = true;
        model->threads[iteration->slot].kernel_stack_bottom =
            stack_bottom;
        model->threads[iteration->slot].kernel_stack_top = stack_top;
        model->threads[iteration->slot].context = context;
    }
    return true;
}

bool kernel_object_test_model_context_detach(
    struct micros_kernel_objects *fixture,
    struct lifecycle_model *model,
    size_t step,
    struct kernel_object_test_model_iteration *iteration
)
{
    struct micros_thread_handle handle;

    iteration->slot = (iteration->value >> 8) % MICROS_THREAD_CAPACITY;
    handle = kernel_object_test_model_thread_handle(model, iteration->slot);
    if (!model->threads[iteration->slot].live) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
    } else if (!model->threads[iteration->slot].context_attached) {
        iteration->expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
    } else {
        iteration->expected = MICROS_KERNEL_OBJECT_OK;
    }
    iteration->actual = micros_thread_detach_execution_context(
        fixture,
        handle
    );
    if (
        !kernel_object_test_model_reports_expected_error(
            iteration->expected,
            iteration->actual,
            step,
            "context-detach"
        )
    ) {
        return false;
    }
    if (iteration->expected == MICROS_KERNEL_OBJECT_OK) {
        model->threads[iteration->slot].context_attached = false;
        model->threads[iteration->slot].kernel_stack_bottom = 0;
        model->threads[iteration->slot].kernel_stack_top = 0;
        memset(
            &model->threads[iteration->slot].context,
            0,
            sizeof(model->threads[iteration->slot].context)
        );
    }
    return true;
}
