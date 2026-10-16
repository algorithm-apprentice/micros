#include "tty_model_internal.h"

const micros_endpoint_t tty_model_tty_endpoint = UINT32_C(0x00007003);
const micros_endpoint_t tty_model_vfs_endpoint = UINT32_C(0x00009005);

bool tty_model_failure(
    const char *expression,
    int line,
    const struct tty_model *model
)
{
    size_t index;

    fprintf(
        stderr,
        "%s:%d: TTY model failure: %s seed=0x%016llx step=%zu\n",
        __FILE__,
        line,
        expression,
        (unsigned long long)model->seed,
        model->step
    );
    fprintf(stderr, "operation trace:");
    for (index = 0; index < model->trace_count; ++index) {
        if ((index % 8) == 0) {
            fputc('\n', stderr);
        }
        fprintf(stderr, " %08x", model->operation_trace[index]);
    }
    fputc('\n', stderr);
    return false;
}

void tty_model_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void tty_model_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

uint32_t tty_model_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

uint64_t tty_model_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

uint64_t tty_model_random(struct tty_model *model)
{
    uint64_t value = model->random_state;

    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    model->random_state = value;
    return value;
}

void tty_model_record(
    struct tty_model *model,
    enum model_operation operation,
    uint32_t detail
)
{
    model->operation_trace[model->step] =
        (uint32_t)operation << 28
        | (detail & UINT32_C(0x0fffffff));
    model->trace_count = model->step + 1;
}
