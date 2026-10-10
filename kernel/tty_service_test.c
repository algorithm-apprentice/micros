#include "kernel/tty_service_test.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/plic.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/tty_service_test_fixture.h"
#include "kernel/user_address_space_internal.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/bootstrap.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/tty.h"
#include "tests/qemu/tty_handoff_protocol.h"

#define MICROS_TTY_TEST_EXCEPTION_BREAKPOINT UINT64_C(3)
#define MICROS_TTY_TEST_UART_LSR UINT8_C(5)
#define MICROS_TTY_TEST_UART_LSR_THRE UINT8_C(0x20)
#define MICROS_TTY_TEST_UART_LSR_TEMT UINT8_C(0x40)
#define MICROS_TTY_TEST_UART_DRAIN_LIMIT UINT64_C(1000000)
#define MICROS_TTY_TEST_CLAIM_TOKEN UINT64_C(0x545459434c41494d)

static const struct micros_privilege_profile profiles[] = {
    {
        .id =
            MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
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
            )
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

static const struct micros_bootstrap_expected_service
    expected_services[] = {
        {
            .service_id =
                MICROS_TTY_HANDOFF_TEST_LAUNCHER_SERVICE_ID,
            .image_id = 101,
            .process_slot = 0,
            .profile_id =
                MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
            .service_name = "bootstrap-launcher",
            .profile_name = "BOOTSTRAP_LAUNCHER",
            .call_targets = 0,
            .role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER,
        },
        {
            .service_id = MICROS_TTY_HANDOFF_TEST_VM_SERVICE_ID,
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
            .service_id = MICROS_TTY_HANDOFF_TEST_PM_SERVICE_ID,
            .image_id = 103,
            .process_slot = 2,
            .profile_id = MICROS_PRIVILEGE_PROFILE_PM,
            .service_name = "pm",
            .profile_name = "PM",
            .prerequisites =
                UINT64_C(1)
                << (MICROS_TTY_HANDOFF_TEST_VM_SERVICE_ID - 1),
            .call_targets =
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
            .role_flags = MICROS_BOOTSTRAP_ROLE_PM,
        },
        {
            .service_id = MICROS_TTY_HANDOFF_TEST_TTY_SERVICE_ID,
            .image_id = 104,
            .process_slot = 3,
            .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
            .service_name = "tty",
            .profile_name = "TTY",
            .prerequisites =
                UINT64_C(1)
                << (MICROS_TTY_HANDOFF_TEST_PM_SERVICE_ID - 1),
            .call_targets =
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
            .role_flags = MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER,
            .irq_source = MICROS_TTY_UART_IRQ_SOURCE,
            .device_base = MICROS_TTY_UART_PHYSICAL_BASE,
            .device_length = MICROS_TTY_UART_MAPPED_LENGTH,
        },
        {
            .service_id = MICROS_TTY_HANDOFF_TEST_VFS_SERVICE_ID,
            .image_id = 105,
            .process_slot = 4,
            .profile_id = MICROS_PRIVILEGE_PROFILE_VFS,
            .service_name = "tty-test-vfs",
            .profile_name = "VFS",
            .prerequisites =
                UINT64_C(1)
                << (MICROS_TTY_HANDOFF_TEST_TTY_SERVICE_ID - 1),
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
    };

static const struct micros_bootstrap_scheduler_policy policies[] = {
    {
        .service_id =
            MICROS_TTY_HANDOFF_TEST_LAUNCHER_SERVICE_ID,
        .priority = 4,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_TTY_HANDOFF_TEST_VM_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_TTY_HANDOFF_TEST_PM_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_TTY_HANDOFF_TEST_TTY_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
    {
        .service_id = MICROS_TTY_HANDOFF_TEST_VFS_SERVICE_ID,
        .priority = 8,
        .preemptible = true,
        .quantum_counter_ticks = UINT64_C(0x00000000000f4240),
    },
};

static const struct micros_bootstrap_runtime_config runtime_config = {
    .manifest = &micros_tty_service_test_manifest,
    .expected_services = expected_services,
    .expected_service_count =
        sizeof(expected_services) / sizeof(expected_services[0]),
    .images = micros_tty_service_test_images,
    .image_count = MICROS_TTY_HANDOFF_TEST_SERVICE_COUNT,
    .profiles = profiles,
    .profile_count = sizeof(profiles) / sizeof(profiles[0]),
    .policies = policies,
    .policy_count = sizeof(policies) / sizeof(policies[0]),
    .scheduler_preemption_interval = UINT64_C(100000),
};

static uint64_t claim_count;
static uint64_t completion_count;
static uint64_t ready_count;
static bool ready_retained_claim;
static bool evidence_invalid;

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

void micros_tty_service_test_record_claim(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    if (frame == NULL || claim_count == UINT64_MAX) {
        evidence_invalid = true;
        return;
    }
    claim_count += 1;
    if (ready_count == 0 && claim_count == 1) {
        const struct micros_tty_handoff_runtime_state *tty =
            micros_tty_handoff_runtime_state();
        const struct micros_kernel_objects *objects =
            micros_kernel_object_runtime_registry();
        struct micros_thread_handle current;

        if (
            hart == NULL
            || tty == NULL
            || objects == NULL
            || micros_hart_current_thread(
                objects,
                micros_kernel_object_runtime_boot_hart_handle(),
                &current
            ) != MICROS_KERNEL_OBJECT_OK
            || current.slot != tty->thread.slot
            || current.generation != tty->thread.generation
        ) {
            evidence_invalid = true;
            return;
        }
        frame->s11 = MICROS_TTY_TEST_CLAIM_TOKEN;
    }
}

void micros_tty_service_test_record_completion(void)
{
    if (completion_count == UINT64_MAX) {
        evidence_invalid = true;
        return;
    }
    completion_count += 1;
}

void micros_tty_service_test_record_ready(bool retained_claim)
{
    if (ready_count == UINT64_MAX) {
        evidence_invalid = true;
        return;
    }
    ready_count += 1;
    ready_retained_claim = (
        ready_retained_claim || retained_claim
    );
}

static bool uart_is_physically_drained(void)
{
    volatile uint8_t *uart = (volatile uint8_t *)(uintptr_t)
        MICROS_TTY_UART_PHYSICAL_BASE;
    uint64_t attempt;

    for (
        attempt = 0;
        attempt < MICROS_TTY_TEST_UART_DRAIN_LIMIT;
        ++attempt
    ) {
        __asm__ volatile("fence iorw, iorw" ::: "memory");
        if (
            (
                uart[MICROS_TTY_TEST_UART_LSR]
                & (
                    MICROS_TTY_TEST_UART_LSR_THRE
                    | MICROS_TTY_TEST_UART_LSR_TEMT
                )
            )
            == (
                MICROS_TTY_TEST_UART_LSR_THRE
                | MICROS_TTY_TEST_UART_LSR_TEMT
            )
        ) {
            return true;
        }
    }
    return false;
}

_Noreturn void micros_tty_service_test_launch(void)
{
    claim_count = 0;
    completion_count = 0;
    ready_count = 0;
    ready_retained_claim = false;
    evidence_invalid = false;
    micros_bootstrap_runtime_launch(&runtime_config);
}

_Noreturn void micros_tty_service_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();
    const struct micros_vm_handoff_state *vm =
        micros_vm_handoff_runtime_state();
    const struct micros_tty_handoff_runtime_state *tty =
        micros_tty_handoff_runtime_state();
    const struct micros_endpoint_registry *registry =
        micros_ipc_runtime_registry();
    const struct micros_grant_registry *grants =
        micros_grant_runtime_registry();
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    const struct micros_bootstrap_binding *vfs;
    struct micros_thread_handle current;
    size_t index;

    if (
        hart == NULL
        || frame == NULL
        || bootstrap == NULL
        || vm == NULL
        || tty == NULL
        || registry == NULL
        || grants == NULL
        || objects == NULL
        || frame->scause != MICROS_TTY_TEST_EXCEPTION_BREAKPOINT
        || frame->sepc != micros_tty_service_test_report_address
        || frame->a0 != MICROS_TTY_HANDOFF_TEST_REPORT_MAGIC
        || frame->a1 != MICROS_TTY_HANDOFF_TEST_READ_COUNT
        || frame->a2
            != sizeof(MICROS_TTY_HANDOFF_TEST_INPUT_TRIGGER) - 1
        || frame->a3 != sizeof(MICROS_TTY_HANDOFF_TEST_PASS) - 1
        || bootstrap->phase != MICROS_BOOTSTRAP_PHASE_SEALED
        || vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || tty->handoff.console_phase != MICROS_TTY_CONSOLE_OWNED
        || tty->handoff.route_phase != MICROS_TTY_ROUTE_IDLE
        || tty->handoff.claimed_source != 0
        || tty->handoff.deadline_armed
        || tty->service_id
            != MICROS_TTY_HANDOFF_TEST_TTY_SERVICE_ID
        || !uart_console_handoff_is_active()
        || !riscv_external_interrupt_is_enabled()
        || !micros_plic_validate(MICROS_PLIC_ENABLED)
        || grants->active_count != 0
        || evidence_invalid
        || claim_count == 0
        || completion_count != claim_count
        || ready_count != 1
        || !ready_retained_claim
        || micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_vm_handoff_runtime_validate(
            bootstrap,
            registry,
            objects
        ) != MICROS_VM_HANDOFF_OK
        || micros_tty_handoff_runtime_validate(
            bootstrap,
            registry,
            objects
        ) != MICROS_TTY_HANDOFF_OK
        || micros_user_address_space_validate_tty_uart_mapping(
            tty->process,
            tty->root_physical_address
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_hart_current_thread(
            objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            &current
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        test_failure(hart, frame, "tty-service-test-state");
    }
    vfs = micros_bootstrap_control_find_binding(
        bootstrap,
        MICROS_TTY_HANDOFF_TEST_VFS_SERVICE_ID
    );
    if (
        vfs == NULL
        || current.slot != vfs->thread.slot
        || current.generation != vfs->thread.generation
    ) {
        test_failure(hart, frame, "tty-service-test-current");
    }
    for (index = 0; index < bootstrap->entry_count; ++index) {
        if (
            bootstrap->transitions.entries[index].state
                != MICROS_BOOTSTRAP_SERVICE_READY
        ) {
            test_failure(
                hart,
                frame,
                "tty-service-test-readiness"
            );
        }
    }
    if (!uart_is_physically_drained()) {
        test_failure(hart, frame, "tty-service-test-uart-drain");
    }
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
