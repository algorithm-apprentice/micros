#include "tty_handoff_test_internal.h"

struct tty_handoff_runtime_case {
    struct micros_bootstrap_control_state bootstrap;
    struct micros_bootstrap_binding *binding;
    struct micros_bootstrap_manifest_entry *entry;
    struct micros_bootstrap_runtime_entry *transition;
    struct micros_endpoint_registry registry;
    struct micros_kernel_objects objects;
    struct micros_syscall_context context;
    struct micros_tty_handoff mapped;
    struct micros_tty_handoff begin;
    struct micros_tty_handoff released;
    struct micros_tty_handoff committed;
    struct micros_tty_handoff claimed;
    struct micros_tty_handoff completed;
    struct micros_tty_handoff ready;
    struct micros_tty_device_authority authority;
    struct micros_tty_device_authority authority_sentinel;
    struct micros_tty_handoff_runtime_state unexpected_state;
    struct micros_tty_handoff_runtime_state *runtime;
};

static void tty_handoff_test_seed_runtime_records(
    struct tty_handoff_test_fixture *fixture,
    struct tty_handoff_runtime_case *work,
    const struct micros_process_handle process,
    const struct micros_thread_handle thread,
    const micros_endpoint_t endpoint,
    const uint64_t root
)
{
    *work->binding = (struct micros_bootstrap_binding){
        .manifest_index = 0,
        .service_id = MICROS_TTY_SERVICE_ID,
        .process = process,
        .thread = thread,
        .root = root,
        .endpoint = endpoint,
        .scheduler_priority = 8,
        .scheduler_preemptible = true,
        .scheduler_quantum_counter_ticks = 100,
    };
    *work->entry = (struct micros_bootstrap_manifest_entry){
        .service_id = MICROS_TTY_SERVICE_ID,
        .image_id = 104,
        .process_slot = MICROS_TTY_PROCESS_SLOT,
        .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
        .role_flags = MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER,
        .device_base = MICROS_TTY_UART_PHYSICAL_BASE,
        .device_length = MICROS_TTY_UART_MAPPED_LENGTH,
        .irq_source = MICROS_TTY_UART_IRQ_SOURCE,
    };
    work->bootstrap.entry_count = 1;
    work->bootstrap.plan.console_service_id = MICROS_TTY_SERVICE_ID;
    work->bootstrap.transitions.entry_count = 1;
    *work->transition = (struct micros_bootstrap_runtime_entry){
        .service_id = MICROS_TTY_SERVICE_ID,
        .state = MICROS_BOOTSTRAP_SERVICE_PREPARED,
        .endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_RESERVED,
    };
    fixture->runtime_process_handle = process;
    fixture->runtime_thread_handle = thread;
    fixture->runtime_process = (struct micros_process){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = process.generation,
        .address_space_root = root,
        .primary_endpoint = endpoint,
    };
    fixture->runtime_thread = (struct micros_thread){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = thread.generation,
        .owner = process,
        .context_attached = true,
        .runtime_flags = MICROS_THREAD_RTS_INACTIVE,
    };
    fixture->runtime_endpoint = (struct micros_endpoint_record){
        .state = MICROS_ENDPOINT_STATE_RESERVED,
        .owner = process,
        .value = endpoint,
    };

}

static bool tty_handoff_test_check_initial_runtime_authority(
    struct tty_handoff_test_fixture *fixture,
    struct tty_handoff_runtime_case *work
)
{
    work->bootstrap.plan.console_service_id = 0;
    EXPECT_TRUE(
        micros_tty_control_syscall_authority_resolve(
            NULL,
            &work->bootstrap,
            &work->registry,
            &work->objects,
            &work->context
        ) == MICROS_TTY_CONTROL_AUTHORITY_UNAUTHORIZED
    );
    EXPECT_TRUE(
        micros_tty_control_syscall_authority_resolve(
            &work->unexpected_state,
            &work->bootstrap,
            &work->registry,
            &work->objects,
            &work->context
        ) == MICROS_TTY_CONTROL_AUTHORITY_INVARIANT
    );
    work->bootstrap.plan.console_service_id = MICROS_TTY_SERVICE_ID;
    EXPECT_TRUE(
        micros_tty_control_syscall_authority_resolve(
            NULL,
            &work->bootstrap,
            &work->registry,
            &work->objects,
            &work->context
        ) == MICROS_TTY_CONTROL_AUTHORITY_INVARIANT
    );

    EXPECT_TRUE(
        micros_tty_handoff_runtime_reset()
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_prepare(work->binding, work->entry)
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_bind_bootstrap(&work->bootstrap)
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_validate(
            &work->bootstrap,
            &work->registry,
            &work->objects
        ) == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_device_authority(
            &work->registry,
            &work->objects,
            &work->authority
        ) == MICROS_TTY_DEVICE_AUTHORITY_NONE
    );
    fixture->runtime_process.privilege_profile =
        MICROS_PRIVILEGE_PROFILE_TTY;
    EXPECT_TRUE(
        micros_tty_handoff_runtime_validate(
            &work->bootstrap,
            &work->registry,
            &work->objects
        ) == MICROS_TTY_HANDOFF_ERROR_INVARIANT
    );
    fixture->runtime_process.privilege_profile = 0;
    return true;
}

static bool tty_handoff_test_begin_and_map_runtime(
    struct tty_handoff_test_fixture *fixture,
    struct tty_handoff_runtime_case *work,
    const struct micros_process_handle process,
    const uint64_t root
)
{
    work->runtime = micros_tty_handoff_runtime_authoritative_state();
    EXPECT_TRUE(
        work->runtime != NULL
        && micros_tty_handoff_runtime_prepare_begin(
            100,
            50,
            &work->begin
        ) == MICROS_TTY_HANDOFF_OK
        && work->runtime->handoff.console_phase
            == MICROS_TTY_CONSOLE_EARLY
        && !work->runtime->handoff.deadline_armed
    );
    micros_tty_handoff_runtime_commit_begin_deadline_prevalidated(
        &work->begin
    );
    EXPECT_TRUE(
        work->runtime->handoff.console_phase
            == MICROS_TTY_CONSOLE_EARLY
        && work->runtime->handoff.deadline_armed
        && work->runtime->handoff.deadline == 150
    );
    micros_tty_handoff_runtime_commit_begin_phase_prevalidated(
        &work->begin
    );
    EXPECT_TRUE(
        work->runtime->handoff.console_phase
            == MICROS_TTY_CONSOLE_MAP_REQUESTED
        && micros_tty_handoff_runtime_validate(
            &work->bootstrap,
            &work->registry,
            &work->objects
        ) == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_prepare_mapped(&work->mapped)
            == MICROS_TTY_HANDOFF_OK
    );
    micros_tty_handoff_runtime_commit_mapped_prevalidated(&work->mapped);
    EXPECT_TRUE(
        micros_tty_handoff_runtime_validate(
            &work->bootstrap,
            &work->registry,
            &work->objects
        ) == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_device_authority(
            &work->registry,
            &work->objects,
            &work->authority
        ) == MICROS_TTY_DEVICE_AUTHORITY_ACTIVE
        && work->authority.process.slot == process.slot
        && work->authority.process.generation == process.generation
        && work->authority.root_physical_address == root
        && work->authority.console_phase == MICROS_TTY_CONSOLE_MAPPED
    );
    memset(&work->authority_sentinel, 0xa5, sizeof(work->authority_sentinel));
    work->authority = work->authority_sentinel;
    fixture->runtime_endpoint.state = MICROS_ENDPOINT_STATE_ACTIVE;
    EXPECT_TRUE(
        micros_tty_handoff_runtime_device_authority(
            &work->registry,
            &work->objects,
            &work->authority
        ) == MICROS_TTY_DEVICE_AUTHORITY_INVARIANT
        && memcmp(
            &work->authority,
            &work->authority_sentinel,
            sizeof(work->authority)
        ) == 0
    );
    fixture->runtime_endpoint.state = MICROS_ENDPOINT_STATE_RESERVED;
    return true;
}

static bool tty_handoff_test_release_and_commit_runtime(
    struct tty_handoff_test_fixture *fixture,
    struct tty_handoff_runtime_case *work
)
{
    EXPECT_TRUE(
        micros_tty_handoff_runtime_prepare_release(&work->released)
            == MICROS_TTY_HANDOFF_OK
        && work->runtime->handoff.console_phase
            == MICROS_TTY_CONSOLE_MAPPED
        && work->released.console_phase
            == MICROS_TTY_CONSOLE_STARTING
        && work->released.deadline == 150
    );
    work->transition->state = MICROS_BOOTSTRAP_SERVICE_STARTING;
    work->transition->endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
    work->transition->profile_installed = true;
    work->transition->scheduler_assigned = true;
    work->transition->ready_deadline = 150;
    fixture->runtime_process.privilege_profile =
        MICROS_PRIVILEGE_PROFILE_TTY;
    fixture->runtime_endpoint.state = MICROS_ENDPOINT_STATE_ACTIVE;
    fixture->runtime_thread.runtime_flags = 0;
    fixture->runtime_thread.scheduler_assigned = true;
    fixture->runtime_thread.scheduler_priority = work->binding->scheduler_priority;
    fixture->runtime_thread.scheduler_preemptible =
        work->binding->scheduler_preemptible;
    fixture->runtime_thread.quantum_counter_ticks =
        work->binding->scheduler_quantum_counter_ticks;
    micros_tty_handoff_runtime_commit_release_prevalidated(&work->released);
    EXPECT_TRUE(
        micros_tty_handoff_runtime_validate(
            &work->bootstrap,
            &work->registry,
            &work->objects
        ) == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_prepare_commit(&work->committed)
            == MICROS_TTY_HANDOFF_OK
        && work->runtime->handoff.console_phase
            == MICROS_TTY_CONSOLE_STARTING
        && work->committed.console_phase == MICROS_TTY_CONSOLE_OWNED
        && work->committed.route_phase == MICROS_TTY_ROUTE_IDLE
    );
    micros_tty_handoff_runtime_commit_console_prevalidated(
        &work->committed
    );
    return true;
}

static bool tty_handoff_test_claim_and_ready_runtime(
    struct tty_handoff_runtime_case *work
)
{
    EXPECT_TRUE(
        micros_tty_handoff_runtime_prepare_claim(
            9,
            &work->claimed
        ) == MICROS_TTY_HANDOFF_ERROR_ARGUMENT
        && work->runtime->handoff.route_phase == MICROS_TTY_ROUTE_IDLE
        && micros_tty_handoff_runtime_prepare_claim(
            MICROS_TTY_UART_IRQ_SOURCE,
            &work->claimed
        ) == MICROS_TTY_HANDOFF_OK
        && work->runtime->handoff.route_phase == MICROS_TTY_ROUTE_IDLE
        && work->claimed.route_phase == MICROS_TTY_ROUTE_IN_SERVICE
        && work->claimed.claimed_source == MICROS_TTY_UART_IRQ_SOURCE
    );
    micros_tty_handoff_runtime_commit_claim_prevalidated(&work->claimed);
    EXPECT_TRUE(
        work->runtime->handoff.route_phase == MICROS_TTY_ROUTE_IN_SERVICE
        && micros_tty_handoff_runtime_prepare_claim(
            MICROS_TTY_UART_IRQ_SOURCE,
            &work->claimed
        ) == MICROS_TTY_HANDOFF_ERROR_STATE
        && micros_tty_handoff_runtime_prepare_complete(
            9,
            &work->completed
        ) == MICROS_TTY_HANDOFF_ERROR_ARGUMENT
        && work->runtime->handoff.route_phase
            == MICROS_TTY_ROUTE_IN_SERVICE
        && micros_tty_handoff_runtime_role_ready(
            &work->registry,
            &work->objects
        )
        && micros_tty_handoff_runtime_prepare_ready(149, &work->ready)
            == MICROS_TTY_HANDOFF_OK
        && work->ready.console_phase == MICROS_TTY_CONSOLE_OWNED
        && work->ready.route_phase == MICROS_TTY_ROUTE_IN_SERVICE
        && work->ready.claimed_source == MICROS_TTY_UART_IRQ_SOURCE
        && !work->ready.deadline_armed
        && work->ready.deadline == 0
        && work->runtime->handoff.route_phase
            == MICROS_TTY_ROUTE_IN_SERVICE
    );
    work->transition->state = MICROS_BOOTSTRAP_SERVICE_READY;
    work->transition->ready_deadline = 0;
    micros_tty_handoff_runtime_commit_ready_prevalidated(&work->ready);
    return true;
}

static bool tty_handoff_test_complete_and_panic_runtime(
    struct tty_handoff_test_fixture *fixture,
    struct tty_handoff_runtime_case *work
)
{
    EXPECT_TRUE(
        micros_tty_handoff_runtime_validate(
            &work->bootstrap,
            &work->registry,
            &work->objects
        ) == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_prepare_complete(
            MICROS_TTY_UART_IRQ_SOURCE,
            &work->completed
        ) == MICROS_TTY_HANDOFF_OK
        && work->runtime->handoff.route_phase
            == MICROS_TTY_ROUTE_IN_SERVICE
        && work->completed.route_phase == MICROS_TTY_ROUTE_IDLE
        && work->completed.claimed_source == 0
    );
    micros_tty_handoff_runtime_commit_complete_prevalidated(
        &work->completed
    );
    EXPECT_TRUE(
        micros_tty_handoff_runtime_validate(
            &work->bootstrap,
            &work->registry,
            &work->objects
        ) == MICROS_TTY_HANDOFF_OK
    );
    work->transition->endpoint_state =
        MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY;
    fixture->runtime_endpoint.state = MICROS_ENDPOINT_STATE_SOURCE_ONLY;
    EXPECT_TRUE(
        micros_tty_handoff_runtime_validate(
            &work->bootstrap,
            &work->registry,
            &work->objects
        ) == MICROS_TTY_HANDOFF_ERROR_INVARIANT
        && micros_tty_handoff_runtime_device_authority(
            &work->registry,
            &work->objects,
            &work->authority
        ) == MICROS_TTY_DEVICE_AUTHORITY_INVARIANT
    );
    fixture->runtime_endpoint.state = MICROS_ENDPOINT_STATE_ACTIVE;
    EXPECT_TRUE(
        micros_tty_handoff_runtime_panic()
            == MICROS_TTY_HANDOFF_OK
        && work->runtime->handoff.console_phase
            == MICROS_TTY_CONSOLE_PANIC
        && work->runtime->handoff.route_phase == MICROS_TTY_ROUTE_PANIC
        && work->runtime->handoff.claimed_source == 0
        && !work->runtime->handoff.deadline_armed
        && micros_tty_handoff_runtime_panic()
            == MICROS_TTY_HANDOFF_OK
    );
    return true;
}

bool tty_handoff_test_runtime_lifecycle_validation(void)
{
    struct tty_handoff_test_fixture *fixture = tty_handoff_test_fixture();

    const struct micros_process_handle process = {3, 7};
    const struct micros_thread_handle thread = {5, 9};
    const micros_endpoint_t endpoint =
        (process.generation << MICROS_ENDPOINT_SLOT_BITS)
        | process.slot;
    const uint64_t root = UINT64_C(0x80200000);
    struct tty_handoff_runtime_case work;

    work.bootstrap = (struct micros_bootstrap_control_state){0};
    work.binding = &work.bootstrap.bindings[0];
    work.entry = &work.bootstrap.manifest.entries[0];
    work.transition = &work.bootstrap.transitions.entries[0];
    work.registry = (struct micros_endpoint_registry){0};
    work.objects = (struct micros_kernel_objects){0};
    work.context = (struct micros_syscall_context){
        .objects = &work.objects,
        .current = thread,
        .process = process,
    };
    work.unexpected_state = (struct micros_tty_handoff_runtime_state){
        .process = {4, 7},
        .thread = {6, 9},
    };

    tty_handoff_test_seed_runtime_records(
        fixture,
        &work,
        process,
        thread,
        endpoint,
        root
    );
    if (!tty_handoff_test_check_initial_runtime_authority(fixture, &work)) {
        return false;
    }
    if (!tty_handoff_test_begin_and_map_runtime(fixture, &work, process, root)) {
        return false;
    }
    if (!tty_handoff_test_release_and_commit_runtime(fixture, &work)) {
        return false;
    }
    if (!tty_handoff_test_claim_and_ready_runtime(&work)) {
        return false;
    }
    return tty_handoff_test_complete_and_panic_runtime(fixture, &work);
}
