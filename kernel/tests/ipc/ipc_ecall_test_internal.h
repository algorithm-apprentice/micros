#ifndef MICROS_KERNEL_IPC_ECALL_TEST_INTERNAL_H
#define MICROS_KERNEL_IPC_ECALL_TEST_INTERNAL_H

#include "kernel/tests/ipc/ipc_ecall_test.h"
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
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

#define IPC_ECALL_TEST_SSTATUS_UBE (UINT64_C(1) << 6)
#define IPC_ECALL_TEST_SSTATUS_VS (UINT64_C(3) << 9)
#define IPC_ECALL_TEST_SSTATUS_FS (UINT64_C(3) << 13)
#define IPC_ECALL_TEST_SSTATUS_XS (UINT64_C(3) << 15)
#define IPC_ECALL_TEST_SSTATUS_MXR (UINT64_C(1) << 19)
#define IPC_ECALL_TEST_SSTATUS_SD (UINT64_C(1) << 63)

#define IPC_ECALL_TEST_CODE_VIRTUAL_ADDRESS (MICROS_USER_VIRTUAL_BASE)
#define IPC_ECALL_TEST_DATA_VIRTUAL_ADDRESS (MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00002000))
#define IPC_ECALL_TEST_STACK_VIRTUAL_ADDRESS (MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00004000))
#define IPC_ECALL_TEST_CONTROL_OFFSET (UINT64_C(0x80))
#define IPC_ECALL_TEST_A4 (UINT64_C(0x4444444444444444))
#define IPC_ECALL_TEST_A5 (UINT64_C(0x5555555555555555))
#define IPC_ECALL_TEST_A6 (UINT64_C(0x6666666666666666))

enum {
    IPC_ECALL_CLIENT = 0,
    IPC_ECALL_SERVER,
    IPC_ECALL_PROCESS_COUNT,
};

extern const unsigned char micros_ipc_ecall_test_payload_start[];
extern const unsigned char micros_ipc_ecall_test_payload_done[];
extern const unsigned char micros_ipc_ecall_test_payload_end[];
extern const unsigned char micros_ipc_ecall_test_supervisor_resume[];

struct ipc_ecall_test_context {
    struct micros_process_handle processes[IPC_ECALL_PROCESS_COUNT];
    struct micros_thread_handle client_thread;
    micros_endpoint_t endpoints[IPC_ECALL_PROCESS_COUNT];
    struct micros_endpoint_registry *registry;
    uint64_t code_physical;
    uint64_t data_physical;
    uint64_t stack_physical;
    uint64_t baseline_owned;
    uint64_t baseline_free;
    size_t baseline_processes;
    size_t baseline_threads;
    size_t baseline_harts;
};

struct ipc_ecall_setup_state {
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    struct micros_user_context context;
    struct micros_ipc_message *message;
    uintptr_t kernel_stack_bottom;
    uintptr_t kernel_stack_top;
    size_t payload_size;
    size_t index;
};

extern uintptr_t micros_ipc_ecall_test_saved_sp;
extern uintptr_t micros_ipc_ecall_test_saved_gp;
extern uintptr_t micros_ipc_ecall_test_saved_tp;
extern const struct micros_privilege_profile ipc_ecall_test_profiles[2];
extern const uint8_t ipc_ecall_test_profile_ids[IPC_ECALL_PROCESS_COUNT];
struct ipc_ecall_test_context *ipc_ecall_test_context_get(void);
bool ipc_ecall_test_prepare_environment(struct ipc_ecall_test_context *fixture, struct ipc_ecall_setup_state *state);
_Noreturn void ipc_ecall_test_setup_failure(uintptr_t saved_status);
void ipc_ecall_test_clear_bytes(void *storage, size_t size);
void ipc_ecall_test_copy_bytes(void *destination, const void *source, size_t size);
uint64_t ipc_ecall_test_user_address_of(const unsigned char *symbol);
bool ipc_ecall_test_thread_ipc_state_is_clear(
    const struct micros_thread *thread
);

#endif
