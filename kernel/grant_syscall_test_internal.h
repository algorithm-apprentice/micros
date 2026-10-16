#ifndef MICROS_KERNEL_GRANT_SYSCALL_TEST_INTERNAL_H
#define MICROS_KERNEL_GRANT_SYSCALL_TEST_INTERNAL_H

#include "kernel/grant_syscall_test.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/grant_runtime_internal.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/kernel_object_runtime_internal.h"
#include "micros/bootstrap_control.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_copy.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_abi.h"
#include "micros/ipc_core.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/scheduler_core.h"
#include "micros/sv39.h"
#include "micros/syscall_abi.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

#define TEST_SSTATUS_UBE (UINT64_C(1) << 6)
#define TEST_SSTATUS_VS (UINT64_C(3) << 9)
#define TEST_SSTATUS_FS (UINT64_C(3) << 13)
#define TEST_SSTATUS_XS (UINT64_C(3) << 15)
#define TEST_SSTATUS_MXR (UINT64_C(1) << 19)
#define TEST_SSTATUS_SD (UINT64_C(1) << 63)

enum {
    GRANT_SYSCALL_GRANTOR = 0,
    GRANT_SYSCALL_GRANTEE,
    GRANT_SYSCALL_WRONG,
    GRANT_SYSCALL_PROCESS_COUNT,
    GRANT_SYSCALL_PAGE_COUNT = 5,
};

enum grant_syscall_command {
    GRANT_COMMAND_NONE = 0,
    GRANT_COMMAND_IPC_NOTIFY,
    GRANT_COMMAND_CREATE_READ,
    GRANT_COMMAND_CREATE_WRITE,
    GRANT_COMMAND_CREATE_REMOTE_READ_ONLY,
    GRANT_COMMAND_CREATE_UNMAPPED,
    GRANT_COMMAND_CREATE_STALE,
    GRANT_COMMAND_CREATE_UPPER_ENDPOINT,
    GRANT_COMMAND_CREATE_UPPER_PERMISSION,
    GRANT_COMMAND_CREATE_UNUSED,
    GRANT_COMMAND_REVOKE_STALE,
    GRANT_COMMAND_REVOKE_UPPER_TOKEN,
    GRANT_COMMAND_REVOKE_UNUSED,
    GRANT_COMMAND_VALIDATE_READ,
    GRANT_COMMAND_VALIDATE_ZERO,
    GRANT_COMMAND_VALIDATE_WRONG_DIRECTION,
    GRANT_COMMAND_VALIDATE_UNMAPPED,
    GRANT_COMMAND_VALIDATE_OVERFLOW_STALE,
    GRANT_COMMAND_VALIDATE_UPPER_PERMISSION,
    GRANT_COMMAND_COPY_FROM_CROSS,
    GRANT_COMMAND_COPY_TO_CROSS,
    GRANT_COMMAND_COPY_FROM_LOCAL,
    GRANT_COMMAND_COPY_TO_LOCAL,
    GRANT_COMMAND_COPY_FROM_ZERO,
    GRANT_COMMAND_COPY_TO_ZERO,
    GRANT_COMMAND_COPY_FROM_WRONG_DIRECTION,
    GRANT_COMMAND_COPY_TO_WRONG_DIRECTION,
    GRANT_COMMAND_COPY_WRONG_GRANTOR,
    GRANT_COMMAND_COPY_STALE_ENDPOINT,
    GRANT_COMMAND_COPY_STALE_TOKEN,
    GRANT_COMMAND_COPY_RANGE,
    GRANT_COMMAND_COPY_OVERFLOW,
    GRANT_COMMAND_COPY_OVERSIZE,
    GRANT_COMMAND_COPY_LOCAL_UNMAPPED,
    GRANT_COMMAND_COPY_REMOTE_UNMAPPED,
    GRANT_COMMAND_COPY_LOCAL_PERMISSION,
    GRANT_COMMAND_COPY_REMOTE_PERMISSION,
    GRANT_COMMAND_COPY_UPPER_ENDPOINT,
    GRANT_COMMAND_COPY_UPPER_TOKEN,
    GRANT_COMMAND_COPY_UNUSED,
    GRANT_COMMAND_UNKNOWN_OPERATION,
    GRANT_COMMAND_BOOTSTRAP_UNAVAILABLE,
    GRANT_COMMAND_COPY_WRONG_GRANTEE,
    GRANT_COMMAND_REVOKE_READ,
    GRANT_COMMAND_REVOKE_WRITE,
    GRANT_COMMAND_COPY_REVOKED,
    GRANT_COMMAND_COPY_REUSED_ENDPOINT,
};

enum grant_syscall_control_action {
    GRANT_CONTROL_NONE = 0,
    GRANT_CONTROL_SWITCH,
    GRANT_CONTROL_REUSE_GRANTOR,
    GRANT_CONTROL_FINISH,
};

struct grant_syscall_script {
    bool armed;
    bool awaiting;
    bool token_result;
    bool preserve_state;
    enum grant_syscall_command command;
    enum micros_syscall_return expected_return;
    uint64_t expected_result;
    struct micros_user_context captured;
};

struct grant_syscall_test_fixture {
    struct micros_process_handle processes[GRANT_SYSCALL_PROCESS_COUNT];
    struct micros_thread_handle threads[GRANT_SYSCALL_PROCESS_COUNT];
    micros_endpoint_t endpoints[GRANT_SYSCALL_PROCESS_COUNT];
    bool thread_live[GRANT_SYSCALL_PROCESS_COUNT];
    bool address_space_live[GRANT_SYSCALL_PROCESS_COUNT];
    uint64_t user_physical[GRANT_SYSCALL_PROCESS_COUNT][GRANT_SYSCALL_PAGE_COUNT];
    struct grant_syscall_script scripts[GRANT_SYSCALL_PROCESS_COUNT];
    struct micros_endpoint_registry *endpoint_registry;
    struct micros_grant_registry *grant_registry;
    micros_grant_t read_grant;
    micros_grant_t write_grant;
    micros_grant_t remote_read_only_grant;
    micros_grant_t unmapped_grant;
    micros_grant_t stale_grant;
    micros_endpoint_t old_grantor_endpoint;
    enum grant_syscall_control_action control_action;
    size_t control_next_actor;
    enum grant_syscall_command control_next_command;
    size_t dispatch_actor;
    uint64_t baseline_owned;
    uint64_t baseline_free;
    size_t baseline_processes;
    size_t baseline_threads;
    size_t baseline_harts;
    struct micros_frame_ownership ownership_snapshot;
    struct micros_frame_allocator allocator_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_kernel_objects objects_observed;
    struct micros_endpoint_registry endpoint_snapshot;
    struct micros_grant_registry grant_snapshot;
    bool root_snapshot[GRANT_SYSCALL_PROCESS_COUNT];
    bool root_observed[GRANT_SYSCALL_PROCESS_COUNT];
    unsigned char page_table_snapshot[GRANT_SYSCALL_PROCESS_COUNT][3][MICROS_SV39_PAGE_SIZE];
    unsigned char page_table_observed[GRANT_SYSCALL_PROCESS_COUNT][3][MICROS_SV39_PAGE_SIZE];
    uint64_t user_physical_snapshot[GRANT_SYSCALL_PROCESS_COUNT][GRANT_SYSCALL_PAGE_COUNT];
    unsigned char user_bytes_snapshot[GRANT_SYSCALL_PROCESS_COUNT][GRANT_SYSCALL_PAGE_COUNT][MICROS_SV39_PAGE_SIZE];
    unsigned char user_bytes_observed[GRANT_SYSCALL_PROCESS_COUNT][GRANT_SYSCALL_PAGE_COUNT][MICROS_SV39_PAGE_SIZE];
    bool failure_snapshot_active;
};

extern const uint64_t TEST_CODE_ADDRESS;
extern const uint64_t TEST_STACK_ADDRESS;
extern const uint64_t TEST_DATA_ADDRESS;
extern const uint64_t TEST_DATA_SECOND_ADDRESS;
extern const uint64_t TEST_READ_ONLY_ADDRESS;
extern const uint64_t TEST_UNMAPPED_ADDRESS;
extern const uint64_t TEST_CONTROL_MAGIC;

extern const unsigned char micros_grant_syscall_payload_start[];
extern const unsigned char micros_grant_syscall_payload_ecall[];
extern const unsigned char micros_grant_syscall_payload_after_ecall[];
extern const unsigned char micros_grant_syscall_payload_spin[];
extern const unsigned char micros_grant_syscall_payload_end[];
extern const unsigned char micros_grant_syscall_test_supervisor_resume[];

struct grant_syscall_test_fixture *grant_syscall_test_context(void);

extern uintptr_t micros_grant_syscall_test_saved_sp;
extern uintptr_t micros_grant_syscall_test_saved_gp;
extern uintptr_t micros_grant_syscall_test_saved_tp;

void grant_syscall_test_copy_bytes(void *destination, const void *source, size_t size);

bool grant_syscall_test_bytes_equal(
    const void *left,
    const void *right,
    size_t size
);

void grant_syscall_test_fill_bytes(void *storage, unsigned char value, size_t size);

uint64_t grant_syscall_test_read_satp(void);

uint64_t grant_syscall_test_user_address_of(const unsigned char *symbol);

uint64_t grant_syscall_test_expected_satp(uint64_t root);

uint64_t grant_syscall_test_abi_result(int64_t result);

uint64_t grant_syscall_test_physical_for(
    struct grant_syscall_test_fixture *fixture,
    size_t process_index,
    uint64_t virtual_address
);

void grant_syscall_test_write_pattern(
    struct grant_syscall_test_fixture *fixture,
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char seed
);

bool grant_syscall_test_pattern_matches(
    struct grant_syscall_test_fixture *fixture,
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char seed
);

bool grant_syscall_test_range_is_value(
    struct grant_syscall_test_fixture *fixture,
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char value
);

void grant_syscall_test_fill_context_pattern(
    struct micros_user_context *context,
    uint64_t base
);

size_t grant_syscall_test_current_actor(
    struct grant_syscall_test_fixture *fixture,
    const struct micros_hart *hart
);

bool grant_syscall_test_test_mismatch(
    uint64_t stage,
    size_t actor,
    enum grant_syscall_command command,
    const struct micros_trap_frame *frame
);

bool grant_syscall_test_snapshot_failure_state(
    struct grant_syscall_test_fixture *fixture
);

bool grant_syscall_test_failure_state_matches(
    struct grant_syscall_test_fixture *fixture
);

bool grant_syscall_test_scheduler_state_valid(
    struct grant_syscall_test_fixture *fixture,
    const struct micros_hart *hart,
    size_t actor,
    bool after_return
);

void grant_syscall_test_arm_raw(
    struct grant_syscall_test_fixture *fixture,
    size_t actor,
    struct micros_user_context *context,
    enum grant_syscall_command command,
    uint64_t operation,
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5,
    uint64_t a6,
    uint64_t expected_result,
    bool token_result,
    bool preserve_state,
    enum micros_syscall_return expected_return
);

void grant_syscall_test_arm_control(struct micros_user_context *context);

bool grant_syscall_test_arm_command(
    struct grant_syscall_test_fixture *fixture,
    size_t actor,
    struct micros_user_context *context,
    enum grant_syscall_command command
);

bool grant_syscall_test_transition_after_return(
    struct grant_syscall_test_fixture *fixture,
    size_t actor,
    enum grant_syscall_command command,
    struct micros_trap_frame *frame
);

bool grant_syscall_test_validate_command_effect(
    struct grant_syscall_test_fixture *fixture,
    size_t actor,
    enum grant_syscall_command command,
    const struct micros_trap_frame *frame
);

bool grant_syscall_test_release_address_space(
    struct grant_syscall_test_fixture *fixture,
    struct micros_kernel_objects *objects,
    size_t actor
);

bool grant_syscall_test_release_thread(
    struct grant_syscall_test_fixture *fixture,
    struct micros_kernel_objects *objects,
    size_t actor
);

bool grant_syscall_test_close_endpoint(
    struct grant_syscall_test_fixture *fixture,
    struct micros_kernel_objects *objects,
    size_t actor
);

bool grant_syscall_test_reuse_grantor(
    struct grant_syscall_test_fixture *fixture
);

bool grant_syscall_test_prepare_process(
    struct grant_syscall_test_fixture *fixture,
    struct micros_kernel_objects *objects,
    size_t actor,
    const unsigned char *payload,
    size_t payload_size
);

bool grant_syscall_test_endpoint_registry_is_empty(
    struct grant_syscall_test_fixture *fixture
);

bool grant_syscall_test_scheduler_is_at_idle_baseline(
    const struct micros_kernel_objects *objects
);

#endif
