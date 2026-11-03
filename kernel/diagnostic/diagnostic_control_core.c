#include "kernel/diagnostic/diagnostic_control_core.h"

#include "micros/sv39.h"
#include "micros/tty.h"
#include "micros/user_address_space.h"

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size)
{
    uint8_t *output = destination;
    const uint8_t *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static bool identity_valid(const struct micros_diagnostic_identity *identity)
{
    return (
        identity != NULL
        && identity->service_id == MICROS_TTY_SERVICE_ID
        && identity->process.slot == MICROS_TTY_PROCESS_SLOT
        && identity->process.generation != 0
        && identity->process.generation <= MICROS_PROCESS_GENERATION_MAX
        && identity->thread.slot < MICROS_THREAD_CAPACITY
        && identity->thread.generation != 0
        && identity->endpoint == (
            identity->process.generation << MICROS_ENDPOINT_SLOT_BITS
            | identity->process.slot
        )
        && identity->profile_id == MICROS_PRIVILEGE_PROFILE_TTY
        && identity->root_physical_address != 0
        && identity->root_physical_address % MICROS_SV39_PAGE_SIZE == 0
    );
}

static bool snapshot_range_valid(uint64_t address, uint64_t length)
{
    return (
        length == MICROS_DIAGNOSTIC_SNAPSHOT_SIZE
        && address >= MICROS_USER_VIRTUAL_BASE
        && address % MICROS_SV39_PAGE_SIZE == 0
        && address <= MICROS_TTY_UART_VIRTUAL_BASE - MICROS_DIAGNOSTIC_SNAPSHOT_SIZE
    );
}

static bool pages_valid(const uint64_t pages[3])
{
    unsigned index;
    unsigned previous;

    for (index = 0; index < 3; ++index) {
        if (
            pages[index] == 0 || pages[index] % MICROS_SV39_PAGE_SIZE != 0
            || pages[index] > UINT64_MAX - (MICROS_SV39_PAGE_SIZE - 1)
        ) {
            return false;
        }
        for (previous = 0; previous < index; ++previous) {
            if (pages[index] == pages[previous]) {
                return false;
            }
        }
    }
    return true;
}

bool micros_diagnostic_reader_valid(const struct micros_diagnostic_reader *reader)
{
    const struct micros_diagnostic_identity *identity;

    if (reader == NULL) {
        return false;
    }
    identity = &reader->identity;
    if (reader->registered) {
        return identity_valid(identity)
            && snapshot_range_valid(reader->virtual_base, MICROS_DIAGNOSTIC_SNAPSHOT_SIZE)
            && pages_valid(reader->pages);
    }
    return (
        identity->service_id == 0 && identity->process.slot == 0 && identity->process.generation == 0
        && identity->thread.slot == 0 && identity->thread.generation == 0
        && identity->endpoint == 0 && identity->profile_id == 0 && identity->root_physical_address == 0
        && reader->virtual_base == 0 && reader->pages[0] == 0 && reader->pages[1] == 0 && reader->pages[2] == 0
    );
}

bool micros_diagnostic_reader_matches(
    const struct micros_diagnostic_reader *reader,
    const struct micros_diagnostic_identity *identity
)
{
    const struct micros_diagnostic_identity *bound;

    if (
        !micros_diagnostic_reader_valid(reader) || !reader->registered || !identity_valid(identity)
    ) {
        return false;
    }
    bound = &reader->identity;
    return (
        bound->service_id == identity->service_id
        && bound->process.slot == identity->process.slot
        && bound->process.generation == identity->process.generation
        && bound->thread.slot == identity->thread.slot
        && bound->thread.generation == identity->thread.generation
        && bound->endpoint == identity->endpoint
        && bound->profile_id == identity->profile_id
        && bound->root_physical_address == identity->root_physical_address
    );
}

enum micros_diagnostic_control_result micros_diagnostic_register(
    struct micros_diagnostic_reader *reader,
    const struct micros_diagnostic_identity *identity,
    uint64_t virtual_base,
    uint64_t length
)
{
    struct micros_diagnostic_reader candidate;
    unsigned index;

    if (!micros_diagnostic_reader_valid(reader) || !identity_valid(identity)) {
        return MICROS_DIAGNOSTIC_CONTROL_INVARIANT;
    }
    if (reader->registered) {
        return micros_diagnostic_reader_matches(reader, identity)
            ? MICROS_DIAGNOSTIC_CONTROL_STATE : MICROS_DIAGNOSTIC_CONTROL_INVARIANT;
    }
    if (!snapshot_range_valid(virtual_base, length)) {
        return MICROS_DIAGNOSTIC_CONTROL_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    copy_bytes(&candidate.identity, identity, sizeof(candidate.identity));
    candidate.virtual_base = virtual_base;
    for (index = 0; index < 3; ++index) {
        uint32_t permissions;
        size_t contiguous;
        enum micros_user_address_space_error error = micros_user_address_space_translate(
            identity->process, virtual_base + (uint64_t)index * MICROS_SV39_PAGE_SIZE,
            &candidate.pages[index], &permissions, &contiguous
        );

        if (error == MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED
            || error == MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION) {
            return MICROS_DIAGNOSTIC_CONTROL_MEMORY_FAULT;
        }
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return MICROS_DIAGNOSTIC_CONTROL_INVARIANT;
        }
        if ((permissions & MICROS_SV39_PERMISSION_WRITE) == 0) {
            return MICROS_DIAGNOSTIC_CONTROL_MEMORY_FAULT;
        }
        if (contiguous < MICROS_SV39_PAGE_SIZE) {
            return MICROS_DIAGNOSTIC_CONTROL_INVARIANT;
        }
    }
    if (!pages_valid(candidate.pages)) {
        return MICROS_DIAGNOSTIC_CONTROL_INVARIANT;
    }
    candidate.registered = true;
    copy_bytes(reader, &candidate, sizeof(candidate));
    return MICROS_DIAGNOSTIC_CONTROL_OK;
}

enum micros_diagnostic_control_result micros_diagnostic_prepare_read(
    const struct micros_diagnostic_reader *reader,
    const struct micros_diagnostic_identity *identity,
    const struct micros_diagnostic_history *history,
    uint32_t previous,
    struct micros_diagnostic_span *span
)
{
    if (!micros_diagnostic_reader_valid(reader) || !identity_valid(identity) || span == NULL) {
        return MICROS_DIAGNOSTIC_CONTROL_INVARIANT;
    }
    if (!reader->registered) {
        return MICROS_DIAGNOSTIC_CONTROL_STATE;
    }
    if (!micros_diagnostic_reader_matches(reader, identity) || !micros_diagnostic_history_valid(history)) {
        return MICROS_DIAGNOSTIC_CONTROL_INVARIANT;
    }
    return micros_diagnostic_span(history, previous, span)
        ? MICROS_DIAGNOSTIC_CONTROL_OK : MICROS_DIAGNOSTIC_CONTROL_ARGUMENT;
}

static void copy_to_snapshot(
    const struct micros_diagnostic_reader *reader,
    uint32_t offset,
    const uint8_t *bytes,
    uint32_t length
)
{
    while (length != 0) {
        uint32_t page = offset / MICROS_SV39_PAGE_SIZE;
        uint32_t within = offset % MICROS_SV39_PAGE_SIZE;
        uint32_t count = MICROS_SV39_PAGE_SIZE - within;

        if (count > length) {
            count = length;
        }
        copy_bytes((void *)(uintptr_t)(reader->pages[page] + within), bytes, count);
        bytes += count;
        offset += count;
        length -= count;
    }
}

static void write_u32(uint8_t *bytes, uint32_t value)
{
    unsigned index;

    for (index = 0; index < 4; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

void micros_diagnostic_copy_prevalidated(
    const struct micros_diagnostic_reader *reader,
    const struct micros_diagnostic_history *history,
    const struct micros_diagnostic_span *span
)
{
    uint8_t header[MICROS_DIAGNOSTIC_HEADER_SIZE] = {0};

    write_u32(header, MICROS_DIAGNOSTIC_VERSION);
    write_u32(header + 4, span->next);
    write_u32(header + 8, span->length);
    copy_to_snapshot(reader, MICROS_DIAGNOSTIC_HEADER_SIZE,
        history->ring + span->first_start, span->first_length);
    copy_to_snapshot(reader, MICROS_DIAGNOSTIC_HEADER_SIZE + span->first_length,
        history->ring, span->length - span->first_length);
    copy_to_snapshot(reader, 0, header, sizeof(header));
}

enum micros_diagnostic_control_result micros_diagnostic_read(
    const struct micros_diagnostic_reader *reader,
    const struct micros_diagnostic_identity *identity,
    const struct micros_diagnostic_history *history,
    uint32_t previous
)
{
    struct micros_diagnostic_span span;
    enum micros_diagnostic_control_result result = micros_diagnostic_prepare_read(
        reader, identity, history, previous, &span
    );

    if (result != MICROS_DIAGNOSTIC_CONTROL_OK) {
        return result;
    }
    micros_diagnostic_copy_prevalidated(reader, history, &span);
    return MICROS_DIAGNOSTIC_CONTROL_OK;
}
