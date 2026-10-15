#ifndef MICROS_KERNEL_OBJECT_PRIVATE_H
#define MICROS_KERNEL_OBJECT_PRIVATE_H

#include "micros/kernel_objects.h"

#define MICROS_KERNEL_OBJECTS_MAGIC UINT64_C(0x4d4943524f534f42)

bool kernel_object_storage_is_zero(
    const struct micros_kernel_objects *objects
);

bool kernel_object_process_handle_is_valid(
    struct micros_process_handle handle
);

bool kernel_object_thread_handle_is_valid(
    struct micros_thread_handle handle
);

bool kernel_object_hart_handle_is_valid(struct micros_hart_handle handle);

bool kernel_object_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
);

bool kernel_object_thread_handle_is_null(
    struct micros_thread_handle handle
);

bool kernel_object_hart_handle_is_null(struct micros_hart_handle handle);

bool kernel_object_thread_scheduler_metadata_is_zero(
    const struct micros_thread *thread
);

void kernel_object_clear_thread_scheduler_metadata(
    struct micros_thread *thread
);

void kernel_object_clear_thread_ipc_state(struct micros_thread *thread);

void kernel_object_copy_user_context(
    struct micros_user_context *destination,
    const struct micros_user_context *source
);

void kernel_object_clear_user_context(struct micros_user_context *context);

bool kernel_object_user_context_is_zero(
    const struct micros_user_context *context
);

bool kernel_object_trap_anchor_is_zero(
    const struct micros_hart_trap_anchor *trap
);

enum micros_kernel_object_error kernel_object_require_initialized(
    const struct micros_kernel_objects *objects
);

enum micros_kernel_object_error kernel_object_resolve_process_mutable(
    struct micros_kernel_objects *objects,
    struct micros_process_handle handle,
    struct micros_process **process
);

enum micros_kernel_object_error kernel_object_resolve_thread_mutable(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    struct micros_thread **thread
);

enum micros_kernel_object_error kernel_object_resolve_hart_mutable(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle handle,
    struct micros_hart **hart
);

bool kernel_object_timer_state_is_initial(
    const struct micros_hart_timer_state *timer
);

#endif
