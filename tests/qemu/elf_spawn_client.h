#ifndef MICROS_TESTS_QEMU_ELF_SPAWN_CLIENT_H
#define MICROS_TESTS_QEMU_ELF_SPAWN_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/pm.h"
#include "tests/qemu/elf_spawn_request.h"

struct micros_elf_spawn_client {
    uint8_t bytes[MICROS_ELF_SPAWN_REQUEST_BUFFER_SIZE];
};

struct micros_elf_spawn_pm_result {
    enum micros_pm_result result;
    enum micros_pm_exit_kind exit_kind;
    uint64_t pid;
    int32_t exit_code;
};

bool micros_elf_spawn_client_spawn(
    struct micros_elf_spawn_client *client,
    enum micros_elf_spawn_request_kind kind,
    struct micros_elf_spawn_pm_result *result
);
bool micros_elf_spawn_client_wait(uint64_t pid, struct micros_elf_spawn_pm_result *result);
bool micros_elf_spawn_client_write(
    struct micros_elf_spawn_client *client, uint32_t descriptor,
    const uint8_t *bytes, size_t size
);
bool micros_elf_spawn_client_read_data(struct micros_elf_spawn_client *client);
bool micros_elf_spawn_client_drain(void);
bool micros_elf_spawn_client_startup(int argc, char *argv[], char *envp[], bool init);

uint32_t elf_spawn_client_read_u32(const uint8_t *bytes);
uint64_t elf_spawn_client_read_u64(const uint8_t *bytes);
void elf_spawn_client_write_u32(uint8_t *bytes, uint32_t value);
void elf_spawn_client_write_u64(uint8_t *bytes, uint64_t value);
bool elf_spawn_client_bytes_equal(const void *left, const void *right, size_t size);
bool elf_spawn_client_bytes_zero(const uint8_t *bytes, size_t size);

#endif
