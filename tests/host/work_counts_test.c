#include "tests/work/work_counts.h"
#include "tests/host/work_counts_fixture.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static struct micros_test_work_snapshot snapshot;
static struct micros_test_work_snapshot before;
static uint64_t capture_label;
static unsigned evaluations;

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%u: %s\n", __FILE__, \
                (unsigned)__LINE__, #condition); \
            return false; \
        } \
    } while (0)

static uint32_t runtime_mask(void)
{
    return UINT32_C(1) << MICROS_WORK_OWNER_RUNTIME;
}

static enum micros_test_work_status begin(uint32_t mask)
{
    return micros_test_work_begin(1, ++capture_label, mask);
}

static bool failure_preserves_output(enum micros_test_work_status expected)
{
    memset(&snapshot, 0xa5, sizeof(snapshot));
    memcpy(&before, &snapshot, sizeof(before));
    REQUIRE(micros_test_work_finish(&snapshot) == expected);
    REQUIRE(memcmp(&before, &snapshot, sizeof(snapshot)) == 0);
    return true;
}

static bool records_primitive_work(void)
{
    enum micros_test_work_status status = begin(runtime_mask());
    unsigned owner;
    unsigned kind;

    if (status != MICROS_TEST_WORK_OK) {
        fprintf(stderr, "valid capture begin returned %u\n", (unsigned)status);
        return false;
    }
    micros_test_work_record(
        MICROS_WORK_OWNER_RUNTIME,
        MICROS_WORK_KIND_RECORD_VISIT,
        1
    );
    micros_test_work_record(
        MICROS_WORK_OWNER_RUNTIME,
        MICROS_WORK_KIND_RECORD_VISIT,
        1
    );
    status = micros_test_work_finish(&snapshot);
    if (
        status != MICROS_TEST_WORK_OK
        || snapshot.counts[MICROS_WORK_OWNER_RUNTIME]
            [MICROS_WORK_KIND_RECORD_VISIT] != 2
    ) {
        fprintf(stderr, "two primitive visits were not captured\n");
        return false;
    }
    REQUIRE(snapshot.version == 1);
    REQUIRE(snapshot.reserved0 == 0 && snapshot.reserved1 == 0);
    REQUIRE(snapshot.producer_label == 1);
    REQUIRE(snapshot.capture_label == capture_label);
    REQUIRE(snapshot.owner_mask == runtime_mask());
    for (owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        for (kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            uint64_t expected = (
                owner == MICROS_WORK_OWNER_RUNTIME
                && kind == MICROS_WORK_KIND_RECORD_VISIT
            ) ? 2 : 0;

            REQUIRE(snapshot.counts[owner][kind] == expected);
        }
    }
    return true;
}

static bool invalid_begin_does_not_consume_capture(void)
{
    uint64_t candidate = ++capture_label;

    REQUIRE(micros_test_work_begin(0, candidate, runtime_mask())
        == MICROS_TEST_WORK_ARGUMENT);
    REQUIRE(micros_test_work_begin(1, 0, runtime_mask())
        == MICROS_TEST_WORK_ARGUMENT);
    REQUIRE(micros_test_work_begin(1, candidate, 0)
        == MICROS_TEST_WORK_ARGUMENT);
    REQUIRE(micros_test_work_begin(1, candidate, UINT32_MAX)
        == MICROS_TEST_WORK_ARGUMENT);
    REQUIRE(micros_test_work_begin(1, candidate, runtime_mask())
        == MICROS_TEST_WORK_OK);
    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
    return true;
}

static bool overlap_invalidates_capture(void)
{
    REQUIRE(begin(runtime_mask()) == MICROS_TEST_WORK_OK);
    REQUIRE(micros_test_work_begin(2, capture_label + 1, runtime_mask())
        == MICROS_TEST_WORK_STATE);
    REQUIRE(failure_preserves_output(MICROS_TEST_WORK_STATE));
    REQUIRE(begin(runtime_mask()) == MICROS_TEST_WORK_OK);
    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
    return true;
}

static bool unknown_dimensions_fail_without_indexing(void)
{
    REQUIRE(begin(runtime_mask()) == MICROS_TEST_WORK_OK);
    micros_test_work_record(
        (enum micros_work_owner)-1,
        MICROS_WORK_KIND_RECORD_VISIT,
        0
    );
    micros_test_work_record(
        MICROS_WORK_OWNER_RUNTIME,
        (enum micros_work_kind)-1,
        0
    );
    REQUIRE(failure_preserves_output(MICROS_TEST_WORK_OWNER));
    REQUIRE(begin(runtime_mask()) == MICROS_TEST_WORK_OK);
    micros_test_work_record(
        MICROS_WORK_OWNER_RUNTIME,
        MICROS_WORK_KIND_COUNT,
        0
    );
    REQUIRE(failure_preserves_output(MICROS_TEST_WORK_KIND));
    REQUIRE(begin(runtime_mask()) == MICROS_TEST_WORK_OK);
    micros_test_work_record(
        MICROS_WORK_OWNER_KERNEL_MEMORY,
        MICROS_WORK_KIND_RECORD_VISIT,
        1
    );
    REQUIRE(failure_preserves_output(MICROS_TEST_WORK_OWNER));
    return true;
}

static bool overflow_never_exports_wrapped_counts(void)
{
    REQUIRE(begin(runtime_mask()) == MICROS_TEST_WORK_OK);
    micros_test_work_record(
        MICROS_WORK_OWNER_RUNTIME,
        MICROS_WORK_KIND_COPIED_BYTE,
        UINT64_MAX
    );
    micros_test_work_record(
        MICROS_WORK_OWNER_RUNTIME,
        MICROS_WORK_KIND_COPIED_BYTE,
        1
    );
    REQUIRE(failure_preserves_output(MICROS_TEST_WORK_OVERFLOW));
    REQUIRE(begin(runtime_mask()) == MICROS_TEST_WORK_OK);
    micros_test_work_record(
        MICROS_WORK_OWNER_RUNTIME,
        MICROS_WORK_KIND_COPIED_BYTE,
        UINT64_MAX
    );
    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
    REQUIRE(snapshot.counts[MICROS_WORK_OWNER_RUNTIME]
        [MICROS_WORK_KIND_COPIED_BYTE] == UINT64_MAX);
    return true;
}

static bool inactive_and_null_finish_do_not_publish(void)
{
    micros_test_work_record(
        MICROS_WORK_OWNER_COUNT,
        MICROS_WORK_KIND_COUNT,
        UINT64_MAX
    );
    REQUIRE(failure_preserves_output(MICROS_TEST_WORK_STATE));
    REQUIRE(begin(runtime_mask()) == MICROS_TEST_WORK_OK);
    REQUIRE(micros_test_work_finish(NULL) == MICROS_TEST_WORK_ARGUMENT);
    REQUIRE(failure_preserves_output(MICROS_TEST_WORK_STATE));
    REQUIRE(begin(runtime_mask()) == MICROS_TEST_WORK_OK);
    micros_test_work_record(
        MICROS_WORK_OWNER_RUNTIME,
        MICROS_WORK_KIND_COPIED_BYTE,
        0
    );
    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
    REQUIRE(snapshot.counts[MICROS_WORK_OWNER_RUNTIME]
        [MICROS_WORK_KIND_COPIED_BYTE] == 0);
    return true;
}

static int next_value(void)
{
    ++evaluations;
    return (int)evaluations;
}

static bool comparison_evaluates_once(void)
{
    evaluations = 0;
    REQUIRE(begin(runtime_mask()) == MICROS_TEST_WORK_OK);
    REQUIRE(MICROS_TEST_WORK_COMPARE(
        "single-evaluation",
        MICROS_WORK_OWNER_RUNTIME,
        next_value() == 1
    ));
    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
    REQUIRE(evaluations == 1);
    REQUIRE(snapshot.counts[MICROS_WORK_OWNER_RUNTIME]
        [MICROS_WORK_KIND_COMPARISON] == 1);
    return true;
}

static bool fixture_case(size_t count, unsigned order, unsigned selection)
{
    uint8_t values[65];
    uint8_t output = 0xa5;
    size_t position = selection == 0 ? 0 :
        (selection == 1 ? count / 2 : (count == 0 ? 0 : count - 1));
    bool found = count != 0 && selection != 3;
    uint8_t target = found
        ? (order == 2 ? 7 : (uint8_t)(
            order == 0 ? position + 1 : count - position
        ))
        : 0;
    uint64_t visits = found
        ? (order == 2 ? 1 : position + 1)
        : count;
    size_t index;
    uint32_t mask = runtime_mask()
        | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_MEMORY);

    for (index = 0; index < count; ++index) {
        values[index] = order == 2 ? 7 :
            (uint8_t)(order == 0 ? index + 1 : count - index);
    }
    REQUIRE(begin(mask) == MICROS_TEST_WORK_OK);
    REQUIRE(work_counts_fixture_search(values, count, target, &output) == found);
    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
    REQUIRE(output == (found ? target : 0xa5));
    REQUIRE(snapshot.counts[MICROS_WORK_OWNER_KERNEL_MEMORY]
        [MICROS_WORK_KIND_RECORD_VISIT] == visits);
    REQUIRE(snapshot.counts[MICROS_WORK_OWNER_KERNEL_MEMORY]
        [MICROS_WORK_KIND_COMPARISON] == visits);
    REQUIRE(snapshot.counts[MICROS_WORK_OWNER_RUNTIME]
        [MICROS_WORK_KIND_COPY_UNIT] == (uint64_t)found);
    REQUIRE(snapshot.counts[MICROS_WORK_OWNER_RUNTIME]
        [MICROS_WORK_KIND_COPIED_BYTE] == (uint64_t)found);
    return true;
}

static bool boundary_and_adversarial_fixtures(void)
{
    static const size_t counts[] = {0, 1, 63, 64, 65};
    size_t index;
    unsigned order;
    unsigned selection;

    for (index = 0; index < sizeof(counts) / sizeof(counts[0]); ++index) {
        for (order = 0; order < 3; ++order) {
            for (selection = 0; selection < 4; ++selection) {
                REQUIRE(fixture_case(counts[index], order, selection));
            }
        }
    }
    return true;
}

static bool stale_and_exhausted_captures_fail(void)
{
    REQUIRE(micros_test_work_begin(1, capture_label, runtime_mask())
        == MICROS_TEST_WORK_ARGUMENT);
    REQUIRE(failure_preserves_output(MICROS_TEST_WORK_STATE));
    REQUIRE(micros_test_work_begin(2, UINT64_MAX, runtime_mask())
        == MICROS_TEST_WORK_OK);
    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
    REQUIRE(snapshot.producer_label == 2);
    REQUIRE(snapshot.capture_label == UINT64_MAX);
    REQUIRE(micros_test_work_begin(2, UINT64_MAX, runtime_mask())
        == MICROS_TEST_WORK_ARGUMENT);
    REQUIRE(micros_test_work_begin(2, 0, runtime_mask())
        == MICROS_TEST_WORK_ARGUMENT);
    REQUIRE(failure_preserves_output(MICROS_TEST_WORK_STATE));
    return true;
}

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"records primitive work", records_primitive_work},
        {"invalid begin preserves labels", invalid_begin_does_not_consume_capture},
        {"overlap invalidates capture", overlap_invalidates_capture},
        {"unknown dimensions fail", unknown_dimensions_fail_without_indexing},
        {"overflow cannot export", overflow_never_exports_wrapped_counts},
        {"inactive/null finish", inactive_and_null_finish_do_not_publish},
        {"single comparison evaluation", comparison_evaluates_once},
        {"boundary/adversarial counts", boundary_and_adversarial_fixtures},
        {"stale/exhausted capture labels", stale_and_exhausted_captures_fail},
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(stderr, "not ok %zu - %s\n", index + 1, tests[index].name);
            return 1;
        }
        printf("ok %zu - %s\n", index + 1, tests[index].name);
    }
    return 0;
}
