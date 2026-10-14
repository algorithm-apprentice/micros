#include "tests/host/vm_service_test_internal.h"

#include "micros/sv39.h"

uint64_t vm_service_test_read_word(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static micros_runtime_result_t service_test_copy(
    void *opaque, micros_endpoint_t grantor, micros_grant_t grant,
    size_t offset, uintptr_t destination, size_t size
)
{
    struct vm_service_test_fixture *fixture = opaque;
    const uint8_t *source;
    uint8_t *expected;
    size_t limit;

    if (grant == 11) {
        source = fixture->context->executable;
        expected = fixture->context->state.executable_bytes;
        limit = fixture->executable_size;
    } else if (grant == 12) {
        source = fixture->context->request;
        expected = fixture->context->state.request_bytes;
        limit = fixture->request_size;
    } else {
        fixture->violation = true;
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    if (grantor != UINT32_C(0x1005) || size > 4096 || offset > limit || size > limit - offset
        || destination != (uintptr_t)&expected[offset]
        || (grant == 11 && offset != fixture->copied_executable)
        || (grant == 12 && (offset != 0 || fixture->copied_executable != fixture->executable_size))) {
        fixture->violation = true;
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    ++fixture->copies;
    if (fixture->failure == VM_SERVICE_TEST_FAIL_COPY) {
        return MICROS_SYSCALL_ABI_MEMORY_FAULT;
    }
    memcpy((void *)destination, &source[offset], size);
    if (grant == 11) {
        fixture->copied_executable += size;
    }
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t service_test_begin(
    void *opaque, uint64_t reservation, struct micros_vm_load_begin_result *result
)
{
    struct vm_service_test_fixture *fixture = opaque;

    if (reservation != 101 || fixture->context->state.phase != MICROS_VM_LOADER_INPUT_COPIED
        || fixture->context->state.transaction.semantic_pid != 2
        || memcmp(fixture->context->state.executable_bytes, fixture->context->executable,
                  fixture->executable_size) != 0
        || memcmp(fixture->context->state.request_bytes, fixture->context->request,
                  fixture->request_size) != 0) {
        fixture->violation = true;
        return MICROS_SYSCALL_ABI_STATE;
    }
    ++fixture->begins;
    if (fixture->failure == VM_SERVICE_TEST_FAIL_BEGIN) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    memset(result, 0, sizeof(*result));
    result->version = MICROS_VM_LOAD_BEGIN_RESULT_VERSION;
    result->size = MICROS_VM_LOAD_BEGIN_RESULT_SIZE;
    result->load_token = 201;
    result->process_slot = 3;
    result->process_generation = 1;
    switch (fixture->corrupt_begin) {
    case 1: result->version = 2; break;
    case 2: result->size = 31; break;
    case 3: result->load_token = 0; break;
    case 4: result->process_slot = MICROS_PROCESS_CAPACITY; break;
    case 5: result->process_generation = 0; break;
    case 6: result->reserved0 = 1; break;
    case 7: result->reserved1 = 1; break;
    default: break;
    }
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t service_test_map(
    void *opaque, uint64_t token, const struct micros_vm_load_page_action *page
)
{
    struct vm_service_test_fixture *fixture = opaque;
    uint64_t expected_va = fixture->maps < 3
        ? MICROS_APPLICATION_ENTRY + fixture->maps * 4096 : MICROS_APPLICATION_STACK_BASE;
    uint32_t expected_permissions = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ;

    if (fixture->maps == 0) {
        expected_permissions |= MICROS_SV39_PERMISSION_EXECUTE;
    } else if (fixture->maps >= 2) {
        expected_permissions |= MICROS_SV39_PERMISSION_WRITE;
    }
    if (token != 201 || fixture->mapped || page->page_index != fixture->maps
        || fixture->context->state.phase != MICROS_VM_LOADER_LOADING
        || page->virtual_address != expected_va || page->permissions != expected_permissions
        || page->physical_address != vm_core_test_frame_address(TEST_FIRST_FREE_FRAME + 4 + fixture->maps)
        || page->reserved != 0) {
        fixture->violation = true;
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (fixture->failure == VM_SERVICE_TEST_FAIL_MAP && fixture->maps == fixture->failure_page) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    fixture->mapped = true;
    ++fixture->maps;
    return MICROS_SYSCALL_ABI_OK;
}

static uint8_t *service_test_scratch(void *opaque)
{
    struct vm_service_test_fixture *fixture = opaque;

    if (!fixture->mapped) {
        fixture->violation = true;
        return NULL;
    }
    if (fixture->failure == VM_SERVICE_TEST_FAIL_SCRATCH) {
        return NULL;
    }
    memset(fixture->context->scratch_page, 0xa5, sizeof(fixture->context->scratch_page));
    return fixture->context->scratch_page;
}

static bool service_test_page_is_correct(const struct vm_service_test_fixture *fixture)
{
    const uint8_t *page = fixture->context->scratch_page;
    size_t index = fixture->commits;
    size_t size;
    const uint8_t *frame = &page[4096 - 144];

    if (index < 3) {
        size = index == 2 ? 1024 : 4096;
        return memcmp(page, &fixture->context->executable[index * 4096], size) == 0
            && vm_core_test_bytes_are_zero(&page[size], 4096 - size);
    }
    return vm_core_test_bytes_are_zero(page, 4096 - 144)
        && vm_service_test_read_word(&frame[0]) == 1
        && vm_service_test_read_word(&frame[8]) == UINT64_C(0x7ffffff0)
        && vm_service_test_read_word(&frame[16]) == 0
        && vm_service_test_read_word(&frame[24]) == UINT64_C(0x7ffffff4)
        && vm_service_test_read_word(&frame[32]) == 0
        && vm_service_test_read_word(&frame[40]) == 0
        && vm_service_test_read_word(&frame[48]) == 1
        && vm_service_test_read_word(&frame[56]) == UINT32_C(0x1002)
        && vm_service_test_read_word(&frame[64]) == 2
        && vm_service_test_read_word(&frame[72]) == UINT32_C(0x1005)
        && vm_service_test_read_word(&frame[80]) == 3
        && vm_service_test_read_word(&frame[88]) == 2
        && vm_service_test_read_word(&frame[96]) == 4
        && vm_service_test_read_word(&frame[104]) == 4096
        && vm_service_test_read_word(&frame[112]) == 0
        && vm_service_test_read_word(&frame[120]) == 0
        && memcmp(&frame[128], "app\0A=B", 8) == 0
        && vm_core_test_bytes_are_zero(&frame[136], 8);
}

static micros_runtime_result_t service_test_scratch_commit(
    void *opaque, uint64_t token, const struct micros_vm_load_page_action *page
)
{
    struct vm_service_test_fixture *fixture = opaque;

    if (token != 201 || !fixture->mapped || page->page_index != fixture->commits
        || fixture->context->state.transaction.scratch_filled != 1
        || !service_test_page_is_correct(fixture)) {
        fixture->violation = true;
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (
        fixture->failure == VM_SERVICE_TEST_FAIL_SCRATCH_COMMIT
        && fixture->commits == fixture->failure_page
    ) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    fixture->mapped = false;
    ++fixture->commits;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t service_test_freeze(
    void *opaque, const struct micros_vm_load_freeze_plan *plan,
    struct micros_vm_load_freeze_result *result
)
{
    struct vm_service_test_fixture *fixture = opaque;

    if (fixture->mapped || fixture->commits != 4 || plan->version != 1 || plan->size != 96
        || plan->load_token != 201 || plan->image_page_count != 3 || plan->data_page_count != 4
        || plan->entry != MICROS_APPLICATION_ENTRY
        || plan->stack_pointer != UINT64_C(0x7fffff70)
        || plan->reserved[0] != 0 || plan->reserved[1] != 0
        || plan->page_plan_digest != vm_core_test_expected_page_digest(fixture->context)) {
        fixture->violation = true;
        return MICROS_SYSCALL_ABI_STATE;
    }
    for (size_t index = 0; index < 4; ++index) {
        if (plan->table_frames[index] != vm_core_test_frame_address(TEST_FIRST_FREE_FRAME + index)) {
            fixture->violation = true;
            return MICROS_SYSCALL_ABI_STATE;
        }
    }
    ++fixture->freezes;
    if (fixture->failure == VM_SERVICE_TEST_FAIL_FREEZE) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    fixture->frozen = true;
    memset(result, 0, sizeof(*result));
    result->version = MICROS_VM_LOAD_FREEZE_RESULT_VERSION;
    result->size = MICROS_VM_LOAD_FREEZE_RESULT_SIZE;
    result->mapping_generation = 301;
    switch (fixture->corrupt_freeze) {
    case 1: result->version = 2; break;
    case 2: result->size = 31; break;
    case 3: result->mapping_generation = 0; break;
    case 4: result->reserved[0] = 1; break;
    case 5: result->reserved[1] = 1; break;
    default: break;
    }
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t service_test_abort(void *opaque, uint64_t token, uint64_t generation)
{
    struct vm_service_test_fixture *fixture = opaque;

    if (!fixture->frozen || fixture->active || token != 201 || generation != 301) {
        fixture->violation = true;
        return MICROS_SYSCALL_ABI_STATE;
    }
    ++fixture->aborts;
    if (fixture->failure == VM_SERVICE_TEST_FAIL_ABORT) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    fixture->frozen = false;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t service_test_commit(void *opaque, uint64_t token, uint64_t generation)
{
    struct vm_service_test_fixture *fixture = opaque;

    if (!fixture->frozen || fixture->active || token != 201 || generation != 301) {
        fixture->violation = true;
        return MICROS_SYSCALL_ABI_STATE;
    }
    ++fixture->load_commits;
    if (fixture->failure == VM_SERVICE_TEST_FAIL_COMMIT) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    fixture->frozen = false;
    fixture->active = true;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t service_test_exit(
    void *opaque, uint64_t exit_token, uint64_t token, uint64_t generation
)
{
    struct vm_service_test_fixture *fixture = opaque;

    if (!fixture->active || exit_token != 701 || token != 201 || generation != 301
        || fixture->context->state.phase != MICROS_VM_LOADER_EXIT_RELEASING) {
        fixture->violation = true;
        return MICROS_SYSCALL_ABI_STATE;
    }
    ++fixture->exits;
    if (fixture->failure == VM_SERVICE_TEST_FAIL_EXIT) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    fixture->active = false;
    return MICROS_SYSCALL_ABI_OK;
}

const struct micros_vm_service_backend *vm_service_test_backend(void)
{
    static const struct micros_vm_service_backend backend = {
        .copy_from = service_test_copy,
        .load_begin = service_test_begin,
        .scratch_map = service_test_map,
        .scratch_page = service_test_scratch,
        .scratch_commit = service_test_scratch_commit,
        .freeze = service_test_freeze,
        .abort_load = service_test_abort,
        .commit_load = service_test_commit,
        .exit_release = service_test_exit,
    };

    return &backend;
}

bool vm_service_test_initialize(
    struct vm_service_test_fixture *fixture, struct vm_core_test_context *context
)
{
    struct micros_vm_service_bindings bindings = {
        .self = TEST_VM_ENDPOINT, .pm = UINT32_C(0x1002),
        .vfs = UINT32_C(0x1005), .reserved = 0,
    };

    memset(fixture, 0, sizeof(*fixture));
    fixture->context = context;
    VM_SERVICE_EXPECT(vm_core_test_initialize_boot_info(context, TEST_MANAGED_FRAME_COUNT));
    VM_SERVICE_EXPECT(vm_core_test_initialize_state(context) == MICROS_VM_CORE_OK);
    fixture->request_size = vm_core_test_build_request(context);
    fixture->executable_size = vm_core_test_build_elf(context, 1, 1, 1);
    VM_SERVICE_EXPECT(
        fixture->request_size >= MICROS_SPAWN_REQUEST_SIZE_MIN
        && micros_vm_service_initialize(&fixture->service, &context->state, &bindings)
            == MICROS_VM_SERVICE_OK
    );
    return true;
}

struct micros_ipc_message vm_service_test_message(uint32_t command, const uint64_t arguments[5])
{
    struct micros_ipc_message message;
    size_t index;

    memset(&message, 0, sizeof(message));
    message.type = MICROS_VM_MESSAGE_PM_CONTROL;
    message.source = UINT32_C(0x1002);
    message.reply_token = 777;
    vm_core_test_write_u64_le(&message.payload[0], UINT64_C(1) | (uint64_t)command << 32);
    for (index = 0; index < 5; ++index) {
        vm_core_test_write_u64_le(&message.payload[(index + 1) * 8], arguments[index]);
    }
    return message;
}

bool vm_service_test_load(struct vm_service_test_fixture *fixture)
{
    uint64_t arguments[5] = {
        101, 2, (uint64_t)fixture->executable_size << 32 | 11,
        (uint64_t)fixture->request_size << 32 | 12, 0,
    };
    struct micros_ipc_message request = vm_service_test_message(MICROS_VM_CONTROL_LOAD, arguments);
    struct micros_ipc_message reply;
    uint64_t token;
    size_t index;

    VM_SERVICE_EXPECT(
        micros_vm_service_handle_message(&fixture->service, vm_service_test_backend(), fixture,
                                         &request, &reply) == MICROS_VM_SERVICE_OK
        && !fixture->violation && reply.type == MICROS_VM_MESSAGE_PM_CONTROL_RESULT
        && reply.source == MICROS_ENDPOINT_NONE && reply.reply_token == 0
        && fixture->maps == 4 && fixture->commits == 4 && fixture->copies == 4
        && !fixture->mapped && fixture->frozen
        && fixture->context->state.phase == MICROS_VM_LOADER_FROZEN
    );
    token = 0;
    for (index = 0; index < 8; ++index) {
        token |= (uint64_t)reply.payload[16 + index] << (index * 8);
    }
    VM_SERVICE_EXPECT(
        token == 201 && vm_service_test_read_word(&reply.payload[0]) == UINT64_C(0x100000001)
        && vm_service_test_read_word(&reply.payload[8]) == 0
        && vm_service_test_read_word(&reply.payload[24]) == 301
        && vm_service_test_read_word(&reply.payload[32]) == MICROS_APPLICATION_ENTRY
        && vm_service_test_read_word(&reply.payload[40]) == UINT64_C(0x7fffff70)
        && micros_vm_state_validate(&fixture->context->state) == MICROS_VM_CORE_OK
    );
    return true;
}
