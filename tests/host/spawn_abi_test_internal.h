#ifndef MICROS_TESTS_HOST_SPAWN_ABI_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_SPAWN_ABI_TEST_INTERNAL_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lib/spawn/spawn_request.h"
#include "micros/sv39.h"
#include "servers/vm/vm_image.h"

struct test_string {
    const uint8_t *bytes;
    uint16_t size;
};

struct spawn_abi_test_context {
    uint8_t request_bytes[MICROS_SPAWN_REQUEST_SIZE_MAX];
    uint8_t elf_bytes[MICROS_APPLICATION_EXECUTABLE_SIZE_MAX + 1];
    uint8_t boundary_path[MICROS_SPAWN_STRING_SIZE_MAX];
    uint8_t boundary_arguments[
        MICROS_SPAWN_ARGUMENT_CAPACITY
    ][MICROS_SPAWN_STRING_SIZE_MAX];
    uint8_t boundary_environment[
        MICROS_SPAWN_ENVIRONMENT_CAPACITY
    ][MICROS_SPAWN_STRING_SIZE_MAX];
    uint8_t occupancy_arguments[8][MICROS_SPAWN_STRING_SIZE_MAX];
    uint8_t occupancy_environment[8][MICROS_SPAWN_STRING_SIZE_MAX];
};

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

struct spawn_abi_test_context *spawn_abi_test_context_get(void);
void spawn_abi_test_write_u16_le(uint8_t *bytes, uint16_t value);
void spawn_abi_test_write_u32_le(uint8_t *bytes, uint32_t value);
void spawn_abi_test_write_u64_le(uint8_t *bytes, uint64_t value);
uint64_t spawn_abi_test_read_u64_le(const uint8_t *bytes);
bool spawn_abi_test_bytes_are_zero(const void *storage, size_t size);
struct test_string spawn_abi_test_literal_string(const char *value);
void spawn_abi_test_write_string_reference(
    uint8_t *bytes,
    size_t reference_offset,
    size_t string_offset,
    uint16_t string_size
);
size_t spawn_abi_test_build_spawn_request(
    struct spawn_abi_test_context *context,
    const struct test_string *path,
    const struct test_string *arguments,
    uint16_t argument_count,
    const struct test_string *environment,
    uint16_t environment_count,
    const struct micros_spawn_descriptor_action *actions,
    uint16_t action_count
);
void spawn_abi_test_fill_plain_string(uint8_t *bytes, uint16_t size, uint8_t value);
void spawn_abi_test_fill_environment_string(uint8_t *bytes, uint16_t size);
bool spawn_abi_test_build_occupancy_request(
    struct spawn_abi_test_context *context,
    bool overflow,
    size_t *request_size
);
size_t spawn_abi_test_build_valid_elf(
    struct spawn_abi_test_context *context,
    uint64_t text_pages,
    uint64_t rodata_pages,
    uint64_t data_pages
);

#endif
