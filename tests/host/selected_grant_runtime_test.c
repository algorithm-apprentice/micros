#include "tests/host/selected_grant_validation_internal.h"

#include "kernel/grant/grant_runtime_internal.h"
#include "kernel/grant/grant_syscall.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/scheduler_core.h"

enum runtime_operation {
    RUNTIME_UNAVAILABLE, RUNTIME_REVOKE, RUNTIME_SHAPE, RUNTIME_CREATE,
    RUNTIME_COPY_FROM, RUNTIME_COPY_TO, RUNTIME_VALIDATE, RUNTIME_DEVICE,
    RUNTIME_SAME_VA, RUNTIME_CREATE_DEVICE, RUNTIME_SYSCALL_FROM, RUNTIME_SYSCALL_TO,
    RUNTIME_SYSCALL_VALIDATE,
};

enum runtime_condition {
    NORMAL, NO_TTY, PREPARED_TTY, STARTING_TTY, BAD_TTY_THREAD, BAD_TTY_ROOT, BAD_TTY_ROUTE,
    UNUSED_BOOTSTRAP_POLICY, BAD_READY_HEADER, UNRELATED_BAD_GRANT, BAD_SELECTED_GRANT,
    ZERO_LENGTH, ZERO_BAD_PHASE, RANGE_BAD_PHASE, REMOTE_FAULT_LOCAL_INVARIANT,
    REMOTE_INVARIANT_LOCAL_FAULT, DEVICE_TAIL_INVARIANT, NO_LEDGER, REVOKED_AFTER_VALIDATE,
};

struct runtime_case {
    const char *name;
    enum runtime_operation operation;
    size_t occupied;
    enum micros_grant_error result;
    uint64_t value;
    enum runtime_condition condition;
};

static const struct runtime_case cases[] = {
    {"syscall-unavailable", RUNTIME_UNAVAILABLE, 0, MICROS_GRANT_ERROR_INVARIANT, 0, NORMAL},
    {"syscall-revoke", RUNTIME_REVOKE, 1, MICROS_GRANT_OK, 0, NORMAL},
    {"syscall-shape", RUNTIME_SHAPE, 64, MICROS_GRANT_OK, UINT64_MAX, NORMAL},
    {"syscall-create", RUNTIME_CREATE, 0, MICROS_GRANT_OK, 64, NORMAL},
    {"copy-from", RUNTIME_COPY_FROM, 1, MICROS_GRANT_OK, 32, NORMAL},
    {"copy-to", RUNTIME_COPY_TO, 1, MICROS_GRANT_OK, 32, NORMAL},
    {"validate-remote", RUNTIME_VALIDATE, 64, MICROS_GRANT_OK, 0, NORMAL},
    {"copy-device", RUNTIME_DEVICE, 1, MICROS_GRANT_ERROR_FAULT, 0, NORMAL},
    {"copy-same-va-ram", RUNTIME_SAME_VA, 1, MICROS_GRANT_OK, 32, NORMAL},
    {"syscall-create-device", RUNTIME_CREATE_DEVICE, 0, MICROS_GRANT_OK, (uint64_t)(int64_t)-6, NORMAL},
    {"syscall-copy-from", RUNTIME_SYSCALL_FROM, 1, MICROS_GRANT_OK, 0, NORMAL},
    {"syscall-copy-to", RUNTIME_SYSCALL_TO, 1, MICROS_GRANT_OK, 0, NORMAL},
    {"syscall-validate", RUNTIME_SYSCALL_VALIDATE, 1, MICROS_GRANT_OK, 0, NORMAL},
    {"copy-no-device", RUNTIME_COPY_FROM, 1, MICROS_GRANT_OK, 32, NO_TTY},
    {"copy-prepared-device", RUNTIME_COPY_FROM, 1, MICROS_GRANT_OK, 32, PREPARED_TTY},
    {"copy-starting-device", RUNTIME_COPY_FROM, 1, MICROS_GRANT_OK, 32, STARTING_TTY},
    {"copy-bad-device-thread", RUNTIME_COPY_FROM, 1, MICROS_GRANT_ERROR_INVARIANT, 0, BAD_TTY_THREAD},
    {"copy-bad-device-root", RUNTIME_COPY_FROM, 1, MICROS_GRANT_ERROR_INVARIANT, 0, BAD_TTY_ROOT},
    {"copy-bad-device-route", RUNTIME_COPY_FROM, 1, MICROS_GRANT_ERROR_INVARIANT, 0, BAD_TTY_ROUTE},
    {"copy-unused-bootstrap-policy", RUNTIME_COPY_FROM, 1, MICROS_GRANT_OK, 32, UNUSED_BOOTSTRAP_POLICY},
    {"syscall-readiness-before-shape", RUNTIME_SHAPE, 64, MICROS_GRANT_ERROR_INVARIANT, 0, BAD_READY_HEADER},
    {"syscall-shape-unrelated-corruption", RUNTIME_SHAPE, 64, MICROS_GRANT_OK, UINT64_MAX, UNRELATED_BAD_GRANT},
    {"syscall-selected-invariant", RUNTIME_REVOKE, 1, MICROS_GRANT_ERROR_INVARIANT, 0, BAD_SELECTED_GRANT},
    {"syscall-create-device-invariant", RUNTIME_CREATE, 0, MICROS_GRANT_ERROR_INVARIANT, 0, BAD_TTY_ROOT},
    {"copy-zero-before-device", RUNTIME_COPY_FROM, 1, MICROS_GRANT_OK, 0, ZERO_LENGTH},
    {"copy-phase-before-zero", RUNTIME_COPY_FROM, 1, MICROS_GRANT_ERROR_PHASE, 0, ZERO_BAD_PHASE},
    {"copy-range-before-phase", RUNTIME_COPY_FROM, 1, MICROS_GRANT_ERROR_RANGE, 0, RANGE_BAD_PHASE},
    {"copy-fault-invariant", RUNTIME_COPY_FROM, 1, MICROS_GRANT_ERROR_INVARIANT, 0, REMOTE_FAULT_LOCAL_INVARIANT},
    {"copy-invariant-fault", RUNTIME_COPY_FROM, 1, MICROS_GRANT_ERROR_INVARIANT, 0, REMOTE_INVARIANT_LOCAL_FAULT},
    {"copy-device-tail-invariant", RUNTIME_DEVICE, 1, MICROS_GRANT_ERROR_INVARIANT, 0, DEVICE_TAIL_INVARIANT},
    {"copy-missing-ledger", RUNTIME_COPY_FROM, 1, MICROS_GRANT_ERROR_INVARIANT, 0, NO_LEDGER},
    {"copy-revalidates-after-validation", RUNTIME_COPY_FROM, 1, MICROS_GRANT_ERROR_STALE_GRANT, 0, REVOKED_AFTER_VALIDATE},
};

static bool is_from(const struct runtime_case *test)
{
    return test->operation == RUNTIME_COPY_FROM || test->operation == RUNTIME_SYSCALL_FROM
        || test->operation == RUNTIME_SAME_VA;
}

static bool is_to(const struct runtime_case *test)
{
    return test->operation == RUNTIME_COPY_TO || test->operation == RUNTIME_SYSCALL_TO;
}

static bool is_captured(const struct runtime_case *test)
{
    return test->operation <= RUNTIME_CREATE || test->operation >= RUNTIME_CREATE_DEVICE;
}

static size_t request_length(const struct runtime_case *test)
{
    return test->condition == ZERO_LENGTH || test->condition == ZERO_BAD_PHASE ? 0 : 32;
}

static bool runtime_publish(void)
{
    struct grant01_fixture *fixture = grant01_fixture();
    const struct micros_privilege_profile profile = {
        .id = 1, .name = "NATIVE", .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
    };

    if (micros_ipc_runtime_registry() == NULL) {
        fixture->before_objects = fixture->objects;
        memset(&fixture->objects, 0, sizeof(fixture->objects));
        REQUIRE(micros_kernel_objects_initialize(&fixture->objects, 1, 1) == MICROS_KERNEL_OBJECT_OK);
        REQUIRE(micros_ipc_runtime_initialize(&profile, 1) == MICROS_ENDPOINT_OK);
        fixture->objects = fixture->before_objects;
    }
    *micros_ipc_runtime_authoritative_registry() = fixture->endpoints;
    if (micros_grant_runtime_registry() == NULL) {
        REQUIRE(micros_grant_runtime_initialize() == MICROS_GRANT_OK);
    }
    *micros_grant_runtime_authoritative_registry() = fixture->grants;
    return true;
}

static bool runtime_context(size_t caller)
{
    struct grant01_fixture *fixture = grant01_fixture();
    struct micros_scheduler_return_plan plan;
    struct micros_user_context context = {.sepc = MICROS_USER_VIRTUAL_BASE, .sp = MICROS_USER_VIRTUAL_END};

    for (size_t index = 0; index < GRANT01_ACTORS; ++index) {
        uintptr_t bottom = UINT64_C(0x03000000) + index * MICROS_THREAD_KERNEL_STACK_SIZE;

        fixture->objects.processes[index].address_space_root = UINT64_C(0x81000000) + index * 4096;
        REQUIRE(micros_thread_attach_execution_context(
            &fixture->objects, fixture->threads[index], bottom, bottom + MICROS_THREAD_KERNEL_STACK_SIZE, &context
        ) == MICROS_KERNEL_OBJECT_OK);
    }
    for (size_t index = 0; index < GRANT01_ACTORS; ++index) {
        size_t selected = (caller + index) % GRANT01_ACTORS;

        REQUIRE(micros_thread_scheduler_admit(
            &fixture->objects, fixture->hart, fixture->threads[selected], 8, 1000, true
        ) == MICROS_KERNEL_OBJECT_OK);
    }
    REQUIRE(micros_hart_plan_user_return(&fixture->objects, fixture->hart, &plan) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_hart_commit_user_return(&fixture->objects, &plan) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(fixture->objects.harts[fixture->hart.slot].current_thread.slot == caller);
    return true;
}

static bool runtime_tty(void)
{
    struct grant01_fixture *fixture = grant01_fixture();
    struct tty_handoff_context *context = tty_handoff_context();
    struct micros_tty_handoff_runtime_state *state = &context->tty_handoff_state;

    *context = (struct tty_handoff_context){0};
    *state = (struct micros_tty_handoff_runtime_state){
        .service_id = MICROS_TTY_SERVICE_ID, .image_id = 4, .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
        .process = fixture->processes[3], .thread = fixture->threads[3], .endpoint = fixture->identities[3],
        .root_physical_address = fixture->objects.processes[3].address_space_root,
    };
    REQUIRE(micros_tty_handoff_initialize(&state->handoff) == MICROS_TTY_HANDOFF_OK);
    REQUIRE(micros_tty_handoff_begin(&state->handoff, 1, 1000) == MICROS_TTY_HANDOFF_OK);
    REQUIRE(micros_tty_handoff_mark_mapped(&state->handoff) == MICROS_TTY_HANDOFF_OK);
    REQUIRE(micros_tty_handoff_release(&state->handoff) == MICROS_TTY_HANDOFF_OK);
    REQUIRE(micros_tty_handoff_commit(&state->handoff) == MICROS_TTY_HANDOFF_OK);
    REQUIRE(micros_tty_handoff_accept_ready(&state->handoff, 2) == MICROS_TTY_HANDOFF_OK);
    fixture->bootstrap.entry_count = 1;
    fixture->bootstrap.plan.console_service_id = MICROS_TTY_SERVICE_ID;
    fixture->bootstrap.bindings[0] = (struct micros_bootstrap_binding){
        .service_id = MICROS_TTY_SERVICE_ID, .process = state->process, .thread = state->thread,
        .root = state->root_physical_address, .endpoint = state->endpoint,
        .scheduler_priority = 8, .scheduler_preemptible = true, .scheduler_quantum_counter_ticks = 1000,
    };
    fixture->bootstrap.manifest.entries[0] = (struct micros_bootstrap_manifest_entry){
        .service_id = MICROS_TTY_SERVICE_ID, .image_id = 4, .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
        .process_slot = MICROS_TTY_PROCESS_SLOT, .role_flags = MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER,
        .irq_source = MICROS_TTY_UART_IRQ_SOURCE, .device_base = MICROS_TTY_UART_PHYSICAL_BASE,
        .device_length = MICROS_TTY_UART_MAPPED_LENGTH,
    };
    fixture->bootstrap.transitions.entry_count = 1;
    fixture->bootstrap.transitions.entries[0] = (struct micros_bootstrap_runtime_entry){
        .service_id = MICROS_TTY_SERVICE_ID, .state = MICROS_BOOTSTRAP_SERVICE_READY,
        .endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE, .profile_installed = true, .scheduler_assigned = true,
    };
    context->tty_bootstrap_state = &fixture->bootstrap;
    REQUIRE(micros_tty_handoff_runtime_validate(
        &fixture->bootstrap, &fixture->endpoints, &fixture->objects
    ) == MICROS_TTY_HANDOFF_OK);
    return true;
}

static bool runtime_condition(const struct runtime_case *test)
{
    struct grant01_fixture *fixture = grant01_fixture();
    struct micros_tty_handoff_runtime_state *state = &tty_handoff_context()->tty_handoff_state;
    struct micros_bootstrap_runtime_entry *transition = &fixture->bootstrap.transitions.entries[0];

    switch (test->condition) {
    case NORMAL: break;
    case NO_TTY: *tty_handoff_context() = (struct tty_handoff_context){0}; break;
    case PREPARED_TTY:
        REQUIRE(micros_thread_scheduler_hold(&fixture->objects, fixture->threads[3]) == MICROS_KERNEL_OBJECT_OK);
        REQUIRE(micros_thread_scheduler_remove(&fixture->objects, fixture->threads[3]) == MICROS_KERNEL_OBJECT_OK);
        fixture->objects.processes[3].privilege_profile = 0;
        fixture->endpoints.endpoints[3].state = MICROS_ENDPOINT_STATE_RESERVED;
        state->handoff.console_phase = MICROS_TTY_CONSOLE_MAPPED;
        state->handoff.route_phase = MICROS_TTY_ROUTE_DISABLED;
        state->handoff.deadline_armed = true;
        state->handoff.deadline = 1001;
        transition->state = MICROS_BOOTSTRAP_SERVICE_PREPARED;
        transition->endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_RESERVED;
        transition->profile_installed = false;
        transition->scheduler_assigned = false;
        break;
    case STARTING_TTY:
        state->handoff.console_phase = MICROS_TTY_CONSOLE_STARTING;
        state->handoff.route_phase = MICROS_TTY_ROUTE_DISABLED;
        state->handoff.deadline_armed = true;
        state->handoff.deadline = 1001;
        transition->state = MICROS_BOOTSTRAP_SERVICE_STARTING;
        transition->ready_deadline = 1001;
        break;
    case BAD_TTY_THREAD: ++state->thread.generation; break;
    case BAD_TTY_ROOT:
    case ZERO_LENGTH: state->root_physical_address += 4096; break;
    case BAD_TTY_ROUTE: state->handoff.claimed_source = MICROS_TTY_UART_IRQ_SOURCE; break;
    case UNUSED_BOOTSTRAP_POLICY: ++fixture->bootstrap.bindings[0].scheduler_priority; break;
    case BAD_READY_HEADER: fixture->grants.active_count = MICROS_GRANT_CAPACITY + 1; break;
    case UNRELATED_BAD_GRANT: fixture->grants.grants[63].permissions = 0; break;
    case BAD_SELECTED_GRANT: fixture->grants.grants[0].permissions = 0; break;
    case ZERO_BAD_PHASE:
    case RANGE_BAD_PHASE: fixture->ledger.phase = 0; break;
    case REMOTE_FAULT_LOCAL_INVARIANT:
        fixture->translation_error[0][0] = MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
        fixture->translation_error[1][0] = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        break;
    case REMOTE_INVARIANT_LOCAL_FAULT:
        fixture->translation_error[0][0] = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        fixture->translation_error[1][0] = MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
        break;
    case DEVICE_TAIL_INVARIANT:
        fixture->grants.grants[0].base = MICROS_TTY_UART_VIRTUAL_BASE + 4088;
        fixture->memory_base[3] = MICROS_TTY_UART_VIRTUAL_BASE;
        fixture->translation_error[3][1] = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        fixture->translation_error[1][0] = MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
        break;
    case NO_LEDGER: fixture->ledger_available = false; break;
    case REVOKED_AFTER_VALIDATE:
        REQUIRE(micros_grant_validate_range(
            &fixture->grants, &fixture->endpoints, &fixture->objects, fixture->processes[1],
            fixture->identities[0], 64, 0, 32, MICROS_GRANT_PERMISSION_READ
        ) == MICROS_GRANT_OK);
        REQUIRE(memcmp(fixture->before_memory, fixture->memory, sizeof(fixture->memory)) == 0);
        REQUIRE(micros_grant_revoke(
            &fixture->grants, &fixture->endpoints, &fixture->objects, fixture->processes[0], 64
        ) == MICROS_GRANT_OK);
        memset(fixture->translations, 0, sizeof(fixture->translations));
        fixture->device_checks = 0;
        break;
    }
    return true;
}

static bool runtime_setup(const struct runtime_case *test)
{
    struct grant01_fixture *fixture = grant01_fixture();

    REQUIRE(grant01_setup(test->occupied, GRANT01_ACTORS));
    REQUIRE(runtime_context(test->operation == RUNTIME_CREATE_DEVICE ? 3
                            : test->operation >= RUNTIME_SYSCALL_FROM ? 1 : 0));
    REQUIRE(runtime_tty());
    fixture->ledger_available = true;
    fixture->ledger.phase = test->operation == RUNTIME_COPY_TO
        ? MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF : MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP;
    for (size_t actor = 0; actor < GRANT01_ACTORS; ++actor) {
        fixture->memory_base[actor] = MICROS_USER_VIRTUAL_BASE;
        fixture->permissions[actor][0] = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;
        fixture->permissions[actor][1] = fixture->permissions[actor][0];
        for (size_t byte = 0; byte < GRANT01_MEMORY_SIZE; ++byte) {
            fixture->memory[actor][byte] = (unsigned char)(actor * 67 + byte * 13 + 19);
        }
    }
    if ((is_from(test) || is_to(test)) && test->operation != RUNTIME_SAME_VA) {
        fixture->grants.grants[0].base = MICROS_USER_VIRTUAL_BASE + 4088;
        fixture->grants.grants[0].length = 32;
    } else if (test->operation == RUNTIME_DEVICE) {
        fixture->grants.grants[0].grantor = fixture->processes[3];
        fixture->grants.grants[0].grantor_endpoint = fixture->identities[3];
        fixture->grants.grants[0].base = MICROS_TTY_UART_VIRTUAL_BASE;
    } else if (test->operation == RUNTIME_SAME_VA) {
        fixture->grants.grants[0].base = MICROS_TTY_UART_VIRTUAL_BASE + 32;
        fixture->memory_base[0] = MICROS_TTY_UART_VIRTUAL_BASE;
        fixture->memory_base[1] = MICROS_TTY_UART_VIRTUAL_BASE;
    }
    REQUIRE(micros_grant_registry_validate(&fixture->grants, &fixture->endpoints, &fixture->objects)
            == MICROS_GRANT_OK);
    if (test->operation != RUNTIME_UNAVAILABLE) {
        REQUIRE(runtime_publish());
    }
    memcpy(fixture->before_memory, fixture->memory, sizeof(fixture->memory));
    REQUIRE(runtime_condition(test));
    if (test->operation != RUNTIME_UNAVAILABLE) {
        *micros_ipc_runtime_authoritative_registry() = fixture->endpoints;
        *micros_grant_runtime_authoritative_registry() = fixture->grants;
    }
    grant01_snapshot();
    fixture->before_tty = *tty_handoff_context();
    memcpy(fixture->before_memory, fixture->memory, sizeof(fixture->memory));
    return true;
}

static bool captured_call(const struct runtime_case *test, enum micros_grant_error *result, uint64_t *value)
{
    struct grant01_fixture *fixture = grant01_fixture();
    struct micros_syscall_arguments arguments = {.a7 = MICROS_SYSCALL_ABI_GRANT_REVOKE, .a0 = 64};
    size_t caller = test->operation == RUNTIME_CREATE_DEVICE ? 3
        : test->operation >= RUNTIME_SYSCALL_FROM ? 1 : 0;
    const char *expected_panic = test->operation == RUNTIME_UNAVAILABLE || test->condition == BAD_READY_HEADER
        ? "grant-runtime-invariant" : test->condition == BAD_SELECTED_GRANT ? "grant-portable-invariant"
        : test->condition == BAD_TTY_ROOT ? "grant-device-authority-invariant" : NULL;
    struct micros_syscall_context context = {
        .objects = &fixture->objects, .hart = fixture->hart,
        .current = fixture->threads[caller], .process = fixture->processes[caller],
    };
    struct micros_trap_frame expected;

    if (test->operation == RUNTIME_SHAPE) {
        arguments.a1 = 1;
    } else if (test->operation == RUNTIME_CREATE || test->operation == RUNTIME_CREATE_DEVICE) {
        arguments = (struct micros_syscall_arguments){
            .a7 = MICROS_SYSCALL_ABI_GRANT_CREATE, .a0 = fixture->identities[1],
            .a1 = caller == 3 ? MICROS_TTY_UART_VIRTUAL_BASE : MICROS_USER_VIRTUAL_BASE,
            .a2 = 64, .a3 = MICROS_GRANT_PERMISSION_DEFINED_MASK,
        };
    } else if (test->operation >= RUNTIME_SYSCALL_FROM) {
        arguments = (struct micros_syscall_arguments){
            .a7 = test->operation == RUNTIME_SYSCALL_FROM ? MICROS_SYSCALL_ABI_GRANT_COPY_FROM
                : test->operation == RUNTIME_SYSCALL_TO ? MICROS_SYSCALL_ABI_GRANT_COPY_TO
                : MICROS_SYSCALL_ABI_GRANT_VALIDATE,
            .a0 = fixture->identities[0], .a1 = 64,
            .a3 = test->operation == RUNTIME_SYSCALL_VALIDATE ? 32 : MICROS_USER_VIRTUAL_BASE + 4090,
            .a4 = test->operation == RUNTIME_SYSCALL_VALIDATE ? MICROS_GRANT_PERMISSION_READ : 32,
        };
    }
    memset(&fixture->frame, 0x6b, sizeof(fixture->frame));
    fixture->frame.sepc = MICROS_USER_VIRTUAL_BASE + 4;
    fixture->frame.a0 = arguments.a0;
    fixture->frame.a1 = arguments.a1;
    fixture->frame.a2 = arguments.a2;
    fixture->frame.a3 = arguments.a3;
    fixture->frame.a4 = arguments.a4;
    fixture->frame.a5 = arguments.a5;
    fixture->frame.a6 = arguments.a6;
    fixture->frame.a7 = arguments.a7;
    expected = fixture->frame;
    fixture->panic_armed = true;
    if (setjmp(fixture->panic_jump) == 0) {
        enum micros_syscall_return returned = micros_grant_handle_captured_user_ecall(
            &fixture->objects.harts[fixture->hart.slot], &fixture->frame, &context, &arguments
        );
        REQUIRE(returned == MICROS_SYSCALL_RETURN_NORMAL);
        *result = MICROS_GRANT_OK;
        *value = fixture->frame.a0;
        expected.a0 = test->value;
        REQUIRE(fixture->panic_reason == NULL && expected_panic == NULL);
    } else {
        REQUIRE(expected_panic != NULL && strcmp(fixture->panic_reason, expected_panic) == 0);
        *result = MICROS_GRANT_ERROR_INVARIANT;
        *value = 0;
    }
    fixture->panic_armed = false;
    REQUIRE(memcmp(&expected, &fixture->frame, sizeof(expected)) == 0);
    if (test->operation != RUNTIME_UNAVAILABLE) {
        fixture->grants = *micros_grant_runtime_registry();
        fixture->endpoints = *micros_ipc_runtime_registry();
    }
    return true;
}

static bool memory_matches(const struct runtime_case *test)
{
    const struct grant01_fixture *fixture = grant01_fixture();
    bool from = is_from(test), to = is_to(test);
    bool copied = test->result == MICROS_GRANT_OK && request_length(test) != 0;
    size_t source = from ? 0 : 1, destination = from ? 1 : 0;
    size_t source_offset = test->operation == RUNTIME_SAME_VA ? 32 : from ? 4088 : 4090;
    size_t destination_offset = test->operation == RUNTIME_SAME_VA ? 128 : from ? 4090 : 4088;

    for (size_t actor = 0; actor < GRANT01_ACTORS; ++actor) {
        for (size_t byte = 0; byte < GRANT01_MEMORY_SIZE; ++byte) {
            unsigned char expected = fixture->before_memory[actor][byte];

            if (copied && (from || to) && actor == destination
                && byte >= destination_offset && byte - destination_offset < 32) {
                expected = fixture->before_memory[source][source_offset + byte - destination_offset];
            }
            REQUIRE(fixture->memory[actor][byte] == expected);
        }
    }
    REQUIRE(memcmp(&fixture->before_tty, tty_handoff_context(), sizeof(fixture->before_tty)) == 0);
    return true;
}

static bool translations_match(const struct runtime_case *test)
{
    const struct grant01_fixture *fixture = grant01_fixture();
    size_t grantor = 0, grantee = 0, tty = 0, device = 0;

    if (test->result == MICROS_GRANT_OK && request_length(test) != 0) {
        if (is_from(test) || is_to(test)) {
            grantor = grantee = test->operation == RUNTIME_SAME_VA ? 1 : 2;
        } else if (test->operation == RUNTIME_VALIDATE || test->operation == RUNTIME_SYSCALL_VALIDATE) {
            grantor = 1;
        }
    }
    if (test->condition == REMOTE_FAULT_LOCAL_INVARIANT || test->condition == REMOTE_INVARIANT_LOCAL_FAULT) {
        grantor = grantee = 1;
    }
    if (test->operation == RUNTIME_DEVICE) {
        grantee = device = 1;
        tty = test->condition == DEVICE_TAIL_INVARIANT ? 1 : 0;
    }
    REQUIRE(fixture->translations[0] == grantor && fixture->translations[1] == grantee
            && fixture->translations[2] == 0 && fixture->translations[3] == tty
            && fixture->device_checks == device);
    return true;
}

static bool run_runtime(size_t index, const struct runtime_case *test)
{
    struct grant01_fixture *fixture = grant01_fixture();
    enum micros_grant_error result;
    uint64_t value = 0;
    bool captured = is_captured(test);
    bool created = test->operation == RUNTIME_CREATE && test->result == MICROS_GRANT_OK;
    bool revoked = test->operation == RUNTIME_REVOKE && test->result == MICROS_GRANT_OK;
    bool preserved = !created && !revoked;
    size_t length = request_length(test);

    REQUIRE(runtime_setup(test));
    fixture->irq_status = captured ? 0 : MICROS_RISCV_SSTATUS_SIE;
    REQUIRE(grant01_begin(index));
    if (captured) {
        REQUIRE(captured_call(test, &result, &value));
    } else if (test->operation == RUNTIME_VALIDATE) {
        result = micros_grant_validate_range(
            &fixture->grants, &fixture->endpoints, &fixture->objects, fixture->processes[1],
            fixture->identities[0], 127, 8, 32, MICROS_GRANT_PERMISSION_READ
        );
    } else if (test->operation == RUNTIME_COPY_TO) {
        result = micros_grant_copy_to(
            &fixture->grants, &fixture->endpoints, &fixture->objects, fixture->processes[1],
            fixture->identities[0], 64, 0, MICROS_USER_VIRTUAL_BASE + 4090, 32
        );
        value = result == MICROS_GRANT_OK ? length : 0;
    } else {
        result = micros_grant_copy_from(
            &fixture->grants, &fixture->endpoints, &fixture->objects, fixture->processes[1],
            fixture->identities[test->operation == RUNTIME_DEVICE ? 3 : 0], 64,
            test->condition == RANGE_BAD_PHASE ? 65 : length == 0 ? 32 : 0,
            length == 0 ? UINTPTR_MAX : test->operation == RUNTIME_SAME_VA ? MICROS_TTY_UART_VIRTUAL_BASE + 128
                : test->operation == RUNTIME_DEVICE ? MICROS_USER_VIRTUAL_BASE + 128
                : MICROS_USER_VIRTUAL_BASE + 4090, length
        );
        value = result == MICROS_GRANT_OK ? length : 0;
    }
    REQUIRE(grant01_finish());
    REQUIRE(result == test->result && value == test->value);
    REQUIRE(fixture->irq_status == (captured ? 0 : MICROS_RISCV_SSTATUS_SIE));
    REQUIRE(memory_matches(test));
    if (created) {
        REQUIRE(fixture->grants.active_count == 1 && fixture->grants.grants[0].generation == 1
                && fixture->grants.grants[0].grantor_endpoint == fixture->identities[0]
                && fixture->grants.grants[0].grantee_endpoint == fixture->identities[1]
                && fixture->grants.grants[0].base == MICROS_USER_VIRTUAL_BASE
                && fixture->grants.grants[0].length == 64
                && fixture->grants.grants[0].permissions == MICROS_GRANT_PERMISSION_DEFINED_MASK);
    } else if (revoked) {
        REQUIRE(fixture->grants.active_count == 0 && fixture->grants.grants[0].generation == 2);
    }
    REQUIRE(micros_grant_registry_validate(&fixture->grants, &fixture->endpoints, &fixture->objects)
            == (test->condition == BAD_READY_HEADER || test->condition == UNRELATED_BAD_GRANT
                || test->condition == BAD_SELECTED_GRANT ? MICROS_GRANT_ERROR_INVARIANT : MICROS_GRANT_OK));
    if (test->condition == UNUSED_BOOTSTRAP_POLICY) {
        REQUIRE(micros_tty_handoff_runtime_validate(
            &fixture->bootstrap, &fixture->endpoints, &fixture->objects
        ) == MICROS_TTY_HANDOFF_ERROR_INVARIANT);
    }
    REQUIRE(translations_match(test));
    return grant01_report(index, test->name, result, value, preserved);
}

bool grant01_runtime_cases(size_t *count)
{
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        REQUIRE(run_runtime(*count, &cases[index]));
        ++*count;
    }
    return true;
}
