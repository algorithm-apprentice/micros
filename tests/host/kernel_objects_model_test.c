#include "kernel_objects_test_internal.h"

bool kernel_object_test_seeded_lifecycle_model(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    enum {
        MODEL_STEPS = 4096,
    };
    struct lifecycle_model model = {0};
    struct micros_hart_handle harts[2];
    uint32_t random_state = UINT32_C(0x7a31d4c9);
    size_t step;

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 3, 2)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(fixture, 0, &harts[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(fixture, 1, &harts[1])
    );

    for (step = 0; step < MODEL_STEPS; ++step) {
        uint32_t value = kernel_object_test_model_random(&random_state);
        size_t slot;
        enum micros_kernel_object_error expected;
        enum micros_kernel_object_error actual;

        switch (value % 8) {
        case 0: {
            struct micros_process_handle output = {
                UINT16_MAX,
                UINT32_MAX,
            };

            slot = kernel_object_test_model_lowest_free_process(&model);
            expected = (
                slot == MICROS_PROCESS_CAPACITY
                    ? MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED
                    : MICROS_KERNEL_OBJECT_OK
            );
            actual = micros_process_create(fixture, &output);
            if (
                !kernel_object_test_model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "process-create"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                ++model.processes[slot].generation;
                model.processes[slot].live = true;
                ++model.process_count;
                if (
                    output.slot != slot
                    || output.generation
                        != model.processes[slot].generation
                ) {
                    return false;
                }
            } else if (
                output.slot != UINT16_MAX
                || output.generation != UINT32_MAX
            ) {
                return false;
            }
            break;
        }
        case 1: {
            struct micros_process_handle handle;

            slot = (value >> 8) % MICROS_PROCESS_CAPACITY;
            handle = kernel_object_test_model_process_handle(&model, slot);
            if (!model.processes[slot].live) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
            } else if (
                model.processes[slot].thread_count != 0
                || model.processes[slot].address_space_root != 0
            ) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
            } else {
                expected = MICROS_KERNEL_OBJECT_OK;
            }
            actual = micros_process_release(fixture, handle);
            if (
                !kernel_object_test_model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "process-release"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                model.processes[slot].live = false;
                model.processes[slot].address_space_root = 0;
                --model.process_count;
            }
            break;
        }
        case 2: {
            size_t owner_slot =
                (value >> 8) % MICROS_PROCESS_CAPACITY;
            struct micros_process_handle owner =
                kernel_object_test_model_process_handle(&model, owner_slot);
            struct micros_thread_handle output = {
                UINT16_MAX,
                UINT32_MAX,
            };

            slot = kernel_object_test_model_lowest_free_thread(&model);
            if (!model.processes[owner_slot].live) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
            } else if (
                model.processes[owner_slot].thread_count >= 3
            ) {
                expected = MICROS_KERNEL_OBJECT_ERROR_THREAD_LIMIT;
            } else if (slot == MICROS_THREAD_CAPACITY) {
                expected = MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED;
            } else {
                expected = MICROS_KERNEL_OBJECT_OK;
            }
            actual = micros_thread_create(fixture, owner, &output);
            if (
                !kernel_object_test_model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "thread-create"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                ++model.threads[slot].generation;
                model.threads[slot].live = true;
                model.threads[slot].owner = owner;
                ++model.processes[owner_slot].thread_count;
                ++model.thread_count;
                if (
                    output.slot != slot
                    || output.generation
                        != model.threads[slot].generation
                ) {
                    return false;
                }
            } else if (
                output.slot != UINT16_MAX
                || output.generation != UINT32_MAX
            ) {
                return false;
            }
            break;
        }
        case 3: {
            struct micros_thread_handle handle;

            slot = (value >> 8) % MICROS_THREAD_CAPACITY;
            handle = kernel_object_test_model_thread_handle(&model, slot);
            if (!model.threads[slot].live) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
            } else if (model.threads[slot].context_attached) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
            } else {
                expected = MICROS_KERNEL_OBJECT_OK;
            }
            actual = micros_thread_release(fixture, handle);
            if (
                !kernel_object_test_model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "thread-release"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                size_t owner_slot =
                    model.threads[slot].owner.slot;

                model.threads[slot].live = false;
                model.threads[slot].owner.generation = 0;
                model.threads[slot].owner.slot = 0;
                model.threads[slot].context_attached = false;
                model.threads[slot].kernel_stack_bottom = 0;
                model.threads[slot].kernel_stack_top = 0;
                memset(
                    &model.threads[slot].context,
                    0,
                    sizeof(model.threads[slot].context)
                );
                --model.processes[owner_slot].thread_count;
                --model.thread_count;
            }
            break;
        }
        case 4: {
            struct micros_process_handle handle;
            uintptr_t root;

            slot = (value >> 8) % MICROS_PROCESS_CAPACITY;
            handle = kernel_object_test_model_process_handle(&model, slot);
            root = ((uintptr_t)slot + 1) * UINT64_C(0x1000);
            if (!model.processes[slot].live) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
            } else if (
                model.processes[slot].address_space_root != 0
            ) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
            } else {
                expected = MICROS_KERNEL_OBJECT_OK;
            }
            actual = micros_process_attach_address_space(
                fixture,
                handle,
                root
            );
            if (
                !kernel_object_test_model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "address-space-attach"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                model.processes[slot].address_space_root = root;
            }
            break;
        }
        case 5: {
            struct micros_process_handle handle;
            uintptr_t expected_root;

            slot = (value >> 8) % MICROS_PROCESS_CAPACITY;
            handle = kernel_object_test_model_process_handle(&model, slot);
            expected_root =
                model.processes[slot].address_space_root;
            if (!model.processes[slot].live) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
            } else if (
                expected_root == 0
                || model.processes[slot].thread_count != 0
            ) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
            } else {
                if (((value >> 16) & 1U) != 0) {
                    ++expected_root;
                    expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
                } else {
                    expected = MICROS_KERNEL_OBJECT_OK;
                }
            }
            actual = micros_process_detach_address_space(
                fixture,
                handle,
                expected_root
            );
            if (
                !kernel_object_test_model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "address-space-detach"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                model.processes[slot].address_space_root = 0;
            }
            break;
        }
        case 6: {
            struct micros_thread_handle handle;
            struct micros_user_context context =
                kernel_object_test_context_pattern(UINT64_C(0x30000000) + step);
            uintptr_t stack_bottom;
            uintptr_t stack_top;

            slot = (value >> 8) % MICROS_THREAD_CAPACITY;
            handle = kernel_object_test_model_thread_handle(&model, slot);
            stack_bottom = UINT64_C(0x01000000)
                + (slot * UINT64_C(0x00008000));
            stack_top = stack_bottom
                + MICROS_THREAD_KERNEL_STACK_SIZE;
            if (!model.threads[slot].live) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
            } else if (model.threads[slot].context_attached) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
            } else {
                expected = MICROS_KERNEL_OBJECT_OK;
            }
            actual = micros_thread_attach_execution_context(
                fixture,
                handle,
                stack_bottom,
                stack_top,
                &context
            );
            if (
                !kernel_object_test_model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "context-attach"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                model.threads[slot].context_attached = true;
                model.threads[slot].kernel_stack_bottom =
                    stack_bottom;
                model.threads[slot].kernel_stack_top = stack_top;
                model.threads[slot].context = context;
            }
            break;
        }
        case 7: {
            struct micros_thread_handle handle;

            slot = (value >> 8) % MICROS_THREAD_CAPACITY;
            handle = kernel_object_test_model_thread_handle(&model, slot);
            if (!model.threads[slot].live) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
            } else if (!model.threads[slot].context_attached) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
            } else {
                expected = MICROS_KERNEL_OBJECT_OK;
            }
            actual = micros_thread_detach_execution_context(
                fixture,
                handle
            );
            if (
                !kernel_object_test_model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "context-detach"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                model.threads[slot].context_attached = false;
                model.threads[slot].kernel_stack_bottom = 0;
                model.threads[slot].kernel_stack_top = 0;
                memset(
                    &model.threads[slot].context,
                    0,
                    sizeof(model.threads[slot].context)
                );
            }
            break;
        }
        }

        if (!kernel_object_test_lifecycle_model_matches_registry(fixture, &model, step)) {
            return false;
        }
    }
    return true;
}
