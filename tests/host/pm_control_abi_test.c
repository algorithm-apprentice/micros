#include "tests/host/pm_control_test_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/endpoint.h"
#include "micros/sv39.h"

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
    struct micros_pm_control_request request;
    struct micros_pm_control_request sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    request = sentinel;
    return (
        micros_pm_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
}

static bool test_contract_constants(void)
{
    EXPECT_TRUE(
        MICROS_SYSCALL_ABI_PM_CONTROL == 13
        && MICROS_PM_CONTROL_RESERVE == 1
        && MICROS_PM_CONTROL_ABORT_RESERVED == 2
        && MICROS_PM_CONTROL_PREPARE_EXECUTION == 3
        && MICROS_PM_CONTROL_ABORT_PREPARED == 4
        && MICROS_PM_CONTROL_ACTIVATE == 5
        && MICROS_PM_CONTROL_EXIT_BEGIN == 6
        && MICROS_PM_CONTROL_EXIT_DETACH == 7
        && MICROS_PM_CONTROL_RELEASE_EXITED_PROCESS == 8
        && MICROS_PM_RESERVATION_VERSION == 1
        && MICROS_PM_RESERVATION_SIZE == 32
        && MICROS_PM_PREPARED_VERSION == 1
        && MICROS_PM_PREPARED_SIZE == 32
        && MICROS_PM_EXIT_BEGIN_VERSION == 1
        && MICROS_PM_EXIT_BEGIN_SIZE == 32
        && sizeof(struct micros_pm_prepared_result) == 32
        && offsetof(
            struct micros_pm_prepared_result,
            prepared_token
        ) == 8
        && offsetof(
            struct micros_pm_prepared_result,
            endpoint
        ) == 16
        && sizeof(struct micros_pm_exit_begin_result) == 32
        && offsetof(
            struct micros_pm_exit_begin_result,
            exit_token
        ) == 8
    );
    return true;
}

static bool test_reservation_decodes(void)
{
    struct micros_pm_control_request request;
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_PM_CONTROL_RESERVE,
        .a1 = MICROS_USER_VIRTUAL_BASE + 8,
        .a2 = MICROS_PM_RESERVATION_SIZE,
        .a7 = MICROS_SYSCALL_ABI_PM_CONTROL,
    };

    EXPECT_TRUE(
        micros_pm_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_PM_CONTROL_RESERVE
        && request.output_address == arguments.a1
        && request.transaction == 0
    );
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_PM_CONTROL_ABORT_RESERVED,
        .a1 = UINT64_C(0xfedcba9876543210),
        .a7 = MICROS_SYSCALL_ABI_PM_CONTROL,
    };
    EXPECT_TRUE(
        micros_pm_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_PM_CONTROL_ABORT_RESERVED
        && request.output_address == 0
        && request.transaction == arguments.a1
    );
    return true;
}

static bool test_execution_decodes(void)
{
    struct micros_pm_control_request request;
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_PM_CONTROL_PREPARE_EXECUTION,
        .a1 = UINT64_C(0x10),
        .a2 = UINT64_C(0x20),
        .a3 = UINT64_C(0x30),
        .a4 = MICROS_USER_VIRTUAL_BASE + 8,
        .a5 = MICROS_PM_PREPARED_SIZE,
        .a7 = MICROS_SYSCALL_ABI_PM_CONTROL,
    };

    EXPECT_TRUE(
        micros_pm_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_PM_CONTROL_PREPARE_EXECUTION
        && request.transaction == arguments.a1
        && request.load_token == arguments.a2
        && request.mapping_generation == arguments.a3
        && request.output_address == arguments.a4
        && request.prepared_token == 0
        && request.endpoint == MICROS_ENDPOINT_NONE
    );
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_PM_CONTROL_ABORT_PREPARED,
        .a1 = UINT64_C(0x10),
        .a2 = UINT64_C(0x20),
        .a3 = UINT64_C(0x30),
        .a4 = UINT64_C(0x40),
        .a7 = MICROS_SYSCALL_ABI_PM_CONTROL,
    };
    EXPECT_TRUE(
        micros_pm_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_PM_CONTROL_ABORT_PREPARED
        && request.transaction == arguments.a1
        && request.load_token == arguments.a2
        && request.mapping_generation == arguments.a3
        && request.prepared_token == arguments.a4
        && request.output_address == 0
        && request.endpoint == MICROS_ENDPOINT_NONE
    );
    arguments.a0 = MICROS_PM_CONTROL_ACTIVATE;
    EXPECT_TRUE(
        micros_pm_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_PM_CONTROL_ACTIVATE
        && request.transaction == arguments.a1
        && request.load_token == arguments.a2
        && request.mapping_generation == arguments.a3
        && request.prepared_token == arguments.a4
        && request.output_address == 0
        && request.endpoint == MICROS_ENDPOINT_NONE
    );
    return true;
}

static bool test_exit_decodes(void)
{
    struct micros_pm_control_request request;
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_PM_CONTROL_EXIT_BEGIN,
        .a1 = UINT32_C(0x00123004),
        .a2 = MICROS_USER_VIRTUAL_BASE + 8,
        .a3 = MICROS_PM_EXIT_BEGIN_SIZE,
        .a7 = MICROS_SYSCALL_ABI_PM_CONTROL,
    };

    EXPECT_TRUE(
        micros_pm_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_PM_CONTROL_EXIT_BEGIN
        && request.endpoint == arguments.a1
        && request.output_address == arguments.a2
        && request.transaction == 0
        && request.load_token == 0
        && request.mapping_generation == 0
        && request.prepared_token == 0
    );
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_PM_CONTROL_EXIT_DETACH,
        .a1 = UINT64_C(0x50),
        .a2 = UINT32_C(0x00123004),
        .a7 = MICROS_SYSCALL_ABI_PM_CONTROL,
    };
    EXPECT_TRUE(
        micros_pm_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_PM_CONTROL_EXIT_DETACH
        && request.transaction == arguments.a1
        && request.endpoint == arguments.a2
        && request.output_address == 0
        && request.load_token == 0
        && request.mapping_generation == 0
        && request.prepared_token == 0
    );
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_PM_CONTROL_RELEASE_EXITED_PROCESS,
        .a1 = UINT64_C(0x50),
        .a7 = MICROS_SYSCALL_ABI_PM_CONTROL,
    };
    EXPECT_TRUE(
        micros_pm_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command
            == MICROS_PM_CONTROL_RELEASE_EXITED_PROCESS
        && request.transaction == arguments.a1
        && request.endpoint == MICROS_ENDPOINT_NONE
        && request.output_address == 0
        && request.load_token == 0
        && request.mapping_generation == 0
        && request.prepared_token == 0
    );
    return true;
}

static bool test_reserve_decode_rejections(void)
{
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_PM_CONTROL_RESERVE,
        .a1 = MICROS_USER_VIRTUAL_BASE + 8,
        .a2 = MICROS_PM_RESERVATION_SIZE,
        .a7 = MICROS_SYSCALL_ABI_PM_CONTROL,
    };
    size_t index;

    ++arguments.a1;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    --arguments.a1;
    --arguments.a2;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    arguments.a2 = MICROS_PM_RESERVATION_SIZE;
    for (index = 3; index <= 6; ++index) {
        switch (index) {
        case 3:
            arguments.a3 = 1;
            break;
        case 4:
            arguments.a4 = 1;
            break;
        case 5:
            arguments.a5 = 1;
            break;
        case 6:
            arguments.a6 = 1;
            break;
        default:
            return false;
        }
        EXPECT_TRUE(decode_failure_preserves_request(arguments));
        arguments.a3 = 0;
        arguments.a4 = 0;
        arguments.a5 = 0;
        arguments.a6 = 0;
    }
    arguments.a7 = MICROS_SYSCALL_ABI_VM_HANDOFF;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    return true;
}

static bool test_abort_decode_rejections(void)
{
    struct micros_pm_control_request request;
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_PM_CONTROL_ABORT_RESERVED,
        .a1 = 1,
        .a7 = MICROS_SYSCALL_ABI_PM_CONTROL,
    };
    size_t index;

    for (index = 2; index <= 6; ++index) {
        switch (index) {
        case 2:
            arguments.a2 = 1;
            break;
        case 3:
            arguments.a3 = 1;
            break;
        case 4:
            arguments.a4 = 1;
            break;
        case 5:
            arguments.a5 = 1;
            break;
        case 6:
            arguments.a6 = 1;
            break;
        default:
            return false;
        }
        EXPECT_TRUE(decode_failure_preserves_request(arguments));
        arguments.a2 = 0;
        arguments.a3 = 0;
        arguments.a4 = 0;
        arguments.a5 = 0;
        arguments.a6 = 0;
    }
    arguments.a1 = 0;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    arguments.a0 = 9;
    arguments.a1 = 1;
    EXPECT_TRUE(
        decode_failure_preserves_request(arguments)
        && micros_pm_control_decode(NULL, &request)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && micros_pm_control_decode(&arguments, NULL)
            == MICROS_SYSCALL_ABI_ARGUMENT
    );
    return true;
}

bool test_contract_and_decode(void)
{
    return (
        test_contract_constants()
        && test_reservation_decodes()
        && test_execution_decodes()
        && test_exit_decodes()
        && test_reserve_decode_rejections()
        && test_abort_decode_rejections()
    );
}
