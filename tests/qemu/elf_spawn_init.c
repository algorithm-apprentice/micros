#include "micros/application_runtime.h"
#include "tests/qemu/elf_spawn_protocol.h"
#include "tests/qemu/elf_spawn_client.h"

static struct micros_elf_spawn_client client;

static _Noreturn void elf_spawn_init_fail(uint64_t detail)
{
    micros_elf_spawn_report(MICROS_ELF_SPAWN_REPORT_FAILURE, detail);
    for (;;) {
        __builtin_trap();
    }
}

static void elf_spawn_parent_rollback(void)
{
    struct micros_elf_spawn_pm_result result;

    if (
        !micros_elf_spawn_client_spawn(
            &client, MICROS_ELF_SPAWN_REQUEST_MISSING_DESCRIPTOR, &result
        )
        || result.result != MICROS_PM_RESULT_DESCRIPTOR || result.pid != 0
        || result.exit_kind != MICROS_PM_EXIT_NONE || result.exit_code != 0
    ) {
        elf_spawn_init_fail(UINT64_C(0x102));
    }
    micros_elf_spawn_report(MICROS_ELF_SPAWN_REPORT_ROLLBACK, MICROS_ELF_SPAWN_FAILED_PID);
}

static void elf_spawn_parent_descriptors(void)
{
    static const uint8_t output[] = "MICROS_ELF_SPAWN_PARENT_IO_READY\n";

    if (!micros_elf_spawn_client_write(&client, 1, output, sizeof(output) - 1)) {
        elf_spawn_init_fail(UINT64_C(0x103));
    }
    if (!micros_elf_spawn_client_write(&client, 2, output, sizeof(output) - 1)) {
        elf_spawn_init_fail(UINT64_C(0x104));
    }
}

static void elf_spawn_parent_waits(void)
{
    struct micros_elf_spawn_pm_result result;

    if (
        !micros_elf_spawn_client_spawn(&client, MICROS_ELF_SPAWN_REQUEST_CHILD, &result)
        || result.result != MICROS_PM_RESULT_OK || result.pid != MICROS_ELF_SPAWN_CHILD_PID
    ) {
        elf_spawn_init_fail(UINT64_C(0x105));
    }
    if (
        !micros_elf_spawn_client_wait(MICROS_ELF_SPAWN_CHILD_PID, &result)
        || result.result != MICROS_PM_RESULT_OK
        || result.pid != MICROS_ELF_SPAWN_CHILD_PID
        || result.exit_kind != MICROS_PM_EXIT_NORMAL
        || result.exit_code != MICROS_ELF_SPAWN_CHILD_EXIT_CODE
    ) {
        elf_spawn_init_fail(UINT64_C(0x106));
    }
    if (
        !micros_elf_spawn_client_wait(MICROS_ELF_SPAWN_CHILD_PID, &result)
        || result.result != MICROS_PM_RESULT_NO_CHILD || result.pid != 0
        || result.exit_kind != MICROS_PM_EXIT_NONE || result.exit_code != 0
    ) {
        elf_spawn_init_fail(UINT64_C(0x107));
    }
}

int micros_application_main(int argc, char *argv[], char *envp[])
{
    static const uint8_t pass[] = MICROS_ELF_SPAWN_PASS;

    if (!micros_elf_spawn_client_startup(argc, argv, envp, true)) {
        elf_spawn_init_fail(UINT64_C(0x101));
    }
    micros_elf_spawn_report(MICROS_ELF_SPAWN_REPORT_BASELINE, MICROS_ELF_SPAWN_INIT_PID);
    elf_spawn_parent_rollback();
    elf_spawn_parent_descriptors();
    elf_spawn_parent_waits();
    if (!micros_elf_spawn_client_write(&client, 1, pass, sizeof(pass) - 1)) {
        elf_spawn_init_fail(UINT64_C(0x108));
    }
    if (!micros_elf_spawn_client_drain()) {
        elf_spawn_init_fail(UINT64_C(0x109));
    }
    micros_elf_spawn_report(MICROS_ELF_SPAWN_REPORT_FINAL, MICROS_ELF_SPAWN_CHILD_PID);
    elf_spawn_init_fail(UINT64_C(0x10a));
}
