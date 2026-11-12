#include "tests/host/selected_ipc_validation_test.h"

#include "kernel/scheduler/scheduler_runtime_internal.h"
#include "micros/bootstrap_control.h"
#include "micros/ipc_runtime.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum completion_case {
    COMPLETION_EMPTY = 0,
    COMPLETION_SUCCESS,
    COMPLETION_ERROR,
    COMPLETION_MESSAGE,
    COMPLETION_CROSS_PAGE,
    COMPLETION_TOKEN,
    COMPLETION_KERNEL,
    COMPLETION_SOURCE_ONLY,
    COMPLETION_MALFORMED,
    COMPLETION_BUFFER_FAULT,
    COMPLETION_DUPLICATE_TOKEN,
};

struct completion_fixture {
    struct scheduler_runtime_context runtime;
    struct scheduler_completion_commit plan;
    struct micros_user_context expected_context;
    struct micros_ipc_message message;
    struct micros_process_handle owner;
    struct micros_thread_handle receiver;
    unsigned char memory[8192];
    unsigned char expected_memory[8192];
    jmp_buf panic_return;
    const char *name;
    const char *panic_reason;
    size_t offset;
    bool panic_expected;
    bool second_page_fault;
    bool forbid_lookups;
};

static struct completion_fixture completion;

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "IPC01_COMPLETION_FAILURE case=%s line=%d expression=%s\n", \
                    completion.name, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

const struct micros_endpoint_registry *micros_ipc_runtime_registry(void)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();

    ++fixture->lookups;
    return completion.forbid_lookups ? NULL : &fixture->registry;
}

enum micros_user_address_space_error micros_user_address_space_translate(
    struct micros_process_handle process,
    uint64_t address,
    uint64_t *physical_address,
    uint32_t *permissions,
    size_t *contiguous
)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    uint64_t base = UINT64_C(0x50000000);
    size_t offset;

    ++fixture->translations;
    if (completion.forbid_lookups || process.slot != completion.owner.slot
        || process.generation != completion.owner.generation) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    if (address < base || address >= base + sizeof(completion.memory)) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    offset = (size_t)(address - base);
    if (completion.second_page_fault && offset >= 4096) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    *physical_address = (uintptr_t)&completion.memory[offset];
    *permissions = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;
    *contiguous = 4096 - offset % 4096;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

void scheduler_runtime_clear_bytes(void *storage, size_t size)
{
    memset(storage, 0, size);
}

void scheduler_runtime_copy_user_context(
    struct micros_user_context *destination,
    const struct micros_user_context *source
)
{
    memcpy(destination, source, sizeof(*destination));
}

_Noreturn void scheduler_runtime_panic_scheduler_invariant(
    struct micros_hart *hart,
    const char *reason,
    struct micros_trap_frame *frame
)
{
    (void)hart;
    (void)frame;
    completion.panic_reason = reason;
    if (!completion.panic_expected) {
        fprintf(stderr, "unexpected IPC01 completion panic: %s\n", reason);
        abort();
    }
    longjmp(completion.panic_return, 1);
}

static void write_u32(unsigned char *bytes, uint32_t value)
{
    for (size_t index = 0; index < 4; ++index) {
        bytes[index] = (unsigned char)(value >> (index * 8));
    }
}

static bool setup_completion(enum completion_case kind, const char *name)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    struct micros_ipc_message message = {.type = UINT32_C(0x1234), .payload = {UINT8_C(0x5a)}};
    struct micros_thread *receiver;

    memset(&completion, 0, sizeof(completion));
    completion.name = name;
    REQUIRE(selected_ipc_setup(2));
    selected_ipc_test_name(name);
    REQUIRE(micros_hart_register(&fixture->objects, 0, &fixture->hart) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_hart_install_trap_stacks(
        &fixture->objects, fixture->hart,
        UINT64_C(0x20000000), UINT64_C(0x20000000) + MICROS_PRIMARY_TRAP_STACK_MIN_SIZE,
        UINT64_C(0x30000000), UINT64_C(0x30000000) + MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE
    ) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(selected_ipc_prepare_thread(0));
    REQUIRE(selected_ipc_prepare_thread(MICROS_PROCESS_CAPACITY - 1));
    completion.receiver = fixture->threads[1];
    receiver = &fixture->objects.threads[completion.receiver.slot];
    completion.owner = receiver->owner;
    completion.offset = kind == COMPLETION_CROSS_PAGE || kind == COMPLETION_BUFFER_FAULT ? 4064 : 0;
    if (kind <= COMPLETION_ERROR) {
        REQUIRE(micros_thread_runtime_flags_unset(
            &fixture->objects, completion.receiver, MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK);
        if (kind != COMPLETION_EMPTY) {
            REQUIRE(micros_ipc_stage_no_message_completion(
                &fixture->registry, &fixture->objects, completion.receiver,
                kind == COMPLETION_ERROR ? MICROS_IPC_ERROR_DEAD_ENDPOINT : MICROS_IPC_OK
            ) == MICROS_IPC_OK);
        }
    } else {
        REQUIRE(micros_ipc_receiver_enqueue(
            &fixture->registry, &fixture->objects, completion.receiver,
            MICROS_ENDPOINT_ANY, UINT64_C(0x50000000) + completion.offset
        ) == MICROS_IPC_OK);
        if (kind == COMPLETION_SOURCE_ONLY) {
            message.type = MICROS_BOOTSTRAP_MESSAGE_READY_ACK;
            memset(message.payload, 0, sizeof(message.payload));
            write_u32(&message.payload[0], MICROS_BOOTSTRAP_MANIFEST_VERSION);
            write_u32(&message.payload[4], 1);
            write_u32(&message.payload[8], MICROS_BOOTSTRAP_MANIFEST_VERSION);
            write_u32(&message.payload[16], fixture->endpoints[1]);
        }
        if (kind == COMPLETION_KERNEL) {
            REQUIRE(micros_ipc_inject_kernel_notification(
                &fixture->registry, &fixture->objects, fixture->endpoints[1], 1
            ) == MICROS_IPC_OK);
        } else if (kind == COMPLETION_TOKEN || kind == COMPLETION_DUPLICATE_TOKEN) {
            REQUIRE(micros_ipc_call(
                &fixture->registry, &fixture->objects, fixture->threads[0],
                fixture->endpoints[1], &message, UINT64_C(0x50000100)
            ) == MICROS_IPC_OK);
        } else {
            REQUIRE(micros_ipc_send(
                &fixture->registry, &fixture->objects, fixture->threads[0], fixture->endpoints[1], &message
            ) == MICROS_IPC_OK);
        }
    }
    completion.message = receiver->ipc_inbound_message;
    completion.runtime.selected_context = receiver->user_context;
    completion.expected_context = receiver->user_context;
    memset(completion.memory, 0xa5, sizeof(completion.memory));
    memcpy(completion.expected_memory, completion.memory, sizeof(completion.memory));
    return true;
}

static bool prepare_completion_mutation(enum completion_case kind)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    struct micros_thread *receiver = &fixture->objects.threads[completion.receiver.slot];

    if (kind == COMPLETION_SOURCE_ONLY) {
        REQUIRE(micros_thread_scheduler_hold(&fixture->objects, fixture->threads[0])
                == MICROS_KERNEL_OBJECT_OK);
        REQUIRE(micros_endpoint_seal_source_only(
            &fixture->registry, &fixture->objects, fixture->endpoints[0]
        ) == MICROS_ENDPOINT_OK);
    } else if (kind == COMPLETION_MALFORMED) {
        receiver->ipc_delivery_pending = false;
    } else if (kind == COMPLETION_BUFFER_FAULT) {
        completion.second_page_fault = true;
    } else if (kind == COMPLETION_DUPLICATE_TOKEN) {
        struct micros_thread *duplicate;

        REQUIRE(selected_ipc_prepare_thread(MICROS_PROCESS_CAPACITY - 1));
        REQUIRE(micros_thread_runtime_flags_unset(
            &fixture->objects, fixture->threads[2], MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK);
        duplicate = &fixture->objects.threads[fixture->threads[2].slot];
        duplicate->ipc_delivery_pending = true;
        duplicate->ipc_receive_buffer = UINT64_C(0x50000200);
        duplicate->ipc_inbound_message = completion.message;
    }
    return true;
}

static void preflight(void)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();

    scheduler_runtime_preflight_completion_or_panic(
        &fixture->objects, completion.receiver, &completion.plan,
        &fixture->objects.harts[fixture->hart.slot], NULL
    );
}

static bool run_completion_case(size_t index, enum completion_case kind, bool commit, const char *name)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    bool invalid = kind >= COMPLETION_MALFORMED;
    size_t chunks = kind <= COMPLETION_ERROR ? 0
        : kind == COMPLETION_CROSS_PAGE || kind == COMPLETION_BUFFER_FAULT ? 2 : 1;
    unsigned result = 0;

    REQUIRE(setup_completion(kind, name));
    REQUIRE(prepare_completion_mutation(kind));
    REQUIRE(micros_endpoint_registry_validate_objects(
        &fixture->registry, &fixture->objects
    ) == (kind == COMPLETION_MALFORMED || kind == COMPLETION_DUPLICATE_TOKEN
          ? MICROS_ENDPOINT_ERROR_INVARIANT : MICROS_ENDPOINT_OK));
    if (commit) {
        preflight();
        completion.expected_context.a0 = kind == COMPLETION_ERROR ? UINT64_MAX - 1 : 0;
        if (chunks != 0) {
            memcpy(completion.expected_memory + completion.offset, &completion.message, sizeof(completion.message));
        }
        completion.forbid_lookups = true;
    }
    fixture->before_objects = fixture->objects;
    fixture->before_registry = fixture->registry;
    fixture->lookups = 0;
    fixture->translations = 0;
    fixture->plan_bytes = sizeof(completion.plan);
    completion.panic_expected = invalid;
    REQUIRE(selected_ipc_begin_capture(index));
    if (invalid) {
        if (setjmp(completion.panic_return) == 0) {
            preflight();
            REQUIRE(false);
        }
        result = 1;
    } else if (commit) {
        scheduler_runtime_commit_completion_or_panic(
            &completion.runtime, &completion.plan,
            &fixture->objects.harts[fixture->hart.slot], NULL
        );
    } else {
        preflight();
    }
    REQUIRE(selected_ipc_finish_capture());
    if (invalid) {
        REQUIRE(strcmp(completion.panic_reason, kind == COMPLETION_BUFFER_FAULT
                       ? "invalid-bootstrap-ipc-buffer" : "scheduler-ipc-completion") == 0);
    } else if (commit) {
        const struct micros_thread *receiver = &fixture->objects.threads[completion.receiver.slot];

        REQUIRE(micros_thread_ipc_state_is_clear(receiver));
        REQUIRE(memcmp(&receiver->user_context, &completion.expected_context, sizeof(receiver->user_context)) == 0);
        REQUIRE(memcmp(&completion.runtime.selected_context, &completion.expected_context,
                       sizeof(completion.expected_context)) == 0);
    } else {
        REQUIRE(completion.plan.pending == (kind != COMPLETION_EMPTY));
        REQUIRE(completion.plan.buffer_plan.chunk_count == chunks);
        if (chunks != 0) {
            REQUIRE(memcmp(&completion.plan.message, &completion.message, sizeof(completion.message)) == 0);
        }
    }
    REQUIRE(memcmp(completion.memory, completion.expected_memory, sizeof(completion.memory)) == 0);
    return selected_ipc_report_case(index, name, result, invalid ? 0 : chunks,
                                    invalid || !commit || kind == COMPLETION_EMPTY);
}

size_t selected_ipc_completion_case_count(void)
{
    return 19;
}

bool selected_ipc_completion_cases(size_t first_case)
{
    static const char *const names[8][2] = {
        {"completion-empty-plan", "completion-empty-commit"},
        {"completion-success-plan", "completion-success-commit"},
        {"completion-error-plan", "completion-error-commit"},
        {"completion-message-plan", "completion-message-commit"},
        {"completion-cross-plan", "completion-cross-commit"},
        {"completion-token-plan", "completion-token-commit"},
        {"completion-kernel-plan", "completion-kernel-commit"},
        {"completion-source-only-plan", "completion-source-only-commit"},
    };
    static const char *const failures[] = {
        "completion-malformed", "completion-buffer-fault", "completion-duplicate-token",
    };

    for (size_t kind = 0; kind < 8; ++kind) {
        for (size_t commit = 0; commit < 2; ++commit) {
            if (!run_completion_case(first_case++, (enum completion_case)kind, commit != 0, names[kind][commit])) {
                return false;
            }
        }
    }
    for (size_t index = 0; index < 3; ++index) {
        if (!run_completion_case(first_case++, (enum completion_case)(COMPLETION_MALFORMED + index),
                                 false, failures[index])) {
            return false;
        }
    }
    return true;
}
