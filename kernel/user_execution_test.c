#include "micros/user_execution.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

#define MICROS_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define MICROS_SCAUSE_CODE_MASK (MICROS_SCAUSE_INTERRUPT - 1)

#define TEST_SSTATUS_UBE (UINT64_C(1) << 6)
#define TEST_SSTATUS_VS (UINT64_C(3) << 9)
#define TEST_SSTATUS_FS (UINT64_C(3) << 13)
#define TEST_SSTATUS_XS (UINT64_C(3) << 15)
#define TEST_SSTATUS_MXR (UINT64_C(1) << 19)
#define TEST_SSTATUS_UXL_MASK (UINT64_C(3) << 32)
#define TEST_SSTATUS_UXL_64 (UINT64_C(2) << 32)
#define TEST_SSTATUS_SD (UINT64_C(1) << 63)

enum {
    USER_EXECUTION_EXCEPTION_USER_ECALL = 8,
    USER_EXECUTION_EXCEPTION_LOAD_PAGE_FAULT = 13,
};

enum user_execution_test_state {
    USER_EXECUTION_TEST_IDLE,
    USER_EXECUTION_TEST_KERNEL_FAULT,
    USER_EXECUTION_TEST_FIRST_ECALL,
    USER_EXECUTION_TEST_SECOND_ECALL,
    USER_EXECUTION_TEST_SUPERVISOR,
};

extern const unsigned char micros_user_execution_payload_start[];
extern const unsigned char micros_user_execution_payload_kernel_fault[];
extern const unsigned char micros_user_execution_payload_after_fault[];
extern const unsigned char micros_user_execution_payload_first_ecall[];
extern const unsigned char micros_user_execution_payload_resume[];
extern const unsigned char micros_user_execution_payload_second_ecall[];
extern const unsigned char micros_user_execution_payload_end[];
extern const unsigned char
    micros_user_execution_test_supervisor_resume[];
extern const unsigned char __thread_kernel_stacks_start[];
extern const unsigned char __thread_kernel_stacks_end[];
extern const uint64_t micros_user_execution_test_saved_state[17];
extern const uint64_t micros_user_execution_test_restored_state[17];
extern const uint64_t micros_user_execution_test_immediate_status;

void micros_user_execution_test_enter(
    uint64_t thread_slot,
    uint64_t thread_generation
);

_Noreturn void micros_user_execution_test_enter_production(
    uint64_t thread_slot,
    uint64_t thread_generation
);

static const uint64_t TEST_CODE_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE;
static const uint64_t TEST_STACK_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00004000);

static struct micros_kernel_objects objects_snapshot;
static struct micros_frame_ownership ownership_snapshot;
static struct micros_frame_allocator allocator_snapshot;
static unsigned char
    kernel_stack_snapshot[MICROS_THREAD_KERNEL_STACK_SIZE];
static unsigned char
    second_kernel_stack_snapshot[MICROS_THREAD_KERNEL_STACK_SIZE];
static struct micros_process_handle test_process;
static struct micros_process_handle second_process;
static struct micros_thread_handle test_thread;
static struct micros_thread_handle second_thread;
static struct micros_user_context initial_context;
static struct micros_user_context prepared_context;
static uint64_t code_physical_address;
static uint64_t stack_physical_address;
static uint64_t second_code_physical_address;
static uint64_t second_stack_physical_address;
static uint64_t kernel_stack_bottom;
static uint64_t kernel_stack_top;
static uint64_t second_kernel_stack_bottom;
static uint64_t second_kernel_stack_top;
static uint64_t live_sstatus_before_prepare;
static uint64_t baseline_owned;
static uint64_t baseline_free;
static volatile enum user_execution_test_state test_state;

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

static uint64_t user_address_of(const unsigned char *payload_symbol)
{
    return TEST_CODE_VIRTUAL_ADDRESS
        + (
            (uintptr_t)payload_symbol
            - (uintptr_t)micros_user_execution_payload_start
        );
}

static uint64_t read_sstatus(void)
{
        uint64_t status;

        __asm__ volatile("csrr %0, sstatus" : "=r"(status));
        return status;
}

static void write_sstatus(uint64_t status)
{
        __asm__ volatile("csrw sstatus, %0" : : "r"(status) : "memory");
}

static bool snapshot_prepare_state(
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects
)
{
    if (
        ledger == NULL
        || ledger->allocator == NULL
        || objects == NULL
        || kernel_stack_top <= kernel_stack_bottom
        || kernel_stack_top - kernel_stack_bottom
            != MICROS_THREAD_KERNEL_STACK_SIZE
        || second_kernel_stack_top <= second_kernel_stack_bottom
        || second_kernel_stack_top - second_kernel_stack_bottom
            != MICROS_THREAD_KERNEL_STACK_SIZE
    ) {
        return false;
    }
    copy_bytes(
        &ownership_snapshot,
        ledger,
        sizeof(ownership_snapshot)
    );
    copy_bytes(
        &allocator_snapshot,
        ledger->allocator,
        sizeof(allocator_snapshot)
    );
    copy_bytes(
        &objects_snapshot,
        objects,
        sizeof(objects_snapshot)
    );
    copy_bytes(
        kernel_stack_snapshot,
        (const void *)(uintptr_t)kernel_stack_bottom,
        sizeof(kernel_stack_snapshot)
    );
    copy_bytes(
        second_kernel_stack_snapshot,
        (const void *)(uintptr_t)second_kernel_stack_bottom,
        sizeof(second_kernel_stack_snapshot)
    );
    return true;
}

static bool prepare_state_matches(
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects
)
{
    return (
        ledger != NULL
        && ledger->allocator != NULL
        && objects != NULL
        && bytes_equal(
            ledger,
            &ownership_snapshot,
            sizeof(ownership_snapshot)
        )
        && bytes_equal(
            ledger->allocator,
            &allocator_snapshot,
            sizeof(allocator_snapshot)
        )
        && bytes_equal(
            objects,
            &objects_snapshot,
            sizeof(objects_snapshot)
        )
        && bytes_equal(
            (const void *)(uintptr_t)kernel_stack_bottom,
            kernel_stack_snapshot,
            sizeof(kernel_stack_snapshot)
        )
        && bytes_equal(
            (const void *)(uintptr_t)second_kernel_stack_bottom,
            second_kernel_stack_snapshot,
            sizeof(second_kernel_stack_snapshot)
        )
    );
}

static void fill_stack(uint64_t stack_bottom, uint64_t value)
{
    uint64_t *words = (uint64_t *)(uintptr_t)stack_bottom;
    size_t index;

    for (
        index = 0;
        index < MICROS_THREAD_KERNEL_STACK_SIZE / sizeof(uint64_t);
        ++index
    ) {
        words[index] = value;
    }
}

static bool stack_has_value(uint64_t stack_bottom, uint64_t value)
{
    const uint64_t *words =
        (const uint64_t *)(uintptr_t)stack_bottom;
    size_t index;

    for (
        index = 0;
        index < MICROS_THREAD_KERNEL_STACK_SIZE / sizeof(uint64_t);
        ++index
    ) {
        if (words[index] != value) {
            return false;
        }
    }
    return true;
}

static bool context_is_valid_user_status(
    const struct micros_user_context *context
)
{
    const uint64_t control_mask =
        MICROS_RISCV_SSTATUS_SIE
        | MICROS_RISCV_SSTATUS_SPIE
        | MICROS_RISCV_SSTATUS_SPP
        | MICROS_RISCV_SSTATUS_SUM
        | TEST_SSTATUS_UBE
        | TEST_SSTATUS_VS
        | TEST_SSTATUS_FS
        | TEST_SSTATUS_XS
        | TEST_SSTATUS_MXR;
    uint64_t expected = (
        live_sstatus_before_prepare
        & ~(control_mask | TEST_SSTATUS_SD)
    ) | MICROS_RISCV_SSTATUS_SPIE;

    return (
        context != NULL
        && context->sstatus == expected
        && (context->sstatus & TEST_SSTATUS_UXL_MASK)
            == TEST_SSTATUS_UXL_64
    );
}

static bool frame_has_user_origin(
    const struct micros_trap_frame *frame,
    uint64_t cause,
    uint64_t sepc
)
{
    return (
        frame != NULL
        && (frame->scause & MICROS_SCAUSE_INTERRUPT) == 0
        && (frame->scause & MICROS_SCAUSE_CODE_MASK) == cause
        && frame->sepc == sepc
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) == 0
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SUM) == 0
        && frame->hart_context != 0
    );
}

static bool inspect_matches_frame(
    const struct micros_trap_frame *frame
)
{
    struct micros_user_context observed;

    return (
        micros_user_execution_inspect(test_thread, &observed)
            == MICROS_USER_EXECUTION_OK
        && bytes_equal(&observed, frame, sizeof(observed))
    );
}

static bool frame_matches_expected_context(
    const struct micros_trap_frame *frame,
    uint64_t expected_sepc,
    uint64_t expected_a0,
    uint64_t expected_a7
)
{
    struct micros_user_context expected;
    uint64_t expected_words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    uint64_t observed_words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    copy_bytes(
        &expected,
        &prepared_context,
        sizeof(expected)
    );
    expected.sepc = expected_sepc;
    expected.a0 = expected_a0;
    expected.a7 = expected_a7;
    if (bytes_equal(&expected, frame, sizeof(expected))) {
        return true;
    }
    copy_bytes(expected_words, &expected, sizeof(expected_words));
    copy_bytes(observed_words, frame, sizeof(observed_words));
    for (
        index = 0;
        index < sizeof(expected_words) / sizeof(expected_words[0]);
        ++index
    ) {
        if (expected_words[index] != observed_words[index]) {
            uart_write("MICROS_USER_EXECUTION_CONTEXT_MISMATCH index=");
            uart_write_hex64(index);
            uart_write(" expected=");
            uart_write_hex64(expected_words[index]);
            uart_write(" observed=");
            uart_write_hex64(observed_words[index]);
            uart_write("\n");
            uart_flush();
            break;
        }
    }
    return false;
}

static uint64_t read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

bool micros_user_execution_test_pre_capture(
    struct micros_hart *hart,
    const struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_hart_handle hart_handle =
        micros_kernel_object_runtime_boot_hart_handle();
    struct micros_thread_handle current;
    const struct micros_process *process;
    uintptr_t frame_address = (uintptr_t)frame;
    uint64_t expected_sepc;
    uint64_t expected_a0;
    uint64_t expected_a7;

    if (
        objects == NULL
        || hart == NULL
        || frame == NULL
    ) {
        uart_write(
            "MICROS_TEST_FAILURE "
            "user-execution-pre-capture-argument\n"
        );
        uart_flush();
        return false;
    }
    if (
        micros_hart_current_thread(
            objects,
            hart_handle,
            &current
        ) != MICROS_KERNEL_OBJECT_OK
        || current.slot != test_thread.slot
        || current.generation != test_thread.generation
        || hart != &objects->harts[hart_handle.slot]
    ) {
        uart_write(
            "MICROS_TEST_FAILURE "
            "user-execution-pre-capture-current\n"
        );
        uart_flush();
        return false;
    }
    if (
        hart->trap.primary_stack_bottom != kernel_stack_bottom
        || hart->trap.primary_stack_top != kernel_stack_top
        || frame_address < kernel_stack_bottom
        || frame_address >= kernel_stack_top
        || sizeof(*frame) > kernel_stack_top - frame_address
    ) {
        uart_write(
            "MICROS_TEST_FAILURE "
            "user-execution-pre-capture-stack\n"
        );
        uart_flush();
        return false;
    }
    if (
        micros_process_resolve(
            objects,
            test_process,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
        || read_satp()
            != (
                MICROS_RISCV_SATP_MODE_SV39
                | (process->address_space_root >> 12)
            )
    ) {
        uart_write(
            "MICROS_TEST_FAILURE "
            "user-execution-pre-capture-root\n"
        );
        uart_flush();
        return false;
    }

    switch (test_state) {
    case USER_EXECUTION_TEST_KERNEL_FAULT:
        expected_sepc = user_address_of(
            micros_user_execution_payload_kernel_fault
        );
        expected_a0 = prepared_context.a0;
        expected_a7 = prepared_context.a7;
        break;
    case USER_EXECUTION_TEST_FIRST_ECALL:
        expected_sepc = user_address_of(
            micros_user_execution_payload_first_ecall
        );
        expected_a0 = prepared_context.a0;
        expected_a7 = 1;
        break;
    case USER_EXECUTION_TEST_SECOND_ECALL:
        expected_sepc = user_address_of(
            micros_user_execution_payload_second_ecall
        );
        expected_a0 = UINT64_C(0x0000000000000abd);
        expected_a7 = 2;
        break;
    default:
        return false;
    }
    if (
        !frame_matches_expected_context(
            frame,
            expected_sepc,
            expected_a0,
            expected_a7
        )
    ) {
        uart_write(
            "MICROS_TEST_FAILURE "
            "user-execution-pre-capture-context\n"
        );
        uart_flush();
        return false;
    }
    return true;
}

enum micros_user_execution_test_trap_result
micros_user_execution_handle_test_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_trap_frame invalid_return;

    if (test_state == USER_EXECUTION_TEST_IDLE) {
        return MICROS_USER_EXECUTION_TEST_TRAP_INACTIVE;
    }
    if (hart == NULL || frame == NULL || !inspect_matches_frame(frame)) {
        return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
    }

    if (test_state == USER_EXECUTION_TEST_KERNEL_FAULT) {
        if (
            !frame_has_user_origin(
                frame,
                USER_EXECUTION_EXCEPTION_LOAD_PAGE_FAULT,
                user_address_of(
                    micros_user_execution_payload_kernel_fault
                )
            )
            || frame->stval != UINT64_C(0x80200000)
        ) {
            return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
        }
        frame->sepc = user_address_of(
            micros_user_execution_payload_after_fault
        );
        test_state = USER_EXECUTION_TEST_FIRST_ECALL;
        return MICROS_USER_EXECUTION_TEST_TRAP_USER_RETURN;
    }

    if (test_state == USER_EXECUTION_TEST_FIRST_ECALL) {
        static const struct {
            uint64_t clear_mask;
            uint64_t set_mask;
        } invalid_statuses[] = {
            {0, MICROS_RISCV_SSTATUS_SIE},
            {MICROS_RISCV_SSTATUS_SPIE, 0},
            {0, MICROS_RISCV_SSTATUS_SPP},
            {0, TEST_SSTATUS_UBE},
            {0, UINT64_C(1) << 9},
            {0, UINT64_C(1) << 13},
            {0, UINT64_C(1) << 15},
            {0, MICROS_RISCV_SSTATUS_SUM},
            {0, TEST_SSTATUS_MXR},
            {
                TEST_SSTATUS_UXL_MASK,
                UINT64_C(1) << 32,
            },
            {0, TEST_SSTATUS_SD},
        };
        size_t index;

        if (
            !frame_has_user_origin(
                frame,
                USER_EXECUTION_EXCEPTION_USER_ECALL,
                user_address_of(
                    micros_user_execution_payload_first_ecall
                )
            )
            || frame->a7 != 1
        ) {
            return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
        }
        copy_bytes(&invalid_return, frame, sizeof(invalid_return));
        invalid_return.sepc = user_address_of(
            micros_user_execution_payload_resume
        ) + 1;
        if (
            micros_user_execution_validate_return(
                hart,
                &invalid_return
            ) != MICROS_USER_EXECUTION_ERROR_CONTEXT
        ) {
            return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
        }
        copy_bytes(&invalid_return, frame, sizeof(invalid_return));
        invalid_return.sepc = TEST_STACK_VIRTUAL_ADDRESS;
        if (
            micros_user_execution_validate_return(
                hart,
                &invalid_return
            ) != MICROS_USER_EXECUTION_ERROR_MAPPING
        ) {
            return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
        }
        copy_bytes(&invalid_return, frame, sizeof(invalid_return));
        invalid_return.sp =
            TEST_CODE_VIRTUAL_ADDRESS + MICROS_SV39_PAGE_SIZE;
        if (
            micros_user_execution_validate_return(
                hart,
                &invalid_return
            ) != MICROS_USER_EXECUTION_ERROR_MAPPING
        ) {
            return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
        }
        copy_bytes(&invalid_return, frame, sizeof(invalid_return));
        invalid_return.sp =
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00009000);
        if (
            micros_user_execution_validate_return(
                hart,
                &invalid_return
            ) != MICROS_USER_EXECUTION_ERROR_MAPPING
        ) {
            return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
        }
        copy_bytes(&invalid_return, frame, sizeof(invalid_return));
        --invalid_return.sp;
        if (
            micros_user_execution_validate_return(
                hart,
                &invalid_return
            ) != MICROS_USER_EXECUTION_ERROR_CONTEXT
        ) {
            return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
        }
        for (
            index = 0;
            index
                < sizeof(invalid_statuses)
                    / sizeof(invalid_statuses[0]);
            ++index
        ) {
            copy_bytes(
                &invalid_return,
                frame,
                sizeof(invalid_return)
            );
            invalid_return.sstatus &=
                ~invalid_statuses[index].clear_mask;
            invalid_return.sstatus |=
                invalid_statuses[index].set_mask;
            if (
                micros_user_execution_validate_return(
                    hart,
                    &invalid_return
                ) != MICROS_USER_EXECUTION_ERROR_CONTEXT
            ) {
                return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
            }
        }
        copy_bytes(&invalid_return, frame, sizeof(invalid_return));
        invalid_return.sstatus ^= UINT64_C(1) << 20;
        if (
            micros_user_execution_validate_return(
                hart,
                &invalid_return
            ) != MICROS_USER_EXECUTION_ERROR_CONTEXT
        ) {
            return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
        }
        frame->a0 = UINT64_C(0x0000000000000abc);
        frame->sepc = user_address_of(
            micros_user_execution_payload_resume
        );
        test_state = USER_EXECUTION_TEST_SECOND_ECALL;
        return MICROS_USER_EXECUTION_TEST_TRAP_USER_RETURN;
    }

    if (test_state == USER_EXECUTION_TEST_SECOND_ECALL) {
        struct micros_kernel_objects *objects =
            micros_kernel_object_runtime_test_registry();
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
            !frame_has_user_origin(
                frame,
                USER_EXECUTION_EXCEPTION_USER_ECALL,
                user_address_of(
                    micros_user_execution_payload_second_ecall
                )
            )
            || frame->a7 != 2
            || frame->a0 != UINT64_C(0x0000000000000abd)
            || objects == NULL
            || micros_scheduler_test_prepare_supervisor_return(
                hart,
                frame
            ) != MICROS_SCHEDULER_OK
        ) {
            return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
        }
        frame->sp = micros_user_execution_test_saved_state[1];
        frame->sepc = (uintptr_t)
            micros_user_execution_test_supervisor_resume;
        frame->sstatus &= ~control_mask;
        frame->sstatus |= MICROS_RISCV_SSTATUS_SPP;
        test_state = USER_EXECUTION_TEST_SUPERVISOR;
        return MICROS_USER_EXECUTION_TEST_TRAP_SUPERVISOR_RETURN;
    }
    return MICROS_USER_EXECUTION_TEST_TRAP_MISMATCH;
}

_Noreturn void micros_user_execution_test_enter_production(
    uint64_t thread_slot,
    uint64_t thread_generation
)
{
    struct micros_thread_handle thread = {
        (uint16_t)thread_slot,
        (uint32_t)thread_generation,
    };

    micros_scheduler_test_enter_without_timer(thread);
}

static _Noreturn void micros_user_execution_test_finish(void)
{
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_user_context observed;
    struct micros_user_context unchanged;
    struct micros_thread_handle replacement;
    uint64_t released;

    if (
        test_state != USER_EXECUTION_TEST_SUPERVISOR
        || ledger == NULL
        || objects == NULL
        || micros_user_execution_inspect(test_thread, &observed)
            != MICROS_USER_EXECUTION_OK
        || observed.a0 != UINT64_C(0x0000000000000abd)
        || observed.a7 != 2
        || observed.sepc != user_address_of(
            micros_user_execution_payload_second_ecall
        )
        || micros_thread_scheduler_remove(objects, test_thread)
            != MICROS_KERNEL_OBJECT_OK
        || micros_user_execution_detach(test_thread)
            != MICROS_USER_EXECUTION_OK
        || micros_thread_release(objects, test_thread)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        goto failure;
    }

    fill_stack(
        kernel_stack_bottom,
        UINT64_C(0xa5a5a5a5a5a5a5a5)
    );
    fill_stack(
        second_kernel_stack_bottom,
        UINT64_C(0x5a5a5a5a5a5a5a5a)
    );
    fill_context_pattern(&unchanged, UINT64_C(0xdead0000));
    copy_bytes(&observed, &unchanged, sizeof(observed));
    if (
        micros_thread_create(objects, test_process, &replacement)
            != MICROS_KERNEL_OBJECT_OK
        || replacement.slot != test_thread.slot
        || replacement.generation != test_thread.generation + 1
        || !snapshot_prepare_state(ledger, objects)
        || micros_user_execution_prepare(
            test_thread,
            &initial_context
        ) != MICROS_USER_EXECUTION_ERROR_STALE
        || micros_user_execution_inspect(test_thread, &observed)
            != MICROS_USER_EXECUTION_ERROR_STALE
        || !bytes_equal(&observed, &unchanged, sizeof(observed))
        || micros_user_execution_detach(test_thread)
            != MICROS_USER_EXECUTION_ERROR_STALE
        || !prepare_state_matches(ledger, objects)
        || micros_user_execution_prepare(
            replacement,
            &initial_context
        ) != MICROS_USER_EXECUTION_OK
        || !stack_has_value(kernel_stack_bottom, 0)
        || micros_user_execution_detach(replacement)
            != MICROS_USER_EXECUTION_OK
        || micros_thread_release(objects, replacement)
            != MICROS_KERNEL_OBJECT_OK
        || micros_user_execution_detach(second_thread)
            != MICROS_USER_EXECUTION_OK
        || micros_thread_release(objects, second_thread)
            != MICROS_KERNEL_OBJECT_OK
        || micros_user_address_space_release_page(
            second_process,
            TEST_CODE_VIRTUAL_ADDRESS,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != second_code_physical_address
        || micros_user_address_space_release_page(
            second_process,
            TEST_STACK_VIRTUAL_ADDRESS,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != second_stack_physical_address
        || micros_user_address_space_destroy(second_process)
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_process_release(objects, second_process)
            != MICROS_KERNEL_OBJECT_OK
        || micros_user_address_space_release_page(
            test_process,
            TEST_CODE_VIRTUAL_ADDRESS,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != code_physical_address
        || micros_user_address_space_release_page(
            test_process,
            TEST_STACK_VIRTUAL_ADDRESS,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != stack_physical_address
        || micros_user_address_space_destroy(test_process)
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_frame_ownership_runtime_release_process(
            objects,
            test_process
        ) != MICROS_KERNEL_OBJECT_OK
        || ledger->owned_frame_count != baseline_owned
        || ledger->allocator->free_frame_count != baseline_free
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto failure;
    }

    uart_write(
        "MICROS_USER_EXECUTION_TEST_PASS "
        "mode=entered faults=isolated context=preserved "
        "stack=owned return=resumed\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );

failure:
    uart_write("MICROS_TEST_FAILURE user-execution-test\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}

_Noreturn void micros_user_execution_runtime_run_self_test(void)
{
    const uint32_t code_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_EXECUTE;
    const uint32_t stack_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE;
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    struct micros_user_context invalid_context;
    struct micros_user_context observed;
    size_t payload_size;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    objects = micros_kernel_object_runtime_test_registry();
    ledger = micros_frame_ownership_runtime_ledger();
    if (
        objects == NULL
        || ledger == NULL
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto failure;
    }
    baseline_owned = ledger->owned_frame_count;
    baseline_free = ledger->allocator->free_frame_count;

    if (
        micros_process_create(objects, &test_process)
            != MICROS_KERNEL_OBJECT_OK
        || micros_user_address_space_create(test_process)
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            test_process,
            TEST_CODE_VIRTUAL_ADDRESS,
            code_permissions,
            &code_physical_address
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            test_process,
            TEST_STACK_VIRTUAL_ADDRESS,
            stack_permissions,
            &stack_physical_address
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_thread_create(
            objects,
            test_process,
            &test_thread
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(objects, &second_process)
            != MICROS_KERNEL_OBJECT_OK
        || micros_user_address_space_create(second_process)
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            second_process,
            TEST_CODE_VIRTUAL_ADDRESS,
            code_permissions,
            &second_code_physical_address
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            second_process,
            TEST_STACK_VIRTUAL_ADDRESS,
            stack_permissions,
            &second_stack_physical_address
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_thread_create(
            objects,
            second_process,
            &second_thread
        ) != MICROS_KERNEL_OBJECT_OK
        || !micros_user_execution_test_stack_bounds(
            test_thread,
            &kernel_stack_bottom,
            &kernel_stack_top
        )
        || !micros_user_execution_test_stack_bounds(
            second_thread,
            &second_kernel_stack_bottom,
            &second_kernel_stack_top
        )
        || (uintptr_t)__thread_kernel_stacks_end
            - (uintptr_t)__thread_kernel_stacks_start
            != (
                MICROS_THREAD_CAPACITY
                * MICROS_THREAD_KERNEL_STACK_SIZE
            )
        || kernel_stack_bottom
            != (
                (uintptr_t)__thread_kernel_stacks_start
                + (
                    test_thread.slot
                    * MICROS_THREAD_KERNEL_STACK_SIZE
                )
            )
        || kernel_stack_top
            != kernel_stack_bottom
                + MICROS_THREAD_KERNEL_STACK_SIZE
        || second_kernel_stack_bottom
            != (
                (uintptr_t)__thread_kernel_stacks_start
                + (
                    second_thread.slot
                    * MICROS_THREAD_KERNEL_STACK_SIZE
                )
            )
        || second_kernel_stack_top
            != second_kernel_stack_bottom
                + MICROS_THREAD_KERNEL_STACK_SIZE
        || kernel_stack_top > second_kernel_stack_bottom
    ) {
        goto failure;
    }

    payload_size =
        (uintptr_t)micros_user_execution_payload_end
        - (uintptr_t)micros_user_execution_payload_start;
    if (payload_size == 0 || payload_size > MICROS_SV39_PAGE_SIZE) {
        goto failure;
    }
    copy_bytes(
        (void *)(uintptr_t)code_physical_address,
        micros_user_execution_payload_start,
        payload_size
    );
    copy_bytes(
        (void *)(uintptr_t)second_code_physical_address,
        micros_user_execution_payload_start,
        payload_size
    );
    __asm__ volatile("fence.i" : : : "memory");

    fill_context_pattern(&initial_context, UINT64_C(0x100));
    initial_context.sepc = TEST_CODE_VIRTUAL_ADDRESS;
    initial_context.sp =
        TEST_STACK_VIRTUAL_ADDRESS + MICROS_SV39_PAGE_SIZE;
    initial_context.sstatus = 0;
    initial_context.t0 = UINT64_C(0x80200000);

    fill_stack(
        kernel_stack_bottom,
        UINT64_C(0xa5a5a5a5a5a5a5a5)
    );
    fill_stack(
        second_kernel_stack_bottom,
        UINT64_C(0x5a5a5a5a5a5a5a5a)
    );
    live_sstatus_before_prepare = read_sstatus() | TEST_SSTATUS_FS;
    write_sstatus(live_sstatus_before_prepare);
    live_sstatus_before_prepare = read_sstatus();
    if (
        (live_sstatus_before_prepare & TEST_SSTATUS_FS)
            != TEST_SSTATUS_FS
        || (live_sstatus_before_prepare & TEST_SSTATUS_SD) == 0
    ) {
        goto failure;
    }
    copy_bytes(
        &invalid_context,
        &initial_context,
        sizeof(invalid_context)
    );
    invalid_context.sstatus = 1;
    if (
        !snapshot_prepare_state(ledger, objects)
        || micros_user_execution_prepare(
            test_thread,
            &invalid_context
        ) != MICROS_USER_EXECUTION_ERROR_CONTEXT
        || !prepare_state_matches(ledger, objects)
    ) {
        goto failure;
    }
    copy_bytes(
        &invalid_context,
        &initial_context,
        sizeof(invalid_context)
    );
    ++invalid_context.sepc;
    if (
        !snapshot_prepare_state(ledger, objects)
        || micros_user_execution_prepare(
            test_thread,
            &invalid_context
        ) != MICROS_USER_EXECUTION_ERROR_CONTEXT
        || !prepare_state_matches(ledger, objects)
    ) {
        goto failure;
    }
    copy_bytes(
        &invalid_context,
        &initial_context,
        sizeof(invalid_context)
    );
    invalid_context.sepc = TEST_STACK_VIRTUAL_ADDRESS;
    if (
        !snapshot_prepare_state(ledger, objects)
        || micros_user_execution_prepare(
            test_thread,
            &invalid_context
        ) != MICROS_USER_EXECUTION_ERROR_MAPPING
        || !prepare_state_matches(ledger, objects)
    ) {
        goto failure;
    }
    copy_bytes(
        &invalid_context,
        &initial_context,
        sizeof(invalid_context)
    );
    --invalid_context.sp;
    if (
        !snapshot_prepare_state(ledger, objects)
        || micros_user_execution_prepare(
            test_thread,
            &invalid_context
        ) != MICROS_USER_EXECUTION_ERROR_CONTEXT
        || !prepare_state_matches(ledger, objects)
    ) {
        goto failure;
    }
    copy_bytes(
        &invalid_context,
        &initial_context,
        sizeof(invalid_context)
    );
    invalid_context.sp =
        TEST_CODE_VIRTUAL_ADDRESS + MICROS_SV39_PAGE_SIZE;
    if (
        !snapshot_prepare_state(ledger, objects)
        || micros_user_execution_prepare(
            test_thread,
            &invalid_context
        ) != MICROS_USER_EXECUTION_ERROR_MAPPING
        || !prepare_state_matches(ledger, objects)
    ) {
        goto failure;
    }
    copy_bytes(
        &invalid_context,
        &initial_context,
        sizeof(invalid_context)
    );
    invalid_context.sp =
        MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00009000);
    if (
        !snapshot_prepare_state(ledger, objects)
        || micros_user_execution_prepare(
            test_thread,
            &invalid_context
        ) != MICROS_USER_EXECUTION_ERROR_MAPPING
        || !prepare_state_matches(ledger, objects)
    ) {
        goto failure;
    }

    if (
        micros_user_execution_prepare(
            test_thread,
            &initial_context
        ) != MICROS_USER_EXECUTION_OK
        || !stack_has_value(kernel_stack_bottom, 0)
        || !stack_has_value(
            second_kernel_stack_bottom,
            UINT64_C(0x5a5a5a5a5a5a5a5a)
        )
        || micros_user_execution_inspect(test_thread, &observed)
            != MICROS_USER_EXECUTION_OK
        || !context_is_valid_user_status(&observed)
        || observed.sepc != initial_context.sepc
        || observed.sp != initial_context.sp
    ) {
        goto failure;
    }
    copy_bytes(
        &prepared_context,
        &observed,
        sizeof(prepared_context)
    );
    if (
        !snapshot_prepare_state(ledger, objects)
        || micros_user_execution_prepare(
            test_thread,
            &initial_context
        ) != MICROS_USER_EXECUTION_ERROR_STATE
        || !prepare_state_matches(ledger, objects)
    ) {
        goto failure;
    }
    fill_stack(
        kernel_stack_bottom,
        UINT64_C(0x3c3c3c3c3c3c3c3c)
    );
    if (
        micros_user_execution_prepare(
            second_thread,
            &initial_context
        ) != MICROS_USER_EXECUTION_OK
        || !stack_has_value(second_kernel_stack_bottom, 0)
        || !stack_has_value(
            kernel_stack_bottom,
            UINT64_C(0x3c3c3c3c3c3c3c3c)
        )
    ) {
        goto failure;
    }
    fill_stack(kernel_stack_bottom, 0);

    test_state = USER_EXECUTION_TEST_KERNEL_FAULT;
    micros_user_execution_test_enter(
        test_thread.slot,
        test_thread.generation
    );
    if (
        test_state != USER_EXECUTION_TEST_SUPERVISOR
        || (
            micros_user_execution_test_immediate_status
            & MICROS_RISCV_SSTATUS_SIE
        ) != 0
        || !bytes_equal(
            micros_user_execution_test_saved_state,
            micros_user_execution_test_restored_state,
            sizeof(uint64_t) * 17
        )
    ) {
        goto failure;
    }
    micros_user_execution_test_finish();

failure:
    riscv_irq_restore(saved_status);
    uart_write("MICROS_TEST_FAILURE user-execution-setup\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
