#include "kernel/tests/runtime/user_runtime_test_internal.h"

#include "arch/riscv64/platform.h"

void user_runtime_test_report_success(void)
{
    uart_write(
        "MICROS_USER_RUNTIME_TEST_PASS "
        "elf=freestanding startup=validated syscalls=1-10 "
        "registers=preserved stack=external data=initialized "
        "bss=zero rodata=protected return=trapped cleanup=complete\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );
}

void user_runtime_test_report_failure(
    const struct user_runtime_test_context *runtime,
    uint64_t failure_stage
)
{
    uart_write("MICROS_TEST_FAILURE user-runtime stage=");
    uart_write_hex64(failure_stage);
    uart_write(" actor=");
    uart_write_hex64(runtime->returned_actor);
    uart_write("\n");
}

void user_runtime_test_report_actor(
    const struct user_runtime_test_context *runtime,
    size_t actor
)
{
    struct micros_user_runtime_test_config config;

    if (user_runtime_test_read_configuration(runtime, actor, &config)) {
        uart_write("MICROS_USER_RUNTIME_STATE actor=");
        uart_write_hex64(actor);
        uart_write(" status=");
        uart_write_hex64(config.status);
        uart_write(" failure=");
        uart_write_hex64(config.failure);
        uart_write(" client=");
        uart_write_hex64(config.client_endpoint);
        uart_write(" server=");
        uart_write_hex64(config.server_endpoint);
        uart_write(" peer=");
        uart_write_hex64(config.peer_endpoint);
        uart_write("\n");
    }
}

_Noreturn void user_runtime_test_report_failure_shutdown(void)
{
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}

_Noreturn void user_runtime_test_report_setup_failure(void)
{
    uart_write("MICROS_TEST_FAILURE user-runtime-setup\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
