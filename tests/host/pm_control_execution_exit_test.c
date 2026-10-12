#include "tests/host/pm_control_execution_test_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/pm_control_runtime_core.h"
#include "micros/ipc_core.h"
#include "micros/spawn.h"

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

struct pm_control_exit_scenario {
    struct pm_control_execution_test_context *context;
    struct micros_thread_handle thread;
    struct micros_process_handle child;
    uint64_t exit_token;
};

static bool begin_child_exit(
    struct pm_control_exit_scenario *scenario
)
{
    struct micros_pm_control_exit_begin_plan begin;
    struct micros_pm_control_exit_begin_plan sentinel;
    struct pm_control_execution_test_context *context =
        scenario->context;

    EXPECT_TRUE(
        pm_control_execution_test_activate_child(
            context,
            &scenario->thread
        )
        && pm_control_execution_test_block_child_in_exit_call(
            context,
            scenario->thread
        )
    );
    scenario->child = context->child;
    memset(&sentinel, 0xa5, sizeof(sentinel));
    begin = sentinel;
    EXPECT_TRUE(
        micros_pm_control_exit_begin_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            pm_control_execution_test_process_endpoint(
                context->pm_process
            ),
            &begin
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && memcmp(&begin, &sentinel, sizeof(begin)) == 0
        && micros_pm_control_exit_begin_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            pm_control_execution_test_process_endpoint(
                scenario->child
            ),
            &begin
        ) == MICROS_PM_CONTROL_OK
        && begin.process.slot == scenario->child.slot
        && begin.thread.slot == scenario->thread.slot
        && begin.exit_token == 1
        && begin.load_token == context->load_token
        && begin.mapping_generation == context->mapping_generation
        && begin.result.version == MICROS_PM_EXIT_BEGIN_VERSION
        && begin.result.size == MICROS_PM_EXIT_BEGIN_SIZE
        && begin.result.exit_token == begin.exit_token
    );
    scenario->exit_token = begin.exit_token;
    micros_pm_control_exit_begin_commit_prevalidated(
        &context->control,
        &context->objects,
        &context->endpoints,
        &begin
    );
    EXPECT_TRUE(
        context->control.exit.phase
            == MICROS_PM_CONTROL_EXIT_STOPPED
        && context->endpoints.endpoints[
            scenario->child.slot
        ].state == MICROS_ENDPOINT_STATE_SOURCE_ONLY
        && context->objects.threads[
            scenario->thread.slot
        ].runtime_flags
            == (
                MICROS_THREAD_RTS_INACTIVE
                | MICROS_THREAD_RTS_IPC_REPLY
            )
        && micros_pm_control_state_validate_objects(
            &context->control,
            &context->objects
        ) == MICROS_PM_CONTROL_OK
        && micros_endpoint_registry_validate_objects(
            &context->endpoints,
            &context->objects
        ) == MICROS_ENDPOINT_OK
        && micros_pm_control_runtime_validate_reservation(
            &context->control,
            &context->objects,
            &context->ownership,
            &context->vm
        ) == MICROS_PM_CONTROL_OK
    );
    return true;
}

static bool detach_child_execution(
    struct pm_control_exit_scenario *scenario
)
{
    struct micros_pm_control_exit_detach_plan detach;
    struct pm_control_execution_test_context *context =
        scenario->context;
    uint32_t endpoint =
        pm_control_execution_test_process_endpoint(
            scenario->child
        );

    EXPECT_TRUE(
        micros_pm_control_exit_detach_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            scenario->exit_token + 1,
            endpoint,
            &detach
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && micros_pm_control_exit_detach_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            scenario->exit_token,
            endpoint,
            &detach
        ) == MICROS_PM_CONTROL_OK
        && micros_ipc_endpoint_close(
            &context->endpoints,
            &context->objects,
            endpoint
        ) == MICROS_IPC_OK
    );
    micros_pm_control_exit_detach_commit_prevalidated(
        &context->control,
        &context->objects,
        &context->vm,
        &detach
    );
    EXPECT_TRUE(
        context->control.exit.phase
            == MICROS_PM_CONTROL_EXIT_DETACHED
        && context->vm.mappings[scenario->child.slot].phase
            == MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED
        && context->vm.mappings[scenario->child.slot].exit_token
            == scenario->exit_token
        && context->objects.processes[
            scenario->child.slot
        ].live_thread_count == 0
        && context->objects.processes[
            scenario->child.slot
        ].primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        && context->objects.processes[
            scenario->child.slot
        ].privilege_profile == 0
        && context->objects.processes[
            scenario->child.slot
        ].endpoint_lifecycle_consumed
        && context->objects.threads[
            scenario->thread.slot
        ].slot_state == MICROS_KERNEL_OBJECT_SLOT_FREE
        && micros_pm_control_state_validate_objects(
            &context->control,
            &context->objects
        ) == MICROS_PM_CONTROL_OK
        && micros_pm_control_runtime_validate_reservation(
            &context->control,
            &context->objects,
            &context->ownership,
            &context->vm
        ) == MICROS_PM_CONTROL_OK
    );
    return true;
}

static bool detached_ownership_corruption_is_rejected(
    struct pm_control_exit_scenario *scenario
)
{
    struct micros_frame_owner owner =
        scenario->context->ownership.owners[0];

    ++scenario->context->ownership.owners[0].generation;
    EXPECT_TRUE(
        micros_pm_control_runtime_validate_reservation(
            &scenario->context->control,
            &scenario->context->objects,
            &scenario->context->ownership,
            &scenario->context->vm
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
    );
    scenario->context->ownership.owners[0] = owner;
    EXPECT_TRUE(
        micros_pm_control_runtime_validate_reservation(
            &scenario->context->control,
            &scenario->context->objects,
            &scenario->context->ownership,
            &scenario->context->vm
        ) == MICROS_PM_CONTROL_OK
    );
    return true;
}

static bool release_child_vm(
    struct pm_control_exit_scenario *scenario
)
{
    struct pm_control_execution_test_context *context =
        scenario->context;
    struct micros_vm_exit_release_action vm_release;
    struct micros_frame_vm_process_release_plan ownership_release;
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    size_t index;

    EXPECT_TRUE(
        micros_vm_exit_release_preflight(
            &context->vm,
            scenario->exit_token,
            context->load_token,
            context->mapping_generation,
            &vm_release
        ) == MICROS_VM_HANDOFF_OK
    );
    for (index = 0; index < vm_release.entry_count; ++index) {
        data_frames[index] =
            vm_release.entries[index].physical_address;
    }
    EXPECT_TRUE(
        micros_frame_ownership_vm_release_process_preflight(
            &context->ownership,
            scenario->child,
            data_frames,
            vm_release.entry_count,
            vm_release.table_frames,
            &ownership_release
        ) == MICROS_FRAME_OWNERSHIP_OK
    );
    context->objects.processes[
        scenario->child.slot
    ].address_space_root = 0;
    micros_frame_ownership_vm_release_process_commit_prevalidated(
        &context->ownership,
        &ownership_release
    );
    micros_vm_exit_release_commit_prevalidated(
        &context->vm,
        scenario->child
    );
    return (
        micros_pm_control_runtime_validate_reservation(
            &context->control,
            &context->objects,
            &context->ownership,
            &context->vm
        ) == MICROS_PM_CONTROL_OK
    );
}

static bool release_child_process(
    struct pm_control_exit_scenario *scenario
)
{
    struct pm_control_execution_test_context *context =
        scenario->context;
    struct micros_pm_control_release_exited_plan release;
    const struct micros_process *stale_process = NULL;

    EXPECT_TRUE(
        micros_pm_control_release_exited_preflight(
            &context->control,
            &context->objects,
            &context->vm,
            scenario->exit_token + 1,
            &release
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && micros_pm_control_release_exited_preflight(
            &context->control,
            &context->objects,
            &context->vm,
            scenario->exit_token,
            &release
        ) == MICROS_PM_CONTROL_OK
    );
    micros_pm_control_release_exited_commit_prevalidated(
        &context->control,
        &context->objects,
        &release
    );
    EXPECT_TRUE(
        context->control.exit.phase == MICROS_PM_CONTROL_EXIT_NONE
        && context->control.last_exit_token == scenario->exit_token
        && context->objects.processes[
            scenario->child.slot
        ].slot_state == MICROS_KERNEL_OBJECT_SLOT_FREE
        && context->objects.processes[
            scenario->child.slot
        ].generation == scenario->child.generation
        && micros_process_resolve(
            &context->objects,
            scenario->child,
            &stale_process
        ) == MICROS_KERNEL_OBJECT_ERROR_STALE
        && stale_process == NULL
        && micros_pm_control_state_validate_objects(
            &context->control,
            &context->objects
        ) == MICROS_PM_CONTROL_OK
        && micros_kernel_objects_validate(&context->objects)
            == MICROS_KERNEL_OBJECT_OK
        && micros_endpoint_registry_validate_objects(
            &context->endpoints,
            &context->objects
        ) == MICROS_ENDPOINT_OK
        && micros_vm_handoff_state_validate(&context->vm)
            == MICROS_VM_HANDOFF_OK
    );
    return true;
}

bool test_exit_begin_detach_release(
    struct pm_control_execution_test_context *context
)
{
    struct pm_control_exit_scenario scenario = {
        .context = context,
    };

    return (
        begin_child_exit(&scenario)
        && detach_child_execution(&scenario)
        && detached_ownership_corruption_is_rejected(&scenario)
        && release_child_vm(&scenario)
        && release_child_process(&scenario)
    );
}
