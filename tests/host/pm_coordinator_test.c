#include "tests/host/pm_coordinator_test_internal.h"

int main(void)
{
    const struct {
        const char *name;
        bool (*run)(void);
    } cases[] = {
        {"coordinator initialization", test_coordinator_initialization},
        {"coordinator embedded bindings", test_coordinator_embedded_bindings},
        {"coordinator binding failures", test_coordinator_binding_failures},
        {"coordinator table failures", test_coordinator_table_failures},
        {"coordinator VFS control contract", test_coordinator_vfs_control_contract},
        {"coordinator VM control contract", test_coordinator_vm_control_contract},
        {"coordinator control rejections", test_coordinator_control_rejections},
        {"coordinator directed init", test_coordinator_directed_init},
        {"coordinator init failure rollback", test_coordinator_init_failure_rollback},
        {"coordinator init postpublication failures", test_coordinator_init_postpublication_failures},
        {"coordinator init preflight preserves PID", test_coordinator_init_preflight_preserves_pid},
    };
    size_t index;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        if (!cases[index].run()) {
            fprintf(stderr, "PM coordinator case failed: %s\n", cases[index].name);
            return 1;
        }
    }
    puts("PM coordinator tests passed");
    return 0;
}
