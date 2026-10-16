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
        struct kernel_object_test_model_iteration iteration;
        iteration.value = kernel_object_test_model_random(&random_state);

        switch (iteration.value % 8) {
        case 0:
            if (!kernel_object_test_model_process_create(fixture, &model, step, &iteration)) {
                return false;
            }
            break;
        case 1:
            if (!kernel_object_test_model_process_release(fixture, &model, step, &iteration)) {
                return false;
            }
            break;
        case 2:
            if (!kernel_object_test_model_thread_create(fixture, &model, step, &iteration)) {
                return false;
            }
            break;
        case 3:
            if (!kernel_object_test_model_thread_release(fixture, &model, step, &iteration)) {
                return false;
            }
            break;
        case 4:
            if (!kernel_object_test_model_address_space_attach(fixture, &model, step, &iteration)) {
                return false;
            }
            break;
        case 5:
            if (!kernel_object_test_model_address_space_detach(fixture, &model, step, &iteration)) {
                return false;
            }
            break;
        case 6:
            if (!kernel_object_test_model_context_attach(fixture, &model, step, &iteration)) {
                return false;
            }
            break;
        case 7:
            if (!kernel_object_test_model_context_detach(fixture, &model, step, &iteration)) {
                return false;
            }
            break;
        }

        if (!kernel_object_test_lifecycle_model_matches_registry(fixture, &model, step)) {
            return false;
        }
    }
    return true;
}
