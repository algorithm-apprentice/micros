#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/kernel_object_runtime_internal.h"
#include "kernel/plic.h"
#include "kernel/tty_fault.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/tty_interrupt.h"
#include "kernel/user_address_space_internal.h"
#include "micros/ipc_core.h"
#include "micros/panic.h"

static const struct micros_process_handle tty_process = {
    .slot = MICROS_TTY_PROCESS_SLOT,
    .generation = 7,
};
static const struct micros_thread_handle tty_thread = {
    .slot = 5,
    .generation = 9,
};
static const micros_endpoint_t tty_endpoint = UINT32_C(0x00007003);

static struct micros_bootstrap_control_state bootstrap;
static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_tty_handoff_runtime_state tty_state;
static uint32_t claim_source;
static unsigned int claim_count;
static bool notification_should_fail;
static bool notification_committed;
static bool order_violation;
static int operation_stage;
static uint64_t notified_events;
static micros_endpoint_t notified_endpoint;
static enum micros_bootstrap_diagnostic_reason failure_reason;
static jmp_buf panic_target;
static bool panic_armed;

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            return false; \
        } \
    } while (false)

static void reset_fixture(uint32_t source)
{
    bootstrap = (struct micros_bootstrap_control_state){
        .phase = MICROS_BOOTSTRAP_PHASE_RUNNING,
    };
    tty_state = (struct micros_tty_handoff_runtime_state){
        .service_id = MICROS_TTY_SERVICE_ID,
        .endpoint = tty_endpoint,
        .image_id = 104,
        .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
        .process = tty_process,
        .thread = tty_thread,
        .root_physical_address = UINT64_C(0x80200000),
    };
    if (
        micros_tty_handoff_initialize(&tty_state.handoff)
            != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_begin(&tty_state.handoff, 100, 50)
            != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_mark_mapped(&tty_state.handoff)
            != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_release(&tty_state.handoff)
            != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_commit(&tty_state.handoff)
            != MICROS_TTY_HANDOFF_OK
    ) {
        abort();
    }
    claim_source = source;
    claim_count = 0;
    notification_should_fail = false;
    notification_committed = false;
    order_violation = false;
    operation_stage = 0;
    notified_events = 0;
    notified_endpoint = MICROS_ENDPOINT_NONE;
    failure_reason = (enum micros_bootstrap_diagnostic_reason)0;
    panic_armed = false;
}

static bool dispatch(
    enum micros_tty_interrupt_result expected
)
{
    struct micros_hart hart = {
        .hardware_id = 0,
    };
    struct micros_trap_frame frame = {0};

    return (
        micros_tty_interrupt_dispatch(&hart, &frame) == expected
        && !order_violation
    );
}

static bool test_spurious_claim_is_observation(void)
{
    struct micros_tty_handoff snapshot;

    reset_fixture(0);
    snapshot = tty_state.handoff;
    EXPECT_TRUE(
        dispatch(MICROS_TTY_INTERRUPT_SPURIOUS)
        && claim_count == 1
        && !notification_committed
        && tty_state.handoff.console_phase
            == snapshot.console_phase
        && tty_state.handoff.route_phase == snapshot.route_phase
        && tty_state.handoff.claimed_source
            == snapshot.claimed_source
    );
    return true;
}

static bool test_source_ten_is_retained_and_notified(void)
{
    reset_fixture(MICROS_TTY_UART_IRQ_SOURCE);
    EXPECT_TRUE(
        dispatch(MICROS_TTY_INTERRUPT_HANDLED)
        && claim_count == 1
        && notification_committed
        && notified_endpoint == tty_endpoint
        && notified_events == MICROS_KERNEL_EVENT_TTY_IRQ
        && tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_OWNED
        && tty_state.handoff.route_phase
            == MICROS_TTY_ROUTE_IN_SERVICE
        && tty_state.handoff.claimed_source
            == MICROS_TTY_UART_IRQ_SOURCE
        && operation_stage == 5
    );
    return true;
}

static bool test_wrong_source_is_fatal_without_completion(void)
{
    struct micros_hart hart = {
        .hardware_id = 0,
    };
    struct micros_trap_frame frame = {0};

    reset_fixture(9);
    panic_armed = true;
    if (setjmp(panic_target) == 0) {
        (void)micros_tty_interrupt_dispatch(&hart, &frame);
        panic_armed = false;
        return false;
    }
    panic_armed = false;
    return (
        claim_count == 1
        && !notification_committed
        && failure_reason == MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT
        && tty_state.handoff.route_phase == MICROS_TTY_ROUTE_IDLE
    );
}

static bool test_notification_failure_retains_claim(void)
{
    struct micros_hart hart = {
        .hardware_id = 0,
    };
    struct micros_trap_frame frame = {0};

    reset_fixture(MICROS_TTY_UART_IRQ_SOURCE);
    notification_should_fail = true;
    panic_armed = true;
    if (setjmp(panic_target) == 0) {
        (void)micros_tty_interrupt_dispatch(&hart, &frame);
        panic_armed = false;
        return false;
    }
    panic_armed = false;
    return (
        claim_count == 1
        && !notification_committed
        && failure_reason == MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT
        && tty_state.handoff.route_phase
            == MICROS_TTY_ROUTE_IN_SERVICE
        && tty_state.handoff.claimed_source
            == MICROS_TTY_UART_IRQ_SOURCE
    );
}

int main(void)
{
    return (
        test_spurious_claim_is_observation()
        && test_source_ten_is_retained_and_notified()
        && test_wrong_source_is_fatal_without_completion()
        && test_notification_failure_retains_claim()
    ) ? 0 : 1;
}

const struct micros_bootstrap_control_state *
micros_bootstrap_runtime_state(void)
{
    return &bootstrap;
}

enum micros_bootstrap_error micros_bootstrap_runtime_validate(void)
{
    return MICROS_BOOTSTRAP_OK;
}

_Noreturn void micros_bootstrap_runtime_fail(
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t detail
)
{
    (void)service_id;
    (void)endpoint;
    (void)detail;
    failure_reason = reason;
    if (panic_armed) {
        longjmp(panic_target, 1);
    }
    abort();
}

struct micros_kernel_objects *
micros_kernel_object_runtime_authoritative_registry(void)
{
    return &objects;
}

struct micros_endpoint_registry *
micros_ipc_runtime_authoritative_registry(void)
{
    return &registry;
}

struct micros_tty_handoff_runtime_state *
micros_tty_handoff_runtime_authoritative_state(void)
{
    return &tty_state;
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_validate(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects
)
{
    return (
        state == &bootstrap
        && endpoint_registry == &registry
        && kernel_objects == &objects
        && micros_tty_handoff_validate(&tty_state.handoff)
            == MICROS_TTY_HANDOFF_OK
    ) ? MICROS_TTY_HANDOFF_OK
      : MICROS_TTY_HANDOFF_ERROR_INVARIANT;
}

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_claim(
    uint32_t source,
    struct micros_tty_handoff *candidate
)
{
    if (
        candidate == NULL
        || operation_stage != 1
        || source != MICROS_TTY_UART_IRQ_SOURCE
    ) {
        order_violation = true;
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    *candidate = tty_state.handoff;
    if (
        micros_tty_handoff_claim(candidate, source)
            != MICROS_TTY_HANDOFF_OK
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    operation_stage = 2;
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_claim_prevalidated(
    const struct micros_tty_handoff *candidate
)
{
    if (candidate == NULL || operation_stage != 2) {
        order_violation = true;
        return;
    }
    tty_state.handoff = *candidate;
    operation_stage = 3;
}

enum micros_ipc_error micros_ipc_prepare_kernel_notification(
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects,
    micros_endpoint_t destination,
    uint64_t event_mask,
    struct micros_ipc_kernel_notification_plan *plan
)
{
    if (
        endpoint_registry != &registry
        || kernel_objects != &objects
        || destination != tty_endpoint
        || event_mask != MICROS_KERNEL_EVENT_TTY_IRQ
        || plan == NULL
        || operation_stage != 3
    ) {
        order_violation = true;
        return MICROS_IPC_ERROR_INVARIANT;
    }
    if (notification_should_fail) {
        return MICROS_IPC_ERROR_STATE;
    }
    *plan = (struct micros_ipc_kernel_notification_plan){
        .active = true,
        .event_mask = event_mask,
    };
    notified_endpoint = destination;
    notified_events = event_mask;
    operation_stage = 4;
    return MICROS_IPC_OK;
}

void micros_ipc_commit_kernel_notification_prevalidated(
    struct micros_endpoint_registry *endpoint_registry,
    struct micros_kernel_objects *kernel_objects,
    struct micros_ipc_kernel_notification_plan *plan
)
{
    if (
        endpoint_registry != &registry
        || kernel_objects != &objects
        || plan == NULL
        || !plan->active
        || operation_stage != 4
    ) {
        order_violation = true;
        return;
    }
    notification_committed = true;
    plan->active = false;
    operation_stage = 5;
}

bool micros_plic_validate(enum micros_plic_phase expected_phase)
{
    return expected_phase == MICROS_PLIC_ENABLED;
}

bool micros_plic_claim(uint32_t *source)
{
    if (source == NULL || operation_stage != 0) {
        order_violation = true;
        return false;
    }
    *source = claim_source;
    claim_count += 1;
    operation_stage = 1;
    return true;
}

bool uart_console_handoff_is_active(void)
{
    return true;
}

void micros_tty_owner_fault_record(void)
{
}

enum micros_user_address_space_error
micros_user_address_space_validate_tty_uart_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address
)
{
    return (
        process.slot == tty_process.slot
        && process.generation == tty_process.generation
        && expected_root_physical_address
            == tty_state.root_physical_address
    ) ? MICROS_USER_ADDRESS_SPACE_OK
      : MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
}

_Noreturn void micros_trap_panic_entry(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line,
    const struct micros_trap_frame *trap_frame
)
{
    (void)hart_id;
    (void)reason;
    (void)file;
    (void)line;
    (void)trap_frame;
    if (panic_armed) {
        longjmp(panic_target, 1);
    }
    abort();
}
