#include "tests/qemu/terminal_failure_kernel.h"

#include <stddef.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "kernel/boot/boot_internal.h"
#include "micros/panic.h"

volatile uint32_t micros_terminal_test_scenario;
_Alignas(8) volatile struct micros_terminal_observation micros_terminal_test_observation;
struct micros_test_work_snapshot micros_terminal_test_work_snapshot;

static bool faults_armed;
static bool recursion_injected;
static uint64_t forced_reads;
static struct boot_context invalid_boot;
static const uint8_t invalid_fdt[40] = {0};

#define OBSERVATION_OFFSET(field, offset) \
    _Static_assert(offsetof(struct micros_terminal_observation, field) == (offset), "observation " #field)

_Static_assert(sizeof(struct micros_terminal_observation) == 104, "complete observation");
_Static_assert(_Alignof(struct micros_terminal_observation) == 8, "observation alignment");
OBSERVATION_OFFSET(version, 0);
OBSERVATION_OFFSET(scenario, 4);
OBSERVATION_OFFSET(phase, 8);
OBSERVATION_OFFSET(reserved0, 12);
OBSERVATION_OFFSET(thre_reads, 16);
OBSERVATION_OFFSET(temt_reads, 24);
OBSERVATION_OFFSET(thr_writes, 32);
OBSERVATION_OFFSET(seizure_entries, 40);
OBSERVATION_OFFSET(report_bodies, 48);
OBSERVATION_OFFSET(reset_entries, 56);
OBSERVATION_OFFSET(first_entry_sp, 64);
OBSERVATION_OFFSET(sstatus, 72);
OBSERVATION_OFFSET(sie, 80);
OBSERVATION_OFFSET(work_status, 88);
OBSERVATION_OFFSET(reset_type, 92);
OBSERVATION_OFFSET(reset_reason, 96);
OBSERVATION_OFFSET(reserved1, 100);

static void observe_interrupts(void)
{
    uintptr_t sstatus;
    uintptr_t sie;

    __asm__ volatile("csrr %0, sstatus" : "=r"(sstatus));
    __asm__ volatile("csrr %0, sie" : "=r"(sie));
    micros_terminal_test_observation.sstatus = sstatus;
    micros_terminal_test_observation.sie = sie;
}

void micros_terminal_test_armed(void)
{
    __asm__ volatile("" : : : "memory");
}

_Noreturn void micros_terminal_test_overrun(void)
{
    for (;;) {
        __asm__ volatile("wfi");
    }
}

_Noreturn void micros_terminal_test_unexpected_return(void)
{
    for (;;) {
        __asm__ volatile("wfi");
    }
}

_Noreturn void micros_terminal_test_error(void)
{
    micros_terminal_test_observation.phase = MICROS_TERMINAL_FIXTURE_ERROR;
    for (;;) {
        __asm__ volatile("wfi");
    }
}

static void finish_capture(enum micros_terminal_test_phase phase)
{
    enum micros_test_work_status status = micros_test_work_finish(&micros_terminal_test_work_snapshot);

    micros_terminal_test_observation.work_status = (uint32_t)status;
    observe_interrupts();
    if (status != MICROS_TEST_WORK_OK) {
        micros_terminal_test_error();
    }
    micros_terminal_test_observation.phase = (uint32_t)phase;
}

uint8_t micros_terminal_test_lsr(uint8_t value, bool drain)
{
    uint32_t scenario = micros_terminal_test_observation.scenario;
    bool selected;

    if (micros_terminal_test_observation.phase != MICROS_TERMINAL_ARMED) {
        return value;
    }
    if (drain) {
        if (micros_terminal_test_observation.temt_reads == UINT64_MAX) {
            micros_terminal_test_error();
        }
        ++micros_terminal_test_observation.temt_reads;
    } else {
        if (micros_terminal_test_observation.thre_reads == UINT64_MAX) {
            micros_terminal_test_error();
        }
        ++micros_terminal_test_observation.thre_reads;
    }
    selected = faults_armed && (
        drain ? (scenario == 3 || scenario == 6 || scenario == 8)
              : (scenario == 2 || scenario == 5 || scenario == 7 || scenario == 11)
    );
    if (selected) {
        if (forced_reads >= UINT64_C(100001)) {
            micros_terminal_test_error();
        }
        ++forced_reads;
        if (forced_reads == UINT64_C(100001)) {
            finish_capture(MICROS_TERMINAL_POLL_OVERRUN);
            micros_terminal_test_overrun();
        }
        if ((scenario != 2 && scenario != 3) || forced_reads < UINT64_C(100000)) {
            value &= (uint8_t)~(drain ? UINT8_C(0x40) : UINT8_C(0x20));
        }
    }
    return value;
}

void micros_terminal_test_thr(void)
{
    if (micros_terminal_test_observation.phase != MICROS_TERMINAL_ARMED) {
        return;
    }
    if (micros_terminal_test_observation.thr_writes == UINT64_MAX) {
        micros_terminal_test_error();
    }
    ++micros_terminal_test_observation.thr_writes;
}

void micros_terminal_test_seizure(void)
{
    if (micros_terminal_test_observation.phase != MICROS_TERMINAL_ARMED) {
        return;
    }
    if (micros_terminal_test_observation.seizure_entries == UINT64_MAX) {
        micros_terminal_test_error();
    }
    ++micros_terminal_test_observation.seizure_entries;
}

void micros_terminal_test_report(const struct micros_panic_machine_context *context)
{
    if (micros_terminal_test_observation.phase != MICROS_TERMINAL_ARMED) {
        return;
    }
    if (context == NULL || micros_terminal_test_observation.report_bodies == UINT64_MAX) {
        micros_terminal_test_error();
    }
    if (micros_terminal_test_observation.report_bodies == 0) {
        micros_terminal_test_observation.first_entry_sp = context->sp;
    }
    ++micros_terminal_test_observation.report_bodies;
}

void micros_terminal_test_after_seize(uintptr_t hart_id)
{
    uint32_t scenario = micros_terminal_test_observation.scenario;

    if (micros_terminal_test_observation.phase != MICROS_TERMINAL_ARMED) {
        return;
    }
    if (scenario == 7 || scenario == 8) {
        faults_armed = true;
    }
    if (scenario == 9 && !recursion_injected) {
        recursion_injected = true;
        MICROS_PANIC(hart_id, "terminal-recursive-test");
    }
}

void micros_terminal_test_reset(uint32_t type, uint32_t reason)
{
    if (micros_terminal_test_observation.phase != MICROS_TERMINAL_ARMED) {
        micros_terminal_test_error();
    }
    if (micros_terminal_test_observation.reset_entries == UINT64_MAX) {
        micros_terminal_test_error();
    }
    ++micros_terminal_test_observation.reset_entries;
    micros_terminal_test_observation.reset_type = type;
    micros_terminal_test_observation.reset_reason = reason;
    finish_capture(MICROS_TERMINAL_RESET_REACHED);
}

static void arm_scenario(uint32_t scenario)
{
    volatile uint8_t *const uart = (volatile uint8_t *)(uintptr_t)MICROS_RISCV_UART0_BASE;

    if (scenario == 3 || scenario == 6) {
        uart[3] = 3;
        riscv_mmio_fence();
        uart[1] = 3;
        riscv_mmio_fence();
    }
    if (scenario == 10 || scenario == 11) {
        micros_terminal_test_corrupt_console();
    }
    faults_armed = scenario == 2 || scenario == 3 || scenario == 5 || scenario == 6 || scenario == 11;
    micros_terminal_test_observation.version = 1;
    micros_terminal_test_observation.scenario = scenario;
    micros_terminal_test_observation.work_status = UINT32_MAX;
    if (micros_test_work_begin(UINT64_C(0x504c415430320000) | scenario, 1, 2) != MICROS_TEST_WORK_OK) {
        micros_terminal_test_error();
    }
    observe_interrupts();
    micros_terminal_test_observation.phase = MICROS_TERMINAL_ARMED;
    micros_terminal_test_armed();
}

_Noreturn void micros_terminal_test_run(uintptr_t hart_id)
{
    uint32_t scenario = micros_terminal_test_scenario;

    (void)riscv_irq_save();
    riscv_external_interrupt_disable();
    if (scenario < 1 || scenario > 12) {
        micros_terminal_test_error();
    }
    uart_flush();
    arm_scenario(scenario);
    if (scenario == 1 || scenario == 2 || scenario == 5) {
        uart_write("x");
    } else if (scenario == 3 || scenario == 6) {
        if (!uart_console_begin_preflight()) {
            micros_terminal_test_error();
        }
        uart_console_begin_quiesce_prevalidated();
    } else if (scenario == 12) {
        invalid_boot.hart_id = hart_id;
        boot_discover_memory(&invalid_boot, (uintptr_t)invalid_fdt);
    } else {
        if (scenario == 4) {
            micros_panic_seize();
        }
        MICROS_PANIC(hart_id, "terminal-failure-test");
    }
    if (scenario <= 3) {
        (void)sbi_system_reset(SBI_RESET_TYPE_SHUTDOWN, SBI_RESET_REASON_NONE);
    }
    finish_capture(MICROS_TERMINAL_UNEXPECTED_RETURN);
    micros_terminal_test_unexpected_return();
}
