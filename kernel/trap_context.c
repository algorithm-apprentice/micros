#include "kernel/trap_internal.h"

#include <stddef.h>

#include "micros/kernel_object_runtime.h"

uintptr_t trap_read_sscratch(void)
{
    uintptr_t value;

    __asm__ volatile("csrr %0, sscratch" : "=r"(value));
    return value;
}

uintptr_t trap_read_stvec(void)
{
    uintptr_t value;

    __asm__ volatile("csrr %0, stvec" : "=r"(value));
    return value;
}

uintptr_t trap_read_tp(void)
{
    uintptr_t value;

    __asm__ volatile("mv %0, tp" : "=r"(value));
    return value;
}

#if defined(MICROS_BUILD_TRAP_TEST) \
    || defined(MICROS_BUILD_NESTED_TRAP_TEST)
uintptr_t trap_read_sstatus(void)
{
    uintptr_t value;

    __asm__ volatile("csrr %0, sstatus" : "=r"(value));
    return value;
}
#endif

bool trap_frame_is_on_primary_stack(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    uintptr_t frame_address = (uintptr_t)frame;

    return (
        hart != NULL
        && hart->trap_installed
        && frame_address >= hart->trap.primary_stack_bottom
        && frame_address < hart->trap.primary_stack_top
        && sizeof(*frame)
            <= hart->trap.primary_stack_top - frame_address
    );
}

struct micros_hart *trap_resolve_trap_hart(
    const struct micros_trap_frame *frame
)
{
    struct micros_hart *hart;

    if (frame == NULL || frame->hart_context == 0) {
        return NULL;
    }
    hart = micros_kernel_object_runtime_hart_from_context(
        frame->hart_context
    );
    if (
        hart == NULL
        || !trap_frame_is_on_primary_stack(hart, frame)
        || trap_read_sscratch() != 0
        || trap_read_tp() != (uintptr_t)hart
    ) {
        return NULL;
    }
    return hart;
}
