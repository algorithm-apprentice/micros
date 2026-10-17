#include "kernel/tests/execution/user_execution_test_internal.h"

#include "arch/riscv64/mmu.h"
#include "arch/riscv64/trap_context.h"
#include "micros/kernel_object_runtime.h"

static struct micros_user_execution_test_context test_context;

void micros_user_execution_test_copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

bool micros_user_execution_test_bytes_equal(
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

struct micros_user_execution_test_context *
micros_user_execution_test_context(void)
{
    return &test_context;
}

bool micros_user_execution_test_acquire(
    struct micros_user_execution_test_context *context
)
{
    unsigned char *bytes;
    size_t index;

    if (context == NULL) {
        return false;
    }
    bytes = (unsigned char *)context;
    for (index = 0; index < sizeof(*context); ++index) {
        bytes[index] = 0;
    }
    context->objects = micros_kernel_object_runtime_test_registry();
    context->ledger = micros_frame_ownership_runtime_ledger();
    if (
        context->objects == NULL
        || context->ledger == NULL
        || micros_frame_ownership_runtime_validate(context->objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return false;
    }
    context->baseline_owned = context->ledger->owned_frame_count;
    context->baseline_free =
        context->ledger->allocator->free_frame_count;
    return true;
}

void micros_user_execution_test_fill_context_pattern(
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
    micros_user_execution_test_copy_bytes(
        context,
        words,
        sizeof(*context)
    );
}

uint64_t micros_user_execution_test_user_address_of(
    const unsigned char *payload_symbol
)
{
    return (
        MICROS_USER_EXECUTION_TEST_CODE_VIRTUAL_ADDRESS
        + (
            (uintptr_t)payload_symbol
            - (uintptr_t)micros_user_execution_payload_start
        )
    );
}

uint64_t micros_user_execution_test_read_sstatus(void)
{
    uint64_t status;

    __asm__ volatile("csrr %0, sstatus" : "=r"(status));
    return status;
}

void micros_user_execution_test_write_sstatus(uint64_t status)
{
    __asm__ volatile(
        "csrw sstatus, %0"
        :
        : "r"(status)
        : "memory"
    );
}

uint64_t micros_user_execution_test_read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

bool micros_user_execution_test_snapshot_prepare_state(
    struct micros_user_execution_test_context *context
)
{
    if (
        context == NULL
        || context->ledger == NULL
        || context->ledger->allocator == NULL
        || context->objects == NULL
        || context->kernel_stack_top
            <= context->kernel_stack_bottom
        || context->kernel_stack_top
                - context->kernel_stack_bottom
            != MICROS_THREAD_KERNEL_STACK_SIZE
        || context->second_kernel_stack_top
            <= context->second_kernel_stack_bottom
        || context->second_kernel_stack_top
                - context->second_kernel_stack_bottom
            != MICROS_THREAD_KERNEL_STACK_SIZE
    ) {
        return false;
    }
    micros_user_execution_test_copy_bytes(
        &context->ownership_snapshot,
        context->ledger,
        sizeof(context->ownership_snapshot)
    );
    micros_user_execution_test_copy_bytes(
        &context->allocator_snapshot,
        context->ledger->allocator,
        sizeof(context->allocator_snapshot)
    );
    micros_user_execution_test_copy_bytes(
        &context->objects_snapshot,
        context->objects,
        sizeof(context->objects_snapshot)
    );
    micros_user_execution_test_copy_bytes(
        context->kernel_stack_snapshot,
        (const void *)(uintptr_t)context->kernel_stack_bottom,
        sizeof(context->kernel_stack_snapshot)
    );
    micros_user_execution_test_copy_bytes(
        context->second_kernel_stack_snapshot,
        (const void *)(uintptr_t)
            context->second_kernel_stack_bottom,
        sizeof(context->second_kernel_stack_snapshot)
    );
    return true;
}

bool micros_user_execution_test_prepare_state_matches(
    const struct micros_user_execution_test_context *context
)
{
    return (
        context != NULL
        && context->ledger != NULL
        && context->ledger->allocator != NULL
        && context->objects != NULL
        && micros_user_execution_test_bytes_equal(
            context->ledger,
            &context->ownership_snapshot,
            sizeof(context->ownership_snapshot)
        )
        && micros_user_execution_test_bytes_equal(
            context->ledger->allocator,
            &context->allocator_snapshot,
            sizeof(context->allocator_snapshot)
        )
        && micros_user_execution_test_bytes_equal(
            context->objects,
            &context->objects_snapshot,
            sizeof(context->objects_snapshot)
        )
        && micros_user_execution_test_bytes_equal(
            (const void *)(uintptr_t)context->kernel_stack_bottom,
            context->kernel_stack_snapshot,
            sizeof(context->kernel_stack_snapshot)
        )
        && micros_user_execution_test_bytes_equal(
            (const void *)(uintptr_t)
                context->second_kernel_stack_bottom,
            context->second_kernel_stack_snapshot,
            sizeof(context->second_kernel_stack_snapshot)
        )
    );
}

void micros_user_execution_test_fill_stack(
    uint64_t stack_bottom,
    uint64_t value
)
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

bool micros_user_execution_test_stack_has_value(
    uint64_t stack_bottom,
    uint64_t value
)
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

bool micros_user_execution_test_context_has_valid_status(
    const struct micros_user_execution_test_context *context,
    const struct micros_user_context *user_context
)
{
    const uint64_t control_mask =
        MICROS_RISCV_SSTATUS_SIE
        | MICROS_RISCV_SSTATUS_SPIE
        | MICROS_RISCV_SSTATUS_SPP
        | MICROS_RISCV_SSTATUS_SUM
        | MICROS_USER_EXECUTION_TEST_SSTATUS_UBE
        | MICROS_USER_EXECUTION_TEST_SSTATUS_VS
        | MICROS_USER_EXECUTION_TEST_SSTATUS_FS
        | MICROS_USER_EXECUTION_TEST_SSTATUS_XS
        | MICROS_USER_EXECUTION_TEST_SSTATUS_MXR;
    uint64_t expected;

    if (context == NULL || user_context == NULL) {
        return false;
    }
    expected = (
        context->live_sstatus_before_prepare
        & ~(control_mask | MICROS_USER_EXECUTION_TEST_SSTATUS_SD)
    ) | MICROS_RISCV_SSTATUS_SPIE;
    return (
        user_context->sstatus == expected
        && (
            user_context->sstatus
            & MICROS_USER_EXECUTION_TEST_SSTATUS_UXL_MASK
        ) == MICROS_USER_EXECUTION_TEST_SSTATUS_UXL_64
    );
}
