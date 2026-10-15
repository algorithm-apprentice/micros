#include "tests/host/ramfs_model_internal.h"

#include <stdio.h>
#include <string.h>

static struct ramfs_model model;
static struct ramfs_model_seed seed_fixture;

void ramfs_model_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void ramfs_model_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

bool ramfs_model_load_seed(const char *path)
{
    FILE *stream;
    long length;

    stream = fopen(path, "rb");
    if (stream == NULL) {
        return false;
    }
    if (
        fseek(stream, 0, SEEK_END) != 0
        || (length = ftell(stream)) < 0
        || (uint64_t)length > MICROS_RAMFS_SEED_IMAGE_MAX
        || fseek(stream, 0, SEEK_SET) != 0
        || fread(seed_fixture.seed_image, 1, (size_t)length, stream) != (size_t)length
        || fclose(stream) != 0
    ) {
        return false;
    }
    seed_fixture.seed_image_size = (size_t)length;
    return true;
}

bool ramfs_model_model_failure(
    const struct ramfs_model *selected,
    const char *expression,
    const char *file_name,
    int line
)
{
    size_t index;

    fprintf(
        stderr,
        "%s:%d: RAMFS model failure: %s "
        "seed=0x%016llx step=%zu\n",
        file_name,
        line,
        expression,
        (unsigned long long)selected->seed,
        selected->step
    );
    fprintf(stderr, "operation trace:");
    for (index = 0; index <= selected->step; ++index) {
        if ((index % 8) == 0) {
            fputc('\n', stderr);
        }
        fprintf(stderr, " %08x", selected->trace[index]);
    }
    fputc('\n', stderr);
    return false;
}

void ramfs_model_record_operation(
    struct ramfs_model *selected,
    enum model_operation operation,
    uint32_t detail
)
{
    uint32_t encoded = (
        (uint32_t)operation << 28
        | (detail & UINT32_C(0x0fffffff))
    );
    size_t byte;

    selected->trace[selected->step] = encoded;
    for (byte = 0; byte < 4; ++byte) {
        selected->trace_hash ^= (uint8_t)(encoded >> (byte * 8));
        selected->trace_hash *= MICROS_RAMFS_SEED_FNV_PRIME;
    }
}

static enum micros_ramfs_copy_result model_copy(
    void *context,
    enum micros_ramfs_copy_direction direction,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t grant_offset,
    uint8_t *local,
    size_t length
)
{
    struct ramfs_model *selected = context;

    if (
        endpoint != UINT32_C(0x00001005)
        || local == NULL
    ) {
        return MICROS_RAMFS_COPY_INVARIANT;
    }
    if (
        selected->bus.reject
        || grant != MODEL_GRANT
        || grant_offset > MODEL_GRANT_CAPACITY
        || length > MODEL_GRANT_CAPACITY - (size_t)grant_offset
    ) {
        return MICROS_RAMFS_COPY_REJECTED;
    }
    if (direction == MICROS_RAMFS_COPY_FROM_VFS) {
        memcpy(local, &selected->bus.storage[grant_offset], length);
    } else {
        memcpy(&selected->bus.storage[grant_offset], local, length);
    }
    return MICROS_RAMFS_COPY_OK;
}

const struct micros_ramfs_io ramfs_model_io = {
    .copy = model_copy,
    .context = &model,
};

struct ramfs_model *ramfs_model_context(void)
{
    return &model;
}

const struct ramfs_model_seed *ramfs_model_seed_context(void)
{
    return &seed_fixture;
}
