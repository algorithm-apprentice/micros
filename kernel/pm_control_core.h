#ifndef MICROS_KERNEL_PM_CONTROL_CORE_H
#define MICROS_KERNEL_PM_CONTROL_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/endpoint.h"
#include "micros/kernel_objects.h"
#include "micros/pm.h"
#include "micros/syscall_abi.h"
#include "micros/user_context.h"

struct micros_vm_handoff_state;

enum micros_pm_control_exit_phase {
    MICROS_PM_CONTROL_EXIT_NONE = 0,
    MICROS_PM_CONTROL_EXIT_STOPPED,
    MICROS_PM_CONTROL_EXIT_DETACHED,
};

struct micros_pm_control_request {
    uint32_t command;
    uint32_t endpoint;
    uint64_t output_address;
    uint64_t transaction;
    uint64_t load_token;
    uint64_t mapping_generation;
    uint64_t prepared_token;
};

struct micros_pm_control_state {
    uint64_t initialization_magic;
    uint32_t service_id;
    uint32_t endpoint;
    uint32_t profile_id;
    uint32_t reserved;
    struct micros_process_handle process;
    struct micros_thread_handle thread;
    uint64_t last_transaction;
    uint64_t active_transaction;
    struct micros_process_handle reserved_process;
    uint64_t last_prepared_token;
    struct {
        uint64_t token;
        uint64_t reservation_token;
        uint64_t load_token;
        uint64_t mapping_generation;
        uint64_t initial_sstatus;
        struct micros_process_handle process;
        struct micros_thread_handle thread;
        struct micros_hart_handle hart;
        uint32_t endpoint;
        uint32_t reserved;
    } prepared;
    uint64_t last_exit_token;
    struct {
        enum micros_pm_control_exit_phase phase;
        uint32_t reserved;
        uint64_t token;
        uint64_t load_token;
        uint64_t mapping_generation;
        struct micros_process_handle process;
        struct micros_thread_handle thread;
        uint32_t endpoint;
        uint32_t reserved_tail;
    } exit;
};

struct micros_pm_control_reserve_plan {
    struct micros_process_handle process;
    uint64_t transaction;
    struct micros_pm_reservation_result result;
};

struct micros_pm_control_abort_plan {
    struct micros_process_handle process;
    uint64_t transaction;
    bool quarantine;
};

struct micros_pm_control_prepare_plan {
    struct micros_process_handle process;
    struct micros_thread_handle thread;
    uint32_t endpoint;
    uint32_t reserved;
    uint64_t reservation_token;
    uint64_t load_token;
    uint64_t mapping_generation;
    uint64_t prepared_token;
    struct micros_user_context initial_context;
    struct micros_pm_prepared_result result;
};

struct micros_pm_control_execution_binding {
    struct micros_thread_handle thread;
    struct micros_hart_handle hart;
    uintptr_t kernel_stack_bottom;
    uintptr_t kernel_stack_top;
    struct micros_user_context context;
};

struct micros_pm_control_abort_prepared_plan {
    struct micros_process_handle process;
    struct micros_thread_handle thread;
    uint32_t endpoint;
    bool quarantine_thread;
};

struct micros_pm_control_activate_plan {
    struct micros_process_handle process;
    struct micros_thread_handle thread;
    struct micros_hart_handle hart;
    uint32_t endpoint;
};

struct micros_pm_control_exit_begin_plan {
    struct micros_process_handle process;
    struct micros_thread_handle thread;
    uint32_t endpoint;
    uint32_t reserved;
    uint64_t exit_token;
    uint64_t load_token;
    uint64_t mapping_generation;
    struct micros_pm_exit_begin_result result;
};

struct micros_pm_control_exit_detach_plan {
    struct micros_process_handle process;
    struct micros_thread_handle thread;
    uint32_t endpoint;
    bool quarantine_thread;
    uint64_t exit_token;
    uint64_t load_token;
    uint64_t mapping_generation;
};

struct micros_pm_control_release_exited_plan {
    struct micros_process_handle process;
    bool quarantine_process;
};

enum micros_pm_control_error {
    MICROS_PM_CONTROL_OK = 0,
    MICROS_PM_CONTROL_ERROR_ARGUMENT,
    MICROS_PM_CONTROL_ERROR_STORAGE,
    MICROS_PM_CONTROL_ERROR_STATE,
    MICROS_PM_CONTROL_ERROR_CAPACITY,
    MICROS_PM_CONTROL_ERROR_INVARIANT,
};

enum micros_syscall_abi_result micros_pm_control_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_pm_control_request *request
);

enum micros_pm_control_error micros_pm_control_state_prepare(
    struct micros_pm_control_state *state,
    uint32_t service_id,
    uint32_t endpoint,
    uint32_t profile_id,
    struct micros_process_handle process,
    struct micros_thread_handle thread
);

enum micros_pm_control_error micros_pm_control_state_validate(
    const struct micros_pm_control_state *state
);

enum micros_pm_control_error micros_pm_control_state_validate_objects(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects
);

enum micros_pm_control_error micros_pm_control_reserve_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    struct micros_pm_control_reserve_plan *plan
);

void micros_pm_control_reserve_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    const struct micros_pm_control_reserve_plan *plan
);

enum micros_pm_control_error micros_pm_control_abort_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    uint64_t transaction,
    struct micros_pm_control_abort_plan *plan
);

void micros_pm_control_abort_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    const struct micros_pm_control_abort_plan *plan
);

enum micros_pm_control_error micros_pm_control_prepare_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm,
    uint64_t reservation_token,
    uint64_t load_token,
    uint64_t mapping_generation,
    struct micros_pm_control_prepare_plan *plan
);

void micros_pm_control_prepare_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    struct micros_endpoint_registry *registry,
    struct micros_vm_handoff_state *vm,
    const struct micros_pm_control_prepare_plan *plan,
    const struct micros_pm_control_execution_binding *binding
);

enum micros_pm_control_error
micros_pm_control_abort_prepared_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm,
    uint64_t reservation_token,
    uint64_t load_token,
    uint64_t mapping_generation,
    uint64_t prepared_token,
    struct micros_pm_control_abort_prepared_plan *plan
);

void micros_pm_control_abort_prepared_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    struct micros_endpoint_registry *registry,
    struct micros_vm_handoff_state *vm,
    const struct micros_pm_control_abort_prepared_plan *plan
);

enum micros_pm_control_error micros_pm_control_activate_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm,
    uint64_t reservation_token,
    uint64_t load_token,
    uint64_t mapping_generation,
    uint64_t prepared_token,
    struct micros_pm_control_activate_plan *plan
);

void micros_pm_control_activate_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    struct micros_endpoint_registry *registry,
    struct micros_vm_handoff_state *vm,
    const struct micros_pm_control_activate_plan *plan
);

enum micros_pm_control_error micros_pm_control_exit_begin_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm,
    uint32_t endpoint,
    struct micros_pm_control_exit_begin_plan *plan
);

void micros_pm_control_exit_begin_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    struct micros_endpoint_registry *registry,
    const struct micros_pm_control_exit_begin_plan *plan
);

enum micros_pm_control_error micros_pm_control_exit_detach_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *registry,
    const struct micros_vm_handoff_state *vm,
    uint64_t exit_token,
    uint32_t endpoint,
    struct micros_pm_control_exit_detach_plan *plan
);

void micros_pm_control_exit_detach_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    struct micros_vm_handoff_state *vm,
    const struct micros_pm_control_exit_detach_plan *plan
);

enum micros_pm_control_error
micros_pm_control_release_exited_preflight(
    const struct micros_pm_control_state *state,
    const struct micros_kernel_objects *objects,
    const struct micros_vm_handoff_state *vm,
    uint64_t exit_token,
    struct micros_pm_control_release_exited_plan *plan
);

void micros_pm_control_release_exited_commit_prevalidated(
    struct micros_pm_control_state *state,
    struct micros_kernel_objects *objects,
    const struct micros_pm_control_release_exited_plan *plan
);

#endif
