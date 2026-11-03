#include "kernel/diagnostic/panic_control_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%u: %s\n", __FILE__, \
                (unsigned)__LINE__, #condition); \
            return false; \
        } \
    } while (0)

struct guarded_control {
    uint32_t before;
    struct micros_panic_control control;
    uint32_t after;
};

_Static_assert(sizeof(struct micros_panic_control) == 4, "terminal ownership is one phase");

static bool entry_transitions(void)
{
    static const struct {
        uint32_t initial;
        enum micros_panic_enter_result result;
        uint32_t final;
    } cases[] = {
        {0, MICROS_PANIC_FIRST, 1},
        {1, MICROS_PANIC_REENTERED, 1},
        {2, MICROS_PANIC_REENTERED, 2},
        {3, MICROS_PANIC_ENTRY_INVARIANT, 3},
        {UINT32_C(0x80000000), MICROS_PANIC_ENTRY_INVARIANT, UINT32_C(0x80000000)},
        {UINT32_MAX, MICROS_PANIC_ENTRY_INVARIANT, UINT32_MAX},
    };
    size_t index;

    REQUIRE(micros_panic_control_enter(NULL) == MICROS_PANIC_ENTRY_INVARIANT);
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        struct guarded_control guarded = {
            UINT32_C(0x13579bdf), {cases[index].initial}, UINT32_C(0x2468ace0),
        };

        REQUIRE(micros_panic_control_enter(&guarded.control) == cases[index].result);
        REQUIRE(guarded.control.phase == cases[index].final);
        REQUIRE(guarded.before == UINT32_C(0x13579bdf));
        REQUIRE(guarded.after == UINT32_C(0x2468ace0));
    }
    return true;
}

static bool reset_transitions(void)
{
    static const struct {
        uint32_t initial;
        enum micros_panic_reset_result result;
        uint32_t final;
    } cases[] = {
        {0, MICROS_PANIC_ISSUE, 2},
        {1, MICROS_PANIC_ISSUE, 2},
        {2, MICROS_PANIC_ALREADY_ISSUED, 2},
        {3, MICROS_PANIC_RESET_INVARIANT, 3},
        {UINT32_C(0x80000000), MICROS_PANIC_RESET_INVARIANT, UINT32_C(0x80000000)},
        {UINT32_MAX, MICROS_PANIC_RESET_INVARIANT, UINT32_MAX},
    };
    size_t index;

    REQUIRE(micros_panic_control_request_reset(NULL) == MICROS_PANIC_RESET_INVARIANT);
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        struct guarded_control guarded = {
            UINT32_C(0x13579bdf), {cases[index].initial}, UINT32_C(0x2468ace0),
        };

        REQUIRE(micros_panic_control_request_reset(&guarded.control) == cases[index].result);
        REQUIRE(guarded.control.phase == cases[index].final);
        REQUIRE(guarded.before == UINT32_C(0x13579bdf));
        REQUIRE(guarded.after == UINT32_C(0x2468ace0));
    }
    return true;
}

static bool first_entry_and_returned_reset_never_reopen_ownership(void)
{
    struct micros_panic_control reporter = {0};
    struct micros_panic_control direct_failure = {0};
    unsigned index;

    REQUIRE(micros_panic_control_enter(&reporter) == MICROS_PANIC_FIRST);
    REQUIRE(micros_panic_control_enter(&reporter) == MICROS_PANIC_REENTERED);
    REQUIRE(micros_panic_control_request_reset(&reporter) == MICROS_PANIC_ISSUE);
    REQUIRE(micros_panic_control_request_reset(&direct_failure) == MICROS_PANIC_ISSUE);
    for (index = 0; index < 4; ++index) {
        REQUIRE(micros_panic_control_enter(&reporter) == MICROS_PANIC_REENTERED);
        REQUIRE(micros_panic_control_request_reset(&reporter) == MICROS_PANIC_ALREADY_ISSUED);
        REQUIRE(micros_panic_control_enter(&direct_failure) == MICROS_PANIC_REENTERED);
        REQUIRE(micros_panic_control_request_reset(&direct_failure) == MICROS_PANIC_ALREADY_ISSUED);
        REQUIRE(reporter.phase == 2 && direct_failure.phase == 2);
    }
    return true;
}

int main(void)
{
    if (
        !entry_transitions()
        || !reset_transitions()
        || !first_entry_and_returned_reset_never_reopen_ownership()
    ) {
        return 1;
    }
    puts("panic ownership: first entry, one reset and invariant preservation passed");
    return 0;
}
