#include "kernel/trap/trap_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"

extern unsigned char __trap_stack_bottom[];
extern unsigned char __trap_stack_top[];
extern unsigned char __trap_emergency_stack_bottom[];
extern unsigned char __trap_emergency_stack_top[];
extern unsigned char micros_trap_entry[];

void micros_riscv_trap_install(struct micros_hart *hart);

bool micros_trap_install(void)
{
    struct micros_hart *hart;

    if (
        micros_kernel_object_runtime_install_trap_stacks(
            (uintptr_t)__trap_stack_bottom,
            (uintptr_t)__trap_stack_top,
            (uintptr_t)__trap_emergency_stack_bottom,
            (uintptr_t)__trap_emergency_stack_top
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    hart = micros_kernel_object_runtime_boot_hart();
    if (hart == NULL) {
        return false;
    }
    micros_riscv_trap_install(hart);
    return (
        trap_read_sscratch() == (uintptr_t)hart
        && trap_read_tp() == (uintptr_t)hart
        && trap_read_stvec() == (uintptr_t)micros_trap_entry
    );
}

void micros_trap_dispatch(struct micros_trap_frame *frame)
{
    struct micros_hart *hart = trap_resolve_trap_hart(frame);
    enum micros_trap_interrupt_route interrupt_route;
    uint64_t cause_code;

    if (hart == NULL) {
        struct micros_hart *boot_hart =
            micros_kernel_object_runtime_boot_hart();
        uintptr_t hart_id =
            boot_hart == NULL ? 0 : boot_hart->hardware_id;

        MICROS_TRAP_PANIC(
            hart_id,
            "trap-route-invalid",
            frame
        );
    }
    cause_code = frame->scause & MICROS_SCAUSE_CODE_MASK;
    interrupt_route = micros_trap_interrupt_route_classify(
        (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0,
        frame->scause
    );

    if ((frame->sstatus & MICROS_RISCV_SSTATUS_SPP) == 0) {
        trap_handle_user_trap(hart, frame, interrupt_route, cause_code);
        return;
    }
    if (trap_test_hooks_supervisor_faults(hart, frame)) {
        return;
    }
    if (trap_handle_supervisor_test(hart, frame)) {
        return;
    }
    if (trap_handle_supervisor_interrupt(hart, frame, interrupt_route)) {
        return;
    }
    MICROS_TRAP_PANIC(
        hart->hardware_id,
        "unexpected-exception",
        frame
    );
}

_Noreturn void micros_trap_nested_panic(
    struct micros_hart *hart,
    const struct micros_trap_frame *outer_frame
)
{
#ifdef MICROS_BUILD_NESTED_TRAP_TEST
    const uint64_t poison_tp = UINT64_C(0x4e45535445445450);
    struct micros_hart *resolved =
        micros_kernel_object_runtime_hart_from_context(
            (uintptr_t)hart
        );
    uintptr_t stack_probe = (uintptr_t)&resolved;

    if (
        resolved == NULL
        || resolved != hart
        || outer_frame == NULL
        || !trap_frame_is_on_primary_stack(resolved, outer_frame)
        || outer_frame->hart_context != (uintptr_t)resolved
        || outer_frame->tp != poison_tp
        || (
            outer_frame->sstatus
            & MICROS_RISCV_SSTATUS_SUM
        ) == 0
        || (trap_read_sstatus() & MICROS_RISCV_SSTATUS_SUM) != 0
        || stack_probe < resolved->trap.emergency_stack_bottom
        || stack_probe >= resolved->trap.emergency_stack_top
        || trap_read_sscratch() != 0
        || trap_read_tp() != (uintptr_t)resolved
    ) {
        struct micros_hart *boot_hart =
            micros_kernel_object_runtime_boot_hart();
        uintptr_t hart_id =
            boot_hart == NULL ? 0 : boot_hart->hardware_id;

        MICROS_PANIC(hart_id, "nested-trap-test-route");
    }
    uart_write(
        "MICROS_NESTED_TRAP_TEST_PASS "
        "hart=routed emergency-stack=selected\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );
    uart_write("MICROS_TEST_FAILURE sbi-system-reset-returned\n");
    uart_flush();
    for (;;) {
        __asm__ volatile("wfi");
    }
#else
    struct micros_hart *resolved =
        micros_kernel_object_runtime_hart_from_context(
            (uintptr_t)hart
        );
    struct micros_hart *boot_hart =
        micros_kernel_object_runtime_boot_hart();
    uintptr_t hart_id;

    (void)outer_frame;
    if (resolved != NULL) {
        hart_id = resolved->hardware_id;
    } else if (boot_hart != NULL) {
        hart_id = boot_hart->hardware_id;
    } else {
        hart_id = 0;
    }
    MICROS_PANIC(hart_id, "nested-trap");
#endif
}
