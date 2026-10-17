#include "kernel/tests/scheduler/scheduler_test_internal.h"

#include "arch/riscv64/platform.h"
#include "micros/user_address_space.h"

const uint64_t TEST_CODE_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE;

const uint64_t TEST_COUNTER_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00002000);

const uint64_t TEST_IPC_BUFFER_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00002100);

const uint64_t TEST_STACK_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00004000);

const uint64_t TEST_TIMER_INTERVAL =
    UINT64_C(0x0000000000010000);

const uint64_t TEST_QUANTUM =
    UINT64_C(0x0000000000030000);

const uint64_t TEST_START_TIMER_DELAY =
    UINT64_C(0x0000000000001000);

const uint32_t TEST_EXTERNAL_ATTEMPTS = UINT32_C(100000);

const uintptr_t TEST_UART_INTERRUPT_ENABLE =
    MICROS_RISCV_UART0_BASE + UINT64_C(1);

const uint8_t TEST_UART_INTERRUPT_TX_EMPTY = UINT8_C(0x02);

static struct scheduler_test_context test_state = {.last_running = 2};

struct scheduler_test_context *scheduler_test_state(void)
{
    return &test_state;
}

bool scheduler_test_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

void scheduler_test_copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

bool scheduler_test_bytes_equal(
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

void scheduler_test_clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

uint64_t scheduler_test_read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

void scheduler_test_fill_context_pattern(
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
    scheduler_test_copy_bytes(context, words, sizeof(*context));
}

uint64_t scheduler_test_user_address_of(const unsigned char *symbol)
{
    return TEST_CODE_VIRTUAL_ADDRESS
        + (
            (uintptr_t)symbol
            - (uintptr_t)micros_scheduler_payload_start
        );
}

size_t scheduler_test_current_thread_index(
    struct scheduler_test_context *fixture,const struct micros_hart *hart)
{
    size_t index;

    if (hart == NULL) {
        return 2;
    }
    for (index = 0; index < 2; ++index) {
        if (
            hart->current_thread.slot == fixture->threads[index].slot
            && hart->current_thread.generation
                == fixture->threads[index].generation
        ) {
            return index;
        }
    }
    return 2;
}
