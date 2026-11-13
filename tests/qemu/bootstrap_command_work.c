#include "tests/qemu/bootstrap_command_work.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/boot/bootstrap_runtime.h"
#include "tests/work/work_counts.h"

enum {
    BOOT02_CAPTURE_CAPACITY = 14,
};

struct boot02_capture {
    struct micros_test_work_snapshot work;
    struct micros_thread_handle thread;
    uint64_t command;
    uint64_t service_id;
    uint64_t result;
    uint32_t role;
    enum micros_syscall_return return_kind;
};

static struct {
    struct boot02_capture captures[BOOT02_CAPTURE_CAPACITY];
    struct boot02_capture current;
    uint64_t label;
    uint64_t audits;
    uint64_t rejected;
    size_t count;
    bool active;
    bool invalid;
    bool reported;
} observer;

_Static_assert(sizeof(observer) <= 12 * 4096,
               "Bootstrap target work has bounded test-owned storage");

static uint64_t producer(void)
{
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    return UINT64_C(0x424f4f5430325402);
#else
    return UINT64_C(0x424f4f5430325401);
#endif
}

static void begin_capture(
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    const struct micros_bootstrap_control_state *state = micros_bootstrap_runtime_state();
    const struct micros_bootstrap_binding *binding;

    if (observer.active || observer.reported || observer.label == UINT64_MAX
        || context->current.generation == 0 || context->current.slot >= MICROS_THREAD_CAPACITY
        || riscv_irq_is_enabled()) {
        observer.invalid = true;
        return;
    }
    observer.current.command = arguments->a0;
    observer.current.service_id = arguments->a1;
    observer.current.thread = context->current;
    observer.current.role = 0;
    if (state != NULL && arguments->a1 <= MICROS_BOOTSTRAP_SERVICE_ID_LIMIT) {
        binding = micros_bootstrap_control_find_binding(state, (uint32_t)arguments->a1);
        if (binding != NULL && binding->manifest_index < state->entry_count) {
            observer.current.role = state->manifest.entries[binding->manifest_index].role_flags;
        }
    }
    ++observer.label;
    if (micros_test_work_begin(
        producer(), observer.label,
        (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_MEMORY)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_OBJECTS)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_SCHEDULER)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_ENDPOINT)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_IPC)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_GRANT)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_EXECUTION)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_BOOT)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_VM)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_TTY)
    ) != MICROS_TEST_WORK_OK) {
        observer.invalid = true;
        return;
    }
    observer.active = true;
}

static void finish_capture(enum micros_syscall_return kind, uint64_t result)
{
    if (!observer.active || riscv_irq_is_enabled()) {
        observer.invalid = true;
        return;
    }
    observer.active = false;
    if (micros_test_work_finish(&observer.current.work) != MICROS_TEST_WORK_OK) {
        observer.invalid = true;
        return;
    }
    for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        uint64_t count = observer.current.work.counts[owner][MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL];

        if (UINT64_MAX - observer.audits < count) {
            observer.invalid = true;
            return;
        }
        observer.audits += count;
    }
    if (kind == MICROS_SYSCALL_RETURN_NORMAL && result != MICROS_SYSCALL_ABI_OK) {
        if (observer.rejected == UINT64_MAX) {
            observer.invalid = true;
        } else {
            ++observer.rejected;
        }
        return;
    }
    if (observer.count == BOOT02_CAPTURE_CAPACITY) {
        observer.invalid = true;
        return;
    }
    {
        struct boot02_capture *capture = &observer.captures[observer.count++];

        capture->thread = observer.current.thread;
        capture->command = observer.current.command;
        capture->service_id = observer.current.service_id;
        capture->role = observer.current.role;
        capture->result = result;
        capture->return_kind = kind;
        capture->work.version = observer.current.work.version;
        capture->work.reserved0 = observer.current.work.reserved0;
        capture->work.producer_label = observer.current.work.producer_label;
        capture->work.capture_label = observer.current.work.capture_label;
        capture->work.owner_mask = observer.current.work.owner_mask;
        capture->work.reserved1 = observer.current.work.reserved1;
        for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
            for (size_t dimension = 0; dimension < MICROS_WORK_KIND_COUNT; ++dimension) {
                capture->work.counts[owner][dimension] = observer.current.work.counts[owner][dimension];
            }
        }
    }
}

enum micros_syscall_return micros_boot02_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    enum micros_syscall_return result;

    begin_capture(context, arguments);
    result = micros_bootstrap_handle_captured_user_ecall(hart, frame, context, arguments);
    finish_capture(result, frame->a0);
    return result;
}

static void field(const char *name, uint64_t value)
{
    uart_write(" ");
    uart_write(name);
    uart_write("=");
    uart_write_hex64(value);
}

static void report_capture(size_t index, const struct boot02_capture *capture)
{
    static const char *const dimensions[] = {
        "comparison", "record", "index", "link", "bitmap", "pte", "queue",
        "validator", "copy", "copied", "clear", "cleared", "range", "hash",
    };

    _Static_assert(sizeof(dimensions) / sizeof(dimensions[0]) == MICROS_WORK_KIND_COUNT,
                   "Bootstrap publishes every shared work dimension");
    uart_write("MICROS_BOOT02_CAPTURE");
    field("index", index);
    field("version", capture->work.version);
    field("reserved0", capture->work.reserved0);
    field("producer", capture->work.producer_label);
    field("capture", capture->work.capture_label);
    field("owner_mask", capture->work.owner_mask);
    field("reserved1", capture->work.reserved1);
    field("command", capture->command);
    field("service", capture->service_id);
    field("role", capture->role);
    field("thread_slot", capture->thread.slot);
    field("thread_generation", capture->thread.generation);
    field("return_kind", capture->return_kind);
    field("result", capture->result);
    uart_write("\n");
    for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        uart_write("MICROS_BOOT02_CAPTURE_COUNTS");
        field("capture", capture->work.capture_label);
        field("owner", owner);
        for (size_t kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            field(dimensions[kind], capture->work.counts[owner][kind]);
        }
        uart_write("\n");
    }
}

bool micros_boot02_work_report(void)
{
    bool complete = !observer.invalid && !observer.active && !observer.reported
        && observer.count != 0 && !riscv_irq_is_enabled();

    observer.reported = true;
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    /* The original semantic and physical-drain checks precede this terminal report. */
    if (uart_panic_seize()) {
        complete = false;
    }
#endif
    for (size_t index = 0; index < observer.count; ++index) {
        report_capture(index, &observer.captures[index]);
    }
    uart_write("MICROS_BOOT02_CAPTURE_END");
    field("complete", complete);
    field("captures", observer.count);
    field("attempts", observer.label);
    field("rejected", observer.rejected);
    field("audits", observer.audits);
    uart_write("\n");
    uart_flush();
    return complete;
}
