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
        {"coordinator public spawn", test_coordinator_public_spawn},
        {"coordinator public failure results", test_coordinator_public_failure_results},
        {"coordinator public preflight results", test_coordinator_public_preflight_results},
        {"coordinator parent loss rollback", test_coordinator_parent_loss_rollback},
        {"coordinator public capacity and unknown grant", test_coordinator_public_capacity_and_unknown_grant},
        {"coordinator parent loss after publication", test_coordinator_parent_loss_after_publication},
        {"coordinator exit and blocked wait", test_coordinator_exit_and_blocked_wait},
        {"coordinator exit failure boundaries", test_coordinator_exit_failure_boundaries},
        {"coordinator immediate wait reply restore", test_coordinator_immediate_wait_reply_restore},
        {"coordinator nested independent wait replies", test_coordinator_nested_independent_wait_replies},
        {"coordinator real runtime backend", test_coordinator_real_runtime_backend},
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
