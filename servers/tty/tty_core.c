#include "servers/tty/tty_core.h"

#include <stddef.h>

#define MICROS_TTY_INITIALIZATION_MAGIC \
    UINT64_C(0x4d4943524f535454)

static void zero_bytes(void *value, size_t size)
{
    uint8_t *bytes = value;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static uint64_t read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static bool request_type_is_known(uint32_t type)
{
    return (
        type == MICROS_TTY_MESSAGE_SUBMIT_READ
        || type == MICROS_TTY_MESSAGE_SUBMIT_WRITE
        || type == MICROS_TTY_MESSAGE_CANCEL
        || type == MICROS_TTY_MESSAGE_COLLECT
    );
}

static bool result_is_defined(enum micros_tty_result result)
{
    return (
        result >= MICROS_TTY_RESULT_PENDING
        && result <= MICROS_TTY_RESULT_OK
    );
}

static void saturating_add(uint64_t *value, uint64_t increment)
{
    if (UINT64_MAX - *value < increment) {
        *value = UINT64_MAX;
    } else {
        *value += increment;
    }
}

static size_t input_index(
    const struct micros_tty_state *state,
    size_t offset
)
{
    return (
        (size_t)state->input_head + offset
    ) % MICROS_TTY_INPUT_CAPACITY;
}

static size_t echo_index(
    const struct micros_tty_state *state,
    size_t offset
)
{
    return (
        (size_t)state->echo_head + offset
    ) % MICROS_TTY_ECHO_CAPACITY;
}

bool micros_tty_output_pending(const struct micros_tty_state *state)
{
    return (
        state != NULL
        && (
            state->echo_count != 0
            || state->write_resident
        )
    );
}

bool micros_tty_input_has_line(const struct micros_tty_state *state)
{
    return state != NULL && state->completed_line_count != 0;
}

static void publish_output(
    struct micros_tty_state *state,
    bool previously_pending,
    struct micros_tty_effects *effects
)
{
    if (!previously_pending && micros_tty_output_pending(state)) {
        state->transmit_interrupt_enabled = true;
        effects->enable_transmit = true;
    }
}

static void enqueue_echo(
    struct micros_tty_state *state,
    const uint8_t *bytes,
    size_t count,
    struct micros_tty_effects *effects
)
{
    bool previously_pending = micros_tty_output_pending(state);
    size_t index;

    if (
        (size_t)state->echo_count + count
        > MICROS_TTY_ECHO_CAPACITY
    ) {
        saturating_add(&state->echo_drop_count, count);
        return;
    }

    for (index = 0; index < count; ++index) {
        state->echo[
            echo_index(state, (size_t)state->echo_count + index)
        ] = bytes[index];
    }
    state->echo_count = (uint16_t)(
        (size_t)state->echo_count + count
    );
    publish_output(state, previously_pending, effects);
}

enum micros_tty_protocol_status micros_tty_decode_request(
    const struct micros_ipc_message *message,
    struct micros_tty_request *request
)
{
    struct micros_tty_request decoded;
    uint32_t version;
    uint32_t flags;

    if (message == NULL || request == NULL) {
        return MICROS_TTY_PROTOCOL_INVARIANT;
    }
    if (!request_type_is_known(message->type)) {
        return MICROS_TTY_PROTOCOL_BAD_TYPE;
    }

    version = read_u32_le(&message->payload[0]);
    if (version != MICROS_TTY_PROTOCOL_VERSION) {
        return MICROS_TTY_PROTOCOL_BAD_VERSION;
    }
    flags = read_u32_le(&message->payload[4]);
    if (
        flags != 0
        || (
            (
                message->type == MICROS_TTY_MESSAGE_SUBMIT_READ
                || message->type
                    == MICROS_TTY_MESSAGE_SUBMIT_WRITE
            )
            && (
                !bytes_are_zero(&message->payload[20], 4)
                || !bytes_are_zero(&message->payload[40], 8)
            )
        )
        || (
            (
                message->type == MICROS_TTY_MESSAGE_CANCEL
                || message->type == MICROS_TTY_MESSAGE_COLLECT
            )
            && !bytes_are_zero(&message->payload[16], 32)
        )
    ) {
        return MICROS_TTY_PROTOCOL_MALFORMED;
    }
    if (message->reply_token == 0) {
        return MICROS_TTY_PROTOCOL_INVARIANT;
    }

    zero_bytes(&decoded, sizeof(decoded));
    decoded.type = message->type;
    decoded.flags = flags;
    decoded.source = message->source;
    decoded.reply_token = message->reply_token;
    decoded.request_id = read_u64_le(&message->payload[8]);
    if (
        message->type == MICROS_TTY_MESSAGE_SUBMIT_READ
        || message->type == MICROS_TTY_MESSAGE_SUBMIT_WRITE
    ) {
        decoded.grant = read_u32_le(&message->payload[16]);
        decoded.grant_offset = read_u64_le(&message->payload[24]);
        decoded.count = read_u64_le(&message->payload[32]);
    }
    *request = decoded;
    return MICROS_TTY_PROTOCOL_OK;
}

enum micros_tty_core_error micros_tty_build_result(
    uint32_t request_type,
    enum micros_tty_result result,
    uint64_t request_id,
    uint64_t transferred_count,
    struct micros_ipc_message *message
)
{
    struct micros_ipc_message built;

    if (
        message == NULL
        || (request_type & MICROS_IPC_TYPE_KERNEL_MASK) != 0
        || !result_is_defined(result)
        || transferred_count > MICROS_TTY_TRANSFER_MAX
        || (
            result != MICROS_TTY_RESULT_OK
            && transferred_count != 0
        )
    ) {
        return MICROS_TTY_CORE_ERROR_ARGUMENT;
    }
    if (
        result == MICROS_TTY_RESULT_OK
        && (
            !request_type_is_known(request_type)
            || request_id == 0
            || (
                request_type == MICROS_TTY_MESSAGE_COLLECT
                    ? transferred_count == 0
                    : transferred_count != 0
            )
        )
    ) {
        return MICROS_TTY_CORE_ERROR_ARGUMENT;
    }

    zero_bytes(&built, sizeof(built));
    built.type = MICROS_TTY_MESSAGE_RESULT;
    write_u32_le(&built.payload[0], MICROS_TTY_PROTOCOL_VERSION);
    write_u32_le(&built.payload[4], request_type);
    write_u32_le(&built.payload[8], (uint32_t)(int32_t)result);
    write_u64_le(&built.payload[16], request_id);
    write_u64_le(&built.payload[24], transferred_count);
    *message = built;
    return MICROS_TTY_CORE_OK;
}

enum micros_tty_core_error micros_tty_state_initialize(
    struct micros_tty_state *state
)
{
    if (state == NULL) {
        return MICROS_TTY_CORE_ERROR_ARGUMENT;
    }
    zero_bytes(state, sizeof(*state));
    state->initialization_magic = MICROS_TTY_INITIALIZATION_MAGIC;
    return MICROS_TTY_CORE_OK;
}

enum micros_tty_core_error micros_tty_state_validate(
    const struct micros_tty_state *state
)
{
    size_t index;
    size_t current_line_length = 0;
    size_t completed_line_count = 0;
    size_t first_line_length = 0;
    bool found_first_line = false;

    if (state == NULL) {
        return MICROS_TTY_CORE_ERROR_ARGUMENT;
    }
    if (
        state->initialization_magic
            != MICROS_TTY_INITIALIZATION_MAGIC
        || state->input_head >= MICROS_TTY_INPUT_CAPACITY
        || state->input_count > MICROS_TTY_INPUT_CAPACITY
        || state->current_line_length
            > MICROS_TTY_INPUT_CAPACITY - 1
        || state->completed_line_count
            > MICROS_TTY_INPUT_CAPACITY
        || state->echo_head >= MICROS_TTY_ECHO_CAPACITY
        || state->echo_count > MICROS_TTY_ECHO_CAPACITY
        || state->read_staging_length
            > MICROS_TTY_INPUT_CAPACITY
        || state->write_length > MICROS_TTY_WRITE_CAPACITY
        || state->write_cursor > state->write_length
    ) {
        return MICROS_TTY_CORE_ERROR_INVARIANT;
    }

    for (index = 0; index < state->input_count; ++index) {
        const struct micros_tty_input_entry *entry =
            &state->input[input_index(state, index)];

        if ((entry->flags & ~MICROS_TTY_INPUT_END_OF_LINE) != 0) {
            return MICROS_TTY_CORE_ERROR_INVARIANT;
        }
        if ((entry->flags & MICROS_TTY_INPUT_END_OF_LINE) != 0) {
            ++completed_line_count;
            current_line_length = 0;
            if (!found_first_line) {
                first_line_length = index + 1;
                found_first_line = true;
            }
        } else {
            ++current_line_length;
        }
    }
    if (
        completed_line_count != state->completed_line_count
        || current_line_length != state->current_line_length
    ) {
        return MICROS_TTY_CORE_ERROR_INVARIANT;
    }

    if (state->read_staging_length != 0) {
        if (
            !found_first_line
            || state->read_staging_length > first_line_length
        ) {
            return MICROS_TTY_CORE_ERROR_INVARIANT;
        }
        for (index = 0; index < state->read_staging_length; ++index) {
            if (
                state->read_staging[index]
                != state->input[input_index(state, index)].byte
            ) {
                return MICROS_TTY_CORE_ERROR_INVARIANT;
            }
        }
    }

    if (
        (
            !state->write_resident
            && (
                state->write_length != 0
                || state->write_cursor != 0
                || state->write_pending_lf
                || state->write_retry_armed
            )
        )
        || (
            state->write_resident
            && (
                state->write_length == 0
                || (
                    state->write_cursor == state->write_length
                    && !state->write_pending_lf
                )
            )
        )
        || (
            state->transmit_interrupt_enabled
            != micros_tty_output_pending(state)
        )
    ) {
        return MICROS_TTY_CORE_ERROR_INVARIANT;
    }
    return MICROS_TTY_CORE_OK;
}

enum micros_tty_core_error micros_tty_receive_byte(
    struct micros_tty_state *state,
    uint8_t byte,
    struct micros_tty_effects *effects
)
{
    static const uint8_t newline_echo[] = {'\r', '\n'};
    static const uint8_t erase_echo[] = {'\b', ' ', '\b'};
    struct micros_tty_effects committed_effects;
    enum micros_tty_core_error error;
    size_t tail;

    if (state == NULL || effects == NULL) {
        return MICROS_TTY_CORE_ERROR_ARGUMENT;
    }
    error = micros_tty_state_validate(state);
    if (error != MICROS_TTY_CORE_OK) {
        return error;
    }
    zero_bytes(&committed_effects, sizeof(committed_effects));

    if (byte == '\r') {
        byte = '\n';
    }
    if (byte == '\n') {
        if (state->input_count == MICROS_TTY_INPUT_CAPACITY) {
            saturating_add(&state->input_drop_count, 1);
        } else {
            tail = input_index(state, state->input_count);
            state->input[tail].byte = byte;
            state->input[tail].flags =
                MICROS_TTY_INPUT_END_OF_LINE;
            ++state->input_count;
            ++state->completed_line_count;
            state->current_line_length = 0;
            enqueue_echo(
                state,
                newline_echo,
                sizeof(newline_echo),
                &committed_effects
            );
        }
    } else if (byte == '\b' || byte == 0x7f) {
        if (state->current_line_length != 0) {
            tail = input_index(state, state->input_count - 1);
            --state->input_count;
            --state->current_line_length;
            state->input[tail].byte = 0;
            state->input[tail].flags = 0;
            enqueue_echo(
                state,
                erase_echo,
                sizeof(erase_echo),
                &committed_effects
            );
        }
    } else if (byte == '\t' || byte >= 0x20) {
        if (
            state->input_count
            >= MICROS_TTY_INPUT_CAPACITY - 1
        ) {
            saturating_add(&state->input_drop_count, 1);
        } else {
            tail = input_index(state, state->input_count);
            state->input[tail].byte = byte;
            state->input[tail].flags = 0;
            ++state->input_count;
            ++state->current_line_length;
            enqueue_echo(
                state,
                &byte,
                1,
                &committed_effects
            );
        }
    }

    *effects = committed_effects;
    return MICROS_TTY_CORE_OK;
}

enum micros_tty_core_error micros_tty_record_receive_error(
    struct micros_tty_state *state
)
{
    enum micros_tty_core_error error;

    if (state == NULL) {
        return MICROS_TTY_CORE_ERROR_ARGUMENT;
    }
    error = micros_tty_state_validate(state);
    if (error != MICROS_TTY_CORE_OK) {
        return error;
    }
    saturating_add(&state->receive_error_count, 1);
    return MICROS_TTY_CORE_OK;
}

enum micros_tty_core_error micros_tty_read_stage(
    struct micros_tty_state *state,
    uint64_t requested_count,
    uint64_t *staged_count
)
{
    enum micros_tty_core_error error;
    size_t first_line_length = 0;
    size_t selected;
    size_t index;

    if (
        state == NULL
        || staged_count == NULL
        || requested_count == 0
        || requested_count > MICROS_TTY_TRANSFER_MAX
    ) {
        return MICROS_TTY_CORE_ERROR_ARGUMENT;
    }
    error = micros_tty_state_validate(state);
    if (error != MICROS_TTY_CORE_OK) {
        return error;
    }
    if (state->read_staging_length != 0) {
        return MICROS_TTY_CORE_ERROR_STATE;
    }
    if (state->completed_line_count == 0) {
        return MICROS_TTY_CORE_ERROR_NOT_READY;
    }

    for (index = 0; index < state->input_count; ++index) {
        const struct micros_tty_input_entry *entry =
            &state->input[input_index(state, index)];

        if ((entry->flags & MICROS_TTY_INPUT_END_OF_LINE) != 0) {
            first_line_length = index + 1;
            break;
        }
    }
    if (first_line_length == 0) {
        return MICROS_TTY_CORE_ERROR_INVARIANT;
    }

    selected = first_line_length;
    if (requested_count < selected) {
        selected = (size_t)requested_count;
    }
    for (index = 0; index < selected; ++index) {
        state->read_staging[index] =
            state->input[input_index(state, index)].byte;
    }
    state->read_staging_length = (uint16_t)selected;
    *staged_count = selected;
    return MICROS_TTY_CORE_OK;
}

enum micros_tty_core_error micros_tty_read_discard(
    struct micros_tty_state *state
)
{
    enum micros_tty_core_error error;

    if (state == NULL) {
        return MICROS_TTY_CORE_ERROR_ARGUMENT;
    }
    error = micros_tty_state_validate(state);
    if (error != MICROS_TTY_CORE_OK) {
        return error;
    }
    if (state->read_staging_length == 0) {
        return MICROS_TTY_CORE_ERROR_STATE;
    }
    state->read_staging_length = 0;
    return MICROS_TTY_CORE_OK;
}

enum micros_tty_core_error micros_tty_read_commit(
    struct micros_tty_state *state
)
{
    enum micros_tty_core_error error;
    size_t committed;
    size_t index;

    if (state == NULL) {
        return MICROS_TTY_CORE_ERROR_ARGUMENT;
    }
    error = micros_tty_state_validate(state);
    if (error != MICROS_TTY_CORE_OK) {
        return error;
    }
    if (state->read_staging_length == 0) {
        return MICROS_TTY_CORE_ERROR_STATE;
    }

    committed = state->read_staging_length;
    for (index = 0; index < committed; ++index) {
        struct micros_tty_input_entry *entry =
            &state->input[input_index(state, index)];

        if ((entry->flags & MICROS_TTY_INPUT_END_OF_LINE) != 0) {
            --state->completed_line_count;
        }
        entry->byte = 0;
        entry->flags = 0;
    }
    state->input_head = (uint16_t)input_index(state, committed);
    state->input_count = (uint16_t)(
        state->input_count - committed
    );
    state->read_staging_length = 0;
    return MICROS_TTY_CORE_OK;
}

enum micros_tty_core_error micros_tty_write_accept(
    struct micros_tty_state *state,
    const uint8_t *bytes,
    uint64_t count,
    struct micros_tty_effects *effects
)
{
    struct micros_tty_effects committed_effects;
    enum micros_tty_core_error error;
    bool previously_pending;
    size_t index;

    if (
        state == NULL
        || bytes == NULL
        || effects == NULL
        || count == 0
        || count > MICROS_TTY_WRITE_CAPACITY
    ) {
        return MICROS_TTY_CORE_ERROR_ARGUMENT;
    }
    error = micros_tty_state_validate(state);
    if (error != MICROS_TTY_CORE_OK) {
        return error;
    }
    if (state->write_resident) {
        return MICROS_TTY_CORE_ERROR_BUSY;
    }

    zero_bytes(&committed_effects, sizeof(committed_effects));
    previously_pending = micros_tty_output_pending(state);
    for (index = 0; index < (size_t)count; ++index) {
        state->write[index] = bytes[index];
    }
    state->write_length = (uint16_t)count;
    state->write_cursor = 0;
    state->write_resident = true;
    state->write_pending_lf = false;
    publish_output(
        state,
        previously_pending,
        &committed_effects
    );
    *effects = committed_effects;
    return MICROS_TTY_CORE_OK;
}

enum micros_tty_core_error micros_tty_write_arm_retry(
    struct micros_tty_state *state
)
{
    enum micros_tty_core_error error;

    if (state == NULL) {
        return MICROS_TTY_CORE_ERROR_ARGUMENT;
    }
    error = micros_tty_state_validate(state);
    if (error != MICROS_TTY_CORE_OK) {
        return error;
    }
    if (!state->write_resident) {
        return MICROS_TTY_CORE_ERROR_STATE;
    }
    state->write_retry_armed = true;
    return MICROS_TTY_CORE_OK;
}

enum micros_tty_core_error micros_tty_output_take(
    struct micros_tty_state *state,
    uint8_t *byte,
    struct micros_tty_effects *effects
)
{
    struct micros_tty_effects committed_effects;
    enum micros_tty_core_error error;
    uint8_t selected;

    if (state == NULL || byte == NULL || effects == NULL) {
        return MICROS_TTY_CORE_ERROR_ARGUMENT;
    }
    error = micros_tty_state_validate(state);
    if (error != MICROS_TTY_CORE_OK) {
        return error;
    }
    if (!micros_tty_output_pending(state)) {
        return MICROS_TTY_CORE_ERROR_EMPTY;
    }

    zero_bytes(&committed_effects, sizeof(committed_effects));
    if (state->write_pending_lf) {
        selected = '\n';
        state->write_pending_lf = false;
    } else if (state->echo_count != 0) {
        selected = state->echo[state->echo_head];
        state->echo[state->echo_head] = 0;
        state->echo_head = (uint16_t)(
            (state->echo_head + 1) % MICROS_TTY_ECHO_CAPACITY
        );
        --state->echo_count;
    } else {
        selected = state->write[state->write_cursor];
        ++state->write_cursor;
        if (selected == '\n') {
            selected = '\r';
            state->write_pending_lf = true;
        }
    }

    if (
        state->write_resident
        && state->write_cursor == state->write_length
        && !state->write_pending_lf
    ) {
        state->write_resident = false;
        state->write_length = 0;
        state->write_cursor = 0;
        if (state->write_retry_armed) {
            state->write_retry_armed = false;
            committed_effects.writable = true;
        }
    }
    if (!micros_tty_output_pending(state)) {
        state->transmit_interrupt_enabled = false;
        committed_effects.disable_transmit = true;
    }

    *byte = selected;
    *effects = committed_effects;
    return MICROS_TTY_CORE_OK;
}
