#include "tests/host/pm_coordinator_spawn_fixture.h"

static void spawn_fixture_write_u32(uint8_t *bytes, uint32_t value)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static void spawn_fixture_write_word(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static uint64_t spawn_fixture_read_word(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool spawn_fixture_step(struct coordinator_spawn_fixture *fixture, uint32_t step, bool valid)
{
    if (fixture->trace_count >= sizeof(fixture->trace) / sizeof(fixture->trace[0])) {
        fixture->violation = true;
        return false;
    }
    fixture->trace[fixture->trace_count++] = step;
    if (!valid) {
        fixture->violation = true;
    }
    if (valid && !fixture->init_mode && fixture->cancel_step == step) {
        if (micros_pm_coordinator_parent_lost(&fixture->coordinator, fixture->parent_reply)
            != MICROS_PM_COORDINATOR_OK) {
            fixture->violation = true;
            return false;
        }
    }
    return valid;
}

static micros_endpoint_t spawn_fixture_endpoint(const struct coordinator_spawn_fixture *fixture)
{
    return (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS)
        | (fixture->init_mode ? UINT32_C(6) : UINT32_C(7));
}

static micros_runtime_result_t spawn_fixture_reserve(void *context, struct micros_pm_reservation_result *result)
{
    struct coordinator_spawn_fixture *fixture = context;

    if (!spawn_fixture_step(fixture, COORDINATOR_MOCK_RESERVE,
        !fixture->kernel_reserved && fixture->table.records[fixture->child_slot].spawn_stage
            == MICROS_PM_SPAWN_PM_RECORD_RESERVED)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (fixture->fail_step == COORDINATOR_MOCK_RESERVE) {
        return MICROS_SYSCALL_ABI_CAPACITY;
    }
    memset(result, 0, sizeof(*result));
    result->version = MICROS_PM_RESERVATION_VERSION;
    result->size = MICROS_PM_RESERVATION_SIZE;
    result->transaction = 101;
    fixture->kernel_reserved = true;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t spawn_fixture_abort_reserved(void *context, uint64_t transaction)
{
    struct coordinator_spawn_fixture *fixture = context;

    if (!spawn_fixture_step(fixture, COORDINATOR_MOCK_ABORT_RESERVED,
        transaction == 101 && fixture->kernel_reserved && !fixture->image_live
            && !fixture->mappings_frozen && !fixture->execution_live)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    fixture->kernel_reserved = false;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t spawn_fixture_prepare(
    void *context, uint64_t transaction, uint64_t load_token,
    uint64_t mapping_generation, struct micros_pm_prepared_result *result
)
{
    struct coordinator_spawn_fixture *fixture = context;

    if (!spawn_fixture_step(fixture, COORDINATOR_MOCK_PREPARE_EXECUTION,
        transaction == 101 && load_token == 201 && mapping_generation == 301
            && fixture->descriptors_live && fixture->mappings_frozen
            && fixture->table.records[fixture->child_slot].spawn_stage
                == MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (fixture->fail_step == COORDINATOR_MOCK_PREPARE_EXECUTION) {
        return MICROS_SYSCALL_ABI_CAPACITY;
    }
    memset(result, 0, sizeof(*result));
    result->version = MICROS_PM_PREPARED_VERSION;
    result->size = MICROS_PM_PREPARED_SIZE;
    result->prepared_token = 401;
    result->endpoint = spawn_fixture_endpoint(fixture);
    fixture->execution_live = true;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t spawn_fixture_abort_prepared(
    void *context, uint64_t transaction, uint64_t load_token,
    uint64_t mapping_generation, uint64_t prepared_token
)
{
    struct coordinator_spawn_fixture *fixture = context;

    if (!spawn_fixture_step(fixture, COORDINATOR_MOCK_ABORT_PREPARED,
        transaction == 101 && load_token == 201 && mapping_generation == 301
            && prepared_token == 401 && fixture->execution_live
            && !fixture->hidden_live && !fixture->published)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    fixture->execution_live = false;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t spawn_fixture_activate(
    void *context, uint64_t transaction, uint64_t load_token,
    uint64_t mapping_generation, uint64_t prepared_token
)
{
    struct coordinator_spawn_fixture *fixture = context;

    if (!spawn_fixture_step(fixture, COORDINATOR_MOCK_ACTIVATE,
        transaction == 101 && load_token == 201 && mapping_generation == 301
            && prepared_token == 401 && fixture->hidden_live
            && fixture->table.records[fixture->child_slot].state == MICROS_PM_PROCESS_SPAWNING
            && fixture->table.records[fixture->child_slot].spawn_stage
                == MICROS_PM_SPAWN_PM_VFS_COMMITTED)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (fixture->fail_step == COORDINATOR_MOCK_ACTIVATE) {
        return MICROS_SYSCALL_ABI_CAPACITY;
    }
    fixture->published = true;
    fixture->kernel_reserved = false;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t spawn_fixture_grant_create(
    void *context, micros_endpoint_t grantee, uintptr_t base, size_t length,
    uint32_t permissions, micros_grant_t *grant
)
{
    struct coordinator_spawn_fixture *fixture = context;

    if (!spawn_fixture_step(fixture, COORDINATOR_MOCK_GRANT_CREATE,
        fixture->kernel_reserved && !fixture->grant_live
            && grantee == fixture->coordinator.bindings.vfs
            && base == (uintptr_t)fixture->coordinator.request_bytes
            && length == fixture->request_size && permissions == MICROS_GRANT_PERMISSION_READ)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (fixture->fail_step == COORDINATOR_MOCK_GRANT_CREATE) {
        return MICROS_SYSCALL_ABI_CAPACITY;
    }
    *grant = UINT32_C(0x100);
    fixture->grant_live = true;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t spawn_fixture_grant_revoke(void *context, micros_grant_t grant)
{
    struct coordinator_spawn_fixture *fixture = context;

    if (!spawn_fixture_step(fixture, COORDINATOR_MOCK_GRANT_REVOKE,
        grant == UINT32_C(0x100) && fixture->grant_live)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    fixture->grant_live = false;
    return MICROS_SYSCALL_ABI_OK;
}

static void spawn_fixture_result(
    struct micros_ipc_message *message, micros_endpoint_t peer,
    uint32_t type, uint32_t command, int32_t result
)
{
    memset(message, 0, sizeof(*message));
    message->source = peer;
    message->type = type;
    spawn_fixture_write_word(&message->payload[0], UINT64_C(1) | (uint64_t)command << 32);
    spawn_fixture_write_word(&message->payload[8], (uint32_t)result);
}

static micros_runtime_result_t spawn_fixture_vfs_call(
    struct coordinator_spawn_fixture *fixture, struct micros_ipc_message *message
)
{
    uint32_t command = (uint32_t)(spawn_fixture_read_word(&message->payload[0]) >> 32);
    uint64_t words[5];
    uint32_t step;
    bool valid;
    size_t index;

    for (index = 0; index < 5; ++index) {
        words[index] = spawn_fixture_read_word(&message->payload[(index + 1) * 8]);
    }
    valid = words[0] == fixture->expected_spawn_token;
    switch (command) {
    case MICROS_VFS_CONTROL_PREPARE_IMAGE:
        step = COORDINATOR_MOCK_PREPARE_IMAGE;
        valid = valid && fixture->grant_live && words[1] == fixture->expected_pid
            && words[2] == (fixture->init_mode ? 0 : fixture->table.records[0].endpoint)
            && words[3] == ((uint64_t)fixture->request_size << 32 | UINT64_C(0x100))
            && words[4] == (uint64_t)fixture->init_mode;
        break;
    case MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS:
        step = COORDINATOR_MOCK_PREPARE_DESCRIPTORS;
        valid = valid && fixture->mappings_frozen && words[1] == 501
            && words[2] == fixture->expected_pid && words[3] == 0 && words[4] == 0;
        break;
    case MICROS_VFS_CONTROL_COMMIT_HIDDEN:
        step = COORDINATOR_MOCK_COMMIT_HIDDEN;
        valid = valid && fixture->execution_live && words[1] == 501 && words[2] == 601
            && words[3] == spawn_fixture_endpoint(fixture) && words[4] == 0;
        break;
    case MICROS_VFS_CONTROL_REVERT_HIDDEN:
        step = COORDINATOR_MOCK_REVERT_HIDDEN;
        valid = valid && fixture->hidden_live && !fixture->published && words[1] == 501
            && words[2] == 601 && words[3] == spawn_fixture_endpoint(fixture) && words[4] == 0;
        break;
    case MICROS_VFS_CONTROL_ABORT_DESCRIPTORS:
        step = COORDINATOR_MOCK_ABORT_DESCRIPTORS;
        valid = valid && fixture->descriptors_live && !fixture->execution_live
            && !fixture->hidden_live && words[1] == 501 && words[2] == 601
            && words[3] == 0 && words[4] == 0;
        break;
    case MICROS_VFS_CONTROL_RELEASE_IMAGE:
        step = COORDINATOR_MOCK_RELEASE_IMAGE;
        valid = valid && fixture->image_live && words[1] == 501
            && words[2] == 0 && words[3] == 0 && words[4] == 0
            && (fixture->published ? fixture->vm_committed
                : !fixture->mappings_frozen && !fixture->descriptors_live && !fixture->execution_live);
        break;
    default:
        fixture->violation = true;
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (!spawn_fixture_step(fixture, step, valid)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    spawn_fixture_result(message, fixture->coordinator.bindings.vfs,
                         MICROS_VFS_MESSAGE_PM_CONTROL_RESULT, command,
                         fixture->fail_step == step
                            ? (step == COORDINATOR_MOCK_PREPARE_IMAGE
                                ? MICROS_VFS_CONTROL_RESULT_IO : MICROS_VFS_CONTROL_RESULT_NO_SPACE)
                            : MICROS_VFS_CONTROL_RESULT_OK);
    if (fixture->fail_step == step) {
        return MICROS_SYSCALL_ABI_OK;
    }
    if (step == COORDINATOR_MOCK_PREPARE_IMAGE) {
        fixture->image_live = true;
        spawn_fixture_write_word(&message->payload[16], 501);
        spawn_fixture_write_word(&message->payload[24], UINT64_C(4096) << 32 | UINT64_C(0x301));
        spawn_fixture_write_word(&message->payload[32],
                                  (uint64_t)fixture->request_size << 32 | UINT64_C(0x300));
    } else if (step == COORDINATOR_MOCK_PREPARE_DESCRIPTORS) {
        fixture->descriptors_live = true;
        spawn_fixture_write_word(&message->payload[16], 601);
    } else if (step == COORDINATOR_MOCK_COMMIT_HIDDEN) {
        fixture->hidden_live = true;
        fixture->vfs_process_live = true;
    } else if (step == COORDINATOR_MOCK_REVERT_HIDDEN) {
        fixture->hidden_live = false;
        fixture->vfs_process_live = false;
    } else if (step == COORDINATOR_MOCK_ABORT_DESCRIPTORS) {
        fixture->descriptors_live = false;
    } else {
        fixture->image_live = false;
        fixture->hidden_live = false;
    }
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t spawn_fixture_vm_call(
    struct coordinator_spawn_fixture *fixture, struct micros_ipc_message *message
)
{
    uint32_t command = (uint32_t)(spawn_fixture_read_word(&message->payload[0]) >> 32);
    uint64_t words[5];
    uint32_t step;
    bool valid;
    size_t index;

    for (index = 0; index < 5; ++index) {
        words[index] = spawn_fixture_read_word(&message->payload[(index + 1) * 8]);
    }
    if (command == MICROS_VM_CONTROL_LOAD) {
        step = COORDINATOR_MOCK_LOAD;
        valid = fixture->image_live && !fixture->grant_live && words[0] == 101
            && words[1] == fixture->expected_pid
            && words[2] == (UINT64_C(4096) << 32 | UINT64_C(0x301))
            && words[3] == ((uint64_t)fixture->request_size << 32 | UINT64_C(0x300))
            && words[4] == 0;
    } else if (command == MICROS_VM_CONTROL_ABORT_LOAD) {
        step = COORDINATOR_MOCK_ABORT_LOAD;
        valid = fixture->mappings_frozen && !fixture->execution_live && !fixture->published
            && words[0] == 201 && words[1] == 301 && words[2] == fixture->expected_pid
            && words[3] == 0 && words[4] == 0;
    } else if (command == MICROS_VM_CONTROL_COMMIT_LOAD) {
        step = COORDINATOR_MOCK_COMMIT_LOAD;
        valid = fixture->published
            && fixture->table.records[fixture->child_slot].state == MICROS_PM_PROCESS_RUNNING
            && fixture->table.records[fixture->child_slot].load_token == 201
            && fixture->table.records[fixture->child_slot].mapping_generation == 301
            && words[0] == 201 && words[1] == 301 && words[2] == fixture->expected_pid
            && words[3] == 0 && words[4] == 0;
    } else {
        fixture->violation = true;
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (!spawn_fixture_step(fixture, step, valid)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    spawn_fixture_result(message, fixture->coordinator.bindings.vm,
                         MICROS_VM_MESSAGE_PM_CONTROL_RESULT, command,
                         fixture->fail_step == step ? MICROS_VM_CONTROL_RESULT_EXECUTABLE
                            : MICROS_VM_CONTROL_RESULT_OK);
    if (fixture->fail_step == step) {
        return MICROS_SYSCALL_ABI_OK;
    }
    if (step == COORDINATOR_MOCK_LOAD) {
        fixture->mappings_frozen = true;
        spawn_fixture_write_word(&message->payload[16], 201);
        spawn_fixture_write_word(&message->payload[24], 301);
        spawn_fixture_write_word(&message->payload[32], MICROS_APPLICATION_ENTRY);
        spawn_fixture_write_word(&message->payload[40], MICROS_APPLICATION_STACK_TOP - 128);
    } else if (step == COORDINATOR_MOCK_COMMIT_LOAD) {
        fixture->mappings_frozen = false;
        fixture->vm_committed = true;
    } else {
        fixture->mappings_frozen = false;
    }
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t spawn_fixture_call(
    void *context, micros_endpoint_t peer, struct micros_ipc_message *message
)
{
    struct coordinator_spawn_fixture *fixture = context;

    if (peer == fixture->coordinator.bindings.vfs
        && message->type == MICROS_VFS_MESSAGE_PM_CONTROL) {
        return spawn_fixture_vfs_call(fixture, message);
    }
    if (peer == fixture->coordinator.bindings.vm
        && message->type == MICROS_VM_MESSAGE_PM_CONTROL) {
        return spawn_fixture_vm_call(fixture, message);
    }
    fixture->violation = true;
    return MICROS_SYSCALL_ABI_STATE;
}

static micros_runtime_result_t spawn_fixture_copy_parent(
    void *context, micros_endpoint_t grantor, micros_grant_t grant,
    size_t offset, uintptr_t local_address, size_t length
)
{
    struct coordinator_spawn_fixture *fixture = context;

    if (
        fixture->init_mode || grantor != fixture->table.records[0].endpoint
        || grant != UINT32_C(0x700) || offset != 0
        || local_address != (uintptr_t)fixture->coordinator.request_bytes
        || length != fixture->request_size
    ) {
        fixture->violation = true;
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (fixture->parent_grant_error) {
        return MICROS_SYSCALL_ABI_UNAUTHORIZED;
    }
    if (fixture->parent_grant_status != MICROS_SYSCALL_ABI_OK) {
        return fixture->parent_grant_status;
    }
    memcpy((void *)local_address, fixture->request, length);
    return MICROS_SYSCALL_ABI_OK;
}

const struct micros_pm_coordinator_backend *coordinator_spawn_fixture_backend(void)
{
    static const struct micros_pm_coordinator_backend backend = {
        .reserve = spawn_fixture_reserve,
        .abort_reserved = spawn_fixture_abort_reserved,
        .prepare_execution = spawn_fixture_prepare,
        .abort_prepared = spawn_fixture_abort_prepared,
        .activate = spawn_fixture_activate,
        .grant_create = spawn_fixture_grant_create,
        .grant_revoke = spawn_fixture_grant_revoke,
        .grant_copy_from = spawn_fixture_copy_parent,
        .call = spawn_fixture_call,
    };

    return &backend;
}

bool coordinator_spawn_fixture_initialize(struct coordinator_spawn_fixture *fixture)
{
    struct micros_pm_coordinator_bindings bindings = {
        .self = (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) | UINT32_C(2),
        .vm = (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) | UINT32_C(1),
        .vfs = (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) | UINT32_C(5),
        .reserved = 0,
    };
    struct micros_ipc_message event;

    memset(fixture, 0, sizeof(*fixture));
    fixture->init_mode = true;
    fixture->expected_pid = 1;
    fixture->expected_spawn_token = 1;
    memset(&event, 0, sizeof(event));
    event.source = MICROS_ENDPOINT_NONE;
    event.type = MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    event.payload[0] = (uint8_t)MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED;
    COORDINATOR_EXPECT(
        micros_pm_table_initialize(&fixture->table) == MICROS_PM_MODEL_OK
        && micros_pm_coordinator_initialize(&fixture->coordinator, &fixture->table, &bindings)
            == MICROS_PM_COORDINATOR_OK
        && micros_pm_enable_runtime(&fixture->table, &event) == MICROS_PM_MODEL_OK
    );
    fixture->request_size = 271;
    spawn_fixture_write_u32(&fixture->request[0], MICROS_SPAWN_REQUEST_VERSION);
    spawn_fixture_write_u32(&fixture->request[4], MICROS_SPAWN_REQUEST_HEADER_SIZE);
    spawn_fixture_write_u32(&fixture->request[8], (uint32_t)fixture->request_size);
    spawn_fixture_write_u32(&fixture->request[16], UINT32_C(256) | UINT32_C(10) << 16);
    fixture->request[24] = 1;
    spawn_fixture_write_u32(&fixture->request[32], UINT32_C(266) | UINT32_C(5) << 16);
    memcpy(&fixture->request[256], "/bin/init", 10);
    memcpy(&fixture->request[266], "init", 5);
    return true;
}

bool coordinator_spawn_fixture_initialize_parent(struct coordinator_spawn_fixture *fixture)
{
    struct micros_pm_process_handle parent;
    uint64_t pid;
    uint64_t reply_token;
    micros_endpoint_t endpoint = (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) | UINT32_C(6);

    COORDINATOR_EXPECT(coordinator_spawn_fixture_initialize(fixture));
    COORDINATOR_EXPECT(
        micros_pm_begin_init(&fixture->table, &parent, &pid) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_reserve_kernel(&fixture->table, parent, 1001) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_prepare_image(&fixture->table, parent, 5001) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_freeze_mappings(&fixture->table, parent, 2001, 3001) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_prepare_descriptors(&fixture->table, parent, 6001) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_prepare_execution(&fixture->table, parent, 4001, endpoint)
            == MICROS_PM_MODEL_OK
        && micros_pm_spawn_commit_hidden(&fixture->table, parent, 1) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_activate(&fixture->table, parent, endpoint, &reply_token)
            == MICROS_PM_MODEL_OK
        && pid == 1 && reply_token == 0
    );
    fixture->coordinator.last_spawn_token = 1;
    fixture->init_mode = false;
    fixture->child_slot = 1;
    fixture->expected_pid = 2;
    fixture->expected_spawn_token = 2;
    fixture->parent_reply = 777;
    return true;
}

struct micros_ipc_message coordinator_spawn_fixture_request(
    const struct coordinator_spawn_fixture *fixture
)
{
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.type = MICROS_PM_MESSAGE_SPAWN;
    message.source = fixture->table.records[0].endpoint;
    message.reply_token = fixture->parent_reply;
    spawn_fixture_write_u32(&message.payload[0], MICROS_PM_PROTOCOL_VERSION);
    spawn_fixture_write_u32(&message.payload[8], UINT32_C(0x700));
    spawn_fixture_write_u32(&message.payload[12], (uint32_t)fixture->request_size);
    return message;
}

int32_t coordinator_spawn_fixture_result_code(const struct micros_ipc_message *message)
{
    uint64_t word = spawn_fixture_read_word(&message->payload[8]) & UINT32_MAX;
    int64_t result = (int64_t)word;

    if (result > INT32_MAX) {
        result -= INT64_C(1) << 32;
    }
    return (int32_t)result;
}

uint64_t coordinator_spawn_fixture_result_pid(const struct micros_ipc_message *message)
{
    return spawn_fixture_read_word(&message->payload[16]);
}
bool coordinator_spawn_fixture_resources_are_empty(const struct coordinator_spawn_fixture *fixture)
{
    return (
        !fixture->kernel_reserved && !fixture->grant_live && !fixture->image_live
        && !fixture->mappings_frozen && !fixture->descriptors_live && !fixture->execution_live
        && !fixture->hidden_live && !fixture->published && !fixture->vm_committed
        && !fixture->vfs_process_live
    );
}
