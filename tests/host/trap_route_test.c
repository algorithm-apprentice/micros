#include "kernel/trap_route_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SCAUSE_INTERRUPT (UINT64_C(1) << 63)

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return 1; \
        } \
    } while (false)

int main(void)
{
    EXPECT_TRUE(
        micros_trap_interrupt_route_classify(
            false,
            SCAUSE_INTERRUPT | UINT64_C(9)
        ) == MICROS_TRAP_INTERRUPT_USER_EXTERNAL
    );
    EXPECT_TRUE(
        micros_trap_interrupt_route_classify(
            true,
            SCAUSE_INTERRUPT | UINT64_C(9)
        ) == MICROS_TRAP_INTERRUPT_SUPERVISOR_EXTERNAL
    );
    EXPECT_TRUE(
        micros_trap_interrupt_route_classify(
            false,
            SCAUSE_INTERRUPT | UINT64_C(5)
        ) == MICROS_TRAP_INTERRUPT_USER_TIMER
    );
    EXPECT_TRUE(
        micros_trap_interrupt_route_classify(
            true,
            SCAUSE_INTERRUPT | UINT64_C(5)
        ) == MICROS_TRAP_INTERRUPT_SUPERVISOR_TIMER
    );
    EXPECT_TRUE(
        micros_trap_interrupt_route_classify(
            false,
            SCAUSE_INTERRUPT | UINT64_C(1)
        ) == MICROS_TRAP_INTERRUPT_USER_OTHER
    );
    EXPECT_TRUE(
        micros_trap_interrupt_route_classify(
            true,
            SCAUSE_INTERRUPT | UINT64_C(1)
        ) == MICROS_TRAP_INTERRUPT_SUPERVISOR_OTHER
    );
    EXPECT_TRUE(
        micros_trap_interrupt_route_classify(false, UINT64_C(9))
            == MICROS_TRAP_INTERRUPT_NONE
        && micros_trap_interrupt_route_classify(true, UINT64_C(9))
            == MICROS_TRAP_INTERRUPT_NONE
    );
    return 0;
}
