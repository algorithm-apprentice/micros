#ifndef MICROS_SERVERS_RAMFS_INTERNAL_H
#define MICROS_SERVERS_RAMFS_INTERNAL_H

#include "servers/ramfs/ramfs_core.h"

struct ramfs_result_fields {
    uint64_t node;
    uint64_t file_size;
    uint64_t position;
    uint32_t count;
    uint32_t mode;
};

void ramfs_zero_bytes(void *storage, size_t size);

bool ramfs_bytes_are_zero(const uint8_t *bytes, size_t size);

uint16_t ramfs_read_u16_le(const uint8_t *bytes);

uint32_t ramfs_read_u32_le(const uint8_t *bytes);

void ramfs_write_u32_le(uint8_t *bytes, uint32_t value);

void ramfs_write_u64_le(uint8_t *bytes, uint64_t value);

bool ramfs_endpoint_is_valid(micros_endpoint_t endpoint);

bool ramfs_mode_is_canonical(uint32_t mode);

bool ramfs_node_is_directory(const struct micros_ramfs_node *node);

bool ramfs_node_is_regular(const struct micros_ramfs_node *node);

bool ramfs_decode_handle(
    uint64_t handle,
    uint16_t *slot,
    uint32_t *generation
);

void ramfs_initialize_empty_node(struct micros_ramfs_node *node);

uint64_t ramfs_node_handle(
    uint16_t slot,
    const struct micros_ramfs_node *node
);

enum micros_ramfs_result ramfs_resolve_referenced_node(
    const struct micros_ramfs_state *state,
    uint64_t handle,
    uint16_t *slot,
    const struct micros_ramfs_node **node
);

bool ramfs_names_equal(
    const struct micros_ramfs_node *left,
    const struct micros_ramfs_node *right
);

bool ramfs_name_equals_bytes(
    const struct micros_ramfs_node *node,
    const uint8_t *name,
    size_t name_length
);

enum micros_ramfs_core_error ramfs_validate_state(
    const struct micros_ramfs_state *state,
    bool require_clear_scratch
);

enum micros_ramfs_core_error ramfs_prepare_reply(
    const struct micros_ipc_message *request,
    enum micros_ramfs_result result,
    const struct ramfs_result_fields *fields,
    struct micros_ramfs_reply_action *action
);

enum micros_ramfs_copy_result ramfs_copy_grant(
    const struct micros_ramfs_state *state,
    const struct micros_ramfs_io *io,
    enum micros_ramfs_copy_direction direction,
    micros_grant_t grant,
    uint64_t grant_offset,
    uint8_t *local,
    size_t length
);

bool ramfs_find_free_node(
    const struct micros_ramfs_state *state,
    uint16_t *slot
);

bool ramfs_find_free_block(
    const struct micros_ramfs_state *state,
    uint16_t *block
);

enum micros_ramfs_result ramfs_handle_mount(
    struct micros_ramfs_state *state,
    struct ramfs_result_fields *fields
);

enum micros_ramfs_core_error ramfs_handle_lookup(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    const struct micros_ramfs_io *io,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result
);

enum micros_ramfs_core_error ramfs_handle_create_or_mkdir(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    const struct micros_ramfs_io *io,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result
);

enum micros_ramfs_core_error ramfs_handle_read(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    const struct micros_ramfs_io *io,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result
);

enum micros_ramfs_core_error ramfs_handle_write(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    const struct micros_ramfs_io *io,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result
);

enum micros_ramfs_core_error ramfs_handle_getdents(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request,
    const struct micros_ramfs_io *io,
    struct ramfs_result_fields *fields,
    enum micros_ramfs_result *result
);

enum micros_ramfs_result ramfs_handle_putnode(
    struct micros_ramfs_state *state,
    const struct micros_ramfs_request *request
);

#endif
