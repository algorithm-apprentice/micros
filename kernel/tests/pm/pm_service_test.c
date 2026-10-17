#include "kernel/tests/pm/pm_service_test_internal.h"

_Noreturn void micros_pm_service_test_launch(void)
{
    micros_bootstrap_runtime_launch(&pm_service_test_runtime_config);
}
