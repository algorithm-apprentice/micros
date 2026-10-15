#include "kernel/ramfs_service_test_internal.h"

_Noreturn void micros_ramfs_service_test_launch(void)
{
    micros_bootstrap_runtime_launch(&ramfs_test_runtime_config);
}
