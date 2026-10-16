#include "tty_handoff_test_internal.h"

bool tty_handoff_test_runtime_lifecycle_validation(void)
{
    struct tty_handoff_test_fixture *fixture = tty_handoff_test_fixture();

    const struct micros_process_handle process = {3, 7};
    const struct micros_thread_handle thread = {5, 9};
    const micros_endpoint_t endpoint =
        (process.generation << MICROS_ENDPOINT_SLOT_BITS)
        | process.slot;
    const uint64_t root = UINT64_C(0x80200000);
    struct micros_bootstrap_control_state bootstrap = {0};
    struct micros_bootstrap_binding *binding =
        &bootstrap.bindings[0];
    struct micros_bootstrap_manifest_entry *entry =
        &bootstrap.manifest.entries[0];
    struct micros_bootstrap_runtime_entry *transition =
        &bootstrap.transitions.entries[0];
    struct micros_endpoint_registry registry = {0};
    struct micros_kernel_objects objects = {0};
    struct micros_syscall_context context = {
        .objects = &objects,
        .current = thread,
        .process = process,
    };
    struct micros_tty_handoff mapped;
    struct micros_tty_handoff begin;
    struct micros_tty_handoff released;
    struct micros_tty_handoff committed;
    struct micros_tty_handoff claimed;
    struct micros_tty_handoff completed;
    struct micros_tty_handoff ready;
    struct micros_tty_device_authority authority;
    struct micros_tty_device_authority authority_sentinel;
    struct micros_tty_handoff_runtime_state unexpected_state = {
        .process = {4, 7},
        .thread = {6, 9},
    };
    struct micros_tty_handoff_runtime_state *runtime;

    *binding = (struct micros_bootstrap_binding){
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
    *entry = (struct micros_bootstrap_manifest_entry){
        .service_id = MICROS_TTY_SERVICE_ID,
        .image_id = 104,
        .process_slot = MICROS_TTY_PROCESS_SLOT,
        .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
        .role_flags = MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER,
        .device_base = MICROS_TTY_UART_PHYSICAL_BASE,
        .device_length = MICROS_TTY_UART_MAPPED_LENGTH,
        .irq_source = MICROS_TTY_UART_IRQ_SOURCE,
    };
    bootstrap.entry_count = 1;
    bootstrap.plan.console_service_id = MICROS_TTY_SERVICE_ID;
    bootstrap.transitions.entry_count = 1;
    *transition = (struct micros_bootstrap_runtime_entry){
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

    bootstrap.plan.console_service_id = 0;
    EXPECT_TRUE(
        micros_tty_control_syscall_authority_resolve(
            NULL,
            &bootstrap,
            &registry,
            &objects,
            &context
        ) == MICROS_TTY_CONTROL_AUTHORITY_UNAUTHORIZED
    );
    EXPECT_TRUE(
        micros_tty_control_syscall_authority_resolve(
            &unexpected_state,
            &bootstrap,
            &registry,
            &objects,
            &context
        ) == MICROS_TTY_CONTROL_AUTHORITY_INVARIANT
    );
    bootstrap.plan.console_service_id = MICROS_TTY_SERVICE_ID;
    EXPECT_TRUE(
        micros_tty_control_syscall_authority_resolve(
            NULL,
            &bootstrap,
            &registry,
            &objects,
            &context
        ) == MICROS_TTY_CONTROL_AUTHORITY_INVARIANT
    );

    EXPECT_TRUE(
        micros_tty_handoff_runtime_reset()
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_prepare(binding, entry)
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_bind_bootstrap(&bootstrap)
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_validate(
            &bootstrap,
            &registry,
            &objects
        ) == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_device_authority(
            &registry,
            &objects,
            &authority
        ) == MICROS_TTY_DEVICE_AUTHORITY_NONE
    );
    fixture->runtime_process.privilege_profile =
        MICROS_PRIVILEGE_PROFILE_TTY;
    EXPECT_TRUE(
        micros_tty_handoff_runtime_validate(
            &bootstrap,
            &registry,
            &objects
        ) == MICROS_TTY_HANDOFF_ERROR_INVARIANT
    );
    fixture->runtime_process.privilege_profile = 0;

    runtime = micros_tty_handoff_runtime_authoritative_state();
    EXPECT_TRUE(
        runtime != NULL
        && micros_tty_handoff_runtime_prepare_begin(
            100,
            50,
            &begin
        ) == MICROS_TTY_HANDOFF_OK
        && runtime->handoff.console_phase
            == MICROS_TTY_CONSOLE_EARLY
        && !runtime->handoff.deadline_armed
    );
    micros_tty_handoff_runtime_commit_begin_deadline_prevalidated(
        &begin
    );
    EXPECT_TRUE(
        runtime->handoff.console_phase
            == MICROS_TTY_CONSOLE_EARLY
        && runtime->handoff.deadline_armed
        && runtime->handoff.deadline == 150
    );
    micros_tty_handoff_runtime_commit_begin_phase_prevalidated(
        &begin
    );
    EXPECT_TRUE(
        runtime->handoff.console_phase
            == MICROS_TTY_CONSOLE_MAP_REQUESTED
        && micros_tty_handoff_runtime_validate(
            &bootstrap,
            &registry,
            &objects
        ) == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_prepare_mapped(&mapped)
            == MICROS_TTY_HANDOFF_OK
    );
    micros_tty_handoff_runtime_commit_mapped_prevalidated(&mapped);
    EXPECT_TRUE(
        micros_tty_handoff_runtime_validate(
            &bootstrap,
            &registry,
            &objects
        ) == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_device_authority(
            &registry,
            &objects,
            &authority
        ) == MICROS_TTY_DEVICE_AUTHORITY_ACTIVE
        && authority.process.slot == process.slot
        && authority.process.generation == process.generation
        && authority.root_physical_address == root
        && authority.console_phase == MICROS_TTY_CONSOLE_MAPPED
    );
    memset(&authority_sentinel, 0xa5, sizeof(authority_sentinel));
    authority = authority_sentinel;
    fixture->runtime_endpoint.state = MICROS_ENDPOINT_STATE_ACTIVE;
    EXPECT_TRUE(
        micros_tty_handoff_runtime_device_authority(
            &registry,
            &objects,
            &authority
        ) == MICROS_TTY_DEVICE_AUTHORITY_INVARIANT
        && memcmp(
            &authority,
            &authority_sentinel,
            sizeof(authority)
        ) == 0
    );
    fixture->runtime_endpoint.state = MICROS_ENDPOINT_STATE_RESERVED;

    EXPECT_TRUE(
        micros_tty_handoff_runtime_prepare_release(&released)
            == MICROS_TTY_HANDOFF_OK
        && runtime->handoff.console_phase
            == MICROS_TTY_CONSOLE_MAPPED
        && released.console_phase
            == MICROS_TTY_CONSOLE_STARTING
        && released.deadline == 150
    );
    transition->state = MICROS_BOOTSTRAP_SERVICE_STARTING;
    transition->endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
    transition->profile_installed = true;
    transition->scheduler_assigned = true;
    transition->ready_deadline = 150;
    fixture->runtime_process.privilege_profile =
        MICROS_PRIVILEGE_PROFILE_TTY;
    fixture->runtime_endpoint.state = MICROS_ENDPOINT_STATE_ACTIVE;
    fixture->runtime_thread.runtime_flags = 0;
    fixture->runtime_thread.scheduler_assigned = true;
    fixture->runtime_thread.scheduler_priority = binding->scheduler_priority;
    fixture->runtime_thread.scheduler_preemptible =
        binding->scheduler_preemptible;
    fixture->runtime_thread.quantum_counter_ticks =
        binding->scheduler_quantum_counter_ticks;
    micros_tty_handoff_runtime_commit_release_prevalidated(&released);
    EXPECT_TRUE(
        micros_tty_handoff_runtime_validate(
            &bootstrap,
            &registry,
            &objects
        ) == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_prepare_commit(&committed)
            == MICROS_TTY_HANDOFF_OK
        && runtime->handoff.console_phase
            == MICROS_TTY_CONSOLE_STARTING
        && committed.console_phase == MICROS_TTY_CONSOLE_OWNED
        && committed.route_phase == MICROS_TTY_ROUTE_IDLE
    );
    micros_tty_handoff_runtime_commit_console_prevalidated(
        &committed
    );
    EXPECT_TRUE(
        micros_tty_handoff_runtime_prepare_claim(
            9,
            &claimed
        ) == MICROS_TTY_HANDOFF_ERROR_ARGUMENT
        && runtime->handoff.route_phase == MICROS_TTY_ROUTE_IDLE
        && micros_tty_handoff_runtime_prepare_claim(
            MICROS_TTY_UART_IRQ_SOURCE,
            &claimed
        ) == MICROS_TTY_HANDOFF_OK
        && runtime->handoff.route_phase == MICROS_TTY_ROUTE_IDLE
        && claimed.route_phase == MICROS_TTY_ROUTE_IN_SERVICE
        && claimed.claimed_source == MICROS_TTY_UART_IRQ_SOURCE
    );
    micros_tty_handoff_runtime_commit_claim_prevalidated(&claimed);
    EXPECT_TRUE(
        runtime->handoff.route_phase == MICROS_TTY_ROUTE_IN_SERVICE
        && micros_tty_handoff_runtime_prepare_claim(
            MICROS_TTY_UART_IRQ_SOURCE,
            &claimed
        ) == MICROS_TTY_HANDOFF_ERROR_STATE
        && micros_tty_handoff_runtime_prepare_complete(
            9,
            &completed
        ) == MICROS_TTY_HANDOFF_ERROR_ARGUMENT
        && runtime->handoff.route_phase
            == MICROS_TTY_ROUTE_IN_SERVICE
        && micros_tty_handoff_runtime_role_ready(
            &registry,
            &objects
        )
        && micros_tty_handoff_runtime_prepare_ready(149, &ready)
            == MICROS_TTY_HANDOFF_OK
        && ready.console_phase == MICROS_TTY_CONSOLE_OWNED
        && ready.route_phase == MICROS_TTY_ROUTE_IN_SERVICE
        && ready.claimed_source == MICROS_TTY_UART_IRQ_SOURCE
        && !ready.deadline_armed
        && ready.deadline == 0
        && runtime->handoff.route_phase
            == MICROS_TTY_ROUTE_IN_SERVICE
    );
    transition->state = MICROS_BOOTSTRAP_SERVICE_READY;
    transition->ready_deadline = 0;
    micros_tty_handoff_runtime_commit_ready_prevalidated(&ready);
    EXPECT_TRUE(
        micros_tty_handoff_runtime_validate(
            &bootstrap,
            &registry,
            &objects
        ) == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_prepare_complete(
            MICROS_TTY_UART_IRQ_SOURCE,
            &completed
        ) == MICROS_TTY_HANDOFF_OK
        && runtime->handoff.route_phase
            == MICROS_TTY_ROUTE_IN_SERVICE
        && completed.route_phase == MICROS_TTY_ROUTE_IDLE
        && completed.claimed_source == 0
    );
    micros_tty_handoff_runtime_commit_complete_prevalidated(
        &completed
    );
    EXPECT_TRUE(
        micros_tty_handoff_runtime_validate(
            &bootstrap,
            &registry,
            &objects
        ) == MICROS_TTY_HANDOFF_OK
    );
    transition->endpoint_state =
        MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY;
    fixture->runtime_endpoint.state = MICROS_ENDPOINT_STATE_SOURCE_ONLY;
    EXPECT_TRUE(
        micros_tty_handoff_runtime_validate(
            &bootstrap,
            &registry,
            &objects
        ) == MICROS_TTY_HANDOFF_ERROR_INVARIANT
        && micros_tty_handoff_runtime_device_authority(
            &registry,
            &objects,
            &authority
        ) == MICROS_TTY_DEVICE_AUTHORITY_INVARIANT
    );
    fixture->runtime_endpoint.state = MICROS_ENDPOINT_STATE_ACTIVE;
    EXPECT_TRUE(
        micros_tty_handoff_runtime_panic()
            == MICROS_TTY_HANDOFF_OK
        && runtime->handoff.console_phase
            == MICROS_TTY_CONSOLE_PANIC
        && runtime->handoff.route_phase == MICROS_TTY_ROUTE_PANIC
        && runtime->handoff.claimed_source == 0
        && !runtime->handoff.deadline_armed
        && micros_tty_handoff_runtime_panic()
            == MICROS_TTY_HANDOFF_OK
    );
    return true;
}
