#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "micros/bootstrap_memory.h"
#include "micros/fdt.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "kernel/plic.h"
#include "micros/timer.h"
#include "micros/trap.h"

#ifdef MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST
#include "kernel/address_space_handoff_test.h"
#endif
#if defined(MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST) \
    || defined(MICROS_BUILD_BOOTSTRAP_READY_TIMEOUT_TEST) \
    || defined(MICROS_BUILD_BOOTSTRAP_MANIFEST_PANIC_TEST)
#include "kernel/bootstrap_test.h"
#endif
#if defined(MICROS_BUILD_VM_HANDOFF_TEST) \
    || defined(MICROS_BUILD_VM_READY_EARLY_TEST) \
    || defined(MICROS_BUILD_VM_SELF_FAULT_TEST) \
    || defined(MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST)
#include "kernel/vm_handoff_test.h"
#endif
#ifdef MICROS_BUILD_PM_SERVICE_TEST
#include "kernel/pm_service_test.h"
#endif
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
#include "kernel/tty_service_test.h"
#endif
#ifdef MICROS_BUILD_RAMFS_SERVICE_TEST
#include "kernel/ramfs_service_test.h"
#endif
#ifdef MICROS_BUILD_UART_CONSOLE_TEST
#include "kernel/uart_console_test.h"
#endif

#ifndef MICROS_VERSION
#error "MICROS_VERSION must be defined by the build"
#endif

void kernel_main(uintptr_t hart_id, uintptr_t fdt_address);

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

static void write_range(
    const char *event,
    const struct micros_fdt_range *range
)
{
    uart_write(event);
    uart_write(" base=");
    uart_write_hex64(range->base);
    uart_write(" size=");
    uart_write_hex64(range->size);
    uart_write("\n");
}

static void write_fdt_counts(
    const struct micros_fdt_memory_map *memory_map
)
{
    uart_write("MICROS_FDT_COUNTS memory=");
    uart_write_hex64((uint64_t)memory_map->memory_range_count);
    uart_write(" reservation=");
    uart_write_hex64((uint64_t)memory_map->reservation_range_count);
    uart_write(" reserved-memory=");
    uart_write_hex64(
        (uint64_t)memory_map->reserved_memory_range_count
    );
    uart_write("\n");
}

static void stop_after_reset_failure(void)
{
    uart_write("MICROS_TEST_FAILURE sbi-system-reset-returned\n");
    uart_flush();
    for (;;) {
        __asm__ volatile("wfi");
    }
}

void kernel_main(uintptr_t hart_id, uintptr_t fdt_address)
{
    const struct micros_frame_allocator *frame_allocator;
    const struct micros_frame_ownership *frame_ownership;
    const struct micros_kernel_address_space_report *address_space;
    const struct micros_kernel_objects *kernel_objects;
    struct micros_hart *boot_hart;
    struct micros_fdt_memory_map memory_map;
    enum micros_fdt_error error;
    size_t index;

    if (
        micros_kernel_object_runtime_initialize(hart_id)
        != MICROS_KERNEL_OBJECT_OK
    ) {
        MICROS_PANIC(hart_id, "objects-init");
    }
    if (!micros_trap_install()) {
        MICROS_PANIC(hart_id, "trap-install");
    }
    kernel_objects = micros_kernel_object_runtime_registry();
    boot_hart = micros_kernel_object_runtime_boot_hart();
    if (kernel_objects == NULL || boot_hart == NULL) {
        MICROS_PANIC(hart_id, "objects-state");
    }

    uart_write("MICROS_BOOT " MICROS_VERSION "\n");
    uart_write("MICROS_OBJECTS_READY processes=");
    uart_write_hex64(kernel_objects->live_process_count);
    uart_write(" threads=");
    uart_write_hex64(kernel_objects->live_thread_count);
    uart_write(" harts=");
    uart_write_hex64(kernel_objects->registered_hart_count);
    uart_write(" max-threads=");
    uart_write_hex64(kernel_objects->max_threads_per_process);
    uart_write(" max-harts=");
    uart_write_hex64(kernel_objects->max_harts);
    uart_write(" boot-hart=");
    uart_write_hex64(boot_hart->hardware_id);
    uart_write("\n");
    uart_write("MICROS_TRAP_READY\n");
    uart_flush();

    error = micros_fdt_parse_memory_map(
        (const void *)fdt_address,
        MICROS_FDT_MAX_BLOB_SIZE,
        &memory_map
    );
    if (error != MICROS_FDT_OK) {
        uart_write("MICROS_TEST_FAILURE fdt-");
        uart_write(micros_fdt_error_name(error));
        uart_write("\n");
        uart_flush();
        (void)sbi_system_reset(
            SBI_RESET_TYPE_SHUTDOWN,
            SBI_RESET_REASON_SYSTEM_FAILURE
        );
        stop_after_reset_failure();
    }

    for (index = 0; index < memory_map.memory_range_count; ++index) {
        write_range(
            "MICROS_FDT_MEMORY",
            &memory_map.memory_ranges[index]
        );
    }
    for (index = 0; index < memory_map.reservation_range_count; ++index) {
        write_range(
            "MICROS_FDT_RESERVATION",
            &memory_map.reservation_ranges[index]
        );
    }
    for (
        index = 0;
        index < memory_map.reserved_memory_range_count;
        ++index
    ) {
        write_range(
            "MICROS_FDT_RESERVED_MEMORY",
            &memory_map.reserved_memory_ranges[index]
        );
    }
    write_fdt_counts(&memory_map);
    uart_write("MICROS_FDT_READY\n");
    uart_flush();

    if (
        micros_bootstrap_memory_initialize(&memory_map)
        != MICROS_FRAME_ALLOCATOR_OK
    ) {
        MICROS_PANIC(hart_id, "frame-allocator-init");
    }
    frame_allocator = micros_bootstrap_frame_allocator();
    if (frame_allocator == NULL) {
        MICROS_PANIC(hart_id, "frame-allocator-state");
    }
    uart_write("MICROS_FRAME_ALLOCATOR_READY managed=");
    uart_write_hex64(frame_allocator->managed_frame_count);
    uart_write(" free=");
    uart_write_hex64(frame_allocator->free_frame_count);
    uart_write("\n");
    uart_flush();

    if (
        micros_frame_ownership_runtime_initialize()
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        MICROS_PANIC(hart_id, "frame-ownership-init");
    }

    if (
        micros_kernel_address_space_initialize()
        != MICROS_KERNEL_ADDRESS_SPACE_OK
    ) {
        MICROS_PANIC(hart_id, "mmu-init");
    }
    if (!micros_plic_initialize()) {
        MICROS_PANIC(hart_id, "plic-init");
    }
    address_space = micros_kernel_address_space_report();
    if (address_space == NULL) {
        MICROS_PANIC(hart_id, "mmu-state");
    }
    uart_write("MICROS_MMU_READY mode=sv39 root=");
    uart_write_hex64(address_space->root_physical_address);
    uart_write(" tables=");
    uart_write_hex64(address_space->table_count);
    uart_write("\n");
    uart_flush();

    frame_ownership = micros_frame_ownership_runtime_ledger();
    if (
        frame_ownership == NULL
        || micros_frame_ownership_runtime_validate(kernel_objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || frame_ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || frame_ownership->owned_frame_count == 0
        || frame_ownership->owned_frame_count
            != address_space->table_count
        || frame_ownership->owner_counts[
            MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
        ] != address_space->table_count
    ) {
        MICROS_PANIC(hart_id, "frame-ownership-state");
    }
    uart_write("MICROS_FRAME_OWNERSHIP_READY owned=");
    uart_write_hex64(frame_ownership->owned_frame_count);
    uart_write(" kernel-tables=");
    uart_write_hex64(
        frame_ownership->owner_counts[
            MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
        ]
    );
    uart_write(" phase=bootstrap\n");
    uart_flush();

#ifdef MICROS_BUILD_UART_CONSOLE_TEST
    micros_uart_console_runtime_run_self_test(hart_id);
#endif

#ifdef MICROS_BUILD_PANIC_TEST
    MICROS_PANIC(hart_id, "intentional-test");
#endif

#ifdef MICROS_BUILD_TRAP_TEST
    if (!micros_trap_run_self_test()) {
        MICROS_PANIC(hart_id, "trap-test-restore");
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
            boot_hart,
            UINT64_C(0x00000000000186a0),
            UINT64_C(3)
        )
    ) {
        MICROS_PANIC(hart_id, "timer-test-failed");
    }
    uart_write("MICROS_TIMER_TEST_PASS ticks=");
    uart_write_hex64(micros_timer_ticks(boot_hart));
    uart_write(" interval=");
    uart_write_hex64(UINT64_C(0x00000000000186a0));
    uart_write(" active=");
    uart_write_hex64(boot_hart->timer.active ? 1 : 0);
    uart_write(" deadline=");
    uart_write_hex64(boot_hart->timer.deadline);
    uart_write(" owner=hart");
    uart_write("\n");
    uart_flush();
#endif

#ifdef MICROS_BUILD_FRAME_ALLOCATOR_TEST
    if (!micros_bootstrap_memory_run_self_test(&memory_map)) {
        MICROS_PANIC(hart_id, "frame-allocator-test");
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
        MICROS_PANIC(hart_id, "mmu-test-failed");
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

#ifdef MICROS_BUILD_FRAME_OWNERSHIP_TEST
    if (!micros_frame_ownership_runtime_run_self_test()) {
        MICROS_PANIC(hart_id, "frame-ownership-test");
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
        MICROS_PANIC(hart_id, "user-address-space-test");
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
        MICROS_PANIC(hart_id, "address-space-handoff-test");
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

#ifdef MICROS_BUILD_USER_EXECUTION_TEST
    micros_user_execution_runtime_run_self_test();
    MICROS_PANIC(hart_id, "user-execution-test-returned");
#endif

#ifdef MICROS_BUILD_SCHEDULER_TEST
    micros_scheduler_runtime_run_self_test();
    MICROS_PANIC(hart_id, "scheduler-test-returned");
#endif

#ifdef MICROS_BUILD_IPC_ECALL_CORE_TEST
    micros_ipc_ecall_core_runtime_run_self_test();
    MICROS_PANIC(hart_id, "ipc-ecall-core-test-returned");
#endif

#ifdef MICROS_BUILD_IPC_SYSCALL_TEST
    micros_ipc_syscall_runtime_run_self_test();
    MICROS_PANIC(hart_id, "ipc-syscall-test-returned");
#endif

#ifdef MICROS_BUILD_IPC_SYSCALL_PANIC_TEST
    micros_ipc_syscall_panic_runtime_run_self_test();
    MICROS_PANIC(hart_id, "ipc-syscall-panic-test-returned");
#endif

#ifdef MICROS_BUILD_GRANT_SYSCALL_TEST
    micros_grant_syscall_runtime_run_self_test();
    MICROS_PANIC(hart_id, "grant-syscall-test-returned");
#endif

#ifdef MICROS_BUILD_USER_RUNTIME_TEST
    micros_user_runtime_runtime_run_self_test();
    MICROS_PANIC(hart_id, "user-runtime-test-returned");
#endif

#if defined(MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST) \
    || defined(MICROS_BUILD_BOOTSTRAP_READY_TIMEOUT_TEST) \
    || defined(MICROS_BUILD_BOOTSTRAP_MANIFEST_PANIC_TEST)
    micros_bootstrap_test_launch();
    MICROS_PANIC(hart_id, "bootstrap-test-returned");
#endif

#if defined(MICROS_BUILD_VM_HANDOFF_TEST) \
    || defined(MICROS_BUILD_VM_READY_EARLY_TEST) \
    || defined(MICROS_BUILD_VM_SELF_FAULT_TEST) \
    || defined(MICROS_BUILD_VM_SELF_FAULT_SEALED_TEST)
    micros_vm_handoff_test_launch();
    MICROS_PANIC(hart_id, "vm-handoff-test-returned");
#endif

#ifdef MICROS_BUILD_PM_SERVICE_TEST
    micros_pm_service_test_launch();
    MICROS_PANIC(hart_id, "pm-service-test-returned");
#endif

#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    micros_tty_service_test_launch();
    MICROS_PANIC(hart_id, "tty-service-test-returned");
#endif

#ifdef MICROS_BUILD_RAMFS_SERVICE_TEST
    micros_ramfs_service_test_launch();
    MICROS_PANIC(hart_id, "ramfs-service-test-returned");
#endif

#if defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST) \
    || defined(MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST)
    micros_scheduler_invalid_runtime_run_self_test();
    MICROS_PANIC(hart_id, "scheduler-invalid-test-returned");
#endif

#ifdef MICROS_BUILD_OBJECT_MODEL_TEST
    if (!micros_kernel_object_runtime_run_self_test()) {
        MICROS_PANIC(hart_id, "object-model-test");
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
        MICROS_PANIC(hart_id, "endpoint-test");
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
        MICROS_PANIC(hart_id, "grant-test");
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
        MICROS_PANIC(hart_id, "ipc-test");
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

#ifdef MICROS_BUILD_NESTED_TRAP_TEST
    if (
        !micros_kernel_object_runtime_set_test_emergency_stack(
            (uintptr_t)
                micros_nested_trap_test_emergency_stack_bottom,
            (uintptr_t)
                micros_nested_trap_test_emergency_stack_top
        )
    ) {
        MICROS_PANIC(hart_id, "nested-trap-test-stack");
    }
    micros_nested_trap_test_trigger();
    MICROS_PANIC(hart_id, "nested-trap-test-returned");
#endif

    (void)sbi_system_reset(SBI_RESET_TYPE_SHUTDOWN, SBI_RESET_REASON_NONE);
    stop_after_reset_failure();
}
