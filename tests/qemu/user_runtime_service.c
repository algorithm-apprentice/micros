#include "tests/qemu/user_runtime_service_internal.h"

static uint64_t startup_checks(void)
{
    uintptr_t stack_pointer;
    uintptr_t global_pointer;
    uintptr_t thread_pointer;

    __asm__ volatile("mv %0, sp" : "=r"(stack_pointer));
    __asm__ volatile("mv %0, gp" : "=r"(global_pointer));
    __asm__ volatile("mv %0, tp" : "=r"(thread_pointer));
    if (
        micros_user_runtime_test_config.version
            != MICROS_USER_RUNTIME_TEST_VERSION
        || micros_user_runtime_test_config.role
            < MICROS_USER_RUNTIME_TEST_ROLE_CLIENT
        || micros_user_runtime_test_config.role
            > MICROS_USER_RUNTIME_TEST_ROLE_PEER
        || micros_user_runtime_test_config.client_endpoint
            == MICROS_ENDPOINT_NONE
        || micros_user_runtime_test_config.server_endpoint
            == MICROS_ENDPOINT_NONE
        || micros_user_runtime_test_config.peer_endpoint
            == MICROS_ENDPOINT_NONE
        || global_pointer != 0
        || thread_pointer != 0
    ) {
        return UINT64_C(0x101);
    }
    if (
        stack_pointer % 16 != 0
        || stack_pointer < UINT64_C(0x000000007ffff000)
        || stack_pointer >= UINT64_C(0x0000000080000000)
    ) {
        return UINT64_C(0x102);
    }
    if (
        micros_user_runtime_test_data_sentinel
            != MICROS_USER_RUNTIME_TEST_DATA_SENTINEL
        || micros_user_runtime_test_rodata
            != MICROS_USER_RUNTIME_TEST_RODATA_SENTINEL
        || micros_user_runtime_test_read_region.before
            != MICROS_USER_RUNTIME_TEST_CANARY_BEFORE
        || micros_user_runtime_test_read_region.after
            != MICROS_USER_RUNTIME_TEST_CANARY_AFTER
        || !user_runtime_service_initial_bss_is_zero()
    ) {
        return UINT64_C(0x103);
    }
    if (!user_runtime_service_memory_support_works()) {
        return UINT64_C(0x104);
    }
    return 0;
}

void micros_service_main(void)
{
    uint64_t failure = startup_checks();

    if (
        failure == 0
        && micros_user_runtime_test_config.role
            == MICROS_USER_RUNTIME_TEST_ROLE_SERVER
    ) {
        micros_user_runtime_test_probe_rodata(
            &micros_user_runtime_test_rodata,
            0
        );
    }
    if (failure == 0) {
        micros_user_runtime_test_config.status |=
            MICROS_USER_RUNTIME_TEST_STATUS_STARTUP
            | MICROS_USER_RUNTIME_TEST_STATUS_STACK
            | MICROS_USER_RUNTIME_TEST_STATUS_MEMORY;
        switch (micros_user_runtime_test_config.role) {
        case MICROS_USER_RUNTIME_TEST_ROLE_CLIENT:
            failure = user_runtime_service_run_client();
            break;
        case MICROS_USER_RUNTIME_TEST_ROLE_SERVER:
            failure = user_runtime_service_run_server();
            break;
        case MICROS_USER_RUNTIME_TEST_ROLE_PEER:
            failure = user_runtime_service_run_peer();
            break;
        default:
            failure = UINT64_C(0x501);
            break;
        }
    }
    micros_user_runtime_test_config.failure = failure;
}
