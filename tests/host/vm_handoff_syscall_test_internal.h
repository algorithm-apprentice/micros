#ifndef MICROS_VM_HANDOFF_SYSCALL_TEST_INTERNAL_H
#define MICROS_VM_HANDOFF_SYSCALL_TEST_INTERNAL_H

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/endpoint/endpoint_internal.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/pm_control_runtime.h"
#include "kernel/platform/plic.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/memory/user_address_space_internal.h"
#include "kernel/vm_handoff_runtime.h"
#include "kernel/vm_handoff_syscall.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/panic.h"

enum {
    TEST_LAUNCHER_SERVICE_ID = 1,
    TEST_VM_SERVICE_ID = 2,
    TEST_VM_PROFILE_ID = 2,
};

enum test_vm_operation {
    TEST_VM_OPERATION_NONE = 0,
    TEST_VM_OPERATION_SCRATCH_MAP,
    TEST_VM_OPERATION_SCRATCH_COMMIT,
    TEST_VM_OPERATION_FREEZE,
    TEST_VM_OPERATION_ABORT_LOADING,
    TEST_VM_OPERATION_ABORT_FROZEN,
    TEST_VM_OPERATION_EXIT_RELEASE,
};

struct vm_handoff_syscall_test_context {
    _Alignas(MICROS_SV39_PAGE_SIZE)
    unsigned char user_memory[3 * MICROS_SV39_PAGE_SIZE];
    struct micros_process_handle vm_process;
    struct micros_thread_handle vm_thread;
    struct micros_thread_handle launcher_thread;
    struct micros_process_handle foreign_process;
    struct micros_thread_handle foreign_thread;
    struct micros_process_handle tty_process;
    struct micros_process_handle child_process;
    micros_endpoint_t launcher_endpoint;
    micros_endpoint_t vm_endpoint;
    micros_endpoint_t tty_endpoint;
    struct micros_kernel_objects objects;
    struct micros_endpoint_registry registry;
    struct micros_process vm_process_record;
    struct micros_process foreign_process_record;
    struct micros_process child_process_record;
    struct micros_thread vm_thread_record;
    struct micros_thread launcher_thread_record;
    struct micros_thread foreign_thread_record;
    struct micros_endpoint_record launcher_endpoint_record;
    struct micros_endpoint_record vm_endpoint_record;
    struct micros_bootstrap_control_state bootstrap;
    struct micros_vm_handoff_state vm_state;
    struct micros_pm_control_state pm_state;
    struct micros_tty_handoff_runtime_state tty_state;
    struct micros_frame_ownership ownership;
    jmp_buf panic_target;
    bool panic_armed;
    bool mapping_committed;
    bool notification_committed;
    bool notification_should_fail;
    bool order_violation;
    int operation_stage;
    enum test_vm_operation vm_operation;
    int vm_operation_stage;
    bool vm_order_violation;
    bool mutate_freeze_input;
    bool abort_has_scratch;
    enum micros_frame_ownership_error claim_preflight_error;
    enum micros_user_address_space_error scratch_mapping_error;
    enum micros_bootstrap_diagnostic_reason failure_reason;
};

struct vm_handoff_syscall_test_context *
vm_handoff_syscall_test_context(void);

bool vm_handoff_syscall_test_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
);

bool vm_handoff_syscall_test_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
);

bool vm_handoff_syscall_test_bytes_are_zero(
    const void *storage,
    size_t size
);

void vm_handoff_syscall_test_reset(
    struct vm_handoff_syscall_test_context *fixture
);

struct micros_syscall_arguments
vm_handoff_syscall_test_mapping_arguments(
    struct vm_handoff_syscall_test_context *fixture
);

struct micros_syscall_arguments
vm_handoff_syscall_test_load_begin_arguments(void);

bool vm_handoff_syscall_test_invoke_vm_command(
    struct vm_handoff_syscall_test_context *fixture,
    const struct micros_syscall_arguments *arguments,
    struct micros_trap_frame *frame
);

bool vm_handoff_syscall_test_invoke_vm_command_panics(
    struct vm_handoff_syscall_test_context *fixture,
    const struct micros_syscall_arguments *arguments,
    struct micros_trap_frame *frame
);

struct micros_syscall_arguments
vm_handoff_syscall_test_scratch_map_arguments(
    uint64_t load_token,
    size_t page_index
);

struct micros_syscall_arguments
vm_handoff_syscall_test_scratch_commit_arguments(
    const struct micros_syscall_arguments *map
);

bool vm_handoff_syscall_test_begin_load(
    struct vm_handoff_syscall_test_context *fixture,
    struct micros_trap_frame *frame
);

bool vm_handoff_syscall_test_append_load_page(
    struct vm_handoff_syscall_test_context *fixture,
    size_t page_index
);

bool vm_handoff_syscall_test_fill_minimum_load(
    struct vm_handoff_syscall_test_context *fixture
);

struct micros_vm_load_freeze_plan *
vm_handoff_syscall_test_prepare_freeze_plan(
    struct vm_handoff_syscall_test_context *fixture
);

bool vm_handoff_syscall_test_freeze_load(
    struct vm_handoff_syscall_test_context *fixture,
    bool mutate_input,
    uint64_t *mapping_generation
);

bool vm_handoff_syscall_test_invoke_mapping(
    struct vm_handoff_syscall_test_context *fixture,
    struct micros_process_handle process,
    struct micros_thread_handle thread,
    struct micros_trap_frame *frame
);

bool vm_handoff_syscall_test_load_begin_dispatches(
    struct vm_handoff_syscall_test_context *fixture
);
bool vm_handoff_syscall_test_load_begin_failures(
    struct vm_handoff_syscall_test_context *fixture
);
bool vm_handoff_syscall_test_phase_and_authority(
    struct vm_handoff_syscall_test_context *fixture
);
bool vm_handoff_syscall_test_freeze_failure(
    struct vm_handoff_syscall_test_context *fixture
);
bool vm_handoff_syscall_test_invariants_are_fatal(
    struct vm_handoff_syscall_test_context *fixture
);
bool vm_handoff_syscall_test_lifecycle_commit_and_exit(
    struct vm_handoff_syscall_test_context *fixture
);
bool vm_handoff_syscall_test_abort_loading_scratch(
    struct vm_handoff_syscall_test_context *fixture
);
bool vm_handoff_syscall_test_abort_loading_clear(
    struct vm_handoff_syscall_test_context *fixture
);
bool vm_handoff_syscall_test_abort_frozen(
    struct vm_handoff_syscall_test_context *fixture
);
bool vm_handoff_syscall_test_successful_mapping(
    struct vm_handoff_syscall_test_context *fixture
);
bool vm_handoff_syscall_test_unauthorized_mapping(
    struct vm_handoff_syscall_test_context *fixture
);
bool vm_handoff_syscall_test_wrong_phase(
    struct vm_handoff_syscall_test_context *fixture
);
bool vm_handoff_syscall_test_notification_atomicity(
    struct vm_handoff_syscall_test_context *fixture
);
bool vm_handoff_syscall_test_uncleared_console_event(
    struct vm_handoff_syscall_test_context *fixture,
    int event_state
);

#endif
