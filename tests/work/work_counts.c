#include "tests/work/work_counts.h"

#include <stdbool.h>
#include <stdatomic.h>

struct work_collector {
    _Atomic bool active;
    _Atomic uint64_t faults;
    uint64_t producer_label;
    uint64_t capture_label;
    uint64_t last_capture_label;
    uint32_t owner_mask;
    _Atomic uint64_t counts[MICROS_WORK_OWNER_COUNT][MICROS_WORK_KIND_COUNT];
};

static struct work_collector collector;

_Static_assert(
    ATOMIC_LONG_LOCK_FREE == 2 && ATOMIC_LLONG_LOCK_FREE == 2,
    "work counters require lock-free 64-bit atomics"
);
_Static_assert(
    sizeof(struct work_collector) <= 4096,
    "work collectors must fit one page"
);

static void latch_fault(enum micros_test_work_status status)
{
    atomic_fetch_or_explicit(
        &collector.faults,
        UINT64_C(1) << (unsigned)status,
        memory_order_relaxed
    );
}

static void clear_counts(void)
{
    unsigned owner;
    unsigned kind;

    for (owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        for (kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            atomic_store_explicit(
                &collector.counts[owner][kind],
                0,
                memory_order_relaxed
            );
        }
    }
}

enum micros_test_work_status micros_test_work_begin(
    uint64_t producer_label,
    uint64_t capture_label,
    uint32_t owner_mask
)
{
    if (atomic_load_explicit(&collector.active, memory_order_acquire)) {
        latch_fault(MICROS_TEST_WORK_STATE);
        return MICROS_TEST_WORK_STATE;
    }
    if (
        producer_label == 0
        || capture_label == 0
        || capture_label <= collector.last_capture_label
        || owner_mask == 0
        || (owner_mask & ~MICROS_TEST_WORK_OWNER_MASK) != 0
    ) {
        return MICROS_TEST_WORK_ARGUMENT;
    }
    clear_counts();
    atomic_store_explicit(&collector.faults, 0, memory_order_relaxed);
    collector.producer_label = producer_label;
    collector.capture_label = capture_label;
    collector.last_capture_label = capture_label;
    collector.owner_mask = owner_mask;
    atomic_store_explicit(&collector.active, true, memory_order_release);
    return MICROS_TEST_WORK_OK;
}

void micros_test_work_record(
    enum micros_work_owner owner,
    enum micros_work_kind kind,
    uint64_t units
)
{
    uint64_t previous;

    if (!atomic_load_explicit(&collector.active, memory_order_acquire)) {
        return;
    }
    if (
        (unsigned)owner >= MICROS_WORK_OWNER_COUNT
        || (collector.owner_mask & (UINT32_C(1) << (unsigned)owner)) == 0
    ) {
        latch_fault(MICROS_TEST_WORK_OWNER);
        return;
    }
    if ((unsigned)kind >= MICROS_WORK_KIND_COUNT) {
        latch_fault(MICROS_TEST_WORK_KIND);
        return;
    }
    previous = atomic_fetch_add_explicit(
        &collector.counts[owner][kind],
        units,
        memory_order_relaxed
    );
    if (units > UINT64_MAX - previous) {
        latch_fault(MICROS_TEST_WORK_OVERFLOW);
    }
}

static enum micros_test_work_status capture_status(void)
{
    uint64_t faults = atomic_load_explicit(
        &collector.faults,
        memory_order_acquire
    );
    unsigned status;

    for (
        status = MICROS_TEST_WORK_STATE;
        status <= MICROS_TEST_WORK_OVERFLOW;
        ++status
    ) {
        if ((faults & (UINT64_C(1) << status)) != 0) {
            return (enum micros_test_work_status)status;
        }
    }
    return MICROS_TEST_WORK_OK;
}

static void publish_snapshot(struct micros_test_work_snapshot *output)
{
    unsigned owner;
    unsigned kind;

    output->version = 1;
    output->reserved0 = 0;
    output->producer_label = collector.producer_label;
    output->capture_label = collector.capture_label;
    output->owner_mask = collector.owner_mask;
    output->reserved1 = 0;
    for (owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        for (kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            output->counts[owner][kind] = atomic_load_explicit(
                &collector.counts[owner][kind],
                memory_order_relaxed
            );
        }
    }
}

enum micros_test_work_status micros_test_work_finish(
    struct micros_test_work_snapshot *output
)
{
    enum micros_test_work_status status;

    if (output == NULL) {
        atomic_store_explicit(&collector.active, false, memory_order_release);
        return MICROS_TEST_WORK_ARGUMENT;
    }
    if (!atomic_load_explicit(&collector.active, memory_order_acquire)) {
        return MICROS_TEST_WORK_STATE;
    }
    atomic_store_explicit(&collector.active, false, memory_order_release);
    status = capture_status();
    if (status != MICROS_TEST_WORK_OK) {
        return status;
    }
    publish_snapshot(output);
    return MICROS_TEST_WORK_OK;
}
