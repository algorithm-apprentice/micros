#include "kernel_objects_test_internal.h"

static struct micros_kernel_objects objects;

struct micros_kernel_objects *kernel_object_test_registry(void)
{
    return &objects;
}

void kernel_object_test_reset_objects(struct micros_kernel_objects *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
}

struct micros_user_context kernel_object_test_context_pattern(uint64_t base)
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

bool kernel_object_test_contexts_equal(
    const struct micros_user_context *left,
    const struct micros_user_context *right
)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}
