#include "tests/host/vm_service_test_internal.h"

static bool release_message(
    struct vm_service_test_fixture *fixture, uint32_t command, const uint64_t arguments[5]
)
{
    struct micros_ipc_message request = vm_service_test_message(command, arguments);
    struct micros_ipc_message reply;

    VM_SERVICE_EXPECT(
        micros_vm_service_handle_message(&fixture->service, vm_service_test_backend(), fixture,
                                         &request, &reply) == MICROS_VM_SERVICE_OK
        && vm_service_test_read_word(&reply.payload[0]) == (UINT64_C(1) | (uint64_t)command << 32)
        && vm_core_test_bytes_are_zero(&reply.payload[8], 40)
    );
    return true;
}

bool test_vm_service_load_and_release(struct vm_core_test_context *context)
{
    struct vm_service_test_fixture fixture;
    const uint64_t commit[5] = {201, 301, 2, 0, 0};
    const uint64_t exit[5] = {701, 201, 301, 2, 0};

    VM_SERVICE_EXPECT(vm_service_test_initialize(&fixture, context) && vm_service_test_load(&fixture));
    VM_SERVICE_EXPECT(release_message(&fixture, MICROS_VM_CONTROL_COMMIT_LOAD, commit));
    VM_SERVICE_EXPECT(
        fixture.active && !fixture.frozen && context->state.phase == MICROS_VM_LOADER_IDLE
        && context->state.mapping_records[3].phase == MICROS_VM_MAPPING_RECORD_ACTIVE
    );
    VM_SERVICE_EXPECT(release_message(&fixture, MICROS_VM_CONTROL_EXIT_RELEASE, exit));
    VM_SERVICE_EXPECT(
        !fixture.active && !fixture.violation && context->state.phase == MICROS_VM_LOADER_IDLE
        && vm_core_test_bytes_are_zero(&context->state.mapping_records[3],
                                      sizeof(context->state.mapping_records[3]))
        && micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK
    );
    return true;
}

bool test_vm_service_abort_and_failure(struct vm_core_test_context *context)
{
    struct vm_service_test_fixture fixture;
    const uint64_t abort[5] = {201, 301, 2, 0, 0};

    VM_SERVICE_EXPECT(vm_service_test_initialize(&fixture, context) && vm_service_test_load(&fixture));
    VM_SERVICE_EXPECT(release_message(&fixture, MICROS_VM_CONTROL_ABORT_LOAD, abort));
    VM_SERVICE_EXPECT(
        !fixture.frozen && !fixture.violation && context->state.phase == MICROS_VM_LOADER_IDLE
        && vm_core_test_bytes_are_zero(context->state.executable_bytes, sizeof(context->state.executable_bytes))
        && micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK
    );
    return true;
}

bool test_vm_service_expected_load_rejection(struct vm_core_test_context *context)
{
    struct vm_service_test_fixture fixture;
    const uint32_t expected[4] = {
        (uint32_t)MICROS_VM_CONTROL_RESULT_EXECUTABLE,
        (uint32_t)MICROS_VM_CONTROL_RESULT_NO_SPACE,
        (uint32_t)MICROS_VM_CONTROL_RESULT_TOO_BIG,
        (uint32_t)MICROS_VM_CONTROL_RESULT_MALFORMED,
    };
    size_t scenario;

    for (scenario = 0; scenario < 4; ++scenario) {
        struct micros_ipc_message request;
        struct micros_ipc_message reply;
        uint64_t arguments[5] = {101, 2, 0, 0, 0};

        VM_SERVICE_EXPECT(vm_service_test_initialize(&fixture, context));
        if (scenario == 0) {
            context->executable[0] = 0;
        } else if (scenario == 1) {
            VM_SERVICE_EXPECT(vm_core_test_initialize_boot_info(context, TEST_FIRST_FREE_FRAME + 7));
            VM_SERVICE_EXPECT(vm_core_test_initialize_state(context) == MICROS_VM_CORE_OK);
        } else if (scenario == 2) {
            fixture.executable_size = MICROS_APPLICATION_EXECUTABLE_SIZE_MAX + 1;
        } else {
            context->request[0] = 0;
        }
        arguments[2] = (uint64_t)fixture.executable_size << 32 | 11;
        arguments[3] = (uint64_t)fixture.request_size << 32 | 12;
        request = vm_service_test_message(MICROS_VM_CONTROL_LOAD, arguments);
        VM_SERVICE_EXPECT(
            micros_vm_service_handle_message(&fixture.service, vm_service_test_backend(), &fixture,
                                             &request, &reply) == MICROS_VM_SERVICE_OK
            && vm_service_test_read_word(&reply.payload[8]) == expected[scenario]
            && vm_core_test_bytes_are_zero(&reply.payload[16], 32)
            && fixture.begins == 0 && fixture.maps == 0 && !fixture.violation
            && context->state.phase == MICROS_VM_LOADER_IDLE
            && vm_core_test_bytes_are_zero(&context->state.transaction, sizeof(context->state.transaction))
            && vm_core_test_bytes_are_zero(context->state.executable_bytes, sizeof(context->state.executable_bytes))
            && vm_core_test_bytes_are_zero(context->state.request_bytes, sizeof(context->state.request_bytes))
            && micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK
        );
    }
    return true;
}

static bool service_invariant_preserves_reply(
    struct vm_service_test_fixture *fixture, uint32_t command, const uint64_t arguments[5]
)
{
    struct micros_ipc_message request = vm_service_test_message(command, arguments);
    struct micros_ipc_message reply;
    struct micros_ipc_message previous;

    memset(&reply, 0xa5, sizeof(reply));
    previous = reply;
    VM_SERVICE_EXPECT(
        micros_vm_service_handle_message(&fixture->service, vm_service_test_backend(), fixture,
                                         &request, &reply) == MICROS_VM_SERVICE_ERROR_INVARIANT
        && memcmp(&reply, &previous, sizeof(reply)) == 0 && !fixture->violation
    );
    return true;
}

static bool service_load_disagreements(struct vm_core_test_context *context)
{
    struct vm_service_test_fixture fixture;
    unsigned int scenario;

    for (scenario = 1; scenario <= 6 + 7 + 5; ++scenario) {
        uint64_t arguments[5] = {101, 2, 0, 0, 0};

        VM_SERVICE_EXPECT(vm_service_test_initialize(&fixture, context));
        if (scenario <= 6) {
            fixture.failure = (enum vm_service_test_failure_point)scenario;
            fixture.failure_page = 2;
        } else if (scenario <= 13) {
            fixture.corrupt_begin = scenario - 6;
        } else {
            fixture.corrupt_freeze = scenario - 13;
        }
        arguments[2] = (uint64_t)fixture.executable_size << 32 | 11;
        arguments[3] = (uint64_t)fixture.request_size << 32 | 12;
        VM_SERVICE_EXPECT(service_invariant_preserves_reply(&fixture, MICROS_VM_CONTROL_LOAD, arguments));
    }
    return true;
}

bool test_vm_service_kernel_disagreement(struct vm_core_test_context *context)
{
    struct vm_service_test_fixture fixture;
    const uint64_t frozen[5] = {201, 301, 2, 0, 0};
    const uint64_t exit[5] = {701, 201, 301, 2, 0};
    uint32_t command;

    VM_SERVICE_EXPECT(service_load_disagreements(context));
    for (command = MICROS_VM_CONTROL_ABORT_LOAD; command <= MICROS_VM_CONTROL_EXIT_RELEASE; ++command) {
        VM_SERVICE_EXPECT(vm_service_test_initialize(&fixture, context) && vm_service_test_load(&fixture));
        if (command == MICROS_VM_CONTROL_EXIT_RELEASE) {
            VM_SERVICE_EXPECT(release_message(&fixture, MICROS_VM_CONTROL_COMMIT_LOAD, frozen));
        }
        fixture.failure = command == MICROS_VM_CONTROL_ABORT_LOAD ? VM_SERVICE_TEST_FAIL_ABORT
            : command == MICROS_VM_CONTROL_COMMIT_LOAD ? VM_SERVICE_TEST_FAIL_COMMIT
            : VM_SERVICE_TEST_FAIL_EXIT;
        VM_SERVICE_EXPECT(service_invariant_preserves_reply(
            &fixture, command, command == MICROS_VM_CONTROL_EXIT_RELEASE ? exit : frozen
        ));
    }
    return true;
}
