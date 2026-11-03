#include "diagnostic_history_test.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "kernel/diagnostic/diagnostic_history_core.h"
#include "tests/work/work_counts.h"

_Static_assert(sizeof(struct micros_diagnostic_history) == 12012, "exact history storage");
_Static_assert(
    sizeof(((struct micros_diagnostic_history *)0)->ring) == 10000,
    "exact circular history"
);
_Static_assert(
    sizeof(((struct micros_diagnostic_history *)0)->tail) == 2000,
    "exact rolling tail"
);
_Static_assert(
    _Generic(((struct micros_diagnostic_history *)0)->next, uint32_t: 1, default: 0)
    && _Generic(((struct micros_diagnostic_history *)0)->size, uint32_t: 1, default: 0)
    && _Generic(((struct micros_diagnostic_history *)0)->tail_position, uint32_t: 1, default: 0),
    "three unsigned 32-bit history scalars"
);

static bool matches_stream(
    const struct micros_diagnostic_history *history,
    const uint8_t *stream,
    uint32_t count
)
{
    uint32_t retained = count < 10000 ? count : 10000;
    uint32_t tail = count < 1998 ? count : 1998;
    uint32_t index;

    DIAGNOSTIC_EXPECT(micros_diagnostic_history_valid(history));
    DIAGNOSTIC_EXPECT(history->next == count % 10000);
    DIAGNOSTIC_EXPECT(history->size == retained);
    DIAGNOSTIC_EXPECT(history->tail_position == tail);
    for (index = 0; index < 10000; ++index) {
        uint8_t expected = 0;

        if (index < count) {
            uint32_t position = index + ((count - 1 - index) / 10000) * 10000;

            expected = stream[position];
        }
        DIAGNOSTIC_EXPECT(history->ring[index] == expected);
    }
    for (index = 0; index < 2000; ++index) {
        uint8_t expected = index < tail ? stream[count - tail + index] : 0;

        DIAGNOSTIC_EXPECT(history->tail[index] == expected);
    }
    return true;
}

static bool complete_sequence_and_work(void)
{
    struct micros_diagnostic_history history = {0};
    struct micros_test_work_snapshot snapshot;
    uint8_t stream[10003];
    uint32_t count;
    uint64_t expected;
    unsigned owner;
    unsigned kind;

    DIAGNOSTIC_EXPECT(matches_stream(&history, stream, 0));
    DIAGNOSTIC_EXPECT(micros_test_work_begin(
        1, 1, UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC
    ) == MICROS_TEST_WORK_OK);
    for (count = 0; count < sizeof(stream); ++count) {
        stream[count] = (uint8_t)(1 + (count * 37) % 255);
        DIAGNOSTIC_EXPECT(micros_diagnostic_append(&history, stream[count]));
        if (!matches_stream(&history, stream, count + 1)) {
            fprintf(stderr, "diagnostic stream position=%" PRIu32 "\n", count);
            return false;
        }
    }
    DIAGNOSTIC_EXPECT(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
    expected = UINT64_C(2) * sizeof(stream)
        + UINT64_C(1999) * (sizeof(stream) - 1998);
    for (owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        for (kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            uint64_t value = (
                owner == MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC
                && (kind == MICROS_WORK_KIND_COPY_UNIT || kind == MICROS_WORK_KIND_COPIED_BYTE)
            ) ? expected : 0;

            DIAGNOSTIC_EXPECT(snapshot.counts[owner][kind] == value);
        }
    }
    return true;
}

static bool zero_and_invalid_metadata_preserve_state(void)
{
    struct micros_diagnostic_history history = {0};
    struct micros_diagnostic_history before;
    struct micros_test_work_snapshot snapshot;
    unsigned mutation;
    unsigned owner;
    unsigned kind;

    DIAGNOSTIC_EXPECT(micros_test_work_begin(
        1, 2, UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC
    ) == MICROS_TEST_WORK_OK);
    before = history;
    DIAGNOSTIC_EXPECT(!micros_diagnostic_append(&history, 0));
    DIAGNOSTIC_EXPECT(memcmp(&history, &before, sizeof(history)) == 0);
    for (mutation = 0; mutation < 6; ++mutation) {
        memset(&history, 0, sizeof(history));
        switch (mutation) {
        case 0: history.next = 10000; break;
        case 1: history.size = 10001; break;
        case 2: history.tail_position = 1999; break;
        case 3: history.next = 1; break;
        case 4: history.tail[0] = 1; break;
        default: history.tail[1999] = 1; break;
        }
        before = history;
        DIAGNOSTIC_EXPECT(!micros_diagnostic_history_valid(&history));
        DIAGNOSTIC_EXPECT(!micros_diagnostic_append(&history, 65));
        DIAGNOSTIC_EXPECT(memcmp(&history, &before, sizeof(history)) == 0);
    }
    DIAGNOSTIC_EXPECT(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
    for (owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        for (kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            DIAGNOSTIC_EXPECT(snapshot.counts[owner][kind] == 0);
        }
    }
    return true;
}

static bool sampled_spans_and_complete_laps(void)
{
    struct micros_diagnostic_history history = {0};
    struct micros_diagnostic_span span;
    struct micros_diagnostic_span before;
    uint32_t index;

    DIAGNOSTIC_EXPECT(micros_diagnostic_span(&history, 0, &span));
    DIAGNOSTIC_EXPECT(span.next == 0 && span.length == 0);
    memset(&span, 0xa5, sizeof(span));
    before = span;
    DIAGNOSTIC_EXPECT(!micros_diagnostic_span(&history, 1, &span));
    DIAGNOSTIC_EXPECT(memcmp(&span, &before, sizeof(span)) == 0);
    for (index = 0; index < 10000; ++index) {
        DIAGNOSTIC_EXPECT(micros_diagnostic_append(&history, (uint8_t)'x'));
    }
    DIAGNOSTIC_EXPECT(micros_diagnostic_span(&history, 0, &span));
    DIAGNOSTIC_EXPECT(span.next == 0 && span.length == 0);
    DIAGNOSTIC_EXPECT(micros_diagnostic_append(&history, (uint8_t)'y'));
    DIAGNOSTIC_EXPECT(micros_diagnostic_span(&history, 9998, &span));
    DIAGNOSTIC_EXPECT(
        span.next == 1 && span.length == 3
        && span.first_start == 9998 && span.first_length == 2
    );
    before = span;
    DIAGNOSTIC_EXPECT(!micros_diagnostic_span(&history, 10000, &span));
    DIAGNOSTIC_EXPECT(memcmp(&span, &before, sizeof(span)) == 0);
    return true;
}

int main(void)
{
    if (
        !complete_sequence_and_work()
        || !zero_and_invalid_metadata_preserve_state()
        || !sampled_spans_and_complete_laps()
        || !diagnostic_control_cases()
        || !tty_diagnostic_cases()
    ) {
        return 1;
    }
    puts("diagnostic histories, exact work, reader and consumer cases passed");
    return 0;
}
