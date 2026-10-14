#include "bootstrap_control_test_internal.h"

#include "kernel/endpoint_internal.h"

struct control_transition_fixture {
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
    struct micros_bootstrap_ready_plan ready_plan;
    struct micros_bootstrap_ready_plan ready_sentinel;
    struct micros_bootstrap_complete_plan complete_plan;
    const struct micros_endpoint_record *endpoint;
};

bool test_binding_lookup_rejects_oversized_count(void)
{
    struct micros_bootstrap_control_state state;

    memset(&state, 0, sizeof(state));
    state.entry_count = MICROS_BOOTSTRAP_SERVICE_CAPACITY + 1;
    state.controller_process.generation = 7;
    return micros_bootstrap_control_find_binding(&state, 7) == NULL;
}

static bool setup_and_release(struct control_transition_fixture *fixture)
{
    EXPECT_TRUE(
        bootstrap_control_test_setup_control_fixture(
            &fixture->state,
            &fixture->manifest,
            &fixture->registry,
            &fixture->objects,
            &fixture->hart,
            fixture->bindings,
            false
        )
    );
    fixture->state_snapshot = fixture->state;
    fixture->registry_snapshot = fixture->registry;
    fixture->objects_snapshot = fixture->objects;
    EXPECT_TRUE(
        micros_bootstrap_control_release(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            fixture->hart,
            99,
            10,
            true
        ) == MICROS_BOOTSTRAP_ERROR_ARGUMENT
        && memcmp(&fixture->state, &fixture->state_snapshot, sizeof(fixture->state)) == 0
        && memcmp(
            &fixture->registry,
            &fixture->registry_snapshot,
            sizeof(fixture->registry)
        ) == 0
        && memcmp(&fixture->objects, &fixture->objects_snapshot, sizeof(fixture->objects)) == 0
    );
    EXPECT_TRUE(
        micros_bootstrap_control_release(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            fixture->hart,
            2,
            10,
            true
        ) == MICROS_BOOTSTRAP_OK
        && fixture->state.transitions.starting_service_id == 2
        && fixture->state.transitions.entries[0].ready_deadline == 110
        && fixture->objects.processes[fixture->bindings[0].process.slot]
            .privilege_profile == 2
        && fixture->objects.threads[fixture->bindings[0].thread.slot]
            .scheduler_assigned
        && fixture->registry.endpoints[fixture->bindings[0].process.slot].state
            == MICROS_ENDPOINT_STATE_ACTIVE
    );
    return true;
}

static bool reject_ready_requests(struct control_transition_fixture *fixture)
{
    memset(&fixture->ready_sentinel, 0xa5, sizeof(fixture->ready_sentinel));
    fixture->ready_plan = fixture->ready_sentinel;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_ready(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            99,
            fixture->bindings[0].endpoint,
            109,
            true,
            &fixture->ready_plan
        ) == MICROS_BOOTSTRAP_ERROR_ARGUMENT
        && memcmp(
            &fixture->ready_plan,
            &fixture->ready_sentinel,
            sizeof(fixture->ready_plan)
        ) == 0
    );
    fixture->ready_plan = fixture->ready_sentinel;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_ready(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            2,
            fixture->bindings[1].endpoint,
            109,
            true,
            &fixture->ready_plan
        ) == MICROS_BOOTSTRAP_ERROR_IDENTITY
        && memcmp(
            &fixture->ready_plan,
            &fixture->ready_sentinel,
            sizeof(fixture->ready_plan)
        ) == 0
    );
    fixture->ready_plan = fixture->ready_sentinel;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_ready(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            2,
            fixture->bindings[1].endpoint,
            109,
            false,
            &fixture->ready_plan
        ) == MICROS_BOOTSTRAP_ERROR_ROLE
        && memcmp(
            &fixture->ready_plan,
            &fixture->ready_sentinel,
            sizeof(fixture->ready_plan)
        ) == 0
    );
    fixture->ready_plan = fixture->ready_sentinel;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_ready(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            2,
            fixture->bindings[0].endpoint,
            110,
            true,
            &fixture->ready_plan
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(
            &fixture->ready_plan,
            &fixture->ready_sentinel,
            sizeof(fixture->ready_plan)
        ) == 0
    );
    return true;
}

static bool accept_ready_and_reject_duplicate(struct control_transition_fixture *fixture)
{
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_ready(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            2,
            fixture->bindings[0].endpoint,
            109,
            true,
            &fixture->ready_plan
        ) == MICROS_BOOTSTRAP_OK
        && fixture->ready_plan.active
        && fixture->ready_plan.acknowledgment.source
            == fixture->bindings[1].endpoint
        && fixture->ready_plan.acknowledgment.type
            == MICROS_BOOTSTRAP_MESSAGE_READY_ACK
        && fixture->ready_plan.acknowledgment.reply_token == 0
        && bootstrap_control_test_read_u32_le(&fixture->ready_plan.acknowledgment.payload[0]) == 1
        && bootstrap_control_test_read_u32_le(&fixture->ready_plan.acknowledgment.payload[4]) == 2
        && bootstrap_control_test_read_u32_le(&fixture->ready_plan.acknowledgment.payload[8]) == 1
        && bootstrap_control_test_read_u32_le(&fixture->ready_plan.acknowledgment.payload[12]) == 0
        && bootstrap_control_test_read_u32_le(&fixture->ready_plan.acknowledgment.payload[16])
            == fixture->bindings[0].endpoint
    );
    micros_bootstrap_control_commit_ready_prevalidated(
        &fixture->state,
        &fixture->ready_plan
    );
    EXPECT_TRUE(
        fixture->state.transitions.entries[0].state
            == MICROS_BOOTSTRAP_SERVICE_READY
        && fixture->state.transitions.starting_service_id == 0
        && !fixture->ready_plan.active
    );
    fixture->ready_plan = fixture->ready_sentinel;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_ready(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            2,
            fixture->bindings[1].endpoint,
            109,
            true,
            &fixture->ready_plan
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(
            &fixture->ready_plan,
            &fixture->ready_sentinel,
            sizeof(fixture->ready_plan)
        ) == 0
    );
    return true;
}

static bool prepare_completion_acknowledgment(struct control_transition_fixture *fixture)
{
    fixture->objects.threads[fixture->bindings[0].thread.slot].ipc_receive_buffer =
        UINT64_C(0x61002000);
    fixture->objects.threads[fixture->bindings[0].thread.slot].ipc_delivery_pending =
        true;
    fixture->objects.threads[fixture->bindings[0].thread.slot]
        .ipc_inbound_message.source = fixture->bindings[1].endpoint;
    fixture->objects.threads[fixture->bindings[0].thread.slot]
        .ipc_inbound_message.type = UINT32_C(60);
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            NULL,
            &fixture->complete_plan
        ) == MICROS_BOOTSTRAP_ERROR_STATE
    );
    memset(
        &fixture->objects.threads[fixture->bindings[0].thread.slot]
            .ipc_inbound_message,
        0,
        sizeof(
            fixture->objects.threads[fixture->bindings[0].thread.slot]
                .ipc_inbound_message
        )
    );
    fixture->objects.threads[fixture->bindings[0].thread.slot]
        .ipc_inbound_message.source = fixture->bindings[1].endpoint;
    fixture->objects.threads[fixture->bindings[0].thread.slot]
        .ipc_inbound_message.type =
            MICROS_BOOTSTRAP_MESSAGE_READY_ACK;
    bootstrap_control_test_write_u32_le(
        &fixture->objects.threads[fixture->bindings[0].thread.slot]
            .ipc_inbound_message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    bootstrap_control_test_write_u32_le(
        &fixture->objects.threads[fixture->bindings[0].thread.slot]
            .ipc_inbound_message.payload[4],
        2
    );
    bootstrap_control_test_write_u32_le(
        &fixture->objects.threads[fixture->bindings[0].thread.slot]
            .ipc_inbound_message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    bootstrap_control_test_write_u32_le(
        &fixture->objects.threads[fixture->bindings[0].thread.slot]
            .ipc_inbound_message.payload[16],
        fixture->bindings[0].endpoint
    );
    return true;
}

static bool seal_and_validate(struct control_transition_fixture *fixture)
{
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            NULL,
            &fixture->complete_plan
        ) == MICROS_BOOTSTRAP_OK
        && fixture->complete_plan.active
        && !fixture->complete_plan.pm_notification.active
    );
    EXPECT_TRUE(
        bootstrap_control_test_commit_complete_for_test(
            &fixture->state,
            &fixture->registry,
            &fixture->objects,
            &fixture->bindings[1],
            &fixture->complete_plan
        )
    );
    EXPECT_TRUE(
        micros_bootstrap_control_validate(
            &fixture->state,
            &fixture->registry,
            &fixture->objects
        ) == MICROS_BOOTSTRAP_OK
        && fixture->state.phase == MICROS_BOOTSTRAP_PHASE_SEALED
        && fixture->state.controller_process.generation == 0
        && fixture->state.controller_thread.generation == 0
        && fixture->state.controller_endpoint == 0
        && micros_endpoint_resolve_internal(
            &fixture->registry,
            &fixture->objects,
            fixture->bindings[1].endpoint,
            &fixture->endpoint
        ) == MICROS_ENDPOINT_OK
        && fixture->endpoint->state == MICROS_ENDPOINT_STATE_SOURCE_ONLY
        && micros_endpoint_resolve_active(
            &fixture->registry,
            &fixture->objects,
            fixture->bindings[1].endpoint,
            &fixture->endpoint
        ) == MICROS_ENDPOINT_ERROR_CLOSING
    );
    bootstrap_control_test_write_u32_le(
        &fixture->objects.threads[fixture->bindings[0].thread.slot]
            .ipc_inbound_message.payload[4],
        1
    );
    EXPECT_TRUE(
        micros_bootstrap_control_validate(
            &fixture->state,
            &fixture->registry,
            &fixture->objects
        ) == MICROS_BOOTSTRAP_ERROR_INVARIANT
    );
    bootstrap_control_test_write_u32_le(
        &fixture->objects.threads[fixture->bindings[0].thread.slot]
            .ipc_inbound_message.payload[4],
        2
    );
    EXPECT_TRUE(
        micros_bootstrap_control_validate(
            &fixture->state,
            &fixture->registry,
            &fixture->objects
        ) == MICROS_BOOTSTRAP_OK
    );
    return true;
}

bool test_control_transitions(void)
{
    struct control_transition_fixture fixture;

    return (
        setup_and_release(&fixture)
        && reject_ready_requests(&fixture)
        && accept_ready_and_reject_duplicate(&fixture)
        && prepare_completion_acknowledgment(&fixture)
        && seal_and_validate(&fixture)
    );
}
