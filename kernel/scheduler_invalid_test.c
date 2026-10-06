#include "micros/scheduler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

#define TEST_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define TEST_SCAUSE_CODE_MASK (TEST_SCAUSE_INTERRUPT - 1)

enum {
    TEST_EXCEPTION_USER_ECALL = 8,
};

static const uint64_t TEST_CODE_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE;
static const uint64_t TEST_COUNTER_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00002000);
static const uint64_t TEST_STACK_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00004000);
static const uint64_t TEST_TIMER_INTERVAL =
    UINT64_C(0x0000000000010000);
static const uint64_t TEST_QUANTUM =
    UINT64_C(0x0000000000018000);

extern const unsigned char micros_scheduler_payload_start[];
extern const unsigned char micros_scheduler_payload_ecall[];
extern const unsigned char micros_scheduler_payload_end[];

void micros_scheduler_test_enter(void);
void micros_scheduler_test_start_production(void);

static struct micros_process_handle processes[2];
static struct micros_thread_handle threads[2];
static uint64_t code_physical[2];
static uint64_t counter_physical[2];
static uint64_t stack_physical[2];
static uintptr_t kernel_stack_bottom[2];
static uintptr_t kernel_stack_top[2];
static struct micros_kernel_objects timer_objects_snapshot;
static struct micros_kernel_objects scheduler_snapshot_candidate;
static struct micros_trap_frame timer_frame_snapshot;
static uint64_t timer_satp_snapshot;

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static bool bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

static uint64_t read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

static uint64_t user_address_of(const unsigned char *symbol)
{
    return TEST_CODE_VIRTUAL_ADDRESS
        + (
            (uintptr_t)symbol
            - (uintptr_t)micros_scheduler_payload_start
        );
}

static void fill_context_pattern(
    struct micros_user_context *context,
    uint64_t base
)
{
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    for (
        index = 0;
        index < sizeof(words) / sizeof(words[0]);
        ++index
    ) {
        words[index] = base + index;
    }
    copy_bytes(context, words, sizeof(*context));
}

bool micros_scheduler_invalid_test_before_user_timer(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();

    if (
        objects == NULL
        || hart == NULL
        || frame == NULL
    ) {
        return false;
    }
    copy_bytes(
        &timer_objects_snapshot,
        objects,
        sizeof(timer_objects_snapshot)
    );
    copy_bytes(
        &timer_frame_snapshot,
        frame,
        sizeof(timer_frame_snapshot)
    );
    timer_satp_snapshot = read_satp();
    return true;
}

bool micros_scheduler_invalid_test_after_user_timer(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();

    if (
        objects == NULL
        || hart == NULL
        || frame == NULL
        || hart_handle.slot >= MICROS_HART_CAPACITY
    ) {
        return false;
    }
    copy_bytes(
        &scheduler_snapshot_candidate,
        objects,
        sizeof(scheduler_snapshot_candidate)
    );
    scheduler_snapshot_candidate.harts[hart_handle.slot].timer =
        timer_objects_snapshot.harts[hart_handle.slot].timer;
    return (
        bytes_equal(
            &scheduler_snapshot_candidate,
            &timer_objects_snapshot,
            sizeof(scheduler_snapshot_candidate)
        )
        && bytes_equal(
            frame,
            &timer_frame_snapshot,
            sizeof(*frame)
        )
        && read_satp() == timer_satp_snapshot
    );
}

bool micros_scheduler_invalid_test_handle_user_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer
)
{
    uint64_t cause_code;

    if (
        hart == NULL
        || frame == NULL
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
    ) {
        return false;
    }
    cause_code = frame->scause & TEST_SCAUSE_CODE_MASK;
    if (user_timer) {
        return (
            (frame->scause & TEST_SCAUSE_INTERRUPT) != 0
        );
    }
    if (
        (frame->scause & TEST_SCAUSE_INTERRUPT) != 0
        || cause_code != TEST_EXCEPTION_USER_ECALL
        || frame->sepc
            != user_address_of(micros_scheduler_payload_ecall)
    ) {
        return false;
    }
    frame->sepc += 4;
    return true;
}

bool micros_scheduler_invalid_test_report(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame,
    bool outgoing
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    const struct micros_process *process;
    bool expected_outgoing;

#ifdef MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST
    expected_outgoing = true;
#else
    expected_outgoing = false;
#endif
    if (objects != NULL && hart_handle.slot < MICROS_HART_CAPACITY) {
        copy_bytes(
            &scheduler_snapshot_candidate,
            objects,
            sizeof(scheduler_snapshot_candidate)
        );
        scheduler_snapshot_candidate.harts[hart_handle.slot].timer =
            timer_objects_snapshot.harts[hart_handle.slot].timer;
    }
    if (
        objects == NULL
        || hart == NULL
        || frame == NULL
        || outgoing != expected_outgoing
        || hart->current_thread.slot != threads[0].slot
        || hart->current_thread.generation
            != threads[0].generation
        || hart->ready_head[7].slot != threads[0].slot
        || hart->ready_head[7].generation
            != threads[0].generation
        || hart->ready_tail[7].slot != threads[1].slot
        || hart->ready_tail[7].generation
            != threads[1].generation
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || hart->trap.primary_stack_bottom
            != kernel_stack_bottom[0]
        || hart->trap.primary_stack_top != kernel_stack_top[0]
        || micros_process_resolve(
            objects,
            processes[0],
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || read_satp()
            != (
                MICROS_RISCV_SATP_MODE_SV39
                | (process->address_space_root >> 12)
            )
        || (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0
        || !bytes_equal(
            &scheduler_snapshot_candidate,
            &timer_objects_snapshot,
            sizeof(scheduler_snapshot_candidate)
        )
        || !bytes_equal(
            frame,
            &timer_frame_snapshot,
            sizeof(*frame)
        )
    ) {
        return false;
    }
    uart_write("MICROS_SCHEDULER_INVALID_CONTEXT state=");
    uart_write(outgoing ? "outgoing" : "next");
    uart_write(" ownership=preserved accounting=kernel\n");
    uart_flush();
    return true;
}

void micros_scheduler_test_start_production(void)
{
    enum micros_scheduler_error error = micros_scheduler_start();

    uart_write("MICROS_TEST_FAILURE scheduler-start-returned error=");
    uart_write_hex64((uint64_t)error);
    uart_write("\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}

_Noreturn void micros_scheduler_invalid_runtime_run_self_test(void)
{
    const uint32_t code_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_EXECUTE;
    const uint32_t stack_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE;
    struct micros_kernel_objects *objects;
    struct micros_user_context context;
    size_t payload_size;
    size_t index;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    objects = micros_kernel_object_runtime_test_registry();
    payload_size =
        (uintptr_t)micros_scheduler_payload_end
        - (uintptr_t)micros_scheduler_payload_start;
    if (
        objects == NULL
        || payload_size == 0
        || payload_size > MICROS_SV39_PAGE_SIZE
    ) {
        goto failure;
    }

    for (index = 0; index < 2; ++index) {
        if (
            micros_process_create(objects, &processes[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_user_address_space_create(processes[index])
                != MICROS_USER_ADDRESS_SPACE_OK
            || micros_user_address_space_allocate_page(
                processes[index],
                TEST_CODE_VIRTUAL_ADDRESS,
                code_permissions,
                &code_physical[index]
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || micros_user_address_space_allocate_page(
                processes[index],
                TEST_COUNTER_VIRTUAL_ADDRESS,
                stack_permissions,
                &counter_physical[index]
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || micros_user_address_space_allocate_page(
                processes[index],
                TEST_STACK_VIRTUAL_ADDRESS,
                stack_permissions,
                &stack_physical[index]
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || micros_thread_create(
                objects,
                processes[index],
                &threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || !micros_user_execution_test_stack_bounds(
                threads[index],
                &kernel_stack_bottom[index],
                &kernel_stack_top[index]
            )
        ) {
            goto failure;
        }
        copy_bytes(
            (void *)(uintptr_t)code_physical[index],
            micros_scheduler_payload_start,
            payload_size
        );
        fill_context_pattern(
            &context,
            UINT64_C(0x1000) + index * UINT64_C(0x1000)
        );
        context.sepc = TEST_CODE_VIRTUAL_ADDRESS;
        context.sp =
            TEST_STACK_VIRTUAL_ADDRESS + MICROS_SV39_PAGE_SIZE;
        context.s0 = TEST_COUNTER_VIRTUAL_ADDRESS;
        context.s1 = 0;
        context.t1 = 1;
        context.sstatus = 0;
        if (
            micros_user_execution_prepare(
                threads[index],
                &context
            ) != MICROS_USER_EXECUTION_OK
        ) {
            goto failure;
        }
    }
    __asm__ volatile("fence.i" : : : "memory");
    if (
        micros_scheduler_initialize(TEST_TIMER_INTERVAL)
            != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            threads[0],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            TEST_QUANTUM
        ) != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            threads[1],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            TEST_QUANTUM
        ) != MICROS_SCHEDULER_OK
    ) {
        goto failure;
    }
#ifdef MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST
    objects->threads[threads[1].slot].user_context.sp = 1;
#endif

    (void)saved_status;
    micros_scheduler_test_enter();

failure:
    riscv_irq_restore(saved_status);
    uart_write("MICROS_TEST_FAILURE scheduler-invalid-setup\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
