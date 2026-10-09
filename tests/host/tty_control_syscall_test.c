#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/plic.h"
#include "kernel/tty_control_syscall.h"
#include "kernel/tty_control_syscall_core.h"
#include "kernel/tty_fault.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/user_address_space_internal.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"

static struct micros_bootstrap_control_state bootstrap;
static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_tty_handoff_runtime_state tty_state;
static struct micros_syscall_context syscall_context;
static enum micros_tty_control_authority_result authority_result;
static enum micros_plic_phase plic_phase;
static bool runtime_valid;
static bool mapping_valid;
static bool uart_active;
static bool irq_enabled;
static bool timer_enabled;
static bool external_enabled;
static bool plic_prepare_valid;
static bool order_violation;
static unsigned int authority_calls;
static unsigned int owner_fault_count;
static int operation_stage;
static jmp_buf panic_target;
static bool panic_armed;

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

static struct micros_syscall_arguments commit_arguments(void)
{
    return (struct micros_syscall_arguments){
        .a0 = MICROS_TTY_CONTROL_COMMIT,
        .a1 = MICROS_TTY_CONTROL_VERSION,
        .a2 = MICROS_TTY_SERVICE_ID,
        .a3 = tty_state.endpoint,
        .a4 = MICROS_TTY_UART_VIRTUAL_BASE,
        .a5 = MICROS_TTY_UART_PHYSICAL_BASE,
        .a6 = (uint64_t)MICROS_TTY_UART_IRQ_SOURCE << 32
            | MICROS_TTY_UART_MAPPED_LENGTH,
        .a7 = MICROS_SYSCALL_ABI_TTY_CONTROL,
    };
}

static struct micros_syscall_arguments complete_arguments(void)
{
    return (struct micros_syscall_arguments){
        .a0 = MICROS_TTY_CONTROL_IRQ_COMPLETE,
        .a1 = MICROS_TTY_CONTROL_VERSION,
        .a2 = MICROS_TTY_UART_IRQ_SOURCE,
        .a7 = MICROS_SYSCALL_ABI_TTY_CONTROL,
    };
}

static void reset_fixture(bool in_service)
{
    memset(&bootstrap, 0, sizeof(bootstrap));
    memset(&registry, 0, sizeof(registry));
    memset(&objects, 0, sizeof(objects));
    tty_state = (struct micros_tty_handoff_runtime_state){
        .service_id = MICROS_TTY_SERVICE_ID,
        .endpoint = UINT32_C(0x00007003),
        .image_id = 104,
        .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
        .process = {
            .slot = MICROS_TTY_PROCESS_SLOT,
            .generation = 7,
        },
        .thread = {
            .slot = 5,
            .generation = 9,
        },
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
        || (
            in_service
            && (
                micros_tty_handoff_commit(&tty_state.handoff)
                    != MICROS_TTY_HANDOFF_OK
                || micros_tty_handoff_claim(
                    &tty_state.handoff,
                    MICROS_TTY_UART_IRQ_SOURCE
                ) != MICROS_TTY_HANDOFF_OK
            )
        )
    ) {
        abort();
    }
    syscall_context = (struct micros_syscall_context){
        .objects = &objects,
        .process = tty_state.process,
        .current = tty_state.thread,
        .hart = {
            .slot = 0,
            .generation = 1,
        },
    };
    authority_result = MICROS_TTY_CONTROL_AUTHORITY_AUTHORIZED;
    plic_phase = in_service
        ? MICROS_PLIC_ENABLED
        : MICROS_PLIC_DISABLED;
    runtime_valid = true;
    mapping_valid = true;
    uart_active = true;
    irq_enabled = false;
    timer_enabled = true;
    external_enabled = in_service;
    plic_prepare_valid = true;
    order_violation = false;
    authority_calls = 0;
    owner_fault_count = 0;
    operation_stage = 0;
    panic_armed = false;
}

static enum micros_syscall_return invoke(
    struct micros_syscall_arguments *arguments,
    struct micros_trap_frame *frame
)
{
    struct micros_hart hart = {
        .hardware_id = 0,
    };

    return micros_tty_control_handle_captured_user_ecall(
        &hart,
        frame,
        &syscall_context,
        arguments
    );
}

static bool test_commit_orders_state_before_delivery(void)
{
    struct micros_syscall_arguments arguments;
    struct micros_trap_frame frame = {0};

    reset_fixture(false);
    arguments = commit_arguments();
    EXPECT_TRUE(
        invoke(&arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_OK
        && tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_OWNED
        && tty_state.handoff.route_phase == MICROS_TTY_ROUTE_IDLE
        && plic_phase == MICROS_PLIC_ENABLED
        && external_enabled
        && timer_enabled
        && !irq_enabled
        && operation_stage == 6
        && !order_violation
        && authority_calls == 1
        && owner_fault_count == 0
    );
    return true;
}

static bool test_completion_orders_hardware_before_idle(void)
{
    struct micros_syscall_arguments arguments;
    struct micros_trap_frame frame = {0};

    reset_fixture(true);
    arguments = complete_arguments();
    EXPECT_TRUE(
        invoke(&arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_OK
        && tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_OWNED
        && tty_state.handoff.route_phase == MICROS_TTY_ROUTE_IDLE
        && tty_state.handoff.claimed_source == 0
        && plic_phase == MICROS_PLIC_ENABLED
        && external_enabled
        && operation_stage == 3
        && !order_violation
        && authority_calls == 1
        && owner_fault_count == 0
    );
    return true;
}

static bool test_returned_failures_preserve_state(void)
{
    struct micros_syscall_arguments arguments;
    struct micros_trap_frame frame = {0};
    struct micros_tty_handoff snapshot;

    reset_fixture(false);
    arguments = commit_arguments();
    snapshot = tty_state.handoff;
    authority_result = MICROS_TTY_CONTROL_AUTHORITY_UNAUTHORIZED;
    EXPECT_TRUE(
        invoke(&arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && memcmp(&tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && plic_phase == MICROS_PLIC_DISABLED
        && !external_enabled
        && operation_stage == 0
        && !order_violation
    );

    reset_fixture(true);
    if (
        micros_tty_handoff_complete(
            &tty_state.handoff,
            MICROS_TTY_UART_IRQ_SOURCE
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        return false;
    }
    arguments = commit_arguments();
    snapshot = tty_state.handoff;
    EXPECT_TRUE(
        invoke(&arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_STATE
        && memcmp(&tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && plic_phase == MICROS_PLIC_ENABLED
        && external_enabled
        && operation_stage == 0
    );

    reset_fixture(false);
    arguments = commit_arguments();
    arguments.a3 += 1;
    snapshot = tty_state.handoff;
    EXPECT_TRUE(
        invoke(&arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_ARGUMENT
        && memcmp(&tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && plic_phase == MICROS_PLIC_DISABLED
        && operation_stage == 1
    );

    reset_fixture(false);
    arguments = commit_arguments();
    arguments.a3 = UINT64_C(1) << 32;
    snapshot = tty_state.handoff;
    EXPECT_TRUE(
        invoke(&arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_ARGUMENT
        && authority_calls == 0
        && memcmp(&tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && operation_stage == 0
    );

    reset_fixture(true);
    if (
        micros_tty_handoff_complete(
            &tty_state.handoff,
            MICROS_TTY_UART_IRQ_SOURCE
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        return false;
    }
    arguments = complete_arguments();
    snapshot = tty_state.handoff;
    EXPECT_TRUE(
        invoke(&arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_STATE
        && memcmp(&tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && operation_stage == 0
    );

    reset_fixture(true);
    arguments = complete_arguments();
    arguments.a2 = 9;
    snapshot = tty_state.handoff;
    EXPECT_TRUE(
        invoke(&arguments, &frame) == MICROS_SYSCALL_RETURN_NORMAL
        && (int64_t)frame.a0 == MICROS_SYSCALL_ABI_ARGUMENT
        && memcmp(&tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && operation_stage == 1
    );
    return true;
}

static bool test_invariant_failure_is_fatal_before_mutation(void)
{
    struct micros_syscall_arguments arguments;
    struct micros_trap_frame frame = {0};
    struct micros_tty_handoff snapshot;

    reset_fixture(false);
    arguments = commit_arguments();
    snapshot = tty_state.handoff;
    plic_prepare_valid = false;
    panic_armed = true;
    if (setjmp(panic_target) == 0) {
        (void)invoke(&arguments, &frame);
        panic_armed = false;
        return false;
    }
    panic_armed = false;
    return (
        owner_fault_count == 1
        && memcmp(&tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && plic_phase == MICROS_PLIC_DISABLED
        && !external_enabled
        && operation_stage == 1
        && !order_violation
    );
}

static bool test_hardware_corruption_precedes_tuple_failure(void)
{
    struct micros_syscall_arguments arguments;
    struct micros_trap_frame frame = {0};
    struct micros_tty_handoff snapshot;

    reset_fixture(false);
    arguments = commit_arguments();
    arguments.a3 += 1;
    snapshot = tty_state.handoff;
    plic_phase = MICROS_PLIC_ENABLED;
    external_enabled = true;
    panic_armed = true;
    if (setjmp(panic_target) == 0) {
        (void)invoke(&arguments, &frame);
        panic_armed = false;
        return false;
    }
    panic_armed = false;
    return (
        owner_fault_count == 1
        && memcmp(&tty_state.handoff, &snapshot, sizeof(snapshot)) == 0
        && operation_stage == 0
        && !order_violation
    );
}

int main(void)
{
    return (
        test_commit_orders_state_before_delivery()
        && test_completion_orders_hardware_before_idle()
        && test_returned_failures_preserve_state()
        && test_invariant_failure_is_fatal_before_mutation()
        && test_hardware_corruption_precedes_tuple_failure()
    ) ? 0 : 1;
}

bool micros_test_irq_is_enabled(void)
{
    return irq_enabled;
}

bool micros_test_timer_interrupt_is_enabled(void)
{
    return timer_enabled;
}

void micros_test_external_interrupt_enable(void)
{
    if (operation_stage != 5) {
        order_violation = true;
        return;
    }
    external_enabled = true;
    operation_stage = 6;
}

bool micros_test_external_interrupt_is_enabled(void)
{
    return external_enabled;
}

const struct micros_bootstrap_control_state *
micros_bootstrap_runtime_state(void)
{
    return &bootstrap;
}

struct micros_bootstrap_control_state *
micros_bootstrap_runtime_authoritative_state(void)
{
    return &bootstrap;
}

enum micros_bootstrap_error micros_bootstrap_runtime_validate(void)
{
    return runtime_valid
        ? MICROS_BOOTSTRAP_OK
        : MICROS_BOOTSTRAP_ERROR_INVARIANT;
}

_Noreturn void micros_bootstrap_runtime_fail(
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t detail
)
{
    (void)reason;
    (void)service_id;
    (void)endpoint;
    (void)detail;
    if (panic_armed) {
        longjmp(panic_target, 1);
    }
    abort();
}

const struct micros_endpoint_registry *micros_ipc_runtime_registry(void)
{
    return &registry;
}

struct micros_endpoint_registry *
micros_ipc_runtime_authoritative_registry(void)
{
    return &registry;
}

const struct micros_kernel_objects *
micros_kernel_object_runtime_registry(void)
{
    return &objects;
}

const struct micros_tty_handoff_runtime_state *
micros_tty_handoff_runtime_state(void)
{
    return &tty_state;
}

struct micros_tty_handoff_runtime_state *
micros_tty_handoff_runtime_authoritative_state(void)
{
    return &tty_state;
}

enum micros_tty_control_authority_result
micros_tty_control_syscall_authority_resolve(
    const struct micros_tty_handoff_runtime_state *state,
    const struct micros_bootstrap_control_state *control,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects,
    const struct micros_syscall_context *context
)
{
    authority_calls += 1;
    return (
        state == &tty_state
        && control == &bootstrap
        && endpoint_registry == &registry
        && kernel_objects == &objects
        && context == &syscall_context
    ) ? authority_result
      : MICROS_TTY_CONTROL_AUTHORITY_INVARIANT;
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_validate(
    const struct micros_bootstrap_control_state *control,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects
)
{
    return (
        runtime_valid
        && control == &bootstrap
        && endpoint_registry == &registry
        && kernel_objects == &objects
        && micros_tty_handoff_validate(&tty_state.handoff)
            == MICROS_TTY_HANDOFF_OK
    ) ? MICROS_TTY_HANDOFF_OK
      : MICROS_TTY_HANDOFF_ERROR_INVARIANT;
}

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_commit(
    struct micros_tty_handoff *candidate
)
{
    if (candidate == NULL || operation_stage != 0) {
        order_violation = true;
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    *candidate = tty_state.handoff;
    if (
        micros_tty_handoff_commit(candidate)
            != MICROS_TTY_HANDOFF_OK
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    operation_stage = 1;
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_console_prevalidated(
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

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_complete(
    uint32_t source,
    struct micros_tty_handoff *candidate
)
{
    if (candidate == NULL || operation_stage != 0) {
        order_violation = true;
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    *candidate = tty_state.handoff;
    if (
        micros_tty_handoff_complete(candidate, source)
            != MICROS_TTY_HANDOFF_OK
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    operation_stage = 1;
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_complete_prevalidated(
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

bool micros_plic_validate(enum micros_plic_phase expected_phase)
{
    return plic_phase == expected_phase;
}

bool micros_plic_prepare_tty_enable(
    struct micros_plic_tty_enable_plan *plan
)
{
    if (
        plan == NULL
        || operation_stage != 1
        || !plic_prepare_valid
    ) {
        return false;
    }
    plan->validation_magic = UINT64_C(1);
    operation_stage = 2;
    return true;
}

void micros_plic_commit_tty_prepare_prevalidated(
    const struct micros_plic_tty_enable_plan *plan
)
{
    if (
        plan == NULL
        || plan->validation_magic != UINT64_C(1)
        || operation_stage != 3
    ) {
        order_violation = true;
        return;
    }
    plic_phase = MICROS_PLIC_PREPARED;
    operation_stage = 4;
}

void micros_plic_commit_tty_enable_prevalidated(
    struct micros_plic_tty_enable_plan *plan
)
{
    if (
        plan == NULL
        || plan->validation_magic != UINT64_C(1)
        || operation_stage != 4
    ) {
        order_violation = true;
        return;
    }
    plan->validation_magic = 0;
    plic_phase = MICROS_PLIC_ENABLED;
    operation_stage = 5;
}

bool micros_plic_complete(uint32_t source)
{
    if (
        source != MICROS_TTY_UART_IRQ_SOURCE
        || plic_phase != MICROS_PLIC_ENABLED
        || operation_stage != 1
    ) {
        order_violation = true;
        return false;
    }
    operation_stage = 2;
    return true;
}

bool uart_console_handoff_is_active(void)
{
    return uart_active;
}

enum micros_user_address_space_error
micros_user_address_space_validate_tty_uart_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address
)
{
    return (
        mapping_valid
        && process.slot == tty_state.process.slot
        && process.generation == tty_state.process.generation
        && expected_root_physical_address
            == tty_state.root_physical_address
    ) ? MICROS_USER_ADDRESS_SPACE_OK
      : MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
}

void micros_tty_owner_fault_record(void)
{
    owner_fault_count += 1;
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
