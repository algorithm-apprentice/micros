#ifndef MICROS_SERVERS_PM_CONTROL_H
#define MICROS_SERVERS_PM_CONTROL_H

#include <stdint.h>

#include "micros/pm.h"
#include "micros/runtime.h"

micros_runtime_result_t micros_pm_control_reserve(
    struct micros_pm_reservation_result *result
);

micros_runtime_result_t micros_pm_control_abort(uint64_t transaction);

#endif
