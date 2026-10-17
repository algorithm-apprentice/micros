#include "tests/host/pm_control_execution_test_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/pm_control_runtime_core.h"
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

static struct pm_control_execution_test_context test_context;

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

struct pm_control_execution_test_context *
pm_control_execution_test_context_get(void)
{
    return &test_context;
}

uint32_t pm_control_execution_test_process_endpoint(
    struct micros_process_handle process
)
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

static bool claim_child_data_frames(
    struct pm_control_execution_test_context *context,
    uint64_t frame_base,
    uint64_t data_frames[4]
)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        struct micros_frame_vm_claim_plan claim;

        data_frames[index] =
            frame_base + index * MICROS_SV39_PAGE_SIZE;
        EXPECT_TRUE(
            micros_frame_ownership_vm_claim_preflight(
                &context->ownership,
                data_frames[index],
                &claim
            ) == MICROS_FRAME_OWNERSHIP_OK
        );
        micros_frame_ownership_vm_claim_commit_prevalidated(
            &context->ownership,
            &claim
        );
    }
    return true;
}

static bool append_child_pages(
    struct pm_control_execution_test_context *context,
    const uint64_t data_frames[4]
)
{
    return (
        append_page(
            &context->vm,
            MICROS_APPLICATION_ENTRY,
            data_frames[0],
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
        )
        && append_page(
            &context->vm,
            MICROS_APPLICATION_ENTRY + MICROS_SV39_PAGE_SIZE,
            data_frames[1],
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
        )
        && append_page(
            &context->vm,
            MICROS_APPLICATION_ENTRY + 2 * MICROS_SV39_PAGE_SIZE,
            data_frames[2],
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
        )
        && append_page(
            &context->vm,
            MICROS_APPLICATION_STACK_BASE,
            data_frames[3],
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
        )
    );
}

static bool freeze_child(
    struct pm_control_execution_test_context *context
)
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
    struct micros_frame_vm_freeze_plan ownership_freeze;
    uint64_t data_frames[4];

    EXPECT_TRUE(
        micros_vm_load_begin_preflight(
            &context->vm,
            context->reservation_token,
            context->child,
            true,
            &begin
        ) == MICROS_VM_HANDOFF_OK
    );
    micros_vm_load_begin_commit_prevalidated(&context->vm, &begin);
    EXPECT_TRUE(
        claim_child_data_frames(context, frame_base, data_frames)
        && append_child_pages(context, data_frames)
    );
    request.load_token = context->vm.load.load_token;
    request.page_plan_digest = micros_vm_load_page_plan_digest(
        request.image_page_count,
        request.data_page_count,
        context->vm.mappings[context->child.slot].entries
    );
    EXPECT_TRUE(
        micros_vm_load_freeze_preflight(
            &context->vm,
            &request,
            &freeze
        ) == MICROS_VM_HANDOFF_OK
        && micros_frame_ownership_vm_freeze_preflight(
            &context->ownership,
            context->child,
            data_frames,
            4,
            request.table_frames,
            &ownership_freeze
        ) == MICROS_FRAME_OWNERSHIP_OK
    );
    micros_frame_ownership_vm_freeze_commit_prevalidated(
        &context->ownership,
        &ownership_freeze
    );
    context->objects.processes[
        context->child.slot
    ].address_space_root = request.table_frames[0];
    micros_vm_load_freeze_commit_prevalidated(
        &context->vm,
        &freeze
    );
    context->load_token = context->vm.load.load_token;
    context->mapping_generation = freeze.mapping_generation;
    return (
        micros_pm_control_runtime_validate_reservation(
            &context->control,
            &context->objects,
            &context->ownership,
            &context->vm
        ) == MICROS_PM_CONTROL_OK
    );
}

static bool initialize_object_and_memory_fixture(
    struct pm_control_execution_test_context *context,
    struct micros_process_handle *vm_process,
    struct micros_process_handle *pm_process,
    struct micros_thread_handle *pm_thread
)
{
    struct micros_process_handle process;
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
    EXPECT_TRUE(
        micros_kernel_objects_initialize(
            &context->objects,
            1,
            1
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_process_create_at(
            &context->objects,
            0,
            &process
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_process_create_at(
            &context->objects,
            1,
            vm_process
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_process_create_at(
            &context->objects,
            2,
            pm_process
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_create(
            &context->objects,
            *pm_process,
            pm_thread
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_hart_register(
            &context->objects,
            0,
            &context->hart
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_frame_allocator_initialize(
            &context->allocator,
            ranges,
            sizeof(ranges) / sizeof(ranges[0]),
            NULL,
            0
        ) == MICROS_FRAME_ALLOCATOR_OK
        && micros_frame_ownership_initialize(
            &context->ownership,
            &context->allocator
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_complete_handoff(
            &context->ownership
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_endpoint_registry_initialize(
            &context->endpoints,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) == MICROS_ENDPOINT_OK
    );
    return true;
}

static bool initialize_pm_fixture(
    struct pm_control_execution_test_context *context,
    struct micros_process_handle pm_process,
    struct micros_thread_handle pm_thread
)
{
    struct micros_pm_control_reserve_plan reserve;
    uint32_t endpoint;

    EXPECT_TRUE(
        micros_endpoint_reserve(
            &context->endpoints,
            &context->objects,
            pm_process,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint == pm_control_execution_test_process_endpoint(pm_process)
        && micros_endpoint_install_profile(
            &context->endpoints,
            &context->objects,
            pm_process,
            MICROS_PRIVILEGE_PROFILE_PM
        ) == MICROS_ENDPOINT_OK
        && micros_endpoint_activate(
            &context->endpoints,
            &context->objects,
            endpoint
        ) == MICROS_ENDPOINT_OK
        && micros_pm_control_state_prepare(
            &context->control,
            3,
            endpoint,
            MICROS_PRIVILEGE_PROFILE_PM,
            pm_process,
            pm_thread
        ) == MICROS_PM_CONTROL_OK
        && micros_pm_control_reserve_preflight(
            &context->control,
            &context->objects,
            &reserve
        ) == MICROS_PM_CONTROL_OK
    );
    micros_pm_control_reserve_commit_prevalidated(
        &context->control,
        &context->objects,
        &reserve
    );
    context->child = reserve.process;
    context->pm_process = pm_process;
    context->pm_thread = pm_thread;
    context->pm_endpoint = endpoint;
    context->reservation_token = reserve.transaction;
    return true;
}

static bool initialize_vm_fixture(
    struct pm_control_execution_test_context *context,
    struct micros_process_handle vm_process
)
{
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

    EXPECT_TRUE(
        micros_vm_handoff_state_prepare(
            &context->vm,
            2,
            pm_control_execution_test_process_endpoint(vm_process),
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
    micros_vm_handoff_commit_prevalidated(&context->vm);
    return true;
}

bool pm_control_execution_test_initialize_fixture(
    struct pm_control_execution_test_context *context
)
{
    struct micros_process_handle vm_process;
    struct micros_process_handle pm_process;
    struct micros_thread_handle pm_thread;

    memset(context, 0, sizeof(*context));
    return (
        initialize_object_and_memory_fixture(
            context,
            &vm_process,
            &pm_process,
            &pm_thread
        )
        && initialize_pm_fixture(context, pm_process, pm_thread)
        && initialize_vm_fixture(context, vm_process)
        && freeze_child(context)
    );
}

struct micros_pm_control_execution_binding
pm_control_execution_test_binding(
    const struct pm_control_execution_test_context *context,
    const struct micros_pm_control_prepare_plan *plan
)
{
    struct micros_pm_control_execution_binding binding;

    memset(&binding, 0, sizeof(binding));
    binding.thread = plan->thread;
    binding.hart = context->hart;
    binding.kernel_stack_bottom =
        (uintptr_t)&context->kernel_stack[0];
    binding.kernel_stack_top =
        binding.kernel_stack_bottom + sizeof(context->kernel_stack);
    binding.context.sp = MICROS_APPLICATION_STACK_TOP - 16;
    binding.context.sstatus =
        (UINT64_C(2) << 32) | MICROS_RISCV_SSTATUS_SPIE;
    binding.context.sepc = MICROS_APPLICATION_ENTRY;
    return binding;
}

bool pm_control_execution_test_activate_child(
    struct pm_control_execution_test_context *context,
    struct micros_thread_handle *thread
)
{
    struct micros_pm_control_prepare_plan prepare;
    struct micros_pm_control_execution_binding binding;
    struct micros_pm_control_activate_plan activate;

    if (
        !pm_control_execution_test_initialize_fixture(context)
        || micros_pm_control_prepare_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            context->reservation_token,
            context->load_token,
            context->mapping_generation,
            &prepare
        ) != MICROS_PM_CONTROL_OK
    ) {
        return false;
    }
    binding = pm_control_execution_test_binding(context, &prepare);
    micros_pm_control_prepare_commit_prevalidated(
        &context->control,
        &context->objects,
        &context->endpoints,
        &context->vm,
        &prepare,
        &binding
    );
    if (
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
        ) != MICROS_PM_CONTROL_OK
    ) {
        return false;
    }
    micros_pm_control_activate_commit_prevalidated(
        &context->control,
        &context->objects,
        &context->endpoints,
        &context->vm,
        &activate
    );
    if (
        micros_vm_load_commit_preflight(
            &context->vm,
            context->load_token,
            context->mapping_generation
        ) != MICROS_VM_HANDOFF_OK
    ) {
        return false;
    }
    micros_vm_load_commit_commit_prevalidated(&context->vm);
    if (
        micros_pm_control_runtime_validate_reservation(
            &context->control,
            &context->objects,
            &context->ownership,
            &context->vm
        ) != MICROS_PM_CONTROL_OK
    ) {
        return false;
    }
    *thread = activate.thread;
    return true;
}

bool pm_control_execution_test_block_child_in_exit_call(
    struct pm_control_execution_test_context *context,
    struct micros_thread_handle thread_handle
)
{
    struct micros_thread *thread =
        &context->objects.threads[thread_handle.slot];
    struct micros_hart *hart =
        &context->objects.harts[context->hart.slot];

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
    thread->ipc_reply_callee = context->pm_endpoint;
    context->endpoints.last_reply_token = 1;
    return (
        micros_kernel_objects_validate(&context->objects)
            == MICROS_KERNEL_OBJECT_OK
        && micros_endpoint_registry_validate_objects(
            &context->endpoints,
            &context->objects
        ) == MICROS_ENDPOINT_OK
    );
}
