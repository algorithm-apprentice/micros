#include "tests/host/pm_control_execution_test_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/pm/pm_control_runtime_core.h"
#include "kernel/vm/vm_handoff_core.h"
#include "micros/endpoint.h"
#include "micros/frame_allocator.h"
#include "micros/frame_ownership.h"
#include "micros/ipc_core.h"
#include "micros/spawn.h"
#include "micros/sv39.h"

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

static bool prepare_initial_execution(
    struct pm_control_execution_test_context *context,
    struct micros_pm_control_prepare_plan *prepare
)
{
    struct micros_pm_control_execution_binding binding;
    struct micros_pm_control_prepare_plan sentinel;

    EXPECT_TRUE(
        pm_control_execution_test_initialize_fixture(context)
    );
    memset(&sentinel, 0xa5, sizeof(sentinel));
    *prepare = sentinel;
    EXPECT_TRUE(
        micros_pm_control_prepare_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            context->reservation_token,
            context->load_token,
            context->mapping_generation,
            prepare
        ) == MICROS_PM_CONTROL_OK
        && prepare->process.slot == context->child.slot
        && prepare->thread.slot == 1
        && prepare->thread.generation == 1
        && prepare->endpoint
            == pm_control_execution_test_process_endpoint(
                context->child
            )
        && prepare->prepared_token == 1
        && prepare->result.version == MICROS_PM_PREPARED_VERSION
        && prepare->result.size == MICROS_PM_PREPARED_SIZE
        && prepare->result.prepared_token
            == prepare->prepared_token
        && prepare->result.endpoint == prepare->endpoint
    );
    binding = pm_control_execution_test_binding(context, prepare);
    micros_pm_control_prepare_commit_prevalidated(
        &context->control,
        &context->objects,
        &context->endpoints,
        &context->vm,
        prepare,
        &binding
    );
    return true;
}

static bool prepared_execution_is_valid(
    const struct pm_control_execution_test_context *context,
    const struct micros_pm_control_prepare_plan *prepare
)
{
    const struct micros_process *process;
    const struct micros_thread *thread;
    const struct micros_endpoint_record *endpoint;

    return (
        context->control.prepared.token == prepare->prepared_token
        && context->vm.mappings[context->child.slot].phase
            == MICROS_VM_KERNEL_MAPPING_PREPARED
        && micros_process_resolve(
            &context->objects,
            context->child,
            &process
        ) == MICROS_KERNEL_OBJECT_OK
        && process->live_thread_count == 1
        && process->primary_endpoint == prepare->endpoint
        && process->privilege_profile
            == MICROS_PRIVILEGE_PROFILE_APPLICATION
        && process->endpoint_lifecycle_consumed
        && micros_thread_resolve(
            &context->objects,
            prepare->thread,
            &thread
        ) == MICROS_KERNEL_OBJECT_OK
        && thread->context_attached
        && thread->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && thread->scheduler_assigned
        && thread->scheduler_preemptible
        && thread->scheduler_priority
            == MICROS_APPLICATION_SCHEDULER_PRIORITY
        && thread->scheduler_hart.slot == context->hart.slot
        && thread->quantum_counter_ticks
            == MICROS_APPLICATION_SCHEDULER_QUANTUM
        && thread->remaining_counter_ticks
            == MICROS_APPLICATION_SCHEDULER_QUANTUM
        && micros_endpoint_resolve_internal(
            &context->endpoints,
            &context->objects,
            prepare->endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint->state == MICROS_ENDPOINT_STATE_RESERVED
        && micros_pm_control_runtime_validate_reservation(
            &context->control,
            &context->objects,
            &context->ownership,
            &context->vm
        ) == MICROS_PM_CONTROL_OK
    );
}

static bool abort_initial_execution(
    struct pm_control_execution_test_context *context,
    uint64_t first_prepared_token
)
{
    struct micros_pm_control_abort_prepared_plan abort;

    EXPECT_TRUE(
        micros_pm_control_abort_prepared_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            context->reservation_token,
            context->load_token,
            context->mapping_generation,
            first_prepared_token + 1,
            &abort
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && micros_pm_control_abort_prepared_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            context->reservation_token,
            context->load_token,
            context->mapping_generation,
            first_prepared_token,
            &abort
        ) == MICROS_PM_CONTROL_OK
    );
    micros_pm_control_abort_prepared_commit_prevalidated(
        &context->control,
        &context->objects,
        &context->endpoints,
        &context->vm,
        &abort
    );
    EXPECT_TRUE(
        context->control.prepared.token == 0
        && context->control.active_transaction
            == context->reservation_token
        && context->objects.processes[context->child.slot]
            .live_thread_count == 0
        && context->objects.processes[context->child.slot]
            .primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        && context->objects.processes[context->child.slot]
            .privilege_profile == 0
        && !context->objects.processes[context->child.slot]
            .endpoint_lifecycle_consumed
        && context->vm.mappings[context->child.slot].phase
            == MICROS_VM_KERNEL_MAPPING_FROZEN
    );
    return true;
}

static bool reprepare_and_activate(
    struct pm_control_execution_test_context *context,
    uint64_t first_prepared_token
)
{
    struct micros_pm_control_prepare_plan prepare;
    struct micros_pm_control_execution_binding binding;
    struct micros_pm_control_activate_plan activate;

    EXPECT_TRUE(
        micros_pm_control_prepare_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            context->reservation_token,
            context->load_token,
            context->mapping_generation,
            &prepare
        ) == MICROS_PM_CONTROL_OK
        && prepare.prepared_token == first_prepared_token + 1
        && prepare.thread.slot == 1
        && prepare.thread.generation == 2
    );
    binding = pm_control_execution_test_binding(context, &prepare);
    micros_pm_control_prepare_commit_prevalidated(
        &context->control,
        &context->objects,
        &context->endpoints,
        &context->vm,
        &prepare,
        &binding
    );
    EXPECT_TRUE(
        micros_pm_control_activate_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            context->reservation_token,
            context->load_token,
            context->mapping_generation,
            prepare.prepared_token,
            &activate
        ) == MICROS_PM_CONTROL_OK
    );
    micros_pm_control_activate_commit_prevalidated(
        &context->control,
        &context->objects,
        &context->endpoints,
        &context->vm,
        &activate
    );
    EXPECT_TRUE(
        context->control.active_transaction == 0
        && context->control.prepared.token == 0
        && context->vm.mappings[context->child.slot].phase
            == MICROS_VM_KERNEL_MAPPING_ACTIVATED_PENDING_COMMIT
        && context->endpoints.endpoints[context->child.slot].state
            == MICROS_ENDPOINT_STATE_ACTIVE
        && context->objects.threads[activate.thread.slot]
            .runtime_flags == 0
        && context->objects.threads[activate.thread.slot]
            .ready_linked
        && context->objects.harts[context->hart.slot]
            .ready_head[MICROS_APPLICATION_SCHEDULER_PRIORITY].slot
            == activate.thread.slot
        && context->objects.harts[context->hart.slot]
            .ready_tail[MICROS_APPLICATION_SCHEDULER_PRIORITY].slot
            == activate.thread.slot
        && micros_pm_control_state_validate(&context->control)
            == MICROS_PM_CONTROL_OK
        && micros_kernel_objects_validate(&context->objects)
            == MICROS_KERNEL_OBJECT_OK
        && micros_endpoint_registry_validate_objects(
            &context->endpoints,
            &context->objects
        ) == MICROS_ENDPOINT_OK
        && micros_vm_handoff_state_validate(&context->vm)
            == MICROS_VM_HANDOFF_OK
        && micros_pm_control_runtime_validate_reservation(
            &context->control,
            &context->objects,
            &context->ownership,
            &context->vm
        ) == MICROS_PM_CONTROL_OK
    );
    return true;
}

bool test_prepare_abort_activate(
    struct pm_control_execution_test_context *context
)
{
    struct micros_pm_control_prepare_plan prepare;

    return (
        prepare_initial_execution(context, &prepare)
        && prepared_execution_is_valid(context, &prepare)
        && abort_initial_execution(
            context,
            prepare.prepared_token
        )
        && reprepare_and_activate(
            context,
            prepare.prepared_token
        )
    );
}

static bool activation_rejects_invariant(
    struct pm_control_execution_test_context *context,
    uint64_t prepared_token
)
{
    struct micros_pm_control_activate_plan activate;
    struct micros_pm_control_activate_plan sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    activate = sentinel;
    return (
        micros_pm_control_activate_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            context->reservation_token,
            context->load_token,
            context->mapping_generation,
            prepared_token,
            &activate
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
        && memcmp(&activate, &sentinel, sizeof(activate)) == 0
    );
}

bool test_prepared_corruption_is_invariant(
    struct pm_control_execution_test_context *context
)
{
    struct micros_pm_control_prepare_plan prepare;
    struct micros_pm_control_execution_binding binding;
    struct micros_thread *thread;
    struct micros_process *process;
    struct micros_endpoint_record *endpoint;
    struct micros_frame_owner owner;

    EXPECT_TRUE(
        pm_control_execution_test_initialize_fixture(context)
        && micros_pm_control_prepare_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            context->reservation_token,
            context->load_token,
            context->mapping_generation,
            &prepare
        ) == MICROS_PM_CONTROL_OK
    );
    binding = pm_control_execution_test_binding(context, &prepare);
    micros_pm_control_prepare_commit_prevalidated(
        &context->control,
        &context->objects,
        &context->endpoints,
        &context->vm,
        &prepare,
        &binding
    );
    thread = &context->objects.threads[prepare.thread.slot];
    process = &context->objects.processes[prepare.process.slot];
    endpoint = &context->endpoints.endpoints[prepare.process.slot];

    ++thread->scheduler_priority;
    EXPECT_TRUE(activation_rejects_invariant(context, prepare.prepared_token));
    --thread->scheduler_priority;

    thread->user_context.sepc += MICROS_SV39_PAGE_SIZE;
    EXPECT_TRUE(activation_rejects_invariant(context, prepare.prepared_token));
    thread->user_context.sepc -= MICROS_SV39_PAGE_SIZE;

    thread->user_context.sstatus ^= UINT64_C(1) << 40;
    EXPECT_TRUE(activation_rejects_invariant(context, prepare.prepared_token));
    thread->user_context.sstatus ^= UINT64_C(1) << 40;

    process->privilege_profile = MICROS_PRIVILEGE_PROFILE_PM;
    EXPECT_TRUE(activation_rejects_invariant(context, prepare.prepared_token));
    process->privilege_profile = MICROS_PRIVILEGE_PROFILE_APPLICATION;

    endpoint->state = MICROS_ENDPOINT_STATE_ACTIVE;
    EXPECT_TRUE(activation_rejects_invariant(context, prepare.prepared_token));
    endpoint->state = MICROS_ENDPOINT_STATE_RESERVED;

    owner = context->ownership.owners[0];
    ++context->ownership.owners[0].generation;
    EXPECT_TRUE(
        micros_pm_control_runtime_validate_reservation(
            &context->control,
            &context->objects,
            &context->ownership,
            &context->vm
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
    );
    context->ownership.owners[0] = owner;
    EXPECT_TRUE(
        micros_pm_control_runtime_validate_reservation(
            &context->control,
            &context->objects,
            &context->ownership,
            &context->vm
        ) == MICROS_PM_CONTROL_OK
        && micros_pm_control_activate_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            context->reservation_token,
            context->load_token,
            context->mapping_generation,
            prepare.prepared_token,
            &(struct micros_pm_control_activate_plan){0}
        ) == MICROS_PM_CONTROL_OK
    );
    return true;
}
