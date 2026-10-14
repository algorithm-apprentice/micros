#include "tests/host/vm_service_test_internal.h"

bool test_vm_service_configuration(struct vm_core_test_context *context)
{
    struct vm_service_test_fixture fixture;
    struct micros_vm_service service = {0};
    struct micros_vm_service previous;
    struct micros_vm_service_bindings bindings = {
        .self = TEST_VM_ENDPOINT, .pm = UINT32_C(0x1002), .vfs = UINT32_C(0x1005),
    };
    struct micros_vm_service_backend backend;
    struct micros_ipc_message request;
    struct micros_ipc_message reply;
    const uint64_t arguments[5] = {101, 2, UINT64_C(1) << 32 | 11, UINT64_C(256) << 32 | 12, 0};

    VM_SERVICE_EXPECT(vm_service_test_initialize(&fixture, context));
    previous = fixture.service;
    VM_SERVICE_EXPECT(
        micros_vm_service_initialize(&fixture.service, &context->state, &bindings)
            == MICROS_VM_SERVICE_ERROR_ARGUMENT
        && memcmp(&previous, &fixture.service, sizeof(previous)) == 0
    );
    bindings.vfs = bindings.pm;
    VM_SERVICE_EXPECT(
        micros_vm_service_initialize(&service, &context->state, &bindings)
            == MICROS_VM_SERVICE_ERROR_ARGUMENT
        && vm_core_test_bytes_are_zero(&service, sizeof(service))
    );
    bindings.vfs = UINT32_C(0x1005);
    service.bindings = bindings;
    VM_SERVICE_EXPECT(
        micros_vm_service_initialize(&service, &context->state, &service.bindings)
            == MICROS_VM_SERVICE_OK && service.bindings.vfs == bindings.vfs
    );
    backend = *vm_service_test_backend();
    backend.freeze = NULL;
    request = vm_service_test_message(MICROS_VM_CONTROL_LOAD, arguments);
    VM_SERVICE_EXPECT(
        micros_vm_service_handle_message(&service, &backend, &fixture, &request, &reply)
            == MICROS_VM_SERVICE_ERROR_ARGUMENT && fixture.copies == 0
        && micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK
    );
    return true;
}

static bool service_rejection_is_exact(
    struct vm_service_test_fixture *fixture, const struct micros_ipc_message *request,
    enum micros_vm_control_result result
)
{
    struct micros_ipc_message reply;

    memset(&reply, 0xa5, sizeof(reply));
    VM_SERVICE_EXPECT(
        micros_vm_service_handle_message(&fixture->service, vm_service_test_backend(), fixture,
                                         request, &reply) == MICROS_VM_SERVICE_OK
        && reply.type == MICROS_VM_MESSAGE_PM_CONTROL_RESULT && reply.source == MICROS_ENDPOINT_NONE
        && reply.reply_token == 0
        && vm_service_test_read_word(&reply.payload[0])
            == (vm_service_test_read_word(&request->payload[0]) & UINT64_C(0xffffffff00000000) | 1)
        && vm_service_test_read_word(&reply.payload[8]) == (uint32_t)result
        && vm_core_test_bytes_are_zero(&reply.payload[16], 32)
        && fixture->copies == 0 && fixture->maps == 0 && fixture->aborts == 0
        && fixture->load_commits == 0 && fixture->exits == 0
        && micros_vm_state_validate(&fixture->context->state) == MICROS_VM_CORE_OK
    );
    return true;
}

bool test_vm_service_protocol_boundaries(struct vm_core_test_context *context)
{
    struct vm_service_test_fixture fixture;
    const uint64_t frozen[5] = {201, 301, 2, 0, 0};
    const uint64_t exit[5] = {701, 201, 301, 2, 0};
    const uint64_t load[5] = {101, 2, UINT64_C(1) << 32 | 11, UINT64_C(256) << 32 | 12, 0};
    uint32_t command;

    VM_SERVICE_EXPECT(vm_service_test_initialize(&fixture, context));
    for (command = MICROS_VM_CONTROL_LOAD; command <= MICROS_VM_CONTROL_EXIT_RELEASE; ++command) {
        const uint64_t *arguments = command == MICROS_VM_CONTROL_EXIT_RELEASE ? exit
            : command == MICROS_VM_CONTROL_LOAD ? load : frozen;
        struct micros_ipc_message valid = vm_service_test_message(command, arguments);
        struct micros_ipc_message request = valid;
        struct micros_ipc_message reply;
        size_t offset;

        request.payload[0] = 2;
        VM_SERVICE_EXPECT(service_rejection_is_exact(&fixture, &request, MICROS_VM_CONTROL_RESULT_MALFORMED));
        request = valid;
        request.type = 0;
        VM_SERVICE_EXPECT(service_rejection_is_exact(&fixture, &request, MICROS_VM_CONTROL_RESULT_MALFORMED));
        request = valid;
        request.source = UINT32_C(0x2002);
        VM_SERVICE_EXPECT(service_rejection_is_exact(&fixture, &request, MICROS_VM_CONTROL_RESULT_CALLER));
        request = valid;
        request.reply_token = 0;
        VM_SERVICE_EXPECT(
            micros_vm_service_handle_message(&fixture.service, vm_service_test_backend(), &fixture,
                                             &request, &reply) == MICROS_VM_SERVICE_ERROR_INVARIANT
        );
        for (offset = 40; offset < 48; ++offset) {
            request = valid;
            request.payload[offset] = 1;
            VM_SERVICE_EXPECT(service_rejection_is_exact(
                &fixture, &request, MICROS_VM_CONTROL_RESULT_MALFORMED
            ));
        }
        request = valid;
        vm_core_test_write_u64_le(&request.payload[8], 0);
        VM_SERVICE_EXPECT(service_rejection_is_exact(&fixture, &request, MICROS_VM_CONTROL_RESULT_MALFORMED));
        if (command != MICROS_VM_CONTROL_LOAD) {
            VM_SERVICE_EXPECT(service_rejection_is_exact(&fixture, &valid, MICROS_VM_CONTROL_RESULT_STATE));
        }
    }
    return true;
}

static bool service_stale_release_preserves_state(
    struct vm_service_test_fixture *fixture, uint32_t command, const uint64_t arguments[5]
)
{
    struct micros_vm_load_transaction transaction = fixture->context->state.transaction;
    struct micros_vm_mapping_record record = fixture->context->state.mapping_records[3];
    uint8_t frames[TEST_MANAGED_FRAME_COUNT];
    uint32_t phase = fixture->context->state.phase;
    size_t calls = fixture->aborts + fixture->load_commits + fixture->exits;
    struct micros_ipc_message request = vm_service_test_message(command, arguments);
    struct micros_ipc_message reply;

    memcpy(frames, fixture->context->boot_info.frame_states, sizeof(frames));
    VM_SERVICE_EXPECT(
        micros_vm_service_handle_message(&fixture->service, vm_service_test_backend(), fixture,
                                         &request, &reply) == MICROS_VM_SERVICE_OK
        && vm_service_test_read_word(&reply.payload[8]) == (uint32_t)MICROS_VM_CONTROL_RESULT_STATE
        && vm_core_test_bytes_are_zero(&reply.payload[16], 32)
        && calls == fixture->aborts + fixture->load_commits + fixture->exits
        && phase == fixture->context->state.phase
        && memcmp(&transaction, &fixture->context->state.transaction, sizeof(transaction)) == 0
        && memcmp(&record, &fixture->context->state.mapping_records[3], sizeof(record)) == 0
        && memcmp(frames, fixture->context->boot_info.frame_states, sizeof(frames)) == 0
        && micros_vm_state_validate(&fixture->context->state) == MICROS_VM_CORE_OK
    );
    return true;
}

bool test_vm_service_release_identity(struct vm_core_test_context *context)
{
    struct vm_service_test_fixture fixture;
    const uint64_t exact[5] = {201, 301, 2, 0, 0};
    struct micros_ipc_message request;
    struct micros_ipc_message reply;
    uint32_t command;
    size_t field;

    VM_SERVICE_EXPECT(vm_service_test_initialize(&fixture, context) && vm_service_test_load(&fixture));
    for (command = MICROS_VM_CONTROL_ABORT_LOAD; command <= MICROS_VM_CONTROL_COMMIT_LOAD; ++command) {
        for (field = 0; field < 3; ++field) {
            uint64_t stale[5] = {201, 301, 2, 0, 0};

            ++stale[field];
            VM_SERVICE_EXPECT(service_stale_release_preserves_state(&fixture, command, stale));
        }
    }
    request = vm_service_test_message(MICROS_VM_CONTROL_COMMIT_LOAD, exact);
    VM_SERVICE_EXPECT(
        micros_vm_service_handle_message(&fixture.service, vm_service_test_backend(), &fixture,
                                         &request, &reply) == MICROS_VM_SERVICE_OK
    );
    VM_SERVICE_EXPECT(service_stale_release_preserves_state(&fixture, MICROS_VM_CONTROL_ABORT_LOAD, exact));
    VM_SERVICE_EXPECT(service_stale_release_preserves_state(&fixture, MICROS_VM_CONTROL_COMMIT_LOAD, exact));
    for (field = 1; field < 4; ++field) {
        uint64_t stale[5] = {701, 201, 301, 2, 0};

        ++stale[field];
        VM_SERVICE_EXPECT(service_stale_release_preserves_state(
            &fixture, MICROS_VM_CONTROL_EXIT_RELEASE, stale
        ));
    }
    return true;
}
