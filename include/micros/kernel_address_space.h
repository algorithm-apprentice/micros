#ifndef MICROS_KERNEL_ADDRESS_SPACE_H
#define MICROS_KERNEL_ADDRESS_SPACE_H

#include <stdbool.h>
#include <stdint.h>

struct micros_trap_frame;

enum micros_kernel_address_space_error {
    MICROS_KERNEL_ADDRESS_SPACE_OK = 0,
    MICROS_KERNEL_ADDRESS_SPACE_ERROR_ALREADY_INITIALIZED,
    MICROS_KERNEL_ADDRESS_SPACE_ERROR_INTERRUPTS_ENABLED,
    MICROS_KERNEL_ADDRESS_SPACE_ERROR_LINKER_LAYOUT,
    MICROS_KERNEL_ADDRESS_SPACE_ERROR_ALLOCATOR_STATE,
    MICROS_KERNEL_ADDRESS_SPACE_ERROR_ALLOCATION,
    MICROS_KERNEL_ADDRESS_SPACE_ERROR_CAPACITY,
    MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE,
    MICROS_KERNEL_ADDRESS_SPACE_ERROR_PTE,
    MICROS_KERNEL_ADDRESS_SPACE_ERROR_CONFLICT,
    MICROS_KERNEL_ADDRESS_SPACE_ERROR_INVARIANT,
    MICROS_KERNEL_ADDRESS_SPACE_ERROR_SATP,
};

struct micros_kernel_address_space_report {
    uint64_t root_physical_address;
    uint64_t table_count;
};

enum micros_kernel_address_space_error
micros_kernel_address_space_initialize(void);

const struct micros_kernel_address_space_report *
micros_kernel_address_space_report(void);

#ifdef MICROS_BUILD_MMU_TEST
enum micros_mmu_test_trap_result {
    MICROS_MMU_TEST_TRAP_INACTIVE = 0,
    MICROS_MMU_TEST_TRAP_HANDLED,
    MICROS_MMU_TEST_TRAP_MISMATCH,
};

bool micros_kernel_address_space_run_self_test(void);

enum micros_mmu_test_trap_result
micros_kernel_address_space_handle_test_trap(
    struct micros_trap_frame *frame
);
#endif

#endif
