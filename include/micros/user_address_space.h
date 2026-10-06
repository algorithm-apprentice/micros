#ifndef MICROS_USER_ADDRESS_SPACE_H
#define MICROS_USER_ADDRESS_SPACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/kernel_objects.h"

struct micros_trap_frame;

#define MICROS_USER_VIRTUAL_BASE UINT64_C(0x0000000040000000)
#define MICROS_USER_VIRTUAL_END UINT64_C(0x0000000080000000)

enum micros_user_address_space_error {
    MICROS_USER_ADDRESS_SPACE_OK = 0,
    MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT,
    MICROS_USER_ADDRESS_SPACE_ERROR_NOT_INITIALIZED,
    MICROS_USER_ADDRESS_SPACE_ERROR_PHASE,
    MICROS_USER_ADDRESS_SPACE_ERROR_STALE,
    MICROS_USER_ADDRESS_SPACE_ERROR_STATE,
    MICROS_USER_ADDRESS_SPACE_ERROR_RANGE,
    MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION,
    MICROS_USER_ADDRESS_SPACE_ERROR_ALLOCATION,
    MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT,
    MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED,
    MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
    MICROS_USER_ADDRESS_SPACE_ERROR_PTE,
    MICROS_USER_ADDRESS_SPACE_ERROR_BUSY,
    MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT,
    MICROS_USER_ADDRESS_SPACE_ERROR_SATP,
};

enum micros_user_address_space_error
micros_user_address_space_create(struct micros_process_handle process);

enum micros_user_address_space_error
micros_user_address_space_destroy(struct micros_process_handle process);

enum micros_user_address_space_error
micros_user_address_space_allocate_page(
    struct micros_process_handle process,
    uint64_t virtual_address,
    uint32_t permissions,
    uint64_t *physical_address
);

enum micros_user_address_space_error
micros_user_address_space_release_page(
    struct micros_process_handle process,
    uint64_t virtual_address,
    uint64_t *physical_address
);

enum micros_user_address_space_error
micros_user_address_space_lookup(
    struct micros_process_handle process,
    uint64_t virtual_address,
    uint64_t *physical_address,
    uint32_t *permissions
);

enum micros_user_address_space_error
micros_user_address_space_activate(
    struct micros_process_handle process
);

enum micros_user_address_space_error
micros_user_address_space_activate_kernel(void);

enum micros_user_address_space_error
micros_user_address_space_validate(
    struct micros_process_handle process
);

#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
enum micros_user_address_space_test_trap_result {
    MICROS_USER_ADDRESS_SPACE_TEST_TRAP_INACTIVE = 0,
    MICROS_USER_ADDRESS_SPACE_TEST_TRAP_HANDLED,
    MICROS_USER_ADDRESS_SPACE_TEST_TRAP_MISMATCH,
};

void micros_user_address_space_test_fail_after_allocations(
    size_t successful_allocations
);

bool micros_user_address_space_runtime_run_self_test(void);

enum micros_user_address_space_test_trap_result
micros_user_address_space_handle_test_trap(
    struct micros_trap_frame *frame
);
#endif

#endif
