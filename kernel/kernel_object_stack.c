#include "kernel_object_private.h"

bool kernel_object_stack_range_is_valid(
    uintptr_t bottom,
    uintptr_t top,
    uintptr_t minimum_size
)
{
    return (
        bottom != 0
        && top > bottom
        && bottom % MICROS_TRAP_STACK_ALIGNMENT == 0
        && top % MICROS_TRAP_STACK_ALIGNMENT == 0
        && top - bottom >= minimum_size
    );
}

bool kernel_object_ranges_overlap(
    uintptr_t first_bottom,
    uintptr_t first_top,
    uintptr_t second_bottom,
    uintptr_t second_top
)
{
    return first_bottom < second_top && second_bottom < first_top;
}

bool kernel_object_trap_anchor_stacks_are_valid(
    const struct micros_hart_trap_anchor *trap
)
{
    return (
        kernel_object_stack_range_is_valid(
            trap->primary_stack_bottom,
            trap->primary_stack_top,
            MICROS_PRIMARY_TRAP_STACK_MIN_SIZE
        )
        && kernel_object_stack_range_is_valid(
            trap->emergency_stack_bottom,
            trap->emergency_stack_top,
            MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE
        )
        && !kernel_object_ranges_overlap(
            trap->primary_stack_bottom,
            trap->primary_stack_top,
            trap->emergency_stack_bottom,
            trap->emergency_stack_top
        )
    );
}
