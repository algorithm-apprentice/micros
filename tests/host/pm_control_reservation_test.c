#include "tests/host/pm_control_test_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/pm_control_runtime_core.h"
#include "micros/endpoint.h"
#include "micros/frame_allocator.h"
#include "micros/frame_ownership.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

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

bool test_state_preparation(void)
{
    struct micros_kernel_objects objects;
    struct micros_pm_control_state state;
    struct micros_pm_control_state sentinel;
    struct micros_process_handle process = {
        .slot = 2,
        .generation = 1,
    };
    struct micros_thread_handle thread = {
        .slot = 4,
        .generation = 1,
    };

    EXPECT_TRUE(pm_control_test_initialize_fixture(&objects, &state));
    sentinel = state;
    EXPECT_TRUE(
        micros_pm_control_state_prepare(
            &state,
            3,
            pm_control_test_process_endpoint(process),
            MICROS_PRIVILEGE_PROFILE_PM,
            process,
            thread
        ) == MICROS_PM_CONTROL_ERROR_STORAGE
        && memcmp(&state, &sentinel, sizeof(state)) == 0
    );
    memset(&state, 0, sizeof(state));
    EXPECT_TRUE(
        micros_pm_control_state_prepare(
            &state,
            2,
            pm_control_test_process_endpoint(process),
            MICROS_PRIVILEGE_PROFILE_PM,
            process,
            thread
        ) == MICROS_PM_CONTROL_ERROR_ARGUMENT
        && micros_pm_control_state_prepare(
            &state,
            3,
            MICROS_ENDPOINT_NONE,
            MICROS_PRIVILEGE_PROFILE_PM,
            process,
            thread
        ) == MICROS_PM_CONTROL_ERROR_ARGUMENT
        && micros_pm_control_state_prepare(
            &state,
            3,
            pm_control_test_process_endpoint(process),
            MICROS_PRIVILEGE_PROFILE_APPLICATION,
            process,
            thread
        ) == MICROS_PM_CONTROL_ERROR_ARGUMENT
    );
    return true;
}

static bool reserve_initial_process(
    struct micros_kernel_objects *objects,
    struct micros_pm_control_state *state,
    struct micros_pm_control_reserve_plan *reserve
)
{
    const struct micros_process *process;

    EXPECT_TRUE(
        micros_pm_control_reserve_preflight(
            state,
            objects,
            reserve
        ) == MICROS_PM_CONTROL_OK
        && reserve->process.slot == 3
        && reserve->process.generation == 1
        && reserve->transaction == 1
        && reserve->result.version == MICROS_PM_RESERVATION_VERSION
        && reserve->result.size == MICROS_PM_RESERVATION_SIZE
        && reserve->result.transaction == 1
        && reserve->result.reserved[0] == 0
        && reserve->result.reserved[1] == 0
    );
    micros_pm_control_reserve_commit_prevalidated(
        state,
        objects,
        reserve
    );
    EXPECT_TRUE(
        state->last_transaction == 1
        && state->active_transaction == 1
        && state->reserved_process.slot == 3
        && state->reserved_process.generation == 1
        && objects->live_process_count == 4
        && micros_process_resolve(
            objects,
            reserve->process,
            &process
        ) == MICROS_KERNEL_OBJECT_OK
        && process->live_thread_count == 0
        && process->address_space_root == 0
        && process->primary_endpoint == MICROS_ENDPOINT_NONE
        && process->privilege_profile == 0
        && !process->endpoint_lifecycle_consumed
        && micros_pm_control_state_validate_objects(
            state,
            objects
        ) == MICROS_PM_CONTROL_OK
    );
    return true;
}

static bool duplicate_reserve_preserves_state(
    struct micros_kernel_objects *objects,
    struct micros_pm_control_state *state
)
{
    struct micros_pm_control_reserve_plan reserve;
    struct micros_pm_control_reserve_plan sentinel;
    struct micros_pm_control_state state_snapshot = *state;
    struct micros_kernel_objects objects_snapshot = *objects;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    reserve = sentinel;
    EXPECT_TRUE(
        micros_pm_control_reserve_preflight(
            state,
            objects,
            &reserve
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && memcmp(&reserve, &sentinel, sizeof(reserve)) == 0
        && memcmp(state, &state_snapshot, sizeof(*state)) == 0
        && memcmp(objects, &objects_snapshot, sizeof(*objects)) == 0
    );
    return true;
}

static bool abort_and_reuse_process(
    struct micros_kernel_objects *objects,
    struct micros_pm_control_state *state
)
{
    struct micros_pm_control_reserve_plan reserve;
    struct micros_pm_control_abort_plan abort;
    struct micros_pm_control_abort_plan sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    abort = sentinel;
    EXPECT_TRUE(
        micros_pm_control_abort_preflight(
            state,
            objects,
            2,
            &abort
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && memcmp(&abort, &sentinel, sizeof(abort)) == 0
    );
    EXPECT_TRUE(
        micros_pm_control_abort_preflight(
            state,
            objects,
            1,
            &abort
        ) == MICROS_PM_CONTROL_OK
    );
    EXPECT_TRUE(
        abort.process.slot == 3
        && abort.process.generation == 1
        && abort.transaction == 1
        && !abort.quarantine
    );
    micros_pm_control_abort_commit_prevalidated(
        state,
        objects,
        &abort
    );
    EXPECT_TRUE(
        state->last_transaction == 1
        && state->active_transaction == 0
        && state->reserved_process.generation == 0
        && objects->live_process_count == 3
        && objects->processes[3].slot_state
            == MICROS_KERNEL_OBJECT_SLOT_FREE
        && objects->processes[3].generation == 1
        && micros_pm_control_state_validate_objects(
            state,
            objects
        ) == MICROS_PM_CONTROL_OK
    );
    abort = sentinel;
    EXPECT_TRUE(
        micros_pm_control_abort_preflight(
            state,
            objects,
            1,
            &abort
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && memcmp(&abort, &sentinel, sizeof(abort)) == 0
    );
    EXPECT_TRUE(
        micros_pm_control_reserve_preflight(
            state,
            objects,
            &reserve
        ) == MICROS_PM_CONTROL_OK
        && reserve.process.slot == 3
        && reserve.process.generation == 2
        && reserve.transaction == 2
    );
    return true;
}

bool test_reserve_abort_lifecycle(void)
{
    struct micros_kernel_objects objects;
    struct micros_pm_control_state state;
    struct micros_pm_control_reserve_plan reserve;

    EXPECT_TRUE(pm_control_test_initialize_fixture(&objects, &state));
    return (
        reserve_initial_process(&objects, &state, &reserve)
        && duplicate_reserve_preserves_state(&objects, &state)
        && abort_and_reuse_process(&objects, &state)
    );
}

bool test_capacity_and_quarantine(void)
{
    struct micros_kernel_objects objects;
    struct micros_kernel_objects snapshot;
    struct micros_pm_control_state state;
    struct micros_pm_control_state state_snapshot;
    struct micros_pm_control_reserve_plan reserve;
    struct micros_pm_control_reserve_plan sentinel;
    struct micros_pm_control_abort_plan abort;
    size_t index;

    EXPECT_TRUE(pm_control_test_initialize_fixture(&objects, &state));
    memset(&sentinel, 0xa5, sizeof(sentinel));
    reserve = sentinel;
    state.last_transaction = UINT64_MAX;
    state_snapshot = state;
    snapshot = objects;
    EXPECT_TRUE(
        micros_pm_control_reserve_preflight(
            &state,
            &objects,
            &reserve
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
        && memcmp(&reserve, &sentinel, sizeof(reserve)) == 0
        && memcmp(&state, &state_snapshot, sizeof(state)) == 0
        && memcmp(&objects, &snapshot, sizeof(objects)) == 0
    );

    EXPECT_TRUE(pm_control_test_initialize_fixture(&objects, &state));
    for (index = 3; index < MICROS_PROCESS_CAPACITY; ++index) {
        objects.processes[index].slot_state =
            MICROS_KERNEL_OBJECT_SLOT_QUARANTINED;
        objects.processes[index].generation =
            MICROS_PROCESS_GENERATION_MAX;
    }
    EXPECT_TRUE(
        micros_kernel_objects_validate(&objects)
            == MICROS_KERNEL_OBJECT_OK
    );
    reserve = sentinel;
    EXPECT_TRUE(
        micros_pm_control_reserve_preflight(
            &state,
            &objects,
            &reserve
        ) == MICROS_PM_CONTROL_ERROR_CAPACITY
        && memcmp(&reserve, &sentinel, sizeof(reserve)) == 0
    );

    EXPECT_TRUE(pm_control_test_initialize_fixture(&objects, &state));
    objects.processes[3].generation =
        MICROS_PROCESS_GENERATION_MAX - 1;
    EXPECT_TRUE(
        micros_pm_control_reserve_preflight(
            &state,
            &objects,
            &reserve
        ) == MICROS_PM_CONTROL_OK
        && reserve.process.slot == 3
        && reserve.process.generation
            == MICROS_PROCESS_GENERATION_MAX
    );
    micros_pm_control_reserve_commit_prevalidated(
        &state,
        &objects,
        &reserve
    );
    EXPECT_TRUE(
        micros_pm_control_abort_preflight(
            &state,
            &objects,
            reserve.transaction,
            &abort
        ) == MICROS_PM_CONTROL_OK
        && abort.quarantine
    );
    micros_pm_control_abort_commit_prevalidated(
        &state,
        &objects,
        &abort
    );
    EXPECT_TRUE(
        objects.processes[3].slot_state
            == MICROS_KERNEL_OBJECT_SLOT_QUARANTINED
        && objects.processes[3].generation
            == MICROS_PROCESS_GENERATION_MAX
        && state.last_transaction == 1
        && micros_pm_control_reserve_preflight(
            &state,
            &objects,
            &reserve
        ) == MICROS_PM_CONTROL_OK
        && reserve.process.slot == 4
        && reserve.transaction == 2
    );
    return true;
}

bool test_invariant_rejection(void)
{
    struct micros_kernel_objects objects;
    struct micros_pm_control_state state;
    struct micros_pm_control_reserve_plan reserve;
    struct micros_pm_control_abort_plan abort;

    EXPECT_TRUE(
        pm_control_test_initialize_fixture(&objects, &state)
        && micros_pm_control_reserve_preflight(
            &state,
            &objects,
            &reserve
        ) == MICROS_PM_CONTROL_OK
    );
    micros_pm_control_reserve_commit_prevalidated(
        &state,
        &objects,
        &reserve
    );
    objects.processes[reserve.process.slot].address_space_root = 0x1000;
    EXPECT_TRUE(
        micros_pm_control_state_validate_objects(
            &state,
            &objects
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
        && micros_pm_control_abort_preflight(
            &state,
            &objects,
            reserve.transaction,
            &abort
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
    );
    objects.processes[reserve.process.slot].address_space_root = 0;
    ++state.active_transaction;
    EXPECT_TRUE(
        micros_pm_control_state_validate(&state)
            == MICROS_PM_CONTROL_ERROR_INVARIANT
    );
    return true;
}
