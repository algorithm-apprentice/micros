#include "tests/host/work_counts_fixture.h"

#include "micros/work_observation.h"

static void copy_result(uint8_t value, uint8_t *output)
{
    *output = value;
    MICROS_TEST_WORK_HIT(
        "result-copy",
        MICROS_WORK_OWNER_RUNTIME,
        MICROS_WORK_KIND_COPY_UNIT,
        1
    );
    MICROS_TEST_WORK_HIT(
        "result-byte",
        MICROS_WORK_OWNER_RUNTIME,
        MICROS_WORK_KIND_COPIED_BYTE,
        1
    );
}

bool work_counts_fixture_search(
    const uint8_t *values,
    size_t count,
    uint8_t target,
    uint8_t *output
)
{
    size_t index;
    uint8_t value;

    for (index = 0; index < count; ++index) {
        value = values[index];
        MICROS_TEST_WORK_HIT(
            "record-read",
            MICROS_WORK_OWNER_KERNEL_MEMORY,
            MICROS_WORK_KIND_RECORD_VISIT,
            1
        );
        if (
            MICROS_TEST_WORK_COMPARE(
                "key-comparison",
                MICROS_WORK_OWNER_KERNEL_MEMORY,
                value == target
            )
        ) {
            copy_result(value, output);
            return true;
        }
    }
    return false;
}
