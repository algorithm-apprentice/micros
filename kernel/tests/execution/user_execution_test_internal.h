#ifndef MICROS_KERNEL_USER_EXECUTION_TEST_INTERNAL_H
#define MICROS_KERNEL_USER_EXECUTION_TEST_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/frame_allocator.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_objects.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

#define MICROS_USER_EXECUTION_TEST_SSTATUS_UBE \
    (UINT64_C(1) << 6)
#define MICROS_USER_EXECUTION_TEST_SSTATUS_VS \
    (UINT64_C(3) << 9)
#define MICROS_USER_EXECUTION_TEST_SSTATUS_FS \
    (UINT64_C(3) << 13)
#define MICROS_USER_EXECUTION_TEST_SSTATUS_XS \
    (UINT64_C(3) << 15)
#define MICROS_USER_EXECUTION_TEST_SSTATUS_MXR \
    (UINT64_C(1) << 19)
#define MICROS_USER_EXECUTION_TEST_SSTATUS_UXL_MASK \
    (UINT64_C(3) << 32)
#define MICROS_USER_EXECUTION_TEST_SSTATUS_UXL_64 \
    (UINT64_C(2) << 32)
#define MICROS_USER_EXECUTION_TEST_SSTATUS_SD \
    (UINT64_C(1) << 63)

#define MICROS_USER_EXECUTION_TEST_CODE_VIRTUAL_ADDRESS \
    MICROS_USER_VIRTUAL_BASE
#define MICROS_USER_EXECUTION_TEST_STACK_VIRTUAL_ADDRESS \
    (MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00004000))

enum {
    MICROS_USER_EXECUTION_TEST_EXCEPTION_USER_ECALL = 8,
    MICROS_USER_EXECUTION_TEST_EXCEPTION_LOAD_PAGE_FAULT = 13,
};

enum micros_user_execution_test_state {
    MICROS_USER_EXECUTION_TEST_IDLE,
    MICROS_USER_EXECUTION_TEST_KERNEL_FAULT,
    MICROS_USER_EXECUTION_TEST_FIRST_ECALL,
    MICROS_USER_EXECUTION_TEST_SECOND_ECALL,
    MICROS_USER_EXECUTION_TEST_SUPERVISOR,
};

struct micros_user_execution_test_context {
    struct micros_kernel_objects objects_snapshot;
    struct micros_frame_ownership ownership_snapshot;
    struct micros_frame_allocator allocator_snapshot;
    unsigned char
        kernel_stack_snapshot[MICROS_THREAD_KERNEL_STACK_SIZE];
    unsigned char second_kernel_stack_snapshot[
        MICROS_THREAD_KERNEL_STACK_SIZE
    ];
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    struct micros_process_handle test_process;
    struct micros_process_handle second_process;
    struct micros_thread_handle test_thread;
    struct micros_thread_handle second_thread;
    struct micros_thread_handle replacement_thread;
    struct micros_user_context initial_context;
    struct micros_user_context prepared_context;
    uint64_t code_physical_address;
    uint64_t stack_physical_address;
    uint64_t second_code_physical_address;
    uint64_t second_stack_physical_address;
    uintptr_t kernel_stack_bottom;
    uintptr_t kernel_stack_top;
    uintptr_t second_kernel_stack_bottom;
    uintptr_t second_kernel_stack_top;
    uint64_t live_sstatus_before_prepare;
    uint64_t baseline_owned;
    uint64_t baseline_free;
    volatile enum micros_user_execution_test_state test_state;
};

extern const unsigned char micros_user_execution_payload_start[];
extern const unsigned char micros_user_execution_payload_kernel_fault[];
extern const unsigned char micros_user_execution_payload_after_fault[];
extern const unsigned char micros_user_execution_payload_first_ecall[];
extern const unsigned char micros_user_execution_payload_resume[];
extern const unsigned char micros_user_execution_payload_second_ecall[];
extern const unsigned char micros_user_execution_payload_end[];
extern const unsigned char
    micros_user_execution_test_supervisor_resume[];
extern const unsigned char __thread_kernel_stacks_start[];
extern const unsigned char __thread_kernel_stacks_end[];
extern const uint64_t micros_user_execution_test_saved_state[17];
extern const uint64_t micros_user_execution_test_restored_state[17];
extern const uint64_t micros_user_execution_test_immediate_status;

void micros_user_execution_test_enter(
    uint64_t thread_slot,
    uint64_t thread_generation
);

_Noreturn void micros_user_execution_test_enter_production(
    uint64_t thread_slot,
    uint64_t thread_generation
);

struct micros_user_execution_test_context *
micros_user_execution_test_context(void);

bool micros_user_execution_test_acquire(
    struct micros_user_execution_test_context *context
);

void micros_user_execution_test_copy_bytes(
    void *destination,
    const void *source,
    size_t size
);

bool micros_user_execution_test_bytes_equal(
    const void *left,
    const void *right,
    size_t size
);

void micros_user_execution_test_fill_context_pattern(
    struct micros_user_context *context,
    uint64_t base
);

uint64_t micros_user_execution_test_user_address_of(
    const unsigned char *payload_symbol
);

uint64_t micros_user_execution_test_read_sstatus(void);
void micros_user_execution_test_write_sstatus(uint64_t status);
uint64_t micros_user_execution_test_read_satp(void);

bool micros_user_execution_test_snapshot_prepare_state(
    struct micros_user_execution_test_context *context
);

bool micros_user_execution_test_prepare_state_matches(
    const struct micros_user_execution_test_context *context
);

void micros_user_execution_test_fill_stack(
    uint64_t stack_bottom,
    uint64_t value
);

bool micros_user_execution_test_stack_has_value(
    uint64_t stack_bottom,
    uint64_t value
);

bool micros_user_execution_test_context_has_valid_status(
    const struct micros_user_execution_test_context *context,
    const struct micros_user_context *user_context
);

bool micros_user_execution_test_create_fixture(
    struct micros_user_execution_test_context *context
);

bool micros_user_execution_test_install_payload(
    struct micros_user_execution_test_context *context
);

bool micros_user_execution_test_initialize_prepare_state(
    struct micros_user_execution_test_context *context
);

bool micros_user_execution_test_reject_invalid_preparations(
    struct micros_user_execution_test_context *context
);

bool micros_user_execution_test_prepare_threads(
    struct micros_user_execution_test_context *context
);

bool micros_user_execution_test_finish(
    struct micros_user_execution_test_context *context
);

#endif
