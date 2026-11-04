#include "kernel/diagnostic/panic_trace.h"

#include <stddef.h>

#include "arch/riscv64/platform.h"
#include "kernel/diagnostic/panic_trace_core.h"
#include "micros/kernel_object_runtime.h"
#include "micros/work_observation.h"
#include "tests/qemu/panic_trace_kernel.h"

extern unsigned char __boot_stack_bottom[];
extern unsigned char __boot_stack_top[];
extern unsigned char __trap_stack_bottom[];
extern unsigned char __trap_stack_top[];
extern unsigned char __trap_emergency_stack_bottom[];
extern unsigned char __trap_emergency_stack_top[];
extern unsigned char micros_thread_kernel_stacks[];
extern unsigned char micros_thread_kernel_stacks_end[];
extern unsigned char __kernel_text_start[];
extern unsigned char __kernel_text_end[];
#ifdef MICROS_BUILD_NESTED_TRAP_TEST
extern unsigned char micros_nested_trap_test_emergency_stack_bottom[];
extern unsigned char micros_nested_trap_test_emergency_stack_top[];
#endif

static void trace_view_initialize(struct micros_panic_trace_view *view, uintptr_t sp, uintptr_t scratch)
{
    view->sp = sp;
    view->scratch = scratch;
    view->anchor = 0;
    view->boot_bottom = (uintptr_t)__boot_stack_bottom;
    view->boot_top = (uintptr_t)__boot_stack_top;
    view->primary_bottom = (uintptr_t)__trap_stack_bottom;
    view->primary_top = (uintptr_t)__trap_stack_top;
#ifdef MICROS_BUILD_NESTED_TRAP_TEST
    view->emergency_bottom = (uintptr_t)micros_nested_trap_test_emergency_stack_bottom;
    view->emergency_top = (uintptr_t)micros_nested_trap_test_emergency_stack_top;
#else
    view->emergency_bottom = (uintptr_t)__trap_emergency_stack_bottom;
    view->emergency_top = (uintptr_t)__trap_emergency_stack_top;
#endif
    view->bank_bottom = (uintptr_t)micros_thread_kernel_stacks;
    view->bank_top = (uintptr_t)micros_thread_kernel_stacks_end;
    view->idle_bottom = 0;
    view->idle_top = 0;
    view->installed_emergency_bottom = 0;
    view->installed_emergency_top = 0;
    view->selected_bottom = 0;
    view->selected_top = 0;
    view->attached_bottom = 0;
    view->attached_top = 0;
    view->current_slot = MICROS_THREAD_CAPACITY;
    view->registry_ready = false;
    view->hart_live = false;
    view->traps_installed = false;
    view->thread_live = false;
    view->process_live = false;
    view->context_attached = false;
}

static void trace_resolve_owners(struct micros_panic_trace_view *view)
{
    const struct micros_kernel_objects *objects;
    const struct micros_hart *hart;
    const struct micros_thread *thread;
    const struct micros_process *process;
    struct micros_hart_handle handle;
    uintptr_t slot;

    if (view->boot_bottom <= view->sp && view->sp < view->boot_top) {
        return;
    }
    objects = micros_kernel_object_runtime_registry();
    if (objects == NULL || objects->max_harts != 1 || objects->registered_hart_count != 1) {
        return;
    }
    handle = micros_kernel_object_runtime_boot_hart_handle();
    if (micros_hart_resolve(objects, handle, &hart) != MICROS_KERNEL_OBJECT_OK) {
        return;
    }
    view->registry_ready = true;
    view->hart_live = true;
    view->traps_installed = hart->trap_installed;
    view->anchor = (uintptr_t)hart;
    view->idle_bottom = hart->idle_primary_stack_bottom;
    view->idle_top = hart->idle_primary_stack_top;
    view->installed_emergency_bottom = hart->trap.emergency_stack_bottom;
    view->installed_emergency_top = hart->trap.emergency_stack_top;
    view->selected_bottom = hart->trap.primary_stack_bottom;
    view->selected_top = hart->trap.primary_stack_top;
    if (view->sp < view->bank_bottom || view->sp >= view->bank_top) {
        return;
    }
    slot = (view->sp - view->bank_bottom) / MICROS_THREAD_KERNEL_STACK_SIZE;
    view->current_slot = hart->current_thread.slot;
    if (
        slot >= MICROS_THREAD_CAPACITY || slot != view->current_slot
        || micros_thread_resolve(objects, hart->current_thread, &thread) != MICROS_KERNEL_OBJECT_OK
    ) {
        return;
    }
    view->thread_live = true;
    view->context_attached = thread->context_attached;
    view->attached_bottom = thread->kernel_stack_bottom;
    view->attached_top = thread->kernel_stack_top;
    view->process_live = micros_process_resolve(objects, thread->owner, &process) == MICROS_KERNEL_OBJECT_OK;
}

static const char *trace_owner_name(enum micros_panic_trace_owner owner)
{
    if (owner == MICROS_PANIC_TRACE_OWNER_BOOT) {
        return "boot";
    }
    if (owner == MICROS_PANIC_TRACE_OWNER_PRIMARY) {
        return "primary";
    }
    if (owner == MICROS_PANIC_TRACE_OWNER_THREAD) {
        return "thread";
    }
    if (owner == MICROS_PANIC_TRACE_OWNER_EMERGENCY) {
        return "emergency";
    }
    return "unavailable";
}

static const char *trace_stop_name(enum micros_panic_trace_stop stop)
{
    if (stop == MICROS_PANIC_TRACE_END) {
        return "end";
    }
    if (stop == MICROS_PANIC_TRACE_BOUNDARY) {
        return "boundary";
    }
    if (stop == MICROS_PANIC_TRACE_LIMIT) {
        return "limit";
    }
    if (stop == MICROS_PANIC_TRACE_OWNER) {
        return "owner";
    }
    if (stop == MICROS_PANIC_TRACE_INVALID_PC) {
        return "invalid-pc";
    }
    if (stop == MICROS_PANIC_TRACE_NONMONOTONIC) {
        return "nonmonotonic";
    }
    return "invalid-frame";
}

static void trace_emit_frame(uint32_t index, uint64_t pc, uintptr_t frame)
{
    uart_write("MICROS_TRACE_FRAME index=");
    uart_write_hex64(index);
    uart_write(" pc=");
    uart_write_hex64(pc);
    uart_write(" fp=");
    uart_write_hex64(frame);
    uart_write("\n");
}

void micros_panic_trace_report(void)
{
    struct micros_panic_trace_view view;
    struct micros_panic_trace_bounds bounds;
    struct micros_panic_trace_cursor cursor;
    enum micros_panic_trace_owner owner;
    uintptr_t sp;
    uintptr_t scratch;
    uintptr_t frame;
    uintptr_t pc_address;
    uintptr_t link_address;
    uint64_t pc;
    uint64_t previous;
    const char *name;

    __asm__ volatile("mv %0, sp" : "=r"(sp));
    __asm__ volatile("csrr %0, sscratch" : "=r"(scratch));
    frame = micros_panic_trace_frame_pointer();
    trace_view_initialize(&view, sp, scratch);
    trace_resolve_owners(&view);
    /* Local outputs, fixed text and selector-authenticated bounds satisfy the API preconditions. */
    (void)micros_panic_trace_owner_select(&view, &owner, &bounds);
    (void)micros_panic_trace_cursor_open(
        owner, &bounds, sp, frame, (uintptr_t)__kernel_text_start, (uintptr_t)__kernel_text_end, &cursor
    );
#ifdef MICROS_BUILD_PANIC_TRACE_TEST
    micros_panic_trace_test_enter(sp, frame);
    micros_panic_trace_test_owner((uint32_t)owner, bounds.bottom, bounds.root);
#endif
    uart_write("MICROS_TRACE_BEGIN version=1 owner=");
    name = trace_owner_name(owner);
    uart_write(name);
    uart_write(" limit=0x0000000000000020\n");
#ifdef MICROS_BUILD_PANIC_TRACE_TEST
    micros_panic_trace_test_header();
    micros_panic_trace_test_started();
#endif
    for (;;) {
#ifdef MICROS_BUILD_PANIC_TRACE_TEST
        uintptr_t prior_frame = cursor.frame;
#endif
        if (!micros_panic_trace_cursor_plan(&cursor, &pc_address, &link_address)) {
            break;
        }
#ifdef MICROS_BUILD_PANIC_TRACE_TEST
        if (cursor.frame != prior_frame) {
            micros_panic_trace_test_link();
        }
        micros_panic_trace_test_before_word();
#endif
        pc = micros_panic_trace_load_word(pc_address);
#ifdef MICROS_TEST_PANIC_TRACE_COUNTS
        MICROS_TEST_WORK_HIT(
            "panic-trace-word-pc", MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC, MICROS_WORK_KIND_RECORD_VISIT, 1
        );
#endif
#ifdef MICROS_BUILD_PANIC_TRACE_TEST
        micros_panic_trace_test_word();
#endif
        previous = micros_panic_trace_load_word(link_address);
#ifdef MICROS_TEST_PANIC_TRACE_COUNTS
        MICROS_TEST_WORK_HIT(
            "panic-trace-word-link", MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC, MICROS_WORK_KIND_RECORD_VISIT, 1
        );
#endif
#ifdef MICROS_BUILD_PANIC_TRACE_TEST
        micros_panic_trace_test_word();
#endif
        if (!micros_panic_trace_cursor_consume(&cursor, pc, previous)) {
            break;
        }
        trace_emit_frame(cursor.count - 1, pc, cursor.frame);
    }
    uart_write("MICROS_TRACE_END count=");
    uart_write_hex64(cursor.count);
    uart_write(" stop=");
    name = trace_stop_name(cursor.stop);
    uart_write(name);
    uart_write("\n");
#ifdef MICROS_BUILD_PANIC_TRACE_TEST
    micros_panic_trace_test_complete(cursor.count, (uint32_t)cursor.stop);
#endif
}
