#include "micros/grant_copy.h"

#include <stddef.h>
#include <stdint.h>

static enum micros_grant_error validate_range_plan(
    const struct micros_grant_copy_range_plan *plan,
    size_t length
)
{
    size_t total = 0;
    size_t index;

    if (plan == NULL) {
        return MICROS_GRANT_ERROR_ARGUMENT;
    }
    if (length == 0) {
        return plan->chunk_count == 0
            ? MICROS_GRANT_OK
            : MICROS_GRANT_ERROR_INVARIANT;
    }
    if (plan->chunk_count == 0 || plan->chunk_count > 2) {
        return MICROS_GRANT_ERROR_INVARIANT;
    }
    for (index = 0; index < plan->chunk_count; ++index) {
        const struct micros_grant_copy_chunk *chunk =
            &plan->chunks[index];

        if (
            chunk->length == 0
            || UINT64_MAX - chunk->physical_address
                < chunk->length
            || SIZE_MAX - total < chunk->length
        ) {
            return MICROS_GRANT_ERROR_INVARIANT;
        }
        total += chunk->length;
    }
    return total == length
        ? MICROS_GRANT_OK
        : MICROS_GRANT_ERROR_INVARIANT;
}

static bool chunks_overlap(
    const struct micros_grant_copy_chunk *left,
    const struct micros_grant_copy_chunk *right
)
{
    uint64_t left_end = left->physical_address + left->length;
    uint64_t right_end = right->physical_address + right->length;

    return (
        left->physical_address < right_end
        && right->physical_address < left_end
    );
}

enum micros_grant_error micros_grant_copy_plan_validate(
    const struct micros_grant_copy_range_plan *source,
    const struct micros_grant_copy_range_plan *destination,
    size_t length
)
{
    enum micros_grant_error error;
    size_t source_index;
    size_t destination_index;

    error = validate_range_plan(source, length);
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    error = validate_range_plan(destination, length);
    if (error != MICROS_GRANT_OK) {
        return error;
    }
    for (
        source_index = 0;
        source_index < source->chunk_count;
        ++source_index
    ) {
        for (
            destination_index = 0;
            destination_index < destination->chunk_count;
            ++destination_index
        ) {
            if (chunks_overlap(
                &source->chunks[source_index],
                &destination->chunks[destination_index]
            )) {
                return MICROS_GRANT_ERROR_INVARIANT;
            }
        }
    }
    return MICROS_GRANT_OK;
}

void micros_grant_copy_commit(
    const struct micros_grant_copy_range_plan *source,
    const struct micros_grant_copy_range_plan *destination,
    size_t length
)
{
    size_t source_index = 0;
    size_t destination_index = 0;
    size_t source_offset = 0;
    size_t destination_offset = 0;
    size_t copied = 0;

    while (copied < length) {
        const struct micros_grant_copy_chunk *source_chunk =
            &source->chunks[source_index];
        const struct micros_grant_copy_chunk *destination_chunk =
            &destination->chunks[destination_index];
        size_t source_remaining =
            source_chunk->length - source_offset;
        size_t destination_remaining =
            destination_chunk->length - destination_offset;
        size_t count = source_remaining < destination_remaining
            ? source_remaining
            : destination_remaining;
        const unsigned char *input =
            (const unsigned char *)(uintptr_t)(
                source_chunk->physical_address + source_offset
            );
        unsigned char *output =
            (unsigned char *)(uintptr_t)(
                destination_chunk->physical_address
                + destination_offset
            );
        size_t index;

        for (index = 0; index < count; ++index) {
            output[index] = input[index];
        }
        copied += count;
        source_offset += count;
        destination_offset += count;
        if (source_offset == source_chunk->length) {
            ++source_index;
            source_offset = 0;
        }
        if (destination_offset == destination_chunk->length) {
            ++destination_index;
            destination_offset = 0;
        }
    }
}
