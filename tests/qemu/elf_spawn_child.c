#include "micros/application_runtime.h"
#include "tests/qemu/elf_spawn_protocol.h"
#include "tests/qemu/elf_spawn_client.h"

static struct micros_elf_spawn_client client;

static _Noreturn void elf_spawn_child_fail(uint64_t detail)
{
    micros_elf_spawn_report(MICROS_ELF_SPAWN_REPORT_FAILURE, detail);
    for (;;) {
        __builtin_trap();
    }
}

int micros_application_main(int argc, char *argv[], char *envp[])
{
    static const uint8_t output[] = "MICROS_ELF_SPAWN_CHILD_IO\n";

    if (!micros_elf_spawn_client_startup(argc, argv, envp, false)) {
        elf_spawn_child_fail(UINT64_C(0x201));
    }
    micros_elf_spawn_report(MICROS_ELF_SPAWN_REPORT_CHILD, MICROS_ELF_SPAWN_CHILD_PID);
    if (!micros_elf_spawn_client_write(&client, 1, output, sizeof(output) - 1)) {
        elf_spawn_child_fail(UINT64_C(0x202));
    }
    if (!micros_elf_spawn_client_read_data(&client)) {
        elf_spawn_child_fail(UINT64_C(0x203));
    }
    return MICROS_ELF_SPAWN_CHILD_EXIT_CODE;
}
