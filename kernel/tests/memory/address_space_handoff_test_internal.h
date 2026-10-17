#ifndef MICROS_KERNEL_ADDRESS_SPACE_HANDOFF_TEST_INTERNAL_H
#define MICROS_KERNEL_ADDRESS_SPACE_HANDOFF_TEST_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/tests/memory/address_space_handoff_test.h"
#include "kernel/boot/bootstrap_control_internal.h"
#include "kernel/grant/grant_runtime_internal.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/memory/user_address_space_internal.h"
#include "kernel/vm/vm_snapshot.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_core.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler_core.h"
#include "micros/sv39.h"
#include "micros/user_execution.h"
#include "micros/vm_bootstrap.h"

enum {
    HANDOFF_GRANTOR = 0,
    HANDOFF_GRANTEE,
    HANDOFF_PROCESS_COUNT,
    HANDOFF_PAGE_COUNT = 5,
    HANDOFF_RESERVED_PROCESS_COUNT = 2,
    HANDOFF_GRANTOR_MAPPING_COUNT = HANDOFF_PAGE_COUNT,
    HANDOFF_GRANTEE_MAPPING_COUNT = HANDOFF_PAGE_COUNT + 2,
    HANDOFF_TOTAL_MAPPING_COUNT =
        HANDOFF_GRANTOR_MAPPING_COUNT
        + HANDOFF_GRANTEE_MAPPING_COUNT,
};

enum handoff_test_state {
    HANDOFF_TEST_IDLE = 0,
    HANDOFF_TEST_USER_RUNNING,
    HANDOFF_TEST_SUPERVISOR,
};

enum handoff_syscall_command {
    HANDOFF_SYSCALL_NONE = 0,
    HANDOFF_SYSCALL_CREATE_READ,
    HANDOFF_SYSCALL_CREATE_WRITE,
    HANDOFF_SYSCALL_COPY_FROM_LOCAL,
    HANDOFF_SYSCALL_COPY_FROM_CROSS,
    HANDOFF_SYSCALL_COPY_TO_LOCAL,
    HANDOFF_SYSCALL_COPY_TO_CROSS,
    HANDOFF_SYSCALL_VALIDATE_READ,
    HANDOFF_SYSCALL_COPY_FROM_ZERO,
    HANDOFF_SYSCALL_COPY_TO_ZERO,
    HANDOFF_SYSCALL_WRONG_DIRECTION,
    HANDOFF_SYSCALL_RANGE,
    HANDOFF_SYSCALL_UNMAPPED,
    HANDOFF_SYSCALL_PERMISSION,
    HANDOFF_SYSCALL_REVOKE_READ,
    HANDOFF_SYSCALL_REVOKE_WRITE,
    HANDOFF_SYSCALL_STALE_READ,
    HANDOFF_SYSCALL_STALE_WRITE,
};

enum handoff_syscall_control_action {
    HANDOFF_SYSCALL_CONTROL_NONE = 0,
    HANDOFF_SYSCALL_CONTROL_SWITCH,
    HANDOFF_SYSCALL_CONTROL_FINISH,
};

struct handoff_syscall_script {
    bool armed;
    bool awaiting;
    bool token_result;
    bool preserve_state;
    enum handoff_syscall_command command;
    uint64_t expected_result;
    struct micros_user_context captured;
};

struct managed_inventory_context {
    size_t count;
    bool saw_uart;
};

#define TEST_CODE_ADDRESS MICROS_USER_VIRTUAL_BASE
#define TEST_STACK_ADDRESS \
    (MICROS_USER_VIRTUAL_BASE + MICROS_SV39_PAGE_SIZE)
#define TEST_DATA_ADDRESS \
    (MICROS_USER_VIRTUAL_BASE + UINT64_C(0x2000))
#define TEST_DATA_SECOND_ADDRESS \
    (MICROS_USER_VIRTUAL_BASE + UINT64_C(0x3000))
#define TEST_READ_ONLY_ADDRESS \
    (MICROS_USER_VIRTUAL_BASE + UINT64_C(0x4000))
#define TEST_TTY_SPARE_ADDRESS \
    (MICROS_USER_VIRTUAL_BASE + UINT64_C(0x5000))
#define TEST_UNMAPPED_ADDRESS \
    (MICROS_USER_VIRTUAL_BASE + UINT64_C(0x6000))
#define TEST_TTY_STACK_ADDRESS \
    (MICROS_TTY_UART_VIRTUAL_BASE + MICROS_SV39_PAGE_SIZE)
#define TEST_SYSCALL_CONTROL_MAGIC UINT64_C(0x48414e444f464653)

struct micros_address_space_handoff_test_context {
    struct micros_frame_ownership ownership_snapshot;
    struct micros_frame_allocator allocator_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_kernel_objects objects_observed;
    struct micros_endpoint_registry endpoint_snapshot;
    struct micros_grant_registry grant_snapshot;
    unsigned char
        kernel_stack_snapshot[MICROS_THREAD_KERNEL_STACK_SIZE];
    unsigned char page_table_snapshot[
        HANDOFF_PROCESS_COUNT
    ][3][MICROS_SV39_PAGE_SIZE];
    unsigned char page_table_observed[
        HANDOFF_PROCESS_COUNT
    ][3][MICROS_SV39_PAGE_SIZE];
    unsigned char user_bytes_snapshot[
        HANDOFF_PROCESS_COUNT
    ][HANDOFF_PAGE_COUNT][MICROS_SV39_PAGE_SIZE];
    unsigned char user_bytes_observed[
        HANDOFF_PROCESS_COUNT
    ][HANDOFF_PAGE_COUNT][MICROS_SV39_PAGE_SIZE];
    uint64_t user_physical[
        HANDOFF_PROCESS_COUNT
    ][HANDOFF_PAGE_COUNT];
    uint64_t tty_spare_physical;
    uint64_t tty_stack_physical;
    struct micros_process_handle
        reserved_processes[HANDOFF_RESERVED_PROCESS_COUNT];
    struct micros_process_handle processes[HANDOFF_PROCESS_COUNT];
    struct micros_thread_handle threads[HANDOFF_PROCESS_COUNT];
    micros_endpoint_t endpoints[HANDOFF_PROCESS_COUNT];
    const struct micros_vm_boot_info *handoff_snapshot_info;
    struct handoff_syscall_script
        syscall_scripts[HANDOFF_PROCESS_COUNT];
    micros_grant_t syscall_read_grant;
    micros_grant_t syscall_write_grant;
    enum handoff_syscall_control_action syscall_control_action;
    size_t syscall_control_next;
    volatile enum handoff_test_state test_state;
    uint64_t failure_stage;
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    struct micros_endpoint_registry *endpoint_registry;
    struct micros_grant_registry *grant_registry;
    struct micros_bootstrap_control_state tty_bootstrap;
    struct micros_bootstrap_binding tty_binding;
    struct micros_bootstrap_manifest_entry tty_entry;
    struct micros_process_handle empty_process;
    struct micros_user_context contexts[HANDOFF_PROCESS_COUNT];
    struct micros_user_context observed_context;
    struct micros_syscall_arguments grant_arguments;
    struct micros_syscall_context grant_context;
    struct micros_trap_frame grant_frame;
    struct micros_hart grant_hart;
    struct micros_thread thread_snapshot;
    struct micros_ipc_message message;
    struct micros_ipc_message observed_message;
    struct micros_frame_owner process_user_owner;
    struct micros_frame_owner retained_owner;
    struct micros_frame_owner saved_owner;
    struct micros_grant_record grant_record;
    struct micros_user_address_space_tty_uart_plan tty_map_plan;
    struct micros_user_address_space_tty_uart_plan tty_map_sentinel;
    struct micros_tty_handoff mapped_handoff;
    struct micros_tty_handoff mapped_handoff_sentinel;
    struct micros_tty_handoff_runtime_state *tty_runtime;
    struct managed_inventory_context inventory;
    micros_grant_t read_grant;
    micros_grant_t write_grant;
    micros_grant_t device_copy_grant;
    uint64_t *tty_spare_pte;
    uint64_t *tty_uart_pte;
    uint64_t *corrupt_pte;
    uint64_t saved_spare_pte;
    uint64_t saved_uart_pte;
    uint64_t saved_pte;
    uint64_t frame_index;
    uint64_t inventory_root;
    uint64_t tty_root;
    uint64_t orphan_physical;
    uint64_t physical_address;
    uint64_t stale_snapshot_frame;
    uint32_t permissions;
    size_t contiguous;
    size_t inventory_count;
    uintptr_t kernel_stack_bottom;
    uintptr_t kernel_stack_top;
    uintptr_t saved_status;
    size_t payload_size;
};

struct micros_address_space_handoff_test_context *
micros_address_space_handoff_test_context(void);

void handoff_test_copy_bytes(
    struct micros_address_space_handoff_test_context *fixture,
    void *destination,
    const void *source,
    size_t size
);
bool handoff_test_bytes_equal(
    struct micros_address_space_handoff_test_context *fixture,
    const void *left,
    const void *right,
    size_t size
);
void handoff_test_fill_bytes(
    struct micros_address_space_handoff_test_context *fixture,
    void *storage,
    unsigned char value,
    size_t size
);

uint64_t handoff_test_physical_for(
    struct micros_address_space_handoff_test_context *fixture,
    size_t process_index,
    uint64_t virtual_address
);
void handoff_test_write_pattern(
    struct micros_address_space_handoff_test_context *fixture,
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char seed
);
bool handoff_test_pattern_matches(
    struct micros_address_space_handoff_test_context *fixture,
    size_t process_index,
    uint64_t virtual_address,
    size_t length,
    unsigned char seed
);
void handoff_test_write_virtual(
    struct micros_address_space_handoff_test_context *fixture,
    size_t process_index,
    uint64_t virtual_address,
    const void *source,
    size_t size
);
bool handoff_test_virtual_matches(
    struct micros_address_space_handoff_test_context *fixture,
    size_t process_index,
    uint64_t virtual_address,
    const void *expected,
    size_t size
);
bool handoff_test_frame_index_for_physical(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    uint64_t *frame_index
);
uint64_t *handoff_test_leaf_pte_for(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    uint64_t virtual_address
);
void handoff_test_prepare_context(
    struct micros_address_space_handoff_test_context *fixture,
    struct micros_user_context *context,
    size_t process_index
);
uint64_t handoff_test_user_address_of(
    struct micros_address_space_handoff_test_context *fixture,
    const unsigned char *symbol
);
uint64_t handoff_test_expected_satp(uint64_t root);
uint64_t handoff_test_abi_result(int64_t result);
size_t handoff_test_current_actor(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_hart *hart
);
bool handoff_test_scheduler_state_valid(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_hart *hart,
    size_t actor,
    bool after_return
);
bool handoff_test_syscall_mismatch(
    struct micros_address_space_handoff_test_context *fixture,
    uint64_t stage,
    size_t actor,
    enum handoff_syscall_command command,
    const struct micros_trap_frame *frame
);

bool handoff_test_snapshot_state(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_grant_registry *grant_registry
);
bool handoff_test_state_matches(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_grant_registry *grant_registry
);
bool handoff_test_snapshot_syscall_state(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_grant_registry *grant_registry
);
bool handoff_test_syscall_state_matches(
    struct micros_address_space_handoff_test_context *fixture,
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_grant_registry *grant_registry
);
bool handoff_test_stage_wired_pages(
    struct micros_address_space_handoff_test_context *fixture
);
bool handoff_test_validate_wired_snapshot(
    struct micros_address_space_handoff_test_context *fixture
);

bool handoff_test_record_managed_mapping(
    void *context,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint32_t permissions
);

extern const unsigned char micros_address_space_handoff_payload_start[];
extern const unsigned char micros_address_space_handoff_payload_ecall[];
extern const unsigned char
    micros_address_space_handoff_payload_after_ecall[];
extern const unsigned char micros_address_space_handoff_payload_spin[];
extern const unsigned char micros_address_space_handoff_payload_end[];
extern const unsigned char
    micros_address_space_handoff_test_supervisor_resume[];
extern const uint64_t micros_address_space_handoff_test_saved_state[17];
extern const uint64_t micros_address_space_handoff_test_restored_state[17];

void micros_address_space_handoff_test_enter(
    uint64_t thread_slot,
    uint64_t thread_generation
);

_Noreturn void micros_address_space_handoff_test_enter_production(
    uint64_t thread_slot,
    uint64_t thread_generation
);

bool handoff_test_setup_runtime(
    struct micros_address_space_handoff_test_context *fixture
);
bool handoff_test_setup_processes(
    struct micros_address_space_handoff_test_context *fixture
);
bool handoff_test_setup_execution_and_grants(
    struct micros_address_space_handoff_test_context *fixture
);
bool handoff_test_verify_pre_handoff_access(
    struct micros_address_space_handoff_test_context *fixture
);
bool handoff_test_prepare_and_commit_handoff(
    struct micros_address_space_handoff_test_context *fixture
);
bool handoff_test_verify_post_handoff_runtime(
    struct micros_address_space_handoff_test_context *fixture
);
bool handoff_test_verify_post_handoff_failures(
    struct micros_address_space_handoff_test_context *fixture
);
bool handoff_test_verify_corruption_rejection(
    struct micros_address_space_handoff_test_context *fixture
);
bool handoff_test_verify_mutation_revocation(
    struct micros_address_space_handoff_test_context *fixture
);
bool handoff_test_run_real_syscalls(
    struct micros_address_space_handoff_test_context *fixture
);
bool handoff_test_verify_tty_mapping(
    struct micros_address_space_handoff_test_context *fixture
);

#endif
