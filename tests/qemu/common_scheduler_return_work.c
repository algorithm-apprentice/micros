#include "tests/qemu/common_scheduler_return_work.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "tests/work/work_counts.h"

struct return_capture {
    struct micros_test_work_snapshot work;
    struct micros_thread_handle outgoing;
    struct micros_thread_handle selected;
    enum micros_obj02_return_mode mode;
    bool present;
};

struct return_observer {
    struct return_capture first[3];
    struct micros_test_work_snapshot completed;
    struct micros_thread_handle outgoing;
    enum micros_obj02_return_mode mode;
    uint64_t label;
    unsigned modes_seen;
    bool active;
    bool invalid;
    bool reported;
};

static struct return_observer observer;

_Static_assert(sizeof(struct return_observer) <= 4 * 4096, "OBJ02 capture storage is bounded");

static bool exact_null(struct micros_thread_handle handle)
{
    return handle.slot == 0 && handle.generation == 0;
}

void micros_obj02_return_begin(
    enum micros_obj02_return_mode mode,
    struct micros_thread_handle outgoing
)
{
    if (
        observer.active || observer.reported || observer.label == UINT64_MAX
        || mode > MICROS_OBJ02_RETURN_IDLE || riscv_irq_is_enabled()
        || (mode == MICROS_OBJ02_RETURN_USER
            ? outgoing.generation == 0 || outgoing.slot >= MICROS_THREAD_CAPACITY
            : !exact_null(outgoing))
    ) {
        observer.invalid = true;
        return;
    }
    ++observer.label;
    if (micros_test_work_begin(
        UINT64_C(0x4f424a3032540001), observer.label,
        UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_EXECUTION
    ) != MICROS_TEST_WORK_OK) {
        observer.invalid = true;
        return;
    }
    observer.mode = mode;
    observer.outgoing = outgoing;
    observer.active = true;
}

void micros_obj02_return_validation(void)
{
    if (!observer.active || riscv_irq_is_enabled()) {
        observer.invalid = true;
        return;
    }
    micros_test_work_record(
        MICROS_WORK_OWNER_KERNEL_EXECUTION,
        MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL, 1
    );
}

static bool finish_capture(void)
{
    if (!observer.active || riscv_irq_is_enabled()) {
        observer.invalid = true;
        return false;
    }
    observer.active = false;
    if (micros_test_work_finish(&observer.completed) != MICROS_TEST_WORK_OK) {
        observer.invalid = true;
        return false;
    }
    return true;
}

void micros_obj02_return_cancel(void)
{
    (void)finish_capture();
}

static void copy_snapshot(
    struct micros_test_work_snapshot *destination,
    const struct micros_test_work_snapshot *source
)
{
    destination->version = source->version;
    destination->reserved0 = source->reserved0;
    destination->producer_label = source->producer_label;
    destination->capture_label = source->capture_label;
    destination->owner_mask = source->owner_mask;
    destination->reserved1 = source->reserved1;
    for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        for (size_t kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            destination->counts[owner][kind] = source->counts[owner][kind];
        }
    }
}

void micros_obj02_return_end(struct micros_thread_handle selected)
{
    size_t index;
    struct return_capture *capture;

    if (!finish_capture()) {
        return;
    }
    if ((selected.generation == 0 && !exact_null(selected))
        || (selected.generation != 0 && selected.slot >= MICROS_THREAD_CAPACITY)) {
        observer.invalid = true;
        return;
    }
    observer.modes_seen |= 1U << observer.mode;
    if (selected.generation == 0 || observer.mode == MICROS_OBJ02_RETURN_START
        || observer.mode == MICROS_OBJ02_RETURN_IDLE) {
        return;
    }
    if (observer.mode == MICROS_OBJ02_RETURN_CAPTURED) {
        index = 2;
    } else {
        index = observer.outgoing.slot == selected.slot
            && observer.outgoing.generation == selected.generation ? 0 : 1;
    }
    capture = &observer.first[index];
    if (capture->present) {
        return;
    }
    copy_snapshot(&capture->work, &observer.completed);
    capture->outgoing = observer.outgoing;
    capture->selected = selected;
    capture->mode = observer.mode;
    capture->present = true;
}

static void report_field(const char *name, uint64_t value)
{
    uart_write(" ");
    uart_write(name);
    uart_write("=");
    uart_write_hex64(value);
}

static void report_capture(size_t index, const struct return_capture *capture)
{
    static const char *const dimensions[] = {
        "comparison", "record", "index", "link", "bitmap", "pte", "queue",
        "validator", "copy", "copied", "clear", "cleared", "range", "hash",
    };

    _Static_assert(
        sizeof(dimensions) / sizeof(dimensions[0]) == MICROS_WORK_KIND_COUNT,
        "OBJ02 reports every shared work dimension"
    );
    uart_write("MICROS_OBJ02_CONTEXT");
    report_field("class", index);
    report_field("version", capture->work.version);
    report_field("reserved0", capture->work.reserved0);
    report_field("producer", capture->work.producer_label);
    report_field("capture", capture->work.capture_label);
    report_field("owner_mask", capture->work.owner_mask);
    report_field("reserved1", capture->work.reserved1);
    report_field("mode", capture->mode);
    report_field("outgoing_slot", capture->outgoing.slot);
    report_field("outgoing_generation", capture->outgoing.generation);
    report_field("selected_slot", capture->selected.slot);
    report_field("selected_generation", capture->selected.generation);
    uart_write("\n");
    for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        uart_write("MICROS_OBJ02_CONTEXT_COUNTS");
        report_field("capture", capture->work.capture_label);
        report_field("owner", owner);
        for (size_t kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            report_field(dimensions[kind], capture->work.counts[owner][kind]);
        }
        uart_write("\n");
    }
}

bool micros_obj02_return_report(void)
{
    bool valid = !observer.invalid && !observer.active && !observer.reported
        && observer.modes_seen == 15;

    observer.reported = true;
    for (size_t index = 0; index < 3; ++index) {
        const struct return_capture *capture = &observer.first[index];

        if (!capture->present) {
            valid = false;
            continue;
        }
        report_capture(index, capture);
        for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
            for (size_t kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
                uint64_t wanted = owner == MICROS_WORK_OWNER_KERNEL_EXECUTION
                    && kind == MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL ? (index == 1 ? 2 : 1) : 0;

                if (capture->work.counts[owner][kind] != wanted) {
                    valid = false;
                }
            }
        }
    }
    uart_write("MICROS_OBJ02_CONTEXT_END");
    report_field("valid", valid);
    report_field("modes", observer.modes_seen);
    report_field("captures", observer.label);
    uart_write("\n");
    return valid;
}
