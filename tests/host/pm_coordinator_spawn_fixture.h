#ifndef MICROS_TESTS_HOST_PM_COORDINATOR_SPAWN_FIXTURE_H
#define MICROS_TESTS_HOST_PM_COORDINATOR_SPAWN_FIXTURE_H

#include "tests/host/pm_coordinator_test_internal.h"

enum coordinator_mock_step {
    COORDINATOR_MOCK_RESERVE = 1,
    COORDINATOR_MOCK_GRANT_CREATE,
    COORDINATOR_MOCK_PREPARE_IMAGE,
    COORDINATOR_MOCK_GRANT_REVOKE,
    COORDINATOR_MOCK_LOAD,
    COORDINATOR_MOCK_PREPARE_DESCRIPTORS,
    COORDINATOR_MOCK_PREPARE_EXECUTION,
    COORDINATOR_MOCK_COMMIT_HIDDEN,
    COORDINATOR_MOCK_ACTIVATE,
    COORDINATOR_MOCK_COMMIT_LOAD,
    COORDINATOR_MOCK_RELEASE_IMAGE,
    COORDINATOR_MOCK_REVERT_HIDDEN,
    COORDINATOR_MOCK_ABORT_PREPARED,
    COORDINATOR_MOCK_ABORT_DESCRIPTORS,
    COORDINATOR_MOCK_ABORT_LOAD,
    COORDINATOR_MOCK_ABORT_RESERVED,
    COORDINATOR_MOCK_EXIT_BEGIN,
    COORDINATOR_MOCK_VFS_EXIT,
    COORDINATOR_MOCK_EXIT_DETACH,
    COORDINATOR_MOCK_VM_EXIT,
    COORDINATOR_MOCK_RELEASE_PROCESS,
    COORDINATOR_MOCK_WAIT_REPLY,
};

struct coordinator_spawn_fixture {
    struct micros_pm_table table;
    struct micros_pm_coordinator coordinator;
    uint8_t request[512];
    size_t request_size;
    uint32_t trace[64];
    size_t trace_count;
    size_t child_slot;
    uint64_t expected_pid;
    uint64_t expected_spawn_token;
    uint64_t parent_reply;
    uint32_t fail_step;
    uint32_t cancel_step;
    bool init_mode;
    bool parent_grant_error;
    micros_runtime_result_t parent_grant_status;
    bool violation;
    bool kernel_reserved;
    bool grant_live;
    bool image_live;
    bool mappings_frozen;
    bool descriptors_live;
    bool execution_live;
    bool hidden_live;
    bool published;
    bool vm_committed;
    bool vfs_process_live;
    bool stopped;
    bool exit_token_live;
    uint64_t replies[4];
    uint64_t reply_pids[4];
    size_t reply_count;
    uint64_t fail_reply;
    micros_endpoint_t exit_endpoint;
    uint64_t exit_load;
    uint64_t exit_mapping;
};

void spawn_fixture_write_word(uint8_t *bytes, uint64_t value);
uint64_t spawn_fixture_read_word(const uint8_t *bytes);
bool spawn_fixture_step(struct coordinator_spawn_fixture *fixture, uint32_t step, bool valid);
void spawn_fixture_result(
    struct micros_ipc_message *message, micros_endpoint_t peer,
    uint32_t type, uint32_t command, int32_t result
);
micros_runtime_result_t coordinator_exit_fixture_begin(
    void *context, micros_endpoint_t endpoint, struct micros_pm_exit_begin_result *result
);
micros_runtime_result_t coordinator_exit_fixture_detach(
    void *context, uint64_t transaction, micros_endpoint_t endpoint
);
micros_runtime_result_t coordinator_exit_fixture_release(void *context, uint64_t transaction);
micros_runtime_result_t coordinator_exit_fixture_reply(
    void *context, uint64_t token, const struct micros_ipc_message *message
);
micros_runtime_result_t coordinator_exit_fixture_vfs(
    struct coordinator_spawn_fixture *fixture, struct micros_ipc_message *message
);
micros_runtime_result_t coordinator_exit_fixture_vm(
    struct coordinator_spawn_fixture *fixture, struct micros_ipc_message *message
);
bool coordinator_spawn_fixture_initialize(struct coordinator_spawn_fixture *fixture);
bool coordinator_spawn_fixture_initialize_parent(struct coordinator_spawn_fixture *fixture);
struct micros_ipc_message coordinator_spawn_fixture_request(
    const struct coordinator_spawn_fixture *fixture
);
int32_t coordinator_spawn_fixture_result_code(const struct micros_ipc_message *message);
uint64_t coordinator_spawn_fixture_result_pid(const struct micros_ipc_message *message);
const struct micros_pm_coordinator_backend *coordinator_spawn_fixture_backend(void);
bool coordinator_spawn_fixture_resources_are_empty(const struct coordinator_spawn_fixture *fixture);

#endif
