#include "endpoint_test_internal.h"

bool micros_ipc_call_test_run(void);
bool micros_ipc_abi_test_run(void);
bool micros_ipc_close_test_run(void);
bool micros_ipc_deadlock_test_run(void);
bool micros_ipc_delivery_test_run(void);
bool micros_ipc_notify_test_run(void);
bool micros_ipc_operation_test_run(void);
bool micros_ipc_queue_test_run(void);
bool micros_ipc_queue_mutation_test_run(void);
bool micros_ipc_reply_test_run(void);
bool micros_ipc_state_test_run(void);
bool micros_grant_test_run(void);
bool micros_grant_copy_test_run(void);
bool micros_grant_syscall_core_test_run(void);
bool micros_user_address_space_core_test_run(void);

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {
            "endpoint encoding boundaries",
            endpoint_test_encoding_boundaries,
        },
        {
            "profile table is immutable and specific",
            endpoint_test_profile_table_is_immutable_and_specific,
        },
        {
            "invalid profile tables preserve storage",
            endpoint_test_invalid_profile_tables_preserve_storage,
        },
        {
            "validator rejects corruption",
            endpoint_test_validator_rejects_corruption,
        },
        {
            "endpoint lifecycle and authorization",
            endpoint_test_lifecycle_and_authorization,
        },
        {
            "profile install and close require held threads",
            endpoint_test_profile_install_and_close_require_held_threads,
        },
        {
            "endpoint reuse rejects stale generation",
            endpoint_test_reuse_rejects_stale_generation,
        },
        {
            "source-only seal preserves staged source",
            endpoint_test_source_only_seal_preserves_staged_source,
        },
        {
            "resolution rejects unimplemented slots",
            endpoint_test_resolution_rejects_unimplemented_slots,
        },
        {
            "relationship validator rejects corruption",
            endpoint_test_relationship_validator_rejects_corruption,
        },
        {
            "dormant IPC state substrate",
            micros_ipc_state_test_run,
        },
        {
            "direct grant registry lifecycle",
            micros_grant_test_run,
        },
        {
            "checked direct grant copy",
            micros_grant_copy_test_run,
        },
        {
            "phase-aware user address-space authority",
            micros_user_address_space_core_test_run,
        },
        {
            "unified grant syscall ABI",
            micros_grant_syscall_core_test_run,
        },
        {
            "stable IPC syscall ABI",
            micros_ipc_abi_test_run,
        },
        {
            "IPC sender and receiver queue topology",
            micros_ipc_queue_test_run,
        },
        {
            "IPC sender and receiver enqueue",
            micros_ipc_queue_mutation_test_run,
        },
        {
            "IPC matching delivery commit",
            micros_ipc_delivery_test_run,
        },
        {
            "authorized IPC send and receive",
            micros_ipc_operation_test_run,
        },
        {
            "authorized IPC call",
            micros_ipc_call_test_run,
        },
        {
            "authorized one-shot IPC reply",
            micros_ipc_reply_test_run,
        },
        {
            "authorized coalesced IPC notify",
            micros_ipc_notify_test_run,
        },
        {
            "bounded IPC deadlock detection",
            micros_ipc_deadlock_test_run,
        },
        {
            "atomic IPC endpoint close cancellation",
            micros_ipc_close_test_run,
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
