#ifndef MICROS_SERVERS_PM_CONTROL_H
#define MICROS_SERVERS_PM_CONTROL_H

#include <stdint.h>

#include "micros/pm.h"
#include "micros/runtime.h"

micros_runtime_result_t micros_pm_control_reserve(
    struct micros_pm_reservation_result *result
);

micros_runtime_result_t micros_pm_control_abort(uint64_t transaction);

micros_runtime_result_t micros_pm_control_prepare_execution(
    uint64_t transaction,
    uint64_t load_token,
    uint64_t mapping_generation,
    struct micros_pm_prepared_result *result
);

micros_runtime_result_t micros_pm_control_abort_prepared(
    uint64_t transaction,
    uint64_t load_token,
    uint64_t mapping_generation,
    uint64_t prepared_token
);

micros_runtime_result_t micros_pm_control_activate(
    uint64_t transaction,
    uint64_t load_token,
    uint64_t mapping_generation,
    uint64_t prepared_token
);

micros_runtime_result_t micros_pm_control_exit_begin(
    uint32_t endpoint,
    struct micros_pm_exit_begin_result *result
);

micros_runtime_result_t micros_pm_control_exit_detach(
    uint64_t exit_token,
    uint32_t endpoint
);

micros_runtime_result_t micros_pm_control_release_exited_process(
    uint64_t exit_token
);

#endif
