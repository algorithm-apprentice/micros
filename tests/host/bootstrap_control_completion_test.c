#include "bootstrap_control_test_internal.h"

#include "micros/ipc_core.h"
#include "micros/scheduler_core.h"

struct pm_completion_rejection_fixture {
    struct micros_bootstrap_control_state state;
    struct micros_bootstrap_control_state state_snapshot;
    struct micros_bootstrap_manifest manifest;
    struct micros_endpoint_registry registry;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects;
    struct micros_kernel_objects objects_snapshot;
    struct micros_hart_handle hart;
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct micros_bootstrap_complete_plan plan;
    struct micros_bootstrap_complete_plan sentinel;
    struct micros_bootstrap_ready_plan ready_plan;
};

struct launcher_dependency_fixture {
    struct micros_bootstrap_control_state state;
    struct micros_bootstrap_control_state state_snapshot;
    struct micros_bootstrap_manifest manifest;
    struct micros_endpoint_registry registry;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects;
    struct micros_kernel_objects objects_snapshot;
    struct micros_hart_handle hart;
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct micros_bootstrap_complete_plan plan;
    struct micros_bootstrap_complete_plan sentinel;
    struct micros_ipc_message request;
    struct micros_thread *launcher_thread;
    const struct micros_thread *pm_thread;
};

struct wake_any_fixture {
    struct micros_bootstrap_control_state state;
    struct micros_bootstrap_manifest manifest;
    struct micros_endpoint_registry registry;
    struct micros_kernel_objects objects;
    struct micros_hart_handle hart;
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct micros_bootstrap_complete_plan plan;
    struct micros_scheduler_return_plan return_plan;
    const struct micros_endpoint_record *pm_endpoint;
    const struct micros_thread *pm_thread;
};

static bool reject_starting_pm_and_accept_ready(
    struct pm_completion_rejection_fixture *fixture
)
{
    EXPECT_TRUE(
        bootstrap_control_test_setup_control_fixture(
            &fixture->state,
            &fixture->manifest,
            &fixture->registry,
            &fixture->objects,
            &fixture->hart,
            fixture->bindings,
            true
        )
        && bootstrap_control_test_release_and_accept_ready(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            fixture->hart,
            &fixture->bindings[0],
            10,
            109
        )
        && micros_bootstrap_control_release(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            fixture->hart,
            fixture->bindings[2].service_id,
            110,
            true
        ) == MICROS_BOOTSTRAP_OK
    );
    memset(&fixture->sentinel, 0xa5, sizeof(fixture->sentinel));
    fixture->plan = fixture->sentinel;
    fixture->state_snapshot = fixture->state;
    fixture->registry_snapshot = fixture->registry;
    fixture->objects_snapshot = fixture->objects;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            NULL,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
        && memcmp(&fixture->state, &fixture->state_snapshot, sizeof(fixture->state)) == 0
        && memcmp(
            &fixture->registry,
            &fixture->registry_snapshot,
            sizeof(fixture->registry)
        ) == 0
        && memcmp(&fixture->objects, &fixture->objects_snapshot, sizeof(fixture->objects)) == 0
    );
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_ready(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            fixture->bindings[2].service_id,
            fixture->bindings[2].endpoint,
            209,
            true,
            &fixture->ready_plan
        ) == MICROS_BOOTSTRAP_OK
    );
    micros_bootstrap_control_commit_ready_prevalidated(
        &fixture->state,
        &fixture->ready_plan
    );
    return true;
}

static bool reject_source_only_pm(struct pm_completion_rejection_fixture *fixture)
{
    fixture->registry.endpoints[fixture->bindings[2].process.slot].state =
        MICROS_ENDPOINT_STATE_SOURCE_ONLY;
    fixture->plan = fixture->sentinel;
    fixture->state_snapshot = fixture->state;
    fixture->registry_snapshot = fixture->registry;
    fixture->objects_snapshot = fixture->objects;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            NULL,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_INVARIANT
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
        && memcmp(&fixture->state, &fixture->state_snapshot, sizeof(fixture->state)) == 0
        && memcmp(
            &fixture->registry,
            &fixture->registry_snapshot,
            sizeof(fixture->registry)
        ) == 0
        && memcmp(&fixture->objects, &fixture->objects_snapshot, sizeof(fixture->objects)) == 0
    );
    fixture->registry.endpoints[fixture->bindings[2].process.slot].state =
        MICROS_ENDPOINT_STATE_ACTIVE;
    return true;
}

static bool reject_unassigned_pm(struct pm_completion_rejection_fixture *fixture)
{
    fixture->objects.threads[fixture->bindings[2].thread.slot].scheduler_assigned =
        false;
    fixture->plan = fixture->sentinel;
    fixture->state_snapshot = fixture->state;
    fixture->registry_snapshot = fixture->registry;
    fixture->objects_snapshot = fixture->objects;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            NULL,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_INVARIANT
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
        && memcmp(&fixture->state, &fixture->state_snapshot, sizeof(fixture->state)) == 0
        && memcmp(
            &fixture->registry,
            &fixture->registry_snapshot,
            sizeof(fixture->registry)
        ) == 0
        && memcmp(&fixture->objects, &fixture->objects_snapshot, sizeof(fixture->objects)) == 0
    );
    return true;
}

bool test_pm_completion_rejections_are_atomic(void)
{
    struct pm_completion_rejection_fixture fixture;

    return (
        reject_starting_pm_and_accept_ready(&fixture)
        && reject_source_only_pm(&fixture)
        && reject_unassigned_pm(&fixture)
    );
}

static bool reject_queued_launcher_dependency(struct launcher_dependency_fixture *fixture)
{
    EXPECT_TRUE(
        bootstrap_control_test_setup_ready_pm_fixture(
            &fixture->state,
            &fixture->manifest,
            &fixture->registry,
            &fixture->objects,
            &fixture->hart,
            fixture->bindings
        )
        && micros_thread_scheduler_hold(
            &fixture->objects,
            fixture->bindings[2].thread
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->bindings[2].thread,
            fixture->bindings[1].endpoint,
            &fixture->request,
            UINT64_C(0x4000)
        ) == MICROS_IPC_OK
    );
    memset(&fixture->sentinel, 0xa5, sizeof(fixture->sentinel));
    fixture->plan = fixture->sentinel;
    fixture->state_snapshot = fixture->state;
    fixture->registry_snapshot = fixture->registry;
    fixture->objects_snapshot = fixture->objects;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            NULL,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
        && memcmp(&fixture->state, &fixture->state_snapshot, sizeof(fixture->state)) == 0
        && memcmp(
            &fixture->registry,
            &fixture->registry_snapshot,
            sizeof(fixture->registry)
        ) == 0
        && memcmp(&fixture->objects, &fixture->objects_snapshot, sizeof(fixture->objects)) == 0
    );
    return true;
}

static bool reject_reply_wait_launcher_dependency(struct launcher_dependency_fixture *fixture)
{
    EXPECT_TRUE(
        bootstrap_control_test_setup_ready_pm_fixture(
            &fixture->state,
            &fixture->manifest,
            &fixture->registry,
            &fixture->objects,
            &fixture->hart,
            fixture->bindings
        )
        && micros_thread_scheduler_hold(
            &fixture->objects,
            fixture->bindings[1].thread
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_ipc_receive(
            &fixture->registry,
            &fixture->objects,
            fixture->bindings[1].thread,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x5000)
        ) == MICROS_IPC_OK
        && micros_thread_scheduler_hold(
            &fixture->objects,
            fixture->bindings[2].thread
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_ipc_call(
            &fixture->registry,
            &fixture->objects,
            fixture->bindings[2].thread,
            fixture->bindings[1].endpoint,
            &fixture->request,
            UINT64_C(0x6000)
        ) == MICROS_IPC_OK
    );
    fixture->launcher_thread = &fixture->objects.threads[fixture->bindings[1].thread.slot];
    fixture->pm_thread = &fixture->objects.threads[fixture->bindings[2].thread.slot];
    EXPECT_TRUE(
        fixture->launcher_thread->ipc_delivery_pending
        && fixture->pm_thread->ipc_reply_token != 0
        && fixture->pm_thread->ipc_reply_callee == fixture->bindings[1].endpoint
    );
    fixture->launcher_thread->ipc_receive_buffer = 0;
    fixture->launcher_thread->ipc_delivery_pending = false;
    memset(
        &fixture->launcher_thread->ipc_inbound_message,
        0,
        sizeof(fixture->launcher_thread->ipc_inbound_message)
    );
    fixture->launcher_thread->ipc_staged_result = MICROS_IPC_OK;
    EXPECT_TRUE(
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        ) == MICROS_ENDPOINT_OK
    );
    fixture->plan = fixture->sentinel;
    fixture->state_snapshot = fixture->state;
    fixture->registry_snapshot = fixture->registry;
    fixture->objects_snapshot = fixture->objects;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            NULL,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)) == 0
        && memcmp(&fixture->state, &fixture->state_snapshot, sizeof(fixture->state)) == 0
        && memcmp(
            &fixture->registry,
            &fixture->registry_snapshot,
            sizeof(fixture->registry)
        ) == 0
        && memcmp(&fixture->objects, &fixture->objects_snapshot, sizeof(fixture->objects)) == 0
    );
    return true;
}

bool test_completion_rejects_launcher_ipc_dependencies(void)
{
    struct launcher_dependency_fixture fixture;

    fixture.request = (struct micros_ipc_message){
        .type = UINT32_C(0x1234),
    };
    return (
        reject_queued_launcher_dependency(&fixture)
        && reject_reply_wait_launcher_dependency(&fixture)
    );
}

bool test_pm_completion_queues_sealed_event_once(void)
{
    struct micros_bootstrap_control_state state;
    struct micros_bootstrap_control_state state_snapshot;
    struct micros_bootstrap_manifest manifest;
    struct micros_endpoint_registry registry;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects;
    struct micros_kernel_objects objects_snapshot;
    struct micros_hart_handle hart;
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct micros_bootstrap_complete_plan plan;
    struct micros_bootstrap_complete_plan sentinel;
    const struct micros_endpoint_record *pm_endpoint;

    EXPECT_TRUE(
        bootstrap_control_test_setup_ready_pm_fixture(
            &state,
            &manifest,
            &registry,
            &objects,
            &hart,
            bindings
        )
    );
    memset(&sentinel, 0xa5, sizeof(sentinel));
    plan = sentinel;
    state_snapshot = state;
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &state,
            &registry,
            &objects,
            NULL,
            &plan
        ) == MICROS_BOOTSTRAP_OK
        && plan.active
        && plan.pm_notification.active
        && !plan.pm_notification.deliver_to_receiver
        && memcmp(&state, &state_snapshot, sizeof(state)) == 0
        && memcmp(
            &registry,
            &registry_snapshot,
            sizeof(registry)
        ) == 0
        && memcmp(&objects, &objects_snapshot, sizeof(objects)) == 0
        && bootstrap_control_test_commit_complete_for_test(
            &state,
            &registry,
            &objects,
            &bindings[1],
            &plan
        )
        && state.phase == MICROS_BOOTSTRAP_PHASE_SEALED
        && micros_endpoint_resolve_active(
            &registry,
            &objects,
            bindings[2].endpoint,
            &pm_endpoint
        ) == MICROS_ENDPOINT_OK
        && pm_endpoint->pending_kernel_events
            == MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED
        && micros_bootstrap_control_validate(
            &state,
            &registry,
            &objects
        ) == MICROS_BOOTSTRAP_OK
    );

    plan = sentinel;
    state_snapshot = state;
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &state,
            &registry,
            &objects,
            NULL,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
        && memcmp(&state, &state_snapshot, sizeof(state)) == 0
        && memcmp(
            &registry,
            &registry_snapshot,
            sizeof(registry)
        ) == 0
        && memcmp(&objects, &objects_snapshot, sizeof(objects)) == 0
    );
    return true;
}

static bool prepare_any_receiver_and_current_launcher(struct wake_any_fixture *fixture)
{
    EXPECT_TRUE(
        bootstrap_control_test_setup_ready_pm_fixture(
            &fixture->state,
            &fixture->manifest,
            &fixture->registry,
            &fixture->objects,
            &fixture->hart,
            fixture->bindings
        )
        && micros_thread_scheduler_hold(
            &fixture->objects,
            fixture->bindings[2].thread
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_ipc_receiver_enqueue(
            &fixture->registry,
            &fixture->objects,
            fixture->bindings[2].thread,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x61004000)
        ) == MICROS_IPC_OK
    );
    EXPECT_TRUE(
        micros_thread_scheduler_hold(
            &fixture->objects,
            fixture->bindings[0].thread
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_scheduler_accounting_initialize(
            &fixture->objects,
            fixture->hart,
            100
        ) == MICROS_KERNEL_OBJECT_OK
    );
    EXPECT_TRUE(
        micros_hart_plan_user_return(
            &fixture->objects,
            fixture->hart,
            &fixture->return_plan
        ) == MICROS_KERNEL_OBJECT_OK
        && fixture->return_plan.selected.slot
            == fixture->bindings[1].thread.slot
        && fixture->return_plan.selected.generation
            == fixture->bindings[1].thread.generation
    );
    EXPECT_TRUE(
        micros_hart_commit_user_return(
            &fixture->objects,
            &fixture->return_plan
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_scheduler_account_enter_thread(
            &fixture->objects,
            fixture->hart,
            fixture->bindings[1].thread,
            101
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_scheduler_account_user_trap(
            &fixture->objects,
            fixture->hart,
            102
        ) == MICROS_KERNEL_OBJECT_OK
    );
    return true;
}

static bool seal_and_check_exact_wake(struct wake_any_fixture *fixture)
{
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            NULL,
            &fixture->plan
        ) == MICROS_BOOTSTRAP_OK
        && fixture->plan.pm_notification.active
        && fixture->plan.pm_notification.deliver_to_receiver
    );
    EXPECT_TRUE(
        bootstrap_control_test_commit_current_complete_for_test(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            fixture->hart,
            &fixture->bindings[1],
            &fixture->plan
        )
    );
    EXPECT_TRUE(
        micros_endpoint_resolve_active(
            &fixture->registry,
            &fixture->objects,
            fixture->bindings[2].endpoint,
            &fixture->pm_endpoint
        ) == MICROS_ENDPOINT_OK
        && micros_thread_resolve(
            &fixture->objects,
            fixture->bindings[2].thread,
            &fixture->pm_thread
        ) == MICROS_KERNEL_OBJECT_OK
        && fixture->pm_endpoint->pending_kernel_events == 0
        && fixture->pm_endpoint->receiver_head.generation == 0
        && fixture->pm_endpoint->receiver_tail.generation == 0
        && fixture->pm_thread->runtime_flags == 0
        && fixture->pm_thread->ready_linked
        && fixture->pm_thread->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && fixture->pm_thread->ipc_receive_source == 0
        && fixture->pm_thread->ipc_receive_buffer == UINT64_C(0x61004000)
        && fixture->pm_thread->ipc_delivery_pending
        && bootstrap_control_test_sealed_notification_matches(
            &fixture->pm_thread->ipc_inbound_message
        )
        && micros_bootstrap_control_validate(
            &fixture->state,
            &fixture->registry,
            &fixture->objects
        ) == MICROS_BOOTSTRAP_OK
    );
    return true;
}

bool test_pm_completion_wakes_only_any_receiver(void)
{
    struct wake_any_fixture fixture;

    return (
        prepare_any_receiver_and_current_launcher(&fixture)
        && seal_and_check_exact_wake(&fixture)
    );
}

bool test_pm_completion_preserves_specific_receiver(void)
{
    struct micros_bootstrap_control_state state;
    struct micros_bootstrap_manifest manifest;
    struct micros_endpoint_registry registry;
    struct micros_kernel_objects objects;
    struct micros_hart_handle hart;
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct micros_bootstrap_complete_plan plan;
    const struct micros_endpoint_record *pm_endpoint;
    const struct micros_thread *pm_thread;

    EXPECT_TRUE(
        bootstrap_control_test_setup_ready_pm_fixture(
            &state,
            &manifest,
            &registry,
            &objects,
            &hart,
            bindings
        )
        && micros_thread_scheduler_hold(
            &objects,
            bindings[2].thread
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            bindings[2].thread,
            bindings[0].endpoint,
            UINT64_C(0x61005000)
        ) == MICROS_IPC_OK
    );
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &state,
            &registry,
            &objects,
            NULL,
            &plan
        ) == MICROS_BOOTSTRAP_OK
        && plan.pm_notification.active
        && !plan.pm_notification.deliver_to_receiver
    );
    EXPECT_TRUE(
        bootstrap_control_test_commit_complete_for_test(
            &state,
            &registry,
            &objects,
            &bindings[1],
            &plan
        )
    );
    EXPECT_TRUE(
        micros_endpoint_resolve_active(
            &registry,
            &objects,
            bindings[2].endpoint,
            &pm_endpoint
        ) == MICROS_ENDPOINT_OK
        && micros_thread_resolve(
            &objects,
            bindings[2].thread,
            &pm_thread
        ) == MICROS_KERNEL_OBJECT_OK
        && pm_endpoint->pending_kernel_events
            == MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED
        && pm_endpoint->receiver_head.slot
            == bindings[2].thread.slot
        && pm_endpoint->receiver_head.generation
            == bindings[2].thread.generation
        && pm_endpoint->receiver_tail.slot
            == bindings[2].thread.slot
        && pm_endpoint->receiver_tail.generation
            == bindings[2].thread.generation
        && pm_thread->runtime_flags
            == MICROS_THREAD_RTS_IPC_RECEIVE
        && !pm_thread->ready_linked
        && pm_thread->ipc_queue_kind
            == MICROS_IPC_QUEUE_RECEIVER
        && pm_thread->ipc_receive_source
            == bindings[0].endpoint
        && !pm_thread->ipc_delivery_pending
        && micros_bootstrap_control_validate(
            &state,
            &registry,
            &objects
        ) == MICROS_BOOTSTRAP_OK
    );
    return true;
}
