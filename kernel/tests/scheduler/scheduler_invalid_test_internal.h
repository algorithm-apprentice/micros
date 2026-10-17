#ifndef MICROS_KERNEL_SCHEDULER_INVALID_TEST_INTERNAL_H
#define MICROS_KERNEL_SCHEDULER_INVALID_TEST_INTERNAL_H

#include "micros/scheduler.h"
#include "micros/trap.h"

#define TEST_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define TEST_SCAUSE_CODE_MASK (TEST_SCAUSE_INTERRUPT - 1)

enum {
    TEST_EXCEPTION_USER_ECALL = 8,
};

struct scheduler_invalid_test_context {
    struct micros_process_handle processes[2];
    struct micros_thread_handle threads[2];
    uint64_t code_physical[2];
    uint64_t counter_physical[2];
    uint64_t stack_physical[2];
    uintptr_t kernel_stack_bottom[2];
    uintptr_t kernel_stack_top[2];
    struct micros_kernel_objects timer_objects_snapshot;
    struct micros_kernel_objects scheduler_snapshot_candidate;
    struct micros_trap_frame timer_frame_snapshot;
    uint64_t timer_satp_snapshot;
};

extern const uint64_t TEST_CODE_VIRTUAL_ADDRESS;
extern const uint64_t TEST_COUNTER_VIRTUAL_ADDRESS;
extern const uint64_t TEST_STACK_VIRTUAL_ADDRESS;
extern const uint64_t TEST_TIMER_INTERVAL;
extern const uint64_t TEST_QUANTUM;
extern const unsigned char micros_scheduler_payload_start[];
extern const unsigned char micros_scheduler_payload_ecall[];
extern const unsigned char micros_scheduler_payload_end[];

struct scheduler_invalid_test_context *scheduler_invalid_test_state(void);
void micros_scheduler_test_enter(void);
void micros_scheduler_test_start_production(void);

bool scheduler_invalid_test_preflight_payload(
    const struct micros_kernel_objects *objects,
    size_t payload_size
);
bool scheduler_invalid_test_reserve_process(
    struct scheduler_invalid_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t index,
    uint32_t code_permissions,
    uint32_t stack_permissions
);
bool scheduler_invalid_test_prepare_image(
    struct scheduler_invalid_test_context *fixture,
    size_t index,
    size_t payload_size
);
bool scheduler_invalid_test_admit_threads(
    struct scheduler_invalid_test_context *fixture
);
#ifdef MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST
void scheduler_invalid_test_corrupt_next_context(
    struct scheduler_invalid_test_context *fixture,
    struct micros_kernel_objects *objects
);
#endif

void scheduler_invalid_test_copy_bytes(void *destination, const void *source, size_t size);

bool scheduler_invalid_test_bytes_equal(
    const void *left,
    const void *right,
    size_t size
);

uint64_t scheduler_invalid_test_read_satp(void);

uint64_t scheduler_invalid_test_user_address_of(const unsigned char *symbol);

void scheduler_invalid_test_fill_context_pattern(
    struct micros_user_context *context,
    uint64_t base
);

#endif
