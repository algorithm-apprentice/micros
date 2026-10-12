#include "frame_ownership_test_internal.h"

#include <stdbool.h>
#include <stdio.h>

struct test_case {
    const char *name;
    bool (*run)(struct frame_ownership_test_context *context);
};

int main(void)
{
    static const struct test_case tests[] = {
        {
            "constructs exact owner records",
            frame_ownership_test_constructs_exact_owner_records,
        },
        {
            "initializes once on pristine storage",
            frame_ownership_test_initializes_once_on_pristine_storage,
        },
        {
            "rejects invalid raw owner records",
            frame_ownership_test_rejects_invalid_raw_owner_records,
        },
        {
            "allocates, looks up, and releases exact owners",
            frame_ownership_test_allocates_looks_up_and_releases_exact_owners,
        },
        {
            "rejects geometry changes before mutation",
            frame_ownership_test_rejects_geometry_changes_before_mutation,
        },
        {
            "rolls back defensive post-allocation failure",
            frame_ownership_test_rolls_back_defensive_post_allocation_failure,
        },
        {
            "atomically releases process-owned sets",
            frame_ownership_test_atomically_releases_process_owned_sets,
        },
        {
            "stages and atomically commits handoff",
            frame_ownership_test_stages_and_atomically_commits_handoff,
        },
        {
            "atomically stages complete wired set",
            frame_ownership_test_atomically_stages_complete_wired_set,
        },
        {
            "rejects independent corruption",
            frame_ownership_test_validator_rejects_independent_corruption,
        },
        {
            "operates at maximum managed capacity",
            frame_ownership_test_operates_at_maximum_managed_capacity,
        },
        {
            "matches seeded reference model",
            frame_ownership_test_matches_seeded_reference_model,
        },
        {
            "commits post-handoff VM ownership lifecycle",
            frame_ownership_test_commits_post_handoff_vm_ownership_lifecycle,
        },
        {
            "keeps post-handoff VM preflight atomic",
            frame_ownership_test_post_handoff_vm_ownership_preflight_is_atomic,
        },
    };
    struct frame_ownership_test_context *context = frame_ownership_test_context();
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run(context)) {
            fprintf(stderr, "not ok %zu - %s\n", index + 1, tests[index].name);
            return 1;
        }
        printf("ok %zu - %s\n", index + 1, tests[index].name);
    }
    return 0;
}
