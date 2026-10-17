#ifndef MICROS_KERNEL_IPC_SYSCALL_TEST_INTERNAL_H
#define MICROS_KERNEL_IPC_SYSCALL_TEST_INTERNAL_H

#include "kernel/tests/ipc/ipc_syscall_test.h"
#include <stddef.h>
#include <stdint.h>
#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_abi.h"
#include "micros/ipc_core.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/scheduler_core.h"
#include "micros/sv39.h"
#include "micros/timer.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

#define IPC_SYSCALL_TEST_SSTATUS_UBE (UINT64_C(1) << 6)
#define IPC_SYSCALL_TEST_SSTATUS_VS (UINT64_C(3) << 9)
#define IPC_SYSCALL_TEST_SSTATUS_FS (UINT64_C(3) << 13)
#define IPC_SYSCALL_TEST_SSTATUS_XS (UINT64_C(3) << 15)
#define IPC_SYSCALL_TEST_SSTATUS_MXR (UINT64_C(1) << 19)
#define IPC_SYSCALL_TEST_SSTATUS_SD (UINT64_C(1) << 63)

#define IPC_SYSCALL_TEST_CODE_VIRTUAL_ADDRESS (MICROS_USER_VIRTUAL_BASE)
#define IPC_SYSCALL_TEST_DATA_VIRTUAL_ADDRESS (MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00002000))
#define IPC_SYSCALL_TEST_DATA_SECOND_VIRTUAL_ADDRESS (MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00003000))
#define IPC_SYSCALL_TEST_STACK_VIRTUAL_ADDRESS (MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00004000))
#define IPC_SYSCALL_TEST_OUTBOUND_ADDRESS (IPC_SYSCALL_TEST_DATA_SECOND_VIRTUAL_ADDRESS - UINT64_C(32))
#define IPC_SYSCALL_TEST_INBOUND_ADDRESS (IPC_SYSCALL_TEST_DATA_VIRTUAL_ADDRESS + UINT64_C(0x100))
#define IPC_SYSCALL_TEST_TIMER_INTERVAL (UINT64_C(0x0000000010000000))
#define IPC_SYSCALL_TEST_DEFERRED_TIMER_INTERVAL (UINT64_C(0x00100000))
#define IPC_SYSCALL_TEST_SERVER_PRIORITY ((uint8_t)(MICROS_SCHEDULER_PRIORITY_DEFAULT_USER - 2))
#define IPC_SYSCALL_TEST_PEER_PRIORITY ((uint8_t)(MICROS_SCHEDULER_PRIORITY_DEFAULT_USER - 1))
#define IPC_SYSCALL_TEST_BACKGROUND_PRIORITY ((uint8_t)(MICROS_SCHEDULER_PRIORITY_DEFAULT_USER + 1))

enum {
    IPC_SYSCALL_CLIENT = 0,
    IPC_SYSCALL_SERVER,
    IPC_SYSCALL_PEER,
    IPC_SYSCALL_PROCESS_COUNT,
};

enum ipc_syscall_command {
    IPC_COMMAND_NONE = 0,
    IPC_COMMAND_CLIENT_SEND_ONE,
    IPC_COMMAND_SERVER_RECEIVE_SEND_ONE,
    IPC_COMMAND_SERVER_WAIT_CALL_ONE,
    IPC_COMMAND_CLIENT_CALL_ONE,
    IPC_COMMAND_SERVER_REPLY_ONE,
    IPC_COMMAND_SERVER_WAIT_CALL_TWO,
    IPC_COMMAND_CLIENT_CALL_TWO,
    IPC_COMMAND_SERVER_REPLY_RECEIVE_TWO,
    IPC_COMMAND_CLIENT_WAIT_SERVER_CALL,
    IPC_COMMAND_PEER_SEND,
    IPC_COMMAND_SERVER_CALL_CLIENT,
    IPC_COMMAND_PEER_NOTIFY_ONE,
    IPC_COMMAND_PEER_NOTIFY_TWO,
    IPC_COMMAND_PEER_BLOCK,
    IPC_COMMAND_CLIENT_REPLY_THREE,
    IPC_COMMAND_SERVER_RECEIVE_NOTIFY,
    IPC_COMMAND_SERVER_WAKE_PEER,
    IPC_COMMAND_SERVER_BLOCK_FINAL,
    IPC_COMMAND_CLIENT_ERROR_UNAUTHORIZED,
    IPC_COMMAND_CLIENT_ERROR_ARGUMENT,
    IPC_COMMAND_CLIENT_ERROR_FAULT,
};

enum ipc_token_expectation {
    IPC_TOKEN_ZERO = 0,
    IPC_TOKEN_NONZERO,
};

struct ipc_thread_script {
    bool armed;
    bool awaiting;
    enum ipc_syscall_command command;
    uint64_t expected_result;
    struct micros_user_context captured;
};

struct ipc_negative_buffer_snapshot {
    struct micros_ipc_message inbound;
    struct micros_ipc_message outbound;
};

extern const unsigned char micros_ipc_syscall_test_payload_start[];
extern const unsigned char micros_ipc_syscall_test_payload_ecall[];
extern const unsigned char micros_ipc_syscall_test_payload_after_ecall[];
extern const unsigned char micros_ipc_syscall_test_payload_spin[];
extern const unsigned char micros_ipc_syscall_test_payload_end[];
extern const unsigned char micros_ipc_syscall_test_supervisor_resume[];

struct ipc_syscall_test_context {
    struct micros_process_handle processes[IPC_SYSCALL_PROCESS_COUNT];
    struct micros_thread_handle threads[IPC_SYSCALL_PROCESS_COUNT];
    micros_endpoint_t endpoints[IPC_SYSCALL_PROCESS_COUNT];
    struct micros_endpoint_registry *registry;
    struct ipc_thread_script scripts[IPC_SYSCALL_PROCESS_COUNT];
    uint64_t code_physical[IPC_SYSCALL_PROCESS_COUNT];
    uint64_t data_physical[IPC_SYSCALL_PROCESS_COUNT][2];
    uint64_t stack_physical[IPC_SYSCALL_PROCESS_COUNT];
    uint64_t baseline_owned;
    uint64_t baseline_free;
    size_t baseline_processes;
    size_t baseline_threads;
    size_t baseline_harts;
    uint64_t token_one;
    uint64_t token_two;
    uint64_t token_three;
    bool finish_ready;
    size_t scenario_phase;
    bool timer_completion_started;
    struct micros_user_context timer_expected_context;
    bool negative_snapshot_active;
    enum ipc_syscall_command negative_snapshot_command;
    struct micros_endpoint_registry negative_registry_snapshot;
    struct micros_kernel_objects negative_objects_snapshot;
    struct micros_kernel_objects negative_objects_candidate;
    struct ipc_negative_buffer_snapshot negative_buffer_snapshots[IPC_SYSCALL_PROCESS_COUNT];
};

struct ipc_syscall_setup_state {
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    size_t payload_size;
    size_t index;
};

extern uintptr_t micros_ipc_syscall_test_saved_sp;
extern uintptr_t micros_ipc_syscall_test_saved_gp;
extern uintptr_t micros_ipc_syscall_test_saved_tp;
extern const size_t ipc_syscall_test_scenario_threads[21];
extern const enum ipc_syscall_command ipc_syscall_test_scenario_commands[21];
struct ipc_syscall_test_context *ipc_syscall_test_context_get(void);
bool ipc_syscall_test_prepare_environment(struct ipc_syscall_test_context *fixture, struct ipc_syscall_setup_state *state);
_Noreturn void ipc_syscall_test_setup_failure(uintptr_t saved_status);
bool ipc_syscall_test_test_mismatch(
    uint64_t stage,
    size_t thread_index,
    enum ipc_syscall_command command
);
void ipc_syscall_test_clear_bytes(void *storage, size_t size);
void ipc_syscall_test_copy_bytes(void *destination, const void *source, size_t size);
bool ipc_syscall_test_bytes_equal(
    const void *left,
    const void *right,
    size_t size
);
void ipc_syscall_test_fill_context_pattern(
    struct micros_user_context *context,
    uint64_t base
);
uint64_t ipc_syscall_test_user_address_of(const unsigned char *symbol);
size_t ipc_syscall_test_current_thread_index(struct ipc_syscall_test_context *fixture, const struct micros_hart *hart);
void ipc_syscall_test_copy_to_user_buffer(
    struct ipc_syscall_test_context *fixture,
    size_t process_index,
    uint64_t user_address,
    const struct micros_ipc_message *message
);
void ipc_syscall_test_copy_from_user_buffer(
    struct ipc_syscall_test_context *fixture,
    size_t process_index,
    uint64_t user_address,
    struct micros_ipc_message *message
);
bool ipc_syscall_test_command_has_stable_error(
    enum ipc_syscall_command command
);
bool ipc_syscall_test_snapshot_negative_state(
    struct ipc_syscall_test_context *fixture,
    const struct micros_kernel_objects *objects,
    enum ipc_syscall_command command
);
bool ipc_syscall_test_negative_state_matches(
    struct ipc_syscall_test_context *fixture,
    const struct micros_kernel_objects *objects,
    enum ipc_syscall_command command
);
void ipc_syscall_test_fill_message(
    struct ipc_syscall_test_context *fixture,
    size_t process_index,
    uint64_t user_address,
    uint32_t type,
    uint8_t seed
);
bool ipc_syscall_test_message_matches(
    struct ipc_syscall_test_context *fixture,
    size_t process_index,
    uint64_t user_address,
    micros_endpoint_t source,
    uint32_t type,
    uint8_t seed,
    enum ipc_token_expectation token_expectation,
    uint64_t *token
);
bool ipc_syscall_test_notification_matches(
    struct ipc_syscall_test_context *fixture,
    size_t process_index,
    micros_endpoint_t source,
    uint64_t mask
);
void ipc_syscall_test_arm_command(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    uint64_t operation,
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3
);
void ipc_syscall_test_arm_send(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    micros_endpoint_t destination,
    uint32_t type,
    uint8_t seed
);
void ipc_syscall_test_arm_receive(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    micros_endpoint_t source
);
void ipc_syscall_test_arm_call(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    micros_endpoint_t destination,
    uint32_t type,
    uint8_t seed
);
void ipc_syscall_test_arm_reply(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    uint64_t token,
    uint32_t type,
    uint8_t seed
);
void ipc_syscall_test_arm_reply_receive(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    struct micros_user_context *context,
    uint64_t token,
    uint32_t type,
    uint8_t seed
);
void ipc_syscall_test_arm_notify(
    struct ipc_syscall_test_context *fixture,
    size_t thread_index,
    struct micros_user_context *context,
    enum ipc_syscall_command command,
    micros_endpoint_t destination,
    uint64_t mask
);
bool ipc_syscall_test_transition_after_return(
    struct ipc_syscall_test_context *fixture,
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    size_t thread_index,
    enum ipc_syscall_command command
);
bool ipc_syscall_test_prepare_process(
    struct ipc_syscall_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t index,
    const unsigned char *payload,
    size_t payload_size
);
bool ipc_syscall_test_cleanup_process(
    struct ipc_syscall_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t index
);

#endif
