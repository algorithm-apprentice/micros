#include "tests/host/pm_control_test_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/pm_control_runtime_core.h"
#include "micros/endpoint.h"
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

struct pm_control_runtime_scenario {
    struct pm_control_test_context *context;
    struct micros_kernel_objects objects;
    struct micros_pm_control_state state;
    struct micros_pm_control_reserve_plan reserve;
    struct micros_vm_load_freeze_state_plan vm_freeze;
    uint64_t data_frames[4];
    uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
};

static bool initialize_reserved_runtime(
    struct pm_control_runtime_scenario *scenario
)
{
    const uint64_t frame_base = UINT64_C(0x00100000);
    struct micros_physical_range range = {
        .base = frame_base,
        .size = 8 * MICROS_FRAME_SIZE,
    };

    EXPECT_TRUE(scenario->context != NULL);
    EXPECT_TRUE(
        pm_control_test_initialize_fixture(
            &scenario->objects,
            &scenario->state
        )
        && micros_pm_control_reserve_preflight(
            &scenario->state,
            &scenario->objects,
            &scenario->reserve
        ) == MICROS_PM_CONTROL_OK
    );
    micros_pm_control_reserve_commit_prevalidated(
        &scenario->state,
        &scenario->objects,
        &scenario->reserve
    );
    memset(
        &scenario->context->runtime_allocator,
        0,
        sizeof(scenario->context->runtime_allocator)
    );
    memset(
        &scenario->context->runtime_ownership,
        0,
        sizeof(scenario->context->runtime_ownership)
    );
    EXPECT_TRUE(
        micros_frame_allocator_initialize(
            &scenario->context->runtime_allocator,
            &range,
            1,
            NULL,
            0
        ) == MICROS_FRAME_ALLOCATOR_OK
        && micros_frame_ownership_initialize(
            &scenario->context->runtime_ownership,
            &scenario->context->runtime_allocator
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_frame_ownership_complete_handoff(
            &scenario->context->runtime_ownership
        ) == MICROS_FRAME_OWNERSHIP_OK
        && micros_pm_control_runtime_validate_reservation(
            &scenario->state,
            &scenario->objects,
            &scenario->context->runtime_ownership,
            NULL
        ) == MICROS_PM_CONTROL_OK
    );
    return true;
}

static bool prepare_handed_off_vm(
    struct pm_control_runtime_scenario *scenario
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
    struct micros_process_handle vm_process = {
        .slot = 1,
        .generation = scenario->objects.processes[1].generation,
    };
    struct micros_thread_handle vm_thread = {
        .slot = 4,
        .generation = 9,
    };

    memset(
        &scenario->context->runtime_vm,
        0,
        sizeof(scenario->context->runtime_vm)
    );
    EXPECT_TRUE(
        micros_vm_handoff_state_prepare(
            &scenario->context->runtime_vm,
            2,
            pm_control_test_process_endpoint(vm_process),
            1,
            MICROS_PRIVILEGE_PROFILE_VM,
            vm_process,
            vm_thread,
            UINT64_C(0x00200000),
            UINT64_C(0x00300000),
            MICROS_VM_BOOT_INFO_SIZE,
            &summary
        ) == MICROS_VM_HANDOFF_OK
    );
    micros_vm_handoff_commit_prevalidated(
        &scenario->context->runtime_vm
    );
    return true;
}

static bool begin_runtime_load(
    struct pm_control_runtime_scenario *scenario
)
{
    struct micros_pm_control_state state_snapshot = scenario->state;
    struct micros_vm_load_begin_plan begin;

    state_snapshot.active_transaction = 0;
    state_snapshot.reserved_process =
        (struct micros_process_handle){0};
    EXPECT_TRUE(
        micros_pm_control_runtime_reserve_preflight(
            &state_snapshot,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_OK
        && micros_pm_control_runtime_reserve_preflight(
            &scenario->state,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && micros_pm_control_runtime_abort_preflight(
            &scenario->state,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_OK
        && micros_vm_load_begin_preflight(
            &scenario->context->runtime_vm,
            scenario->state.active_transaction,
            scenario->reserve.process,
            true,
            &begin
        ) == MICROS_VM_HANDOFF_OK
    );
    micros_vm_load_begin_commit_prevalidated(
        &scenario->context->runtime_vm,
        &begin
    );
    EXPECT_TRUE(
        micros_pm_control_runtime_validate_reservation(
            &scenario->state,
            &scenario->objects,
            &scenario->context->runtime_ownership,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_OK
        && micros_pm_control_runtime_reserve_preflight(
            &scenario->state,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && micros_pm_control_runtime_abort_preflight(
            &scenario->state,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_ERROR_STATE
    );
    return true;
}

static bool append_runtime_pages(
    struct pm_control_runtime_scenario *scenario
)
{
    const uint64_t frame_base = UINT64_C(0x00100000);
    const uint32_t permissions[] = {
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_EXECUTE,
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ,
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE,
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE,
    };
    const uint64_t virtual_addresses[] = {
        MICROS_APPLICATION_ENTRY,
        MICROS_APPLICATION_ENTRY + MICROS_SV39_PAGE_SIZE,
        MICROS_APPLICATION_ENTRY + 2 * MICROS_SV39_PAGE_SIZE,
        MICROS_APPLICATION_STACK_BASE,
    };
    size_t index;

    for (index = 0; index < 4; ++index) {
        struct micros_frame_vm_claim_plan claim;
        struct micros_vm_scratch_plan scratch;
        struct micros_vm_scratch_map_request map_request = {
            .load_token =
                scenario->context->runtime_vm.load.load_token,
            .virtual_address = virtual_addresses[index],
            .physical_address =
                frame_base + index * MICROS_FRAME_SIZE,
            .permissions = permissions[index],
        };
        struct micros_vm_scratch_commit_request commit_request = {
            .load_token =
                scenario->context->runtime_vm.load.load_token,
            .virtual_address = virtual_addresses[index],
            .physical_address = map_request.physical_address,
        };

        scenario->data_frames[index] =
            map_request.physical_address;
        EXPECT_TRUE(
            micros_frame_ownership_vm_claim_preflight(
                &scenario->context->runtime_ownership,
                map_request.physical_address,
                &claim
            ) == MICROS_FRAME_OWNERSHIP_OK
            && micros_vm_scratch_map_preflight(
                &scenario->context->runtime_vm,
                &map_request,
                &scratch
            ) == MICROS_VM_HANDOFF_OK
        );
        micros_frame_ownership_vm_claim_commit_prevalidated(
            &scenario->context->runtime_ownership,
            &claim
        );
        micros_vm_scratch_map_commit_prevalidated(
            &scenario->context->runtime_vm,
            &scratch
        );
        EXPECT_TRUE(
            micros_vm_scratch_commit_preflight(
                &scenario->context->runtime_vm,
                &commit_request,
                &scratch
            ) == MICROS_VM_HANDOFF_OK
        );
        micros_vm_scratch_commit_commit_prevalidated(
            &scenario->context->runtime_vm,
            &scratch
        );
    }
    return true;
}

static bool freeze_runtime_mapping(
    struct pm_control_runtime_scenario *scenario
)
{
    const uint64_t frame_base = UINT64_C(0x00100000);
    struct micros_frame_vm_freeze_plan ownership_freeze;
    struct micros_vm_load_freeze_plan request;
    size_t index;

    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        scenario->table_frames[index] =
            frame_base + (4 + index) * MICROS_FRAME_SIZE;
    }
    request = (struct micros_vm_load_freeze_plan){
        .version = MICROS_VM_LOAD_FREEZE_PLAN_VERSION,
        .size = MICROS_VM_LOAD_FREEZE_PLAN_SIZE,
        .load_token =
            scenario->context->runtime_vm.load.load_token,
        .table_frames = {
            scenario->table_frames[0],
            scenario->table_frames[1],
            scenario->table_frames[2],
            scenario->table_frames[3],
        },
        .entry = MICROS_APPLICATION_ENTRY,
        .stack_pointer = MICROS_APPLICATION_STACK_TOP - 16,
        .image_page_count = 3,
        .data_page_count = 4,
    };
    request.page_plan_digest = micros_vm_load_page_plan_digest(
        request.image_page_count,
        request.data_page_count,
        scenario->context->runtime_vm
            .mappings[scenario->reserve.process.slot].entries
    );
    EXPECT_TRUE(
        micros_vm_load_freeze_preflight(
            &scenario->context->runtime_vm,
            &request,
            &scenario->vm_freeze
        ) == MICROS_VM_HANDOFF_OK
        && micros_frame_ownership_vm_freeze_preflight(
            &scenario->context->runtime_ownership,
            scenario->reserve.process,
            scenario->data_frames,
            4,
            scenario->table_frames,
            &ownership_freeze
        ) == MICROS_FRAME_OWNERSHIP_OK
    );
    micros_frame_ownership_vm_freeze_commit_prevalidated(
        &scenario->context->runtime_ownership,
        &ownership_freeze
    );
    scenario->objects.processes[
        scenario->reserve.process.slot
    ].address_space_root = scenario->table_frames[0];
    micros_vm_load_freeze_commit_prevalidated(
        &scenario->context->runtime_vm,
        &scenario->vm_freeze
    );
    return true;
}

static bool validate_frozen_runtime(
    struct pm_control_runtime_scenario *scenario
)
{
    struct micros_pm_control_state state_snapshot;
    struct micros_process *process =
        &scenario->objects.processes[
            scenario->reserve.process.slot
        ];

    EXPECT_TRUE(
        micros_pm_control_state_validate_objects(
            &scenario->state,
            &scenario->objects
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
        && micros_pm_control_runtime_validate_reservation(
            &scenario->state,
            &scenario->objects,
            &scenario->context->runtime_ownership,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_OK
        && micros_pm_control_runtime_reserve_preflight(
            &scenario->state,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && micros_pm_control_runtime_abort_preflight(
            &scenario->state,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_ERROR_STATE
    );
    process->address_space_root += MICROS_FRAME_SIZE;
    EXPECT_TRUE(
        micros_pm_control_runtime_validate_reservation(
            &scenario->state,
            &scenario->objects,
            &scenario->context->runtime_ownership,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
    );
    process->address_space_root = scenario->table_frames[0];
    state_snapshot = scenario->state;
    state_snapshot.active_transaction = 0;
    state_snapshot.reserved_process =
        (struct micros_process_handle){0};
    EXPECT_TRUE(
        micros_pm_control_runtime_validate_reservation(
            &state_snapshot,
            &scenario->objects,
            &scenario->context->runtime_ownership,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
        && micros_pm_control_runtime_abort_preflight(
            &state_snapshot,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
    );
    return true;
}

static bool validate_activated_pending_runtime(
    struct pm_control_runtime_scenario *scenario
)
{
    EXPECT_TRUE(
        micros_vm_load_mark_prepared(
            &scenario->context->runtime_vm,
            scenario->reserve.process,
            scenario->state.active_transaction,
            scenario->context->runtime_vm.load.load_token,
            scenario->vm_freeze.mapping_generation
        ) == MICROS_VM_HANDOFF_OK
        && micros_vm_load_mark_activated(
            &scenario->context->runtime_vm,
            scenario->reserve.process,
            scenario->state.active_transaction,
            scenario->context->runtime_vm.load.load_token,
            scenario->vm_freeze.mapping_generation
        ) == MICROS_VM_HANDOFF_OK
    );
    scenario->state.active_transaction = 0;
    scenario->state.reserved_process =
        (struct micros_process_handle){0};
    EXPECT_TRUE(
        micros_pm_control_runtime_validate_reservation(
            &scenario->state,
            &scenario->objects,
            &scenario->context->runtime_ownership,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_OK
        && micros_pm_control_runtime_reserve_preflight(
            &scenario->state,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && micros_pm_control_runtime_abort_preflight(
            &scenario->state,
            &scenario->context->runtime_vm
        ) == MICROS_PM_CONTROL_ERROR_STATE
    );
    return true;
}

bool test_runtime_accepts_exact_frozen_vm_reservation(
    struct pm_control_test_context *context
)
{
    struct pm_control_runtime_scenario scenario = {
        .context = context,
    };

    return (
        initialize_reserved_runtime(&scenario)
        && prepare_handed_off_vm(&scenario)
        && begin_runtime_load(&scenario)
        && append_runtime_pages(&scenario)
        && freeze_runtime_mapping(&scenario)
        && validate_frozen_runtime(&scenario)
        && validate_activated_pending_runtime(&scenario)
    );
}
