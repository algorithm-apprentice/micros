#ifndef MICROS_TEST_TERMINAL_FAILURE_KERNEL_H
#define MICROS_TEST_TERMINAL_FAILURE_KERNEL_H

#ifdef MICROS_BUILD_TERMINAL_FAILURE_TEST

#include <stdbool.h>
#include <stdint.h>

#include "tests/work/work_counts.h"

struct micros_panic_machine_context;

enum micros_terminal_test_phase {
    MICROS_TERMINAL_SETUP,
    MICROS_TERMINAL_ARMED,
    MICROS_TERMINAL_RESET_REACHED,
    MICROS_TERMINAL_POLL_OVERRUN,
    MICROS_TERMINAL_UNEXPECTED_RETURN,
    MICROS_TERMINAL_FIXTURE_ERROR,
};

struct micros_terminal_observation {
    uint32_t version;
    uint32_t scenario;
    uint32_t phase;
    uint32_t reserved0;
    uint64_t thre_reads;
    uint64_t temt_reads;
    uint64_t thr_writes;
    uint64_t seizure_entries;
    uint64_t report_bodies;
    uint64_t reset_entries;
    uint64_t first_entry_sp;
    uint64_t sstatus;
    uint64_t sie;
    uint32_t work_status;
    uint32_t reset_type;
    uint32_t reset_reason;
    uint32_t reserved1;
};

extern volatile uint32_t micros_terminal_test_scenario;
extern volatile struct micros_terminal_observation micros_terminal_test_observation;
extern struct micros_test_work_snapshot micros_terminal_test_work_snapshot;

_Noreturn void micros_terminal_test_run(uintptr_t hart_id);
void micros_terminal_test_armed(void);
_Noreturn void micros_terminal_test_overrun(void);
_Noreturn void micros_terminal_test_unexpected_return(void);
_Noreturn void micros_terminal_test_error(void);
uint8_t micros_terminal_test_lsr(uint8_t value, bool drain);
void micros_terminal_test_thr(void);
void micros_terminal_test_seizure(void);
void micros_terminal_test_report(const struct micros_panic_machine_context *context);
void micros_terminal_test_after_seize(uintptr_t hart_id);
void micros_terminal_test_reset(uint32_t type, uint32_t reason);
void micros_terminal_test_corrupt_console(void);

#define MICROS_TERMINAL_TEST_LSR(value, drain) micros_terminal_test_lsr((value), (drain))

#else

#define MICROS_TERMINAL_TEST_LSR(value, drain) (value)

#endif
#endif
