#ifndef MICROS_TESTS_QEMU_ELF_SPAWN_REQUEST_H
#define MICROS_TESTS_QEMU_ELF_SPAWN_REQUEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tests/qemu/elf_spawn_protocol.h"

enum micros_elf_spawn_request_kind {
    MICROS_ELF_SPAWN_REQUEST_INIT = 1,
    MICROS_ELF_SPAWN_REQUEST_CHILD,
    MICROS_ELF_SPAWN_REQUEST_MISSING_DESCRIPTOR,
};

bool micros_elf_spawn_build_request(
    uint8_t *bytes,
    size_t capacity,
    enum micros_elf_spawn_request_kind kind,
    size_t *size
);

#endif
