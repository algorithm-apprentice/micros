#ifndef MICROS_KERNEL_OBJECT_MODEL_TEST_INTERNAL_H
#define MICROS_KERNEL_OBJECT_MODEL_TEST_INTERNAL_H

#include "micros/kernel_object_runtime.h"
#include "micros/scheduler_core.h"

struct kernel_object_model_test_work {
    struct micros_kernel_objects *objects;
    struct micros_hart *hart;
    struct micros_hart_handle hart_handle;
    struct micros_process_handle first_process;
    struct micros_process_handle replacement_process;
    struct micros_thread_handle thread;
    struct micros_thread_handle unchanged_thread;
    struct micros_thread_handle current;
    struct micros_scheduler_return_plan plan;
    const struct micros_process *resolved_process;
    enum micros_kernel_object_error error;
};

bool kernel_object_model_test_create_process_and_thread(
    struct kernel_object_model_test_work *work
);

bool kernel_object_model_test_attach_and_select_thread(
    struct kernel_object_model_test_work *work,
    uintptr_t stack_bottom,
    uintptr_t stack_top,
    const struct micros_user_context *context
);

bool kernel_object_model_test_retire_thread_and_process(
    struct kernel_object_model_test_work *work
);

bool kernel_object_model_test_verify_process_reuse(
    struct kernel_object_model_test_work *work
);

#endif
