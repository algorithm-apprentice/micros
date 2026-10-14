#include "tests/host/scheduler_test_internal.h"

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {
            "priority queues preserve tail order",
            scheduler_host_test_priority_queues_and_tail_order,
        },
        {
            "runtime flags follow zero boundary",
            scheduler_host_test_runtime_flag_zero_boundary,
        },
        {
            "policy replacement is atomic",
            scheduler_host_test_policy_replacement_is_atomic,
        },
        {
            "current selection repairs preemption",
            scheduler_host_test_current_selection_and_preemption_repair,
        },
        {
            "quantum rotation retains nonpreemptible current",
            scheduler_host_test_quantum_rotation_and_nonpreemptible_current,
        },
        {
            "current block and stale plan are atomic",
            scheduler_host_test_current_block_and_stale_plan_are_atomic,
        },
        {
            "accounting owners are separate",
            scheduler_host_test_accounting_owners_are_separate,
        },
        {
            "trap accounting uses minimal route",
            scheduler_host_test_trap_accounting_uses_minimal_route,
        },
        {
            "corrupt queue state is rejected",
            scheduler_host_test_corrupt_queue_state_is_rejected,
        },
        {
            "seeded two-hart model",
            micros_scheduler_model_test_run,
        },
        {
            "current IPC scheduler guard",
            micros_ipc_current_guard_test_run,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(
                stderr,
                "not ok %zu - %s\n",
                index + 1,
                tests[index].name
            );
            return 1;
        }
        printf("ok %zu - %s\n", index + 1, tests[index].name);
    }
    return 0;
}
