#ifndef MICROS_TESTS_HOST_RAMFS_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_RAMFS_TEST_INTERNAL_H

#include "servers/ramfs/ramfs_core.h"

#include <stdio.h>

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

enum {
    TEST_GRANT = 1,
    TEST_GRANT_CAPACITY = 8192,
};

struct test_bus {
    uint8_t storage[TEST_GRANT_CAPACITY];
    bool reject;
    bool invariant;
    size_t calls;
    enum micros_ramfs_copy_direction last_direction;
    uint64_t last_offset;
    size_t last_length;
};

struct ramfs_test_fixture {
    uint8_t seed_image[MICROS_RAMFS_SEED_IMAGE_MAX];
    size_t seed_image_size;
    struct micros_ramfs_state state;
    struct micros_ramfs_state state_snapshot;
    struct micros_ramfs_state corrupt_state;
    uint8_t storage_snapshot[TEST_GRANT_CAPACITY];
    struct test_bus bus;
};

void ramfs_test_write_u32_le(uint8_t *bytes, uint32_t value);

void ramfs_test_write_u64_le(uint8_t *bytes, uint64_t value);

uint32_t ramfs_test_read_u32_le(const uint8_t *bytes);

uint64_t ramfs_test_read_u64_le(const uint8_t *bytes);

bool ramfs_test_load_seed(
    struct ramfs_test_fixture *fixture,
    const char *path
);

void ramfs_test_reset_bus(
    struct ramfs_test_fixture *fixture
);

struct micros_ipc_message ramfs_test_request(uint32_t type);

struct micros_ipc_message ramfs_test_lookup_request(
    uint64_t start,
    uint64_t root,
    uint32_t length,
    uint64_t grant_offset
);

struct micros_ipc_message ramfs_test_create_request(
    uint32_t type,
    uint64_t parent,
    uint32_t mode,
    uint32_t length,
    uint64_t grant_offset
);

struct micros_ipc_message ramfs_test_transfer_request(
    uint32_t type,
    uint64_t node,
    uint64_t file_offset,
    uint32_t count,
    uint64_t grant_offset
);

struct micros_ipc_message ramfs_test_getdents_request(
    uint64_t node,
    uint64_t cursor,
    uint32_t count,
    uint64_t grant_offset
);

struct micros_ipc_message ramfs_test_putnode_request(
    uint64_t node,
    uint32_t count
);

bool ramfs_test_call_expect(
    struct ramfs_test_fixture *fixture,
    const struct micros_ipc_message *message,
    enum micros_ramfs_result expected,
    struct micros_ramfs_reply_action *action
);

bool ramfs_test_failure_preserves_state(
    struct ramfs_test_fixture *fixture,
    const struct micros_ipc_message *message,
    enum micros_ramfs_result expected
);

bool ramfs_test_initialize_unmounted(
    struct ramfs_test_fixture *fixture
);

bool ramfs_test_initialize_mounted(
    struct ramfs_test_fixture *fixture
);

bool ramfs_test_lookup_path(
    struct ramfs_test_fixture *fixture,
    uint64_t start,
    uint64_t root,
    const uint8_t *path,
    uint32_t length,
    enum micros_ramfs_result expected,
    struct micros_ramfs_reply_action *action
);

bool ramfs_test_create_node(
    struct ramfs_test_fixture *fixture,
    uint32_t type,
    uint64_t parent,
    uint32_t mode,
    const char *name,
    struct micros_ramfs_reply_action *action
);

bool ramfs_test_seed_state_is_exact_and_invariants_fail_closed(
    struct ramfs_test_fixture *fixture
);

bool ramfs_test_protocol_mount_and_result_layout_are_exact(
    struct ramfs_test_fixture *fixture
);

bool ramfs_test_lookup_semantics_and_reference_limits_are_exact(
    struct ramfs_test_fixture *fixture
);

bool ramfs_test_handles_create_mkdir_and_capacity_are_exact(
    struct ramfs_test_fixture *fixture
);

bool ramfs_test_read_write_sparse_and_block_capacity_are_exact(
    struct ramfs_test_fixture *fixture
);

bool ramfs_test_getdents_cursor_and_records_are_exact(
    struct ramfs_test_fixture *fixture
);

bool ramfs_test_putnode_and_handle_validation_are_exact(
    struct ramfs_test_fixture *fixture
);

struct ramfs_test_fixture *ramfs_test_context(void);

extern const micros_endpoint_t ramfs_test_vfs_endpoint;

extern const struct micros_ramfs_io ramfs_test_io;

#endif
