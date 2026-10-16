#include "tty_model_internal.h"

static size_t tty_model_reference_input_index(
    const struct model_reference *reference,
    size_t offset
)
{
    return (
        (size_t)reference->input_head + offset
    ) % MICROS_TTY_INPUT_CAPACITY;
}

static size_t tty_model_reference_echo_index(
    const struct model_reference *reference,
    size_t offset
)
{
    return (
        (size_t)reference->echo_head + offset
    ) % MICROS_TTY_ECHO_CAPACITY;
}

bool tty_model_reference_output_pending(
    const struct model_reference *reference
)
{
    return reference->echo_count != 0 || reference->write_resident;
}

static void tty_model_reference_publish_output(
    struct model_reference *reference,
    bool previously_pending
)
{
    if (!previously_pending && tty_model_reference_output_pending(reference)) {
        reference->transmit_interrupt_enabled = true;
    }
}

static void tty_model_reference_enqueue_echo(
    struct model_reference *reference,
    const uint8_t *bytes,
    size_t count
)
{
    bool previously_pending = tty_model_reference_output_pending(reference);
    size_t index;

    if (
        (size_t)reference->echo_count + count
        > MICROS_TTY_ECHO_CAPACITY
    ) {
        reference->echo_drop_count += count;
        return;
    }
    for (index = 0; index < count; ++index) {
        reference->echo[
            tty_model_reference_echo_index(
                reference,
                (size_t)reference->echo_count + index
            )
        ] = bytes[index];
    }
    reference->echo_count = (uint16_t)(
        (size_t)reference->echo_count + count
    );
    tty_model_reference_publish_output(reference, previously_pending);
}

void tty_model_reference_receive_byte(
    struct model_reference *reference,
    uint8_t byte
)
{
    static const uint8_t newline_echo[] = {'\r', '\n'};
    static const uint8_t erase_echo[] = {'\b', ' ', '\b'};
    size_t tail;

    if (byte == '\r') {
        byte = '\n';
    }
    if (byte == '\n') {
        if (reference->input_count == MICROS_TTY_INPUT_CAPACITY) {
            ++reference->input_drop_count;
            return;
        }
        tail = tty_model_reference_input_index(
            reference,
            reference->input_count
        );
        reference->input[tail].byte = byte;
        reference->input[tail].flags =
            MICROS_TTY_INPUT_END_OF_LINE;
        ++reference->input_count;
        ++reference->completed_line_count;
        reference->current_line_length = 0;
        tty_model_reference_enqueue_echo(
            reference,
            newline_echo,
            sizeof(newline_echo)
        );
        return;
    }
    if (byte == '\b' || byte == 0x7f) {
        if (reference->current_line_length == 0) {
            return;
        }
        tail = tty_model_reference_input_index(
            reference,
            reference->input_count - 1
        );
        --reference->input_count;
        --reference->current_line_length;
        reference->input[tail].byte = 0;
        reference->input[tail].flags = 0;
        tty_model_reference_enqueue_echo(
            reference,
            erase_echo,
            sizeof(erase_echo)
        );
        return;
    }
    if (byte != '\t' && byte < 0x20) {
        return;
    }
    if (
        reference->input_count
        >= MICROS_TTY_INPUT_CAPACITY - 1
    ) {
        ++reference->input_drop_count;
        return;
    }
    tail = tty_model_reference_input_index(reference, reference->input_count);
    reference->input[tail].byte = byte;
    reference->input[tail].flags = 0;
    ++reference->input_count;
    ++reference->current_line_length;
    tty_model_reference_enqueue_echo(reference, &byte, 1);
}

size_t tty_model_reference_first_line_length(
    const struct model_reference *reference
)
{
    size_t index;

    for (index = 0; index < reference->input_count; ++index) {
        if (
            (
                reference->input[
                    tty_model_reference_input_index(reference, index)
                ].flags
                & MICROS_TTY_INPUT_END_OF_LINE
            ) != 0
        ) {
            return index + 1;
        }
    }
    return 0;
}

bool tty_model_reference_consume_input(
    struct model_reference *reference,
    const uint8_t *bytes,
    size_t count
)
{
    size_t index;

    if (count == 0 || count > reference->input_count) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        size_t input_index = tty_model_reference_input_index(reference, index);

        if (reference->input[input_index].byte != bytes[index]) {
            return false;
        }
        if (
            (
                reference->input[input_index].flags
                & MICROS_TTY_INPUT_END_OF_LINE
            ) != 0
        ) {
            --reference->completed_line_count;
        }
        reference->input[input_index].byte = 0;
        reference->input[input_index].flags = 0;
    }
    reference->input_head = (uint16_t)tty_model_reference_input_index(
        reference,
        count
    );
    reference->input_count = (uint16_t)(
        reference->input_count - count
    );
    return true;
}

bool tty_model_reference_write_accept(
    struct model_reference *reference,
    const uint8_t *bytes,
    size_t count
)
{
    bool previously_pending;

    if (
        bytes == NULL
        || count == 0
        || count > sizeof(reference->write)
        || reference->write_resident
    ) {
        return false;
    }
    previously_pending = tty_model_reference_output_pending(reference);
    memcpy(reference->write, bytes, count);
    reference->write_length = (uint16_t)count;
    reference->write_cursor = 0;
    reference->write_resident = true;
    reference->write_pending_lf = false;
    tty_model_reference_publish_output(reference, previously_pending);
    return true;
}

bool tty_model_reference_write_arm_retry(
    struct model_reference *reference
)
{
    if (!reference->write_resident) {
        return false;
    }
    reference->write_retry_armed = true;
    return true;
}

bool tty_model_reference_output_take(
    struct model_reference *reference,
    uint8_t *byte,
    bool *writable
)
{
    uint8_t selected;

    if (
        byte == NULL
        || writable == NULL
        || !tty_model_reference_output_pending(reference)
    ) {
        return false;
    }
    *writable = false;
    if (reference->write_pending_lf) {
        selected = '\n';
        reference->write_pending_lf = false;
    } else if (reference->echo_count != 0) {
        selected = reference->echo[reference->echo_head];
        reference->echo[reference->echo_head] = 0;
        reference->echo_head = (uint16_t)(
            (reference->echo_head + 1) % MICROS_TTY_ECHO_CAPACITY
        );
        --reference->echo_count;
    } else {
        selected = reference->write[reference->write_cursor];
        ++reference->write_cursor;
        if (selected == '\n') {
            selected = '\r';
            reference->write_pending_lf = true;
        }
    }
    if (
        reference->write_resident
        && reference->write_cursor == reference->write_length
        && !reference->write_pending_lf
    ) {
        reference->write_resident = false;
        reference->write_length = 0;
        reference->write_cursor = 0;
        if (reference->write_retry_armed) {
            reference->write_retry_armed = false;
            *writable = true;
        }
    }
    if (!tty_model_reference_output_pending(reference)) {
        reference->transmit_interrupt_enabled = false;
    }
    *byte = selected;
    return true;
}
