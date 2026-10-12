#include "lib/spawn/spawn_request.h"

#include <stdbool.h>

static bool spawn_request_add_size(
    size_t left,
    size_t right,
    size_t *result
)
{
    if (right > SIZE_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool spawn_request_align_size_16(
    size_t value,
    size_t *result
)
{
    size_t added;

    if (!spawn_request_add_size(value, 15, &added)) {
        return false;
    }
    *result = added & ~(size_t)15;
    return true;
}

enum micros_spawn_stack_layout_error
micros_spawn_stack_layout_plan(
    const struct micros_spawn_request *request,
    struct micros_spawn_stack_layout *layout
)
{
    enum {
        APPLICATION_AUXILIARY_BYTES =
            MICROS_APPLICATION_AUXV_COUNT
                * 2
                * sizeof(uint64_t),
    };
    struct micros_spawn_stack_layout candidate = {0};
    size_t relative_offset = sizeof(uint64_t);
    size_t strings_size = 0;
    size_t occupied_size;
    size_t index;

    if (
        request == NULL
        || layout == NULL
        || request->argument_count == 0
        || request->argument_count
            > MICROS_SPAWN_ARGUMENT_CAPACITY
        || request->environment_count
            > MICROS_SPAWN_ENVIRONMENT_CAPACITY
    ) {
        return MICROS_SPAWN_STACK_LAYOUT_ERROR_ARGUMENT;
    }
    if (
        !spawn_request_add_size(
            relative_offset,
            ((size_t)request->argument_count + 1)
                * sizeof(uint64_t),
            &relative_offset
        )
    ) {
        return MICROS_SPAWN_STACK_LAYOUT_ERROR_TOO_BIG;
    }
    candidate.environment_offset = (uint32_t)relative_offset;
    if (
        !spawn_request_add_size(
            relative_offset,
            ((size_t)request->environment_count + 1)
                * sizeof(uint64_t),
            &relative_offset
        )
        || !spawn_request_align_size_16(
            relative_offset,
            &relative_offset
        )
    ) {
        return MICROS_SPAWN_STACK_LAYOUT_ERROR_TOO_BIG;
    }
    candidate.auxiliary_offset = (uint32_t)relative_offset;
    if (
        !spawn_request_add_size(
            relative_offset,
            APPLICATION_AUXILIARY_BYTES,
            &relative_offset
        )
    ) {
        return MICROS_SPAWN_STACK_LAYOUT_ERROR_TOO_BIG;
    }
    candidate.strings_offset = (uint32_t)relative_offset;
    for (index = 0; index < request->argument_count; ++index) {
        if (
            request->arguments[index].size == 0
            || request->arguments[index].size
                > MICROS_SPAWN_STRING_SIZE_MAX
            || !spawn_request_add_size(
                strings_size,
                request->arguments[index].size,
                &strings_size
            )
        ) {
            return MICROS_SPAWN_STACK_LAYOUT_ERROR_ARGUMENT;
        }
    }
    for (
        index = 0;
        index < request->environment_count;
        ++index
    ) {
        if (
            request->environment[index].size == 0
            || request->environment[index].size
                > MICROS_SPAWN_STRING_SIZE_MAX
            || !spawn_request_add_size(
                strings_size,
                request->environment[index].size,
                &strings_size
            )
        ) {
            return MICROS_SPAWN_STACK_LAYOUT_ERROR_ARGUMENT;
        }
    }
    if (
        !spawn_request_add_size(
            relative_offset,
            strings_size,
            &relative_offset
        )
        || !spawn_request_align_size_16(
            relative_offset,
            &occupied_size
        )
        || occupied_size > MICROS_APPLICATION_STACK_OCCUPIED_MAX
        || occupied_size > MICROS_APPLICATION_STACK_PAGE_SIZE
    ) {
        return MICROS_SPAWN_STACK_LAYOUT_ERROR_TOO_BIG;
    }
    candidate.frame_size = (uint32_t)relative_offset;
    candidate.occupied_size = (uint32_t)occupied_size;
    *layout = candidate;
    return MICROS_SPAWN_STACK_LAYOUT_OK;
}
