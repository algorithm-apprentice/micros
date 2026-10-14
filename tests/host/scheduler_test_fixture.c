#include "tests/host/scheduler_test_internal.h"

static struct scheduler_host_fixture fixture_state;

struct scheduler_host_fixture *scheduler_host_fixture(void)
{
    return &fixture_state;
}

bool scheduler_host_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static struct micros_user_context context_pattern(uint64_t base)
{
    struct micros_user_context context;
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
    memcpy(&context, words, sizeof(context));
    return context;
}

bool scheduler_host_setup_fixture(struct scheduler_host_fixture *fixture)
{
    size_t index;

    memset(&fixture->objects, 0, sizeof(fixture->objects));
    if (
        micros_kernel_objects_initialize(
            &fixture->objects,
            TEST_THREAD_COUNT,
            2
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&fixture->objects, 0, &fixture->harts[0])
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&fixture->objects, 1, &fixture->harts[1])
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            &fixture->objects,
            fixture->harts[0],
            UINT64_C(0x01000000),
            UINT64_C(0x01004000),
            UINT64_C(0x02000000),
            UINT64_C(0x02001000)
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            &fixture->objects,
            fixture->harts[1],
            UINT64_C(0x03000000),
            UINT64_C(0x03004000),
            UINT64_C(0x04000000),
            UINT64_C(0x04001000)
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(&fixture->objects, &fixture->process)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }

    for (index = 0; index < TEST_THREAD_COUNT; ++index) {
        struct micros_user_context context =
            context_pattern(UINT64_C(0x1000) + index * UINT64_C(0x100));
        uintptr_t bottom =
            UINT64_C(0x10000000)
            + index * UINT64_C(0x00008000);

        if (
            micros_thread_create(
                &fixture->objects,
                fixture->process,
                &fixture->threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_attach_execution_context(
                &fixture->objects,
                fixture->threads[index],
                bottom,
                bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
                &context
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    return true;
}
