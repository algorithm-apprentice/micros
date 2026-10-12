#include "frame_allocator_test_internal.h"

#include <stdbool.h>
#include <stdio.h>

static struct frame_allocator_test_context test_context;

struct test_case {
    const char *name;
    bool (*run)(struct frame_allocator_test_context *context);
};

int main(void)
{
    static const struct test_case tests[] = {
        {
            "normalizes and subtracts ranges",
            frame_allocator_test_normalizes_and_subtracts_ranges,
        },
        {
            "allocates, releases, and reuses lowest frame",
            frame_allocator_test_allocates_releases_and_reuses_lowest_frame,
        },
        {
            "merges reservations and handles full removal",
            frame_allocator_test_merges_reservations_and_handles_full_removal,
        },
        {
            "rejects invalid inputs without state changes",
            frame_allocator_test_rejects_invalid_inputs_without_changing_state,
        },
        {
            "accepts capacity boundaries",
            frame_allocator_test_accepts_capacity_boundaries,
        },
        {
            "matches seeded reference model",
            frame_allocator_test_matches_seeded_reference_model,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run(&test_context)) {
            fprintf(stderr, "not ok %zu - %s\n", index + 1, tests[index].name);
            return 1;
        }
        printf("ok %zu - %s\n", index + 1, tests[index].name);
    }
    return 0;
}
