#include "tests/qemu/selected_ipc_work.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "tests/work/work_counts.h"

enum {
    IPC01_SYSCALL_SCOPE = 0,
    IPC01_COMPLETION_SCOPE,
    IPC01_CAPTURE_CLASSES = 10,
};

struct ipc01_capture {
    struct micros_test_work_snapshot work;
    struct micros_thread_handle thread;
    uint64_t operation;
    uint64_t result;
    uint64_t token;
    size_t chunks;
    unsigned kind;
    bool present;
};

struct ipc01_observer {
    struct ipc01_capture first[IPC01_CAPTURE_CLASSES];
    struct micros_test_work_snapshot completed;
    struct micros_thread_handle thread;
    uint64_t operation;
    uint64_t label;
    uint64_t audits;
    uint64_t token_completions;
    unsigned kind;
    unsigned syscall_mask;
    unsigned completion_mask;
    bool active;
    bool invalid;
    bool reported;
};

static struct ipc01_observer observer;

_Static_assert(sizeof(observer) <= 12 * 4096, "IPC01 target snapshots have bounded test-owned storage");

static void begin_scope(unsigned kind, uint64_t operation, struct micros_thread_handle thread)
{
    if (observer.active || observer.reported || observer.label == UINT64_MAX
        || thread.generation == 0 || thread.slot >= MICROS_THREAD_CAPACITY
        || riscv_irq_is_enabled()) {
        observer.invalid = true;
        return;
    }
    ++observer.label;
    if (micros_test_work_begin(
        UINT64_C(0x4950433031540001), observer.label,
        (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_OBJECTS)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_SCHEDULER)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_ENDPOINT)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_IPC)
    ) != MICROS_TEST_WORK_OK) {
        observer.invalid = true;
        return;
    }
    observer.kind = kind;
    observer.operation = operation;
    observer.thread = thread;
    observer.active = true;
}

void micros_ipc01_syscall_begin(uint64_t operation, struct micros_thread_handle thread)
{
    begin_scope(IPC01_SYSCALL_SCOPE, operation, thread);
}

void micros_ipc01_completion_begin(struct micros_thread_handle thread)
{
    begin_scope(IPC01_COMPLETION_SCOPE, 0, thread);
}

static bool finish_scope(unsigned kind)
{
    if (!observer.active || observer.kind != kind || riscv_irq_is_enabled()) {
        observer.invalid = true;
        return false;
    }
    observer.active = false;
    if (micros_test_work_finish(&observer.completed) != MICROS_TEST_WORK_OK) {
        observer.invalid = true;
        return false;
    }
    for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        uint64_t audits = observer.completed.counts[owner][MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL];

        if (UINT64_MAX - observer.audits < audits) {
            observer.invalid = true;
            return false;
        }
        observer.audits += audits;
    }
    return true;
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

static void retain_capture(size_t index, uint64_t result, size_t chunks, uint64_t token)
{
    struct ipc01_capture *capture = &observer.first[index];

    if (capture->present) {
        return;
    }
    copy_snapshot(&capture->work, &observer.completed);
    capture->thread = observer.thread;
    capture->operation = observer.operation;
    capture->kind = observer.kind;
    capture->result = result;
    capture->chunks = chunks;
    capture->token = token;
    capture->present = true;
}

void micros_ipc01_syscall_end(bool accepted)
{
    if (!finish_scope(IPC01_SYSCALL_SCOPE) || !accepted) {
        return;
    }
    if (observer.operation < 1 || observer.operation > 6) {
        observer.invalid = true;
        return;
    }
    observer.syscall_mask |= 1U << (observer.operation - 1);
    retain_capture((size_t)observer.operation - 1, 0, 0, 0);
}

void micros_ipc01_completion_end(bool accepted, uint64_t result, size_t chunks, uint64_t token)
{
    size_t index;

    if (!finish_scope(IPC01_COMPLETION_SCOPE) || !accepted) {
        return;
    }
    if (chunks == 0 && token == 0 && (result == 0 || result == UINT64_MAX - 1)) {
        index = result == 0 ? 0 : 1;
    } else if (result == 0 && (chunks == 1 || chunks == 2)) {
        index = chunks + 1;
    } else {
        observer.invalid = true;
        return;
    }
    if (token != 0) {
        if (observer.token_completions == UINT64_MAX) {
            observer.invalid = true;
            return;
        }
        ++observer.token_completions;
    }
    observer.completion_mask |= 1U << index;
    retain_capture(index + 6, result, chunks, token);
}

static void report_field(const char *name, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char text[19];
    size_t position = sizeof(text) - 1;

    text[position] = '\0';
    for (size_t digit = 0; digit < 16; ++digit) {
        text[--position] = digits[value & UINT64_C(15)];
        value >>= 4;
        if (value == 0) {
            break;
        }
    }
    text[--position] = 'x';
    text[--position] = '0';
    uart_write(" ");
    uart_write(name);
    uart_write("=");
    uart_write(&text[position]);
}

static void report_capture(size_t index, const struct ipc01_capture *capture)
{
    static const char *const dimensions[] = {
        "comparison", "record", "index", "link", "bitmap", "pte", "queue",
        "validator", "copy", "copied", "clear", "cleared", "range", "hash",
    };

    _Static_assert(sizeof(dimensions) / sizeof(dimensions[0]) == MICROS_WORK_KIND_COUNT,
                   "IPC01 publishes every shared work dimension");
    uart_write("MICROS_IPC01_CAPTURE");
    report_field("class", index);
    report_field("version", capture->work.version);
    report_field("reserved0", capture->work.reserved0);
    report_field("producer", capture->work.producer_label);
    report_field("capture", capture->work.capture_label);
    report_field("owner_mask", capture->work.owner_mask);
    report_field("reserved1", capture->work.reserved1);
    report_field("kind", capture->kind);
    report_field("operation", capture->operation);
    report_field("thread_slot", capture->thread.slot);
    report_field("thread_generation", capture->thread.generation);
    report_field("result", capture->result);
    report_field("chunks", capture->chunks);
    report_field("token", capture->token);
    uart_write("\n");
    for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        uart_write("MICROS_IPC01_CAPTURE_COUNTS");
        report_field("capture", capture->work.capture_label);
        report_field("owner", owner);
        for (size_t kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            report_field(dimensions[kind], capture->work.counts[owner][kind]);
        }
        uart_write("\n");
    }
}

bool micros_ipc01_work_report(void)
{
    bool complete = !observer.invalid && !observer.active && !observer.reported
        && observer.syscall_mask == 63 && observer.completion_mask == 15
        && observer.token_completions != 0;

    observer.reported = true;
    for (size_t index = 0; index < IPC01_CAPTURE_CLASSES; ++index) {
        if (!observer.first[index].present) {
            complete = false;
        } else {
            report_capture(index, &observer.first[index]);
        }
    }
    uart_write("MICROS_IPC01_CAPTURE_END");
    report_field("complete", complete);
    report_field("syscalls", observer.syscall_mask);
    report_field("completions", observer.completion_mask);
    report_field("captures", observer.label);
    report_field("audits", observer.audits);
    report_field("tokens", observer.token_completions);
    uart_write("\n");
    return complete;
}
