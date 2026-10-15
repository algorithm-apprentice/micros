#ifndef MICROS_TESTS_QEMU_ELF_SPAWN_PROTOCOL_H
#define MICROS_TESTS_QEMU_ELF_SPAWN_PROTOCOL_H

#include <stdint.h>

enum {
    MICROS_ELF_SPAWN_SERVICE_COUNT = 6,
    MICROS_ELF_SPAWN_LAUNCHER_SERVICE_ID = 1,
    MICROS_ELF_SPAWN_VM_SERVICE_ID = 2,
    MICROS_ELF_SPAWN_PM_SERVICE_ID = 3,
    MICROS_ELF_SPAWN_TTY_SERVICE_ID = 4,
    MICROS_ELF_SPAWN_RAMFS_SERVICE_ID = 5,
    MICROS_ELF_SPAWN_VFS_SERVICE_ID = 6,
    MICROS_ELF_SPAWN_REQUEST_BUFFER_SIZE = 512,
    MICROS_ELF_SPAWN_INIT_PID = 1,
    MICROS_ELF_SPAWN_FAILED_PID = 2,
    MICROS_ELF_SPAWN_CHILD_PID = 3,
    MICROS_ELF_SPAWN_CHILD_EXIT_CODE = 37,
};

enum micros_elf_spawn_report_stage {
    MICROS_ELF_SPAWN_REPORT_BASELINE = 1,
    MICROS_ELF_SPAWN_REPORT_ROLLBACK,
    MICROS_ELF_SPAWN_REPORT_CHILD,
    MICROS_ELF_SPAWN_REPORT_FINAL,
    MICROS_ELF_SPAWN_REPORT_FAILURE,
};

#define MICROS_ELF_SPAWN_INIT_PATH "/sbin/elf-spawn-init"
#define MICROS_ELF_SPAWN_CHILD_PATH "/bin/elf-spawn-child"
#define MICROS_ELF_SPAWN_DATA_PATH "/etc/elf-spawn-data"
#define MICROS_ELF_SPAWN_INIT_ARGUMENT "elf-spawn-init"
#define MICROS_ELF_SPAWN_CHILD_ARGUMENT "elf-spawn-child"
#define MICROS_ELF_SPAWN_CHILD_SECOND_ARGUMENT "inherited"
#define MICROS_ELF_SPAWN_ENVIRONMENT "MODE=elf-spawn"
#define MICROS_ELF_SPAWN_DATA "micros ELF spawn fixture\n"
#define MICROS_ELF_SPAWN_READY_TIMEOUT UINT64_C(100000000)
#define MICROS_ELF_SPAWN_PASS \
    "MICROS_ELF_SPAWN_TEST_PASS services=production init=dynamic " \
    "rollback=post-freeze arguments=validated descriptors=inherited " \
    "io=two-hop exit=normal wait=one-shot resources=baseline\n"

void micros_elf_spawn_report(uint64_t stage, uint64_t detail);

#endif
