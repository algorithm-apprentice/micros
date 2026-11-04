#ifndef MICROS_TESTS_PANIC_TRACE_KERNEL_H
#define MICROS_TESTS_PANIC_TRACE_KERNEL_H

#ifdef MICROS_BUILD_PANIC_TRACE_TEST

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/panic_context.h"
#include "tests/work/work_counts.h"

enum {
    MICROS_TRACE_TEST_SETUP = 0,
    MICROS_TRACE_TEST_ARMED = 1,
    MICROS_TRACE_TEST_TRACE = 2,
    MICROS_TRACE_TEST_RESET = 3,
    MICROS_TRACE_TEST_ERROR = 4,
};

struct micros_panic_trace_test_observation {
    uint32_t version;
    uint32_t scenario;
    uint32_t phase;
    uint32_t work_status;
    uint64_t report_bodies;
    uint64_t seizure_entries;
    uint64_t trace_entries;
    uint64_t completed_traces;
    uint64_t read_words;
    uint64_t accepted_links;
    uint64_t reset_entries;
    uint64_t first_panic_sp;
    uint64_t trace_sp;
    uint64_t trace_fp;
    uint64_t allocation_bottom;
    uint64_t root_cfa;
    uint32_t owner;
    uint32_t stop;
    uint32_t record_count;
    uint32_t reserved0;
    uint64_t trace_sstatus;
    uint64_t trace_sie;
    uint64_t trace_sscratch;
    uint64_t reset_sstatus;
    uint64_t reset_sie;
    uint64_t reset_sscratch;
    uint32_t reset_type;
    uint32_t reset_reason;
    uint64_t reserved1;
};

_Static_assert(sizeof(struct micros_panic_trace_test_observation) == 192, "trace observation size mismatch");
_Static_assert(_Alignof(struct micros_panic_trace_test_observation) == 8, "trace observation alignment mismatch");
_Static_assert(offsetof(struct micros_panic_trace_test_observation, report_bodies) == 16, "trace counter offset mismatch");
_Static_assert(offsetof(struct micros_panic_trace_test_observation, owner) == 112, "trace owner offset mismatch");
_Static_assert(offsetof(struct micros_panic_trace_test_observation, trace_sstatus) == 128, "trace CSR offset mismatch");
_Static_assert(offsetof(struct micros_panic_trace_test_observation, reset_type) == 176, "trace reset offset mismatch");
_Static_assert(offsetof(struct micros_panic_trace_test_observation, reserved1) == 184, "trace reserved offset mismatch");

extern volatile uint32_t micros_panic_trace_test_scenario;
extern volatile struct micros_panic_trace_test_observation micros_panic_trace_test_observation;
extern struct micros_test_work_snapshot micros_panic_trace_test_work_snapshot;
extern unsigned char micros_panic_trace_test_history[12012];

void micros_panic_trace_test_armed(void);
void micros_panic_trace_test_started(void);
_Noreturn void micros_panic_trace_test_error(void);
_Noreturn void micros_panic_trace_test_run(uintptr_t hart_id);
_Noreturn void micros_panic_trace_test_synthetic(uint32_t scenario);
_Noreturn void micros_panic_trace_test_trap(uintptr_t root_cfa);
void micros_panic_trace_test_fake_pc(void);
uint8_t micros_panic_trace_test_lsr(uint8_t value);
void micros_panic_trace_test_thr(void);
void micros_panic_trace_test_seizure(void);
void micros_panic_trace_test_report(const struct micros_panic_machine_context *context);
void micros_panic_trace_test_after_core(void);
void micros_panic_trace_test_enter(uintptr_t sp, uintptr_t fp);
void micros_panic_trace_test_owner(uint32_t owner, uintptr_t bottom, uintptr_t root_cfa);
void micros_panic_trace_test_header(void);
void micros_panic_trace_test_before_word(void);
void micros_panic_trace_test_word(void);
void micros_panic_trace_test_link(void);
void micros_panic_trace_test_complete(uint32_t count, uint32_t stop);
void micros_panic_trace_test_reset(uint32_t type, uint32_t reason);

#define MICROS_PANIC_TRACE_TEST_LSR(value) micros_panic_trace_test_lsr(value)

#else

#define MICROS_PANIC_TRACE_TEST_LSR(value) (value)

#endif

#endif
