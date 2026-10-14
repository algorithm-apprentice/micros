#include "bootstrap_manifest_test_internal.h"

bool test_runtime_transitions(void)
{
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_manifest_plan plan = {
        .entry_count = 2,
        .total_user_page_limit = 7,
        .controller_service_id = 1,
        .vm_service_id = 2,
        .ordered_service_ids = {1, 2},
        .ordered_manifest_indices = {1, 0},
    };
    struct micros_bootstrap_runtime runtime;
    struct micros_bootstrap_runtime snapshot;

    bootstrap_manifest_test_initialize_manifest(&manifest);
    memset(&runtime, 0, sizeof(runtime));
    EXPECT_TRUE(
        micros_bootstrap_runtime_initialize(
            &manifest,
            &plan,
            &runtime
        ) == MICROS_BOOTSTRAP_OK
        && runtime.phase == MICROS_BOOTSTRAP_PHASE_RUNNING
        && runtime.entries[1].state
            == MICROS_BOOTSTRAP_SERVICE_READY
        && runtime.entries[1].endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        && runtime.entries[1].scheduler_assigned
        && runtime.entries[0].state
            == MICROS_BOOTSTRAP_SERVICE_PREPARED
        && runtime.entries[0].endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_RESERVED
        && !runtime.entries[0].scheduler_assigned
    );
    snapshot = runtime;
    EXPECT_TRUE(
        micros_bootstrap_runtime_release(&runtime, 99, 10)
            == MICROS_BOOTSTRAP_ERROR_ARGUMENT
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
        && micros_bootstrap_runtime_accept_ready(&runtime, 99, 10)
            == MICROS_BOOTSTRAP_ERROR_ARGUMENT
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
        && micros_bootstrap_runtime_release(&runtime, 1, 10)
            == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
        && micros_bootstrap_classify_ready(&runtime, 1, 1, true)
            == MICROS_BOOTSTRAP_READY_DUPLICATE
        && micros_bootstrap_classify_ready(&runtime, 2, 2, true)
            == MICROS_BOOTSTRAP_READY_EARLY
        && micros_bootstrap_classify_ready(&runtime, 99, 99, true)
            == MICROS_BOOTSTRAP_READY_FOREIGN
        && micros_bootstrap_classify_ready(&runtime, 2, 2, false)
            == MICROS_BOOTSTRAP_READY_MALFORMED
    );
    EXPECT_TRUE(
        micros_bootstrap_runtime_release(&runtime, 2, 10)
            == MICROS_BOOTSTRAP_OK
        && runtime.starting_service_id == 2
        && runtime.entries[0].state
            == MICROS_BOOTSTRAP_SERVICE_STARTING
        && runtime.entries[0].ready_deadline == 110
        && runtime.entries[0].profile_installed
        && runtime.entries[0].scheduler_assigned
        && runtime.entries[0].endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        && micros_bootstrap_classify_ready(&runtime, 2, 2, true)
            == MICROS_BOOTSTRAP_READY_ACCEPT
        && micros_bootstrap_classify_ready(&runtime, 2, 1, true)
            == MICROS_BOOTSTRAP_READY_MALFORMED
        && micros_bootstrap_classify_ready(&runtime, 1, 1, true)
            == MICROS_BOOTSTRAP_READY_DUPLICATE
    );
    snapshot = runtime;
    EXPECT_TRUE(
        micros_bootstrap_runtime_accept_ready(&runtime, 2, 111)
            == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
        && micros_bootstrap_runtime_accept_ready(&runtime, 2, 110)
            == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
        && micros_bootstrap_runtime_accept_ready(&runtime, 2, 109)
            == MICROS_BOOTSTRAP_OK
        && runtime.entries[0].state
            == MICROS_BOOTSTRAP_SERVICE_READY
        && runtime.starting_service_id == 0
        && runtime.next_order_index == 2
        && micros_bootstrap_runtime_complete(&runtime)
            == MICROS_BOOTSTRAP_OK
        && runtime.phase == MICROS_BOOTSTRAP_PHASE_SEALED
        && runtime.controller_service_id == 0
        && runtime.entries[1].endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY
        && !runtime.entries[1].scheduler_assigned
        && micros_bootstrap_runtime_release(&runtime, 2, 0)
            == MICROS_BOOTSTRAP_ERROR_STATE
    );
    snapshot = runtime;
    EXPECT_TRUE(
        micros_bootstrap_runtime_initialize(
            &manifest,
            &plan,
            &runtime
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
    );
    return true;
}

bool test_retained_runtime_deadline(void)
{
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_manifest_plan plan = {
        .entry_count = 2,
        .total_user_page_limit = 7,
        .controller_service_id = 1,
        .vm_service_id = 2,
        .ordered_service_ids = {1, 2},
        .ordered_manifest_indices = {1, 0},
    };
    struct micros_bootstrap_runtime runtime;
    struct micros_bootstrap_runtime snapshot;

    bootstrap_manifest_test_initialize_manifest(&manifest);
    memset(&runtime, 0, sizeof(runtime));
    EXPECT_TRUE(
        micros_bootstrap_runtime_initialize(
            &manifest,
            &plan,
            &runtime
        ) == MICROS_BOOTSTRAP_OK
    );
    snapshot = runtime;
    EXPECT_TRUE(
        micros_bootstrap_runtime_release_retaining_deadline(
            &runtime,
            2,
            120,
            150
        ) == MICROS_BOOTSTRAP_OK
        && runtime.entries[0].ready_deadline == 150
        && runtime.starting_service_id == 2
    );

    runtime = snapshot;
    EXPECT_TRUE(
        micros_bootstrap_runtime_release(&runtime, 2, 120)
            == MICROS_BOOTSTRAP_OK
        && runtime.entries[0].ready_deadline == 220
    );

    runtime = snapshot;
    EXPECT_TRUE(
        micros_bootstrap_runtime_release_retaining_deadline(
            &runtime,
            2,
            150,
            150
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
        && micros_bootstrap_runtime_release_retaining_deadline(
            &runtime,
            2,
            151,
            150
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
        && micros_bootstrap_runtime_release_retaining_deadline(
            &runtime,
            2,
            120,
            0
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&runtime, &snapshot, sizeof(runtime)) == 0
    );
    return true;
}
