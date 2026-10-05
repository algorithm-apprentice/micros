#ifndef MICROS_KERNEL_OBJECTS_H
#define MICROS_KERNEL_OBJECTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    MICROS_PROCESS_CAPACITY = 64,
    MICROS_THREAD_CAPACITY = 128,
    MICROS_HART_CAPACITY = 8,
    MICROS_PROCESS_SLOT_BITS = 12,
    MICROS_PROCESS_GENERATION_BITS = 20,
};

#define MICROS_PROCESS_GENERATION_MAX UINT32_C(0x000fffff)
#define MICROS_PROCESS_ENDPOINT_NONE UINT32_C(0xfffffffe)
#define MICROS_PROCESS_ENDPOINT_ANY UINT32_C(0xffffffff)

struct micros_process_handle {
    uint16_t slot;
    uint32_t generation;
};

struct micros_thread_handle {
    uint16_t slot;
    uint32_t generation;
};

struct micros_hart_handle {
    uint16_t slot;
    uint32_t generation;
};

enum micros_kernel_object_slot_state {
    MICROS_KERNEL_OBJECT_SLOT_FREE = 0,
    MICROS_KERNEL_OBJECT_SLOT_LIVE,
    MICROS_KERNEL_OBJECT_SLOT_QUARANTINED,
};

enum micros_thread_state {
    MICROS_THREAD_STATE_INACTIVE = 0,
    MICROS_THREAD_STATE_RUNNING,
};

struct micros_process {
    enum micros_kernel_object_slot_state slot_state;
    uint32_t generation;
    size_t live_thread_count;
    uintptr_t address_space_root;
    uint32_t primary_endpoint;
    uint32_t privilege_profile;
};

struct micros_thread {
    enum micros_kernel_object_slot_state slot_state;
    uint32_t generation;
    struct micros_process_handle owner;
    enum micros_thread_state state;
};

struct micros_hart_trap_anchor {
    uintptr_t primary_stack_top;
    uintptr_t primary_stack_bottom;
    uintptr_t emergency_stack_top;
    uintptr_t emergency_stack_bottom;
    uintptr_t entry_t0;
    uintptr_t entry_t1;
    uintptr_t entry_t2;
};

struct micros_hart_timer_state {
    bool initialized;
    volatile bool active;
    uint64_t interval;
    uint64_t deadline;
    volatile uint64_t ticks;
    uint64_t test_stop_after;
};

struct micros_hart {
    struct micros_hart_trap_anchor trap;
    enum micros_kernel_object_slot_state slot_state;
    uint32_t generation;
    uintptr_t hardware_id;
    bool trap_installed;
    struct micros_thread_handle current_thread;
    uint32_t interrupt_depth;
    uint32_t preempt_disable_count;
    bool reschedule_pending;
    struct micros_hart_timer_state timer;
};

struct micros_kernel_objects {
    uint64_t initialization_magic;
    size_t max_threads_per_process;
    size_t max_harts;
    size_t live_process_count;
    size_t live_thread_count;
    size_t registered_hart_count;
    struct micros_process processes[MICROS_PROCESS_CAPACITY];
    struct micros_thread threads[MICROS_THREAD_CAPACITY];
    struct micros_hart harts[MICROS_HART_CAPACITY];
};

enum micros_kernel_object_error {
    MICROS_KERNEL_OBJECT_OK = 0,
    MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
    MICROS_KERNEL_OBJECT_ERROR_STORAGE,
    MICROS_KERNEL_OBJECT_ERROR_ALREADY_INITIALIZED,
    MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
    MICROS_KERNEL_OBJECT_ERROR_POLICY,
    MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED,
    MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED,
    MICROS_KERNEL_OBJECT_ERROR_STALE,
    MICROS_KERNEL_OBJECT_ERROR_STATE,
    MICROS_KERNEL_OBJECT_ERROR_THREAD_LIMIT,
    MICROS_KERNEL_OBJECT_ERROR_DUPLICATE_HART,
    MICROS_KERNEL_OBJECT_ERROR_STACK,
    MICROS_KERNEL_OBJECT_ERROR_INVARIANT,
};

enum micros_kernel_object_error micros_process_next_generation(
    uint16_t slot,
    uint32_t current_generation,
    uint32_t *next_generation
);

enum micros_kernel_object_error micros_thread_next_generation(
    uint32_t current_generation,
    uint32_t *next_generation
);

enum micros_kernel_object_error micros_kernel_objects_initialize(
    struct micros_kernel_objects *objects,
    size_t max_threads_per_process,
    size_t max_harts
);

enum micros_kernel_object_error micros_kernel_objects_validate(
    const struct micros_kernel_objects *objects
);

enum micros_kernel_object_error micros_process_create(
    struct micros_kernel_objects *objects,
    struct micros_process_handle *handle
);

enum micros_kernel_object_error micros_process_release(
    struct micros_kernel_objects *objects,
    struct micros_process_handle handle
);

enum micros_kernel_object_error micros_process_resolve(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle handle,
    const struct micros_process **process
);

enum micros_kernel_object_error micros_thread_create(
    struct micros_kernel_objects *objects,
    struct micros_process_handle owner,
    struct micros_thread_handle *handle
);

enum micros_kernel_object_error micros_thread_release(
    struct micros_kernel_objects *objects,
    struct micros_thread_handle handle
);

enum micros_kernel_object_error micros_thread_resolve(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    const struct micros_thread **thread
);

enum micros_kernel_object_error micros_hart_register(
    struct micros_kernel_objects *objects,
    uintptr_t hardware_id,
    struct micros_hart_handle *handle
);

enum micros_kernel_object_error micros_hart_resolve(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle handle,
    const struct micros_hart **hart
);

enum micros_kernel_object_error micros_hart_resolve_context(
    const struct micros_kernel_objects *objects,
    uintptr_t hart_context,
    const struct micros_hart **hart
);

enum micros_kernel_object_error micros_hart_install_trap_stacks(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle handle,
    uintptr_t primary_stack_bottom,
    uintptr_t primary_stack_top,
    uintptr_t emergency_stack_bottom,
    uintptr_t emergency_stack_top
);

enum micros_kernel_object_error micros_hart_bind_thread(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    struct micros_thread_handle thread
);

enum micros_kernel_object_error micros_hart_clear_thread(
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    struct micros_thread_handle thread
);

enum micros_kernel_object_error micros_hart_current_thread(
    const struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    struct micros_thread_handle *thread
);

#endif
