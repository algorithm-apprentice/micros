#include "micros/grant_syscall_core.h"
#include "micros/ipc_abi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

bool micros_grant_syscall_core_test_run(void);

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

static bool request_matches(
    const struct micros_grant_syscall_request *request,
    uint64_t operation,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t offset,
    uintptr_t local_address,
    size_t length,
    uint32_t permissions
)
{
    return (
        request->operation == operation
        && request->endpoint == endpoint
        && request->grant == grant
        && request->offset == offset
        && request->local_address == local_address
        && request->length == length
        && request->permissions == permissions
    );
}

static bool expect_decode_failure(
    struct micros_syscall_arguments arguments
)
{
    struct micros_grant_syscall_request request;
    struct micros_grant_syscall_request sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    request = sentinel;
    return (
        micros_grant_syscall_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
}

bool micros_grant_syscall_core_test_run(void)
{
    static const struct {
        enum micros_grant_error error;
        int64_t result;
    } mappings[] = {
        {MICROS_GRANT_OK, MICROS_SYSCALL_ABI_OK},
        {
            MICROS_GRANT_ERROR_ARGUMENT,
            MICROS_SYSCALL_ABI_ARGUMENT,
        },
        {
            MICROS_GRANT_ERROR_CAPACITY,
            MICROS_SYSCALL_ABI_CAPACITY,
        },
        {
            MICROS_GRANT_ERROR_STALE_GRANT,
            MICROS_SYSCALL_ABI_STALE_GRANT,
        },
        {
            MICROS_GRANT_ERROR_DEAD_ENDPOINT,
            MICROS_SYSCALL_ABI_DEAD_ENDPOINT,
        },
        {
            MICROS_GRANT_ERROR_UNAUTHORIZED,
            MICROS_SYSCALL_ABI_UNAUTHORIZED,
        },
        {MICROS_GRANT_ERROR_RANGE, MICROS_SYSCALL_ABI_RANGE},
        {MICROS_GRANT_ERROR_STATE, MICROS_SYSCALL_ABI_STATE},
        {
            MICROS_GRANT_ERROR_FAULT,
            MICROS_SYSCALL_ABI_MEMORY_FAULT,
        },
    };
    struct micros_syscall_arguments arguments = {0};
    struct micros_grant_syscall_request request;
    uint64_t output;
    size_t index;

    EXPECT_TRUE(
        MICROS_SYSCALL_ABI_SEND == 1
        && MICROS_SYSCALL_ABI_RECEIVE == 2
        && MICROS_SYSCALL_ABI_CALL == 3
        && MICROS_SYSCALL_ABI_REPLY == 4
        && MICROS_SYSCALL_ABI_REPLY_RECEIVE == 5
        && MICROS_SYSCALL_ABI_NOTIFY == 6
        && MICROS_SYSCALL_ABI_GRANT_CREATE == 7
        && MICROS_SYSCALL_ABI_GRANT_REVOKE == 8
        && MICROS_SYSCALL_ABI_GRANT_COPY_FROM == 9
        && MICROS_SYSCALL_ABI_GRANT_COPY_TO == 10
        && (int)MICROS_IPC_ABI_SEND
            == (int)MICROS_SYSCALL_ABI_SEND
        && (int)MICROS_IPC_ABI_MESSAGE_FAULT
            == (int)MICROS_SYSCALL_ABI_MEMORY_FAULT
        && MICROS_SYSCALL_ABI_OK == 0
        && MICROS_SYSCALL_ABI_ARGUMENT == -1
        && MICROS_SYSCALL_ABI_DEAD_ENDPOINT == -2
        && MICROS_SYSCALL_ABI_UNAUTHORIZED == -3
        && MICROS_SYSCALL_ABI_STATE == -4
        && MICROS_SYSCALL_ABI_DEADLOCK == -5
        && MICROS_SYSCALL_ABI_MEMORY_FAULT == -6
        && MICROS_SYSCALL_ABI_REPLY_TOKEN == -7
        && MICROS_SYSCALL_ABI_REPLY_TOKEN_EXHAUSTED == -8
        && MICROS_SYSCALL_ABI_ENDPOINT_CLOSING == -9
        && MICROS_SYSCALL_ABI_CAPACITY == -10
        && MICROS_SYSCALL_ABI_STALE_GRANT == -11
        && MICROS_SYSCALL_ABI_RANGE == -12
        && (int)MICROS_IPC_ABI_OK
            == (int)MICROS_SYSCALL_ABI_OK
        && (int)MICROS_IPC_ABI_ARGUMENT
            == (int)MICROS_SYSCALL_ABI_ARGUMENT
        && (int)MICROS_IPC_ABI_DEAD_ENDPOINT
            == (int)MICROS_SYSCALL_ABI_DEAD_ENDPOINT
        && (int)MICROS_IPC_ABI_UNAUTHORIZED
            == (int)MICROS_SYSCALL_ABI_UNAUTHORIZED
        && (int)MICROS_IPC_ABI_STATE
            == (int)MICROS_SYSCALL_ABI_STATE
        && (int)MICROS_IPC_ABI_DEADLOCK
            == (int)MICROS_SYSCALL_ABI_DEADLOCK
        && (int)MICROS_IPC_ABI_REPLY_TOKEN
            == (int)MICROS_SYSCALL_ABI_REPLY_TOKEN
        && (int)MICROS_IPC_ABI_REPLY_TOKEN_EXHAUSTED
            == (int)MICROS_SYSCALL_ABI_REPLY_TOKEN_EXHAUSTED
        && (int)MICROS_IPC_ABI_ENDPOINT_CLOSING
            == (int)MICROS_SYSCALL_ABI_ENDPOINT_CLOSING
        && (uint64_t)(MICROS_GRANT_NONE - 1) <= INT64_MAX
    );

    for (index = 0; index < sizeof(mappings) / sizeof(mappings[0]); ++index) {
        output = UINT64_C(0xa5a5a5a5a5a5a5a5);
        EXPECT_TRUE(
            micros_grant_abi_map_error(
                mappings[index].error,
                &output
            )
            && output == (uint64_t)mappings[index].result
        );
    }
    for (
        index = MICROS_GRANT_ERROR_ALREADY_INITIALIZED;
        index <= MICROS_GRANT_ERROR_INVARIANT;
        ++index
    ) {
        enum micros_grant_error error =
            (enum micros_grant_error)index;
        bool fatal = (
            error == MICROS_GRANT_ERROR_ALREADY_INITIALIZED
            || error == MICROS_GRANT_ERROR_NOT_INITIALIZED
            || error == MICROS_GRANT_ERROR_PHASE
            || error == MICROS_GRANT_ERROR_INVARIANT
        );

        if (!fatal) {
            continue;
        }
        output = UINT64_C(0x5a5a5a5a5a5a5a5a);
        EXPECT_TRUE(
            !micros_grant_abi_map_error(error, &output)
            && output == UINT64_C(0x5a5a5a5a5a5a5a5a)
        );
    }
    output = UINT64_C(0x5a5a5a5a5a5a5a5a);
    EXPECT_TRUE(
        !micros_grant_abi_map_error(
            (enum micros_grant_error)UINT32_C(0x7fffffff),
            &output
        )
        && output == UINT64_C(0x5a5a5a5a5a5a5a5a)
    );
    EXPECT_TRUE(!micros_grant_abi_map_error(MICROS_GRANT_OK, NULL));

    arguments = (struct micros_syscall_arguments){
        .a0 = UINT32_C(0x00123001),
        .a1 = UINT64_C(0x0000000040001000),
        .a2 = UINT64_C(0x0000000100000001),
        .a3 = MICROS_GRANT_PERMISSION_READ
            | MICROS_GRANT_PERMISSION_WRITE,
        .a7 = MICROS_SYSCALL_ABI_GRANT_CREATE,
    };
    memset(&request, 0, sizeof(request));
    EXPECT_TRUE(
        micros_grant_syscall_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request_matches(
            &request,
            MICROS_SYSCALL_ABI_GRANT_CREATE,
            UINT32_C(0x00123001),
            0,
            0,
            UINT64_C(0x0000000040001000),
            UINT64_C(0x0000000100000001),
            MICROS_GRANT_PERMISSION_READ
                | MICROS_GRANT_PERMISSION_WRITE
        )
    );

    arguments = (struct micros_syscall_arguments){
        .a0 = UINT32_C(0x12345678),
        .a7 = MICROS_SYSCALL_ABI_GRANT_REVOKE,
    };
    EXPECT_TRUE(
        micros_grant_syscall_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request_matches(
            &request,
            MICROS_SYSCALL_ABI_GRANT_REVOKE,
            0,
            UINT32_C(0x12345678),
            0,
            0,
            0,
            0
        )
    );

    arguments = (struct micros_syscall_arguments){
        .a0 = UINT32_C(0x00123001),
        .a1 = UINT32_C(0x12345678),
        .a2 = UINT64_C(0xfedcba9876543210),
        .a3 = UINT64_C(0x000000007ffff000),
        .a4 = UINT64_C(0x0000000100000001),
        .a7 = MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
    };
    EXPECT_TRUE(
        micros_grant_syscall_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request_matches(
            &request,
            MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
            UINT32_C(0x00123001),
            UINT32_C(0x12345678),
            UINT64_C(0xfedcba9876543210),
            UINT64_C(0x000000007ffff000),
            UINT64_C(0x0000000100000001),
            0
        )
    );
    arguments.a7 = MICROS_SYSCALL_ABI_GRANT_COPY_TO;
    EXPECT_TRUE(
        micros_grant_syscall_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.operation == MICROS_SYSCALL_ABI_GRANT_COPY_TO
    );

    arguments.a0 = UINT64_C(1) << 32;
    EXPECT_TRUE(expect_decode_failure(arguments));
    arguments.a0 = UINT32_C(0x00123001);
    arguments.a1 = UINT64_C(1) << 32;
    EXPECT_TRUE(expect_decode_failure(arguments));
    arguments.a1 = UINT32_C(0x12345678);
    arguments.a5 = 1;
    EXPECT_TRUE(expect_decode_failure(arguments));
    arguments.a5 = 0;
    arguments.a6 = 1;
    EXPECT_TRUE(expect_decode_failure(arguments));

    arguments = (struct micros_syscall_arguments){
        .a0 = UINT32_C(0x00123001),
        .a1 = UINT64_C(0x0000000040001000),
        .a2 = 1,
        .a3 = UINT64_C(1) << 32,
        .a7 = MICROS_SYSCALL_ABI_GRANT_CREATE,
    };
    EXPECT_TRUE(expect_decode_failure(arguments));
    arguments.a3 = MICROS_GRANT_PERMISSION_READ;
    arguments.a4 = 1;
    EXPECT_TRUE(expect_decode_failure(arguments));
    arguments.a4 = 0;
    arguments.a5 = 1;
    EXPECT_TRUE(expect_decode_failure(arguments));
    arguments.a5 = 0;
    arguments.a6 = 1;
    EXPECT_TRUE(expect_decode_failure(arguments));

    arguments = (struct micros_syscall_arguments){
        .a0 = UINT32_C(0x12345678),
        .a7 = MICROS_SYSCALL_ABI_GRANT_REVOKE,
    };
    for (index = 1; index <= 6; ++index) {
        switch (index) {
        case 1:
            arguments.a1 = 1;
            break;
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
        EXPECT_TRUE(expect_decode_failure(arguments));
        arguments.a1 = 0;
        arguments.a2 = 0;
        arguments.a3 = 0;
        arguments.a4 = 0;
        arguments.a5 = 0;
        arguments.a6 = 0;
    }
    arguments.a0 = UINT64_C(1) << 32;
    EXPECT_TRUE(expect_decode_failure(arguments));

    arguments = (struct micros_syscall_arguments){
        .a0 = UINT32_C(0x00123001),
        .a1 = UINT32_C(0x12345678),
        .a2 = UINT64_C(0xfedcba9876543210),
        .a3 = UINT64_C(0x000000007ffff000),
        .a4 = 1,
        .a7 = MICROS_SYSCALL_ABI_GRANT_COPY_FROM,
    };
    arguments.a5 = 1;
    EXPECT_TRUE(expect_decode_failure(arguments));
    arguments.a5 = 0;
    arguments.a6 = 1;
    EXPECT_TRUE(expect_decode_failure(arguments));

    arguments = (struct micros_syscall_arguments){0};
    EXPECT_TRUE(
        expect_decode_failure(arguments)
        && expect_decode_failure(
            (struct micros_syscall_arguments){
                .a7 = UINT64_C(11),
            }
        )
        && micros_grant_syscall_decode(NULL, &request)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && micros_grant_syscall_decode(&arguments, NULL)
            == MICROS_SYSCALL_ABI_ARGUMENT
    );
    return true;
}
