#include "kernel/user_runtime_test_internal.h"

#include "micros/kernel_object_runtime.h"

static struct user_runtime_test_context runtime_state = {
    .returned_actor = USER_RUNTIME_PROCESS_COUNT,
};

struct user_runtime_test_context *user_runtime_test_state(void)
{
    return &runtime_state;
}

void user_runtime_test_clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void user_runtime_test_fill_bytes(
    void *storage,
    unsigned char value,
    size_t size
)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = value;
    }
}

void user_runtime_test_copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    unsigned char *target = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        target[index] = input[index];
    }
}

bool user_runtime_test_bytes_equal(
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

bool user_runtime_test_bytes_are_zero(const void *storage, size_t size)
{
    const unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

uint64_t user_runtime_test_read_satp(void)
{
    uint64_t value;

    __asm__ volatile("csrr %0, satp" : "=r"(value));
    return value;
}

size_t user_runtime_test_current_actor(
    const struct user_runtime_test_context *runtime,
    const struct micros_hart *hart
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_thread_handle current;
    size_t actor;

    if (
        objects == NULL
        || hart == NULL
        || micros_hart_current_thread(
            objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            &current
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return USER_RUNTIME_PROCESS_COUNT;
    }
    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        if (
            current.slot == runtime->threads[actor].slot
            && current.generation == runtime->threads[actor].generation
        ) {
            return actor;
        }
    }
    return USER_RUNTIME_PROCESS_COUNT;
}
