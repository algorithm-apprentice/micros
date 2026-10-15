#ifndef MICROS_KERNEL_ELF_SPAWN_TEST_FIXTURE_H
#define MICROS_KERNEL_ELF_SPAWN_TEST_FIXTURE_H

#include <stdint.h>

#include "kernel/bootstrap_image.h"
#include "micros/bootstrap.h"
#include "tests/qemu/elf_spawn_protocol.h"

enum micros_elf_spawn_service_object_index {
    MICROS_ELF_SPAWN_OBJECT_VM_CONTEXT = 0,
    MICROS_ELF_SPAWN_OBJECT_PM_CONTEXT,
    MICROS_ELF_SPAWN_OBJECT_TTY_STATE,
    MICROS_ELF_SPAWN_OBJECT_TTY_UART,
    MICROS_ELF_SPAWN_OBJECT_RAMFS_STATE,
    MICROS_ELF_SPAWN_OBJECT_VFS_STATE,
    MICROS_ELF_SPAWN_OBJECT_COUNT,
};

struct micros_elf_spawn_service_object {
    uint32_t service_id;
    uint32_t reserved;
    uint64_t address;
    uint64_t size;
};

extern const struct micros_bootstrap_image
    micros_elf_spawn_test_images[MICROS_ELF_SPAWN_SERVICE_COUNT];
extern const struct micros_bootstrap_manifest micros_elf_spawn_test_manifest;
extern const struct micros_elf_spawn_service_object
    micros_elf_spawn_test_objects[MICROS_ELF_SPAWN_OBJECT_COUNT];
extern const uint64_t micros_elf_spawn_report_addresses[2];

#endif
