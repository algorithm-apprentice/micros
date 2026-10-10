#include "micros/scheduler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/plic.h"
#include "micros/endpoint.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_abi.h"
#include "micros/ipc_core.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/sv39.h"
#include "micros/timer.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

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

static const uint64_t TEST_CODE_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE;
static const uint64_t TEST_COUNTER_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00002000);
static const uint64_t TEST_IPC_BUFFER_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00002100);
static const uint64_t TEST_STACK_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00004000);
static const uint64_t TEST_TIMER_INTERVAL =
    UINT64_C(0x0000000000010000);
static const uint64_t TEST_QUANTUM =
    UINT64_C(0x0000000000030000);
static const uint64_t TEST_START_TIMER_DELAY =
    UINT64_C(0x0000000000001000);
static const uint32_t TEST_EXTERNAL_ATTEMPTS = UINT32_C(100000);
static const uintptr_t TEST_UART_INTERRUPT_ENABLE =
    MICROS_RISCV_UART0_BASE + UINT64_C(1);
static const uint8_t TEST_UART_INTERRUPT_TX_EMPTY = UINT8_C(0x02);

extern const unsigned char micros_scheduler_payload_start[];
extern const unsigned char micros_scheduler_payload_ecall[];
extern const unsigned char micros_scheduler_payload_loop[];
extern const unsigned char micros_scheduler_payload_end[];
extern const unsigned char micros_scheduler_test_supervisor_resume[];
extern const uint64_t micros_scheduler_test_saved_state[17];
extern const uint64_t micros_scheduler_test_restored_state[17];
extern const uint64_t micros_scheduler_test_immediate_status;

void micros_scheduler_test_enter(void);
void micros_scheduler_test_start_production(void);

static struct micros_process_handle processes[2];
static struct micros_thread_handle threads[2];
static micros_endpoint_t endpoints[2];
static struct micros_endpoint_registry *registry;
static struct micros_user_context prepared_contexts[2];
static uint64_t code_physical[2];
static uint64_t counter_physical[2];
static uint64_t stack_physical[2];
static uintptr_t kernel_stack_bottom[2];
static uintptr_t kernel_stack_top[2];
static uint64_t baseline_owned;
static uint64_t baseline_free;
static uint64_t previous_counter[2];
static uint64_t ecall_count[2];
static uint64_t timer_trap_count;
static uint64_t switch_count;
static size_t last_running = 2;
static bool supervisor_returned;
static bool start_boundary_observed;
static bool start_completion_observed;
static bool idle_requested;
static bool idle_woke_thread;
static bool spurious_idle_bypassed;
static uint64_t selector_entry_count;
static uint64_t selector_entries_before_spurious;
static uint64_t kernel_ticks_before_start;
static struct micros_kernel_objects start_snapshot;
static uint64_t start_satp_snapshot;
static bool start_sie_snapshot;
static bool start_stie_snapshot;
static uint64_t start_timer_attempts;
static enum test_completion_kind expected_completion[2];
static uint64_t expected_completion_result[2];
static struct micros_ipc_message expected_message;
static bool message_completion_deferred;
static bool message_completion_observed;
static bool captured_completion_observed;
static bool error_completion_observed;
static bool error_completion_staged;
static bool idle_completion_observed;
static volatile enum test_external_origin external_origin;
static bool user_external_started;
static bool user_external_observed;
static bool user_external_return_observed;
static bool supervisor_external_observed;
static bool idle_external_started;
static bool idle_external_observed;
static bool idle_external_completion_observed;
volatile uint64_t micros_scheduler_test_idle_bypass_once;
volatile uint64_t micros_scheduler_test_idle_bypass_observed;

static volatile uint8_t *test_uart_interrupt_enable(void)
{
    return (volatile uint8_t *)TEST_UART_INTERRUPT_ENABLE;
}

static bool arm_external_interrupt(
    enum test_external_origin origin
)
{
    struct micros_plic_tty_enable_plan plan;

    if (
        origin == TEST_EXTERNAL_NONE
        || external_origin != TEST_EXTERNAL_NONE
        || !micros_plic_prepare_tty_enable(&plan)
    ) {
        return false;
    }
    external_origin = origin;
    micros_plic_commit_tty_prepare_prevalidated(&plan);
    micros_plic_commit_tty_enable_prevalidated(&plan);
    riscv_external_interrupt_enable();
    *test_uart_interrupt_enable() =
        TEST_UART_INTERRUPT_TX_EMPTY;
    riscv_mmio_fence();
    return (
        micros_plic_validate(MICROS_PLIC_ENABLED)
        && riscv_external_interrupt_is_enabled()
    );
}

static bool run_supervisor_external_test(void)
{
    uintptr_t saved_status;
    uint32_t attempt;

    if (
        !user_external_return_observed
        || !arm_external_interrupt(TEST_EXTERNAL_SUPERVISOR)
    ) {
        return false;
    }
    saved_status = riscv_irq_save();
    riscv_irq_restore(MICROS_RISCV_SSTATUS_SIE);
    for (
        attempt = 0;
        attempt < TEST_EXTERNAL_ATTEMPTS
            && !supervisor_external_observed;
        ++attempt
    ) {
        __asm__ volatile("" : : : "memory");
    }
    (void)riscv_irq_save();
    riscv_irq_restore(saved_status);
    return (
        supervisor_external_observed
        && external_origin == TEST_EXTERNAL_NONE
        && !riscv_external_interrupt_is_enabled()
        && micros_plic_validate(MICROS_PLIC_DISABLED)
    );
}

static bool thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

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

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool completion_state_is_clear(
    const struct micros_thread *thread
)
{
    static const struct micros_ipc_message zero_message;

    return (
        thread != NULL
        && thread->ipc_receive_buffer == 0
        && !thread->ipc_delivery_pending
        && bytes_equal(
            &thread->ipc_inbound_message,
            &zero_message,
            sizeof(zero_message)
        )
        && thread->ipc_staged_result == MICROS_IPC_OK
    );
}

static bool observe_completion(
    const struct micros_kernel_objects *objects,
    size_t thread_index
)
{
    enum test_completion_kind kind;
    const struct micros_thread *thread;

    if (objects == NULL || thread_index >= 2) {
        return false;
    }
    kind = expected_completion[thread_index];
    if (kind == TEST_COMPLETION_NONE) {
        return true;
    }
    thread = &objects->threads[threads[thread_index].slot];
    if (
        !completion_state_is_clear(thread)
        || thread->user_context.a0
            != expected_completion_result[thread_index]
    ) {
        return false;
    }
    if (
        kind == TEST_COMPLETION_MESSAGE
        && !bytes_equal(
            (const void *)(uintptr_t)(
                counter_physical[thread_index] + UINT64_C(0x100)
            ),
            &expected_message,
            sizeof(expected_message)
        )
    ) {
        return false;
    }
    switch (kind) {
    case TEST_COMPLETION_START:
        start_completion_observed = true;
        break;
    case TEST_COMPLETION_MESSAGE:
        message_completion_observed = true;
        break;
    case TEST_COMPLETION_CAPTURED:
        captured_completion_observed = true;
        break;
    case TEST_COMPLETION_ERROR:
        error_completion_observed = true;
        break;
    case TEST_COMPLETION_IDLE:
        idle_completion_observed = true;
        break;
    case TEST_COMPLETION_EXTERNAL_IDLE:
        idle_external_completion_observed = true;
        break;
    case TEST_COMPLETION_NONE:
        return false;
    }
    expected_completion[thread_index] = TEST_COMPLETION_NONE;
    return true;
}

static bool expect_no_message_completion(
    struct micros_kernel_objects *objects,
    size_t thread_index,
    enum micros_ipc_error result,
    enum test_completion_kind kind
)
{
    int64_t abi_result;

    if (objects == NULL || thread_index >= 2) {
        return false;
    }
    switch (result) {
    case MICROS_IPC_OK:
        abi_result = MICROS_IPC_ABI_OK;
        break;
    case MICROS_IPC_ERROR_DEAD_ENDPOINT:
        abi_result = MICROS_IPC_ABI_DEAD_ENDPOINT;
        break;
    default:
        return false;
    }
    if (
        micros_ipc_stage_no_message_completion(
            registry,
            objects,
            threads[thread_index],
            result
        ) != MICROS_IPC_OK
    ) {
        return false;
    }
    expected_completion[thread_index] = kind;
    expected_completion_result[thread_index] =
        (uint64_t)abi_result;
    prepared_contexts[thread_index].a0 =
        (uint64_t)abi_result;
    return true;
}

static bool expect_message_completion(
    struct micros_kernel_objects *objects,
    size_t thread_index
)
{
    struct micros_thread *thread;

    if (
        objects == NULL
        || thread_index >= 2
    ) {
        return false;
    }
    thread = &objects->threads[threads[thread_index].slot];
    clear_bytes(&expected_message, sizeof(expected_message));
    expected_message.source = MICROS_ENDPOINT_NONE;
    expected_message.type =
        MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    expected_message.payload[0] = UINT8_C(0x05);
    if (
        micros_thread_scheduler_hold(
            objects,
            threads[thread_index]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_ipc_receive(
            registry,
            objects,
            threads[thread_index],
            MICROS_ENDPOINT_ANY,
            TEST_IPC_BUFFER_VIRTUAL_ADDRESS
        ) != MICROS_IPC_OK
        || micros_ipc_inject_kernel_notification(
            registry,
            objects,
            endpoints[thread_index],
            UINT64_C(0x05)
        ) != MICROS_IPC_OK
        || !thread->ipc_delivery_pending
        || !bytes_equal(
            &thread->ipc_inbound_message,
            &expected_message,
            sizeof(expected_message)
        )
    ) {
        return false;
    }
    expected_completion[thread_index] =
        TEST_COMPLETION_MESSAGE;
    expected_completion_result[thread_index] =
        MICROS_IPC_ABI_OK;
    prepared_contexts[thread_index].a0 =
        MICROS_IPC_ABI_OK;
    return micros_ipc_runtime_validate() == MICROS_ENDPOINT_OK;
}

static enum micros_scheduler_test_trap_action
completion_test_mismatch(uint64_t stage)
{
    uart_write("MICROS_TEST_FAILURE scheduler-completion-stage=");
    uart_write_hex64(stage);
    uart_write("\n");
    uart_flush();
    return MICROS_SCHEDULER_TEST_MISMATCH;
}

static uint64_t read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
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

static uint64_t user_address_of(const unsigned char *symbol)
{
    return TEST_CODE_VIRTUAL_ADDRESS
        + (
            (uintptr_t)symbol
            - (uintptr_t)micros_scheduler_payload_start
        );
}

static size_t current_thread_index(const struct micros_hart *hart)
{
    size_t index;

    if (hart == NULL) {
        return 2;
    }
    for (index = 0; index < 2; ++index) {
        if (
            hart->current_thread.slot == threads[index].slot
            && hart->current_thread.generation
                == threads[index].generation
        ) {
            return index;
        }
    }
    return 2;
}

static bool user_registers_match(
    size_t thread_index,
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct micros_user_context observed;
    struct micros_user_context expected;
    const struct micros_process *process;
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();

    if (
        thread_index >= 2
        || hart == NULL
        || frame == NULL
        || objects == NULL
        || micros_user_execution_inspect(
            threads[thread_index],
            &observed
        ) != MICROS_USER_EXECUTION_OK
    ) {
        return false;
    }
    copy_bytes(
        &expected,
        &prepared_contexts[thread_index],
        sizeof(expected)
    );
    expected.sepc = frame->sepc;
    expected.s1 = (
        frame->sepc < user_address_of(micros_scheduler_payload_loop)
    ) ? 0 : 1;
    if (
        !bytes_equal(&observed, &expected, sizeof(expected))
        || !bytes_equal(&observed, frame, sizeof(observed))
        || hart->trap.primary_stack_bottom
            != kernel_stack_bottom[thread_index]
        || hart->trap.primary_stack_top
            != kernel_stack_top[thread_index]
        || !thread_handles_equal(
            hart->ready_head[MICROS_SCHEDULER_PRIORITY_DEFAULT_USER],
            threads[thread_index]
        )
        || micros_process_resolve(
            objects,
            processes[thread_index],
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || read_satp()
            != (
                MICROS_RISCV_SATP_MODE_SV39
                | (process->address_space_root >> 12)
            )
    ) {
        return false;
    }
    return true;
}

static bool snapshot_start_state(
    const struct micros_kernel_objects *objects
)
{
    if (objects == NULL) {
        return false;
    }
    copy_bytes(&start_snapshot, objects, sizeof(start_snapshot));
    start_satp_snapshot = read_satp();
    start_sie_snapshot = riscv_irq_is_enabled();
    start_stie_snapshot = riscv_timer_interrupt_is_enabled();
    start_timer_attempts = micros_timer_test_program_attempts();
    return true;
}

static bool start_state_matches(
    const struct micros_kernel_objects *objects
)
{
    return (
        objects != NULL
        && bytes_equal(
            objects,
            &start_snapshot,
            sizeof(start_snapshot)
        )
        && read_satp() == start_satp_snapshot
        && riscv_irq_is_enabled() == start_sie_snapshot
        && riscv_timer_interrupt_is_enabled() == start_stie_snapshot
        && micros_timer_test_program_attempts()
            == start_timer_attempts + 1
    );
}

bool micros_scheduler_test_after_start(
    const struct micros_hart *hart
)
{
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();

    if (
        objects == NULL
        || hart == NULL
        || current_thread_index(hart) != 0
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_THREAD
        || hart->accounting_started_at
            < micros_timer_test_last_program_counter()
        || hart->kernel_counter_ticks
            < kernel_ticks_before_start + TEST_START_TIMER_DELAY
        || objects->threads[threads[0].slot]
            .remaining_counter_ticks != TEST_QUANTUM
        || !observe_completion(objects, 0)
    ) {
        return false;
    }
    start_boundary_observed = true;
    return true;
}

bool micros_scheduler_test_after_user_return(
    const struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t thread_index = current_thread_index(hart);
    size_t index;

    if (
        thread_index >= 2
        || objects == NULL
        || hart == NULL
        || frame == NULL
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_THREAD
        || !user_registers_match(thread_index, hart, frame)
        || !observe_completion(objects, thread_index)
    ) {
        return false;
    }
    for (index = 0; index < 2; ++index) {
        if (
            index != thread_index
            && expected_completion[index]
                == TEST_COMPLETION_MESSAGE
            && objects->threads[threads[index].slot]
                .ipc_delivery_pending
        ) {
            message_completion_deferred = true;
        }
    }
    if (last_running == 2) {
        last_running = thread_index;
    } else if (last_running != thread_index) {
        last_running = thread_index;
        ++switch_count;
    }
    if (
        user_external_observed
        && !user_external_return_observed
    ) {
        user_external_return_observed = true;
    }
    return true;
}

bool micros_scheduler_test_handle_external_interrupt(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    enum test_external_origin origin = external_origin;
    bool supervisor_origin;
    uint32_t source = 0;

    if (origin == TEST_EXTERNAL_NONE) {
        return false;
    }
    supervisor_origin = (
        frame != NULL
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0
    );
    if (
        hart == NULL
        || frame == NULL
        || (
            origin == TEST_EXTERNAL_USER
            && supervisor_origin
        )
        || (
            (
                origin == TEST_EXTERNAL_SUPERVISOR
                || origin == TEST_EXTERNAL_IDLE
            )
            && !supervisor_origin
        )
        || (
            origin == TEST_EXTERNAL_IDLE
            && (
                !idle_external_started
                || idle_external_observed
                || hart->accounting_owner
                    != MICROS_SCHEDULER_ACCOUNTING_KERNEL
                || hart->current_thread.generation != 0
                || selector_entry_count
                    != selector_entries_before_spurious + 1
            )
        )
        || !micros_plic_claim(&source)
        || source != MICROS_TTY_UART_IRQ_SOURCE
    ) {
        return false;
    }
    *test_uart_interrupt_enable() = 0;
    riscv_mmio_fence();
    riscv_external_interrupt_disable();
    if (
        !micros_plic_complete(MICROS_TTY_UART_IRQ_SOURCE)
        || !micros_plic_disable_tty()
    ) {
        return false;
    }
    external_origin = TEST_EXTERNAL_NONE;
    if (origin == TEST_EXTERNAL_USER) {
        user_external_observed = true;
    } else if (origin == TEST_EXTERNAL_SUPERVISOR) {
        supervisor_external_observed = true;
    } else {
        struct micros_kernel_objects *objects =
            micros_kernel_object_runtime_test_registry();

        if (objects == NULL) {
            return false;
        }
        objects->threads[threads[0].slot].user_context.sepc =
            user_address_of(micros_scheduler_payload_ecall);
        objects->threads[threads[0].slot].user_context.s1 = 0;
        if (
            !expect_no_message_completion(
                objects,
                0,
                MICROS_IPC_OK,
                TEST_COMPLETION_EXTERNAL_IDLE
            )
            || micros_thread_runtime_flags_unset(
                objects,
                threads[0],
                MICROS_THREAD_RTS_INACTIVE
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        idle_external_observed = true;
    }
    return true;
}

enum micros_scheduler_test_trap_action
micros_scheduler_test_handle_user_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    bool user_timer
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t thread_index = current_thread_index(hart);
    uint64_t cause_code;
    uint64_t counter;

    if (
        objects == NULL
        || hart == NULL
        || frame == NULL
        || thread_index >= 2
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || !user_registers_match(thread_index, hart, frame)
        || !observe_completion(objects, thread_index)
    ) {
        return MICROS_SCHEDULER_TEST_MISMATCH;
    }
    cause_code = frame->scause & TEST_SCAUSE_CODE_MASK;
    counter = *(const volatile uint64_t *)(uintptr_t)
        counter_physical[thread_index];
    if (counter < previous_counter[thread_index]) {
        return MICROS_SCHEDULER_TEST_MISMATCH;
    }
    previous_counter[thread_index] = counter;

    if (!user_timer) {
        uint64_t control_mask =
            MICROS_RISCV_SSTATUS_SIE
            | MICROS_RISCV_SSTATUS_SPIE
            | MICROS_RISCV_SSTATUS_SPP
            | MICROS_RISCV_SSTATUS_SUM
            | TEST_SSTATUS_UBE
            | TEST_SSTATUS_VS
            | TEST_SSTATUS_FS
            | TEST_SSTATUS_XS
            | TEST_SSTATUS_MXR
            | TEST_SSTATUS_SD;

        if (
            (frame->scause & TEST_SCAUSE_INTERRUPT) != 0
            || cause_code != TEST_EXCEPTION_USER_ECALL
            || frame->sepc
                != user_address_of(micros_scheduler_payload_ecall)
        ) {
            return MICROS_SCHEDULER_TEST_MISMATCH;
        }
        ++ecall_count[thread_index];
        if (idle_woke_thread && !idle_external_started) {
            if (
                thread_index != 0
                || !spurious_idle_bypassed
                || !idle_completion_observed
                || selector_entry_count
                    != selector_entries_before_spurious + 1
                || !arm_external_interrupt(TEST_EXTERNAL_IDLE)
                || micros_thread_scheduler_hold(
                    objects,
                    threads[0]
                ) != MICROS_KERNEL_OBJECT_OK
            ) {
                return MICROS_SCHEDULER_TEST_MISMATCH;
            }
            idle_external_started = true;
            return MICROS_SCHEDULER_TEST_CONTINUE;
        }
        if (idle_external_observed) {
            if (
                thread_index != 0
                || !idle_external_completion_observed
                || selector_entry_count
                    != selector_entries_before_spurious + 2
                || micros_scheduler_test_prepare_supervisor_return(
                    hart,
                    frame
                ) != MICROS_SCHEDULER_OK
            ) {
                return MICROS_SCHEDULER_TEST_MISMATCH;
            }
            frame->sp = micros_scheduler_test_saved_state[1];
            frame->sepc =
                (uintptr_t)micros_scheduler_test_supervisor_resume;
            frame->sstatus &= ~control_mask;
            frame->sstatus |= MICROS_RISCV_SSTATUS_SPP;
            supervisor_returned = true;
            return MICROS_SCHEDULER_TEST_RETURN_SUPERVISOR;
        }
        if (!message_completion_observed) {
            size_t target = thread_index == 0 ? 1 : 0;

            if (
                expected_completion[target]
                    != TEST_COMPLETION_NONE
                || !expect_message_completion(
                    objects,
                    target
                )
            ) {
                return completion_test_mismatch(1);
            }
            frame->sepc += 4;
            return MICROS_SCHEDULER_TEST_CONTINUE;
        }
        if (!captured_completion_observed) {
            struct micros_scheduler_current_ipc_guard guard = {0};
            struct micros_user_context context;

            if (
                micros_user_execution_inspect(
                    threads[thread_index],
                    &context
                ) != MICROS_USER_EXECUTION_OK
            ) {
                return completion_test_mismatch(2);
            }
            context.sepc += 4;
            if (
                micros_user_execution_store_context(
                    threads[thread_index],
                    &context
                ) != MICROS_USER_EXECUTION_OK
            ) {
                return completion_test_mismatch(3);
            }
            if (
                micros_scheduler_begin_current_ipc(
                    objects,
                    micros_kernel_object_runtime_boot_hart_handle(),
                    &guard
                ) != MICROS_KERNEL_OBJECT_OK
            ) {
                return completion_test_mismatch(4);
            }
            if (
                micros_thread_runtime_flags_unset(
                    objects,
                    threads[thread_index],
                    MICROS_THREAD_RTS_INACTIVE
                ) != MICROS_KERNEL_OBJECT_OK
            ) {
                return completion_test_mismatch(5);
            }
            if (
                !expect_no_message_completion(
                    objects,
                    thread_index,
                    MICROS_IPC_OK,
                    TEST_COMPLETION_CAPTURED
                )
            ) {
                return completion_test_mismatch(6);
            }
            if (
                micros_scheduler_commit_current_ipc(
                    objects,
                    &guard
                ) != MICROS_KERNEL_OBJECT_OK
            ) {
                return completion_test_mismatch(7);
            }
            return MICROS_SCHEDULER_TEST_CAPTURED_USER_RETURN;
        }
        frame->sepc += 4;
        return MICROS_SCHEDULER_TEST_CONTINUE;
    }
    if (
        (frame->scause & TEST_SCAUSE_INTERRUPT) == 0
        || cause_code != TEST_INTERRUPT_SUPERVISOR_TIMER
    ) {
        return MICROS_SCHEDULER_TEST_MISMATCH;
    }
    ++timer_trap_count;
    if (
        captured_completion_observed
        && !error_completion_staged
    ) {
        if (
            !expect_no_message_completion(
                objects,
                thread_index,
                MICROS_IPC_ERROR_DEAD_ENDPOINT,
                TEST_COMPLETION_ERROR
            )
        ) {
            return MICROS_SCHEDULER_TEST_MISMATCH;
        }
        error_completion_staged = true;
    }
    if (
        switch_count >= 6
        && error_completion_observed
        && !user_external_started
    ) {
        if (!arm_external_interrupt(TEST_EXTERNAL_USER)) {
            return MICROS_SCHEDULER_TEST_MISMATCH;
        }
        user_external_started = true;
        return MICROS_SCHEDULER_TEST_CONTINUE;
    }
    if (
        switch_count >= 6
        && error_completion_observed
        && user_external_return_observed
        && !idle_requested
    ) {
        size_t other = thread_index == 0 ? 1 : 0;

        if (
            micros_thread_scheduler_hold(
                objects,
                threads[thread_index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_hold(
                objects,
                threads[other]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return MICROS_SCHEDULER_TEST_MISMATCH;
        }
        idle_requested = true;
        selector_entries_before_spurious = selector_entry_count;
        micros_scheduler_test_idle_bypass_once = 1;
    }
    return MICROS_SCHEDULER_TEST_CONTINUE;
}

void micros_scheduler_test_note_selector_entry(void)
{
    ++selector_entry_count;
}

bool micros_scheduler_test_handle_idle_timer(struct micros_hart *hart)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();

    if (
        objects == NULL
        || hart == NULL
        || !idle_requested
        || idle_woke_thread
        || micros_scheduler_test_idle_bypass_observed != 1
        || selector_entry_count != selector_entries_before_spurious
        || !hart->reschedule_pending
        || hart->accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
    ) {
        return false;
    }
    spurious_idle_bypassed = true;
    objects->threads[threads[0].slot].user_context.sepc =
        user_address_of(micros_scheduler_payload_ecall);
    objects->threads[threads[0].slot].user_context.s1 = 0;
    if (
        !expect_no_message_completion(
            objects,
            0,
            MICROS_IPC_OK,
            TEST_COMPLETION_IDLE
        )
        || micros_thread_runtime_flags_unset(
            objects,
            threads[0],
            MICROS_THREAD_RTS_INACTIVE
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    idle_woke_thread = true;
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

static _Noreturn void finish_test(void)
{
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    uint64_t released;
    size_t index;

    if (!run_supervisor_external_test()) {
        goto failure;
    }
    if (
        !supervisor_returned
        || !start_boundary_observed
        || !start_completion_observed
        || !message_completion_deferred
        || !message_completion_observed
        || !captured_completion_observed
        || !error_completion_observed
        || !idle_completion_observed
        || timer_trap_count == 0
        || switch_count < 6
        || !idle_requested
        || !spurious_idle_bypassed
        || !idle_woke_thread
        || !user_external_started
        || !user_external_observed
        || !user_external_return_observed
        || !supervisor_external_observed
        || !idle_external_started
        || !idle_external_observed
        || !idle_external_completion_observed
        || selector_entry_count
            != selector_entries_before_spurious + 2
        || ledger == NULL
        || objects == NULL
        || previous_counter[0] == 0
        || previous_counter[1] == 0
        || ecall_count[0] == 0
        || ecall_count[1] == 0
        || objects->harts[0].current_thread.generation != 0
        || objects->harts[0].accounting_owner
            != MICROS_SCHEDULER_ACCOUNTING_KERNEL
        || objects->harts[0].timer.active
        || (
            micros_scheduler_test_immediate_status
            & MICROS_RISCV_SSTATUS_SIE
        ) != 0
        || !bytes_equal(
            micros_scheduler_test_saved_state,
            micros_scheduler_test_restored_state,
            sizeof(uint64_t) * 17
        )
    ) {
        goto failure;
    }

    for (index = 0; index < 2; ++index) {
        if (
            micros_thread_scheduler_remove(
                objects,
                threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_user_execution_detach(threads[index])
                != MICROS_USER_EXECUTION_OK
            || micros_thread_release(objects, threads[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_ipc_endpoint_close(
                registry,
                objects,
                endpoints[index]
            ) != MICROS_IPC_OK
            || micros_user_address_space_release_page(
                processes[index],
                TEST_CODE_VIRTUAL_ADDRESS,
                &released
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || released != code_physical[index]
            || micros_user_address_space_release_page(
                processes[index],
                TEST_COUNTER_VIRTUAL_ADDRESS,
                &released
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || released != counter_physical[index]
            || micros_user_address_space_release_page(
                processes[index],
                TEST_STACK_VIRTUAL_ADDRESS,
                &released
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || released != stack_physical[index]
            || micros_user_address_space_destroy(processes[index])
                != MICROS_USER_ADDRESS_SPACE_OK
            || micros_frame_ownership_runtime_release_process(
                objects,
                processes[index]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            goto failure;
        }
    }
    if (
        ledger->owned_frame_count != baseline_owned
        || ledger->allocator->free_frame_count != baseline_free
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto failure;
    }
    uart_write(
        "MICROS_IPC_RETURN completions=shared "
        "paths=start,user,captured,idle buffers=bounded\n"
    );
    uart_write(
        "MICROS_SCHEDULER_TEST_PASS "
        "queues=minix-priority current=reachable "
        "accounting=separate switches=alternating "
        "idle=resumed registers=preserved\n"
    );
    uart_write(
        "MICROS_TTY_TRAP_TEST_PASS "
        "user=cause9-scheduled supervisor=cause9-direct "
        "idle=cause9-selected\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );

failure:
    uart_write("MICROS_TEST_FAILURE scheduler-test\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}

_Noreturn void micros_scheduler_runtime_run_self_test(void)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "SCHEDULER_TEST",
            .operations = MICROS_PRIVILEGE_OPERATION_DEFINED_MASK,
            .call_targets = UINT32_C(1) << 1,
            .send_targets = UINT32_C(1) << 1,
            .notify_targets = UINT32_C(1) << 1,
        },
    };
    const uint32_t code_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_EXECUTE;
    const uint32_t data_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE;
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    size_t payload_size;
    size_t index;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    ledger = micros_frame_ownership_runtime_ledger();
    objects = micros_kernel_object_runtime_test_registry();
    if (ledger == NULL || objects == NULL) {
        goto failure;
    }
    if (
        micros_ipc_runtime_initialize(
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
    ) {
        goto failure;
    }
    registry = micros_ipc_runtime_authoritative_registry();
    if (registry == NULL) {
        goto failure;
    }
    baseline_owned = ledger->owned_frame_count;
    baseline_free = ledger->allocator->free_frame_count;
    payload_size =
        (uintptr_t)micros_scheduler_payload_end
        - (uintptr_t)micros_scheduler_payload_start;
    if (payload_size == 0 || payload_size > MICROS_SV39_PAGE_SIZE) {
        goto failure;
    }

    for (index = 0; index < 2; ++index) {
        if (
            micros_process_create(objects, &processes[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                registry,
                objects,
                processes[index],
                &endpoints[index]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                registry,
                objects,
                processes[index],
                1
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                registry,
                objects,
                endpoints[index]
            ) != MICROS_ENDPOINT_OK
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
                data_permissions,
                &counter_physical[index]
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || micros_user_address_space_allocate_page(
                processes[index],
                TEST_STACK_VIRTUAL_ADDRESS,
                data_permissions,
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
            &prepared_contexts[index],
            UINT64_C(0x1000) + index * UINT64_C(0x1000)
        );
        prepared_contexts[index].sepc = TEST_CODE_VIRTUAL_ADDRESS;
        prepared_contexts[index].sp =
            TEST_STACK_VIRTUAL_ADDRESS + MICROS_SV39_PAGE_SIZE;
        prepared_contexts[index].s0 = TEST_COUNTER_VIRTUAL_ADDRESS;
        prepared_contexts[index].s1 = 0;
        prepared_contexts[index].t1 = 1;
        prepared_contexts[index].sstatus = 0;
        if (
            micros_user_execution_prepare(
                threads[index],
                &prepared_contexts[index]
            ) != MICROS_USER_EXECUTION_OK
            || micros_user_execution_inspect(
                threads[index],
                &prepared_contexts[index]
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
    objects->threads[threads[1].slot].ipc_receive_buffer =
        TEST_IPC_BUFFER_VIRTUAL_ADDRESS;
    if (
        !micros_scheduler_test_rejects_malformed_completion(
            threads[1]
        )
    ) {
        goto failure;
    }
    objects->threads[threads[1].slot].ipc_receive_buffer = 0;
    if (
        micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || !expect_no_message_completion(
            objects,
            0,
            MICROS_IPC_OK,
            TEST_COMPLETION_START
        )
    ) {
        goto failure;
    }
    micros_timer_test_fail_next_program();
    if (
        !snapshot_start_state(objects)
        || micros_scheduler_start() != MICROS_SCHEDULER_ERROR_TIMER
        || !start_state_matches(objects)
    ) {
        goto failure;
    }
    kernel_ticks_before_start =
        micros_kernel_object_runtime_boot_hart()
            ->kernel_counter_ticks;
    micros_timer_test_delay_next_program(
        TEST_START_TIMER_DELAY
    );

    (void)saved_status;
    micros_scheduler_test_enter();
    finish_test();

failure:
    riscv_irq_restore(saved_status);
    uart_write("MICROS_TEST_FAILURE scheduler-setup\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
