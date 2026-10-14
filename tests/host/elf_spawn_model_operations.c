#include "tests/host/elf_spawn_model_internal.h"

static size_t model_build_request(struct elf_spawn_model_context *context)
{
    uint8_t *bytes = context->vm->request;
    const char *path = context->operation == ELF_MODEL_MISSING_PATH ? "/absent" : "/app";
    const char *strings[3] = {path, "app", "A=B"};
    const size_t references[3] = {
        MICROS_SPAWN_PATH_REFERENCE_OFFSET, MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET,
        MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET,
    };
    size_t cursor = MICROS_SPAWN_REQUEST_HEADER_SIZE;
    size_t index;

    memset(bytes, 0, sizeof(context->vm->request));
    vm_core_test_write_u32_le(&bytes[0], MICROS_SPAWN_REQUEST_VERSION);
    vm_core_test_write_u32_le(&bytes[4], MICROS_SPAWN_REQUEST_HEADER_SIZE);
    vm_core_test_write_u16_le(&bytes[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET], 1);
    vm_core_test_write_u16_le(&bytes[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET], 1);
    for (index = 0; index < 3; ++index) {
        size_t size = strlen(strings[index]) + 1;

        vm_core_test_write_u16_le(&bytes[references[index]], (uint16_t)cursor);
        vm_core_test_write_u16_le(&bytes[references[index] + 2], (uint16_t)size);
        memcpy(&bytes[cursor], strings[index], size);
        cursor += size;
    }
    vm_core_test_write_u32_le(&bytes[8], (uint32_t)cursor);
    if (context->operation != ELF_MODEL_INIT) {
        vm_core_test_write_u16_le(&bytes[MICROS_SPAWN_DESCRIPTOR_ACTION_COUNT_OFFSET], 3);
        for (index = 0; index < 3; ++index) {
            size_t offset = MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET + index * 8;

            vm_core_test_write_u16_le(&bytes[offset],
                context->operation == ELF_MODEL_BAD_DESCRIPTOR && index == 0 ? 15 : (uint16_t)index);
            vm_core_test_write_u16_le(&bytes[offset + 2], (uint16_t)index);
            vm_core_test_write_u32_le(&bytes[offset + 4], MICROS_SPAWN_DESCRIPTOR_DUPLICATE);
        }
    }
    return cursor;
}

static struct micros_ipc_message model_application_message(
    const struct elf_spawn_model_context *context, uint32_t type
)
{
    const struct elf_reference_process *parent = &context->reference.processes[context->parent];
    struct micros_ipc_message message = {
        .source = parent->kernel_generation << MICROS_ENDPOINT_SLOT_BITS | parent->kernel_slot,
        .type = type, .reply_token = context->operation_reply,
    };

    vm_core_test_write_u32_le(message.payload, MICROS_PM_PROTOCOL_VERSION);
    return message;
}

static bool run_init(struct elf_spawn_model_context *context, size_t request_size)
{
    struct micros_pm_coordinator_child child;

    ELF_MODEL_EXPECT(context,
        micros_pm_coordinator_spawn_init(&context->coordinator, elf_model_pm_backend(), context,
            context->vm->request, request_size, &child) == MICROS_PM_COORDINATOR_OK
        && elf_reference_apply_spawn(context)
        && child.pid == 1 && child.process.slot == 0 && child.process.generation == 1
        && child.endpoint == (context->supplied_generation << MICROS_ENDPOINT_SLOT_BITS | context->supplied_slot)
        && child.reserved == 0
    );
    return true;
}

static bool run_spawn(struct elf_spawn_model_context *context)
{
    struct micros_ipc_message message;
    struct micros_pm_coordinator_response response;
    enum micros_pm_coordinator_error status;
    size_t request_size = model_build_request(context);
    micros_grant_t grant = MICROS_GRANT_NONE;
    uint8_t magic = context->filesystem.nodes[context->executable_node].data[0];

    context->spawn_plan = elf_reference_plan_spawn(context, context->operation);
    if (context->operation == ELF_MODEL_INIT) {
        return run_init(context, request_size);
    }
    message = model_application_message(context, MICROS_PM_MESSAGE_SPAWN);
    if (context->operation != ELF_MODEL_BAD_PARENT_GRANT) {
        grant = vfs_test_fixture_add_application_grant(
            &context->filesystem, message.source, MICROS_GRANT_PERMISSION_READ,
            context->vm->request, request_size
        );
        ELF_MODEL_EXPECT(context, grant != MICROS_GRANT_NONE);
    }
    vm_core_test_write_u32_le(&message.payload[8], grant);
    vm_core_test_write_u32_le(&message.payload[12], (uint32_t)request_size);
    if (context->operation == ELF_MODEL_BAD_EXECUTABLE) {
        context->filesystem.nodes[context->executable_node].data[0] = 0;
    }
    status = micros_pm_coordinator_spawn_request(&context->coordinator, elf_model_pm_backend(),
                                                context, &message, &response);
    context->filesystem.nodes[context->executable_node].data[0] = magic;
    if (grant != MICROS_GRANT_NONE) {
        ELF_MODEL_EXPECT(context, vfs_test_fixture_revoke_application_grant(&context->filesystem, grant));
    }
    ELF_MODEL_EXPECT(context, status == MICROS_PM_COORDINATOR_OK && elf_reference_apply_spawn(context));
    ELF_MODEL_EXPECT(context, elf_model_compare_response(context, &response,
        context->spawn_plan.parent_lost ? MICROS_PM_COORDINATOR_RESPONSE_PARENT_LOST
                                       : MICROS_PM_COORDINATOR_RESPONSE_REPLY,
        MICROS_PM_MESSAGE_SPAWN, context->spawn_plan.result,
        context->spawn_plan.active ? context->reference.last_pid : 0));
    return true;
}

static bool run_exit(struct elf_spawn_model_context *context)
{
    struct micros_ipc_message message = model_application_message(context, MICROS_PM_MESSAGE_EXIT);
    struct micros_pm_coordinator_response response;

    vm_core_test_write_u32_le(&message.payload[8], (uint32_t)context->argument);
    ELF_MODEL_EXPECT(context,
        micros_pm_coordinator_exit_request(&context->coordinator, elf_model_pm_backend(),
            context, &message, &response) == MICROS_PM_COORDINATOR_OK
        && elf_model_compare_response(context, &response, MICROS_PM_COORDINATOR_RESPONSE_EXIT_ACCEPTED,
            MICROS_PM_MESSAGE_EXIT, MICROS_PM_RESULT_OK, 0)
    );
    elf_reference_apply_exit(context);
    return true;
}

static bool run_wait(struct elf_spawn_model_context *context)
{
    struct micros_ipc_message message = model_application_message(context, MICROS_PM_MESSAGE_WAIT);
    struct micros_pm_coordinator_response response;
    enum micros_pm_result result;
    enum micros_pm_coordinator_response_kind kind;
    bool blocked;
    bool completed;

    vm_core_test_write_u32_le(&message.payload[4], context->wait_flags);
    vm_core_test_write_u64_le(&message.payload[8], context->argument);
    ELF_MODEL_EXPECT(context,
        micros_pm_coordinator_wait_request(&context->coordinator, elf_model_pm_backend(),
            context, &message, &response) == MICROS_PM_COORDINATOR_OK
    );
    result = elf_reference_apply_wait(context, &blocked, &completed);
    kind = blocked ? MICROS_PM_COORDINATOR_RESPONSE_WAIT_BLOCKED
        : completed ? MICROS_PM_COORDINATOR_RESPONSE_WAIT_REPLY_COMPLETED
        : MICROS_PM_COORDINATOR_RESPONSE_REPLY;
    ELF_MODEL_EXPECT(context, elf_model_compare_response(context, &response, kind,
        MICROS_PM_MESSAGE_WAIT, result, 0));
    return true;
}

static bool run_malformed_pm(struct elf_spawn_model_context *context)
{
    struct micros_ipc_message message = model_application_message(context, MICROS_PM_MESSAGE_WAIT);
    struct micros_pm_coordinator_response response;
    enum micros_pm_result result = MICROS_PM_RESULT_MALFORMED;

    switch (context->argument & 3) {
    case 0:
        message.payload[0] = 2;
        result = MICROS_PM_RESULT_BAD_VERSION;
        break;
    case 1: message.payload[4] = 2; break;
    case 2:
        message.source += UINT32_C(0x1000);
        result = MICROS_PM_RESULT_CALLER;
        break;
    default: message.payload[16] = 1; break;
    }
    ELF_MODEL_EXPECT(context,
        micros_pm_coordinator_wait_request(&context->coordinator, elf_model_pm_backend(),
            context, &message, &response) == MICROS_PM_COORDINATOR_OK
        && elf_model_compare_response(context, &response, MICROS_PM_COORDINATOR_RESPONSE_REPLY,
            MICROS_PM_MESSAGE_WAIT, result, 0)
    );
    return true;
}

static bool run_invalid_control(struct elf_spawn_model_context *context)
{
    bool vm = context->operation == ELF_MODEL_INVALID_VM;
    uint32_t command = vm ? MICROS_VM_CONTROL_COMMIT_LOAD : MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS;
    struct micros_ipc_message message = {
        .source = ELF_MODEL_PM,
        .type = vm ? MICROS_VM_MESSAGE_PM_CONTROL : MICROS_VFS_MESSAGE_PM_CONTROL,
        .reply_token = context->operation_reply,
    };
    struct micros_ipc_message reply;
    int32_t expected = -1;
    size_t index;

    vm_core_test_write_u64_le(&message.payload[0], UINT64_C(1) | (uint64_t)command << 32);
    for (index = 1; index <= 3; ++index) {
        vm_core_test_write_u64_le(&message.payload[index * 8], 1);
    }
    switch (context->argument & 3) {
    case 0: message.source = UINT32_C(0x2002); expected = -2; break;
    case 1: message.payload[40] = 1; break;
    case 2: expected = -3; break;
    default: message.payload[0] = 2; break;
    }
    if (vm) {
        ELF_MODEL_EXPECT(context,
            micros_vm_service_handle_message(&context->vm_service, elf_model_vm_backend(), context,
                &message, &reply) == MICROS_VM_SERVICE_OK);
    } else {
        struct micros_vfs_service_reply_action action;

        ELF_MODEL_EXPECT(context,
            micros_vfs_service_handle_message(&context->vfs, &message, &context->filesystem.io, &action)
                == MICROS_VFS_SERVICE_OK && action.active && action.reply_token == message.reply_token);
        reply = action.message;
    }
    ELF_MODEL_EXPECT(context,
        reply.type == (vm ? MICROS_VM_MESSAGE_PM_CONTROL_RESULT : MICROS_VFS_MESSAGE_PM_CONTROL_RESULT)
        && elf_model_read_word(&reply.payload[0]) == (UINT64_C(1) | (uint64_t)command << 32)
        && elf_model_read_word(&reply.payload[8]) == (uint32_t)expected
        && vm_core_test_bytes_are_zero(&reply.payload[16], 32));
    return true;
}

static bool record_operation(
    struct elf_spawn_model_context *context, enum elf_model_operation operation,
    size_t parent, uint64_t argument, uint64_t random
)
{
    uint64_t words[4] = {operation, parent, argument, random};
    size_t word;
    size_t byte;

    ELF_MODEL_EXPECT(context, context->trace_count < ELF_MODEL_TRACE_CAPACITY);
    context->trace[context->trace_count++] = (struct elf_model_trace_entry){
        .operation = operation, .parent = parent, .argument = argument, .random = random,
    };
    for (word = 0; word < 4; ++word) {
        for (byte = 0; byte < 8; ++byte) {
            context->trace_digest ^= (uint8_t)(words[word] >> (byte * 8));
            context->trace_digest *= UINT64_C(1099511628211);
        }
    }
    ++context->coverage[operation];
    context->operation = operation;
    context->parent = parent;
    context->argument = argument;
    context->operation_reply = ++context->last_reply;
    context->wait_flags = (random & 1) != 0 ? MICROS_PM_WAIT_NOHANG : 0;
    context->stage_count = 0;
    context->expected_stage_count = 0;
    context->reply_count = 0;
    context->expected_reply_count = 0;
    context->supplied_slot = ELF_MODEL_NO_SLOT;
    context->supplied_generation = 0;
    context->supplied_load = 0;
    context->supplied_mapping = 0;
    return true;
}

bool elf_model_run_operation(
    struct elf_spawn_model_context *context, enum elf_model_operation operation,
    size_t parent, uint64_t argument, uint64_t random
)
{
    bool success;

    ELF_MODEL_EXPECT(context, record_operation(context, operation, parent, argument, random));
    if (operation <= ELF_MODEL_BAD_PARENT_GRANT) {
        success = run_spawn(context);
    } else if (operation == ELF_MODEL_EXIT) {
        success = run_exit(context);
    } else if (operation == ELF_MODEL_WAIT) {
        success = run_wait(context);
    } else if (operation == ELF_MODEL_MALFORMED_PM) {
        success = run_malformed_pm(context);
    } else {
        success = run_invalid_control(context);
    }
    ELF_MODEL_EXPECT(context, success && elf_model_compare(context));
    return true;
}
