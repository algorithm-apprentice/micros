#include "tests/qemu/panic_trace_kernel.h"

#include <stdbool.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/diagnostic/diagnostic_history.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler_core.h"

volatile uint32_t micros_panic_trace_test_scenario;
volatile struct micros_panic_trace_test_observation micros_panic_trace_test_observation;
struct micros_test_work_snapshot micros_panic_trace_test_work_snapshot;
_Alignas(4) unsigned char micros_panic_trace_test_history[12012];

static struct micros_scheduler_return_plan selection;
static struct micros_user_context initial_context;
static struct micros_kernel_objects *objects;
static struct micros_hart *hart;
static struct micros_thread_handle selected_thread;
static uintptr_t fixture_hart_id;
static uint64_t forced_reads;
static uint64_t physical_writes;
static bool corruption_injected;
static bool recursion_injected;

extern unsigned char micros_thread_kernel_stacks[];
extern unsigned char micros_thread_kernel_stacks_end[];
#ifdef MICROS_TEST_PANIC_TRACE_COUNTS
extern const unsigned char micros_panic_trace_test_load_fault[];
#endif

_Static_assert(sizeof(struct micros_diagnostic_history) == 12012, "trace fixture history size mismatch");
_Static_assert(sizeof(struct micros_test_work_snapshot) == 3056, "trace fixture work size mismatch");

static bool active(void)
{
    uint32_t phase = micros_panic_trace_test_observation.phase;

    return phase == MICROS_TRACE_TEST_ARMED || phase == MICROS_TRACE_TEST_TRACE;
}

static void require(bool condition)
{
    if (!condition) {
        micros_panic_trace_test_error();
    }
}

static void prepare_thread_stack(void)
{
    struct micros_process_handle first_process;
    struct micros_process_handle second_process;
    struct micros_thread_handle first_thread;
    uintptr_t bottom = (uintptr_t)micros_thread_kernel_stacks;
    struct micros_hart_handle owner = micros_kernel_object_runtime_boot_hart_handle();
    volatile unsigned char *storage = micros_thread_kernel_stacks;
    size_t index;

    require(
        (uintptr_t)micros_thread_kernel_stacks_end - bottom
            == MICROS_THREAD_CAPACITY * MICROS_THREAD_KERNEL_STACK_SIZE
    );
    require(micros_process_create(objects, &first_process) == MICROS_KERNEL_OBJECT_OK);
    require(micros_thread_create(objects, first_process, &first_thread) == MICROS_KERNEL_OBJECT_OK);
    require(micros_process_create(objects, &second_process) == MICROS_KERNEL_OBJECT_OK);
    require(micros_thread_create(objects, second_process, &selected_thread) == MICROS_KERNEL_OBJECT_OK);
    require(first_thread.slot == 0 && selected_thread.slot == 1);
    for (index = 0; index < 2 * MICROS_THREAD_KERNEL_STACK_SIZE; ++index) {
        storage[index] = 0;
    }
    require(
        micros_thread_attach_execution_context(
            objects, first_thread, bottom, bottom + MICROS_THREAD_KERNEL_STACK_SIZE, &initial_context
        ) == MICROS_KERNEL_OBJECT_OK
    );
    require(
        micros_thread_attach_execution_context(
            objects, selected_thread, bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
            bottom + 2 * MICROS_THREAD_KERNEL_STACK_SIZE, &initial_context
        ) == MICROS_KERNEL_OBJECT_OK
    );
    require(micros_scheduler_accounting_initialize(objects, owner, 0) == MICROS_KERNEL_OBJECT_OK);
    require(
        micros_thread_scheduler_admit(objects, owner, selected_thread, 8, UINT64_C(1000000), false)
            == MICROS_KERNEL_OBJECT_OK
    );
    require(micros_hart_plan_user_return(objects, owner, &selection) == MICROS_KERNEL_OBJECT_OK);
    require(micros_hart_commit_user_return(objects, &selection) == MICROS_KERNEL_OBJECT_OK);
    require(hart->current_thread.slot == 1 && hart->current_thread.generation == selected_thread.generation);
}

__attribute__((noinline, disable_tail_calls))
static _Noreturn void trace_chain_one(void)
{
    MICROS_PANIC(fixture_hart_id, "panic-trace-test");
}

__attribute__((noinline, disable_tail_calls))
static _Noreturn void trace_chain_two(void)
{
    trace_chain_one();
}

__attribute__((noinline, disable_tail_calls))
static _Noreturn void trace_chain_three(void)
{
    trace_chain_two();
}

_Noreturn void micros_panic_trace_test_run(uintptr_t hart_id)
{
    const unsigned char *history = (const unsigned char *)micros_diagnostic_history_state();
    uint32_t scenario = micros_panic_trace_test_scenario;
    size_t index;

    (void)riscv_irq_save();
    riscv_external_interrupt_disable();
    fixture_hart_id = hart_id;
    objects = micros_kernel_object_runtime_test_registry();
    hart = micros_kernel_object_runtime_boot_hart();
    require(scenario >= 1 && scenario <= 18 && objects != NULL && hart != NULL && history != NULL);
    require(hart->hardware_id == hart_id);
    micros_panic_trace_test_observation.version = 1;
    micros_panic_trace_test_observation.scenario = scenario;
    micros_panic_trace_test_observation.work_status = UINT32_MAX;
    if (scenario == 4 || scenario == 13 || scenario == 14) {
        prepare_thread_stack();
    }
    for (index = 0; index < sizeof(micros_panic_trace_test_history); ++index) {
        micros_panic_trace_test_history[index] = history[index];
    }
    require(
        micros_test_work_begin(
            UINT64_C(0x504c415430330000) | scenario, 1,
            UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC
        ) == MICROS_TEST_WORK_OK
    );
    micros_panic_trace_test_observation.phase = MICROS_TRACE_TEST_ARMED;
    micros_panic_trace_test_armed();
    if (scenario == 18) {
        (void)sbi_system_reset(SBI_RESET_TYPE_SHUTDOWN, SBI_RESET_REASON_NONE);
        micros_panic_trace_test_error();
    }
    if (scenario == 3 || scenario == 4 || scenario == 5 || scenario == 13 || scenario == 14) {
        micros_panic_trace_test_trap(hart->trap.primary_stack_top - MICROS_TRAP_CONTEXT_SIZE);
    }
    if (scenario >= 6 && scenario <= 12) {
        micros_panic_trace_test_synthetic(scenario);
    }
    trace_chain_three();
}

uint8_t micros_panic_trace_test_lsr(uint8_t value)
{
    if (
        micros_panic_trace_test_observation.phase == MICROS_TRACE_TEST_TRACE
        && micros_panic_trace_test_observation.scenario == 17
    ) {
        require(forced_reads < UINT64_C(100000));
        ++forced_reads;
        value &= (uint8_t)~UINT8_C(0x20);
    }
    return value;
}

void micros_panic_trace_test_thr(void)
{
    if (active()) {
        require(
            micros_panic_trace_test_observation.phase != MICROS_TRACE_TEST_TRACE
            || micros_panic_trace_test_observation.scenario != 17
        );
        require(physical_writes != UINT64_MAX);
        ++physical_writes;
    }
}

void micros_panic_trace_test_seizure(void)
{
    if (active()) {
        require(micros_panic_trace_test_observation.seizure_entries != UINT64_MAX);
        ++micros_panic_trace_test_observation.seizure_entries;
    }
}

void micros_panic_trace_test_report(const struct micros_panic_machine_context *context)
{
    if (active()) {
        require(context != NULL && micros_panic_trace_test_observation.report_bodies != UINT64_MAX);
        if (micros_panic_trace_test_observation.report_bodies == 0) {
            micros_panic_trace_test_observation.first_panic_sp = context->sp;
        }
        ++micros_panic_trace_test_observation.report_bodies;
    }
}

void micros_panic_trace_test_after_core(void)
{
    uint32_t scenario = micros_panic_trace_test_observation.scenario;
    bool observing = active();

    require(observing && !corruption_injected);
    if (scenario == 2) {
        objects->initialization_magic ^= UINT64_C(1);
        corruption_injected = true;
    } else if (scenario == 13) {
        require(hart->current_thread.generation != UINT32_MAX);
        ++hart->current_thread.generation;
        corruption_injected = true;
    } else if (scenario == 14) {
        uintptr_t bottom = (uintptr_t)micros_thread_kernel_stacks;

        objects->threads[selected_thread.slot].kernel_stack_bottom = bottom;
        objects->threads[selected_thread.slot].kernel_stack_top = bottom + MICROS_THREAD_KERNEL_STACK_SIZE;
        hart->trap.primary_stack_bottom = bottom;
        hart->trap.primary_stack_top = bottom + MICROS_THREAD_KERNEL_STACK_SIZE;
        corruption_injected = true;
    }
}

void micros_panic_trace_test_enter(uintptr_t sp, uintptr_t fp)
{
    uint64_t status;
    uint64_t interrupts;
    uint64_t scratch;
    bool observing = active();

    require(observing && micros_panic_trace_test_observation.trace_entries == 0);
    __asm__ volatile("csrr %0, sstatus" : "=r"(status));
    __asm__ volatile("csrr %0, sie" : "=r"(interrupts));
    __asm__ volatile("csrr %0, sscratch" : "=r"(scratch));
    micros_panic_trace_test_observation.trace_sp = sp;
    micros_panic_trace_test_observation.trace_fp = fp;
    micros_panic_trace_test_observation.trace_sstatus = status;
    micros_panic_trace_test_observation.trace_sie = interrupts;
    micros_panic_trace_test_observation.trace_sscratch = scratch;
    micros_panic_trace_test_observation.trace_entries = 1;
}

void micros_panic_trace_test_owner(uint32_t owner, uintptr_t bottom, uintptr_t root_cfa)
{
    bool observing = active();

    require(observing && micros_panic_trace_test_observation.trace_entries == 1 && owner <= 4);
    require(owner != 0 || (bottom == 0 && root_cfa == 0));
    micros_panic_trace_test_observation.owner = owner;
    micros_panic_trace_test_observation.allocation_bottom = bottom;
    micros_panic_trace_test_observation.root_cfa = root_cfa;
}

void micros_panic_trace_test_header(void)
{
    require(
        micros_panic_trace_test_observation.phase == MICROS_TRACE_TEST_ARMED
        && micros_panic_trace_test_observation.trace_entries == 1
    );
    micros_panic_trace_test_observation.phase = MICROS_TRACE_TEST_TRACE;
}

void micros_panic_trace_test_before_word(void)
{
    require(micros_panic_trace_test_observation.phase == MICROS_TRACE_TEST_TRACE);
    if (micros_panic_trace_test_observation.scenario == 16 && !recursion_injected) {
        recursion_injected = true;
        MICROS_PANIC(fixture_hart_id, "panic-trace-recursive");
    }
}

void micros_panic_trace_test_word(void)
{
    require(
        micros_panic_trace_test_observation.phase == MICROS_TRACE_TEST_TRACE
        && micros_panic_trace_test_observation.read_words < 64
    );
    ++micros_panic_trace_test_observation.read_words;
}

void micros_panic_trace_test_link(void)
{
    require(
        micros_panic_trace_test_observation.phase == MICROS_TRACE_TEST_TRACE
        && micros_panic_trace_test_observation.accepted_links < 31
    );
    ++micros_panic_trace_test_observation.accepted_links;
}

void micros_panic_trace_test_complete(uint32_t count, uint32_t stop)
{
    require(
        micros_panic_trace_test_observation.phase == MICROS_TRACE_TEST_TRACE
        && micros_panic_trace_test_observation.completed_traces == 0
        && count <= 32 && stop >= 1 && stop <= 7
        && micros_panic_trace_test_observation.read_words >= 2 * count
    );
    micros_panic_trace_test_observation.record_count = count;
    micros_panic_trace_test_observation.stop = stop;
    micros_panic_trace_test_observation.completed_traces = 1;
}

void micros_panic_trace_test_reset(uint32_t type, uint32_t reason)
{
    uint64_t status;
    uint64_t interrupts;
    uint64_t scratch;
    enum micros_test_work_status work;
    bool observing = active();

    require(observing && micros_panic_trace_test_observation.reset_entries == 0);
#ifdef MICROS_TEST_PANIC_TRACE_COUNTS
    if (micros_panic_trace_test_observation.scenario == 15) {
        uint64_t cause;
        uint64_t fault_address;
        uint64_t fault_pc;

        __asm__ volatile("csrr %0, scause" : "=r"(cause));
        __asm__ volatile("csrr %0, stval" : "=r"(fault_address));
        __asm__ volatile("csrr %0, sepc" : "=r"(fault_pc));
        require(cause == 13 && fault_address == 0);
        require(fault_pc == (uintptr_t)micros_panic_trace_test_load_fault);
    }
#endif
    if (
        micros_panic_trace_test_observation.scenario == 17
        && micros_panic_trace_test_observation.trace_entries != 0
    ) {
        require(forced_reads == UINT64_C(100000));
    }
    work = micros_test_work_finish(&micros_panic_trace_test_work_snapshot);
    require(work == MICROS_TEST_WORK_OK);
    require(
        micros_panic_trace_test_work_snapshot.counts[MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC][MICROS_WORK_KIND_COMPARISON]
            >= physical_writes + forced_reads
    );
    require(
        micros_panic_trace_test_work_snapshot.counts[MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC][MICROS_WORK_KIND_RECORD_VISIT]
            == micros_panic_trace_test_observation.read_words
        && micros_panic_trace_test_work_snapshot.counts[MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC][MICROS_WORK_KIND_LINK_TRAVERSE]
            == micros_panic_trace_test_observation.accepted_links
    );
    __asm__ volatile("csrr %0, sstatus" : "=r"(status));
    __asm__ volatile("csrr %0, sie" : "=r"(interrupts));
    __asm__ volatile("csrr %0, sscratch" : "=r"(scratch));
    micros_panic_trace_test_observation.work_status = (uint32_t)work;
    micros_panic_trace_test_observation.reset_type = type;
    micros_panic_trace_test_observation.reset_reason = reason;
    micros_panic_trace_test_observation.reset_sstatus = status;
    micros_panic_trace_test_observation.reset_sie = interrupts;
    micros_panic_trace_test_observation.reset_sscratch = scratch;
    micros_panic_trace_test_observation.reset_entries = 1;
    micros_panic_trace_test_observation.phase = MICROS_TRACE_TEST_RESET;
}
