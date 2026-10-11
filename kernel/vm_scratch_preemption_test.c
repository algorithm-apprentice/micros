#include "kernel/vm_scratch_preemption_test.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/kernel_object_runtime_internal.h"
#include "kernel/pm_control_runtime.h"
#include "kernel/vm_handoff_runtime.h"
#include "kernel/vm_scratch_preemption_test_fixture.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/vm.h"

enum {
    MICROS_TEST_EXCEPTION_BREAKPOINT = 3,
    MICROS_TEST_EXCEPTION_LOAD_PAGE_FAULT = 13,
};

enum scratch_preemption_phase {
    SCRATCH_PREEMPTION_IDLE = 0,
    SCRATCH_PREEMPTION_WAITING_PEER_HOLD,
    SCRATCH_PREEMPTION_PEER_HELD,
    SCRATCH_PREEMPTION_WAITING_TIMER,
    SCRATCH_PREEMPTION_PEER_PREEMPTED,
    SCRATCH_PREEMPTION_WAITING_VM_REACTIVATION,
    SCRATCH_PREEMPTION_VM_REACTIVATED,
    SCRATCH_PREEMPTION_WAITING_STALE_FAULT,
};

static const struct micros_bootstrap_expected_service
    expected_services[] = {
        {
            .service_id =
                MICROS_VM_SCRATCH_PREEMPTION_LAUNCHER_SERVICE_ID,
            .image_id = 101,
            .process_slot = 0,
            .profile_id =
                MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
            .service_name = "bootstrap-launcher",
            .profile_name = "BOOTSTRAP_LAUNCHER",
            .role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER,
        },
        {
            .service_id =
                MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID,
            .image_id = 102,
            .process_slot = 1,
            .profile_id = MICROS_PRIVILEGE_PROFILE_VM,
            .service_name = "vm",
            .profile_name = "VM",
            .prerequisites = UINT64_C(1),
            .call_targets =
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
            .role_flags = MICROS_BOOTSTRAP_ROLE_VM,
        },
        {
            .service_id =
                MICROS_VM_SCRATCH_PREEMPTION_PM_SERVICE_ID,
            .image_id = 103,
            .process_slot = 2,
            .profile_id = MICROS_PRIVILEGE_PROFILE_PM,
            .service_name = "pm",
            .profile_name = "PM",
            .prerequisites =
                UINT64_C(1)
                << (
                    MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID
                    - 1
                ),
            .call_targets =
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
            .role_flags = MICROS_BOOTSTRAP_ROLE_PM,
        },
        {
            .service_id =
                MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID,
            .image_id = 104,
            .process_slot = 3,
            .profile_id =
                MICROS_VM_SCRATCH_PREEMPTION_DRIVER_PROFILE_ID,
            .service_name = "vm-scratch-driver",
            .profile_name = "VM_SCRATCH_DRIVER",
            .prerequisites =
                UINT64_C(1)
                << (
                    MICROS_VM_SCRATCH_PREEMPTION_PM_SERVICE_ID
                    - 1
                ),
            .call_targets =
                (
                    UINT32_C(1)
                    << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
                ) | (
                    UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VM
                ) | (
                    UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_PM
                ),
        },
    };

static const struct micros_privilege_profile profiles[] = {
    {
        .id = MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
        .name = "BOOTSTRAP_LAUNCHER",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_REPLY,
        .kernel_operations =
            MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_VM,
        .name = "VM",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY,
        .call_targets =
            UINT32_C(1)
            << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
        .kernel_operations = MICROS_KERNEL_OPERATION_VM_HANDOFF,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_PM,
        .name = "PM",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        .call_targets =
            UINT32_C(1)
            << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
        .kernel_operations = MICROS_KERNEL_OPERATION_PM_CONTROL,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_TTY,
        .name = "TTY",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_NOTIFY,
        .call_targets =
            UINT32_C(1)
            << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
        .notify_targets =
            UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS,
        .kernel_operations = MICROS_KERNEL_OPERATION_TTY_CONTROL,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_RAMFS,
        .name = "RAMFS",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        .call_targets =
            UINT32_C(1)
            << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
    },
    {
        .id = MICROS_PRIVILEGE_PROFILE_VFS,
        .name = "VFS",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        .call_targets =
            (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
            ) | (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_TTY
            ) | (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_RAMFS
            ),
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
    {
        .id = MICROS_VM_SCRATCH_PREEMPTION_DRIVER_PROFILE_ID,
        .name = "VM_SCRATCH_DRIVER",
        .operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL,
        .call_targets =
            (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
            ) | (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VM
            ) | (
                UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_PM
            ),
    },
};

static const struct micros_bootstrap_scheduler_policy policies[] = {
    {
        .service_id =
            MICROS_VM_SCRATCH_PREEMPTION_LAUNCHER_SERVICE_ID,
        .priority = 4,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id =
            MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id =
            MICROS_VM_SCRATCH_PREEMPTION_PM_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id =
            MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
};

static const struct micros_bootstrap_runtime_config runtime_config = {
    .manifest = &micros_vm_scratch_preemption_test_manifest,
    .expected_services = expected_services,
    .expected_service_count =
        sizeof(expected_services) / sizeof(expected_services[0]),
    .images = micros_vm_scratch_preemption_test_images,
    .image_count =
        sizeof(micros_vm_scratch_preemption_test_images)
            / sizeof(micros_vm_scratch_preemption_test_images[0]),
    .profiles = profiles,
    .profile_count = sizeof(profiles) / sizeof(profiles[0]),
    .policies = policies,
    .policy_count = sizeof(policies) / sizeof(policies[0]),
    .scheduler_preemption_interval =
        MICROS_VM_SCRATCH_PREEMPTION_SCHEDULER_INTERVAL,
};

static volatile enum scratch_preemption_phase test_phase;
static bool timer_observed;
static uint64_t scratch_physical_address;
static uint64_t scratch_load_token;
static struct micros_process_handle scratch_process;

static _Noreturn void test_failure(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
)
{
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        reason,
        frame
    );
}

static uint64_t read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

static uint64_t expected_satp(uint64_t root)
{
    return MICROS_RISCV_SATP_MODE_SV39 | (root >> 12);
}

static bool current_is(
    const struct micros_hart *hart,
    const struct micros_bootstrap_binding *binding
)
{
    return (
        hart != NULL
        && binding != NULL
        && hart->current_thread.slot == binding->thread.slot
        && hart->current_thread.generation
            == binding->thread.generation
    );
}

static bool scratch_runtime_is_valid(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_vm_handoff_state *handoff,
    const struct micros_pm_control_state *control,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    return (
        bootstrap != NULL
        && handoff != NULL
        && control != NULL
        && registry != NULL
        && objects != NULL
        && bootstrap->phase == MICROS_BOOTSTRAP_PHASE_SEALED
        && handoff->phase == MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        && handoff->load.phase == MICROS_VM_LOAD_PHASE_LOADING
        && handoff->load.scratch_phase
            == MICROS_VM_SCRATCH_MAPPED
        && handoff->load.process_reservation_token
            == control->active_transaction
        && control->active_transaction != 0
        && handoff->load.load_token != 0
        && handoff->load.scratch.virtual_address
            == MICROS_USER_VIRTUAL_BASE
        && handoff->load.scratch.permissions
            == (
                MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
            )
        && handoff->load.scratch.physical_address != 0
        && micros_bootstrap_runtime_validate()
            == MICROS_BOOTSTRAP_OK
        && micros_vm_handoff_runtime_validate(
            bootstrap,
            registry,
            objects
        ) == MICROS_VM_HANDOFF_OK
        && micros_pm_control_runtime_validate(
            bootstrap,
            registry,
            objects
        ) == MICROS_PM_CONTROL_OK
        && micros_grant_runtime_validate() == MICROS_GRANT_OK
        && micros_frame_ownership_runtime_validate(objects)
            == MICROS_FRAME_OWNERSHIP_OK
    );
}

static _Noreturn void report_success(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();
    const struct micros_vm_handoff_state *handoff =
        micros_vm_handoff_runtime_state();
    const struct micros_pm_control_state *control =
        micros_pm_control_runtime_state();
    const struct micros_frame_ownership *ownership =
        micros_frame_ownership_runtime_ledger();
    const struct micros_endpoint_registry *registry =
        micros_ipc_runtime_registry();
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    const struct micros_bootstrap_binding *vm;
    const struct micros_vm_kernel_mapping_record *record;
    struct micros_frame_owner owner;
    uint64_t physical = UINT64_C(0x1122334455667788);
    uint32_t permissions = UINT32_C(0xa5a5a5a5);

    vm = bootstrap == NULL
        ? NULL
        : micros_bootstrap_control_find_binding(
            bootstrap,
            MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID
        );
    record = (
        handoff == NULL
        || scratch_process.slot >= MICROS_PROCESS_CAPACITY
    )
        ? NULL
        : &handoff->mappings[scratch_process.slot];
    if (
        test_phase != SCRATCH_PREEMPTION_WAITING_STALE_FAULT
        || hart == NULL
        || frame == NULL
        || bootstrap == NULL
        || handoff == NULL
        || control == NULL
        || ownership == NULL
        || registry == NULL
        || objects == NULL
        || vm == NULL
        || record == NULL
        || frame->scause != MICROS_TEST_EXCEPTION_LOAD_PAGE_FAULT
        || frame->sepc
            != micros_vm_scratch_preemption_stale_load_fault_address
        || frame->stval != MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
        || frame->a0 != MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
        || !current_is(hart, vm)
        || read_satp()
            != expected_satp(
                objects->processes[vm->process.slot]
                    .address_space_root
            )
        || handoff->load.phase != MICROS_VM_LOAD_PHASE_LOADING
        || handoff->load.scratch_phase != MICROS_VM_SCRATCH_CLEAR
        || handoff->load.load_token != scratch_load_token
        || handoff->load.process.slot != scratch_process.slot
        || handoff->load.process.generation
            != scratch_process.generation
        || control->active_transaction
            != handoff->load.process_reservation_token
        || control->reserved_process.slot != scratch_process.slot
        || control->reserved_process.generation
            != scratch_process.generation
        || record->phase != MICROS_VM_KERNEL_MAPPING_LOADING
        || record->load_token != scratch_load_token
        || record->entry_count != 1
        || record->entries[0].virtual_address
            != MICROS_USER_VIRTUAL_BASE
        || record->entries[0].physical_address
            != scratch_physical_address
        || record->entries[0].permissions
            != (
                MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
            )
        || micros_user_address_space_lookup(
            vm->process,
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            &physical,
            &permissions
        ) != MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED
        || physical != UINT64_C(0x1122334455667788)
        || permissions != UINT32_C(0xa5a5a5a5)
        || micros_user_address_space_validate(vm->process)
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_frame_ownership_lookup(
            ownership,
            scratch_physical_address,
            &owner
        ) != MICROS_FRAME_OWNERSHIP_OK
        || owner.kind != MICROS_FRAME_OWNER_VM_TRANSFERABLE
        || owner.slot != 0
        || owner.generation != 0
        || owner.reserved != 0
        || *(const volatile uint64_t *)(uintptr_t)
            scratch_physical_address
            != MICROS_VM_SCRATCH_PREEMPTION_SENTINEL
        || micros_vm_handoff_runtime_validate(
            bootstrap,
            registry,
            objects
        ) != MICROS_VM_HANDOFF_OK
        || micros_pm_control_runtime_validate(
            bootstrap,
            registry,
            objects
        ) != MICROS_PM_CONTROL_OK
    ) {
        test_failure(hart, frame, "vm-scratch-preemption-final");
    }
    uart_write(
        "MICROS_VM_SCRATCH_PREEMPTION_TEST_PASS "
        "map=real timer=preempted roots=distinct "
        "inactive-validation=accepted reactivation=fenced "
        "sentinel=preserved commit=real stale-translation=faulted "
        "pte=absent ownership=transferable scratch=clear\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}

_Noreturn void micros_vm_scratch_preemption_test_launch(void)
{
    test_phase = SCRATCH_PREEMPTION_WAITING_PEER_HOLD;
    timer_observed = false;
    scratch_physical_address = 0;
    scratch_load_token = 0;
    scratch_process = (struct micros_process_handle){0, 0};
    micros_bootstrap_runtime_launch(&runtime_config);
}

enum micros_vm_scratch_preemption_test_trap_result
micros_vm_scratch_preemption_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();
    const struct micros_vm_handoff_state *handoff =
        micros_vm_handoff_runtime_state();
    const struct micros_pm_control_state *control =
        micros_pm_control_runtime_state();
    const struct micros_endpoint_registry *registry =
        micros_ipc_runtime_registry();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    const struct micros_bootstrap_binding *vm;
    const struct micros_bootstrap_binding *peer;

    if (
        hart == NULL
        || frame == NULL
        || bootstrap == NULL
        || objects == NULL
    ) {
        return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_MISMATCH;
    }
    vm = micros_bootstrap_control_find_binding(
        bootstrap,
        MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID
    );
    peer = micros_bootstrap_control_find_binding(
        bootstrap,
        MICROS_VM_SCRATCH_PREEMPTION_PM_SERVICE_ID
    );
    if (vm == NULL || peer == NULL) {
        return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_MISMATCH;
    }

    if (
        test_phase == SCRATCH_PREEMPTION_WAITING_PEER_HOLD
        && frame->scause == MICROS_TEST_EXCEPTION_BREAKPOINT
        && frame->sepc
            == micros_vm_scratch_preemption_peer_hold_trap_address
        && frame->a0
            == MICROS_VM_SCRATCH_PREEMPTION_PEER_HOLD_MAGIC
        && current_is(hart, peer)
        && bootstrap->phase == MICROS_BOOTSTRAP_PHASE_SEALED
        && control != NULL
        && control->active_transaction != 0
        && control->reserved_process.slot
            == MICROS_VM_SCRATCH_PREEMPTION_RESERVED_PROCESS_SLOT
        && control->reserved_process.generation == 1
        && objects->processes[vm->process.slot].address_space_root
            != objects->processes[peer->process.slot]
                .address_space_root
        && read_satp()
            == expected_satp(
                objects->processes[peer->process.slot]
                    .address_space_root
            )
        && objects->threads[peer->thread.slot].runtime_flags == 0
        && objects->threads[peer->thread.slot].scheduler_assigned
    ) {
        frame->sepc =
            micros_vm_scratch_preemption_peer_hold_resume_address;
        if (
            micros_thread_scheduler_hold(objects, peer->thread)
                != MICROS_KERNEL_OBJECT_OK
        ) {
            return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_MISMATCH;
        }
        test_phase = SCRATCH_PREEMPTION_PEER_HELD;
        return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_USER_RETURN;
    }

    if (
        test_phase == SCRATCH_PREEMPTION_PEER_HELD
        && frame->scause == MICROS_TEST_EXCEPTION_BREAKPOINT
        && frame->sepc
            == micros_vm_scratch_preemption_vm_hook_trap_address
        && frame->a0
            == MICROS_VM_SCRATCH_PREEMPTION_VM_HOOK_MAGIC
        && current_is(hart, vm)
        && scratch_runtime_is_valid(
            bootstrap,
            handoff,
            control,
            registry,
            objects
        )
        && read_satp()
            == expected_satp(
                objects->processes[vm->process.slot]
                    .address_space_root
            )
        && objects->threads[peer->thread.slot].runtime_flags
            == MICROS_THREAD_RTS_INACTIVE
        && objects->threads[peer->thread.slot].scheduler_assigned
        && *(const volatile uint64_t *)(uintptr_t)
            handoff->load.scratch.physical_address
            == MICROS_VM_SCRATCH_PREEMPTION_SENTINEL
    ) {
        scratch_physical_address =
            handoff->load.scratch.physical_address;
        scratch_load_token = handoff->load.load_token;
        scratch_process = handoff->load.process;
        frame->sepc =
            micros_vm_scratch_preemption_vm_hook_spin_address;
        if (
            micros_thread_runtime_flags_unset(
                objects,
                peer->thread,
                MICROS_THREAD_RTS_INACTIVE
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_MISMATCH;
        }
        objects->threads[vm->thread.slot]
            .remaining_counter_ticks = 1;
        timer_observed = false;
        test_phase = SCRATCH_PREEMPTION_WAITING_TIMER;
        return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_USER_RETURN;
    }

    if (
        test_phase == SCRATCH_PREEMPTION_PEER_PREEMPTED
        && frame->scause == MICROS_TEST_EXCEPTION_BREAKPOINT
        && frame->sepc
            == micros_vm_scratch_preemption_peer_run_trap_address
        && frame->a0
            == MICROS_VM_SCRATCH_PREEMPTION_PEER_RUN_MAGIC
        && current_is(hart, peer)
        && read_satp()
            == expected_satp(
                objects->processes[peer->process.slot]
                    .address_space_root
            )
        && scratch_runtime_is_valid(
            bootstrap,
            handoff,
            control,
            registry,
            objects
        )
        && micros_user_address_space_validate(vm->process)
            == MICROS_USER_ADDRESS_SPACE_OK
        && *(const volatile uint64_t *)(uintptr_t)
            scratch_physical_address
            == MICROS_VM_SCRATCH_PREEMPTION_SENTINEL
    ) {
        frame->sepc =
            micros_vm_scratch_preemption_peer_run_resume_address;
        test_phase =
            SCRATCH_PREEMPTION_WAITING_VM_REACTIVATION;
        return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_USER_RETURN;
    }
    if (
        test_phase == SCRATCH_PREEMPTION_WAITING_STALE_FAULT
        && frame->scause == MICROS_TEST_EXCEPTION_LOAD_PAGE_FAULT
        && frame->sepc
            == micros_vm_scratch_preemption_stale_load_fault_address
    ) {
        report_success(hart, frame);
    }
    uart_write("MICROS_TEST_FAILURE vm-scratch-preemption phase=");
    uart_write_hex64(test_phase);
    uart_write("\n");
    return MICROS_VM_SCRATCH_PREEMPTION_TEST_TRAP_MISMATCH;
}

bool micros_vm_scratch_preemption_test_before_user_timer(
    const struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();
    const struct micros_bootstrap_binding *vm =
        bootstrap == NULL
            ? NULL
            : micros_bootstrap_control_find_binding(
                bootstrap,
                MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID
            );

    if (test_phase != SCRATCH_PREEMPTION_WAITING_TIMER) {
        return true;
    }
    if (
        !current_is(hart, vm)
        || frame == NULL
        || frame->sepc
            != micros_vm_scratch_preemption_vm_hook_spin_address
        || timer_observed
    ) {
        return false;
    }
    frame->sepc =
        micros_vm_scratch_preemption_vm_hook_resume_address;
    timer_observed = true;
    return true;
}

bool micros_vm_scratch_preemption_test_after_user_timer(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    const struct micros_bootstrap_binding *peer =
        bootstrap == NULL
            ? NULL
            : micros_bootstrap_control_find_binding(
                bootstrap,
                MICROS_VM_SCRATCH_PREEMPTION_PM_SERVICE_ID
            );

    if (test_phase != SCRATCH_PREEMPTION_WAITING_TIMER) {
        return true;
    }
    if (
        !timer_observed
        || hart == NULL
        || frame == NULL
        || objects == NULL
        || !current_is(hart, peer)
        || frame->sepc
            != micros_vm_scratch_preemption_peer_hold_resume_address
        || read_satp()
            != expected_satp(
                objects->processes[peer->process.slot]
                    .address_space_root
            )
    ) {
        return false;
    }
    test_phase = SCRATCH_PREEMPTION_PEER_PREEMPTED;
    return true;
}

bool micros_vm_scratch_preemption_test_after_syscall_return(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();
    const struct micros_vm_handoff_state *handoff =
        micros_vm_handoff_runtime_state();
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    const struct micros_bootstrap_binding *vm =
        bootstrap == NULL
            ? NULL
            : micros_bootstrap_control_find_binding(
                bootstrap,
                MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID
            );
    const struct micros_vm_kernel_mapping_record *record;

    if (
        test_phase
            == SCRATCH_PREEMPTION_WAITING_VM_REACTIVATION
    ) {
        if (
            hart == NULL
            || frame == NULL
            || objects == NULL
            || syscall_return != MICROS_SYSCALL_RETURN_CAPTURED
            || !current_is(hart, vm)
            || read_satp()
                != expected_satp(
                    objects->processes[vm->process.slot]
                        .address_space_root
                )
        ) {
            return false;
        }
        test_phase = SCRATCH_PREEMPTION_VM_REACTIVATED;
        return true;
    }
    if (test_phase != SCRATCH_PREEMPTION_VM_REACTIVATED) {
        return true;
    }
    record = (
        handoff == NULL
        || scratch_process.slot >= MICROS_PROCESS_CAPACITY
    )
        ? NULL
        : &handoff->mappings[scratch_process.slot];
    if (
        hart == NULL
        || frame == NULL
        || objects == NULL
        || handoff == NULL
        || record == NULL
        || syscall_return != MICROS_SYSCALL_RETURN_NORMAL
        || !current_is(hart, vm)
        || frame->a0 != MICROS_SYSCALL_ABI_OK
        || frame->a7 != MICROS_SYSCALL_ABI_VM_HANDOFF
        || handoff->load.scratch_phase != MICROS_VM_SCRATCH_CLEAR
        || handoff->load.load_token != scratch_load_token
        || record->entry_count != 1
        || record->entries[0].physical_address
            != scratch_physical_address
        || read_satp()
            != expected_satp(
                objects->processes[vm->process.slot]
                    .address_space_root
            )
    ) {
        return false;
    }
    test_phase = SCRATCH_PREEMPTION_WAITING_STALE_FAULT;
    return true;
}
