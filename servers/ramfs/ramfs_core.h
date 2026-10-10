#ifndef MICROS_SERVERS_RAMFS_CORE_H
#define MICROS_SERVERS_RAMFS_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/endpoint.h"
#include "micros/grant.h"
#include "micros/ipc.h"
#include "micros/ramfs.h"

#define MICROS_RAMFS_BLOCK_NONE UINT16_MAX
#define MICROS_RAMFS_NODE_NONE UINT16_MAX

enum micros_ramfs_phase {
    MICROS_RAMFS_PHASE_UNINITIALIZED = 0,
    MICROS_RAMFS_PHASE_READY_UNMOUNTED,
    MICROS_RAMFS_PHASE_MOUNTED,
};

enum micros_ramfs_core_error {
    MICROS_RAMFS_CORE_OK = 0,
    MICROS_RAMFS_CORE_ERROR_ARGUMENT,
    MICROS_RAMFS_CORE_ERROR_SEED,
    MICROS_RAMFS_CORE_ERROR_INVARIANT,
};

enum micros_ramfs_protocol_status {
    MICROS_RAMFS_PROTOCOL_OK = 0,
    MICROS_RAMFS_PROTOCOL_BAD_TYPE,
    MICROS_RAMFS_PROTOCOL_BAD_VERSION,
    MICROS_RAMFS_PROTOCOL_MALFORMED,
    MICROS_RAMFS_PROTOCOL_INVARIANT,
};

enum micros_ramfs_copy_direction {
    MICROS_RAMFS_COPY_FROM_VFS = 0,
    MICROS_RAMFS_COPY_TO_VFS,
};

enum micros_ramfs_copy_result {
    MICROS_RAMFS_COPY_OK = 0,
    MICROS_RAMFS_COPY_REJECTED,
    MICROS_RAMFS_COPY_INVARIANT,
};

struct micros_ramfs_request {
    uint32_t type;
    micros_endpoint_t source;
    uint64_t reply_token;
    uint64_t node;
    uint64_t start;
    uint64_t root;
    uint64_t file_offset;
    uint64_t cursor;
    uint64_t grant_offset;
    uint32_t mode;
    micros_grant_t grant;
    uint32_t length;
    uint32_t count;
};

struct micros_ramfs_node {
    bool live;
    uint8_t reserved0[3];
    uint32_t generation;
    uint32_t mode;
    uint32_t link_count;
    uint32_t reference_count;
    uint16_t name_length;
    uint16_t reserved1;
    uint64_t parent;
    uint64_t size;
    uint8_t name[MICROS_RAMFS_NAME_MAX];
    uint16_t blocks[MICROS_RAMFS_BLOCK_CAPACITY];
};

struct micros_ramfs_state {
    uint64_t initialization_magic;
    micros_endpoint_t vfs_endpoint;
    enum micros_ramfs_phase phase;
    uint16_t node_count;
    uint16_t allocated_block_count;
    uint32_t reserved;
    struct micros_ramfs_node nodes[MICROS_RAMFS_NODE_CAPACITY];
    uint16_t block_owner[MICROS_RAMFS_BLOCK_CAPACITY];
    uint16_t block_logical[MICROS_RAMFS_BLOCK_CAPACITY];
    uint8_t data[MICROS_RAMFS_BLOCK_CAPACITY][MICROS_RAMFS_BLOCK_SIZE];
    uint8_t scratch[MICROS_RAMFS_TRANSFER_MAX];
};

typedef enum micros_ramfs_copy_result
(*micros_ramfs_copy_fn)(
    void *context,
    enum micros_ramfs_copy_direction direction,
    micros_endpoint_t vfs_endpoint,
    micros_grant_t grant,
    uint64_t grant_offset,
    uint8_t *local,
    size_t length
);

struct micros_ramfs_io {
    micros_ramfs_copy_fn copy;
    void *context;
};

struct micros_ramfs_reply_action {
    bool active;
    uint64_t reply_token;
    struct micros_ipc_message message;
};

uint64_t micros_ramfs_handle(uint16_t slot, uint32_t generation);

enum micros_ramfs_protocol_status micros_ramfs_decode_request(
    const struct micros_ipc_message *message,
    struct micros_ramfs_request *request
);

enum micros_ramfs_core_error micros_ramfs_build_result(
    uint32_t request_type,
    enum micros_ramfs_result result,
    uint64_t node,
    uint64_t file_size,
    uint64_t position,
    uint32_t count,
    uint32_t mode,
    struct micros_ipc_message *message
);

enum micros_ramfs_core_error micros_ramfs_state_initialize(
    struct micros_ramfs_state *state,
    micros_endpoint_t vfs_endpoint,
    const uint8_t *seed_image,
    size_t seed_image_size
);

enum micros_ramfs_core_error micros_ramfs_state_validate(
    const struct micros_ramfs_state *state
);

enum micros_ramfs_core_error micros_ramfs_handle_call(
    struct micros_ramfs_state *state,
    const struct micros_ipc_message *message,
    const struct micros_ramfs_io *io,
    struct micros_ramfs_reply_action *action
);

#endif
