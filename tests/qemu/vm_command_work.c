#include "tests/qemu/vm_command_work.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/tests/spawn/elf_spawn_test_internal.h"
#include "tests/qemu/vm_server_internal.h"

static struct {
    struct micros_vm01_work_capture kernel;
    struct micros_vm01_work_capture service;
    struct micros_thread_handle thread;
    bool reported;
} observer;

_Static_assert(sizeof(observer) <= 3 * 4096,
               "VM kernel captures plus the existing collector fit four pages");

bool micros_vm01_work_active(void)
{
    return observer.kernel.active == 1;
}

enum micros_syscall_return micros_vm01_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    struct micros_vm01_work_capture *capture = &observer.kernel;
    enum micros_syscall_return result;
    uint64_t command = arguments->a0;

    if (command < MICROS_VM_LOAD_BEGIN || command > MICROS_VM_EXIT_RELEASE) {
        return micros_vm_handoff_handle_captured_user_ecall(hart, frame, context, arguments);
    }
    if (capture->active || capture->complete || capture->invalid || observer.reported
        || riscv_irq_is_enabled() || context->current.generation == 0) {
        capture->invalid = true;
    } else if (!capture->started) {
        capture->started = true;
        observer.thread = context->current;
        capture->invalid = micros_test_work_begin(
            MICROS_VM01_KERNEL_PRODUCER, 1, MICROS_VM01_KERNEL_OWNER_MASK
        ) != MICROS_TEST_WORK_OK;
    } else if (observer.thread.slot != context->current.slot
               || observer.thread.generation != context->current.generation) {
        capture->invalid = true;
    }
    if (capture->attempts == UINT64_MAX
        || capture->commands[command - MICROS_VM_LOAD_BEGIN] == UINT64_MAX) {
        capture->invalid = true;
    } else {
        ++capture->attempts;
        ++capture->commands[command - MICROS_VM_LOAD_BEGIN];
    }
    capture->active = true;
    result = micros_vm_handoff_handle_captured_user_ecall(hart, frame, context, arguments);
    capture->active = false;
    if (result != MICROS_SYSCALL_RETURN_NORMAL || frame->a0 != MICROS_SYSCALL_ABI_OK
        || riscv_irq_is_enabled()) {
        capture->invalid = true;
    }
    return result;
}

bool micros_vm01_work_finish(void)
{
    struct micros_vm01_work_capture *capture = &observer.kernel;

    if (!capture->started || capture->active || capture->complete || capture->invalid
        || observer.reported || riscv_irq_is_enabled()
        || micros_test_work_finish(&capture->work) != MICROS_TEST_WORK_OK
        || !elf_spawn_test_read_vm_work(&observer.service)) {
        capture->invalid = true;
        return false;
    }
    capture->complete = 1;
    return observer.service.complete == 1 && observer.service.reserved == 0
        && observer.service.started == 1 && observer.service.active == 0 && observer.service.invalid == 0
        && observer.service.work.version == 1
        && observer.service.work.reserved0 == 0 && observer.service.work.reserved1 == 0
        && observer.service.work.producer_label == MICROS_VM01_SERVICE_PRODUCER
        && observer.service.work.capture_label == 1
        && observer.service.work.owner_mask == MICROS_VM01_SERVICE_OWNER_MASK;
}

static void field(const char *name, uint64_t value)
{
    uart_write(" ");
    uart_write(name);
    uart_write("=");
    uart_write_hex64(value);
}

static void report_capture(const struct micros_vm01_work_capture *capture)
{
    static const char *const dimensions[] = {
        "comparison", "record", "index", "link", "bitmap", "pte", "queue",
        "validator", "copy", "copied", "clear", "cleared", "range", "hash",
    };

    _Static_assert(sizeof(dimensions) / sizeof(dimensions[0]) == MICROS_WORK_KIND_COUNT,
                   "VM publishes every shared work dimension");
    uart_write("MICROS_VM01_CAPTURE");
    field("version", capture->work.version);
    field("reserved0", capture->work.reserved0);
    field("producer", capture->work.producer_label);
    field("capture", capture->work.capture_label);
    field("owner_mask", capture->work.owner_mask);
    field("reserved1", capture->work.reserved1);
    field("attempts", capture->attempts);
    field("complete", capture->complete);
    uart_write("\n");
    for (size_t command = 0; command < 7; ++command) {
        uart_write("MICROS_VM01_COMMAND");
        field("producer", capture->work.producer_label);
        field("index", command);
        field("count", capture->commands[command]);
        uart_write("\n");
    }
    for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        uart_write("MICROS_VM01_COUNTS");
        field("producer", capture->work.producer_label);
        field("owner", owner);
        for (size_t kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            field(dimensions[kind], capture->work.counts[owner][kind]);
        }
        uart_write("\n");
    }
}

bool micros_vm01_work_report(void)
{
    if (observer.reported || observer.kernel.complete != 1
        || observer.kernel.invalid || observer.kernel.active || riscv_irq_is_enabled()) {
        return false;
    }
    observer.reported = true;
    if (uart_panic_seize()) {
        return false;
    }
    report_capture(&observer.kernel);
    report_capture(&observer.service);
    uart_write("MICROS_VM01_CAPTURE_END complete=0x0000000000000001 producers=0x0000000000000002\n");
    uart_flush();
    return true;
}
