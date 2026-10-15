#include "tests/qemu/elf_spawn_request.h"

#include "micros/spawn.h"

static void elf_spawn_write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void elf_spawn_write_u32_le(uint8_t *bytes, uint32_t value)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static size_t elf_spawn_request_size(bool init)
{
    if (init) {
        return (
            MICROS_SPAWN_REQUEST_HEADER_SIZE
            + sizeof(MICROS_ELF_SPAWN_INIT_PATH)
            + sizeof(MICROS_ELF_SPAWN_INIT_ARGUMENT)
            + sizeof(MICROS_ELF_SPAWN_ENVIRONMENT)
        );
    }
    return (
        MICROS_SPAWN_REQUEST_HEADER_SIZE
        + sizeof(MICROS_ELF_SPAWN_CHILD_PATH)
        + sizeof(MICROS_ELF_SPAWN_CHILD_ARGUMENT)
        + sizeof(MICROS_ELF_SPAWN_CHILD_SECOND_ARGUMENT)
        + sizeof(MICROS_ELF_SPAWN_ENVIRONMENT)
    );
}

static void elf_spawn_append_string(
    uint8_t *bytes,
    size_t reference,
    const char *string,
    size_t length,
    size_t *cursor
)
{
    size_t index;

    elf_spawn_write_u16_le(bytes + reference, (uint16_t)*cursor);
    elf_spawn_write_u16_le(bytes + reference + 2, (uint16_t)length);
    for (index = 0; index < length; ++index) {
        bytes[*cursor + index] = (uint8_t)string[index];
    }
    *cursor += length;
}

static void elf_spawn_write_strings(uint8_t *bytes, bool init)
{
    size_t cursor = MICROS_SPAWN_REQUEST_HEADER_SIZE;

    elf_spawn_append_string(
        bytes, MICROS_SPAWN_PATH_REFERENCE_OFFSET,
        init ? MICROS_ELF_SPAWN_INIT_PATH : MICROS_ELF_SPAWN_CHILD_PATH,
        init ? sizeof(MICROS_ELF_SPAWN_INIT_PATH)
             : sizeof(MICROS_ELF_SPAWN_CHILD_PATH),
        &cursor
    );
    elf_spawn_append_string(
        bytes, MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET,
        init ? MICROS_ELF_SPAWN_INIT_ARGUMENT : MICROS_ELF_SPAWN_CHILD_ARGUMENT,
        init ? sizeof(MICROS_ELF_SPAWN_INIT_ARGUMENT)
             : sizeof(MICROS_ELF_SPAWN_CHILD_ARGUMENT),
        &cursor
    );
    if (!init) {
        elf_spawn_append_string(
            bytes,
            MICROS_SPAWN_ARGUMENT_REFERENCES_OFFSET
                + MICROS_SPAWN_STRING_REFERENCE_SIZE,
            MICROS_ELF_SPAWN_CHILD_SECOND_ARGUMENT,
            sizeof(MICROS_ELF_SPAWN_CHILD_SECOND_ARGUMENT),
            &cursor
        );
    }
    elf_spawn_append_string(
        bytes, MICROS_SPAWN_ENVIRONMENT_REFERENCES_OFFSET,
        MICROS_ELF_SPAWN_ENVIRONMENT,
        sizeof(MICROS_ELF_SPAWN_ENVIRONMENT), &cursor
    );
}

static void elf_spawn_write_descriptors(
    uint8_t *bytes,
    bool init,
    bool missing
)
{
    uint16_t count = init ? 0 : missing ? 1 : 3;
    uint16_t index;

    elf_spawn_write_u16_le(bytes + MICROS_SPAWN_DESCRIPTOR_ACTION_COUNT_OFFSET, count);
    for (index = 0; index < count; ++index) {
        size_t offset = MICROS_SPAWN_DESCRIPTOR_ACTIONS_OFFSET
            + index * MICROS_SPAWN_DESCRIPTOR_ACTION_SIZE;
        uint16_t source = missing ? MICROS_SPAWN_DESCRIPTOR_CAPACITY - 1 : index;

        elf_spawn_write_u16_le(bytes + offset, source);
        elf_spawn_write_u16_le(bytes + offset + 2, index);
        elf_spawn_write_u32_le(bytes + offset + 4, MICROS_SPAWN_DESCRIPTOR_DUPLICATE);
    }
}

bool micros_elf_spawn_build_request(
    uint8_t *bytes,
    size_t capacity,
    enum micros_elf_spawn_request_kind kind,
    size_t *size
)
{
    bool init = kind == MICROS_ELF_SPAWN_REQUEST_INIT;
    size_t needed = elf_spawn_request_size(init);
    size_t index;

    if (
        bytes == NULL || size == NULL
        || (
            !init && kind != MICROS_ELF_SPAWN_REQUEST_CHILD
            && kind != MICROS_ELF_SPAWN_REQUEST_MISSING_DESCRIPTOR
        )
        || needed > MICROS_ELF_SPAWN_REQUEST_BUFFER_SIZE
        || capacity < needed
    ) {
        return false;
    }
    for (index = 0; index < needed; ++index) {
        bytes[index] = 0;
    }
    elf_spawn_write_u32_le(bytes, MICROS_SPAWN_REQUEST_VERSION);
    elf_spawn_write_u32_le(bytes + 4, MICROS_SPAWN_REQUEST_HEADER_SIZE);
    elf_spawn_write_u32_le(bytes + 8, (uint32_t)needed);
    elf_spawn_write_u16_le(bytes + MICROS_SPAWN_ARGUMENT_COUNT_OFFSET, init ? 1 : 2);
    elf_spawn_write_u16_le(bytes + MICROS_SPAWN_ENVIRONMENT_COUNT_OFFSET, 1);
    elf_spawn_write_strings(bytes, init);
    elf_spawn_write_descriptors(
        bytes, init, kind == MICROS_ELF_SPAWN_REQUEST_MISSING_DESCRIPTOR
    );
    *size = needed;
    return true;
}
