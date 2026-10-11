#include "kernel/pm_control_core.h"
#include "kernel/pm_control_runtime_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/vm_handoff_core.h"
#include "micros/endpoint.h"
#include "micros/frame_allocator.h"
#include "micros/frame_ownership.h"
#include "micros/ipc_core.h"
#include "micros/spawn.h"
#include "micros/sv39.h"

bool micros_pm_control_execution_test_run(void);

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

struct execution_fixture {
    struct micros_kernel_objects objects;
    struct micros_endpoint_registry endpoints;
    struct micros_pm_control_state control;
    struct micros_vm_handoff_state vm;
    struct micros_frame_allocator allocator;
    struct micros_frame_ownership ownership;
    struct micros_process_handle child;
    struct micros_process_handle pm_process;
    struct micros_thread_handle pm_thread;
    struct micros_hart_handle hart;
    uint32_t pm_endpoint;
    uint64_t reservation_token;
    uint64_t load_token;
    uint64_t mapping_generation;
    _Alignas(MICROS_SV39_PAGE_SIZE)
    unsigned char kernel_stack[MICROS_THREAD_KERNEL_STACK_SIZE];
};

static struct execution_fixture fixture;
static struct execution_fixture fixture_snapshot;

static bool activate_child(struct micros_thread_handle *thread);
static bool block_child_in_exit_call(
    struct micros_thread_handle thread_handle
);

static const struct micros_privilege_profile profiles[] = {
    {
        .id = MICROS_PRIVILEGE_PROFILE_PM,
        .name = "PM",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        .kernel_operations = MICROS_KERNEL_OPERATION_PM_CONTROL,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_VFS,
        .name = "VFS",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_APPLICATION,
        .name = "APPLICATION",
        .operations = MICROS_PRIVILEGE_OPERATION_CALL,
        .call_targets =
            (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_PM
            ) | (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS
            ),
    },
};

static uint32_t process_endpoint(struct micros_process_handle process)
{
    return (
        process.generation << MICROS_ENDPOINT_SLOT_BITS
    ) | process.slot;
}

static bool append_page(
    struct micros_vm_handoff_state *vm,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint32_t permissions
)
{
    struct micros_vm_scratch_map_request map = {
        .load_token = vm->load.load_token,
        .virtual_address = virtual_address,
        .physical_address = physical_address,
        .permissions = permissions,
    };
    struct micros_vm_scratch_commit_request commit = {
        .load_token = vm->load.load_token,
        .virtual_address = virtual_address,
        .physical_address = physical_address,
    };
    struct micros_vm_scratch_plan plan;

    if (
        micros_vm_scratch_map_preflight(vm, &map, &plan)
            != MICROS_VM_HANDOFF_OK
    ) {
        return false;
    }
    micros_vm_scratch_map_commit_prevalidated(vm, &plan);
    if (
        micros_vm_scratch_commit_preflight(vm, &commit, &plan)
            != MICROS_VM_HANDOFF_OK
    ) {
        return false;
    }
    micros_vm_scratch_commit_commit_prevalidated(vm, &plan);
    return true;
}

static bool freeze_child(void)
{
    const uint64_t frame_base = UINT64_C(0x00400000);
    const uint64_t table_base = UINT64_C(0x00500000);
    struct micros_vm_load_begin_plan begin;
    struct micros_vm_load_freeze_plan request = {
        .version = MICROS_VM_LOAD_FREEZE_PLAN_VERSION,
        .size = MICROS_VM_LOAD_FREEZE_PLAN_SIZE,
        .table_frames = {
            table_base,
            table_base + MICROS_SV39_PAGE_SIZE,
            table_base + 2 * MICROS_SV39_PAGE_SIZE,
            table_base + 3 * MICROS_SV39_PAGE_SIZE,
        },
        .entry = MICROS_APPLICATION_ENTRY,
        .stack_pointer = MICROS_APPLICATION_STACK_TOP - 16,
        .image_page_count = 3,
        .data_page_count = 4,
    };
    struct micros_vm_load_freeze_state_plan freeze;
    struct micros_frame_vm_claim_plan claim;
    struct micros_frame_vm_freeze_plan ownership_freeze;
    uint64_t data_frames[4];
    size_t index;

    EXPECT_TRUE(
        micros_vm_load_begin_preflight(
            &fixture.vm,
            fixture.reservation_token,
            fixture.child,
            true,
            &begin
        ) == MICROS_VM_HANDOFF_OK
    );
    micros_vm_load_begin_commit_prevalidated(&fixture.vm, &begin);
    for (index = 0; index < 4; ++index) {
        data_frames[index] =
            frame_base + index * MICROS_SV39_PAGE_SIZE;
        EXPECT_TRUE(
            micros_frame_ownership_vm_claim_preflight(
                &fixture.ownership,
                data_frames[index],
                &claim
            ) == MICROS_FRAME_OWNERSHIP_OK
        );
        micros_frame_ownership_vm_claim_commit_prevalidated(
            &fixture.ownership,
            &claim
        );
    }
    EXPECT_TRUE(
        append_page(
            &fixture.vm,
            MICROS_APPLICATION_ENTRY,
            data_frames[0],
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
        )
        && append_page(
            &fixture.vm,
            MICROS_APPLICATION_ENTRY + MICROS_SV39_PAGE_SIZE,
            data_frames[1],
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
        )
        && append_page(
            &fixture.vm,
            MICROS_APPLICATION_ENTRY + 2 * MICROS_SV39_PAGE_SIZE,
            data_frames[2],
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
        )
        && append_page(
            &fixture.vm,
            MICROS_APPLICATION_STACK_BASE,
            data_frames[3],
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
        )
    );
    request.load_token = fixture.vm.load.load_token;
    request.page_plan_digest = micros_vm_load_page_plan_digest(
        request.image_page_count,
        request.data_page_count,
        fixture.vm.mappings[fixture.child.slot].entries
    );
    EXPECT_TRUE(
        micros_vm_load_freeze_preflight(
            &fixture.vm,
            &request,
            &freeze
        ) == MICROS_VM_HANDOFF_OK
        && micros_frame_ownership_vm_freeze_preflight(
            &fixture.ownership,
            fixture.child,
            data_frames,
            4,
            request.table_frames,
            &ownership_freeze
        ) == MICROS_FRAME_OWNERSHIP_OK
    );
    micros_frame_ownership_vm_freeze_commit_prevalidated(
        &fixture.ownership,
        &ownership_freeze
    );
    fixture.objects.processes[
        fixture.child.slot
    ].address_space_root = request.table_frames[0];
    micros_vm_load_freeze_commit_prevalidated(
        &fixture.vm,
        &freeze
    );
    fixture.load_token = fixture.vm.load.load_token;
    fixture.mapping_generation = freeze.mapping_generation;
    return (
        micros_pm_control_runtime_validate_reservation(
            &fixture.control,
            &fixture.objects,
            &fixture.ownership,
            &fixture.vm
        ) == MICROS_PM_CONTROL_OK
    );
}

static bool initialize_fixture(void)
{
    struct micros_process_handle process;
    struct micros_process_handle vm_process;
    struct micros_process_handle pm_process;
    struct micros_thread_handle pm_thread;
    struct micros_pm_control_reserve_plan reserve;
    struct micros_vm_boot_summary summary = {
        .version = MICROS_VM_BOOT_INFO_VERSION,
        .managed_range_count = 1,
        .address_space_count = 1,
        .mapping_count = 1,
        .managed_frame_count = 8,
        .free_frame_count = 8,
        .vm_self_wired_frame_count = 1,
        .digest = UINT64_C(0x1234),
    };
    const struct micros_physical_range ranges[] = {
        {
            .base = UINT64_C(0x00400000),
            .size = 4 * MICROS_SV39_PAGE_SIZE,
        },
        {
            .base = UINT64_C(0x00500000),
            .size = 4 * MICROS_SV39_PAGE_SIZE,
        },
    };
    uint32_t endpoint;

    memset(&fixture, 0, sizeof(fixture));
    EXPECT_TRUE(
        micros_kernel_objects_initialize(
            &fixture.objects,
            1,
            1
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_process_create_at(
            &fixture.objects,
            0,
            &process
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_process_create_at(
            &fixture.objects,
            1,
            &vm_process
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_process_create_at(
            &fixture.objects,
            2,
            &pm_process
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_create(
            &fixture.objects,
            pm_process,
            &pm_thread
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_hart_register(
            &fixture.objects,
            0,
            &fixture.hart
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_frame_allocator_initialize(
            &fixture.allocator,
            ranges,
            sizeof(ranges) / sizeof(ranges[0]),
            NULL,
            0
        ) == MICROS_FRAME_ALLOCATOR_OK
        && micros_frame_ownership_initialize(
            &fixture.ownership,
            &fixture.allocator
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_complete_handoff(
            &fixture.ownership
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_endpoint_registry_initialize(
            &fixture.endpoints,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) == MICROS_ENDPOINT_OK
        && micros_endpoint_reserve(
            &fixture.endpoints,
            &fixture.objects,
            pm_process,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint == process_endpoint(pm_process)
        && micros_endpoint_install_profile(
            &fixture.endpoints,
            &fixture.objects,
            pm_process,
            MICROS_PRIVILEGE_PROFILE_PM
        ) == MICROS_ENDPOINT_OK
        && micros_endpoint_activate(
            &fixture.endpoints,
            &fixture.objects,
            endpoint
        ) == MICROS_ENDPOINT_OK
        && micros_pm_control_state_prepare(
            &fixture.control,
            3,
            endpoint,
            MICROS_PRIVILEGE_PROFILE_PM,
            pm_process,
            pm_thread
        ) == MICROS_PM_CONTROL_OK
        && micros_pm_control_reserve_preflight(
            &fixture.control,
            &fixture.objects,
            &reserve
        ) == MICROS_PM_CONTROL_OK
    );
    micros_pm_control_reserve_commit_prevalidated(
        &fixture.control,
        &fixture.objects,
        &reserve
    );
    fixture.child = reserve.process;
    fixture.pm_process = pm_process;
    fixture.pm_thread = pm_thread;
    fixture.pm_endpoint = endpoint;
    fixture.reservation_token = reserve.transaction;
    EXPECT_TRUE(
        micros_vm_handoff_state_prepare(
            &fixture.vm,
            2,
            process_endpoint(vm_process),
            1,
            MICROS_PRIVILEGE_PROFILE_VM,
            vm_process,
            (struct micros_thread_handle){
                .slot = 4,
                .generation = 9,
            },
            UINT64_C(0x00200000),
            UINT64_C(0x00300000),
            MICROS_VM_BOOT_INFO_SIZE,
            &summary
        ) == MICROS_VM_HANDOFF_OK
    );
    micros_vm_handoff_commit_prevalidated(&fixture.vm);
    return freeze_child();
}

static struct micros_pm_control_execution_binding execution_binding(
    const struct micros_pm_control_prepare_plan *plan
)
{
    struct micros_pm_control_execution_binding binding;

    memset(&binding, 0, sizeof(binding));
    binding.thread = plan->thread;
    binding.hart = fixture.hart;
    binding.kernel_stack_bottom =
        (uintptr_t)&fixture.kernel_stack[0];
    binding.kernel_stack_top =
        binding.kernel_stack_bottom + sizeof(fixture.kernel_stack);
    binding.context.sp = MICROS_APPLICATION_STACK_TOP - 16;
    binding.context.sstatus =
        (UINT64_C(2) << 32) | MICROS_RISCV_SSTATUS_SPIE;
    binding.context.sepc = MICROS_APPLICATION_ENTRY;
    return binding;
}

static bool test_prepare_abort_activate(void)
{
    struct micros_pm_control_prepare_plan prepare;
    struct micros_pm_control_abort_prepared_plan abort;
    struct micros_pm_control_activate_plan activate;
    struct micros_pm_control_execution_binding binding;
    struct micros_pm_control_prepare_plan sentinel;
    const struct micros_process *process;
    const struct micros_thread *thread;
    const struct micros_endpoint_record *endpoint;
    uint64_t first_prepared_token;

    EXPECT_TRUE(initialize_fixture());
    memset(&sentinel, 0xa5, sizeof(sentinel));
    prepare = sentinel;
    EXPECT_TRUE(
        micros_pm_control_prepare_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            fixture.reservation_token,
            fixture.load_token,
            fixture.mapping_generation,
            &prepare
        ) == MICROS_PM_CONTROL_OK
        && prepare.process.slot == fixture.child.slot
        && prepare.thread.slot == 1
        && prepare.thread.generation == 1
        && prepare.endpoint == process_endpoint(fixture.child)
        && prepare.prepared_token == 1
        && prepare.result.version == MICROS_PM_PREPARED_VERSION
        && prepare.result.size == MICROS_PM_PREPARED_SIZE
        && prepare.result.prepared_token == prepare.prepared_token
        && prepare.result.endpoint == prepare.endpoint
    );
    binding = execution_binding(&prepare);
    micros_pm_control_prepare_commit_prevalidated(
        &fixture.control,
        &fixture.objects,
        &fixture.endpoints,
        &fixture.vm,
        &prepare,
        &binding
    );
    first_prepared_token = prepare.prepared_token;
    EXPECT_TRUE(
        fixture.control.prepared.token == first_prepared_token
        && fixture.vm.mappings[fixture.child.slot].phase
            == MICROS_VM_KERNEL_MAPPING_PREPARED
        && micros_process_resolve(
            &fixture.objects,
            fixture.child,
            &process
        ) == MICROS_KERNEL_OBJECT_OK
        && process->live_thread_count == 1
        && process->primary_endpoint == prepare.endpoint
        && process->privilege_profile
            == MICROS_PRIVILEGE_PROFILE_APPLICATION
        && process->endpoint_lifecycle_consumed
        && micros_thread_resolve(
            &fixture.objects,
            prepare.thread,
            &thread
        ) == MICROS_KERNEL_OBJECT_OK
        && thread->context_attached
        && thread->runtime_flags == MICROS_THREAD_RTS_INACTIVE
        && thread->scheduler_assigned
        && thread->scheduler_preemptible
        && thread->scheduler_priority
            == MICROS_APPLICATION_SCHEDULER_PRIORITY
        && thread->scheduler_hart.slot == fixture.hart.slot
        && thread->quantum_counter_ticks
            == MICROS_APPLICATION_SCHEDULER_QUANTUM
        && thread->remaining_counter_ticks
            == MICROS_APPLICATION_SCHEDULER_QUANTUM
        && micros_endpoint_resolve_internal(
            &fixture.endpoints,
            &fixture.objects,
            prepare.endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint->state == MICROS_ENDPOINT_STATE_RESERVED
        && micros_pm_control_runtime_validate_reservation(
            &fixture.control,
            &fixture.objects,
            &fixture.ownership,
            &fixture.vm
        ) == MICROS_PM_CONTROL_OK
    );
    EXPECT_TRUE(
        micros_pm_control_abort_prepared_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            fixture.reservation_token,
            fixture.load_token,
            fixture.mapping_generation,
            first_prepared_token + 1,
            &abort
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && micros_pm_control_abort_prepared_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            fixture.reservation_token,
            fixture.load_token,
            fixture.mapping_generation,
            first_prepared_token,
            &abort
        ) == MICROS_PM_CONTROL_OK
    );
    micros_pm_control_abort_prepared_commit_prevalidated(
        &fixture.control,
        &fixture.objects,
        &fixture.endpoints,
        &fixture.vm,
        &abort
    );
    EXPECT_TRUE(
        fixture.control.prepared.token == 0
        && fixture.control.active_transaction
            == fixture.reservation_token
        && fixture.objects.processes[fixture.child.slot]
            .live_thread_count == 0
        && fixture.objects.processes[fixture.child.slot]
            .primary_endpoint == MICROS_PROCESS_ENDPOINT_NONE
        && fixture.objects.processes[fixture.child.slot]
            .privilege_profile == 0
        && !fixture.objects.processes[fixture.child.slot]
            .endpoint_lifecycle_consumed
        && fixture.vm.mappings[fixture.child.slot].phase
            == MICROS_VM_KERNEL_MAPPING_FROZEN
    );

    EXPECT_TRUE(
        micros_pm_control_prepare_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            fixture.reservation_token,
            fixture.load_token,
            fixture.mapping_generation,
            &prepare
        ) == MICROS_PM_CONTROL_OK
        && prepare.prepared_token == first_prepared_token + 1
        && prepare.thread.slot == 1
        && prepare.thread.generation == 2
    );
    binding = execution_binding(&prepare);
    micros_pm_control_prepare_commit_prevalidated(
        &fixture.control,
        &fixture.objects,
        &fixture.endpoints,
        &fixture.vm,
        &prepare,
        &binding
    );
    EXPECT_TRUE(
        micros_pm_control_activate_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            fixture.reservation_token,
            fixture.load_token,
            fixture.mapping_generation,
            prepare.prepared_token,
            &activate
        ) == MICROS_PM_CONTROL_OK
    );
    micros_pm_control_activate_commit_prevalidated(
        &fixture.control,
        &fixture.objects,
        &fixture.endpoints,
        &fixture.vm,
        &activate
    );
    EXPECT_TRUE(
        fixture.control.active_transaction == 0
        && fixture.control.prepared.token == 0
        && fixture.vm.mappings[fixture.child.slot].phase
            == MICROS_VM_KERNEL_MAPPING_ACTIVATED_PENDING_COMMIT
        && fixture.endpoints.endpoints[fixture.child.slot].state
            == MICROS_ENDPOINT_STATE_ACTIVE
        && fixture.objects.threads[activate.thread.slot]
            .runtime_flags == 0
        && fixture.objects.threads[activate.thread.slot]
            .ready_linked
        && fixture.objects.harts[fixture.hart.slot]
            .ready_head[MICROS_APPLICATION_SCHEDULER_PRIORITY].slot
            == activate.thread.slot
        && fixture.objects.harts[fixture.hart.slot]
            .ready_tail[MICROS_APPLICATION_SCHEDULER_PRIORITY].slot
            == activate.thread.slot
        && micros_pm_control_state_validate(&fixture.control)
            == MICROS_PM_CONTROL_OK
        && micros_kernel_objects_validate(&fixture.objects)
            == MICROS_KERNEL_OBJECT_OK
        && micros_endpoint_registry_validate_objects(
            &fixture.endpoints,
            &fixture.objects
        ) == MICROS_ENDPOINT_OK
        && micros_vm_handoff_state_validate(&fixture.vm)
            == MICROS_VM_HANDOFF_OK
        && micros_pm_control_runtime_validate_reservation(
            &fixture.control,
            &fixture.objects,
            &fixture.ownership,
            &fixture.vm
        ) == MICROS_PM_CONTROL_OK
    );
    return true;
}

static bool activation_rejects_invariant(uint64_t prepared_token)
{
    struct micros_pm_control_activate_plan activate;
    struct micros_pm_control_activate_plan sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    activate = sentinel;
    return (
        micros_pm_control_activate_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            fixture.reservation_token,
            fixture.load_token,
            fixture.mapping_generation,
            prepared_token,
            &activate
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
        && memcmp(&activate, &sentinel, sizeof(activate)) == 0
    );
}

static bool test_prepared_corruption_is_invariant(void)
{
    struct micros_pm_control_prepare_plan prepare;
    struct micros_pm_control_execution_binding binding;
    struct micros_thread *thread;
    struct micros_process *process;
    struct micros_endpoint_record *endpoint;
    struct micros_frame_owner owner;

    EXPECT_TRUE(
        initialize_fixture()
        && micros_pm_control_prepare_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            fixture.reservation_token,
            fixture.load_token,
            fixture.mapping_generation,
            &prepare
        ) == MICROS_PM_CONTROL_OK
    );
    binding = execution_binding(&prepare);
    micros_pm_control_prepare_commit_prevalidated(
        &fixture.control,
        &fixture.objects,
        &fixture.endpoints,
        &fixture.vm,
        &prepare,
        &binding
    );
    thread = &fixture.objects.threads[prepare.thread.slot];
    process = &fixture.objects.processes[prepare.process.slot];
    endpoint = &fixture.endpoints.endpoints[prepare.process.slot];

    ++thread->scheduler_priority;
    EXPECT_TRUE(activation_rejects_invariant(prepare.prepared_token));
    --thread->scheduler_priority;

    thread->user_context.sepc += MICROS_SV39_PAGE_SIZE;
    EXPECT_TRUE(activation_rejects_invariant(prepare.prepared_token));
    thread->user_context.sepc -= MICROS_SV39_PAGE_SIZE;

    thread->user_context.sstatus ^= UINT64_C(1) << 40;
    EXPECT_TRUE(activation_rejects_invariant(prepare.prepared_token));
    thread->user_context.sstatus ^= UINT64_C(1) << 40;

    process->privilege_profile = MICROS_PRIVILEGE_PROFILE_PM;
    EXPECT_TRUE(activation_rejects_invariant(prepare.prepared_token));
    process->privilege_profile = MICROS_PRIVILEGE_PROFILE_APPLICATION;

    endpoint->state = MICROS_ENDPOINT_STATE_ACTIVE;
    EXPECT_TRUE(activation_rejects_invariant(prepare.prepared_token));
    endpoint->state = MICROS_ENDPOINT_STATE_RESERVED;

    owner = fixture.ownership.owners[0];
    ++fixture.ownership.owners[0].generation;
    EXPECT_TRUE(
        micros_pm_control_runtime_validate_reservation(
            &fixture.control,
            &fixture.objects,
            &fixture.ownership,
            &fixture.vm
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
    );
    fixture.ownership.owners[0] = owner;
    EXPECT_TRUE(
        micros_pm_control_runtime_validate_reservation(
            &fixture.control,
            &fixture.objects,
            &fixture.ownership,
            &fixture.vm
        ) == MICROS_PM_CONTROL_OK
        && micros_pm_control_activate_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            fixture.reservation_token,
            fixture.load_token,
            fixture.mapping_generation,
            prepare.prepared_token,
            &(struct micros_pm_control_activate_plan){0}
        ) == MICROS_PM_CONTROL_OK
    );
    return true;
}

static bool test_token_exhaustion_is_invariant(void)
{
    struct micros_pm_control_prepare_plan prepare;
    struct micros_pm_control_prepare_plan prepare_sentinel;
    struct micros_pm_control_exit_begin_plan begin;
    struct micros_pm_control_exit_begin_plan begin_sentinel;
    struct micros_thread_handle thread;

    EXPECT_TRUE(initialize_fixture());
    fixture.control.last_prepared_token = UINT64_MAX;
    memcpy(&fixture_snapshot, &fixture, sizeof(fixture));
    memset(&prepare_sentinel, 0xa5, sizeof(prepare_sentinel));
    prepare = prepare_sentinel;
    EXPECT_TRUE(
        micros_pm_control_prepare_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            fixture.reservation_token,
            fixture.load_token,
            fixture.mapping_generation,
            &prepare
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
        && memcmp(&prepare, &prepare_sentinel, sizeof(prepare)) == 0
        && memcmp(&fixture, &fixture_snapshot, sizeof(fixture)) == 0
    );

    EXPECT_TRUE(
        activate_child(&thread)
        && block_child_in_exit_call(thread)
    );
    fixture.control.last_exit_token = UINT64_MAX;
    memcpy(&fixture_snapshot, &fixture, sizeof(fixture));
    memset(&begin_sentinel, 0x5a, sizeof(begin_sentinel));
    begin = begin_sentinel;
    EXPECT_TRUE(
        micros_pm_control_exit_begin_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            process_endpoint(fixture.child),
            &begin
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
        && memcmp(&begin, &begin_sentinel, sizeof(begin)) == 0
        && memcmp(&fixture, &fixture_snapshot, sizeof(fixture)) == 0
    );
    return true;
}

static bool activate_child(
    struct micros_thread_handle *thread
)
{
    struct micros_pm_control_prepare_plan prepare;
    struct micros_pm_control_execution_binding binding;
    struct micros_pm_control_activate_plan activate;

    if (
        !initialize_fixture()
        || micros_pm_control_prepare_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            fixture.reservation_token,
            fixture.load_token,
            fixture.mapping_generation,
            &prepare
        ) != MICROS_PM_CONTROL_OK
    ) {
        return false;
    }
    binding = execution_binding(&prepare);
    micros_pm_control_prepare_commit_prevalidated(
        &fixture.control,
        &fixture.objects,
        &fixture.endpoints,
        &fixture.vm,
        &prepare,
        &binding
    );
    if (
        micros_pm_control_activate_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            fixture.reservation_token,
            fixture.load_token,
            fixture.mapping_generation,
            prepare.prepared_token,
            &activate
        ) != MICROS_PM_CONTROL_OK
    ) {
        return false;
    }
    micros_pm_control_activate_commit_prevalidated(
        &fixture.control,
        &fixture.objects,
        &fixture.endpoints,
        &fixture.vm,
        &activate
    );
    if (
        micros_vm_load_commit_preflight(
            &fixture.vm,
            fixture.load_token,
            fixture.mapping_generation
        ) != MICROS_VM_HANDOFF_OK
    ) {
        return false;
    }
    micros_vm_load_commit_commit_prevalidated(&fixture.vm);
    if (
        micros_pm_control_runtime_validate_reservation(
            &fixture.control,
            &fixture.objects,
            &fixture.ownership,
            &fixture.vm
        ) != MICROS_PM_CONTROL_OK
    ) {
        return false;
    }
    *thread = activate.thread;
    return true;
}

static bool block_child_in_exit_call(
    struct micros_thread_handle thread_handle
)
{
    struct micros_thread *thread =
        &fixture.objects.threads[thread_handle.slot];
    struct micros_hart *hart =
        &fixture.objects.harts[fixture.hart.slot];

    if (
        !thread->ready_linked
        || hart->ready_head[
            MICROS_APPLICATION_SCHEDULER_PRIORITY
        ].slot != thread_handle.slot
        || hart->ready_tail[
            MICROS_APPLICATION_SCHEDULER_PRIORITY
        ].slot != thread_handle.slot
    ) {
        return false;
    }
    hart->ready_head[MICROS_APPLICATION_SCHEDULER_PRIORITY] =
        (struct micros_thread_handle){0, 0};
    hart->ready_tail[MICROS_APPLICATION_SCHEDULER_PRIORITY] =
        (struct micros_thread_handle){0, 0};
    thread->ready_linked = false;
    thread->runtime_flags = MICROS_THREAD_RTS_IPC_REPLY;
    thread->ipc_receive_buffer = MICROS_APPLICATION_STACK_BASE;
    thread->ipc_reply_token = 1;
    thread->ipc_reply_callee = fixture.pm_endpoint;
    fixture.endpoints.last_reply_token = 1;
    return (
        micros_kernel_objects_validate(&fixture.objects)
            == MICROS_KERNEL_OBJECT_OK
        && micros_endpoint_registry_validate_objects(
            &fixture.endpoints,
            &fixture.objects
        ) == MICROS_ENDPOINT_OK
    );
}

static bool test_exit_begin_detach_release(void)
{
    struct micros_pm_control_exit_begin_plan begin;
    struct micros_pm_control_exit_begin_plan begin_sentinel;
    struct micros_pm_control_exit_detach_plan detach;
    struct micros_pm_control_release_exited_plan release;
    struct micros_vm_exit_release_action vm_release;
    struct micros_frame_vm_process_release_plan ownership_release;
    struct micros_frame_owner owner;
    struct micros_thread_handle thread;
    struct micros_process_handle child;
    const struct micros_process *stale_process = NULL;
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    uint64_t exit_token;
    size_t index;

    EXPECT_TRUE(
        activate_child(&thread)
        && block_child_in_exit_call(thread)
    );
    child = fixture.child;
    memset(&begin_sentinel, 0xa5, sizeof(begin_sentinel));
    begin = begin_sentinel;
    EXPECT_TRUE(
        micros_pm_control_exit_begin_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            process_endpoint(fixture.pm_process),
            &begin
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && memcmp(&begin, &begin_sentinel, sizeof(begin)) == 0
        && micros_pm_control_exit_begin_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            process_endpoint(child),
            &begin
        ) == MICROS_PM_CONTROL_OK
        && begin.process.slot == child.slot
        && begin.thread.slot == thread.slot
        && begin.exit_token == 1
        && begin.load_token == fixture.load_token
        && begin.mapping_generation == fixture.mapping_generation
        && begin.result.version == MICROS_PM_EXIT_BEGIN_VERSION
        && begin.result.size == MICROS_PM_EXIT_BEGIN_SIZE
        && begin.result.exit_token == begin.exit_token
    );
    exit_token = begin.exit_token;
    micros_pm_control_exit_begin_commit_prevalidated(
        &fixture.control,
        &fixture.objects,
        &fixture.endpoints,
        &begin
    );
    EXPECT_TRUE(
        fixture.control.exit.phase
            == MICROS_PM_CONTROL_EXIT_STOPPED
        && fixture.endpoints.endpoints[child.slot].state
            == MICROS_ENDPOINT_STATE_SOURCE_ONLY
        && fixture.objects.threads[thread.slot].runtime_flags
            == (
                MICROS_THREAD_RTS_INACTIVE
                | MICROS_THREAD_RTS_IPC_REPLY
            )
        && micros_pm_control_state_validate_objects(
            &fixture.control,
            &fixture.objects
        ) == MICROS_PM_CONTROL_OK
        && micros_endpoint_registry_validate_objects(
            &fixture.endpoints,
            &fixture.objects
        ) == MICROS_ENDPOINT_OK
        && micros_pm_control_runtime_validate_reservation(
            &fixture.control,
            &fixture.objects,
            &fixture.ownership,
            &fixture.vm
        ) == MICROS_PM_CONTROL_OK
    );
    EXPECT_TRUE(
        micros_pm_control_exit_detach_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            exit_token + 1,
            process_endpoint(child),
            &detach
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && micros_pm_control_exit_detach_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.endpoints,
            &fixture.vm,
            exit_token,
            process_endpoint(child),
            &detach
        ) == MICROS_PM_CONTROL_OK
        && micros_ipc_endpoint_close(
            &fixture.endpoints,
            &fixture.objects,
            process_endpoint(child)
        ) == MICROS_IPC_OK
    );
    micros_pm_control_exit_detach_commit_prevalidated(
        &fixture.control,
        &fixture.objects,
        &fixture.vm,
        &detach
    );
    EXPECT_TRUE(
        fixture.control.exit.phase
            == MICROS_PM_CONTROL_EXIT_DETACHED
        && fixture.vm.mappings[child.slot].phase
            == MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED
        && fixture.vm.mappings[child.slot].exit_token == exit_token
        && fixture.objects.processes[child.slot].live_thread_count == 0
        && fixture.objects.processes[child.slot].primary_endpoint
            == MICROS_PROCESS_ENDPOINT_NONE
        && fixture.objects.processes[child.slot].privilege_profile == 0
        && fixture.objects.processes[child.slot]
            .endpoint_lifecycle_consumed
        && fixture.objects.threads[thread.slot].slot_state
            == MICROS_KERNEL_OBJECT_SLOT_FREE
        && micros_pm_control_state_validate_objects(
            &fixture.control,
            &fixture.objects
        ) == MICROS_PM_CONTROL_OK
        && micros_pm_control_runtime_validate_reservation(
            &fixture.control,
            &fixture.objects,
            &fixture.ownership,
            &fixture.vm
        ) == MICROS_PM_CONTROL_OK
    );
    owner = fixture.ownership.owners[0];
    ++fixture.ownership.owners[0].generation;
    EXPECT_TRUE(
        micros_pm_control_runtime_validate_reservation(
            &fixture.control,
            &fixture.objects,
            &fixture.ownership,
            &fixture.vm
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
    );
    fixture.ownership.owners[0] = owner;
    EXPECT_TRUE(
        micros_pm_control_runtime_validate_reservation(
            &fixture.control,
            &fixture.objects,
            &fixture.ownership,
            &fixture.vm
        ) == MICROS_PM_CONTROL_OK
    );
    EXPECT_TRUE(
        micros_vm_exit_release_preflight(
            &fixture.vm,
            exit_token,
            fixture.load_token,
            fixture.mapping_generation,
            &vm_release
        ) == MICROS_VM_HANDOFF_OK
    );
    for (index = 0; index < vm_release.entry_count; ++index) {
        data_frames[index] =
            vm_release.entries[index].physical_address;
    }
    EXPECT_TRUE(
        micros_frame_ownership_vm_release_process_preflight(
            &fixture.ownership,
            child,
            data_frames,
            vm_release.entry_count,
            vm_release.table_frames,
            &ownership_release
        ) == MICROS_FRAME_OWNERSHIP_OK
    );
    fixture.objects.processes[child.slot].address_space_root = 0;
    micros_frame_ownership_vm_release_process_commit_prevalidated(
        &fixture.ownership,
        &ownership_release
    );
    micros_vm_exit_release_commit_prevalidated(
        &fixture.vm,
        child
    );
    EXPECT_TRUE(
        micros_pm_control_runtime_validate_reservation(
            &fixture.control,
            &fixture.objects,
            &fixture.ownership,
            &fixture.vm
        ) == MICROS_PM_CONTROL_OK
        &&
        micros_pm_control_release_exited_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.vm,
            exit_token + 1,
            &release
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && micros_pm_control_release_exited_preflight(
            &fixture.control,
            &fixture.objects,
            &fixture.vm,
            exit_token,
            &release
        ) == MICROS_PM_CONTROL_OK
    );
    micros_pm_control_release_exited_commit_prevalidated(
        &fixture.control,
        &fixture.objects,
        &release
    );
    EXPECT_TRUE(
        fixture.control.exit.phase == MICROS_PM_CONTROL_EXIT_NONE
        && fixture.control.last_exit_token == exit_token
        && fixture.objects.processes[child.slot].slot_state
            == MICROS_KERNEL_OBJECT_SLOT_FREE
        && fixture.objects.processes[child.slot].generation
            == child.generation
        && micros_process_resolve(
            &fixture.objects,
            child,
            &stale_process
        ) == MICROS_KERNEL_OBJECT_ERROR_STALE
        && stale_process == NULL
        && micros_pm_control_state_validate_objects(
            &fixture.control,
            &fixture.objects
        ) == MICROS_PM_CONTROL_OK
        && micros_kernel_objects_validate(&fixture.objects)
            == MICROS_KERNEL_OBJECT_OK
        && micros_endpoint_registry_validate_objects(
            &fixture.endpoints,
            &fixture.objects
        ) == MICROS_ENDPOINT_OK
        && micros_vm_handoff_state_validate(&fixture.vm)
            == MICROS_VM_HANDOFF_OK
    );
    return true;
}

bool micros_pm_control_execution_test_run(void)
{
    return (
        test_prepare_abort_activate()
        && test_prepared_corruption_is_invariant()
        && test_token_exhaustion_is_invariant()
        && test_exit_begin_detach_release()
    );
}
