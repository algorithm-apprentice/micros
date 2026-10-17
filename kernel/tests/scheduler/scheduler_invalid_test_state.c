#include "kernel/tests/scheduler/scheduler_invalid_test_internal.h"

#include "micros/user_address_space.h"

const uint64_t TEST_CODE_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE;

const uint64_t TEST_COUNTER_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00002000);

const uint64_t TEST_STACK_VIRTUAL_ADDRESS =
    MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00004000);

const uint64_t TEST_TIMER_INTERVAL =
    UINT64_C(0x0000000000010000);

const uint64_t TEST_QUANTUM =
    UINT64_C(0x0000000000018000);

static struct scheduler_invalid_test_context test_state;

struct scheduler_invalid_test_context *scheduler_invalid_test_state(void)
{
    return &test_state;
}

void scheduler_invalid_test_copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

bool scheduler_invalid_test_bytes_equal(
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

uint64_t scheduler_invalid_test_read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

uint64_t scheduler_invalid_test_user_address_of(const unsigned char *symbol)
{
    return TEST_CODE_VIRTUAL_ADDRESS
        + (
            (uintptr_t)symbol
            - (uintptr_t)micros_scheduler_payload_start
        );
}

void scheduler_invalid_test_fill_context_pattern(
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
    scheduler_invalid_test_copy_bytes(context, words, sizeof(*context));
}
