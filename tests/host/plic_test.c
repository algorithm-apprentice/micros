#include "kernel/plic_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

bool micros_plic_test_run(void);

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
            return false; \
        } \
    } while (false)

struct plic_fixture {
    uint32_t priority;
    uint32_t enable;
    uint32_t threshold;
    uint32_t claim_complete;
    struct micros_plic_controller controller;
};

static struct micros_plic_registers fixture_registers(
    struct plic_fixture *fixture
)
{
    return (struct micros_plic_registers){
        .priority = &fixture->priority,
        .enable = &fixture->enable,
        .threshold = &fixture->threshold,
        .claim_complete = &fixture->claim_complete,
    };
}

static bool initialize_fixture(struct plic_fixture *fixture)
{
    struct micros_plic_registers registers;

    memset(fixture, 0xa5, sizeof(*fixture));
    registers = fixture_registers(fixture);
    memset(&fixture->controller, 0, sizeof(fixture->controller));
    return (
        micros_plic_controller_initialize(
            &fixture->controller,
            &registers
        ) == MICROS_PLIC_OK
        && fixture->priority == 0
        && fixture->enable == 0
        && fixture->threshold == 0
        && fixture->claim_complete == UINT32_C(0xa5a5a5a5)
        && fixture->controller.phase == MICROS_PLIC_DISABLED
        && micros_plic_controller_validate(&fixture->controller)
            == MICROS_PLIC_OK
    );
}

static bool test_route_transitions(void)
{
    struct plic_fixture fixture;
    uint32_t source = UINT32_MAX;

    EXPECT_TRUE(initialize_fixture(&fixture));
    EXPECT_TRUE(
        micros_plic_controller_prepare_tty(&fixture.controller)
            == MICROS_PLIC_OK
        && fixture.priority == 1
        && fixture.enable == 0
        && fixture.threshold == 0
        && fixture.controller.phase == MICROS_PLIC_PREPARED
        && micros_plic_controller_enable_tty(&fixture.controller)
            == MICROS_PLIC_OK
        && fixture.priority == 1
        && fixture.enable
            == (UINT32_C(1) << MICROS_TTY_UART_IRQ_SOURCE)
        && fixture.threshold == 0
        && fixture.controller.phase == MICROS_PLIC_ENABLED
    );

    fixture.claim_complete = 0;
    EXPECT_TRUE(
        micros_plic_controller_claim(&fixture.controller, &source)
            == MICROS_PLIC_OK
        && source == 0
        && fixture.claim_complete == 0
    );
    fixture.claim_complete = MICROS_TTY_UART_IRQ_SOURCE;
    source = UINT32_MAX;
    EXPECT_TRUE(
        micros_plic_controller_claim(&fixture.controller, &source)
            == MICROS_PLIC_OK
        && source == MICROS_TTY_UART_IRQ_SOURCE
        && fixture.claim_complete == MICROS_TTY_UART_IRQ_SOURCE
        && micros_plic_controller_complete(
            &fixture.controller,
            MICROS_TTY_UART_IRQ_SOURCE
        ) == MICROS_PLIC_OK
        && fixture.claim_complete == MICROS_TTY_UART_IRQ_SOURCE
    );
    EXPECT_TRUE(
        micros_plic_controller_disable_tty(&fixture.controller)
            == MICROS_PLIC_OK
        && fixture.priority == 0
        && fixture.enable == 0
        && fixture.threshold == 0
        && fixture.controller.phase == MICROS_PLIC_DISABLED
        && micros_plic_controller_disable_tty(&fixture.controller)
            == MICROS_PLIC_OK
    );
    return true;
}

static bool test_prevalidated_enable_order(void)
{
    struct plic_fixture fixture;
    struct plic_fixture snapshot;
    struct micros_plic_tty_enable_plan plan;
    struct micros_plic_tty_enable_plan plan_sentinel;

    EXPECT_TRUE(initialize_fixture(&fixture));
    snapshot = fixture;
    memset(&plan_sentinel, 0xa5, sizeof(plan_sentinel));
    plan = plan_sentinel;
    EXPECT_TRUE(
        micros_plic_controller_prepare_tty_enable(
            &fixture.controller,
            &plan
        ) == MICROS_PLIC_OK
        && memcmp(&fixture, &snapshot, sizeof(fixture)) == 0
        && memcmp(&plan, &plan_sentinel, sizeof(plan)) != 0
    );
    micros_plic_controller_commit_tty_prepare_prevalidated(
        &fixture.controller,
        &plan
    );
    EXPECT_TRUE(
        fixture.priority == 1
        && fixture.enable == 0
        && fixture.threshold == 0
        && fixture.controller.phase == MICROS_PLIC_PREPARED
    );
    micros_plic_controller_commit_tty_enable_prevalidated(
        &fixture.controller,
        &plan
    );
    EXPECT_TRUE(
        fixture.priority == 1
        && fixture.enable
            == (UINT32_C(1) << MICROS_TTY_UART_IRQ_SOURCE)
        && fixture.threshold == 0
        && fixture.controller.phase == MICROS_PLIC_ENABLED
    );
    snapshot = fixture;
    plan = plan_sentinel;
    EXPECT_TRUE(
        micros_plic_controller_prepare_tty_enable(
            &fixture.controller,
            &plan
        ) == MICROS_PLIC_ERROR_STATE
        && memcmp(&fixture, &snapshot, sizeof(fixture)) == 0
        && memcmp(&plan, &plan_sentinel, sizeof(plan)) == 0
    );
    return true;
}

static bool test_failure_preservation(void)
{
    struct plic_fixture fixture;
    struct plic_fixture snapshot;
    uint32_t source = UINT32_MAX;
    uint32_t source_sentinel = source;

    EXPECT_TRUE(initialize_fixture(&fixture));
    snapshot = fixture;
    EXPECT_TRUE(
        micros_plic_controller_enable_tty(&fixture.controller)
            == MICROS_PLIC_ERROR_STATE
        && memcmp(&fixture, &snapshot, sizeof(fixture)) == 0
        && micros_plic_controller_claim(
            &fixture.controller,
            &source
        ) == MICROS_PLIC_ERROR_STATE
        && source == source_sentinel
        && memcmp(&fixture, &snapshot, sizeof(fixture)) == 0
    );
    EXPECT_TRUE(
        micros_plic_controller_prepare_tty(&fixture.controller)
            == MICROS_PLIC_OK
        && micros_plic_controller_enable_tty(&fixture.controller)
            == MICROS_PLIC_OK
    );
    snapshot = fixture;
    EXPECT_TRUE(
        micros_plic_controller_complete(&fixture.controller, 9)
            == MICROS_PLIC_ERROR_ARGUMENT
        && memcmp(&fixture, &snapshot, sizeof(fixture)) == 0
    );
    fixture.enable |= UINT32_C(1) << 11;
    EXPECT_TRUE(
        micros_plic_controller_validate(&fixture.controller)
            == MICROS_PLIC_ERROR_INVARIANT
        && micros_plic_controller_disable_tty(&fixture.controller)
            == MICROS_PLIC_OK
        && micros_plic_controller_validate(&fixture.controller)
            == MICROS_PLIC_OK
    );
    return true;
}

static bool test_register_validation(void)
{
    struct plic_fixture fixture;
    struct micros_plic_registers registers;
    struct micros_plic_controller sentinel;
    struct micros_plic_controller controller;

    memset(&fixture, 0, sizeof(fixture));
    registers = fixture_registers(&fixture);
    memset(&sentinel, 0xa5, sizeof(sentinel));
    controller = sentinel;
    registers.priority = NULL;
    EXPECT_TRUE(
        micros_plic_controller_initialize(
            &controller,
            &registers
        ) == MICROS_PLIC_ERROR_ARGUMENT
        && memcmp(&controller, &sentinel, sizeof(controller)) == 0
    );
    return true;
}

bool micros_plic_test_run(void)
{
    return (
        test_route_transitions()
        && test_prevalidated_enable_order()
        && test_failure_preservation()
        && test_register_validation()
    );
}
