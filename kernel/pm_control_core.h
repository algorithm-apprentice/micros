#ifndef MICROS_KERNEL_PM_CONTROL_CORE_H
#define MICROS_KERNEL_PM_CONTROL_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/kernel_objects.h"
#include "micros/pm.h"
#include "micros/syscall_abi.h"

struct micros_pm_control_request {
    uint32_t command;
    uint64_t output_address;
    uint64_t transaction;
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

#endif
