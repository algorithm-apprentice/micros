#include "tests/host/runtime_test_internal.h"

int main(void)
{
    if (
        !test_ipc_wrappers()
        || !test_grant_wrappers()
        || !test_bootstrap_launcher_console_begin()
        || !test_memory_support()
    ) {
        return 1;
    }
    return 0;
}
