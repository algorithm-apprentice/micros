#include "bootstrap_control_test_internal.h"

static bool decode_preserves_output_on_error(
    struct micros_syscall_arguments arguments
)
{
    struct micros_bootstrap_control_request request;
    struct micros_bootstrap_control_request sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    request = sentinel;
    return (
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
}

static bool failure_detail_result(
    struct micros_syscall_arguments arguments,
    enum micros_syscall_abi_result expected
)
{
    struct micros_bootstrap_control_request request;

    return (
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && micros_bootstrap_control_validate_failure_detail(
            &request
        ) == expected
    );
}

bool test_valid_commands(void)
{
    struct micros_bootstrap_control_request request;
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_BOOTSTRAP_COMMAND_RELEASE,
        .a1 = 2,
        .a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL,
    };

    EXPECT_TRUE(
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_BOOTSTRAP_COMMAND_RELEASE
        && request.service_id == 2
        && request.endpoint == 0
        && request.reply_token == 0
    );
    arguments.a0 = MICROS_BOOTSTRAP_COMMAND_ACCEPT_READY;
    arguments.a2 = UINT32_C(0x12345678);
    arguments.a3 = UINT64_C(0xfedcba9876543210);
    EXPECT_TRUE(
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command
            == MICROS_BOOTSTRAP_COMMAND_ACCEPT_READY
        && request.service_id == 2
        && request.endpoint == UINT32_C(0x12345678)
        && request.reply_token
            == UINT64_C(0xfedcba9876543210)
    );
    arguments.a0 = MICROS_BOOTSTRAP_COMMAND_FAIL;
    arguments.a1 = 0;
    arguments.a2 = MICROS_ENDPOINT_NONE;
    arguments.a3 = MICROS_BOOTSTRAP_FAILURE_AUTHORITY;
    EXPECT_TRUE(
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_BOOTSTRAP_COMMAND_FAIL
        && request.service_id == 0
        && request.endpoint == MICROS_ENDPOINT_NONE
        && request.failure_reason
            == MICROS_BOOTSTRAP_FAILURE_AUTHORITY
        && request.failure_detail == 0
        && micros_bootstrap_control_validate_failure_detail(
            &request
        ) == MICROS_SYSCALL_ABI_OK
    );
    arguments.a1 = 2;
    arguments.a2 = UINT32_C(0x12345678);
    arguments.a3 = MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED;
    arguments.a4 = 7;
    EXPECT_TRUE(
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.failure_detail == 7
        && micros_bootstrap_control_validate_failure_detail(
            &request
        ) == MICROS_SYSCALL_ABI_OK
    );
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_BOOTSTRAP_COMMAND_COMPLETE,
        .a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL,
    };
    EXPECT_TRUE(
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_BOOTSTRAP_COMMAND_COMPLETE
    );
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_BOOTSTRAP_COMMAND_CONSOLE_BEGIN,
        .a1 = MICROS_TTY_SERVICE_ID,
        .a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL,
    };
    EXPECT_TRUE(
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command
            == MICROS_BOOTSTRAP_COMMAND_CONSOLE_BEGIN
        && request.service_id == MICROS_TTY_SERVICE_ID
        && request.endpoint == 0
        && request.reply_token == 0
    );
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_BOOTSTRAP_COMMAND_FAIL,
        .a1 = MICROS_TTY_SERVICE_ID,
        .a2 = MICROS_ENDPOINT_NONE,
        .a3 = MICROS_BOOTSTRAP_FAILURE_CONSOLE_PROTOCOL,
        .a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL,
    };
    EXPECT_TRUE(
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && micros_bootstrap_control_validate_failure_detail(
            &request
        ) == MICROS_SYSCALL_ABI_OK
    );
    return true;
}

static bool reject_register_shapes(struct micros_syscall_arguments *arguments)
{
    EXPECT_TRUE(
        decode_preserves_output_on_error(
            (struct micros_syscall_arguments){0}
        )
    );
    arguments->a0 = UINT64_C(1) << 32;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a0 = MICROS_BOOTSTRAP_COMMAND_RELEASE;
    arguments->a1 = UINT64_C(1) << 32;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a1 = 2;
    arguments->a2 = UINT64_C(1) << 32;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a2 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a2 = 0;
    arguments->a3 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a3 = 0;
    arguments->a4 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a4 = 0;
    arguments->a5 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a5 = 0;
    arguments->a6 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a6 = 0;
    arguments->a7 = 10;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    return true;
}

static bool reject_failure_details(struct micros_syscall_arguments *arguments)
{
    *arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_BOOTSTRAP_COMMAND_FAIL,
        .a2 = MICROS_ENDPOINT_NONE,
        .a3 = UINT64_C(1) << 32,
        .a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL,
    };
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a3 = MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED;
    arguments->a4 = UINT64_C(1) << 32;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a3 = 0;
    arguments->a4 = 0;
    EXPECT_TRUE(
        failure_detail_result(
            *arguments,
            MICROS_SYSCALL_ABI_ARGUMENT
        )
    );
    arguments->a3 = MICROS_BOOTSTRAP_FAILURE_CONSOLE_PROTOCOL + 1;
    EXPECT_TRUE(
        failure_detail_result(
            *arguments,
            MICROS_SYSCALL_ABI_ARGUMENT
        )
    );
    arguments->a3 = MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED;
    for (arguments->a4 = 1; arguments->a4 <= 9; ++arguments->a4) {
        EXPECT_TRUE(
            failure_detail_result(
                *arguments,
                MICROS_SYSCALL_ABI_OK
            )
        );
    }
    arguments->a4 = 0;
    EXPECT_TRUE(
        failure_detail_result(
            *arguments,
            MICROS_SYSCALL_ABI_ARGUMENT
        )
    );
    arguments->a4 = 10;
    EXPECT_TRUE(
        failure_detail_result(
            *arguments,
            MICROS_SYSCALL_ABI_ARGUMENT
        )
    );
    arguments->a3 = MICROS_BOOTSTRAP_FAILURE_AUTHORITY;
    arguments->a4 = 1;
    EXPECT_TRUE(
        failure_detail_result(
            *arguments,
            MICROS_SYSCALL_ABI_ARGUMENT
        )
    );
    return true;
}

static bool reject_completion_and_console_shapes(
    struct micros_syscall_arguments *arguments,
    struct micros_bootstrap_control_request *request
)
{
    *arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_BOOTSTRAP_COMMAND_COMPLETE,
        .a1 = 1,
        .a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL,
    };
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a1 = 0;
    arguments->a2 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a2 = 0;
    arguments->a3 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a3 = 0;
    arguments->a4 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a0 = MICROS_BOOTSTRAP_COMMAND_CONSOLE_BEGIN;
    arguments->a4 = 0;
    arguments->a1 = MICROS_TTY_SERVICE_ID;
    EXPECT_TRUE(
        micros_bootstrap_control_decode(arguments, request)
            == MICROS_SYSCALL_ABI_OK
    );
    arguments->a2 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a2 = 0;
    arguments->a3 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a3 = 0;
    arguments->a4 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a4 = 0;
    arguments->a5 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a5 = 0;
    arguments->a6 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a6 = 0;
    arguments->a1 = 0;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    arguments->a1 = MICROS_TTY_SERVICE_ID;
    arguments->a0 = MICROS_BOOTSTRAP_COMMAND_CONSOLE_BEGIN + 1;
    EXPECT_TRUE(decode_preserves_output_on_error(*arguments));
    return true;
}

bool test_malformed_commands(void)
{
    struct micros_bootstrap_control_request request;
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_BOOTSTRAP_COMMAND_RELEASE,
        .a1 = 2,
        .a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL,
    };

    return (
        reject_register_shapes(&arguments)
        && reject_failure_details(&arguments)
        && reject_completion_and_console_shapes(&arguments, &request)
    );
}
