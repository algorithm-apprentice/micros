#ifndef MICROS_TESTS_HOST_RAMFS_MODEL_INTERNAL_H
#define MICROS_TESTS_HOST_RAMFS_MODEL_INTERNAL_H

#include "servers/ramfs/ramfs_core.h"

enum {
    MODEL_STEP_COUNT = 8192,
    MODEL_GRANT = 1,
    MODEL_GRANT_CAPACITY = 8192,
    MODEL_CAPACITY_PHASE = 7900,
    MODEL_CAPACITY_PHASE_END = 7965,
};

enum model_operation {
    MODEL_OPERATION_MOUNT = 1,
    MODEL_OPERATION_LOOKUP_ABSOLUTE,
    MODEL_OPERATION_LOOKUP_RELATIVE,
    MODEL_OPERATION_CREATE,
    MODEL_OPERATION_MKDIR,
    MODEL_OPERATION_READ,
    MODEL_OPERATION_WRITE,
    MODEL_OPERATION_GETDENTS,
    MODEL_OPERATION_PUTNODE,
    MODEL_OPERATION_STALE,
    MODEL_OPERATION_MALFORMED,
    MODEL_OPERATION_GRANT_FAILURE,
};

struct reference_node {
    bool live;
    uint32_t generation;
    uint32_t mode;
    uint32_t link_count;
    uint32_t reference_count;
    uint16_t parent;
    uint16_t name_length;
    uint64_t size;
    uint8_t name[MICROS_RAMFS_NAME_MAX];
    uint16_t blocks[MICROS_RAMFS_BLOCK_CAPACITY];
};

struct reference_state {
    bool mounted;
    uint16_t node_count;
    uint16_t allocated_block_count;
    struct reference_node nodes[MICROS_RAMFS_NODE_CAPACITY];
    uint16_t block_owner[MICROS_RAMFS_BLOCK_CAPACITY];
    uint16_t block_logical[MICROS_RAMFS_BLOCK_CAPACITY];
    uint8_t data[MICROS_RAMFS_BLOCK_CAPACITY][MICROS_RAMFS_BLOCK_SIZE];
};

struct model_fields {
    uint64_t node;
    uint64_t file_size;
    uint64_t position;
    uint32_t count;
    uint32_t mode;
};

struct model_bus {
    uint8_t storage[MODEL_GRANT_CAPACITY];
    uint8_t expected[MODEL_GRANT_CAPACITY];
    bool reject;
};

struct model_coverage {
    size_t mount;
    size_t lookup_absolute;
    size_t lookup_relative;
    size_t create;
    size_t mkdir;
    size_t read;
    size_t write;
    size_t getdents;
    size_t putnode;
    size_t stale;
    size_t malformed;
    size_t grant_failure;
    size_t no_space;
};

struct ramfs_model {
    uint64_t seed;
    uint64_t random_state;
    uint64_t trace_hash;
    size_t step;
    uint32_t trace[MODEL_STEP_COUNT];
    struct micros_ramfs_state production;
    struct reference_state reference;
    struct model_bus bus;
    struct model_coverage coverage;
    uint64_t directory_cursor;
    uint16_t model_directory_slot;
    uint16_t capacity_file_slot;
};

struct ramfs_model_seed {
    uint8_t seed_image[MICROS_RAMFS_SEED_IMAGE_MAX];
    size_t seed_image_size;
};

void ramfs_model_write_u32_le(uint8_t *bytes, uint32_t value);

void ramfs_model_write_u64_le(uint8_t *bytes, uint64_t value);

bool ramfs_model_load_seed(const char *path);

bool ramfs_model_model_failure(
    const struct ramfs_model *selected,
    const char *expression,
    const char *file_name,
    int line
);

void ramfs_model_record_operation(
    struct ramfs_model *selected,
    enum model_operation operation,
    uint32_t detail
);

void ramfs_model_initialize_reference(struct reference_state *reference);

uint64_t ramfs_model_reference_handle(
    const struct reference_state *reference,
    uint16_t slot
);

enum micros_ramfs_result ramfs_model_reference_mount(
    struct reference_state *reference,
    struct model_fields *fields
);

enum micros_ramfs_result ramfs_model_reference_lookup(
    struct reference_state *reference,
    uint64_t start_handle,
    uint64_t root_handle,
    const uint8_t *path,
    size_t length,
    struct model_fields *fields
);

enum micros_ramfs_result ramfs_model_reference_create(
    struct reference_state *reference,
    uint32_t type,
    uint64_t parent_handle,
    uint32_t mode,
    const uint8_t *name,
    size_t length,
    struct model_fields *fields
);

enum micros_ramfs_result ramfs_model_reference_write(
    struct reference_state *reference,
    uint64_t handle,
    uint64_t offset,
    const uint8_t *bytes,
    uint32_t count,
    struct model_fields *fields
);

enum micros_ramfs_result ramfs_model_reference_read(
    const struct reference_state *reference,
    uint64_t handle,
    uint64_t offset,
    uint32_t count,
    uint8_t *output,
    struct model_fields *fields
);

enum micros_ramfs_result ramfs_model_reference_getdents(
    const struct reference_state *reference,
    uint64_t handle,
    uint64_t cursor,
    uint32_t capacity,
    uint8_t *output,
    struct model_fields *fields
);

enum micros_ramfs_result ramfs_model_reference_putnode(
    struct reference_state *reference,
    uint64_t handle,
    uint32_t count
);

bool ramfs_model_compare_state(struct ramfs_model *selected);

bool ramfs_model_execute_mount(struct ramfs_model *selected);

bool ramfs_model_execute_lookup(
    struct ramfs_model *selected,
    uint64_t start,
    uint64_t root,
    const uint8_t *path,
    size_t length,
    bool relative,
    bool reject
);

bool ramfs_model_execute_create(
    struct ramfs_model *selected,
    uint32_t type,
    uint64_t parent,
    const char *name
);

bool ramfs_model_execute_write(
    struct ramfs_model *selected,
    uint64_t handle,
    uint64_t offset,
    const uint8_t *bytes,
    uint32_t count
);

bool ramfs_model_execute_read(
    struct ramfs_model *selected,
    uint64_t handle,
    uint64_t offset,
    uint32_t count
);

bool ramfs_model_execute_getdents(
    struct ramfs_model *selected,
    uint64_t handle,
    uint32_t capacity
);

bool ramfs_model_execute_putnode(
    struct ramfs_model *selected,
    uint64_t handle,
    uint32_t count
);

bool ramfs_model_execute_stale(struct ramfs_model *selected);

bool ramfs_model_execute_malformed(struct ramfs_model *selected);

bool ramfs_model_execute_random_operation(struct ramfs_model *selected);

bool ramfs_model_execute_scripted_operation(struct ramfs_model *selected);

struct ramfs_model *ramfs_model_context(void);

const struct ramfs_model_seed *ramfs_model_seed_context(void);

extern const struct micros_ramfs_io ramfs_model_io;

#define MODEL_EXPECT(selected, expression) \
    do { \
        if (!(expression)) { \
            return ramfs_model_model_failure( \
                (selected), \
                #expression, \
                __FILE__, \
                __LINE__ \
            ); \
        } \
    } while (false)

#endif
