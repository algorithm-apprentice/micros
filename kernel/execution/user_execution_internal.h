#ifndef MICROS_KERNEL_USER_EXECUTION_INTERNAL_H
#define MICROS_KERNEL_USER_EXECUTION_INTERNAL_H

#include "micros/user_execution.h"

enum micros_user_execution_error
micros_user_execution_map_object_error(
    enum micros_kernel_object_error error
);

enum micros_user_execution_error
micros_user_execution_resolve_thread_and_process(
    struct micros_thread_handle thread_handle,
    struct micros_kernel_objects **objects,
    const struct micros_thread **thread,
    const struct micros_process **process
);

enum micros_user_execution_error
micros_user_execution_prepare_post_handoff_context(
    struct micros_process_handle process,
    const struct micros_user_context *initial_context,
    struct micros_user_context *prepared
);

enum micros_user_execution_error
micros_user_execution_prepare_bootstrap_context(
    struct micros_process_handle process,
    const struct micros_user_context *initial_context,
    struct micros_user_context *prepared
);

enum micros_user_execution_error
micros_user_execution_validate_trap_capture(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame,
    struct micros_kernel_objects **objects,
    struct micros_thread_handle *thread
);

#endif
