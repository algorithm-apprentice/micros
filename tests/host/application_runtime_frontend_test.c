#include "tests/host/application_runtime_test_internal.h"

#include <limits.h>
#include <setjmp.h>
#include <string.h>

#include "lib/runtime/raw_syscall.h"
#include "micros/pm.h"
#include "micros/runtime.h"

enum frontend_stop {
    FRONTEND_STARTUP_FAILED = 1,
    FRONTEND_EXIT_RETURNED,
};

struct frontend_test_state {
    jmp_buf escape;
    struct micros_ipc_message message;
    struct micros_application_stack_values values;
    uint64_t arguments_address;
    uint64_t environment_address;
    uint32_t main_calls;
    uint32_t syscall_calls;
    int32_t main_result;
    int64_t syscall_result;
    int64_t diagnostic;
    enum frontend_stop stopped;
    bool main_arguments_valid;
    bool syscall_arguments_valid;
};

static struct frontend_test_state frontend;

static void prepare_frontend(const struct application_runtime_fixture *fixture)
{
    memset(&frontend, 0, sizeof(frontend));
    frontend.values = fixture->values;
    frontend.arguments_address =
        MICROS_APPLICATION_STACK_BASE + fixture->plan.arguments_offset;
    frontend.environment_address =
        MICROS_APPLICATION_STACK_BASE + fixture->plan.environment_offset;
    micros_application_pm_endpoint = UINT32_C(0xa5a50002);
    micros_application_vfs_endpoint = UINT32_C(0xa5a50003);
    micros_application_pid = UINT64_C(0xa5a50000feed0007);
    micros_application_page_size = UINT64_C(0xa5a5000012340000);
}

int micros_application_main(int argc, char *argv[], char *envp[])
{
    ++frontend.main_calls;
    frontend.main_arguments_valid = (
        argc == 2
        && (uint64_t)(uintptr_t)argv == frontend.arguments_address
        && (uint64_t)(uintptr_t)envp == frontend.environment_address
        && micros_application_pm_endpoint == frontend.values.pm_endpoint
        && micros_application_vfs_endpoint == frontend.values.vfs_endpoint
        && micros_application_pid == frontend.values.pid
        && micros_application_page_size == MICROS_APPLICATION_STACK_PAGE_SIZE
    );
    return frontend.main_result;
}

int64_t micros_runtime_raw_syscall(
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5,
    uint64_t a6,
    uint64_t a7
)
{
    ++frontend.syscall_calls;
    frontend.syscall_arguments_valid = (
        a0 == frontend.values.pm_endpoint && a1 != 0
        && a2 == 0 && a3 == 0 && a4 == 0 && a5 == 0 && a6 == 0
        && a7 == MICROS_SYSCALL_ABI_CALL
    );
    if (a1 != 0) {
        memcpy(&frontend.message, (const void *)(uintptr_t)a1, sizeof(frontend.message));
    }
    return frontend.syscall_result;
}

_Noreturn void micros_application_startup_failed(int64_t error)
{
    frontend.stopped = FRONTEND_STARTUP_FAILED;
    frontend.diagnostic = error;
    longjmp(frontend.escape, 1);
}

_Noreturn void micros_application_exit_returned(int64_t result)
{
    frontend.stopped = FRONTEND_EXIT_RETURNED;
    frontend.diagnostic = result;
    longjmp(frontend.escape, 1);
}

static bool exit_wire_is_exact(int32_t code)
{
    struct micros_ipc_message expected = {0};

    expected.type = MICROS_PM_MESSAGE_EXIT;
    spawn_abi_test_write_u32_le(&expected.payload[0], MICROS_PM_PROTOCOL_VERSION);
    spawn_abi_test_write_u32_le(&expected.payload[8], (uint32_t)code);
    EXPECT_TRUE(
        frontend.syscall_calls == 1
        && frontend.syscall_arguments_valid
        && memcmp(&frontend.message, &expected, sizeof(expected)) == 0
    );
    return true;
}

bool test_application_frontend_valid(void)
{
    static const int32_t codes[] = {INT32_MIN, -1234567, 0, 37, INT32_MAX};
    struct application_runtime_fixture fixture;
    size_t index;

    EXPECT_TRUE(application_test_prepare_stack(&fixture, 2, 1));
    for (index = 0; index < sizeof(codes) / sizeof(codes[0]); ++index) {
        prepare_frontend(&fixture);
        frontend.main_result = codes[index];
        if (setjmp(frontend.escape) == 0) {
            micros_application_start_from_page(
                fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer
            );
        }
        EXPECT_TRUE(
            frontend.main_calls == 1 && frontend.main_arguments_valid
            && frontend.stopped == FRONTEND_EXIT_RETURNED && frontend.diagnostic == 0
        );
        EXPECT_TRUE(exit_wire_is_exact(codes[index]));
    }
    return true;
}

static bool startup_publication_is_untouched(void)
{
    EXPECT_TRUE(
        frontend.main_calls == 0 && frontend.syscall_calls == 0
        && frontend.stopped == FRONTEND_STARTUP_FAILED
        && micros_application_pm_endpoint == UINT32_C(0xa5a50002)
        && micros_application_vfs_endpoint == UINT32_C(0xa5a50003)
        && micros_application_pid == UINT64_C(0xa5a50000feed0007)
        && micros_application_page_size == UINT64_C(0xa5a5000012340000)
    );
    return true;
}

bool test_application_frontend_invalid(void)
{
    struct application_runtime_fixture fixture;

    EXPECT_TRUE(application_test_prepare_stack(&fixture, 2, 1));
    fixture.page[fixture.plan.argc_offset] = 0;
    prepare_frontend(&fixture);
    if (setjmp(frontend.escape) == 0) {
        micros_application_start_from_page(
            fixture.page, sizeof(fixture.page), fixture.plan.stack_pointer
        );
    }
    EXPECT_TRUE(startup_publication_is_untouched());
    EXPECT_TRUE(frontend.diagnostic == MICROS_APPLICATION_STARTUP_ERROR_MALFORMED);
    return true;
}

bool test_application_exit_return_failure(void)
{
    static const int64_t results[] = {0, -1, -5, INT64_MIN, INT64_MAX};
    struct application_runtime_fixture fixture;
    size_t index;

    EXPECT_TRUE(application_test_prepare_stack(&fixture, 2, 1));
    for (index = 0; index < sizeof(results) / sizeof(results[0]); ++index) {
        prepare_frontend(&fixture);
        micros_application_pm_endpoint = fixture.values.pm_endpoint;
        frontend.syscall_result = results[index];
        if (setjmp(frontend.escape) == 0) {
            micros_application_exit(INT32_MIN);
        }
        EXPECT_TRUE(
            frontend.main_calls == 0
            && frontend.stopped == FRONTEND_EXIT_RETURNED
            && frontend.diagnostic == results[index]
        );
        EXPECT_TRUE(exit_wire_is_exact(INT32_MIN));
    }
    return true;
}

bool test_application_frontend_argument_errors(void)
{
    struct application_runtime_fixture fixture;

    EXPECT_TRUE(application_test_prepare_stack(&fixture, 2, 1));
    prepare_frontend(&fixture);
    if (setjmp(frontend.escape) == 0) {
        micros_application_start_from_page(NULL, sizeof(fixture.page), fixture.plan.stack_pointer);
    }
    EXPECT_TRUE(startup_publication_is_untouched());
    EXPECT_TRUE(frontend.diagnostic == MICROS_APPLICATION_STARTUP_ERROR_ARGUMENT);
    return true;
}
