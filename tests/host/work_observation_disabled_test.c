#include "micros/work_observation.h"
#include "tests/host/work_counts_fixture.h"

#include <stdio.h>

static int evaluations;

static int next_value(void)
{
    ++evaluations;
    return evaluations;
}

int main(void)
{
    uint8_t values[65];
    uint8_t output;
    static const size_t counts[] = {0, 1, 63, 64, 65};
    size_t count_index;
    size_t index;

    MICROS_TEST_WORK_HIT(
        "discarded-arguments",
        undeclared_owner,
        undeclared_kind,
        next_value()
    );
    if (evaluations != 0) {
        fprintf(stderr, "not ok 1 - disabled observation evaluated arguments\n");
        return 1;
    }
    if (
        !MICROS_TEST_WORK_COMPARE(
            "single-evaluation",
            undeclared_owner,
            next_value() == 1
        )
        || evaluations != 1
    ) {
        fprintf(stderr, "not ok 2 - comparison evaluation changed\n");
        return 1;
    }
    printf("ok 1 - disabled observation discards arguments\n");
    printf("ok 2 - comparison evaluates exactly once\n");
    for (count_index = 0; count_index < sizeof(counts) / sizeof(counts[0]);
        ++count_index) {
        size_t count = counts[count_index];

        for (index = 0; index < count; ++index) {
            values[index] = (uint8_t)(count - index);
        }
        output = 0xa5;
        if (
            work_counts_fixture_search(values, count, 1, &output) != (count != 0)
            || output != (count == 0 ? 0xa5 : 1)
        ) {
            fprintf(stderr, "not ok 3 - disabled fixture semantics changed\n");
            return 1;
        }
    }
    printf("ok 3 - disabled fixture retains semantic results\n");
    return 0;
}
