#include "tests/host/vm_service_test_internal.h"
#include "tests/host/vm_handoff_test_internal.h"
#include "tests/qemu/vm_validation_work.h"
#include "lib/vm/vm_keys.h"
#include "servers/vm/vm_internal.h"

#include <inttypes.h>
#include <stdlib.h>
#if defined(MICROS_SELECTED_VM_LAUNCHER)
#include <unistd.h>
#endif

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "VM01_FIXTURE_FAILURE line=%d expression=%s\n", __LINE__, #condition); \
            return false; \
        } \
    } while (false)

static struct micros_vm_state before_service;
static struct micros_vm_boot_info before_boot;
static struct micros_vm_handoff_state kernel_state, before_kernel;
static struct vm_handoff_test_memory before_memory;
static struct vm_service_test_fixture service;
static size_t case_count;
static size_t case_extent;
#if defined(MICROS_TEST_WORK_COUNTS)
static struct micros_test_work_snapshot snapshot;
static bool capture_active;

bool micros_vm01_work_active(void)
{
    return capture_active;
}
#endif

static uint64_t digest(uint64_t value, const void *storage, size_t size)
{
    const uint8_t *bytes = storage;

    for (size_t index = 0; index < size; ++index) {
        value = (value ^ bytes[index]) * UINT64_C(1099511628211);
    }
    return value;
}

static uint64_t service_digest(
    const struct micros_vm_state *state, const struct micros_vm_boot_info *info
)
{
    size_t start = offsetof(struct micros_vm_state, boot_summary);
    uint64_t value = digest(UINT64_C(14695981039346656037), state,
                            offsetof(struct micros_vm_state, boot_info));

    /* The address is process-local; begin/report separately require its exact fixture binding. */
    value = digest(value, (const uint8_t *)state + start, sizeof(*state) - start);
    return digest(value, info, sizeof(*info));
}

static uint64_t kernel_digest(
    const struct micros_vm_handoff_state *state, const struct vm_handoff_test_memory *memory)
{
    size_t start = offsetof(struct micros_frame_ownership, phase);
    uint64_t value = digest(UINT64_C(14695981039346656037), state, sizeof(*state));

    value = digest(value, &memory->allocator, sizeof(memory->allocator));
    value = digest(value, &memory->ownership, offsetof(struct micros_frame_ownership, allocator));
    return digest(value, (const uint8_t *)&memory->ownership + start, sizeof(memory->ownership) - start);
}

static bool begin(bool kernel)
{
    struct vm_core_test_context *context = vm_core_test_context_get();

    case_extent = 0;
    if (kernel) {
        before_kernel = kernel_state;
        REQUIRE(vm_handoff_test_memory_get()->ownership.allocator == &vm_handoff_test_memory_get()->allocator);
        before_memory = *vm_handoff_test_memory_get();
    } else {
        REQUIRE(context->state.boot_info == &context->boot_info);
        before_service = context->state;
        before_boot = context->boot_info;
    }
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(micros_test_work_begin(
        UINT64_C(0x564d30314e000001), case_count + 1,
        (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_VM)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_MEMORY)
            | (UINT32_C(1) << MICROS_WORK_OWNER_LIB_VM)
            | (UINT32_C(1) << MICROS_WORK_OWNER_LIB_SPAWN)
            | (UINT32_C(1) << MICROS_WORK_OWNER_VM_SERVICE)
    ) == MICROS_TEST_WORK_OK);
    capture_active = true;
#endif
    return true;
}

static bool report_scoped(const char *name, int result, int expected, bool kernel, bool full_valid)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    uint64_t before, after;
    uint32_t phase;
    bool preserved;

#if defined(MICROS_TEST_WORK_COUNTS)
    capture_active = false;
    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
#endif
    if (result != expected) {
        fprintf(stderr, "VM01_EXPECTATION name=%s result=%d expected=%d\n", name, result, expected);
        return false;
    }
    if (kernel) {
        struct vm_handoff_test_memory *memory = vm_handoff_test_memory_get();

        REQUIRE(memory->ownership.allocator == &memory->allocator
                && memory->ownership.allocator_identity == (uintptr_t)&memory->allocator);
        before = kernel_digest(&before_kernel, &before_memory);
        after = kernel_digest(&kernel_state, memory);
        preserved = memcmp(&before_kernel, &kernel_state, sizeof(kernel_state)) == 0
            && memcmp(&before_memory, memory, sizeof(*memory)) == 0;
        phase = (uint32_t)kernel_state.load.phase;
        REQUIRE(micros_vm_handoff_state_validate(&kernel_state)
                == (full_valid ? MICROS_VM_HANDOFF_OK : MICROS_VM_HANDOFF_ERROR_INVARIANT));
    } else {
        REQUIRE(context->state.boot_info == &context->boot_info);
        before = service_digest(&before_service, &before_boot);
        after = service_digest(&context->state, &context->boot_info);
        preserved = memcmp(&before_service, &context->state, sizeof(before_service)) == 0
            && memcmp(&before_boot, &context->boot_info, sizeof(before_boot)) == 0;
        phase = context->state.phase;
        REQUIRE(micros_vm_state_validate(&context->state)
                == (full_valid ? MICROS_VM_CORE_OK : MICROS_VM_CORE_ERROR_INVARIANT));
    }
    ++case_count;
    printf("MICROS_VM01_CASE {\"version\":1,\"case\":%zu,\"name\":\"%s\",\"result\":%d,\"extent\":%zu,"
           "\"phase\":%u,\"preserved\":%u,\"before_state\":%" PRIu64 ",\"after_state\":%" PRIu64 "}\n",
           case_count, name, result, case_extent, phase, (unsigned)preserved, before, after);
#if defined(MICROS_TEST_WORK_COUNTS)
    printf("MICROS_VM01_WORK {\"version\":%u,\"reserved0\":%u,\"producer\":%" PRIu64
           ",\"case\":%" PRIu64 ",\"owner_mask\":%u,\"reserved1\":%u,\"counts\":[",
           snapshot.version, snapshot.reserved0, snapshot.producer_label, snapshot.capture_label,
           snapshot.owner_mask, snapshot.reserved1);
    for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        printf("%s[", owner == 0 ? "" : ",");
        for (size_t kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            printf("%s%" PRIu64, kind == 0 ? "" : ",", snapshot.counts[owner][kind]);
        }
        printf("]");
    }
    printf("]}\n");
#endif
    return true;
}

static bool report(const char *name, int result, int expected, bool kernel)
{
    return report_scoped(name, result, expected, kernel, true);
}

static bool initialize_service_state(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();

    REQUIRE(vm_core_test_initialize_boot_info(context, TEST_MANAGED_FRAME_COUNT));
    REQUIRE(vm_core_test_initialize_state(context) == MICROS_VM_CORE_OK);
    return true;
}

static bool service_load_cases(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct micros_application_stack_values values = vm_core_test_stack_values(7);
    struct micros_vm_load_page_action page;
    struct micros_vm_freeze_plan freeze;
    struct micros_vm_exit_action exit;
    size_t executable_size, request_size;
    int result;

    REQUIRE(initialize_service_state());
    executable_size = vm_core_test_build_elf(context, 1, 1, 1);
    request_size = vm_core_test_build_request(context);
    REQUIRE(begin(false));
    result = micros_vm_load_prepare(&context->state, 0x1111, 7, context->executable,
                                   executable_size, context->request, request_size, &values);
    REQUIRE(report("service-prepare", result, MICROS_VM_CORE_OK, false));
    REQUIRE(context->state.transaction.page_count == 4);
    for (size_t index = 0; index < 4; ++index) {
        REQUIRE(context->state.transaction.table_frames[index] == vm_core_test_frame_address(TEST_FIRST_LOAD_FRAME + index));
        REQUIRE(context->state.transaction.data_frames[index] == vm_core_test_frame_address(TEST_FIRST_LOAD_FRAME + 4 + index));
    }
    REQUIRE(begin(false));
    result = micros_vm_load_bind(&context->state, (struct micros_process_handle){2, 7}, 0x2202);
    REQUIRE(report("service-bind", result, MICROS_VM_CORE_OK, false));
    REQUIRE(context->state.mapping_records[2].entry_count == 8);
    REQUIRE(begin(false));
    result = micros_vm_load_next_page(&context->state, &page);
    REQUIRE(report("service-next-page", result, MICROS_VM_CORE_OK, false));
    REQUIRE(page.page_index == 0 && page.virtual_address == MICROS_APPLICATION_ENTRY
            && page.physical_address == vm_core_test_frame_address(TEST_FIRST_LOAD_FRAME + 4)
            && page.permissions == (MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ
                                    | MICROS_SV39_PERMISSION_EXECUTE));
    REQUIRE(begin(false));
    result = micros_vm_load_scratch_mapped(&context->state, 0);
    REQUIRE(report("service-map", result, MICROS_VM_CORE_OK, false));
    REQUIRE(context->state.transaction.scratch_page == 1
            && context->boot_info.frame_states[TEST_FIRST_LOAD_FRAME + 4] == MICROS_VM_LOCAL_FRAME_TRANSFERABLE);
    memset(context->scratch_page, 0xa5, sizeof(context->scratch_page));
    REQUIRE(begin(false));
    result = micros_vm_load_fill_scratch(&context->state, 0, context->scratch_page, sizeof(context->scratch_page));
    REQUIRE(report("service-fill", result, MICROS_VM_CORE_OK, false));
    REQUIRE(memcmp(context->scratch_page, context->executable, 4096) == 0);
    REQUIRE(begin(false));
    result = micros_vm_load_scratch_committed(&context->state, 0);
    REQUIRE(report("service-page-commit", result, MICROS_VM_CORE_OK, false));
    for (uint32_t index = 1; index < 4; ++index) {
        REQUIRE(micros_vm_load_scratch_mapped(&context->state, index) == MICROS_VM_CORE_OK);
        REQUIRE(micros_vm_load_fill_scratch(&context->state, index, context->scratch_page,
                                           sizeof(context->scratch_page)) == MICROS_VM_CORE_OK);
        REQUIRE(micros_vm_load_scratch_committed(&context->state, index) == MICROS_VM_CORE_OK);
    }
    REQUIRE(begin(false));
    result = micros_vm_load_freeze_plan(&context->state, &freeze);
    REQUIRE(report("service-freeze-plan", result, MICROS_VM_CORE_OK, false));
    REQUIRE(freeze.load_token == 0x2202 && freeze.image_page_count == 3 && freeze.data_page_count == 4
            && freeze.entry == MICROS_APPLICATION_ENTRY && freeze.stack_pointer == UINT64_C(0x7fffff70)
            && freeze.page_plan_digest == vm_core_test_expected_page_digest(context));
    REQUIRE(begin(false));
    result = micros_vm_load_frozen(&context->state, 0x3333);
    REQUIRE(report("service-freeze", result, MICROS_VM_CORE_OK, false));
    REQUIRE(begin(false));
    result = micros_vm_load_committed(&context->state, 0x2202, 0x3333, 7);
    REQUIRE(report("service-commit", result, MICROS_VM_CORE_OK, false));
    REQUIRE(context->state.mapping_records[2].phase == MICROS_VM_MAPPING_RECORD_ACTIVE
            && vm_core_test_bytes_are_zero(&context->state.transaction, sizeof(context->state.transaction)));
    REQUIRE(begin(false));
    result = micros_vm_exit_release_begin(&context->state, 0x2202, 0x3333, 7, &exit);
    REQUIRE(report("service-exit-begin", result, MICROS_VM_CORE_OK, false));
    REQUIRE(exit.process_slot == 2 && exit.process_generation == 7 && exit.load_token == 0x2202
            && exit.mapping_generation == 0x3333 && exit.semantic_pid == 7);
    REQUIRE(begin(false));
    result = micros_vm_exit_release_complete(&context->state, &exit);
    REQUIRE(report("service-exit-complete", result, MICROS_VM_CORE_OK, false));
    REQUIRE(vm_core_test_bytes_are_zero(context->state.mapping_records, sizeof(context->state.mapping_records)));
    for (size_t index = TEST_FIRST_FREE_FRAME; index < TEST_MANAGED_FRAME_COUNT; ++index) {
        REQUIRE(context->boot_info.frame_states[index] == MICROS_VM_LOCAL_FRAME_FREE);
    }
    return true;
}

static bool service_abort_cases(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    int result;

    REQUIRE(initialize_service_state() && vm_core_test_prepare_minimal_load(context));
    REQUIRE(micros_vm_load_bind(&context->state, (struct micros_process_handle){2, 7}, 0x2202) == MICROS_VM_CORE_OK);
    REQUIRE(micros_vm_load_scratch_mapped(&context->state, 0) == MICROS_VM_CORE_OK);
    REQUIRE(begin(false));
    result = micros_vm_load_aborted(&context->state, 0x2202, 0);
    REQUIRE(report("service-abort-loading", result, MICROS_VM_CORE_OK, false));
    REQUIRE(vm_core_test_run_to_frozen(context, 0x3333));
    REQUIRE(begin(false));
    result = micros_vm_load_aborted(&context->state, 0x2202, 0x3333);
    REQUIRE(report("service-abort-frozen", result, MICROS_VM_CORE_OK, false));
    REQUIRE(vm_core_test_run_to_frozen(context, 0x3333));
    REQUIRE(begin(false));
    result = micros_vm_load_aborted(&context->state, 0x2223, 0x3333);
    REQUIRE(report("service-stale-token", result, MICROS_VM_CORE_ERROR_STATE, false));
    REQUIRE(begin(false));
    result = micros_vm_state_validate(&context->state);
    REQUIRE(report("service-diagnostic", result, MICROS_VM_CORE_OK, false));
    return true;
}

static bool kernel_freeze(void)
{
    struct micros_vm_load_freeze_plan request = vm_handoff_test_valid_freeze_plan(&kernel_state);
    struct micros_vm_load_freeze_state_plan plan;

    REQUIRE(micros_vm_load_freeze_preflight(
        &kernel_state, &vm_handoff_test_memory_get()->ownership, &request, &plan) == MICROS_VM_HANDOFF_OK);
    REQUIRE(vm_handoff_test_commit_freeze(&kernel_state, &plan));
    return true;
}

static bool kernel_load_cases(void)
{
    struct micros_process_handle process = vm_handoff_test_dynamic_process();
    struct micros_vm_load_begin_plan start;
    struct micros_vm_scratch_map_request map = {
        .load_token = process.slot, .virtual_address = MICROS_APPLICATION_ENTRY, .physical_address = 0x800000,
        .permissions = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE,
    };
    struct micros_vm_scratch_commit_request commit = {
        .load_token = process.slot, .virtual_address = MICROS_APPLICATION_ENTRY, .physical_address = 0x800000,
    };
    struct micros_vm_scratch_plan page;
    struct micros_vm_load_freeze_plan request;
    struct micros_vm_load_freeze_state_plan freeze;
    struct micros_vm_exit_release_action exit;
    int result;

    kernel_state = vm_handoff_test_handed_off_state();
    REQUIRE(begin(true));
    result = micros_vm_load_begin_preflight(&kernel_state, 101, process, true, &start);
    REQUIRE(result == MICROS_VM_HANDOFF_OK);
    micros_vm_load_begin_commit_prevalidated(&kernel_state, &start);
    REQUIRE(report("kernel-begin", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(start.result.load_token == process.slot && start.result.process_slot == process.slot
            && start.result.process_generation == process.generation);
    REQUIRE(begin(true));
    result = micros_vm_scratch_map_preflight(&kernel_state, &vm_handoff_test_memory_get()->ownership, &map, &page);
    REQUIRE(result == MICROS_VM_HANDOFF_OK);
    REQUIRE(vm_handoff_test_commit_scratch(&kernel_state, &page));
    REQUIRE(report("kernel-map", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(kernel_state.load.scratch.physical_address == map.physical_address);
    REQUIRE(begin(true));
    result = micros_vm_scratch_commit_preflight(&kernel_state, &commit, &page);
    REQUIRE(result == MICROS_VM_HANDOFF_OK);
    micros_vm_scratch_commit_commit_prevalidated(&kernel_state, &page);
    REQUIRE(report("kernel-page-commit", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(kernel_state.mappings[process.slot].entry_count == 1);
    REQUIRE(vm_handoff_test_append_page(&kernel_state, MICROS_APPLICATION_ENTRY + 4096, 0x801000,
                                        MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ));
    REQUIRE(vm_handoff_test_append_page(&kernel_state, MICROS_APPLICATION_ENTRY + 8192, 0x802000,
                                        MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ
                                            | MICROS_SV39_PERMISSION_WRITE));
    REQUIRE(vm_handoff_test_append_page(&kernel_state, MICROS_APPLICATION_STACK_BASE, 0x803000,
                                        MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ
                                            | MICROS_SV39_PERMISSION_WRITE));
    request = vm_handoff_test_valid_freeze_plan(&kernel_state);
    REQUIRE(begin(true));
    result = micros_vm_load_freeze_preflight(&kernel_state, &vm_handoff_test_memory_get()->ownership, &request, &freeze);
    REQUIRE(result == MICROS_VM_HANDOFF_OK);
    REQUIRE(vm_handoff_test_commit_freeze(&kernel_state, &freeze));
    REQUIRE(report("kernel-freeze", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(freeze.result.mapping_generation == 1 && kernel_state.mappings[process.slot].root_physical_address == 0x810000);
    REQUIRE(begin(true));
    result = micros_vm_load_mark_prepared(&kernel_state, process, 101, start.load_token, 1);
    REQUIRE(report("kernel-prepare", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(begin(true));
    result = micros_vm_load_mark_unprepared(&kernel_state, process, 101, start.load_token, 1);
    REQUIRE(report("kernel-unprepare", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(micros_vm_load_mark_prepared(&kernel_state, process, 101, start.load_token, 1) == MICROS_VM_HANDOFF_OK);
    REQUIRE(begin(true));
    result = micros_vm_load_mark_activated(&kernel_state, process, 101, start.load_token, 1);
    REQUIRE(report("kernel-activate", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(begin(true));
    result = micros_vm_load_commit_preflight(&kernel_state, start.load_token, 1);
    REQUIRE(result == MICROS_VM_HANDOFF_OK);
    micros_vm_load_commit_commit_prevalidated(&kernel_state);
    REQUIRE(report("kernel-commit", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(kernel_state.mappings[process.slot].phase == MICROS_VM_KERNEL_MAPPING_ACTIVE);
    REQUIRE(begin(true));
    result = micros_vm_load_mark_exit_detached(&kernel_state, process, 701, start.load_token, 1);
    REQUIRE(report("kernel-exit-detach", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(begin(true));
    result = micros_vm_exit_release_preflight(&kernel_state, 701, start.load_token, 1, &exit);
    REQUIRE(result == MICROS_VM_HANDOFF_OK && exit.entry_count == 4
            && exit.process.slot == process.slot && exit.process.generation == process.generation);
    REQUIRE(vm_handoff_test_commit_exit(&kernel_state, &exit));
    REQUIRE(report("kernel-exit-release", result, MICROS_VM_HANDOFF_OK, true));
    return true;
}

static bool kernel_abort_cases(void)
{
    struct micros_vm_load_abort_action action;
    int result;

    kernel_state = vm_handoff_test_handed_off_state();
    REQUIRE(vm_handoff_test_begin_and_fill_load(&kernel_state, 101));
    REQUIRE(begin(true));
    result = micros_vm_load_abort_preflight(&kernel_state, kernel_state.load.load_token, 0, &action);
    REQUIRE(result == MICROS_VM_HANDOFF_OK && !action.frozen && action.entry_count == 4);
    REQUIRE(vm_handoff_test_commit_abort(&kernel_state, &action));
    REQUIRE(report("kernel-abort-loading", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(vm_handoff_test_begin_and_fill_load(&kernel_state, 102) && kernel_freeze());
    REQUIRE(begin(true));
    result = micros_vm_load_abort_preflight(&kernel_state, kernel_state.load.load_token, 1, &action);
    REQUIRE(result == MICROS_VM_HANDOFF_OK && action.frozen && action.root_physical_address == 0x810000);
    REQUIRE(vm_handoff_test_commit_abort(&kernel_state, &action));
    REQUIRE(report("kernel-abort-frozen", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(begin(true));
    result = micros_vm_handoff_state_validate(&kernel_state);
    REQUIRE(report("kernel-diagnostic", result, MICROS_VM_HANDOFF_OK, true));
    return true;
}

static bool service_message(uint32_t command, const uint64_t arguments[5], const char *name)
{
    struct micros_ipc_message request = vm_service_test_message(command, arguments);
    struct micros_ipc_message reply;
    int result;

    memset(&reply, 0xa5, sizeof(reply));
    REQUIRE(begin(false));
    result = micros_vm_service_handle_message(
        &service.service, vm_service_test_backend(), &service, &request, &reply
    );
    REQUIRE(report(name, result, MICROS_VM_SERVICE_OK, false));
    REQUIRE(!service.violation && reply.type == MICROS_VM_MESSAGE_PM_CONTROL_RESULT
            && reply.reply_token == 0 && reply.source == MICROS_ENDPOINT_NONE
            && vm_service_test_read_word(&reply.payload[0]) == (UINT64_C(1) | (uint64_t)command << 32)
            && vm_service_test_read_word(&reply.payload[8]) == MICROS_VM_CONTROL_RESULT_OK);
    if (command == MICROS_VM_CONTROL_LOAD) {
        REQUIRE(vm_service_test_read_word(&reply.payload[16]) == 195
                && vm_service_test_read_word(&reply.payload[24]) == 301
                && vm_service_test_read_word(&reply.payload[32]) == MICROS_APPLICATION_ENTRY
                && vm_service_test_read_word(&reply.payload[40]) == UINT64_C(0x7fffff70)
                && service.maps == 4 && service.commits == 4 && service.copies == 4);
    } else {
        REQUIRE(vm_core_test_bytes_are_zero(&reply.payload[16], 32));
    }
    return true;
}

static bool message_cases(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    uint64_t load[5];
    const uint64_t frozen[5] = {195, 301, 2, 0, 0};
    const uint64_t exit[5] = {701, 195, 301, 2, 0};

    REQUIRE(vm_service_test_initialize(&service, context));
    load[0] = 101;
    load[1] = 2;
    load[2] = (uint64_t)service.executable_size << 32 | 11;
    load[3] = (uint64_t)service.request_size << 32 | 12;
    load[4] = 0;
    REQUIRE(service_message(MICROS_VM_CONTROL_LOAD, load, "message-load"));
    REQUIRE(service_message(MICROS_VM_CONTROL_ABORT_LOAD, frozen, "message-abort"));
    REQUIRE(service.aborts == 1 && !service.frozen);
    REQUIRE(vm_service_test_initialize(&service, context) && vm_service_test_load(&service));
    REQUIRE(service_message(MICROS_VM_CONTROL_COMMIT_LOAD, frozen, "message-commit"));
    REQUIRE(service.load_commits == 1 && service.active);
    REQUIRE(service_message(MICROS_VM_CONTROL_EXIT_RELEASE, exit, "message-exit"));
    REQUIRE(service.exits == 1 && !service.active);
    return true;
}

static bool service_scope_cases(void)
{
    static const char *const names[] = {
        "service-unvisited-frame", "service-unvisited-input", "service-unvisited-record",
        "service-selected-count", "service-zero-geometry", "service-overflow-geometry",
    };
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct micros_vm_load_page_action page, sentinel;
    int result;

    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        REQUIRE(initialize_service_state() && vm_core_test_prepare_minimal_load(context));
        REQUIRE(micros_vm_load_bind(&context->state, (struct micros_process_handle){2, 7}, 0x2202) == MICROS_VM_CORE_OK);
        if (index == 0) {
            context->boot_info.frame_states[MICROS_VM_MAX_MANAGED_FRAMES - 1] = MICROS_VM_FRAME_FREE;
        } else if (index == 1) {
            context->state.executable_bytes[MICROS_APPLICATION_EXECUTABLE_SIZE_MAX - 1] = 1;
        } else if (index == 2) {
            context->state.mapping_records[MICROS_VM_MAPPING_RECORD_CAPACITY - 1].semantic_pid = 99;
        } else if (index == 3) {
            context->state.mapping_records[2].entry_count = MICROS_VM_MAPPING_ENTRY_CAPACITY + 1;
        } else {
            uint64_t count = index == 4 ? 0 : MICROS_VM_MAX_MANAGED_FRAMES + 1;
            context->state.boot_summary.managed_frame_count = count;
            context->boot_info.header.managed_frame_count = count;
        }
        memset(&sentinel, 0xa5, sizeof(sentinel));
        page = sentinel;
        REQUIRE(begin(false));
        result = micros_vm_load_next_page(&context->state, &page);
        REQUIRE(report_scoped(names[index], result,
                              index < 3 ? MICROS_VM_CORE_OK : MICROS_VM_CORE_ERROR_INVARIANT,
                              false, false));
        if (index < 3) {
            REQUIRE(page.virtual_address == MICROS_APPLICATION_ENTRY
                    && page.physical_address == vm_core_test_frame_address(TEST_FIRST_LOAD_FRAME + 4) && page.page_index == 0);
        } else {
            REQUIRE(memcmp(&page, &sentinel, sizeof(page)) == 0);
        }
    }
    REQUIRE(initialize_service_state() && vm_core_test_run_to_frozen(context, 0x3333));
    context->state.transaction.data_frames[3] = UINT64_C(0xfffffffffffff000);
    context->state.mapping_records[2].entries[7].physical_address = UINT64_C(0xfffffffffffff000);
    REQUIRE(begin(false));
    result = micros_vm_load_aborted(&context->state, 0x2202, 0x3333);
    REQUIRE(report_scoped("service-cleanup-preflight", result, MICROS_VM_CORE_ERROR_INVARIANT, false, false));
    REQUIRE(memcmp(&before_service, &context->state, sizeof(before_service)) == 0
            && memcmp(&before_boot, &context->boot_info, sizeof(before_boot)) == 0);
    return true;
}

static bool message_preflight_cases(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    const uint64_t arguments[5] = {195, 301, 2, 0, 0};
    struct micros_ipc_message request, reply, sentinel;
    int result;

    for (uint32_t command = MICROS_VM_CONTROL_ABORT_LOAD; command <= MICROS_VM_CONTROL_COMMIT_LOAD; ++command) {
        REQUIRE(vm_service_test_initialize(&service, context) && vm_service_test_load(&service));
        context->boot_info.frame_states[TEST_FIRST_LOAD_FRAME + 7] = MICROS_VM_LOCAL_FRAME_FREE;
        request = vm_service_test_message(command, arguments);
        memset(&sentinel, 0xa5, sizeof(sentinel));
        reply = sentinel;
        REQUIRE(begin(false));
        result = micros_vm_service_handle_message(
            &service.service, vm_service_test_backend(), &service, &request, &reply
        );
        REQUIRE(report_scoped(command == MICROS_VM_CONTROL_ABORT_LOAD
                              ? "message-abort-preflight" : "message-commit-preflight",
                              result, MICROS_VM_SERVICE_ERROR_INVARIANT, false, false));
        REQUIRE(service.aborts == 0 && service.load_commits == 0 && service.frozen
                && memcmp(&reply, &sentinel, sizeof(reply)) == 0);
    }
    REQUIRE(vm_service_test_initialize(&service, context));
    {
        const uint64_t load[5] = {
            101, 2, (uint64_t)service.executable_size << 32 | 11,
            (uint64_t)service.request_size << 32 | 12, 0,
        };

        request = vm_service_test_message(MICROS_VM_CONTROL_LOAD, load);
    }
    context->state.mapping_records[MICROS_VM_MAPPING_RECORD_CAPACITY - 1].entry_count = 255;
    reply = sentinel;
    REQUIRE(begin(false));
    result = micros_vm_service_handle_message(
        &service.service, vm_service_test_backend(), &service, &request, &reply
    );
    REQUIRE(report_scoped("message-load-preflight", result, MICROS_VM_SERVICE_ERROR_INVARIANT, false, false));
    REQUIRE(service.copies == 0 && service.begins == 0
            && memcmp(&reply, &sentinel, sizeof(reply)) == 0);
    return true;
}

static bool kernel_scope_cases(void)
{
    struct micros_process_handle process = vm_handoff_test_dynamic_process();
    struct micros_vm_load_begin_plan start;
    struct micros_vm_scratch_map_request map = {
        .load_token = process.slot, .virtual_address = MICROS_APPLICATION_ENTRY, .physical_address = 0x800000,
        .permissions = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE,
    };
    struct micros_vm_scratch_commit_request commit = {
        .load_token = process.slot, .virtual_address = MICROS_APPLICATION_ENTRY, .physical_address = 0x800000,
    };
    struct micros_vm_scratch_plan page, sentinel;
    struct micros_frame_vm_claim_plan claim;
    struct micros_frame_vm_freeze_plan freeze;
    const uint64_t tables[] = {0x900000, 0x901000, 0x902000, 0x903000};
    int result;

    kernel_state = vm_handoff_test_handed_off_state();
    REQUIRE(micros_vm_load_begin_preflight(&kernel_state, 101, process, true, &start) == MICROS_VM_HANDOFF_OK);
    micros_vm_load_begin_commit_prevalidated(&kernel_state, &start);
    REQUIRE(micros_vm_scratch_map_preflight(
        &kernel_state, &vm_handoff_test_memory_get()->ownership, &map, &page) == MICROS_VM_HANDOFF_OK);
    REQUIRE(vm_handoff_test_commit_scratch(&kernel_state, &page));
    kernel_state.mappings[MICROS_PROCESS_CAPACITY - 1].entry_count = 255;
    REQUIRE(begin(true));
    result = micros_vm_scratch_commit_preflight(&kernel_state, &commit, &page);
    REQUIRE(report_scoped("kernel-unvisited-record", result, MICROS_VM_HANDOFF_OK, true, false));
    REQUIRE(page.entry.physical_address == 0x800000 && page.entry.virtual_address == MICROS_APPLICATION_ENTRY);

    kernel_state = vm_handoff_test_handed_off_state();
    REQUIRE(micros_vm_load_begin_preflight(&kernel_state, 101, process, true, &start) == MICROS_VM_HANDOFF_OK);
    micros_vm_load_begin_commit_prevalidated(&kernel_state, &start);
    kernel_state.mappings[MICROS_PROCESS_CAPACITY - 1].entry_count = 255;
    memset(&sentinel, 0xa5, sizeof(sentinel));
    page = sentinel;
    REQUIRE(begin(true));
    result = micros_vm_scratch_map_preflight(&kernel_state, &vm_handoff_test_memory_get()->ownership, &map, &page);
    REQUIRE(report_scoped("kernel-owner-unvisited", result, MICROS_VM_HANDOFF_OK, true, false));
    REQUIRE(page.entry.physical_address == map.physical_address);
    REQUIRE(micros_frame_ownership_vm_claim_preflight(
        &vm_handoff_test_memory_get()->ownership, map.physical_address, &claim) == MICROS_FRAME_OWNERSHIP_OK);
    micros_frame_ownership_vm_claim_commit_prevalidated(&vm_handoff_test_memory_get()->ownership, &claim);
    REQUIRE(micros_frame_ownership_vm_freeze_preflight(
        &vm_handoff_test_memory_get()->ownership, (struct micros_process_handle){63, 1},
        &map.physical_address, 1, tables, &freeze) == MICROS_FRAME_OWNERSHIP_OK);
    micros_frame_ownership_vm_freeze_commit_prevalidated(&vm_handoff_test_memory_get()->ownership, &freeze);
    REQUIRE(micros_frame_ownership_validate(&vm_handoff_test_memory_get()->ownership) == MICROS_FRAME_OWNERSHIP_OK);
    page = sentinel;
    REQUIRE(begin(true));
    result = micros_vm_scratch_map_preflight(&kernel_state, &vm_handoff_test_memory_get()->ownership, &map, &page);
    REQUIRE(report_scoped("kernel-scanned-count", result, MICROS_VM_HANDOFF_ERROR_INVARIANT, true, false));
    REQUIRE(memcmp(&page, &sentinel, sizeof(page)) == 0);

    kernel_state = vm_handoff_test_handed_off_state();
    REQUIRE(micros_vm_load_begin_preflight(&kernel_state, 101, process, true, &start) == MICROS_VM_HANDOFF_OK);
    micros_vm_load_begin_commit_prevalidated(&kernel_state, &start);
    kernel_state.load.process.slot = MICROS_PROCESS_CAPACITY;
    page = sentinel;
    REQUIRE(begin(true));
    result = micros_vm_scratch_map_preflight(&kernel_state, &vm_handoff_test_memory_get()->ownership, &map, &page);
    REQUIRE(report_scoped("kernel-selected-slot", result, MICROS_VM_HANDOFF_ERROR_INVARIANT, true, false));
    REQUIRE(memcmp(&page, &sentinel, sizeof(page)) == 0);
    return true;
}

static bool initialize_fragmented_service(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct micros_vm_boot_summary summary;
    struct micros_vm_boot_info *info = &context->boot_info;
    uint64_t cursor = vm_core_test_frame_address(0);
    uint64_t frames = 0;
    size_t managed = 0, reserved = 0;

    REQUIRE(vm_core_test_initialize_boot_info(context, 111));
    for (size_t memory = 0; memory < 16; ++memory) {
        uint64_t base = cursor;

        for (size_t part = 0; part < 6; ++part) {
            uint64_t count = memory == 0 && part == 0 ? 16 : 1;

            info->managed_ranges[managed++] = (struct micros_vm_managed_range){cursor, count, frames};
            frames += count;
            cursor += count * MICROS_FRAME_SIZE;
            if (part != 5) {
                info->reserved_ranges[reserved++] = (struct micros_vm_physical_range){cursor, MICROS_FRAME_SIZE};
                cursor += MICROS_FRAME_SIZE;
            }
        }
        info->memory_ranges[memory] = (struct micros_vm_physical_range){base, cursor - base};
        cursor += MICROS_FRAME_SIZE;
    }
    REQUIRE(frames == 111 && managed == MICROS_VM_MAX_MANAGED_RANGES && reserved == MICROS_VM_MAX_RESERVED_RANGES);
    info->header.memory_range_count = MICROS_VM_MAX_MEMORY_RANGES;
    info->header.reserved_range_count = MICROS_VM_MAX_RESERVED_RANGES;
    info->header.managed_range_count = MICROS_VM_MAX_MANAGED_RANGES;
    info->header.digest = 0;
    REQUIRE(micros_vm_boot_finalize(info, &summary) == MICROS_VM_BOOT_OK);
    REQUIRE(vm_core_test_initialize_state(context) == MICROS_VM_CORE_OK);
    return true;
}

static bool service_extent_cases(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct micros_application_stack_values values = vm_core_test_stack_values(7);
    struct micros_vm_load_page_action page;
    size_t executable_size, request_size;
    int result;

    for (unsigned shape = 0; shape < 2; ++shape) {
        if (shape == 0) {
            REQUIRE(vm_core_test_initialize_boot_info(context, MICROS_VM_MAX_MANAGED_FRAMES));
            REQUIRE(vm_core_test_initialize_state(context) == MICROS_VM_CORE_OK);
        } else {
            REQUIRE(initialize_fragmented_service());
        }
        executable_size = vm_core_test_build_elf(context, shape == 0 ? 1 : 13, 1, 1);
        request_size = vm_core_test_build_request(context);
        REQUIRE(micros_vm_load_prepare(&context->state, 0x1111, 7, context->executable,
                                       executable_size, context->request, request_size, &values) == MICROS_VM_CORE_OK);
        REQUIRE(micros_vm_load_bind(&context->state, (struct micros_process_handle){2, 7}, 0x2202) == MICROS_VM_CORE_OK);
        REQUIRE(context->state.transaction.page_count == (shape == 0 ? 4 : 16));
        REQUIRE(begin(false));
        result = micros_vm_load_next_page(&context->state, &page);
        REQUIRE(report(shape == 0 ? "service-max-frames" : "service-max-ranges-pages",
                       result, MICROS_VM_CORE_OK, false));
        REQUIRE(page.page_index == 0 && page.physical_address == (shape == 0
                    ? vm_core_test_frame_address(TEST_FIRST_LOAD_FRAME + 4)
                    : context->boot_info.managed_ranges[80].base)
                && page.virtual_address == MICROS_APPLICATION_ENTRY);
    }
    return true;
}

static bool kernel_dense_case(void)
{
    struct micros_process_handle selected = vm_handoff_test_dynamic_process();
    int result;

    kernel_state = vm_handoff_test_handed_off_state();
    REQUIRE(vm_handoff_test_begin_and_fill_load(&kernel_state, 101) && kernel_freeze());
    for (size_t slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        struct micros_vm_kernel_mapping_record *record;
        uint64_t offset = (slot + 1) * UINT64_C(0x1000000);

        if (slot == selected.slot || slot == kernel_state.process.slot) {
            continue;
        }
        record = &kernel_state.mappings[slot];
        *record = kernel_state.mappings[selected.slot];
        record->process = (struct micros_process_handle){(uint16_t)slot, 1};
        record->load_token = 64 + slot;
        record->mapping_generation = slot + 2;
        record->phase = MICROS_VM_KERNEL_MAPPING_ACTIVE;
        record->root_physical_address += offset;
        for (size_t index = 0; index < 4; ++index) {
            record->table_frames[index] += offset;
            record->entries[index].physical_address += offset;
        }
    }
    kernel_state.last_load_token = 2 * MICROS_PROCESS_CAPACITY - 1;
    kernel_state.last_mapping_generation = MICROS_PROCESS_CAPACITY + 1;
    REQUIRE(micros_vm_handoff_state_validate(&kernel_state) == MICROS_VM_HANDOFF_OK);
    REQUIRE(begin(true));
    result = micros_vm_load_mark_prepared(&kernel_state, selected, 101, kernel_state.load.load_token, 1);
    REQUIRE(report("kernel-dense-records", result, MICROS_VM_HANDOFF_OK, true));
    return true;
}

static bool initialize_boot_mapping_extent(size_t mapping_count, size_t stride)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct micros_vm_boot_info *info = &context->boot_info;
    struct micros_vm_boot_summary summary;
    struct micros_vm_mapping service_mapping;
    size_t managed = mapping_count * stride + TEST_FIRST_FREE_FRAME;

    REQUIRE(mapping_count >= 3 && mapping_count <= MICROS_VM_MAX_STATIC_MAPPINGS);
    REQUIRE(vm_core_test_initialize_boot_info(context, managed));
    service_mapping = info->mappings[2];
    for (size_t index = 2; index + 1 < mapping_count; ++index) {
        size_t frame_index = TEST_FIRST_FREE_FRAME + (mapping_count - 2 - index) * stride;

        info->mappings[index] = info->mappings[0];
        info->mappings[index].virtual_address = MICROS_USER_VIRTUAL_BASE + index * MICROS_FRAME_SIZE;
        info->mappings[index].physical_address = vm_core_test_frame_address(frame_index);
        info->frame_states[frame_index] = MICROS_VM_FRAME_SELF_WIRED;
    }
    info->mappings[mapping_count - 1] = service_mapping;
    info->header.mapping_count = (uint32_t)mapping_count;
    info->header.vm_self_wired_frame_count = mapping_count - 1;
    info->header.free_frame_count = managed - mapping_count - info->header.kernel_frame_count;
    info->address_spaces[0].mapping_count = (uint16_t)(mapping_count - 1);
    info->address_spaces[1].mapping_index = (uint32_t)(mapping_count - 1);
    info->header.digest = 0;
    REQUIRE(micros_vm_boot_finalize(info, &summary) == MICROS_VM_BOOT_OK);
    REQUIRE(vm_core_test_initialize_state(context) == MICROS_VM_CORE_OK);
    return true;
}

static bool boot_mapping_cases(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct micros_vm_boot_summary summary, sentinel;
    int result;

    for (unsigned shape = 0; shape < 2; ++shape) {
        size_t mappings = shape == 0 ? 257 : MICROS_VM_MAX_STATIC_MAPPINGS;

        REQUIRE(initialize_boot_mapping_extent(mappings, shape == 0 ? 3 : 1));
        REQUIRE(begin(false));
        case_extent = context->boot_info.header.mapping_count;
        result = micros_vm_boot_validate(&context->boot_info, &summary);
        REQUIRE(report(shape == 0 ? "boot-mapping-sparse" : "boot-mapping-max",
                       result, MICROS_VM_BOOT_OK, false));
        REQUIRE(summary.mapping_count == mappings
                && summary.managed_frame_count == context->boot_info.header.managed_frame_count
                && summary.vm_self_wired_frame_count == mappings - 1
                && summary.digest == context->state.boot_summary.digest);
    }
    context->boot_info.mappings[MICROS_VM_MAX_STATIC_MAPPINGS - 2].physical_address =
        context->boot_info.mappings[0].physical_address;
    memset(&sentinel, 0xa5, sizeof(sentinel));
    summary = sentinel;
    REQUIRE(begin(false));
    case_extent = context->boot_info.header.mapping_count;
    result = micros_vm_boot_validate(&context->boot_info, &summary);
    REQUIRE(report_scoped("boot-mapping-duplicate", result, MICROS_VM_BOOT_ERROR_MAPPING, false, false));
    REQUIRE(memcmp(&summary, &sentinel, sizeof(summary)) == 0);
    return true;
}

static bool key_workspace_cases(void)
{
    struct micros_vm_key_view view, other, sentinel, copied;
    uint32_t *words32;
    uint64_t *words64;
    uint64_t reference[] = {
        UINT64_MAX, 0, UINT64_C(0x8000000000000000), 1,
        UINT64_MAX, UINT64_C(0x7fffffffffffffff), 0, 7,
        UINT64_C(0x8000000000000001), 6, 7, 2,
    };

    REQUIRE(!micros_vm_keys_begin32(NULL));
    REQUIRE(!micros_vm_keys_append32(NULL, 1));
    REQUIRE(!micros_vm_keys_sort(NULL) && !micros_vm_keys_end(NULL));
    REQUIRE(micros_vm_keys_begin32(&view) && view.count == 0);
    memset(&sentinel, 0xa5, sizeof(sentinel));
    other = sentinel;
    REQUIRE(!micros_vm_keys_begin64(&other) && memcmp(&other, &sentinel, sizeof(other)) == 0);
    REQUIRE(!micros_vm_keys_begin64(&view) && view.count == 0);
    REQUIRE(!micros_vm_keys_append64(&view, UINT64_MAX));
    REQUIRE(micros_vm_keys_sort(&view));
    for (size_t index = 0; index < MICROS_VM_KEYS32_CAPACITY; ++index) {
        REQUIRE(micros_vm_keys_append32(&view, UINT32_MAX - (uint32_t)index));
    }
    REQUIRE(view.count == MICROS_VM_KEYS32_CAPACITY && !micros_vm_keys_append32(&view, 0));
    copied = view;
    REQUIRE(!micros_vm_keys_sort(&copied) && !micros_vm_keys_end(&copied));
    REQUIRE(micros_vm_keys_sort(&view));
    for (size_t index = 0; index < view.count; ++index) {
        REQUIRE(view.keys32[index] == UINT32_MAX - (MICROS_VM_KEYS32_CAPACITY - 1) + index);
    }
    words32 = view.keys32;
    REQUIRE(micros_vm_keys_end(&view));
    REQUIRE(view.count == 0 && view.keys32 == NULL && view.keys64 == NULL);
    REQUIRE(!micros_vm_keys_sort(&view) && !micros_vm_keys_end(&view));
    for (size_t index = 0; index < MICROS_VM_KEYS32_CAPACITY; ++index) {
        REQUIRE(words32[index] == 0);
    }

    REQUIRE(micros_vm_keys_begin64(&view));
    for (size_t index = 0; index < sizeof(reference) / sizeof(reference[0]); ++index) {
        REQUIRE(micros_vm_keys_append64(&view, reference[index]));
    }
    for (size_t index = 1; index < sizeof(reference) / sizeof(reference[0]); ++index) {
        uint64_t value = reference[index];
        size_t position = index;

        while (position != 0 && reference[position - 1] > value) {
            reference[position] = reference[position - 1];
            --position;
        }
        reference[position] = value;
    }
    REQUIRE(!micros_vm_keys_append32(&view, 1) && micros_vm_keys_sort(&view));
    REQUIRE(memcmp(view.keys64, reference, sizeof(reference)) == 0);
    words64 = view.keys64;
    REQUIRE(micros_vm_keys_end(&view));
    for (size_t index = 0; index < sizeof(reference) / sizeof(reference[0]); ++index) {
        REQUIRE(words64[index] == 0);
    }
    REQUIRE(micros_vm_keys_begin64(&view));
    for (size_t index = 0; index < MICROS_VM_KEYS64_CAPACITY; ++index) {
        REQUIRE(micros_vm_keys_append64(&view, UINT64_MAX - index));
    }
    REQUIRE(!micros_vm_keys_append64(&view, 0) && micros_vm_keys_sort(&view));
    for (size_t index = 0; index < view.count; ++index) {
        REQUIRE(view.keys64[index] == UINT64_MAX - (MICROS_VM_KEYS64_CAPACITY - 1) + index);
    }
    REQUIRE(micros_vm_keys_end(&view));
    REQUIRE(micros_vm_keys_begin32(&view) && micros_vm_keys_append32(&view, 0));
    REQUIRE(micros_vm_keys_sort(&view) && view.count == 1 && view.keys32[0] == 0);
    REQUIRE(micros_vm_keys_end(&view));
    REQUIRE(micros_vm_keys_begin64(&view));
    REQUIRE(micros_vm_keys_append_page(&view, UINT64_C(0x9000), 0));
    REQUIRE(micros_vm_keys_append_page(&view, UINT64_C(0x1000), 1));
    REQUIRE(micros_vm_keys_append_page(&view, UINT64_C(0x9000), 2));
    REQUIRE(micros_vm_keys_append_page(&view, UINT64_C(0x1000), 3));
    size_t duplicate = SIZE_MAX;
    REQUIRE(micros_vm_keys_finish_pages(&view, &duplicate) && duplicate == 2);
    REQUIRE(micros_vm_keys_begin64(&view));
    REQUIRE(micros_vm_keys_append_page(&view, 1, 0) && view.count == 0);
    REQUIRE(micros_vm_keys_append_page(&view, UINT64_MAX - 4095, 1));
    REQUIRE(micros_vm_keys_append_page(&view, UINT64_MAX - 4095, 4095));
    REQUIRE(!micros_vm_keys_append_page(&view, 0, 4096));
    REQUIRE(micros_vm_keys_finish_pages(&view, &duplicate) && duplicate == 4095);
    REQUIRE(micros_vm_keys_begin64(&view));
    REQUIRE(micros_vm_keys_append_page(&view, 0, 7));
    REQUIRE(micros_vm_keys_finish_pages(&view, &duplicate) && duplicate == SIZE_MAX);
    return true;
}

static bool geometry_lookup_cases(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    uint64_t index = UINT64_MAX;
    uint64_t physical = UINT64_MAX;
    uint64_t last;
    int result;

    REQUIRE(initialize_fragmented_service());
    last = context->boot_info.managed_ranges[MICROS_VM_MAX_MANAGED_RANGES - 1].base;
    REQUIRE(begin(false));
    case_extent = context->boot_info.header.managed_range_count;
    result = vm_core_physical_to_frame_index(&context->state, last, &index)
        ? MICROS_VM_CORE_OK : MICROS_VM_CORE_ERROR_STATE;
    REQUIRE(report("service-range-to-index", result, MICROS_VM_CORE_OK, false));
    REQUIRE(index == context->state.boot_summary.managed_frame_count - 1);
    REQUIRE(begin(false));
    case_extent = context->boot_info.header.managed_range_count;
    result = vm_core_frame_index_to_physical(&context->state, index, &physical)
        ? MICROS_VM_CORE_OK : MICROS_VM_CORE_ERROR_STATE;
    REQUIRE(report("service-index-to-range", result, MICROS_VM_CORE_OK, false));
    REQUIRE(physical == last);

    for (unsigned boundary = 0; boundary < 3; ++boundary) {
        uint64_t address = boundary == 0 ? context->boot_info.reserved_ranges[0].base
            : boundary == 1 ? last + MICROS_FRAME_SIZE : last + 1;

        index = UINT64_MAX;
        REQUIRE(!vm_core_physical_to_frame_index(&context->state, address, &index));
        REQUIRE(index == UINT64_MAX);
    }
    physical = UINT64_MAX;
    REQUIRE(!vm_core_frame_index_to_physical(
        &context->state, context->state.boot_summary.managed_frame_count, &physical
    ));
    REQUIRE(physical == UINT64_MAX);
    REQUIRE(!vm_core_physical_to_frame_index(NULL, 0, NULL));
    REQUIRE(!vm_core_physical_to_frame_index(NULL, 1, &index));
    before_service.boot_info = NULL;
    REQUIRE(!vm_core_frame_index_to_physical(&before_service, 0, NULL));
    REQUIRE(!vm_core_frame_index_to_physical(
        &before_service, before_service.boot_summary.managed_frame_count, &physical
    ));
    REQUIRE(physical == UINT64_MAX);
    return true;
}

static uint32_t maximal_page_permissions(size_t page)
{
    return MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ
        | (page < 13 ? MICROS_SV39_PERMISSION_EXECUTE
           : page == 13 ? 0 : MICROS_SV39_PERMISSION_WRITE);
}

static bool kernel_global_record_cases(void)
{
    struct micros_vm_kernel_mapping_record saved;
    size_t records = 0;
    int result;

    kernel_state = vm_handoff_test_handed_off_state();
    for (size_t slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        struct micros_vm_kernel_mapping_record *record = &kernel_state.mappings[slot];
        uint64_t base = UINT64_C(0x800000) + slot * UINT64_C(0x20000);

        if (slot == kernel_state.process.slot) {
            continue;
        }
        record->load_token = 64 + slot;
        record->mapping_generation = slot + 1;
        record->process = (struct micros_process_handle){(uint16_t)slot, 1};
        record->phase = MICROS_VM_KERNEL_MAPPING_ACTIVE;
        record->entry_count = MICROS_APPLICATION_RESIDENT_PAGE_MAX;
        record->entry = MICROS_APPLICATION_ENTRY;
        record->stack_pointer = MICROS_USER_VIRTUAL_END - 16;
        for (size_t index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
            record->table_frames[index] = base + UINT64_C(0x10000) + index * MICROS_FRAME_SIZE;
        }
        record->root_physical_address = record->table_frames[0];
        for (size_t index = 0; index < MICROS_APPLICATION_RESIDENT_PAGE_MAX; ++index) {
            record->entries[index] = (struct micros_vm_kernel_mapping_entry){
                .virtual_address = index < 15 ? MICROS_APPLICATION_ENTRY + index * MICROS_FRAME_SIZE
                    : MICROS_APPLICATION_STACK_BASE,
                .physical_address = base + index * MICROS_FRAME_SIZE,
                .permissions = maximal_page_permissions(index),
            };
        }
        ++records;
    }
    kernel_state.last_load_token = 127;
    kernel_state.last_mapping_generation = 64;
    REQUIRE(records == MICROS_PROCESS_CAPACITY - 1);
    REQUIRE(micros_vm_handoff_state_validate(&kernel_state) == MICROS_VM_HANDOFF_OK);
    REQUIRE(begin(true));
    case_extent = records * (MICROS_APPLICATION_RESIDENT_PAGE_MAX + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT);
    result = micros_vm_handoff_state_validate(&kernel_state);
    REQUIRE(report("kernel-global-records", result, MICROS_VM_HANDOFF_OK, true));

    saved = kernel_state.mappings[MICROS_PROCESS_CAPACITY - 1];
    kernel_state.mappings[MICROS_PROCESS_CAPACITY - 1].entries[MICROS_APPLICATION_RESIDENT_PAGE_MAX - 1]
        .physical_address = kernel_state.mappings[0].entries[0].physical_address;
    REQUIRE(begin(true));
    case_extent = records * (MICROS_APPLICATION_RESIDENT_PAGE_MAX + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT);
    result = micros_vm_handoff_state_validate(&kernel_state);
    REQUIRE(report_scoped("kernel-global-duplicate", result, MICROS_VM_HANDOFF_ERROR_INVARIANT, true, false));
    kernel_state.mappings[MICROS_PROCESS_CAPACITY - 1] = saved;
    kernel_state.mappings[MICROS_PROCESS_CAPACITY - 1].mapping_generation =
        kernel_state.mappings[0].mapping_generation;
    REQUIRE(micros_vm_handoff_state_validate(&kernel_state) == MICROS_VM_HANDOFF_ERROR_INVARIANT);
    kernel_state.mappings[MICROS_PROCESS_CAPACITY - 1] = saved;
    kernel_state.mappings[0].phase = MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED;
    kernel_state.mappings[0].exit_token = 99;
    kernel_state.mappings[MICROS_PROCESS_CAPACITY - 1].phase = MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED;
    kernel_state.mappings[MICROS_PROCESS_CAPACITY - 1].exit_token = 99;
    REQUIRE(micros_vm_handoff_state_validate(&kernel_state) == MICROS_VM_HANDOFF_ERROR_INVARIANT);
    return true;
}

static bool check_membership(
    const struct micros_vm_state *state, uint64_t physical, size_t ignored_slot, bool expected
)
{
    struct vm_core_membership_view view;
    bool present = !expected;

    REQUIRE(vm_core_membership_begin(state, &view) == MICROS_VM_CORE_OK);
    REQUIRE(vm_core_membership_query(&view, physical, ignored_slot, &present) == MICROS_VM_CORE_OK);
    REQUIRE(vm_core_membership_end(&view) && present == expected);
    return true;
}

static bool service_membership_cases(struct vm_core_test_context *context, size_t frames)
{
    struct micros_vm_mapping_record *ignored = &context->state.mapping_records[63];
    uint64_t saved = ignored->entries[1].physical_address;
    uint64_t foreign = context->state.mapping_records[0].entries[0].physical_address;
    struct vm_core_membership_view view, copied;
    struct micros_application_stack_values values = vm_core_test_stack_values(900);
    size_t executable_size = vm_core_test_build_elf(context, 13, 1, 1);
    size_t request_size = vm_core_test_build_request(context);
    bool present = true;
    int result;

    REQUIRE(vm_core_membership_begin(&context->state, &view) == MICROS_VM_CORE_OK);
    for (size_t index = 0; index < 20; ++index) {
        REQUIRE(vm_core_membership_query(&view, ignored->entries[index].physical_address, 63, &present)
                == MICROS_VM_CORE_OK && !present);
    }
    REQUIRE(vm_core_membership_end(&view));
    ignored->entries[1].physical_address = ignored->entries[0].physical_address;
    REQUIRE(check_membership(&context->state, ignored->entries[0].physical_address, 63, false));
    context->state.mapping_records[0].entries[0].physical_address = ignored->entries[0].physical_address;
    REQUIRE(check_membership(&context->state, ignored->entries[0].physical_address, 63, true));
    REQUIRE(check_membership(&context->state, ignored->entries[0].physical_address, 64, true));
    context->state.mapping_records[0].entries[0].physical_address = foreign;
    ignored->entries[1].physical_address = saved;
    context->state.mapping_records[0].entries[0].physical_address = ignored->entries[0].physical_address + 1;
    REQUIRE(check_membership(&context->state, ignored->entries[0].physical_address, 63, false));
    context->state.mapping_records[0].entries[0].physical_address = foreign;
    REQUIRE(vm_core_membership_begin(&context->state, &view) == MICROS_VM_CORE_OK);
    copied = view;
    present = true;
    REQUIRE(vm_core_membership_query(&copied, ignored->entries[0].physical_address, 63, &present)
            == MICROS_VM_CORE_ERROR_INVARIANT && present);
    REQUIRE(!vm_core_membership_end(&copied));
    before_service = context->state;
    before_boot = context->boot_info;
    REQUIRE(micros_vm_load_prepare(&context->state, 0x1111, values.pid, context->executable,
        executable_size, context->request, request_size, &values) == MICROS_VM_CORE_ERROR_INVARIANT);
    REQUIRE(memcmp(&before_service, &context->state, sizeof(before_service)) == 0
            && memcmp(&before_boot, &context->boot_info, sizeof(before_boot)) == 0);
    REQUIRE(vm_core_membership_query(&view, ignored->entries[0].physical_address, 63, &present)
            == MICROS_VM_CORE_OK && !present);
    REQUIRE(vm_core_membership_end(&view));

    for (size_t index = TEST_FIRST_FREE_FRAME; index < frames; ++index) {
        context->boot_info.frame_states[index] = MICROS_VM_LOCAL_FRAME_FREE;
        context->state.free_bitmap[index / 64] |= UINT64_C(1) << (index % 64);
    }
    REQUIRE(begin(false));
    case_extent = frames - TEST_FIRST_FREE_FRAME;
    result = vm_core_membership_begin(&context->state, &view);
    REQUIRE(result == MICROS_VM_CORE_OK);
    for (size_t index = TEST_FIRST_FREE_FRAME; index < frames + 20; ++index) {
        REQUIRE(vm_core_membership_query(&view, vm_core_test_frame_address(index), 64, &present)
                == MICROS_VM_CORE_OK && present == (index < frames));
    }
    REQUIRE(vm_core_membership_end(&view));
    REQUIRE(report_scoped("service-membership-max", result, MICROS_VM_CORE_OK, false, false));
    memset(ignored, 0, sizeof(*ignored));
    REQUIRE(micros_vm_load_prepare(&context->state, 0x1111, values.pid, context->executable,
        executable_size, context->request, request_size, &values) == MICROS_VM_CORE_OK);
    before_service = context->state;
    before_boot = context->boot_info;
    REQUIRE(micros_vm_load_bind(&context->state, (struct micros_process_handle){63, 7}, 127)
            == MICROS_VM_CORE_ERROR_INVARIANT);
    REQUIRE(memcmp(&before_service, &context->state, sizeof(before_service)) == 0
            && memcmp(&before_boot, &context->boot_info, sizeof(before_boot)) == 0);
    REQUIRE(micros_vm_load_aborted(&context->state, 0, 0) == MICROS_VM_CORE_OK);
    return true;
}

static bool service_global_record_cases(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct micros_vm_mapping_record saved;
    const uint8_t table_roles[] = {
        MICROS_VM_MAPPING_ENTRY_ROOT_PAGE_TABLE, MICROS_VM_MAPPING_ENTRY_LEVEL1_PAGE_TABLE,
        MICROS_VM_MAPPING_ENTRY_IMAGE_PAGE_TABLE, MICROS_VM_MAPPING_ENTRY_STACK_PAGE_TABLE,
    };
    size_t frames = TEST_FIRST_FREE_FRAME;
    int result;

    REQUIRE(vm_core_test_initialize_boot_info(context,
        TEST_FIRST_FREE_FRAME + MICROS_VM_MAPPING_RECORD_CAPACITY * MICROS_VM_MAPPING_ENTRY_CAPACITY));
    REQUIRE(vm_core_test_initialize_state(context) == MICROS_VM_CORE_OK);
    for (size_t slot = 0; slot < MICROS_VM_MAPPING_RECORD_CAPACITY; ++slot) {
        struct micros_vm_mapping_record *record = &context->state.mapping_records[slot];

        if (slot == context->boot_info.address_spaces[0].process_slot) {
            continue;
        }
        record->load_token = 64 + slot;
        record->mapping_generation = slot + 1;
        record->semantic_pid = slot + 1;
        record->process_generation = 1;
        record->process_slot = (uint16_t)slot;
        record->phase = MICROS_VM_MAPPING_RECORD_ACTIVE;
        record->entry_count = MICROS_VM_MAPPING_ENTRY_CAPACITY;
        for (size_t index = 0; index < MICROS_VM_MAPPING_ENTRY_CAPACITY; ++index) {
            struct micros_vm_mapping_entry *entry = &record->entries[index];

            entry->physical_address = vm_core_test_frame_address(frames);
            if (index < MICROS_VM_PAGE_TABLE_FRAME_COUNT) {
                entry->role = table_roles[index];
                context->boot_info.frame_states[frames] = MICROS_VM_LOCAL_FRAME_PROCESS_PAGE_TABLE;
            } else {
                size_t page = index - MICROS_VM_PAGE_TABLE_FRAME_COUNT;

                entry->role = MICROS_VM_MAPPING_ENTRY_USER_PAGE;
                entry->virtual_address = page < 15 ? MICROS_APPLICATION_ENTRY + page * MICROS_FRAME_SIZE
                    : MICROS_APPLICATION_STACK_BASE;
                entry->permissions = maximal_page_permissions(page);
                context->boot_info.frame_states[frames] = MICROS_VM_LOCAL_FRAME_PROCESS_WIRED;
            }
            context->state.free_bitmap[frames / 64] &= ~(UINT64_C(1) << (frames % 64));
            ++frames;
        }
    }
    REQUIRE(frames + MICROS_VM_MAPPING_ENTRY_CAPACITY == context->boot_info.header.managed_frame_count);
    REQUIRE(micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK);
    REQUIRE(begin(false));
    case_extent = frames - TEST_FIRST_FREE_FRAME;
    result = micros_vm_state_validate(&context->state);
    REQUIRE(report("service-global-records", result, MICROS_VM_CORE_OK, false));

    saved = context->state.mapping_records[MICROS_VM_MAPPING_RECORD_CAPACITY - 1];
    memcpy(context->state.mapping_records[MICROS_VM_MAPPING_RECORD_CAPACITY - 1].entries,
           context->state.mapping_records[0].entries, sizeof(saved.entries));
    REQUIRE(begin(false));
    case_extent = frames - TEST_FIRST_FREE_FRAME;
    result = micros_vm_state_validate(&context->state);
    REQUIRE(report_scoped("service-global-duplicate", result, MICROS_VM_CORE_ERROR_INVARIANT, false, false));
    context->state.mapping_records[MICROS_VM_MAPPING_RECORD_CAPACITY - 1] = saved;
    context->state.mapping_records[MICROS_VM_MAPPING_RECORD_CAPACITY - 1].semantic_pid =
        context->state.mapping_records[0].semantic_pid;
    REQUIRE(micros_vm_state_validate(&context->state) == MICROS_VM_CORE_ERROR_INVARIANT);
    context->state.mapping_records[MICROS_VM_MAPPING_RECORD_CAPACITY - 1] = saved;
    return service_membership_cases(context, frames);
}

static bool load_token_issuance_cases(void)
{
    static const struct {
        uint64_t previous;
        uint16_t slot;
        uint64_t token;
    } cases[] = {
        {0, 0, 64}, {0, 7, 7}, {6, 7, 7}, {7, 7, 71},
        {62, 63, 63}, {63, 0, 64}, {64, 63, 127},
        {UINT64_MAX - 64, 63, UINT64_MAX},
        {UINT64_MAX - 63, 0, 0}, {UINT64_MAX - 1, 63, UINT64_MAX},
        {UINT64_MAX, 63, 0}, {UINT64_MAX, 0, 0},
    };
    struct micros_vm_load_begin_plan plan, sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        struct micros_process_handle process = {cases[index].slot, 9};
        int result;

        kernel_state = vm_handoff_test_handed_off_state();
        kernel_state.last_load_token = cases[index].previous;
        before_kernel = kernel_state;
        plan = sentinel;
        result = micros_vm_load_begin_preflight(&kernel_state, 19, process, true, &plan);
        REQUIRE(result == (cases[index].token == 0
                            ? MICROS_VM_HANDOFF_ERROR_CAPACITY : MICROS_VM_HANDOFF_OK));
        REQUIRE(memcmp(&before_kernel, &kernel_state, sizeof(kernel_state)) == 0);
        if (cases[index].token == 0) {
            REQUIRE(memcmp(&plan, &sentinel, sizeof(plan)) == 0);
        } else {
            if (plan.load_token != cases[index].token) {
                fprintf(stderr, "VM02_TOKEN_ROUTE previous=%" PRIu64 " slot=%u actual=%" PRIu64
                        " expected=%" PRIu64 "\n", cases[index].previous, cases[index].slot,
                        plan.load_token, cases[index].token);
                return false;
            }
            REQUIRE(plan.result.load_token == cases[index].token);
            micros_vm_load_begin_commit_prevalidated(&kernel_state, &plan);
            REQUIRE(kernel_state.last_load_token == cases[index].token
                    && kernel_state.mappings[process.slot].load_token == cases[index].token);
        }
    }
    return true;
}

static bool exit_token_scope_cases(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct micros_vm_exit_action service_action, service_sentinel;
    struct micros_vm_exit_release_action kernel_action, kernel_sentinel;
    struct micros_process_handle process = vm_handoff_test_dynamic_process();
    int result;

    REQUIRE(initialize_service_state() && vm_core_test_run_to_frozen(context, 0x3333));
    REQUIRE(micros_vm_load_committed(&context->state, 0x2202, 0x3333, 7) == MICROS_VM_CORE_OK);
    context->state.mapping_records[63].entry_count = 255;
    REQUIRE(begin(false));
    result = micros_vm_exit_release_begin(&context->state, 0x2202, 0x3333, 7, &service_action);
    REQUIRE(report_scoped("service-exit-unvisited", result, MICROS_VM_CORE_OK, false, false));
    REQUIRE(service_action.process_slot == 2 && service_action.process_generation == 7);
    REQUIRE(micros_vm_exit_release_complete(&context->state, &service_action) == MICROS_VM_CORE_OK);
    REQUIRE(context->state.mapping_records[63].entry_count == 255);

    REQUIRE(initialize_service_state() && vm_core_test_run_to_frozen(context, 0x3333));
    REQUIRE(micros_vm_load_committed(&context->state, 0x2202, 0x3333, 7) == MICROS_VM_CORE_OK);
    memset(&service_sentinel, 0xa5, sizeof(service_sentinel));
    service_action = service_sentinel;
    context->state.mapping_records[2].entries[19].reserved[0] = 1;
    before_service = context->state;
    REQUIRE(micros_vm_exit_release_begin(&context->state, 0x2242, 0x3333, 7, &service_action)
            == MICROS_VM_CORE_ERROR_STATE);
    REQUIRE(memcmp(&service_action, &service_sentinel, sizeof(service_action)) == 0
            && memcmp(&context->state, &before_service, sizeof(before_service)) == 0);
    context->state.mapping_records[2].entries[19].reserved[0] = 0;
    context->state.mapping_records[2].load_token = 0x2203;
    REQUIRE(micros_vm_exit_release_begin(&context->state, 0x2202, 0x3333, 7, &service_action)
            == MICROS_VM_CORE_ERROR_INVARIANT);
    REQUIRE(memcmp(&service_action, &service_sentinel, sizeof(service_action)) == 0);
    context->state.mapping_records[2].load_token = 0x2242;
    ++context->state.mapping_records[2].process_generation;
    REQUIRE(micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK);
    REQUIRE(micros_vm_exit_release_begin(&context->state, 0x2202, 0x3333, 7, &service_action)
            == MICROS_VM_CORE_ERROR_STATE);
    REQUIRE(memcmp(&service_action, &service_sentinel, sizeof(service_action)) == 0);

    kernel_state = vm_handoff_test_handed_off_state();
    REQUIRE(vm_handoff_test_begin_and_fill_load(&kernel_state, 101) && kernel_freeze());
    REQUIRE(micros_vm_load_mark_prepared(&kernel_state, process, 101, 7, 1) == MICROS_VM_HANDOFF_OK);
    REQUIRE(micros_vm_load_mark_activated(&kernel_state, process, 101, 7, 1) == MICROS_VM_HANDOFF_OK);
    REQUIRE(micros_vm_load_commit_preflight(&kernel_state, 7, 1) == MICROS_VM_HANDOFF_OK);
    micros_vm_load_commit_commit_prevalidated(&kernel_state);
    REQUIRE(micros_vm_load_mark_exit_detached(&kernel_state, process, 701, 7, 1) == MICROS_VM_HANDOFF_OK);
    kernel_state.mappings[63].entry_count = 255;
    REQUIRE(begin(true));
    result = micros_vm_exit_release_preflight(&kernel_state, 701, 7, 1, &kernel_action);
    REQUIRE(report_scoped("kernel-exit-unvisited", result, MICROS_VM_HANDOFF_OK, true, false));
    REQUIRE(kernel_action.process.slot == 7 && kernel_action.process.generation == 9);
    kernel_state.mappings[63].entry_count = 0;
    memset(&kernel_sentinel, 0xa5, sizeof(kernel_sentinel));
    kernel_action = kernel_sentinel;
    kernel_state.mappings[7].entries[15].reserved = 1;
    before_kernel = kernel_state;
    REQUIRE(micros_vm_exit_release_preflight(&kernel_state, 701, 71, 1, &kernel_action)
            == MICROS_VM_HANDOFF_ERROR_STATE);
    REQUIRE(memcmp(&kernel_action, &kernel_sentinel, sizeof(kernel_action)) == 0
            && memcmp(&kernel_state, &before_kernel, sizeof(kernel_state)) == 0);
    kernel_state.mappings[7].entries[15].reserved = 0;
    kernel_state.mappings[7].load_token = 6;
    REQUIRE(micros_vm_exit_release_preflight(&kernel_state, 701, 7, 1, &kernel_action)
            == MICROS_VM_HANDOFF_ERROR_INVARIANT);
    REQUIRE(memcmp(&kernel_action, &kernel_sentinel, sizeof(kernel_action)) == 0);
    kernel_state.mappings[7].load_token = 71;
    ++kernel_state.mappings[7].process.generation;
    kernel_state.last_load_token = 71;
    REQUIRE(micros_vm_handoff_state_validate(&kernel_state) == MICROS_VM_HANDOFF_OK);
    REQUIRE(micros_vm_exit_release_preflight(&kernel_state, 701, 7, 1, &kernel_action)
            == MICROS_VM_HANDOFF_ERROR_STATE);
    REQUIRE(memcmp(&kernel_action, &kernel_sentinel, sizeof(kernel_action)) == 0);
    return true;
}

static bool append_minimal_kernel_pages(uint64_t physical)
{
    const uint32_t read = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ;

    return vm_handoff_test_append_page(&kernel_state, MICROS_APPLICATION_ENTRY,
               physical, read | MICROS_SV39_PERMISSION_EXECUTE)
        && vm_handoff_test_append_page(&kernel_state, MICROS_APPLICATION_ENTRY + 4096,
               physical + 4096, read)
        && vm_handoff_test_append_page(&kernel_state, MICROS_APPLICATION_ENTRY + 8192,
               physical + 8192, read | MICROS_SV39_PERMISSION_WRITE)
        && vm_handoff_test_append_page(&kernel_state, MICROS_APPLICATION_STACK_BASE,
               physical + 12288, read | MICROS_SV39_PERMISSION_WRITE);
}

static bool prepare_foreign_kernel_mapping(void)
{
    struct micros_process_handle process = {63, 1};
    struct micros_vm_load_begin_plan begin_plan;

    kernel_state = vm_handoff_test_handed_off_state();
    REQUIRE(micros_vm_load_begin_preflight(&kernel_state, 101, process, true, &begin_plan)
            == MICROS_VM_HANDOFF_OK);
    micros_vm_load_begin_commit_prevalidated(&kernel_state, &begin_plan);
    REQUIRE(begin_plan.load_token == 63 && append_minimal_kernel_pages(0x900000) && kernel_freeze());
    REQUIRE(micros_vm_load_mark_prepared(&kernel_state, process, 101, 63, 1) == MICROS_VM_HANDOFF_OK);
    REQUIRE(micros_vm_load_mark_activated(&kernel_state, process, 101, 63, 1) == MICROS_VM_HANDOFF_OK);
    REQUIRE(micros_vm_load_commit_preflight(&kernel_state, 63, 1) == MICROS_VM_HANDOFF_OK);
    micros_vm_load_commit_commit_prevalidated(&kernel_state);
    REQUIRE(micros_vm_load_begin_preflight(&kernel_state, 102, vm_handoff_test_dynamic_process(),
                                          true, &begin_plan) == MICROS_VM_HANDOFF_OK);
    micros_vm_load_begin_commit_prevalidated(&kernel_state, &begin_plan);
    REQUIRE(begin_plan.load_token == 71);
    return true;
}

static bool kernel_owner_collision_cases(void)
{
    struct micros_vm_load_freeze_state_plan freeze, freeze_sentinel;
    struct micros_vm_load_freeze_plan request;
    struct micros_vm_scratch_plan page, sentinel;
    struct micros_frame_vm_claim_plan claim;
    struct micros_vm_scratch_map_request map = {
        .load_token = 71, .virtual_address = MICROS_APPLICATION_ENTRY, .physical_address = 0x900000,
        .permissions = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE,
    };
    struct micros_frame_ownership *ownership = &vm_handoff_test_memory_get()->ownership;
    static const char *const names[] = {"kernel-owner-bit", "kernel-owner-link", "kernel-owner-generation"};
    int result;

    REQUIRE(prepare_foreign_kernel_mapping());
    memset(&sentinel, 0xa5, sizeof(sentinel));
    page = sentinel;
    REQUIRE(begin(true));
    result = micros_vm_scratch_map_preflight(&kernel_state, ownership, &map, &page);
    REQUIRE(report("kernel-owner-collision", result, MICROS_VM_HANDOFF_ERROR_STATE, true));
    REQUIRE(memcmp(&page, &sentinel, sizeof(page)) == 0);
    for (unsigned mutation = 0; mutation < 3; ++mutation) {
        struct micros_frame_owner_links saved = ownership->owner_links[768];

        if (mutation == 0) {
            ownership->allocator->allocated_bitmap[12] ^= UINT64_C(1);
        } else if (mutation == 1) {
            ownership->owner_links[768].next = 769;
        } else {
            ++kernel_state.mappings[63].process.generation;
        }
        REQUIRE(begin(true));
        result = micros_vm_scratch_map_preflight(&kernel_state, ownership, &map, &page);
        REQUIRE(report(names[mutation], result, MICROS_VM_HANDOFF_ERROR_INVARIANT, true));
        REQUIRE(memcmp(&page, &sentinel, sizeof(page)) == 0);
        REQUIRE(micros_frame_ownership_validate(ownership) == (mutation < 2
            ? MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT : MICROS_FRAME_OWNERSHIP_OK));
        if (mutation == 0) {
            ownership->allocator->allocated_bitmap[12] ^= UINT64_C(1);
        } else if (mutation == 1) {
            ownership->owner_links[768] = saved;
        } else {
            --kernel_state.mappings[63].process.generation;
        }
    }
    map.physical_address = UINT64_MAX - 4095;
    REQUIRE(micros_vm_scratch_map_preflight(&kernel_state, ownership, &map, &page) == MICROS_VM_HANDOFF_OK);
    REQUIRE(micros_frame_ownership_vm_claim_preflight(ownership, map.physical_address, &claim)
            == MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED);
    REQUIRE(append_minimal_kernel_pages(0x800000));
    request = vm_handoff_test_valid_freeze_plan(&kernel_state);
    memset(&freeze_sentinel, 0xa5, sizeof(freeze_sentinel));
    freeze = freeze_sentinel;
    REQUIRE(begin(true));
    result = micros_vm_load_freeze_preflight(&kernel_state, ownership, &request, &freeze);
    REQUIRE(report("kernel-table-collision", result, MICROS_VM_HANDOFF_ERROR_ARGUMENT, true));
    REQUIRE(memcmp(&freeze, &freeze_sentinel, sizeof(freeze)) == 0);
    return true;
}

static bool service_retained_page_cases(void)
{
    static const char *const names[2][4] = {
        {"service-large-next", "service-large-map", "service-large-fill", "service-large-commit"},
        {"service-long-next", "service-long-map", "service-long-fill", "service-long-commit"},
    };
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct micros_application_stack_values values = vm_core_test_stack_values(7);
    struct micros_vm_service_load_transaction retained;
    struct micros_vm_load_page_action page;

    for (unsigned shape = 0; shape < 2; ++shape) {
        size_t executable_size, request_size;
        int result;

        REQUIRE(initialize_service_state());
        executable_size = vm_core_test_build_elf(context, 1, 1, shape == 0 ? 13 : 1);
        request_size = vm_core_test_build_request(context);
        if (shape == 1) {
            const size_t argument = MICROS_SPAWN_REQUEST_HEADER_SIZE + sizeof("/bin/app");
            const size_t environment = argument + MICROS_SPAWN_STRING_SIZE_MAX;

            memset(&context->request[argument], 'a', MICROS_SPAWN_STRING_SIZE_MAX - 1);
            context->request[environment - 1] = 0;
            memcpy(&context->request[environment], "A=B", sizeof("A=B"));
            request_size = environment + sizeof("A=B");
            vm_core_test_write_u16_le(
                &context->request[MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET + 2],
                MICROS_SPAWN_STRING_SIZE_MAX);
            vm_core_test_write_u16_le(
                &context->request[MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET], (uint16_t)environment);
            vm_core_test_write_u32_le(&context->request[8], (uint32_t)request_size);
        }
        REQUIRE(micros_vm_load_prepare(&context->state, 0x1111, 7, context->executable,
            executable_size, context->request, request_size, &values) == MICROS_VM_CORE_OK);
        REQUIRE(micros_vm_load_bind(&context->state, (struct micros_process_handle){2, 7}, 0x2202)
                == MICROS_VM_CORE_OK);
        retained = context->state.transaction;
        REQUIRE(begin(false));
        result = micros_vm_load_next_page(&context->state, &page);
        REQUIRE(report(names[shape][0], result, MICROS_VM_CORE_OK, false));
        REQUIRE(page.page_index == 0 && page.virtual_address == MICROS_APPLICATION_ENTRY
                && page.physical_address == vm_core_test_frame_address(
                    shape == 0 ? TEST_FIRST_FREE_FRAME + 4 : TEST_FIRST_LOAD_FRAME + 4));
        REQUIRE(begin(false));
        result = micros_vm_load_scratch_mapped(&context->state, 0);
        REQUIRE(report(names[shape][1], result, MICROS_VM_CORE_OK, false));
        REQUIRE(begin(false));
        result = micros_vm_load_fill_scratch(&context->state, 0, context->scratch_page,
                                             sizeof(context->scratch_page));
        REQUIRE(report(names[shape][2], result, MICROS_VM_CORE_OK, false));
        REQUIRE(memcmp(context->scratch_page, context->executable, 4096) == 0);
        REQUIRE(begin(false));
        result = micros_vm_load_scratch_committed(&context->state, 0);
        REQUIRE(report(names[shape][3], result, MICROS_VM_CORE_OK, false));
        retained.completed_page_count = 1;
        REQUIRE(memcmp(&retained, &context->state.transaction, sizeof(retained)) == 0);
    }
    return true;
}

static bool kernel_retained_prefix_cases(void)
{
    static const char *const names[2][2] = {
        {"kernel-short-map", "kernel-short-commit"},
        {"kernel-long-map", "kernel-long-commit"},
    };
    struct micros_process_handle process = vm_handoff_test_dynamic_process();
    struct micros_vm_load_begin_plan start;
    const uint32_t permissions = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_EXECUTE;

    for (unsigned shape = 0; shape < 2; ++shape) {
        size_t count = shape == 0 ? 1 : 11;
        struct micros_vm_scratch_plan page;
        struct micros_vm_scratch_map_request map = {
            .load_token = process.slot,
            .virtual_address = MICROS_APPLICATION_ENTRY + count * MICROS_FRAME_SIZE,
            .physical_address = UINT64_C(0x800000) + count * MICROS_FRAME_SIZE,
            .permissions = permissions,
        };
        struct micros_vm_scratch_commit_request commit = {
            .load_token = process.slot, .virtual_address = map.virtual_address,
            .physical_address = map.physical_address,
        };
        int result;

        kernel_state = vm_handoff_test_handed_off_state();
        REQUIRE(micros_vm_load_begin_preflight(&kernel_state, 101, process, true, &start)
                == MICROS_VM_HANDOFF_OK);
        micros_vm_load_begin_commit_prevalidated(&kernel_state, &start);
        for (size_t index = 0; index < count; ++index) {
            REQUIRE(vm_handoff_test_append_page(&kernel_state,
                MICROS_APPLICATION_ENTRY + index * MICROS_FRAME_SIZE,
                UINT64_C(0x800000) + index * MICROS_FRAME_SIZE, permissions));
        }
        REQUIRE(begin(true));
        result = micros_vm_scratch_map_preflight(
            &kernel_state, &vm_handoff_test_memory_get()->ownership, &map, &page);
        REQUIRE(result == MICROS_VM_HANDOFF_OK);
        REQUIRE(vm_handoff_test_commit_scratch(&kernel_state, &page));
        REQUIRE(report(names[shape][0], result, MICROS_VM_HANDOFF_OK, true));
        REQUIRE(begin(true));
        result = micros_vm_scratch_commit_preflight(&kernel_state, &commit, &page);
        REQUIRE(result == MICROS_VM_HANDOFF_OK);
        micros_vm_scratch_commit_commit_prevalidated(&kernel_state, &page);
        REQUIRE(report(names[shape][1], result, MICROS_VM_HANDOFF_OK, true));
        REQUIRE(kernel_state.mappings[process.slot].entry_count == count + 1);
    }
    return true;
}

static bool service_retained_scope_cases(void)
{
    static const char *const names[] = {
        "service-unvisited-page-plan", "service-unvisited-old-page", "service-unvisited-plan-tail",
        "service-unvisited-elf-header", "service-unvisited-request-header",
        "service-selected-segment", "service-selected-file-slice",
        "service-selected-page-role", "service-selected-frame-state",
    };
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct micros_vm_load_page_action page, sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    for (size_t mutation = 0; mutation < sizeof(names) / sizeof(names[0]); ++mutation) {
        struct micros_vm_service_load_transaction *transaction = &context->state.transaction;
        int result;

        REQUIRE(initialize_service_state() && vm_core_test_prepare_minimal_load(context));
        REQUIRE(micros_vm_load_bind(&context->state, (struct micros_process_handle){2, 7}, 0x2202)
                == MICROS_VM_CORE_OK);
        if (mutation == 1) {
            for (uint32_t index = 0; index < 2; ++index) {
                REQUIRE(micros_vm_load_scratch_mapped(&context->state, index) == MICROS_VM_CORE_OK);
                REQUIRE(micros_vm_load_fill_scratch(&context->state, index, context->scratch_page,
                                                   sizeof(context->scratch_page)) == MICROS_VM_CORE_OK);
                REQUIRE(micros_vm_load_scratch_committed(&context->state, index) == MICROS_VM_CORE_OK);
            }
        }
        switch (mutation) {
        case 0:
            ++transaction->elf_plan.pages[2].file_offset;
            break;
        case 1:
            ++transaction->elf_plan.pages[0].file_offset;
            break;
        case 2:
            transaction->elf_plan.pages[14].permissions = MICROS_SV39_PERMISSION_WRITE;
            break;
        case 3:
            context->state.executable_bytes[5] = 0;
            break;
        case 4:
            context->state.request_bytes[0] = 0;
            break;
        case 5:
            transaction->elf_plan.segments[0].memory_size = UINT64_MAX;
            break;
        case 6:
            ++transaction->elf_plan.pages[0].file_offset;
            break;
        case 7:
            context->state.mapping_records[2].entries[4].role = MICROS_VM_MAPPING_ENTRY_ROOT_PAGE_TABLE;
            break;
        default:
            context->boot_info.frame_states[TEST_FIRST_LOAD_FRAME + 4] = MICROS_VM_LOCAL_FRAME_TRANSFERABLE;
            break;
        }
        page = sentinel;
        REQUIRE(begin(false));
        result = micros_vm_load_next_page(&context->state, &page);
        REQUIRE(report_scoped(names[mutation], result,
            mutation < 5 ? MICROS_VM_CORE_OK : MICROS_VM_CORE_ERROR_INVARIANT, false, false));
        if (mutation < 5) {
            REQUIRE(page.page_index == (mutation == 1 ? 2 : 0)
                    && page.virtual_address == MICROS_APPLICATION_ENTRY + page.page_index * MICROS_FRAME_SIZE);
        } else {
            REQUIRE(memcmp(&page, &sentinel, sizeof(page)) == 0);
        }
        if (mutation < 2) {
            REQUIRE(micros_vm_load_aborted(&context->state, 0x2202, 0) == MICROS_VM_CORE_ERROR_INVARIANT);
            REQUIRE(memcmp(&before_service, &context->state, sizeof(before_service)) == 0
                    && memcmp(&before_boot, &context->boot_info, sizeof(before_boot)) == 0);
        }
    }
    return true;
}

static bool kernel_retained_scope_cases(void)
{
    struct micros_process_handle process = vm_handoff_test_dynamic_process();
    struct micros_vm_load_begin_plan start;
    struct micros_vm_scratch_plan page, sentinel;
    struct micros_vm_load_abort_action abort, abort_sentinel;
    const uint32_t read = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ;
    struct micros_vm_scratch_map_request map = {
        .load_token = process.slot, .virtual_address = MICROS_APPLICATION_ENTRY + 3 * MICROS_FRAME_SIZE,
        .physical_address = 0x803000, .permissions = read | MICROS_SV39_PERMISSION_WRITE,
    };
    int result;

    kernel_state = vm_handoff_test_handed_off_state();
    REQUIRE(micros_vm_load_begin_preflight(&kernel_state, 101, process, true, &start)
            == MICROS_VM_HANDOFF_OK);
    micros_vm_load_begin_commit_prevalidated(&kernel_state, &start);
    for (size_t index = 0; index < 3; ++index) {
        REQUIRE(vm_handoff_test_append_page(&kernel_state,
            MICROS_APPLICATION_ENTRY + index * MICROS_FRAME_SIZE,
            UINT64_C(0x800000) + index * MICROS_FRAME_SIZE,
            read | (index == 0 ? MICROS_SV39_PERMISSION_EXECUTE : index == 2 ? MICROS_SV39_PERMISSION_WRITE : 0)));
    }
    kernel_state.mappings[process.slot].entries[0].reserved = 1;
    REQUIRE(begin(true));
    result = micros_vm_scratch_map_preflight(
        &kernel_state, &vm_handoff_test_memory_get()->ownership, &map, &page);
    REQUIRE(report_scoped("kernel-unvisited-prefix", result, MICROS_VM_HANDOFF_OK, true, false));
    REQUIRE(page.entry.virtual_address == map.virtual_address && page.entry.physical_address == map.physical_address);

    memset(&abort_sentinel, 0xa5, sizeof(abort_sentinel));
    abort = abort_sentinel;
    REQUIRE(begin(true));
    result = micros_vm_load_abort_preflight(&kernel_state, process.slot, 0, &abort);
    REQUIRE(report_scoped("kernel-prefix-cleanup", result, MICROS_VM_HANDOFF_ERROR_INVARIANT, true, false));
    REQUIRE(memcmp(&abort, &abort_sentinel, sizeof(abort)) == 0);

    kernel_state.mappings[process.slot].entries[2].reserved = 1;
    memset(&sentinel, 0xa5, sizeof(sentinel));
    page = sentinel;
    REQUIRE(begin(true));
    result = micros_vm_scratch_map_preflight(
        &kernel_state, &vm_handoff_test_memory_get()->ownership, &map, &page);
    REQUIRE(report_scoped("kernel-selected-predecessor", result, MICROS_VM_HANDOFF_ERROR_INVARIANT, true, false));
    REQUIRE(memcmp(&page, &sentinel, sizeof(page)) == 0);
    return true;
}

static size_t build_maximum_stack_request(struct vm_core_test_context *context)
{
    size_t cursor = MICROS_SPAWN_REQUEST_HEADER_SIZE + sizeof("/bin/app");

    (void)vm_core_test_build_request(context);
    vm_core_test_write_u16_le(&context->request[MICROS_SPAWN_ARGUMENT_COUNT_OFFSET], 8);
    vm_core_test_write_u16_le(&context->request[MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET], 8);
    for (size_t index = 0; index < 16; ++index) {
        size_t reference = (index < 8 ? MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
                            : MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET) + (index % 8) * 8;

        vm_core_test_write_u16_le(&context->request[reference], (uint16_t)cursor);
        vm_core_test_write_u16_le(&context->request[reference + 2], 113);
        memset(&context->request[cursor], 'a' + (int)index, 112);
        context->request[cursor + 112] = 0;
        if (index >= 8) {
            context->request[cursor + 1] = '=';
        }
        cursor += 113;
    }
    vm_core_test_write_u32_le(&context->request[8], (uint32_t)cursor);
    return cursor;
}

static bool retained_stack_cases(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct micros_application_stack_values values = vm_core_test_stack_values(7);
    struct micros_vm_load_page_action page, sentinel;
    struct micros_vm_freeze_plan freeze;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    for (unsigned shape = 0; shape < 2; ++shape) {
        size_t image_pages = shape == 0 ? 3 : 15;
        size_t executable_size, request_size;
        int result;

        REQUIRE(initialize_service_state());
        executable_size = vm_core_test_build_elf(context, 1, 1, image_pages - 2);
        request_size = shape == 0 ? vm_core_test_build_request(context) : build_maximum_stack_request(context);
        REQUIRE(request_size == (shape == 0 ? 273 : 2073));
        REQUIRE(micros_vm_load_prepare(&context->state, 0x1111, 7, context->executable,
            executable_size, context->request, request_size, &values) == MICROS_VM_CORE_OK);
        REQUIRE(context->state.transaction.stack_plan.occupied_size == (shape == 0 ? 144 : 2048));
        REQUIRE(micros_vm_load_bind(&context->state, (struct micros_process_handle){2, 7}, 0x2202)
                == MICROS_VM_CORE_OK);
        for (uint32_t index = 0; index < image_pages; ++index) {
            size_t copied = index < 2 ? 4096 : index == 2 ? 1024 : 0;
            uint32_t permissions = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ
                | (index == 0 ? MICROS_SV39_PERMISSION_EXECUTE : index >= 2 ? MICROS_SV39_PERMISSION_WRITE : 0);

            REQUIRE(micros_vm_load_next_page(&context->state, &page) == MICROS_VM_CORE_OK);
            REQUIRE(page.virtual_address == MICROS_APPLICATION_ENTRY + index * MICROS_FRAME_SIZE
                    && page.permissions == permissions && page.page_index == index);
            REQUIRE(micros_vm_load_scratch_mapped(&context->state, index) == MICROS_VM_CORE_OK);
            REQUIRE(micros_vm_load_fill_scratch(&context->state, index, context->scratch_page,
                                               sizeof(context->scratch_page)) == MICROS_VM_CORE_OK);
            REQUIRE((copied == 0 || memcmp(context->scratch_page, &context->executable[index * 4096], copied) == 0)
                    && vm_core_test_bytes_are_zero(&context->scratch_page[copied], 4096 - copied));
            REQUIRE(micros_vm_load_scratch_committed(&context->state, index) == MICROS_VM_CORE_OK);
        }
        REQUIRE(micros_vm_load_scratch_mapped(&context->state, (uint32_t)image_pages) == MICROS_VM_CORE_OK);
        memset(context->scratch_page, 0xa5, sizeof(context->scratch_page));
        --context->state.transaction.stack_plan.frame_size;
        before_service = context->state;
        before_boot = context->boot_info;
        REQUIRE(micros_vm_load_fill_scratch(&context->state, (uint32_t)image_pages, context->scratch_page,
                                           sizeof(context->scratch_page)) == MICROS_VM_CORE_ERROR_INVARIANT);
        REQUIRE(memcmp(&before_service, &context->state, sizeof(before_service)) == 0
                && memcmp(&before_boot, &context->boot_info, sizeof(before_boot)) == 0);
        for (size_t index = 0; index < sizeof(context->scratch_page); ++index) {
            REQUIRE(context->scratch_page[index] == 0xa5);
        }
        ++context->state.transaction.stack_plan.frame_size;
        REQUIRE(begin(false));
        case_extent = request_size;
        result = micros_vm_load_fill_scratch(&context->state, (uint32_t)image_pages,
                                             context->scratch_page, sizeof(context->scratch_page));
        REQUIRE(report(shape == 0 ? "service-stack-min" : "service-stack-max",
                       result, MICROS_VM_CORE_OK, false));
        REQUIRE(vm_core_test_bytes_are_zero(context->scratch_page, shape == 0 ? 3952 : 2048));
        REQUIRE(context->scratch_page[shape == 0 ? 3952 : 2048] == (shape == 0 ? 1 : 8));
        REQUIRE(micros_vm_load_scratch_committed(&context->state, (uint32_t)image_pages) == MICROS_VM_CORE_OK);
        page = sentinel;
        REQUIRE(micros_vm_load_next_page(&context->state, &page) == MICROS_VM_CORE_ERROR_STATE);
        REQUIRE(memcmp(&page, &sentinel, sizeof(page)) == 0);
        REQUIRE(micros_vm_load_freeze_plan(&context->state, &freeze) == MICROS_VM_CORE_OK);
        REQUIRE(freeze.image_page_count == image_pages && freeze.data_page_count == image_pages + 1);
        REQUIRE(micros_vm_load_frozen(&context->state, 0x3333) == MICROS_VM_CORE_OK);
        context->state.request_bytes[0] = 0;
        context->state.executable_bytes[5] = 0;
        REQUIRE(micros_vm_state_validate(&context->state) == MICROS_VM_CORE_ERROR_INVARIANT);
        REQUIRE(micros_vm_load_aborted(&context->state, 0x2202, 0x3333) == MICROS_VM_CORE_OK);
        REQUIRE(micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK);
    }
    return true;
}

struct allocator_reference {
    bool free[TEST_MANAGED_FRAME_COUNT];
    uint32_t cache[20];
    size_t count;
    int origin;
};

static bool reference_allocate(struct allocator_reference *reference, uint32_t frames[8])
{
    struct allocator_reference before = *reference;

    for (size_t page = 0; page < 8; ++page) {
        int selected = -1;

        while (reference->count != 0 && selected < 0) {
            uint32_t candidate = reference->cache[--reference->count];

            if (reference->free[candidate]) {
                selected = (int)candidate;
            }
        }
        for (unsigned pass = 0; pass < 2 && selected < 0; ++pass) {
            int first = pass == 0 && reference->origin < TEST_MANAGED_FRAME_COUNT
                ? reference->origin : TEST_MANAGED_FRAME_COUNT - 1;

            for (int index = first; index >= 0; --index) {
                if (reference->free[index]) {
                    selected = index;
                    break;
                }
            }
        }
        if (selected < 0) {
            *reference = before;
            return false;
        }
        reference->free[selected] = false;
        reference->origin = selected;
        frames[page] = (uint32_t)selected;
    }
    for (size_t index = 1; index < 8; ++index) {
        uint32_t value = frames[index];
        size_t position = index;

        while (position != 0 && frames[position - 1] > value) {
            frames[position] = frames[position - 1];
            --position;
        }
        frames[position] = value;
    }
    return true;
}

static void reference_release(struct allocator_reference *reference, const uint32_t frames[8])
{
    for (size_t index = 0; index < 8; ++index) {
        reference->free[frames[index]] = true;
        if (reference->count < 20) {
            reference->cache[reference->count++] = frames[index];
        }
    }
}

static bool check_reference_selection(
    const struct vm_core_test_context *context, const uint32_t frames[8]
)
{
    for (size_t index = 0; index < 8; ++index) {
        uint64_t actual = index < 4 ? context->state.transaction.table_frames[index]
            : context->state.transaction.data_frames[index - 4];
        uint64_t expected = vm_core_test_frame_address(frames[index]);

        if (actual != expected) {
            fprintf(stderr, "VM04_SELECTION index=%zu actual=%" PRIx64 " expected=%" PRIx64 "\n",
                    index, actual, expected);
            return false;
        }
        REQUIRE(context->boot_info.frame_states[frames[index]] == UINT8_C(0x86));
    }
    return true;
}

static bool allocator_reference_cases(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct allocator_reference reference = {.origin = TEST_MANAGED_FRAME_COUNT};
    uint32_t selections[5][8], replacement[8];
    struct micros_vm_exit_action exit;

    REQUIRE(initialize_service_state());
    for (uint32_t index = TEST_FIRST_FREE_FRAME; index < TEST_MANAGED_FRAME_COUNT; ++index) {
        reference.free[index] = true;
        if (reference.count < 20) {
            reference.cache[reference.count++] = index;
        }
    }
    for (uint16_t slot = 2; slot < 7; ++slot) {
        struct micros_application_stack_values values = vm_core_test_stack_values(100 + slot);
        size_t executable_size = vm_core_test_build_elf(context, 1, 1, 1);
        size_t request_size = vm_core_test_build_request(context);
        uint64_t token = UINT64_C(0x2200) + slot;

        REQUIRE(reference_allocate(&reference, selections[slot - 2]));
        REQUIRE(micros_vm_load_prepare(&context->state, 0x1100 + slot, values.pid, context->executable,
            executable_size, context->request, request_size, &values) == MICROS_VM_CORE_OK);
        REQUIRE(check_reference_selection(context, selections[slot - 2]));
        REQUIRE(micros_vm_load_bind(&context->state, (struct micros_process_handle){slot, 7}, token)
                == MICROS_VM_CORE_OK);
        for (uint32_t page = 0; page < 4; ++page) {
            REQUIRE(micros_vm_load_scratch_mapped(&context->state, page) == MICROS_VM_CORE_OK);
            REQUIRE(micros_vm_load_fill_scratch(&context->state, page, context->scratch_page,
                                               sizeof(context->scratch_page)) == MICROS_VM_CORE_OK);
            REQUIRE(micros_vm_load_scratch_committed(&context->state, page) == MICROS_VM_CORE_OK);
        }
        REQUIRE(micros_vm_load_frozen(&context->state, 200 + slot) == MICROS_VM_CORE_OK);
        REQUIRE(micros_vm_load_committed(&context->state, token, 200 + slot, values.pid) == MICROS_VM_CORE_OK);
        REQUIRE(micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK);
    }
    {
        struct micros_application_stack_values values = vm_core_test_stack_values(107);
        size_t executable_size = vm_core_test_build_elf(context, 1, 1, 1);
        size_t request_size = vm_core_test_build_request(context);

        REQUIRE(!reference_allocate(&reference, replacement));
        before_service = context->state;
        before_boot = context->boot_info;
        REQUIRE(micros_vm_load_prepare(&context->state, 0x1107, values.pid, context->executable,
            executable_size, context->request, request_size, &values) == MICROS_VM_CORE_ERROR_NO_SPACE);
        REQUIRE(memcmp(&before_service, &context->state, sizeof(before_service)) == 0
                && memcmp(&before_boot, &context->boot_info, sizeof(before_boot)) == 0);
    }
    REQUIRE(micros_vm_exit_release_begin(&context->state, 0x2204, 204, 104, &exit) == MICROS_VM_CORE_OK);
    REQUIRE(micros_vm_exit_release_complete(&context->state, &exit) == MICROS_VM_CORE_OK);
    reference_release(&reference, selections[2]);
    REQUIRE(reference_allocate(&reference, replacement));
    REQUIRE(vm_core_test_prepare_minimal_load(context));
    REQUIRE(check_reference_selection(context, replacement));
    REQUIRE(micros_vm_load_aborted(&context->state, 0, 0) == MICROS_VM_CORE_OK);
    reference_release(&reference, replacement);
    for (size_t index = TEST_FIRST_FREE_FRAME; index < TEST_MANAGED_FRAME_COUNT; ++index) {
        REQUIRE((context->boot_info.frame_states[index] == MICROS_VM_LOCAL_FRAME_FREE) == reference.free[index]);
    }
    REQUIRE(micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK);
    return true;
}

static bool allocator_guard_cases(void)
{
    static const char *const names[] = {
        "service-cache-count", "service-cache-origin", "service-cache-index",
        "service-cache-static", "service-cache-bit", "service-cache-late", "service-cache-tag",
    };
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct micros_application_stack_values values = vm_core_test_stack_values(8);

    for (size_t mutation = 0; mutation < sizeof(names) / sizeof(names[0]); ++mutation) {
        size_t executable_size, request_size;
        int result;

        REQUIRE(initialize_service_state());
        executable_size = vm_core_test_build_elf(context, 1, 1, 1);
        request_size = vm_core_test_build_request(context);
        switch (mutation) {
        case 0: context->state.free_page_cache_count = 21; break;
        case 1: context->state.allocation_origin = TEST_MANAGED_FRAME_COUNT + 1; break;
        case 2: context->state.free_page_cache[19] = TEST_MANAGED_FRAME_COUNT; break;
        case 3: context->state.free_page_cache[19] = 0; break;
        case 4: context->state.free_bitmap[0] ^= UINT64_C(1) << 25; break;
        case 5: context->state.free_page_cache[18] = TEST_MANAGED_FRAME_COUNT; break;
        default: context->boot_info.frame_states[25] = MICROS_VM_FRAME_TRANSFERABLE; break;
        }
        REQUIRE(begin(false));
        result = micros_vm_load_prepare(&context->state, 0x1111, values.pid, context->executable,
            executable_size, context->request, request_size, &values);
        REQUIRE(report_scoped(names[mutation], result, MICROS_VM_CORE_ERROR_INVARIANT, false, false));
        REQUIRE(memcmp(&before_service, &context->state, sizeof(before_service)) == 0
                && memcmp(&before_boot, &context->boot_info, sizeof(before_boot)) == 0);
    }
    REQUIRE(initialize_service_state() && vm_core_test_run_to_frozen(context, 0x3333));
    REQUIRE(micros_vm_load_committed(&context->state, 0x2202, 0x3333, 7) == MICROS_VM_CORE_OK);
    context->state.free_page_cache[context->state.free_page_cache_count++] = TEST_FIRST_LOAD_FRAME;
    REQUIRE(micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK);
    {
        size_t executable_size = vm_core_test_build_elf(context, 1, 1, 1);
        size_t request_size = vm_core_test_build_request(context);
        int result;

        REQUIRE(begin(false));
        result = micros_vm_load_prepare(&context->state, 0x1112, values.pid, context->executable,
            executable_size, context->request, request_size, &values);
        REQUIRE(report("service-cache-stale", result, MICROS_VM_CORE_OK, false));
        REQUIRE(context->state.free_page_cache_count == 4 && context->state.allocation_origin == 10);
        for (size_t index = 0; index < 4; ++index) {
            REQUIRE(context->state.transaction.table_frames[index] == vm_core_test_frame_address(10 + index));
            REQUIRE(context->state.transaction.data_frames[index] == vm_core_test_frame_address(14 + index));
        }
        REQUIRE(micros_vm_load_aborted(&context->state, 0, 0) == MICROS_VM_CORE_OK);
    }
    return true;
}

static bool allocator_word_cases(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct micros_vm_boot_summary summary;

    for (unsigned shape = 0; shape < 2; ++shape) {
        size_t managed = shape == 0 ? 130 : MICROS_VM_MAX_MANAGED_FRAMES;
        struct micros_application_stack_values values = vm_core_test_stack_values(7);
        size_t executable_size, request_size;
        int result;

        REQUIRE(vm_core_test_initialize_boot_info(context, managed));
        for (size_t index = TEST_FIRST_FREE_FRAME; index < managed; ++index) {
            if (index >= 26 && (index < 64 || index >= 84)) {
                context->boot_info.frame_states[index] = MICROS_VM_FRAME_KERNEL;
            }
        }
        context->boot_info.header.free_frame_count = 40;
        context->boot_info.header.kernel_frame_count = managed - 43;
        context->boot_info.header.digest = 0;
        REQUIRE(micros_vm_boot_finalize(&context->boot_info, &summary) == MICROS_VM_BOOT_OK);
        REQUIRE(vm_core_test_initialize_state(context) == MICROS_VM_CORE_OK);
        executable_size = vm_core_test_build_elf(context, 13, 1, 1);
        request_size = vm_core_test_build_request(context);
        REQUIRE(micros_vm_load_prepare(&context->state, 0x1111, values.pid, context->executable,
            executable_size, context->request, request_size, &values) == MICROS_VM_CORE_OK);
        REQUIRE(micros_vm_load_bind(&context->state, (struct micros_process_handle){2, 7}, 0x2202)
                == MICROS_VM_CORE_OK);
        for (uint32_t page = 0; page < 16; ++page) {
            REQUIRE(micros_vm_load_scratch_mapped(&context->state, page) == MICROS_VM_CORE_OK);
            REQUIRE(micros_vm_load_fill_scratch(&context->state, page, context->scratch_page,
                                               sizeof(context->scratch_page)) == MICROS_VM_CORE_OK);
            REQUIRE(micros_vm_load_scratch_committed(&context->state, page) == MICROS_VM_CORE_OK);
        }
        REQUIRE(micros_vm_load_frozen(&context->state, 0x3333) == MICROS_VM_CORE_OK);
        REQUIRE(micros_vm_load_committed(&context->state, 0x2202, 0x3333, 7) == MICROS_VM_CORE_OK);
        REQUIRE(context->state.free_page_cache_count == 0 && context->state.allocation_origin == 6);
        values.pid = 8;
        executable_size = vm_core_test_build_elf(context, 1, 1, 1);
        request_size = vm_core_test_build_request(context);
        REQUIRE(begin(false));
        case_extent = managed;
        result = micros_vm_load_prepare(&context->state, 0x1112, values.pid, context->executable,
            executable_size, context->request, request_size, &values);
        REQUIRE(report(shape == 0 ? "service-bitmap-boundary" : "service-bitmap-max",
                       result, MICROS_VM_CORE_OK, false));
        REQUIRE(context->state.transaction.table_frames[0] == vm_core_test_frame_address(76)
                && context->state.transaction.data_frames[3] == vm_core_test_frame_address(83)
                && context->state.allocation_origin == 76 && context->state.free_page_cache_count == 0);
        REQUIRE(micros_vm_load_aborted(&context->state, 0, 0) == MICROS_VM_CORE_OK);
    }
    return true;
}

static bool allocator_backend_guard_cases(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    struct micros_ipc_message request, reply, sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    for (unsigned mutation = 0; mutation < 2; ++mutation) {
        uint64_t arguments[5];
        int result;

        REQUIRE(vm_service_test_initialize(&service, context));
        arguments[0] = 101;
        arguments[1] = 2;
        arguments[2] = (uint64_t)service.executable_size << 32 | 11;
        arguments[3] = (uint64_t)service.request_size << 32 | 12;
        arguments[4] = 0;
        request = vm_service_test_message(MICROS_VM_CONTROL_LOAD, arguments);
        if (mutation == 0) {
            context->state.free_page_cache_count = 21;
        } else {
            context->state.allocation_origin = TEST_MANAGED_FRAME_COUNT + 1;
        }
        reply = sentinel;
        REQUIRE(begin(false));
        result = micros_vm_service_handle_message(
            &service.service, vm_service_test_backend(), &service, &request, &reply);
        REQUIRE(report_scoped(mutation == 0 ? "message-cache-count" : "message-cache-origin",
                               result, MICROS_VM_SERVICE_ERROR_INVARIANT, false, false));
        REQUIRE(service.copies == 0 && service.begins == 0
                && memcmp(&reply, &sentinel, sizeof(reply)) == 0);
    }
    return true;
}

int main(int argc, char **argv)
{
#if defined(MICROS_SELECTED_VM_LAUNCHER)
    if (argc == 2) {
        const char *launcher = getenv("__PYVENV_LAUNCHER__");
        char *const arguments[] = {
            MICROS_SELECTED_VM_PYTHON, "-E", "-S", "-B", MICROS_SELECTED_VM_DRIVER,
            "--executable", argv[1], "--executable", argv[0], NULL,
        };

        if (launcher != NULL && launcher[0] != '\0') {
            fputs("__PYVENV_LAUNCHER__ is unsupported\n", stderr);
            return 2;
        }
        if (chdir(MICROS_SELECTED_VM_ROOT) != 0) {
            return 2;
        }
        execv(MICROS_SELECTED_VM_PYTHON, arguments);
        perror("VM01 launcher");
        return 2;
    }
#endif
    if (argc != 3 || strcmp(argv[1], "--run") != 0
#if defined(MICROS_TEST_WORK_COUNTS)
        || strcmp(argv[2], "work") != 0
#else
        || strcmp(argv[2], "ordinary") != 0
#endif
    ) {
        return 2;
    }
    if (!key_workspace_cases() || !service_load_cases() || !service_abort_cases() || !kernel_load_cases()
        || !kernel_abort_cases() || !message_cases() || !service_scope_cases()
        || !message_preflight_cases() || !kernel_scope_cases()
        || !service_extent_cases() || !kernel_dense_case() || !boot_mapping_cases()
        || !geometry_lookup_cases() || !kernel_global_record_cases() || !service_global_record_cases()
        || !load_token_issuance_cases() || !exit_token_scope_cases() || !kernel_owner_collision_cases()
        || !service_retained_page_cases() || !kernel_retained_prefix_cases()
        || !service_retained_scope_cases() || !kernel_retained_scope_cases() || !retained_stack_cases()
        || !allocator_reference_cases() || !allocator_guard_cases() || !allocator_word_cases()
        || !allocator_backend_guard_cases()) {
        return 1;
    }
    printf("MICROS_VM01_NATIVE_PASS cases=%zu\n", case_count);
    return 0;
}
