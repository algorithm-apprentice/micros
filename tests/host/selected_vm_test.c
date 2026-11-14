#include "tests/host/vm_service_test_internal.h"
#include "tests/host/vm_handoff_test_internal.h"
#include "tests/qemu/vm_validation_work.h"

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
static struct vm_service_test_fixture service;
static size_t case_count;
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

static bool begin(bool kernel)
{
    struct vm_core_test_context *context = vm_core_test_context_get();

    if (kernel) {
        before_kernel = kernel_state;
    } else {
        REQUIRE(context->state.boot_info == &context->boot_info);
        before_service = context->state;
        before_boot = context->boot_info;
    }
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(micros_test_work_begin(
        UINT64_C(0x564d30314e000001), case_count + 1,
        (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_VM)
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
        before = digest(UINT64_C(14695981039346656037), &before_kernel, sizeof(before_kernel));
        after = digest(UINT64_C(14695981039346656037), &kernel_state, sizeof(kernel_state));
        preserved = memcmp(&before_kernel, &kernel_state, sizeof(kernel_state)) == 0;
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
    printf("MICROS_VM01_CASE {\"version\":1,\"case\":%zu,\"name\":\"%s\",\"result\":%d,"
           "\"phase\":%u,\"preserved\":%u,\"before_state\":%" PRIu64 ",\"after_state\":%" PRIu64 "}\n",
           case_count, name, result, phase, (unsigned)preserved, before, after);
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
        REQUIRE(context->state.transaction.table_frames[index] == vm_core_test_frame_address(6 + index));
        REQUIRE(context->state.transaction.data_frames[index] == vm_core_test_frame_address(10 + index));
    }
    REQUIRE(begin(false));
    result = micros_vm_load_bind(&context->state, (struct micros_process_handle){2, 7}, 0x2222);
    REQUIRE(report("service-bind", result, MICROS_VM_CORE_OK, false));
    REQUIRE(context->state.mapping_records[2].entry_count == 8);
    REQUIRE(begin(false));
    result = micros_vm_load_next_page(&context->state, &page);
    REQUIRE(report("service-next-page", result, MICROS_VM_CORE_OK, false));
    REQUIRE(page.page_index == 0 && page.virtual_address == MICROS_APPLICATION_ENTRY
            && page.physical_address == vm_core_test_frame_address(10)
            && page.permissions == (MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ
                                    | MICROS_SV39_PERMISSION_EXECUTE));
    REQUIRE(begin(false));
    result = micros_vm_load_scratch_mapped(&context->state, 0);
    REQUIRE(report("service-map", result, MICROS_VM_CORE_OK, false));
    REQUIRE(context->state.transaction.scratch_page == 1
            && context->boot_info.frame_states[10] == MICROS_VM_LOCAL_FRAME_TRANSFERABLE);
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
    REQUIRE(freeze.load_token == 0x2222 && freeze.image_page_count == 3 && freeze.data_page_count == 4
            && freeze.entry == MICROS_APPLICATION_ENTRY && freeze.stack_pointer == UINT64_C(0x7fffff70)
            && freeze.page_plan_digest == vm_core_test_expected_page_digest(context));
    REQUIRE(begin(false));
    result = micros_vm_load_frozen(&context->state, 0x3333);
    REQUIRE(report("service-freeze", result, MICROS_VM_CORE_OK, false));
    REQUIRE(begin(false));
    result = micros_vm_load_committed(&context->state, 0x2222, 0x3333, 7);
    REQUIRE(report("service-commit", result, MICROS_VM_CORE_OK, false));
    REQUIRE(context->state.mapping_records[2].phase == MICROS_VM_MAPPING_RECORD_ACTIVE
            && vm_core_test_bytes_are_zero(&context->state.transaction, sizeof(context->state.transaction)));
    REQUIRE(begin(false));
    result = micros_vm_exit_release_begin(&context->state, 0x2222, 0x3333, 7, &exit);
    REQUIRE(report("service-exit-begin", result, MICROS_VM_CORE_OK, false));
    REQUIRE(exit.process_slot == 2 && exit.process_generation == 7 && exit.load_token == 0x2222
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
    REQUIRE(micros_vm_load_bind(&context->state, (struct micros_process_handle){2, 7}, 0x2222) == MICROS_VM_CORE_OK);
    REQUIRE(micros_vm_load_scratch_mapped(&context->state, 0) == MICROS_VM_CORE_OK);
    REQUIRE(begin(false));
    result = micros_vm_load_aborted(&context->state, 0x2222, 0);
    REQUIRE(report("service-abort-loading", result, MICROS_VM_CORE_OK, false));
    REQUIRE(vm_core_test_run_to_frozen(context, 0x3333));
    REQUIRE(begin(false));
    result = micros_vm_load_aborted(&context->state, 0x2222, 0x3333);
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

    REQUIRE(micros_vm_load_freeze_preflight(&kernel_state, &request, &plan) == MICROS_VM_HANDOFF_OK);
    micros_vm_load_freeze_commit_prevalidated(&kernel_state, &plan);
    return true;
}

static bool kernel_load_cases(void)
{
    struct micros_process_handle process = vm_handoff_test_dynamic_process();
    struct micros_vm_load_begin_plan start;
    struct micros_vm_scratch_map_request map = {
        .load_token = 1, .virtual_address = MICROS_APPLICATION_ENTRY, .physical_address = 0x800000,
        .permissions = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE,
    };
    struct micros_vm_scratch_commit_request commit = {
        .load_token = 1, .virtual_address = MICROS_APPLICATION_ENTRY, .physical_address = 0x800000,
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
    REQUIRE(start.result.load_token == 1 && start.result.process_slot == process.slot
            && start.result.process_generation == process.generation);
    REQUIRE(begin(true));
    result = micros_vm_scratch_map_preflight(&kernel_state, &map, &page);
    REQUIRE(result == MICROS_VM_HANDOFF_OK);
    micros_vm_scratch_map_commit_prevalidated(&kernel_state, &page);
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
    result = micros_vm_load_freeze_preflight(&kernel_state, &request, &freeze);
    REQUIRE(result == MICROS_VM_HANDOFF_OK);
    micros_vm_load_freeze_commit_prevalidated(&kernel_state, &freeze);
    REQUIRE(report("kernel-freeze", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(freeze.result.mapping_generation == 1 && kernel_state.mappings[process.slot].root_physical_address == 0x810000);
    REQUIRE(begin(true));
    result = micros_vm_load_mark_prepared(&kernel_state, process, 101, 1, 1);
    REQUIRE(report("kernel-prepare", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(begin(true));
    result = micros_vm_load_mark_unprepared(&kernel_state, process, 101, 1, 1);
    REQUIRE(report("kernel-unprepare", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(micros_vm_load_mark_prepared(&kernel_state, process, 101, 1, 1) == MICROS_VM_HANDOFF_OK);
    REQUIRE(begin(true));
    result = micros_vm_load_mark_activated(&kernel_state, process, 101, 1, 1);
    REQUIRE(report("kernel-activate", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(begin(true));
    result = micros_vm_load_commit_preflight(&kernel_state, 1, 1);
    REQUIRE(result == MICROS_VM_HANDOFF_OK);
    micros_vm_load_commit_commit_prevalidated(&kernel_state);
    REQUIRE(report("kernel-commit", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(kernel_state.mappings[process.slot].phase == MICROS_VM_KERNEL_MAPPING_ACTIVE);
    REQUIRE(begin(true));
    result = micros_vm_load_mark_exit_detached(&kernel_state, process, 701, 1, 1);
    REQUIRE(report("kernel-exit-detach", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(begin(true));
    result = micros_vm_exit_release_preflight(&kernel_state, 701, 1, 1, &exit);
    REQUIRE(result == MICROS_VM_HANDOFF_OK && exit.entry_count == 4
            && exit.process.slot == process.slot && exit.process.generation == process.generation);
    micros_vm_exit_release_commit_prevalidated(&kernel_state, exit.process);
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
    result = micros_vm_load_abort_preflight(&kernel_state, 1, 0, &action);
    REQUIRE(result == MICROS_VM_HANDOFF_OK && !action.frozen && action.entry_count == 4);
    micros_vm_load_abort_commit_prevalidated(&kernel_state);
    REQUIRE(report("kernel-abort-loading", result, MICROS_VM_HANDOFF_OK, true));
    REQUIRE(vm_handoff_test_begin_and_fill_load(&kernel_state, 102) && kernel_freeze());
    REQUIRE(begin(true));
    result = micros_vm_load_abort_preflight(&kernel_state, 2, 1, &action);
    REQUIRE(result == MICROS_VM_HANDOFF_OK && action.frozen && action.root_physical_address == 0x810000);
    micros_vm_load_abort_commit_prevalidated(&kernel_state);
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
        REQUIRE(vm_service_test_read_word(&reply.payload[16]) == 201
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
    const uint64_t frozen[5] = {201, 301, 2, 0, 0};
    const uint64_t exit[5] = {701, 201, 301, 2, 0};

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
        REQUIRE(micros_vm_load_bind(&context->state, (struct micros_process_handle){2, 7}, 0x2222) == MICROS_VM_CORE_OK);
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
                    && page.physical_address == vm_core_test_frame_address(10) && page.page_index == 0);
        } else {
            REQUIRE(memcmp(&page, &sentinel, sizeof(page)) == 0);
        }
    }
    REQUIRE(initialize_service_state() && vm_core_test_run_to_frozen(context, 0x3333));
    context->state.transaction.data_frames[3] = UINT64_C(0xfffffffffffff000);
    context->state.mapping_records[2].entries[7].physical_address = UINT64_C(0xfffffffffffff000);
    REQUIRE(begin(false));
    result = micros_vm_load_aborted(&context->state, 0x2222, 0x3333);
    REQUIRE(report_scoped("service-cleanup-preflight", result, MICROS_VM_CORE_ERROR_INVARIANT, false, false));
    REQUIRE(memcmp(&before_service, &context->state, sizeof(before_service)) == 0
            && memcmp(&before_boot, &context->boot_info, sizeof(before_boot)) == 0);
    return true;
}

static bool message_preflight_cases(void)
{
    struct vm_core_test_context *context = vm_core_test_context_get();
    const uint64_t arguments[5] = {201, 301, 2, 0, 0};
    struct micros_ipc_message request, reply, sentinel;
    int result;

    for (uint32_t command = MICROS_VM_CONTROL_ABORT_LOAD; command <= MICROS_VM_CONTROL_COMMIT_LOAD; ++command) {
        REQUIRE(vm_service_test_initialize(&service, context) && vm_service_test_load(&service));
        context->boot_info.frame_states[13] = MICROS_VM_LOCAL_FRAME_FREE;
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
        .load_token = 1, .virtual_address = MICROS_APPLICATION_ENTRY, .physical_address = 0x800000,
        .permissions = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE,
    };
    struct micros_vm_scratch_commit_request commit = {
        .load_token = 1, .virtual_address = MICROS_APPLICATION_ENTRY, .physical_address = 0x800000,
    };
    struct micros_vm_scratch_plan page, sentinel;
    int result;

    kernel_state = vm_handoff_test_handed_off_state();
    REQUIRE(micros_vm_load_begin_preflight(&kernel_state, 101, process, true, &start) == MICROS_VM_HANDOFF_OK);
    micros_vm_load_begin_commit_prevalidated(&kernel_state, &start);
    REQUIRE(micros_vm_scratch_map_preflight(&kernel_state, &map, &page) == MICROS_VM_HANDOFF_OK);
    micros_vm_scratch_map_commit_prevalidated(&kernel_state, &page);
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
    result = micros_vm_scratch_map_preflight(&kernel_state, &map, &page);
    REQUIRE(report_scoped("kernel-scanned-count", result, MICROS_VM_HANDOFF_ERROR_INVARIANT, true, false));
    REQUIRE(memcmp(&page, &sentinel, sizeof(page)) == 0);

    kernel_state = vm_handoff_test_handed_off_state();
    REQUIRE(micros_vm_load_begin_preflight(&kernel_state, 101, process, true, &start) == MICROS_VM_HANDOFF_OK);
    micros_vm_load_begin_commit_prevalidated(&kernel_state, &start);
    kernel_state.load.process.slot = MICROS_PROCESS_CAPACITY;
    page = sentinel;
    REQUIRE(begin(true));
    result = micros_vm_scratch_map_preflight(&kernel_state, &map, &page);
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
        REQUIRE(micros_vm_load_bind(&context->state, (struct micros_process_handle){2, 7}, 0x2222) == MICROS_VM_CORE_OK);
        REQUIRE(context->state.transaction.page_count == (shape == 0 ? 4 : 16));
        REQUIRE(begin(false));
        result = micros_vm_load_next_page(&context->state, &page);
        REQUIRE(report(shape == 0 ? "service-max-frames" : "service-max-ranges-pages",
                       result, MICROS_VM_CORE_OK, false));
        REQUIRE(page.page_index == 0 && page.physical_address == vm_core_test_frame_address(10)
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
        record->load_token = slot + 2;
        record->mapping_generation = slot + 2;
        record->phase = MICROS_VM_KERNEL_MAPPING_ACTIVE;
        record->root_physical_address += offset;
        for (size_t index = 0; index < 4; ++index) {
            record->table_frames[index] += offset;
            record->entries[index].physical_address += offset;
        }
    }
    kernel_state.last_load_token = MICROS_PROCESS_CAPACITY + 1;
    kernel_state.last_mapping_generation = MICROS_PROCESS_CAPACITY + 1;
    REQUIRE(micros_vm_handoff_state_validate(&kernel_state) == MICROS_VM_HANDOFF_OK);
    REQUIRE(begin(true));
    result = micros_vm_load_mark_prepared(&kernel_state, selected, 101, 1, 1);
    REQUIRE(report("kernel-dense-records", result, MICROS_VM_HANDOFF_OK, true));
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
    if (!service_load_cases() || !service_abort_cases() || !kernel_load_cases()
        || !kernel_abort_cases() || !message_cases() || !service_scope_cases()
        || !message_preflight_cases() || !kernel_scope_cases()
        || !service_extent_cases() || !kernel_dense_case()) {
        return 1;
    }
    printf("MICROS_VM01_NATIVE_PASS cases=%zu\n", case_count);
    return 0;
}
