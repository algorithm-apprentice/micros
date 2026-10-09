#include "kernel/tty_control_core.h"
#include "kernel/tty_control_syscall_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

bool micros_tty_control_test_run(void);

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

static bool decode_failure_preserves_request(
    struct micros_syscall_arguments arguments
)
{
    struct micros_tty_control_request request;
    struct micros_tty_control_request sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    request = sentinel;
    return (
        micros_tty_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
}

static bool test_contract_and_decode(void)
{
    const micros_endpoint_t endpoint = UINT32_C(0x00007003);
    struct micros_tty_control_request request;
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_TTY_CONTROL_COMMIT,
        .a1 = MICROS_TTY_CONTROL_VERSION,
        .a2 = MICROS_TTY_SERVICE_ID,
        .a3 = endpoint,
        .a4 = MICROS_TTY_UART_VIRTUAL_BASE,
        .a5 = MICROS_TTY_UART_PHYSICAL_BASE,
        .a6 = (uint64_t)MICROS_TTY_UART_IRQ_SOURCE << 32
            | MICROS_TTY_UART_MAPPED_LENGTH,
        .a7 = MICROS_SYSCALL_ABI_TTY_CONTROL,
    };

    EXPECT_TRUE(
        MICROS_SYSCALL_ABI_TTY_CONTROL == 14
        && MICROS_TTY_CONTROL_COMMIT == 1
        && MICROS_TTY_CONTROL_IRQ_COMPLETE == 2
        && MICROS_TTY_CONTROL_VERSION == 1
    );
    EXPECT_TRUE(
        micros_tty_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_TTY_CONTROL_COMMIT
        && request.version == MICROS_TTY_CONTROL_VERSION
        && request.service_id == MICROS_TTY_SERVICE_ID
        && request.endpoint == endpoint
        && request.virtual_base == MICROS_TTY_UART_VIRTUAL_BASE
        && request.physical_base
            == MICROS_TTY_UART_PHYSICAL_BASE
        && request.mapped_length
            == MICROS_TTY_UART_MAPPED_LENGTH
        && request.irq_source == MICROS_TTY_UART_IRQ_SOURCE
        && micros_tty_control_commit_tuple_matches(
            &request,
            MICROS_TTY_SERVICE_ID,
            endpoint
        )
    );
    request.version += 1;
    EXPECT_TRUE(
        !micros_tty_control_commit_tuple_matches(
            &request,
            MICROS_TTY_SERVICE_ID,
            endpoint
        )
    );

    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_TTY_CONTROL_IRQ_COMPLETE,
        .a1 = MICROS_TTY_CONTROL_VERSION,
        .a2 = MICROS_TTY_UART_IRQ_SOURCE,
        .a7 = MICROS_SYSCALL_ABI_TTY_CONTROL,
    };
    EXPECT_TRUE(
        micros_tty_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_TTY_CONTROL_IRQ_COMPLETE
        && request.version == MICROS_TTY_CONTROL_VERSION
        && request.irq_source == MICROS_TTY_UART_IRQ_SOURCE
        && micros_tty_control_complete_tuple_matches(&request)
    );
    request.irq_source -= 1;
    EXPECT_TRUE(
        !micros_tty_control_complete_tuple_matches(&request)
    );
    return true;
}

static bool test_commit_width_rejection(void)
{
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_TTY_CONTROL_COMMIT,
        .a1 = MICROS_TTY_CONTROL_VERSION,
        .a2 = MICROS_TTY_SERVICE_ID,
        .a3 = UINT32_C(0x00007003),
        .a4 = MICROS_TTY_UART_VIRTUAL_BASE,
        .a5 = MICROS_TTY_UART_PHYSICAL_BASE,
        .a6 = (uint64_t)MICROS_TTY_UART_IRQ_SOURCE << 32
            | MICROS_TTY_UART_MAPPED_LENGTH,
        .a7 = MICROS_SYSCALL_ABI_TTY_CONTROL,
    };

    arguments.a0 |= UINT64_C(1) << 32;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    arguments.a0 = MICROS_TTY_CONTROL_COMMIT;
    arguments.a1 |= UINT64_C(1) << 32;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    arguments.a1 = MICROS_TTY_CONTROL_VERSION;
    arguments.a2 |= UINT64_C(1) << 32;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    arguments.a2 = MICROS_TTY_SERVICE_ID;
    arguments.a3 |= UINT64_C(1) << 32;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    arguments.a3 = UINT32_C(0x00007003);
    arguments.a0 = 3;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    arguments.a0 = MICROS_TTY_CONTROL_COMMIT;
    arguments.a7 = MICROS_SYSCALL_ABI_PM_CONTROL;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    return true;
}

static bool test_complete_shape_rejection(void)
{
    struct micros_tty_control_request request;
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_TTY_CONTROL_IRQ_COMPLETE,
        .a1 = MICROS_TTY_CONTROL_VERSION,
        .a2 = MICROS_TTY_UART_IRQ_SOURCE,
        .a7 = MICROS_SYSCALL_ABI_TTY_CONTROL,
    };

    arguments.a3 = 1;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    arguments.a3 = 0;
    arguments.a4 = 1;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    arguments.a4 = 0;
    arguments.a5 = 1;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    arguments.a5 = 0;
    arguments.a6 = 1;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    arguments.a6 = 0;
    arguments.a2 |= UINT64_C(1) << 32;
    EXPECT_TRUE(
        decode_failure_preserves_request(arguments)
        && micros_tty_control_decode(NULL, &request)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && micros_tty_control_decode(&arguments, NULL)
            == MICROS_SYSCALL_ABI_ARGUMENT
    );
    return true;
}

static bool test_syscall_authority(void)
{
    const struct micros_process_handle process = {3, 7};
    const struct micros_thread_handle thread_handle = {5, 9};
    const micros_endpoint_t endpoint_value =
        (process.generation << MICROS_ENDPOINT_SLOT_BITS)
        | process.slot;
    struct micros_tty_handoff_runtime_state state = {
        .service_id = MICROS_TTY_SERVICE_ID,
        .endpoint = endpoint_value,
        .image_id = 104,
        .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
        .process = process,
        .thread = thread_handle,
        .root_physical_address = UINT64_C(0x80200000),
    };
    struct micros_bootstrap_binding binding = {
        .manifest_index = 0,
        .service_id = MICROS_TTY_SERVICE_ID,
        .process = process,
        .thread = thread_handle,
        .root = UINT64_C(0x80200000),
        .endpoint = endpoint_value,
    };
    struct micros_bootstrap_manifest_entry entry = {
        .service_id = MICROS_TTY_SERVICE_ID,
        .image_id = 104,
        .process_slot = MICROS_TTY_PROCESS_SLOT,
        .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
        .role_flags = MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER,
        .device_base = MICROS_TTY_UART_PHYSICAL_BASE,
        .device_length = MICROS_TTY_UART_MAPPED_LENGTH,
        .irq_source = MICROS_TTY_UART_IRQ_SOURCE,
    };
    struct micros_syscall_context context = {
        .current = thread_handle,
        .process = process,
    };
    struct micros_process caller_process = {
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = process.generation,
        .address_space_root = UINT64_C(0x80200000),
        .primary_endpoint = endpoint_value,
        .privilege_profile = MICROS_PRIVILEGE_PROFILE_TTY,
    };
    struct micros_thread caller_thread = {
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = thread_handle.generation,
        .owner = process,
    };
    struct micros_endpoint_record endpoint = {
        .state = MICROS_ENDPOINT_STATE_ACTIVE,
        .owner = process,
        .value = endpoint_value,
    };
    struct micros_privilege_profile profile = {
        .id = MICROS_PRIVILEGE_PROFILE_TTY,
        .kernel_operations = MICROS_KERNEL_OPERATION_TTY_CONTROL,
    };

    EXPECT_TRUE(
        micros_tty_control_syscall_authority_classify(
            &state,
            1,
            MICROS_TTY_SERVICE_ID,
            &binding,
            &entry,
            &context,
            &caller_process,
            &caller_thread,
            &endpoint,
            &profile
        ) == MICROS_TTY_CONTROL_AUTHORITY_AUTHORIZED
    );
    context.current.slot += 1;
    EXPECT_TRUE(
        micros_tty_control_syscall_authority_classify(
            &state,
            1,
            MICROS_TTY_SERVICE_ID,
            &binding,
            &entry,
            &context,
            &caller_process,
            &caller_thread,
            &endpoint,
            &profile
        ) == MICROS_TTY_CONTROL_AUTHORITY_UNAUTHORIZED
    );
    context.current = thread_handle;
    caller_thread.slot_state = MICROS_KERNEL_OBJECT_SLOT_FREE;
    EXPECT_TRUE(
        micros_tty_control_syscall_authority_classify(
            &state,
            1,
            MICROS_TTY_SERVICE_ID,
            &binding,
            &entry,
            &context,
            &caller_process,
            &caller_thread,
            &endpoint,
            &profile
        ) == MICROS_TTY_CONTROL_AUTHORITY_INVARIANT
    );
    caller_thread.slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE;
    profile.kernel_operations = MICROS_KERNEL_OPERATION_PM_CONTROL;
    EXPECT_TRUE(
        micros_tty_control_syscall_authority_classify(
            &state,
            1,
            MICROS_TTY_SERVICE_ID,
            &binding,
            &entry,
            &context,
            &caller_process,
            &caller_thread,
            &endpoint,
            &profile
        ) == MICROS_TTY_CONTROL_AUTHORITY_INVARIANT
    );
    profile.kernel_operations = MICROS_KERNEL_OPERATION_TTY_CONTROL;
    entry.irq_source -= 1;
    EXPECT_TRUE(
        micros_tty_control_syscall_authority_classify(
            &state,
            1,
            MICROS_TTY_SERVICE_ID,
            &binding,
            &entry,
            &context,
            &caller_process,
            &caller_thread,
            &endpoint,
            &profile
        ) == MICROS_TTY_CONTROL_AUTHORITY_INVARIANT
    );
    return true;
}

static bool test_no_console_authority(void)
{
    struct micros_tty_handoff_runtime_state state = {0};

    EXPECT_TRUE(
        micros_tty_control_syscall_presence_classify(
            NULL,
            1,
            0
        ) == MICROS_TTY_CONTROL_AUTHORITY_UNAUTHORIZED
    );
    EXPECT_TRUE(
        micros_tty_control_syscall_presence_classify(
            NULL,
            1,
            MICROS_TTY_SERVICE_ID
        ) == MICROS_TTY_CONTROL_AUTHORITY_INVARIANT
        && micros_tty_control_syscall_presence_classify(
            &state,
            1,
            0
        ) == MICROS_TTY_CONTROL_AUTHORITY_INVARIANT
        && micros_tty_control_syscall_presence_classify(
            &state,
            1,
            MICROS_TTY_SERVICE_ID
        ) == MICROS_TTY_CONTROL_AUTHORITY_AUTHORIZED
    );
    return true;
}

bool micros_tty_control_test_run(void)
{
    return (
        test_contract_and_decode()
        && test_commit_width_rejection()
        && test_complete_shape_rejection()
        && test_syscall_authority()
        && test_no_console_authority()
    );
}
