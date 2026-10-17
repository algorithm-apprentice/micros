#include "kernel/boot/boot_internal.h"

#include "arch/riscv64/platform.h"
#include "micros/bootstrap_memory.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "kernel/platform/plic.h"
#include "micros/timer.h"
#include "micros/trap.h"

#ifdef MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST
#include "kernel/tests/memory/address_space_handoff_test.h"
#endif
#if defined(MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST) \
    || defined(MICROS_BUILD_BOOTSTRAP_READY_TIMEOUT_TEST) \
    || defined(MICROS_BUILD_BOOTSTRAP_MANIFEST_PANIC_TEST)
#include "kernel/tests/boot/bootstrap_test.h"
#endif
#if defined(MICROS_BUILD_VM_HANDOFF_TEST) \
    || defined(MICROS_BUILD_VM_READY_EARLY_TEST) \
    || defined(MICROS_BUILD_VM_SELF_FAULT_TEST) \
    || defined(MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST)
#include "kernel/vm_handoff_test.h"
#endif
#ifdef MICROS_BUILD_PM_SERVICE_TEST
#include "kernel/tests/pm/pm_service_test.h"
#endif
#ifdef MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST
#include "kernel/vm_scratch_preemption_test.h"
#endif
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
#include "kernel/tty_service_test.h"
#endif
#ifdef MICROS_BUILD_RAMFS_SERVICE_TEST
#include "kernel/tests/ramfs/ramfs_service_test.h"
#endif
#ifdef MICROS_BUILD_VFS_SERVICE_TEST
#include "kernel/vfs_service_test.h"
#endif
#ifdef MICROS_BUILD_ELF_SPAWN_TEST
#include "kernel/elf_spawn_test.h"
#endif
#ifdef MICROS_BUILD_UART_CONSOLE_TEST
#include "kernel/uart_console_test.h"
#endif

#ifdef MICROS_BUILD_OBJECT_MODEL_TEST
bool micros_kernel_object_runtime_run_self_test(void);
#endif
#ifdef MICROS_BUILD_ENDPOINT_TEST
bool micros_endpoint_runtime_run_self_test(void);
#endif
#ifdef MICROS_BUILD_GRANT_TEST
bool micros_grant_runtime_run_self_test(void);
#endif
#ifdef MICROS_BUILD_IPC_TEST
bool micros_ipc_runtime_run_self_test(void);
#endif
#ifdef MICROS_BUILD_IPC_ECALL_CORE_TEST
_Noreturn void micros_ipc_ecall_core_runtime_run_self_test(void);
#endif
#ifdef MICROS_BUILD_IPC_SYSCALL_TEST
_Noreturn void micros_ipc_syscall_runtime_run_self_test(void);
#endif
#ifdef MICROS_BUILD_IPC_SYSCALL_PANIC_TEST
_Noreturn void micros_ipc_syscall_panic_runtime_run_self_test(void);
#endif
#ifdef MICROS_BUILD_GRANT_SYSCALL_TEST
_Noreturn void micros_grant_syscall_runtime_run_self_test(void);
#endif
#ifdef MICROS_BUILD_USER_RUNTIME_TEST
_Noreturn void micros_user_runtime_runtime_run_self_test(void);
#endif
#ifdef MICROS_BUILD_FRAME_OWNERSHIP_TEST
bool micros_frame_ownership_runtime_run_self_test(void);
#endif
#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
bool micros_user_address_space_runtime_run_self_test(void);
#endif
#ifdef MICROS_BUILD_USER_EXECUTION_TEST
_Noreturn void micros_user_execution_runtime_run_self_test(void);
#endif
#ifdef MICROS_BUILD_SCHEDULER_TEST
_Noreturn void micros_scheduler_runtime_run_self_test(void);
#endif
#if defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
_Noreturn void micros_scheduler_invalid_runtime_run_self_test(void);
#endif
#ifdef MICROS_BUILD_NESTED_TRAP_TEST
void micros_nested_trap_test_trigger(void);
extern unsigned char
    micros_nested_trap_test_emergency_stack_bottom[];
extern unsigned char
    micros_nested_trap_test_emergency_stack_top[];
#endif

void boot_test_foundation(const struct boot_context *boot)
{
    (void)boot;
#ifdef MICROS_BUILD_UART_CONSOLE_TEST
    micros_uart_console_runtime_run_self_test(boot->hart_id);
#endif

#ifdef MICROS_BUILD_PANIC_TEST
    MICROS_PANIC(boot->hart_id, "intentional-test");
#endif

#ifdef MICROS_BUILD_TRAP_TEST
    if (!micros_trap_run_self_test()) {
        MICROS_PANIC(boot->hart_id, "trap-test-restore");
    }
    uart_write(
        "MICROS_TRAP_TEST_PASS "
        "origin=S cause=illegal-instruction registers=preserved "
        "hart-context=routed primary-stack=selected "
        "sscratch=anchor\n"
    );
    uart_flush();
#endif

#ifdef MICROS_BUILD_TRAP_PANIC_TEST
    micros_trap_run_panic_test();
#endif

#ifdef MICROS_BUILD_TIMER_TEST
    if (
        !micros_timer_run_self_test(
            boot->boot_hart,
            UINT64_C(0x00000000000186a0),
            UINT64_C(3)
        )
    ) {
        MICROS_PANIC(boot->hart_id, "timer-test-failed");
    }
    uart_write("MICROS_TIMER_TEST_PASS ticks=");
    uart_write_hex64(micros_timer_ticks(boot->boot_hart));
    uart_write(" interval=");
    uart_write_hex64(UINT64_C(0x00000000000186a0));
    uart_write(" active=");
    uart_write_hex64(boot->boot_hart->timer.active ? 1 : 0);
    uart_write(" deadline=");
    uart_write_hex64(boot->boot_hart->timer.deadline);
    uart_write(" owner=hart");
    uart_write("\n");
    uart_flush();
#endif

#ifdef MICROS_BUILD_FRAME_ALLOCATOR_TEST
    if (!micros_bootstrap_memory_run_self_test(&boot->memory_map)) {
        MICROS_PANIC(boot->hart_id, "frame-allocator-test");
    }
    uart_write(
        "MICROS_FRAME_ALLOCATOR_TEST_PASS "
        "allocations=0x0000000000000004 "
        "reuse=lowest invariants=preserved\n"
    );
    uart_flush();
#endif

#ifdef MICROS_BUILD_MMU_TEST
    if (
        !micros_kernel_address_space_run_self_test()
        || !micros_plic_runtime_run_self_test()
    ) {
        MICROS_PANIC(boot->hart_id, "mmu-test-failed");
    }
    uart_write(
        "MICROS_MMU_TEST_PASS "
        "store-fault=text execute-fault=writable "
        "traps=0x0000000000000002\n"
    );
    uart_write(
        "MICROS_TTY_CONTROLLER_TEST_PASS "
        "mapping=exact priority=disabled enable=disabled "
        "context=supervisor seie=independent\n"
    );
    uart_flush();
#endif
}

void boot_test_address_spaces(const struct boot_context *boot)
{
    (void)boot;
#ifdef MICROS_BUILD_FRAME_OWNERSHIP_TEST
    if (!micros_frame_ownership_runtime_run_self_test()) {
        MICROS_PANIC(boot->hart_id, "frame-ownership-test");
    }
    uart_write(
        "MICROS_FRAME_OWNERSHIP_TEST_PASS "
        "stale=rejected release=blocked "
        "handoff=atomic invariants=preserved\n"
    );
    uart_flush();
#endif

#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
    if (!micros_user_address_space_runtime_run_self_test()) {
        MICROS_PANIC(boot->hart_id, "user-address-space-test");
    }
    uart_write(
        "MICROS_USER_ADDRESS_SPACE_TEST_PASS "
        "roots=isolated reuse=zeroed active=guarded "
        "ownership=validated sum=cleared\n"
    );
    uart_flush();
#endif

#ifdef MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST
    if (!micros_address_space_handoff_runtime_run_self_test()) {
        MICROS_PANIC(boot->hart_id, "address-space-handoff-test");
    }
    uart_write(
        "MICROS_ADDRESS_SPACE_HANDOFF_TEST_PASS "
        "phase=handed-off wired=validated ipc=resident "
        "grants=atomic mutation=revoked\n"
    );
    uart_write(
        "MICROS_GRANT_SYSCALL_HANDOFF_PASS "
        "phase=handed-off operations=create,revoke,copy-from,copy-to "
        "errors=stable registers=preserved\n"
    );
    uart_flush();
#endif
}

void boot_test_execution(const struct boot_context *boot)
{
    (void)boot;
#ifdef MICROS_BUILD_USER_EXECUTION_TEST
    micros_user_execution_runtime_run_self_test();
    MICROS_PANIC(boot->hart_id, "user-execution-test-returned");
#endif

#ifdef MICROS_BUILD_SCHEDULER_TEST
    micros_scheduler_runtime_run_self_test();
    MICROS_PANIC(boot->hart_id, "scheduler-test-returned");
#endif

#ifdef MICROS_BUILD_IPC_ECALL_CORE_TEST
    micros_ipc_ecall_core_runtime_run_self_test();
    MICROS_PANIC(boot->hart_id, "ipc-ecall-core-test-returned");
#endif

#ifdef MICROS_BUILD_IPC_SYSCALL_TEST
    micros_ipc_syscall_runtime_run_self_test();
    MICROS_PANIC(boot->hart_id, "ipc-syscall-test-returned");
#endif

#ifdef MICROS_BUILD_IPC_SYSCALL_PANIC_TEST
    micros_ipc_syscall_panic_runtime_run_self_test();
    MICROS_PANIC(boot->hart_id, "ipc-syscall-panic-test-returned");
#endif

#ifdef MICROS_BUILD_GRANT_SYSCALL_TEST
    micros_grant_syscall_runtime_run_self_test();
    MICROS_PANIC(boot->hart_id, "grant-syscall-test-returned");
#endif

#ifdef MICROS_BUILD_USER_RUNTIME_TEST
    micros_user_runtime_runtime_run_self_test();
    MICROS_PANIC(boot->hart_id, "user-runtime-test-returned");
#endif
}

void boot_test_services(const struct boot_context *boot)
{
    (void)boot;
#if defined(MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST) \
    || defined(MICROS_BUILD_BOOTSTRAP_READY_TIMEOUT_TEST) \
    || defined(MICROS_BUILD_BOOTSTRAP_MANIFEST_PANIC_TEST)
    micros_bootstrap_test_launch();
    MICROS_PANIC(boot->hart_id, "bootstrap-test-returned");
#endif

#if defined(MICROS_BUILD_VM_HANDOFF_TEST) \
    || defined(MICROS_BUILD_VM_READY_EARLY_TEST) \
    || defined(MICROS_BUILD_VM_SELF_FAULT_TEST) \
    || defined(MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST)
    micros_vm_handoff_test_launch();
    MICROS_PANIC(boot->hart_id, "vm-handoff-test-returned");
#endif

#ifdef MICROS_BUILD_PM_SERVICE_TEST
    micros_pm_service_test_launch();
    MICROS_PANIC(boot->hart_id, "pm-service-test-returned");
#endif

#ifdef MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST
    micros_vm_scratch_preemption_test_launch();
    MICROS_PANIC(
        boot->hart_id,
        "vm-scratch-preemption-test-returned"
    );
#endif

#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    micros_tty_service_test_launch();
    MICROS_PANIC(boot->hart_id, "tty-service-test-returned");
#endif

#ifdef MICROS_BUILD_RAMFS_SERVICE_TEST
    micros_ramfs_service_test_launch();
    MICROS_PANIC(boot->hart_id, "ramfs-service-test-returned");
#endif

#ifdef MICROS_BUILD_VFS_SERVICE_TEST
    micros_vfs_service_test_launch();
    MICROS_PANIC(boot->hart_id, "vfs-service-test-returned");
#endif
#ifdef MICROS_BUILD_ELF_SPAWN_TEST
    micros_elf_spawn_test_launch();
    MICROS_PANIC(boot->hart_id, "elf-spawn-test-returned");
#endif

#if defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
    micros_scheduler_invalid_runtime_run_self_test();
    MICROS_PANIC(boot->hart_id, "scheduler-invalid-test-returned");
#endif
}

void boot_test_objects_and_ipc(const struct boot_context *boot)
{
    (void)boot;
#ifdef MICROS_BUILD_OBJECT_MODEL_TEST
    if (!micros_kernel_object_runtime_run_self_test()) {
        MICROS_PANIC(boot->hart_id, "object-model-test");
    }
    uart_write(
        "MICROS_OBJECT_MODEL_TEST_PASS "
        "process-generation=advanced stale=rejected "
        "thread-limit=enforced hart-local=preserved\n"
    );
    uart_flush();
#endif

#ifdef MICROS_BUILD_ENDPOINT_TEST
    if (!micros_endpoint_runtime_run_self_test()) {
        MICROS_PANIC(boot->hart_id, "endpoint-test");
    }
    uart_write(
        "MICROS_ENDPOINT_TEST_PASS "
        "generation=validated profiles=immutable "
        "visibility=staged authorization=separate "
        "grants=generation-safe\n"
    );
    uart_flush();
#endif

#ifdef MICROS_BUILD_GRANT_TEST
    if (!micros_grant_runtime_run_self_test()) {
        MICROS_PANIC(boot->hart_id, "grant-test");
    }
    uart_write(
        "MICROS_GRANT_TEST_PASS "
        "identity=generation-safe directions=checked "
        "bounds=validated copies=atomic "
        "phase=bootstrap cleanup=complete\n"
    );
    uart_flush();
#endif

#ifdef MICROS_BUILD_IPC_TEST
    if (!micros_ipc_runtime_run_self_test()) {
        MICROS_PANIC(boot->hart_id, "ipc-test");
    }
    uart_write(
        "MICROS_IPC_ADDRESS_SPACES count=three roots=preserved "
        "contexts=preserved stacks=preserved scheduler=preserved "
        "messages=preserved\n"
    );
    uart_write(
        "MICROS_IPC_TEST_PASS "
        "endpoints=generation-safe queues=blocking "
        "calls=tokenized notifications=coalesced "
        "kernel-events=injected deadlock=rejected\n"
    );
    uart_flush();
#endif
}

void boot_test_nested_trap(const struct boot_context *boot)
{
    (void)boot;
#ifdef MICROS_BUILD_NESTED_TRAP_TEST
    if (
        !micros_kernel_object_runtime_set_test_emergency_stack(
            (uintptr_t)
                micros_nested_trap_test_emergency_stack_bottom,
            (uintptr_t)
                micros_nested_trap_test_emergency_stack_top
        )
    ) {
        MICROS_PANIC(boot->hart_id, "nested-trap-test-stack");
    }
    micros_nested_trap_test_trigger();
    MICROS_PANIC(boot->hart_id, "nested-trap-test-returned");
#endif
}
