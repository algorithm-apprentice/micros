#include "kernel/tty_handoff_core.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/tty_control_syscall_core.h"
#include "kernel/uart_console_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/sv39.h"

bool micros_tty_handoff_test_run(void);

static struct micros_process runtime_process;
static struct micros_thread runtime_thread;
static struct micros_endpoint_record runtime_endpoint;
static struct micros_process_handle runtime_process_handle;
static struct micros_thread_handle runtime_thread_handle;

static bool advance_to_phase(
    enum micros_tty_console_phase phase,
    struct micros_tty_handoff *state
);

const struct micros_bootstrap_binding *
micros_bootstrap_control_find_binding(
    const struct micros_bootstrap_control_state *state,
    uint32_t service_id
)
{
    return micros_bootstrap_control_find_binding_bounded(
        state,
        service_id
    );
}

enum micros_kernel_object_error micros_process_resolve(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle handle,
    const struct micros_process **process
)
{
    if (
        objects == NULL
        || process == NULL
        || handle.slot != runtime_process_handle.slot
        || handle.generation != runtime_process_handle.generation
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STALE;
    }
    *process = &runtime_process;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_kernel_object_error micros_thread_resolve(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle handle,
    const struct micros_thread **thread
)
{
    if (
        objects == NULL
        || thread == NULL
        || handle.slot != runtime_thread_handle.slot
        || handle.generation != runtime_thread_handle.generation
    ) {
        return MICROS_KERNEL_OBJECT_ERROR_STALE;
    }
    *thread = &runtime_thread;
    return MICROS_KERNEL_OBJECT_OK;
}

enum micros_endpoint_error micros_endpoint_resolve_internal(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint,
    const struct micros_endpoint_record **record
)
{
    if (
        registry == NULL
        || objects == NULL
        || record == NULL
        || endpoint != runtime_endpoint.value
    ) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    *record = &runtime_endpoint;
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error
micros_privilege_profile_allows_kernel_operation(
    const struct micros_endpoint_registry *registry,
    uint8_t profile_id,
    uint8_t operation
)
{
    return (
        registry != NULL
        && profile_id == MICROS_PRIVILEGE_PROFILE_TTY
        && operation == 3
    )
        ? MICROS_ENDPOINT_OK
        : MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
}

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

static bool test_complete_handoff(void)
{
    struct micros_tty_handoff state;

    EXPECT_TRUE(
        micros_tty_handoff_initialize(&state)
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_validate(&state)
            == MICROS_TTY_HANDOFF_OK
        && state.console_phase == MICROS_TTY_CONSOLE_EARLY
        && state.route_phase == MICROS_TTY_ROUTE_DISABLED
        && !state.deadline_armed
    );
    EXPECT_TRUE(
        micros_tty_handoff_begin(&state, 100, 50)
            == MICROS_TTY_HANDOFF_OK
        && state.console_phase
            == MICROS_TTY_CONSOLE_MAP_REQUESTED
        && state.deadline_armed
        && state.deadline == 150
        && !micros_tty_handoff_deadline_expired(&state, 149)
        && micros_tty_handoff_deadline_expired(&state, 150)
        && micros_tty_handoff_mark_mapped(&state)
            == MICROS_TTY_HANDOFF_OK
        && state.console_phase == MICROS_TTY_CONSOLE_MAPPED
        && state.deadline == 150
        && micros_tty_handoff_release(&state)
            == MICROS_TTY_HANDOFF_OK
        && state.console_phase == MICROS_TTY_CONSOLE_STARTING
        && state.deadline == 150
        && micros_tty_handoff_commit(&state)
            == MICROS_TTY_HANDOFF_OK
        && state.console_phase == MICROS_TTY_CONSOLE_OWNED
        && state.route_phase == MICROS_TTY_ROUTE_IDLE
        && state.deadline == 150
        && micros_tty_handoff_claim(
            &state,
            MICROS_TTY_UART_IRQ_SOURCE
        ) == MICROS_TTY_HANDOFF_OK
        && state.route_phase == MICROS_TTY_ROUTE_IN_SERVICE
        && state.claimed_source == MICROS_TTY_UART_IRQ_SOURCE
        && micros_tty_handoff_complete(
            &state,
            MICROS_TTY_UART_IRQ_SOURCE
        ) == MICROS_TTY_HANDOFF_OK
        && state.route_phase == MICROS_TTY_ROUTE_IDLE
        && state.claimed_source == 0
        && micros_tty_handoff_accept_ready(&state, 149)
            == MICROS_TTY_HANDOFF_OK
        && !state.deadline_armed
        && state.deadline == 0
        && micros_tty_handoff_validate(&state)
            == MICROS_TTY_HANDOFF_OK
    );
    return true;
}

static bool test_deadline_covers_every_pre_ready_phase(void)
{
    const enum micros_tty_console_phase phases[] = {
        MICROS_TTY_CONSOLE_MAP_REQUESTED,
        MICROS_TTY_CONSOLE_MAPPED,
        MICROS_TTY_CONSOLE_STARTING,
        MICROS_TTY_CONSOLE_OWNED,
    };
    size_t index;

    for (index = 0; index < sizeof(phases) / sizeof(phases[0]); ++index) {
        struct micros_tty_handoff state;

        EXPECT_TRUE(
            advance_to_phase(phases[index], &state)
            && state.deadline_armed
            && state.deadline == 20
            && !micros_tty_handoff_deadline_expired(&state, 19)
            && micros_tty_handoff_deadline_expired(&state, 20)
            && micros_tty_handoff_deadline_expired(&state, 21)
        );
    }
    return true;
}

static bool test_ready_requires_idle_route(void)
{
    struct micros_tty_handoff state;
    struct micros_tty_handoff snapshot;

    EXPECT_TRUE(
        advance_to_phase(MICROS_TTY_CONSOLE_OWNED, &state)
        && micros_tty_handoff_claim(
            &state,
            MICROS_TTY_UART_IRQ_SOURCE
        ) == MICROS_TTY_HANDOFF_OK
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_tty_handoff_accept_ready(&state, 19)
            == MICROS_TTY_HANDOFF_ERROR_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    return true;
}

static bool test_uart_console_ownership(void)
{
    struct micros_uart_console_state state;
    struct micros_uart_console_state snapshot;

    EXPECT_TRUE(
        micros_uart_console_initialize(&state)
            == MICROS_UART_CONSOLE_OK
        && micros_uart_console_validate(&state)
            == MICROS_UART_CONSOLE_OK
        && micros_uart_console_preflight_begin(&state)
            == MICROS_UART_CONSOLE_OK
        && micros_uart_console_output_allowed(&state)
    );
    EXPECT_TRUE(
        micros_uart_console_quiesce(&state)
            == MICROS_UART_CONSOLE_OK
        && state.owner == MICROS_UART_CONSOLE_OWNER_EARLY
        && state.quiesced
        && micros_uart_console_output_allowed(&state)
        && micros_uart_console_commit_handoff(&state)
            == MICROS_UART_CONSOLE_OK
        && state.owner == MICROS_UART_CONSOLE_OWNER_TTY
        && !micros_uart_console_output_allowed(&state)
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_uart_console_preflight_begin(&state)
            == MICROS_UART_CONSOLE_ERROR_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && micros_uart_console_panic(&state)
            == MICROS_UART_CONSOLE_OK
        && state.owner == MICROS_UART_CONSOLE_OWNER_PANIC
        && micros_uart_console_output_allowed(&state)
        && micros_uart_console_panic(&state)
            == MICROS_UART_CONSOLE_OK
        && micros_uart_console_validate(&state)
            == MICROS_UART_CONSOLE_OK
    );
    return true;
}

static bool test_failure_preservation(void)
{
    struct micros_tty_handoff state;
    struct micros_tty_handoff snapshot;

    EXPECT_TRUE(
        micros_tty_handoff_initialize(&state)
            == MICROS_TTY_HANDOFF_OK
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_tty_handoff_mark_mapped(&state)
            == MICROS_TTY_HANDOFF_ERROR_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && micros_tty_handoff_begin(&state, UINT64_MAX, 1)
            == MICROS_TTY_HANDOFF_ERROR_RANGE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && micros_tty_handoff_begin(&state, 1, 0)
            == MICROS_TTY_HANDOFF_ERROR_ARGUMENT
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    EXPECT_TRUE(
        micros_tty_handoff_begin(&state, 1, 10)
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_mark_mapped(&state)
            == MICROS_TTY_HANDOFF_OK
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_tty_handoff_mark_mapped(&state)
            == MICROS_TTY_HANDOFF_ERROR_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && micros_tty_handoff_release(&state)
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_commit(&state)
            == MICROS_TTY_HANDOFF_OK
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_tty_handoff_claim(&state, 9)
            == MICROS_TTY_HANDOFF_ERROR_ARGUMENT
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && micros_tty_handoff_complete(
            &state,
            MICROS_TTY_UART_IRQ_SOURCE
        ) == MICROS_TTY_HANDOFF_ERROR_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && micros_tty_handoff_accept_ready(&state, 11)
            == MICROS_TTY_HANDOFF_ERROR_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    EXPECT_TRUE(
        micros_tty_handoff_claim(
            &state,
            MICROS_TTY_UART_IRQ_SOURCE
        ) == MICROS_TTY_HANDOFF_OK
    );
    snapshot = state;
    EXPECT_TRUE(
        micros_tty_handoff_claim(
            &state,
            MICROS_TTY_UART_IRQ_SOURCE
        ) == MICROS_TTY_HANDOFF_ERROR_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && micros_tty_handoff_complete(&state, 9)
            == MICROS_TTY_HANDOFF_ERROR_ARGUMENT
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    return true;
}

static bool test_device_leaf_classification(void)
{
    const struct micros_process_handle tty_process = {3, 7};
    const struct micros_process_handle foreign_process = {2, 5};
    const uint64_t tty_root = UINT64_C(0x80200000);
    const uint32_t exact_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_USER;
    struct micros_tty_device_authority authority = {
        .process = tty_process,
        .root_physical_address = tty_root,
        .console_phase = MICROS_TTY_CONSOLE_MAPPED,
    };
    enum micros_tty_device_leaf_class classification;

    EXPECT_TRUE(
        micros_tty_device_leaf_required(
            &authority,
            tty_process,
            tty_root
        )
        && !micros_tty_device_leaf_required(
            &authority,
            foreign_process,
            tty_root
        )
        && micros_tty_device_leaf_classify(
            &authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions,
            &classification
        ) == MICROS_TTY_HANDOFF_OK
        && classification == MICROS_TTY_DEVICE_LEAF_EXACT
        && micros_tty_device_range_intersects(
            &authority,
            tty_process,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_MAPPED_LENGTH
        )
        && micros_tty_device_range_intersects(
            &authority,
            tty_process,
            MICROS_TTY_UART_VIRTUAL_BASE - 1,
            2
        )
        && !micros_tty_device_range_intersects(
            &authority,
            tty_process,
            MICROS_TTY_UART_VIRTUAL_BASE
                - MICROS_TTY_UART_MAPPED_LENGTH,
            MICROS_TTY_UART_MAPPED_LENGTH
        )
        && !micros_tty_device_range_intersects(
            &authority,
            tty_process,
            MICROS_TTY_UART_VIRTUAL_BASE
                + MICROS_TTY_UART_MAPPED_LENGTH,
            1
        )
        && !micros_tty_device_range_intersects(
            &authority,
            foreign_process,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_MAPPED_LENGTH
        )
    );
    EXPECT_TRUE(
        micros_tty_device_leaf_classify(
            &authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions | MICROS_SV39_PERMISSION_EXECUTE,
            &classification
        ) == MICROS_TTY_HANDOFF_OK
        && classification == MICROS_TTY_DEVICE_LEAF_INVALID
        && micros_tty_device_leaf_classify(
            &authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE
                + MICROS_TTY_UART_MAPPED_LENGTH,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions,
            &classification
        ) == MICROS_TTY_HANDOFF_OK
        && classification == MICROS_TTY_DEVICE_LEAF_INVALID
        && micros_tty_device_leaf_classify(
            &authority,
            foreign_process,
            UINT64_C(0x80300000),
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions,
            &classification
        ) == MICROS_TTY_HANDOFF_OK
        && classification == MICROS_TTY_DEVICE_LEAF_INVALID
    );
    EXPECT_TRUE(
        micros_tty_device_leaf_classify(
            &authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE,
            UINT64_C(0x80400000),
            exact_permissions,
            &classification
        ) == MICROS_TTY_HANDOFF_OK
        && classification == MICROS_TTY_DEVICE_LEAF_INVALID
        && micros_tty_device_leaf_classify(
            &authority,
            foreign_process,
            UINT64_C(0x80300000),
            MICROS_TTY_UART_VIRTUAL_BASE,
            UINT64_C(0x80400000),
            exact_permissions,
            &classification
        ) == MICROS_TTY_HANDOFF_OK
        && classification == MICROS_TTY_DEVICE_LEAF_MANAGED
    );
    authority.console_phase = MICROS_TTY_CONSOLE_MAP_REQUESTED;
    classification = MICROS_TTY_DEVICE_LEAF_INVALID;
    EXPECT_TRUE(
        micros_tty_device_leaf_classify(
            &authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions,
            &classification
        ) == MICROS_TTY_HANDOFF_ERROR_STATE
        && classification == MICROS_TTY_DEVICE_LEAF_INVALID
    );
    return true;
}

static bool test_runtime_lifecycle_validation(void)
{
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
    runtime_process_handle = process;
    runtime_thread_handle = thread;
    runtime_process = (struct micros_process){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = process.generation,
        .address_space_root = root,
        .primary_endpoint = endpoint,
    };
    runtime_thread = (struct micros_thread){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = thread.generation,
        .owner = process,
        .context_attached = true,
        .runtime_flags = MICROS_THREAD_RTS_INACTIVE,
    };
    runtime_endpoint = (struct micros_endpoint_record){
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
    runtime_process.privilege_profile =
        MICROS_PRIVILEGE_PROFILE_TTY;
    EXPECT_TRUE(
        micros_tty_handoff_runtime_validate(
            &bootstrap,
            &registry,
            &objects
        ) == MICROS_TTY_HANDOFF_ERROR_INVARIANT
    );
    runtime_process.privilege_profile = 0;

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
    runtime_endpoint.state = MICROS_ENDPOINT_STATE_ACTIVE;
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
    runtime_endpoint.state = MICROS_ENDPOINT_STATE_RESERVED;

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
    runtime_process.privilege_profile =
        MICROS_PRIVILEGE_PROFILE_TTY;
    runtime_endpoint.state = MICROS_ENDPOINT_STATE_ACTIVE;
    runtime_thread.runtime_flags = 0;
    runtime_thread.scheduler_assigned = true;
    runtime_thread.scheduler_priority = binding->scheduler_priority;
    runtime_thread.scheduler_preemptible =
        binding->scheduler_preemptible;
    runtime_thread.quantum_counter_ticks =
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
        micros_tty_handoff_runtime_role_ready(
            &registry,
            &objects
        )
        && micros_tty_handoff_runtime_validate(
            &bootstrap,
            &registry,
            &objects
        ) == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_runtime_prepare_ready(149, &ready)
            == MICROS_TTY_HANDOFF_OK
        && ready.console_phase == MICROS_TTY_CONSOLE_OWNED
        && !ready.deadline_armed
        && ready.deadline == 0
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
    );
    transition->endpoint_state =
        MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY;
    runtime_endpoint.state = MICROS_ENDPOINT_STATE_SOURCE_ONLY;
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
    runtime_endpoint.state = MICROS_ENDPOINT_STATE_ACTIVE;
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

static bool advance_to_phase(
    enum micros_tty_console_phase phase,
    struct micros_tty_handoff *state
)
{
    if (
        micros_tty_handoff_initialize(state)
            != MICROS_TTY_HANDOFF_OK
        || phase == MICROS_TTY_CONSOLE_EARLY
    ) {
        return phase == MICROS_TTY_CONSOLE_EARLY;
    }
    if (
        micros_tty_handoff_begin(state, 10, 10)
            != MICROS_TTY_HANDOFF_OK
        || phase == MICROS_TTY_CONSOLE_MAP_REQUESTED
    ) {
        return phase == MICROS_TTY_CONSOLE_MAP_REQUESTED;
    }
    if (
        micros_tty_handoff_mark_mapped(state)
            != MICROS_TTY_HANDOFF_OK
        || phase == MICROS_TTY_CONSOLE_MAPPED
    ) {
        return phase == MICROS_TTY_CONSOLE_MAPPED;
    }
    if (
        micros_tty_handoff_release(state)
            != MICROS_TTY_HANDOFF_OK
        || phase == MICROS_TTY_CONSOLE_STARTING
    ) {
        return phase == MICROS_TTY_CONSOLE_STARTING;
    }
    return (
        micros_tty_handoff_commit(state) == MICROS_TTY_HANDOFF_OK
        && phase == MICROS_TTY_CONSOLE_OWNED
    );
}

static bool test_terminal_panic(void)
{
    const enum micros_tty_console_phase phases[] = {
        MICROS_TTY_CONSOLE_EARLY,
        MICROS_TTY_CONSOLE_MAP_REQUESTED,
        MICROS_TTY_CONSOLE_MAPPED,
        MICROS_TTY_CONSOLE_STARTING,
        MICROS_TTY_CONSOLE_OWNED,
    };
    size_t index;

    for (index = 0; index < sizeof(phases) / sizeof(phases[0]); ++index) {
        struct micros_tty_handoff state;

        EXPECT_TRUE(
            advance_to_phase(phases[index], &state)
            && micros_tty_handoff_panic(&state)
                == MICROS_TTY_HANDOFF_OK
            && state.console_phase == MICROS_TTY_CONSOLE_PANIC
            && state.route_phase == MICROS_TTY_ROUTE_PANIC
            && !state.deadline_armed
            && state.deadline == 0
            && state.claimed_source == 0
            && micros_tty_handoff_panic(&state)
                == MICROS_TTY_HANDOFF_OK
            && micros_tty_handoff_begin(&state, 1, 1)
                == MICROS_TTY_HANDOFF_ERROR_STATE
            && micros_tty_handoff_validate(&state)
                == MICROS_TTY_HANDOFF_OK
        );
    }
    return true;
}

static bool test_invariant_rejection(void)
{
    struct micros_tty_handoff state;

    EXPECT_TRUE(
        micros_tty_handoff_initialize(&state)
            == MICROS_TTY_HANDOFF_OK
    );
    state.route_phase = MICROS_TTY_ROUTE_IDLE;
    EXPECT_TRUE(
        micros_tty_handoff_validate(&state)
            == MICROS_TTY_HANDOFF_ERROR_INVARIANT
        && micros_tty_handoff_panic(&state)
            == MICROS_TTY_HANDOFF_OK
        && micros_tty_handoff_validate(&state)
            == MICROS_TTY_HANDOFF_OK
    );
    return true;
}

bool micros_tty_handoff_test_run(void)
{
    return (
        test_complete_handoff()
        && test_deadline_covers_every_pre_ready_phase()
        && test_ready_requires_idle_route()
        && test_uart_console_ownership()
        && test_failure_preservation()
        && test_device_leaf_classification()
        && test_runtime_lifecycle_validation()
        && test_terminal_panic()
        && test_invariant_rejection()
    );
}
