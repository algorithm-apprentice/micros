#ifndef MICROS_KERNEL_SCHEDULER_TEST_INTERNAL_H
#define MICROS_KERNEL_SCHEDULER_TEST_INTERNAL_H

#include "micros/scheduler.h"
#include "micros/endpoint.h"
#include "micros/ipc_core.h"

#define TEST_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define TEST_SCAUSE_CODE_MASK (TEST_SCAUSE_INTERRUPT - 1)
#define TEST_SSTATUS_UBE (UINT64_C(1) << 6)
#define TEST_SSTATUS_VS (UINT64_C(3) << 9)
#define TEST_SSTATUS_FS (UINT64_C(3) << 13)
#define TEST_SSTATUS_XS (UINT64_C(3) << 15)
#define TEST_SSTATUS_MXR (UINT64_C(1) << 19)
#define TEST_SSTATUS_SD (UINT64_C(1) << 63)

enum {
    TEST_EXCEPTION_USER_ECALL = 8,
    TEST_INTERRUPT_SUPERVISOR_TIMER = 5,
};

enum test_external_origin {
    TEST_EXTERNAL_NONE = 0,
    TEST_EXTERNAL_USER,
    TEST_EXTERNAL_SUPERVISOR,
    TEST_EXTERNAL_IDLE,
};

enum test_completion_kind {
    TEST_COMPLETION_NONE = 0,
    TEST_COMPLETION_START,
    TEST_COMPLETION_MESSAGE,
    TEST_COMPLETION_CAPTURED,
    TEST_COMPLETION_ERROR,
    TEST_COMPLETION_IDLE,
    TEST_COMPLETION_EXTERNAL_IDLE,
};

struct scheduler_test_context {
    struct micros_process_handle processes[2];
    struct micros_thread_handle threads[2];
    micros_endpoint_t endpoints[2];
    struct micros_endpoint_registry *registry;
    struct micros_user_context prepared_contexts[2];
    uint64_t code_physical[2];
    uint64_t counter_physical[2];
    uint64_t stack_physical[2];
    uintptr_t kernel_stack_bottom[2];
    uintptr_t kernel_stack_top[2];
    uint64_t baseline_owned;
    uint64_t baseline_free;
    uint64_t previous_counter[2];
    uint64_t ecall_count[2];
    uint64_t timer_trap_count;
    uint64_t switch_count;
    size_t last_running;
    bool supervisor_returned;
    bool start_boundary_observed;
    bool start_completion_observed;
    bool idle_requested;
    bool idle_woke_thread;
    bool spurious_idle_bypassed;
    uint64_t selector_entry_count;
    uint64_t selector_entries_before_spurious;
    uint64_t kernel_ticks_before_start;
    struct micros_kernel_objects start_snapshot;
    uint64_t start_satp_snapshot;
    bool start_sie_snapshot;
    bool start_stie_snapshot;
    uint64_t start_timer_attempts;
    enum test_completion_kind expected_completion[2];
    uint64_t expected_completion_result[2];
    struct micros_ipc_message expected_message;
    bool message_completion_deferred;
    bool message_completion_observed;
    bool captured_completion_observed;
    bool error_completion_observed;
    bool error_completion_staged;
    bool idle_completion_observed;
    volatile enum test_external_origin external_origin;
    bool user_external_started;
    bool user_external_observed;
    bool user_external_return_observed;
    bool supervisor_external_observed;
    bool idle_external_started;
    bool idle_external_observed;
    bool idle_external_completion_observed;
};

extern const uint64_t TEST_CODE_VIRTUAL_ADDRESS;
extern const uint64_t TEST_COUNTER_VIRTUAL_ADDRESS;
extern const uint64_t TEST_IPC_BUFFER_VIRTUAL_ADDRESS;
extern const uint64_t TEST_STACK_VIRTUAL_ADDRESS;
extern const uint64_t TEST_TIMER_INTERVAL;
extern const uint64_t TEST_QUANTUM;
extern const uint64_t TEST_START_TIMER_DELAY;
extern const uint32_t TEST_EXTERNAL_ATTEMPTS;
extern const uintptr_t TEST_UART_INTERRUPT_ENABLE;
extern const uint8_t TEST_UART_INTERRUPT_TX_EMPTY;

extern const unsigned char micros_scheduler_payload_start[];
extern const unsigned char micros_scheduler_payload_ecall[];
extern const unsigned char micros_scheduler_payload_loop[];
extern const unsigned char micros_scheduler_payload_end[];
extern const unsigned char micros_scheduler_test_supervisor_resume[];
extern const uint64_t micros_scheduler_test_saved_state[17];
extern const uint64_t micros_scheduler_test_restored_state[17];
extern const uint64_t micros_scheduler_test_immediate_status;
extern volatile uint64_t micros_scheduler_test_idle_bypass_once;
extern volatile uint64_t micros_scheduler_test_idle_bypass_observed;

struct scheduler_test_context *scheduler_test_state(void);
void micros_scheduler_test_enter(void);
void micros_scheduler_test_start_production(void);

bool scheduler_test_reserve_process(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t index,
    uint32_t code_permissions,
    uint32_t data_permissions
);
bool scheduler_test_prepare_process_image(
    struct scheduler_test_context *fixture,
    size_t index,
    size_t payload_size
);
bool scheduler_test_admit_threads(struct scheduler_test_context *fixture);
bool scheduler_test_prove_malformed_completion(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects
);
bool scheduler_test_stage_start_completion(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects
);
bool scheduler_test_prove_start_program_failure(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects
);

bool scheduler_test_arm_external_interrupt(
    struct scheduler_test_context *fixture,
    enum test_external_origin origin
);

bool scheduler_test_run_supervisor_external_test(struct scheduler_test_context *fixture);

bool scheduler_test_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
);

void scheduler_test_copy_bytes(void *destination, const void *source, size_t size);

bool scheduler_test_bytes_equal(
    const void *left,
    const void *right,
    size_t size
);

void scheduler_test_clear_bytes(void *storage, size_t size);

bool scheduler_test_observe_completion(
    struct scheduler_test_context *fixture,
    const struct micros_kernel_objects *objects,
    size_t thread_index
);

bool scheduler_test_expect_no_message_completion(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t thread_index,
    enum micros_ipc_error result,
    enum test_completion_kind kind
);

bool scheduler_test_expect_message_completion(
    struct scheduler_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t thread_index
);

enum micros_scheduler_test_trap_action
scheduler_test_completion_test_mismatch(uint64_t stage);

uint64_t scheduler_test_read_satp(void);

void scheduler_test_fill_context_pattern(
    struct micros_user_context *context,
    uint64_t base
);

uint64_t scheduler_test_user_address_of(const unsigned char *symbol);

size_t scheduler_test_current_thread_index(
    struct scheduler_test_context *fixture,const struct micros_hart *hart);

bool scheduler_test_user_registers_match(
    struct scheduler_test_context *fixture,
    size_t thread_index,
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
);

bool scheduler_test_snapshot_start_state(
    struct scheduler_test_context *fixture,
    const struct micros_kernel_objects *objects
);

bool scheduler_test_start_state_matches(
    struct scheduler_test_context *fixture,
    const struct micros_kernel_objects *objects
);

_Noreturn void scheduler_test_finish_test(struct scheduler_test_context *fixture);

#endif
