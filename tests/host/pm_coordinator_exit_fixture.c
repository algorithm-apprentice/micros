#include "tests/host/pm_coordinator_spawn_fixture.h"

micros_runtime_result_t coordinator_exit_fixture_begin(
    void *context, micros_endpoint_t endpoint, struct micros_pm_exit_begin_result *result
)
{
    struct coordinator_spawn_fixture *fixture = context;
    const struct micros_pm_process_record *record = &fixture->table.records[fixture->child_slot];

    if (!spawn_fixture_step(fixture, COORDINATOR_MOCK_EXIT_BEGIN,
        fixture->published && fixture->execution_live && !fixture->stopped
            && record->state == MICROS_PM_PROCESS_RUNNING && record->endpoint == endpoint)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (fixture->fail_step == COORDINATOR_MOCK_EXIT_BEGIN) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    memset(result, 0, sizeof(*result));
    result->version = MICROS_PM_EXIT_BEGIN_VERSION;
    result->size = MICROS_PM_EXIT_BEGIN_SIZE;
    result->exit_token = 701;
    fixture->exit_endpoint = endpoint;
    fixture->exit_load = record->load_token;
    fixture->exit_mapping = record->mapping_generation;
    fixture->stopped = true;
    fixture->exit_token_live = true;
    return MICROS_SYSCALL_ABI_OK;
}

micros_runtime_result_t coordinator_exit_fixture_vfs(
    struct coordinator_spawn_fixture *fixture, struct micros_ipc_message *message
)
{
    const struct micros_pm_process_record *record = &fixture->table.records[fixture->child_slot];

    if (!spawn_fixture_step(fixture, COORDINATOR_MOCK_VFS_EXIT,
        fixture->stopped && fixture->vfs_process_live && fixture->descriptors_live
            && record->state == MICROS_PM_PROCESS_EXITING
            && record->exit_stage == MICROS_PM_EXIT_STAGE_STOPPED
            && spawn_fixture_read_word(&message->payload[8]) == 701
            && spawn_fixture_read_word(&message->payload[16]) == fixture->exit_endpoint
            && spawn_fixture_read_word(&message->payload[24]) == record->pid
            && spawn_fixture_read_word(&message->payload[32]) == 0
            && spawn_fixture_read_word(&message->payload[40]) == 0)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    spawn_fixture_result(message, fixture->coordinator.bindings.vfs,
                         MICROS_VFS_MESSAGE_PM_CONTROL_RESULT, MICROS_VFS_CONTROL_EXIT_PROCESS,
                         fixture->fail_step == COORDINATOR_MOCK_VFS_EXIT
                            ? MICROS_VFS_CONTROL_RESULT_STATE : MICROS_VFS_CONTROL_RESULT_OK);
    if (fixture->fail_step != COORDINATOR_MOCK_VFS_EXIT) {
        fixture->vfs_process_live = false;
        fixture->descriptors_live = false;
    }
    return MICROS_SYSCALL_ABI_OK;
}

micros_runtime_result_t coordinator_exit_fixture_detach(
    void *context, uint64_t transaction, micros_endpoint_t endpoint
)
{
    struct coordinator_spawn_fixture *fixture = context;
    const struct micros_pm_process_record *record = &fixture->table.records[fixture->child_slot];

    if (!spawn_fixture_step(fixture, COORDINATOR_MOCK_EXIT_DETACH,
        transaction == 701 && endpoint == fixture->exit_endpoint && fixture->stopped
            && !fixture->vfs_process_live && !fixture->descriptors_live
            && fixture->execution_live && record->exit_stage == MICROS_PM_EXIT_STAGE_STOPPED)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (fixture->fail_step == COORDINATOR_MOCK_EXIT_DETACH) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    fixture->execution_live = false;
    fixture->published = false;
    return MICROS_SYSCALL_ABI_OK;
}

micros_runtime_result_t coordinator_exit_fixture_vm(
    struct coordinator_spawn_fixture *fixture, struct micros_ipc_message *message
)
{
    const struct micros_pm_process_record *record = &fixture->table.records[fixture->child_slot];

    if (!spawn_fixture_step(fixture, COORDINATOR_MOCK_VM_EXIT,
        fixture->stopped && !fixture->execution_live && fixture->vm_committed
            && record->exit_stage == MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING
            && record->load_token == fixture->exit_load && record->mapping_generation == fixture->exit_mapping
            && spawn_fixture_read_word(&message->payload[8]) == 701
            && spawn_fixture_read_word(&message->payload[16]) == fixture->exit_load
            && spawn_fixture_read_word(&message->payload[24]) == fixture->exit_mapping
            && spawn_fixture_read_word(&message->payload[32]) == record->pid
            && spawn_fixture_read_word(&message->payload[40]) == 0)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    spawn_fixture_result(message, fixture->coordinator.bindings.vm,
                         MICROS_VM_MESSAGE_PM_CONTROL_RESULT, MICROS_VM_CONTROL_EXIT_RELEASE,
                         fixture->fail_step == COORDINATOR_MOCK_VM_EXIT
                            ? MICROS_VM_CONTROL_RESULT_STATE : MICROS_VM_CONTROL_RESULT_OK);
    if (fixture->fail_step != COORDINATOR_MOCK_VM_EXIT) {
        fixture->vm_committed = false;
    }
    return MICROS_SYSCALL_ABI_OK;
}

micros_runtime_result_t coordinator_exit_fixture_release(void *context, uint64_t transaction)
{
    struct coordinator_spawn_fixture *fixture = context;
    const struct micros_pm_process_record *record = &fixture->table.records[fixture->child_slot];

    if (!spawn_fixture_step(fixture, COORDINATOR_MOCK_RELEASE_PROCESS,
        transaction == 701 && fixture->stopped && fixture->exit_token_live
            && !fixture->execution_live && !fixture->vm_committed && !fixture->vfs_process_live
            && record->exit_stage == MICROS_PM_EXIT_STAGE_KERNEL_PROCESS_EMPTY
            && record->load_token == 0 && record->mapping_generation == 0)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (fixture->fail_step == COORDINATOR_MOCK_RELEASE_PROCESS) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    fixture->exit_token_live = false;
    fixture->stopped = false;
    return MICROS_SYSCALL_ABI_OK;
}

micros_runtime_result_t coordinator_exit_fixture_reply(
    void *context, uint64_t token, const struct micros_ipc_message *message
)
{
    struct coordinator_spawn_fixture *fixture = context;

    if (!spawn_fixture_step(fixture, COORDINATOR_MOCK_WAIT_REPLY,
        token != 0 && message->type == MICROS_PM_MESSAGE_RESULT
            && (uint32_t)spawn_fixture_read_word(&message->payload[4]) == MICROS_PM_MESSAGE_WAIT
            && coordinator_spawn_fixture_result_code(message) == MICROS_PM_RESULT_OK
            && fixture->reply_count < sizeof(fixture->replies) / sizeof(fixture->replies[0]))) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    fixture->replies[fixture->reply_count] = token;
    fixture->reply_pids[fixture->reply_count] = coordinator_spawn_fixture_result_pid(message);
    ++fixture->reply_count;
    return token == fixture->fail_reply ? MICROS_SYSCALL_ABI_REPLY_TOKEN : MICROS_SYSCALL_ABI_OK;
}
