#include "servers/tty/tty_service_core.h"
#include "servers/tty/tty_uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    MODEL_RANDOM_STEP_COUNT = 8192,
    MODEL_TRACE_CAPACITY = MODEL_RANDOM_STEP_COUNT + 32,
    MODEL_READ_GRANT = 1,
    MODEL_WRITE_GRANT = 2,
    MODEL_GRANT_CAPACITY = 64,
    MODEL_WRITE_SOURCE_CAPACITY = 32,
    MODEL_IIR_NO_PENDING = 0x01,
    MODEL_IIR_TRANSMIT_EMPTY = 0x02,
    MODEL_IIR_RECEIVED_DATA = 0x04,
    MODEL_IER_BASE = 0x05,
    MODEL_IER_TRANSMIT_EMPTY = 0x02,
    MODEL_LCR_DLAB = 0x80,
    MODEL_LSR_DATA_READY = 0x01,
    MODEL_LSR_TRANSMIT_EMPTY = 0x20,
};

enum model_operation {
    MODEL_OPERATION_INPUT = 1,
    MODEL_OPERATION_SUBMIT_READ,
    MODEL_OPERATION_SUBMIT_WRITE,
    MODEL_OPERATION_CANCEL,
    MODEL_OPERATION_COLLECT,
    MODEL_OPERATION_TRANSMIT_IRQ,
    MODEL_OPERATION_MALFORMED_SUBMIT,
};

enum model_grant_mode {
    MODEL_GRANT_VALID = 0,
    MODEL_GRANT_INVALID,
    MODEL_GRANT_REJECT_TRANSFER,
};

static const micros_endpoint_t tty_endpoint = UINT32_C(0x00007003);
static const micros_endpoint_t vfs_endpoint = UINT32_C(0x00009005);

struct model_input_entry {
    uint8_t byte;
    uint8_t flags;
};

struct model_completion {
    bool active;
    enum micros_tty_result result;
    uint64_t request_id;
    uint64_t transferred_count;
};

struct model_reference {
    struct model_input_entry input[MICROS_TTY_INPUT_CAPACITY];
    uint16_t input_head;
    uint16_t input_count;
    uint16_t current_line_length;
    uint16_t completed_line_count;
    uint8_t echo[MICROS_TTY_ECHO_CAPACITY];
    uint16_t echo_head;
    uint16_t echo_count;
    uint8_t write[MODEL_WRITE_SOURCE_CAPACITY];
    uint16_t write_length;
    uint16_t write_cursor;
    bool write_resident;
    bool write_pending_lf;
    bool transmit_interrupt_enabled;
    bool write_retry_armed;
    uint64_t input_drop_count;
    uint64_t echo_drop_count;
    uint64_t last_accepted_request_id;
    bool pending_read;
    uint64_t pending_read_request_id;
    uint64_t pending_read_count;
    uint64_t rejected_read_request_id;
    struct model_completion read_completion;
    struct model_completion write_completion;
};

struct tty_model;

struct model_bus {
    struct tty_model *model;
    uint8_t line_control;
    uint8_t interrupt_enable;
    uint8_t pending_interrupt;
    uint8_t receive_byte;
    bool receive_pending;
    bool transmit_ready;
    bool expect_lf;
    bool expected_writable;
    bool failed;
    uint64_t output_count;
    uint64_t output_hash;
};

struct model_copy {
    struct tty_model *model;
    uint8_t write_source[MODEL_WRITE_SOURCE_CAPACITY];
    size_t write_source_length;
    bool reject_write_transfer;
    bool failed;
};

struct tty_model {
    uint64_t seed;
    uint64_t random_state;
    size_t step;
    size_t trace_count;
    uint32_t operation_trace[MODEL_TRACE_CAPACITY];
    struct micros_tty_service_state service;
    struct micros_tty_uart_state uart;
    struct model_reference reference;
    struct model_bus bus;
    struct model_copy copy;
    size_t input_irq_count;
    size_t transmit_irq_count;
    size_t accepted_read_count;
    size_t accepted_write_count;
    size_t accepted_cancel_count;
    size_t completed_read_count;
    size_t successful_collect_count;
    size_t rejected_grant_count;
    size_t rejected_transfer_count;
};

static bool model_failure(
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

#define MODEL_EXPECT(model, expression) \
    do { \
        if (!(expression)) { \
            return model_failure(#expression, __LINE__, model); \
        } \
    } while (false)

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

static uint64_t model_random(struct tty_model *model)
{
    uint64_t value = model->random_state;

    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    model->random_state = value;
    return value;
}

static void model_record(
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

static size_t reference_input_index(
    const struct model_reference *reference,
    size_t offset
)
{
    return (
        (size_t)reference->input_head + offset
    ) % MICROS_TTY_INPUT_CAPACITY;
}

static size_t reference_echo_index(
    const struct model_reference *reference,
    size_t offset
)
{
    return (
        (size_t)reference->echo_head + offset
    ) % MICROS_TTY_ECHO_CAPACITY;
}

static bool reference_output_pending(
    const struct model_reference *reference
)
{
    return reference->echo_count != 0 || reference->write_resident;
}

static void reference_publish_output(
    struct model_reference *reference,
    bool previously_pending
)
{
    if (!previously_pending && reference_output_pending(reference)) {
        reference->transmit_interrupt_enabled = true;
    }
}

static void reference_enqueue_echo(
    struct model_reference *reference,
    const uint8_t *bytes,
    size_t count
)
{
    bool previously_pending = reference_output_pending(reference);
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
            reference_echo_index(
                reference,
                (size_t)reference->echo_count + index
            )
        ] = bytes[index];
    }
    reference->echo_count = (uint16_t)(
        (size_t)reference->echo_count + count
    );
    reference_publish_output(reference, previously_pending);
}

static void reference_receive_byte(
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
        tail = reference_input_index(
            reference,
            reference->input_count
        );
        reference->input[tail].byte = byte;
        reference->input[tail].flags =
            MICROS_TTY_INPUT_END_OF_LINE;
        ++reference->input_count;
        ++reference->completed_line_count;
        reference->current_line_length = 0;
        reference_enqueue_echo(
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
        tail = reference_input_index(
            reference,
            reference->input_count - 1
        );
        --reference->input_count;
        --reference->current_line_length;
        reference->input[tail].byte = 0;
        reference->input[tail].flags = 0;
        reference_enqueue_echo(
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
    tail = reference_input_index(reference, reference->input_count);
    reference->input[tail].byte = byte;
    reference->input[tail].flags = 0;
    ++reference->input_count;
    ++reference->current_line_length;
    reference_enqueue_echo(reference, &byte, 1);
}

static size_t reference_first_line_length(
    const struct model_reference *reference
)
{
    size_t index;

    for (index = 0; index < reference->input_count; ++index) {
        if (
            (
                reference->input[
                    reference_input_index(reference, index)
                ].flags
                & MICROS_TTY_INPUT_END_OF_LINE
            ) != 0
        ) {
            return index + 1;
        }
    }
    return 0;
}

static bool reference_consume_input(
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
        size_t input_index = reference_input_index(reference, index);

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
    reference->input_head = (uint16_t)reference_input_index(
        reference,
        count
    );
    reference->input_count = (uint16_t)(
        reference->input_count - count
    );
    return true;
}

static bool reference_write_accept(
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
    previously_pending = reference_output_pending(reference);
    memcpy(reference->write, bytes, count);
    reference->write_length = (uint16_t)count;
    reference->write_cursor = 0;
    reference->write_resident = true;
    reference->write_pending_lf = false;
    reference_publish_output(reference, previously_pending);
    return true;
}

static bool reference_write_arm_retry(
    struct model_reference *reference
)
{
    if (!reference->write_resident) {
        return false;
    }
    reference->write_retry_armed = true;
    return true;
}

static bool reference_output_take(
    struct model_reference *reference,
    uint8_t *byte,
    bool *writable
)
{
    uint8_t selected;

    if (
        byte == NULL
        || writable == NULL
        || !reference_output_pending(reference)
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
    if (!reference_output_pending(reference)) {
        reference->transmit_interrupt_enabled = false;
    }
    *byte = selected;
    return true;
}

static enum micros_tty_service_copy_result model_copy_grant(
    void *context,
    enum micros_tty_service_copy_direction direction,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t offset,
    uint8_t *local,
    size_t length
)
{
    struct tty_model *model = context;
    struct model_reference *reference;

    if (model == NULL) {
        return MICROS_TTY_SERVICE_COPY_INVARIANT;
    }
    reference = &model->reference;
    if (
        endpoint != vfs_endpoint
        || (length != 0 && local == NULL)
    ) {
        model->copy.failed = true;
        return MICROS_TTY_SERVICE_COPY_INVARIANT;
    }
    if (grant == MODEL_READ_GRANT) {
        size_t first_line_length;
        size_t expected_length;

        if (
            direction != MICROS_TTY_SERVICE_COPY_TO_VFS
            || offset > MODEL_GRANT_CAPACITY
            || length > MODEL_GRANT_CAPACITY - (size_t)offset
        ) {
            model->copy.failed = true;
            return MICROS_TTY_SERVICE_COPY_INVARIANT;
        }
        if (length == 0) {
            return MICROS_TTY_SERVICE_COPY_OK;
        }
        if (
            !reference->pending_read
            || reference->pending_read_request_id == 0
        ) {
            model->copy.failed = true;
            return MICROS_TTY_SERVICE_COPY_INVARIANT;
        }
        if (
            reference->rejected_read_request_id
            == reference->pending_read_request_id
        ) {
            ++model->rejected_transfer_count;
            return MICROS_TTY_SERVICE_COPY_REJECTED;
        }
        first_line_length = reference_first_line_length(reference);
        expected_length = first_line_length;
        if (reference->pending_read_count < expected_length) {
            expected_length = (size_t)reference->pending_read_count;
        }
        if (
            first_line_length == 0
            || length != expected_length
            || !reference_consume_input(reference, local, length)
        ) {
            model->copy.failed = true;
            return MICROS_TTY_SERVICE_COPY_INVARIANT;
        }
        return MICROS_TTY_SERVICE_COPY_OK;
    }
    if (grant == MODEL_WRITE_GRANT) {
        if (
            direction != MICROS_TTY_SERVICE_COPY_FROM_VFS
            || offset > MODEL_GRANT_CAPACITY
            || length > MODEL_GRANT_CAPACITY - (size_t)offset
        ) {
            model->copy.failed = true;
            return MICROS_TTY_SERVICE_COPY_INVARIANT;
        }
        if (length == 0) {
            return MICROS_TTY_SERVICE_COPY_OK;
        }
        if (model->copy.reject_write_transfer) {
            model->copy.reject_write_transfer = false;
            ++model->rejected_transfer_count;
            return MICROS_TTY_SERVICE_COPY_REJECTED;
        }
        if (length != model->copy.write_source_length) {
            model->copy.failed = true;
            return MICROS_TTY_SERVICE_COPY_INVARIANT;
        }
        memcpy(local, model->copy.write_source, length);
        return MICROS_TTY_SERVICE_COPY_OK;
    }
    ++model->rejected_grant_count;
    return MICROS_TTY_SERVICE_COPY_REJECTED;
}

static uint8_t model_bus_read(void *context, uint8_t offset)
{
    struct tty_model *model = context;
    struct model_bus *bus;

    if (model == NULL) {
        return 0xff;
    }
    bus = &model->bus;
    switch (offset) {
    case MICROS_TTY_UART_REGISTER_IIR_FCR: {
        uint8_t interrupt = bus->pending_interrupt;

        bus->pending_interrupt = MODEL_IIR_NO_PENDING;
        return interrupt;
    }
    case MICROS_TTY_UART_REGISTER_LSR: {
        uint8_t status = 0;

        if (bus->receive_pending) {
            status |= MODEL_LSR_DATA_READY;
        }
        if (bus->transmit_ready) {
            status |= MODEL_LSR_TRANSMIT_EMPTY;
            bus->transmit_ready = false;
        }
        return status;
    }
    case MICROS_TTY_UART_REGISTER_RBR_THR_DLL:
        if (
            (bus->line_control & MODEL_LCR_DLAB) != 0
            || !bus->receive_pending
        ) {
            bus->failed = true;
            return 0xff;
        }
        bus->receive_pending = false;
        return bus->receive_byte;
    case MICROS_TTY_UART_REGISTER_MSR:
        return 0;
    default:
        bus->failed = true;
        return 0xff;
    }
}

static void model_bus_write(
    void *context,
    uint8_t offset,
    uint8_t value
)
{
    struct tty_model *model = context;
    struct model_bus *bus;

    if (model == NULL) {
        return;
    }
    bus = &model->bus;
    switch (offset) {
    case MICROS_TTY_UART_REGISTER_LCR:
        bus->line_control = value;
        break;
    case MICROS_TTY_UART_REGISTER_IER_DLM:
        if ((bus->line_control & MODEL_LCR_DLAB) == 0) {
            bus->interrupt_enable = value;
        }
        break;
    case MICROS_TTY_UART_REGISTER_RBR_THR_DLL:
        if ((bus->line_control & MODEL_LCR_DLAB) != 0) {
            break;
        } else {
            uint8_t expected;
            bool writable;

            if (
                !reference_output_take(
                    &model->reference,
                    &expected,
                    &writable
                )
                || expected != value
                || (bus->expect_lf && value != '\n')
            ) {
                bus->failed = true;
            }
            bus->expect_lf = value == '\r';
            bus->expected_writable |= writable;
            bus->output_hash ^= value;
            bus->output_hash *= UINT64_C(1099511628211);
            ++bus->output_count;
        }
        break;
    case MICROS_TTY_UART_REGISTER_IIR_FCR:
    case MICROS_TTY_UART_REGISTER_MCR:
        break;
    default:
        bus->failed = true;
        break;
    }
}

static struct micros_ipc_message submit_request(
    struct tty_model *model,
    uint32_t type,
    uint64_t request_id,
    micros_grant_t grant,
    uint64_t count
)
{
    struct micros_ipc_message message = {
        .source = vfs_endpoint,
        .type = type,
        .reply_token = UINT64_C(0x1000) + model->step,
    };

    write_u32_le(&message.payload[0], MICROS_TTY_PROTOCOL_VERSION);
    write_u64_le(&message.payload[8], request_id);
    write_u32_le(&message.payload[16], grant);
    write_u64_le(&message.payload[24], 0);
    write_u64_le(&message.payload[32], count);
    return message;
}

static struct micros_ipc_message identity_request(
    struct tty_model *model,
    uint32_t type,
    uint64_t request_id
)
{
    struct micros_ipc_message message = {
        .source = vfs_endpoint,
        .type = type,
        .reply_token = UINT64_C(0x2000) + model->step,
    };

    write_u32_le(&message.payload[0], MICROS_TTY_PROTOCOL_VERSION);
    write_u64_le(&message.payload[8], request_id);
    return message;
}

static bool model_check_reply(
    struct tty_model *model,
    const struct micros_tty_service_reply_action *action,
    uint32_t request_type,
    enum micros_tty_result expected_result,
    uint64_t expected_request_id,
    uint64_t expected_transferred_count
)
{
    MODEL_EXPECT(model, action->active);
    MODEL_EXPECT(model, action->reply_token != 0);
    MODEL_EXPECT(
        model,
        action->message.type == MICROS_TTY_MESSAGE_RESULT
    );
    MODEL_EXPECT(
        model,
        read_u32_le(&action->message.payload[0])
            == MICROS_TTY_PROTOCOL_VERSION
    );
    MODEL_EXPECT(
        model,
        read_u32_le(&action->message.payload[4]) == request_type
    );
    MODEL_EXPECT(
        model,
        (int32_t)read_u32_le(&action->message.payload[8])
            == (int32_t)expected_result
    );
    MODEL_EXPECT(
        model,
        read_u64_le(&action->message.payload[16])
            == expected_request_id
    );
    MODEL_EXPECT(
        model,
        read_u64_le(&action->message.payload[24])
            == expected_transferred_count
    );
    return true;
}

static bool model_commit_reply(
    struct tty_model *model,
    struct micros_tty_service_reply_action *action,
    uint64_t expected_notification
)
{
    const struct micros_tty_uart_bus bus = {
        .read = model_bus_read,
        .write = model_bus_write,
        .context = model,
    };
    uint64_t notification;

    MODEL_EXPECT(model, !action->uart_effects.writable);
    MODEL_EXPECT(
        model,
        micros_tty_uart_apply_effects(
            &model->uart,
            &bus,
            &model->service.terminal,
            &action->uart_effects
        ) == MICROS_TTY_UART_OK
    );
    MODEL_EXPECT(
        model,
        micros_tty_service_commit_reply(
            &model->service,
            action,
            &notification
        ) == MICROS_TTY_SERVICE_OK
    );
    MODEL_EXPECT(model, notification == expected_notification);
    return true;
}

static bool model_submit_read(
    struct tty_model *model,
    uint64_t count,
    enum model_grant_mode grant_mode
)
{
    struct model_reference *reference = &model->reference;
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message;
    uint64_t request_id = reference->last_accepted_request_id + 1;
    uint64_t staged_count = 0;
    enum micros_tty_result expected_result;
    bool busy = (
        reference->pending_read
        || reference->read_completion.active
    );
    bool immediate = false;
    uint64_t expected_notification = 0;
    micros_grant_t grant = (
        grant_mode == MODEL_GRANT_INVALID
        ? UINT32_C(99)
        : MODEL_READ_GRANT
    );

    model_record(
        model,
        MODEL_OPERATION_SUBMIT_READ,
        (uint32_t)count << 4 | (uint32_t)grant_mode
    );
    if (busy) {
        expected_result = MICROS_TTY_RESULT_BUSY;
    } else if (grant_mode == MODEL_GRANT_INVALID) {
        expected_result = MICROS_TTY_RESULT_GRANT;
    } else {
        size_t first_line_length =
            reference_first_line_length(reference);

        expected_result = MICROS_TTY_RESULT_OK;
        reference->last_accepted_request_id = request_id;
        reference->pending_read = true;
        reference->pending_read_request_id = request_id;
        reference->pending_read_count = count;
        if (grant_mode == MODEL_GRANT_REJECT_TRANSFER) {
            reference->rejected_read_request_id = request_id;
        }
        if (first_line_length != 0) {
            staged_count = first_line_length;
            if (count < staged_count) {
                staged_count = count;
            }
            immediate = true;
        }
    }
    message = submit_request(
        model,
        MICROS_TTY_MESSAGE_SUBMIT_READ,
        request_id,
        grant,
        count
    );
    MODEL_EXPECT(
        model,
        micros_tty_service_handle_call(
            &model->service,
            &message,
            &(const struct micros_tty_service_io){
                .copy = model_copy_grant,
                .context = model,
            },
            &action
        ) == MICROS_TTY_SERVICE_OK
    );
    MODEL_EXPECT(
        model,
        model_check_reply(
            model,
            &action,
            MICROS_TTY_MESSAGE_SUBMIT_READ,
            expected_result,
            request_id,
            0
        )
    );
    if (expected_result == MICROS_TTY_RESULT_OK) {
        ++model->accepted_read_count;
        if (immediate) {
            reference->pending_read = false;
            reference->pending_read_request_id = 0;
            reference->pending_read_count = 0;
            reference->read_completion = (struct model_completion){
                .active = true,
                .result = (
                    grant_mode == MODEL_GRANT_REJECT_TRANSFER
                    ? MICROS_TTY_RESULT_GRANT
                    : MICROS_TTY_RESULT_OK
                ),
                .request_id = request_id,
                .transferred_count = (
                    grant_mode == MODEL_GRANT_REJECT_TRANSFER
                    ? 0
                    : staged_count
                ),
            };
            reference->rejected_read_request_id = 0;
            expected_notification = MICROS_TTY_EVENT_COMPLETION;
            ++model->completed_read_count;
        }
    }
    MODEL_EXPECT(
        model,
        model_commit_reply(
            model,
            &action,
            expected_notification
        )
    );
    return true;
}

static bool model_submit_write(
    struct tty_model *model,
    const uint8_t *bytes,
    size_t count,
    enum model_grant_mode grant_mode
)
{
    struct model_reference *reference = &model->reference;
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message;
    uint64_t request_id = reference->last_accepted_request_id + 1;
    enum micros_tty_result expected_result;
    bool busy_completion = reference->write_completion.active;
    bool busy_resident = reference->write_resident;
    micros_grant_t grant = (
        grant_mode == MODEL_GRANT_INVALID
        ? UINT32_C(99)
        : MODEL_WRITE_GRANT
    );
    uint64_t expected_notification = 0;

    model_record(
        model,
        MODEL_OPERATION_SUBMIT_WRITE,
        (uint32_t)count << 4 | (uint32_t)grant_mode
    );
    MODEL_EXPECT(
        model,
        bytes != NULL
        && count != 0
        && count <= sizeof(model->copy.write_source)
    );
    memcpy(model->copy.write_source, bytes, count);
    model->copy.write_source_length = count;
    model->copy.reject_write_transfer =
        grant_mode == MODEL_GRANT_REJECT_TRANSFER;
    if (busy_completion || busy_resident) {
        expected_result = MICROS_TTY_RESULT_BUSY;
    } else if (grant_mode != MODEL_GRANT_VALID) {
        expected_result = MICROS_TTY_RESULT_GRANT;
    } else {
        expected_result = MICROS_TTY_RESULT_OK;
    }
    message = submit_request(
        model,
        MICROS_TTY_MESSAGE_SUBMIT_WRITE,
        request_id,
        grant,
        count
    );
    MODEL_EXPECT(
        model,
        micros_tty_service_handle_call(
            &model->service,
            &message,
            &(const struct micros_tty_service_io){
                .copy = model_copy_grant,
                .context = model,
            },
            &action
        ) == MICROS_TTY_SERVICE_OK
    );
    model->copy.reject_write_transfer = false;
    MODEL_EXPECT(
        model,
        model_check_reply(
            model,
            &action,
            MICROS_TTY_MESSAGE_SUBMIT_WRITE,
            expected_result,
            request_id,
            0
        )
    );
    if (
        expected_result == MICROS_TTY_RESULT_BUSY
        && !busy_completion
    ) {
        MODEL_EXPECT(
            model,
            reference_write_arm_retry(reference)
        );
    } else if (expected_result == MICROS_TTY_RESULT_OK) {
        reference->last_accepted_request_id = request_id;
        reference->write_completion = (struct model_completion){
            .active = true,
            .result = MICROS_TTY_RESULT_OK,
            .request_id = request_id,
            .transferred_count = count,
        };
        MODEL_EXPECT(
            model,
            reference_write_accept(reference, bytes, count)
        );
        expected_notification = MICROS_TTY_EVENT_COMPLETION;
        ++model->accepted_write_count;
    }
    MODEL_EXPECT(
        model,
        model_commit_reply(
            model,
            &action,
            expected_notification
        )
    );
    return true;
}

static bool model_cancel(
    struct tty_model *model,
    uint64_t request_id
)
{
    struct model_reference *reference = &model->reference;
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message = identity_request(
        model,
        MICROS_TTY_MESSAGE_CANCEL,
        request_id
    );
    enum micros_tty_result expected_result = (
        reference->pending_read
        && reference->pending_read_request_id == request_id
        ? MICROS_TTY_RESULT_OK
        : MICROS_TTY_RESULT_REQUEST
    );

    model_record(
        model,
        MODEL_OPERATION_CANCEL,
        (uint32_t)request_id
    );
    MODEL_EXPECT(
        model,
        micros_tty_service_handle_call(
            &model->service,
            &message,
            &(const struct micros_tty_service_io){
                .copy = model_copy_grant,
                .context = model,
            },
            &action
        ) == MICROS_TTY_SERVICE_OK
    );
    MODEL_EXPECT(
        model,
        model_check_reply(
            model,
            &action,
            MICROS_TTY_MESSAGE_CANCEL,
            expected_result,
            request_id,
            0
        )
    );
    if (expected_result == MICROS_TTY_RESULT_OK) {
        reference->pending_read = false;
        reference->pending_read_request_id = 0;
        reference->pending_read_count = 0;
        reference->rejected_read_request_id = 0;
        ++model->accepted_cancel_count;
    }
    MODEL_EXPECT(
        model,
        model_commit_reply(model, &action, 0)
    );
    return true;
}

static bool model_collect(
    struct tty_model *model,
    uint64_t request_id
)
{
    struct model_reference *reference = &model->reference;
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message = identity_request(
        model,
        MICROS_TTY_MESSAGE_COLLECT,
        request_id
    );
    enum micros_tty_result expected_result;
    uint64_t expected_transferred_count = 0;
    enum micros_tty_service_release expected_release =
        MICROS_TTY_SERVICE_RELEASE_NONE;

    model_record(
        model,
        MODEL_OPERATION_COLLECT,
        (uint32_t)request_id
    );
    if (
        reference->pending_read
        && reference->pending_read_request_id == request_id
    ) {
        expected_result = MICROS_TTY_RESULT_PENDING;
    } else if (
        reference->read_completion.active
        && reference->read_completion.request_id == request_id
    ) {
        expected_result = reference->read_completion.result;
        expected_transferred_count =
            reference->read_completion.transferred_count;
        expected_release = MICROS_TTY_SERVICE_RELEASE_READ;
    } else if (
        reference->write_completion.active
        && reference->write_completion.request_id == request_id
    ) {
        expected_result = reference->write_completion.result;
        expected_transferred_count =
            reference->write_completion.transferred_count;
        expected_release = MICROS_TTY_SERVICE_RELEASE_WRITE;
    } else {
        expected_result = MICROS_TTY_RESULT_REQUEST;
    }
    MODEL_EXPECT(
        model,
        micros_tty_service_handle_call(
            &model->service,
            &message,
            &(const struct micros_tty_service_io){
                .copy = model_copy_grant,
                .context = model,
            },
            &action
        ) == MICROS_TTY_SERVICE_OK
    );
    MODEL_EXPECT(
        model,
        model_check_reply(
            model,
            &action,
            MICROS_TTY_MESSAGE_COLLECT,
            expected_result,
            request_id,
            expected_transferred_count
        )
    );
    MODEL_EXPECT(model, action.release == expected_release);
    MODEL_EXPECT(
        model,
        model_commit_reply(model, &action, 0)
    );
    if (expected_release == MICROS_TTY_SERVICE_RELEASE_READ) {
        memset(
            &reference->read_completion,
            0,
            sizeof(reference->read_completion)
        );
        ++model->successful_collect_count;
    } else if (
        expected_release == MICROS_TTY_SERVICE_RELEASE_WRITE
    ) {
        memset(
            &reference->write_completion,
            0,
            sizeof(reference->write_completion)
        );
        ++model->successful_collect_count;
    }
    return true;
}

static bool model_input_irq(
    struct tty_model *model,
    uint8_t byte
)
{
    struct model_reference *reference = &model->reference;
    const struct micros_tty_uart_bus bus = {
        .read = model_bus_read,
        .write = model_bus_write,
        .context = model,
    };
    struct micros_tty_uart_drain_effects drain_effects;
    uint64_t notification;
    uint64_t staged_count = 0;
    bool will_complete;
    bool rejected;

    model_record(model, MODEL_OPERATION_INPUT, byte);
    MODEL_EXPECT(
        model,
        !model->bus.receive_pending
        && model->bus.pending_interrupt == MODEL_IIR_NO_PENDING
    );
    reference_receive_byte(reference, byte);
    will_complete = (
        reference->pending_read
        && reference->completed_line_count != 0
    );
    rejected = (
        will_complete
        && reference->rejected_read_request_id
            == reference->pending_read_request_id
    );
    if (will_complete) {
        staged_count = reference_first_line_length(reference);
        if (reference->pending_read_count < staged_count) {
            staged_count = reference->pending_read_count;
        }
    }
    model->bus.receive_byte = byte;
    model->bus.receive_pending = true;
    model->bus.pending_interrupt = MODEL_IIR_RECEIVED_DATA;
    model->bus.expected_writable = false;
    MODEL_EXPECT(
        model,
        micros_tty_uart_drain(
            &model->uart,
            &bus,
            &model->service.terminal,
            &drain_effects
        ) == MICROS_TTY_UART_OK
    );
    MODEL_EXPECT(model, !drain_effects.writable);
    MODEL_EXPECT(
        model,
        micros_tty_service_complete_read(
            &model->service,
            &(const struct micros_tty_service_io){
                .copy = model_copy_grant,
                .context = model,
            },
            &notification
        ) == MICROS_TTY_SERVICE_OK
    );
    if (will_complete) {
        uint64_t request_id = reference->pending_read_request_id;

        reference->pending_read = false;
        reference->pending_read_request_id = 0;
        reference->pending_read_count = 0;
        reference->rejected_read_request_id = 0;
        reference->read_completion = (struct model_completion){
            .active = true,
            .result = (
                rejected
                ? MICROS_TTY_RESULT_GRANT
                : MICROS_TTY_RESULT_OK
            ),
            .request_id = request_id,
            .transferred_count = rejected ? 0 : staged_count,
        };
        MODEL_EXPECT(
            model,
            notification == MICROS_TTY_EVENT_COMPLETION
        );
        ++model->completed_read_count;
    } else {
        MODEL_EXPECT(model, notification == 0);
    }
    ++model->input_irq_count;
    return true;
}

static bool model_transmit_irq(struct tty_model *model)
{
    const struct micros_tty_uart_bus bus = {
        .read = model_bus_read,
        .write = model_bus_write,
        .context = model,
    };
    struct micros_tty_uart_drain_effects drain_effects;

    model_record(model, MODEL_OPERATION_TRANSMIT_IRQ, 0);
    MODEL_EXPECT(
        model,
        reference_output_pending(&model->reference)
        && model->bus.pending_interrupt == MODEL_IIR_NO_PENDING
    );
    model->bus.pending_interrupt = MODEL_IIR_TRANSMIT_EMPTY;
    model->bus.transmit_ready = true;
    model->bus.expected_writable = false;
    MODEL_EXPECT(
        model,
        micros_tty_uart_drain(
            &model->uart,
            &bus,
            &model->service.terminal,
            &drain_effects
        ) == MICROS_TTY_UART_OK
    );
    MODEL_EXPECT(
        model,
        drain_effects.writable == model->bus.expected_writable
    );
    ++model->transmit_irq_count;
    return true;
}

static bool model_malformed_submit(struct tty_model *model)
{
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message = submit_request(
        model,
        MICROS_TTY_MESSAGE_SUBMIT_READ,
        0,
        MODEL_READ_GRANT,
        1
    );

    model_record(model, MODEL_OPERATION_MALFORMED_SUBMIT, 0);
    MODEL_EXPECT(
        model,
        micros_tty_service_handle_call(
            &model->service,
            &message,
            &(const struct micros_tty_service_io){
                .copy = model_copy_grant,
                .context = model,
            },
            &action
        ) == MICROS_TTY_SERVICE_OK
    );
    MODEL_EXPECT(
        model,
        model_check_reply(
            model,
            &action,
            MICROS_TTY_MESSAGE_SUBMIT_READ,
            MICROS_TTY_RESULT_MALFORMED,
            0,
            0
        )
    );
    MODEL_EXPECT(
        model,
        model_commit_reply(model, &action, 0)
    );
    return true;
}

static bool model_validate(struct tty_model *model)
{
    const struct micros_tty_state *terminal =
        &model->service.terminal;
    const struct model_reference *reference = &model->reference;
    size_t index;

    MODEL_EXPECT(
        model,
        micros_tty_service_validate(&model->service)
            == MICROS_TTY_SERVICE_OK
    );
    MODEL_EXPECT(
        model,
        micros_tty_uart_validate(&model->uart, terminal)
            == MICROS_TTY_UART_OK
    );
    MODEL_EXPECT(model, !model->bus.failed && !model->copy.failed);
    MODEL_EXPECT(
        model,
        model->bus.pending_interrupt == MODEL_IIR_NO_PENDING
        && !model->bus.receive_pending
        && !model->bus.transmit_ready
    );
    MODEL_EXPECT(
        model,
        model->bus.interrupt_enable
            == (
                MODEL_IER_BASE
                | (
                    reference->transmit_interrupt_enabled
                    ? MODEL_IER_TRANSMIT_EMPTY
                    : 0
                )
            )
    );
    MODEL_EXPECT(
        model,
        model->bus.interrupt_enable == model->uart.interrupt_enable
    );
    MODEL_EXPECT(
        model,
        terminal->input_head == reference->input_head
        && terminal->input_count == reference->input_count
        && terminal->current_line_length
            == reference->current_line_length
        && terminal->completed_line_count
            == reference->completed_line_count
        && terminal->input_drop_count
            == reference->input_drop_count
    );
    for (index = 0; index < MICROS_TTY_INPUT_CAPACITY; ++index) {
        MODEL_EXPECT(
            model,
            terminal->input[index].byte
                == reference->input[index].byte
            && terminal->input[index].flags
                == reference->input[index].flags
        );
    }
    MODEL_EXPECT(
        model,
        terminal->echo_head == reference->echo_head
        && terminal->echo_count == reference->echo_count
        && terminal->echo_drop_count == reference->echo_drop_count
        && memcmp(
            terminal->echo,
            reference->echo,
            sizeof(reference->echo)
        ) == 0
    );
    MODEL_EXPECT(
        model,
        terminal->write_length == reference->write_length
        && terminal->write_cursor == reference->write_cursor
        && terminal->write_resident == reference->write_resident
        && terminal->write_pending_lf
            == reference->write_pending_lf
        && terminal->transmit_interrupt_enabled
            == reference->transmit_interrupt_enabled
        && terminal->write_retry_armed
            == reference->write_retry_armed
    );
    if (reference->write_resident) {
        MODEL_EXPECT(
            model,
            memcmp(
                terminal->write,
                reference->write,
                reference->write_length
            ) == 0
        );
    }
    MODEL_EXPECT(
        model,
        model->service.last_accepted_request_id
            == reference->last_accepted_request_id
    );
    MODEL_EXPECT(
        model,
        model->service.pending_read.active
            == reference->pending_read
    );
    if (reference->pending_read) {
        MODEL_EXPECT(
            model,
            model->service.pending_read.request_id
                == reference->pending_read_request_id
            && model->service.pending_read.grant
                == MODEL_READ_GRANT
            && model->service.pending_read.grant_offset == 0
            && model->service.pending_read.count
                == reference->pending_read_count
        );
    }
    MODEL_EXPECT(
        model,
        model->service.read_completion.active
            == reference->read_completion.active
    );
    if (reference->read_completion.active) {
        MODEL_EXPECT(
            model,
            model->service.read_completion.result
                == reference->read_completion.result
            && model->service.read_completion.request_id
                == reference->read_completion.request_id
            && model->service.read_completion.transferred_count
                == reference->read_completion.transferred_count
        );
    }
    MODEL_EXPECT(
        model,
        model->service.write_completion.active
            == reference->write_completion.active
    );
    if (reference->write_completion.active) {
        MODEL_EXPECT(
            model,
            model->service.write_completion.result
                == reference->write_completion.result
            && model->service.write_completion.request_id
                == reference->write_completion.request_id
            && model->service.write_completion.transferred_count
                == reference->write_completion.transferred_count
        );
    }
    MODEL_EXPECT(
        model,
        (
            reference->rejected_read_request_id == 0
            || (
                reference->pending_read
                && reference->rejected_read_request_id
                    == reference->pending_read_request_id
            )
        )
    );
    MODEL_EXPECT(
        model,
        !reference->write_pending_lf || model->bus.expect_lf
    );
    MODEL_EXPECT(
        model,
        !model->bus.expect_lf || reference_output_pending(reference)
    );
    return true;
}

static uint64_t model_collect_request_id(
    const struct tty_model *model,
    uint64_t random
)
{
    const struct model_reference *reference = &model->reference;

    switch (random % 4) {
    case 0:
        if (reference->pending_read) {
            return reference->pending_read_request_id;
        }
        break;
    case 1:
        if (reference->read_completion.active) {
            return reference->read_completion.request_id;
        }
        break;
    case 2:
        if (reference->write_completion.active) {
            return reference->write_completion.request_id;
        }
        break;
    default:
        break;
    }
    return reference->last_accepted_request_id + 1;
}

static bool model_random_transition(
    struct tty_model *model,
    uint64_t random
)
{
    static const uint8_t input_bytes[] = {
        'a', 'b', 'c', 'X', '\r', '\n', '\b', 0x7f, '\t', 0x01,
    };
    uint32_t operation = (uint32_t)(random % 7);

    if (
        model->reference.input_count
            >= MICROS_TTY_INPUT_CAPACITY - 1
        && model->reference.completed_line_count == 0
    ) {
        operation = 0;
        random = (random & ~UINT64_C(0xff)) | '\n';
    }
    switch (operation) {
    case 0: {
        uint8_t byte = input_bytes[
            (random >> 8)
            % (sizeof(input_bytes) / sizeof(input_bytes[0]))
        ];

        if ((random & UINT64_C(0xff)) == '\n') {
            byte = '\n';
        }
        return model_input_irq(model, byte);
    }
    case 1: {
        enum model_grant_mode mode = MODEL_GRANT_VALID;
        uint64_t selector = (random >> 16) % 8;

        if (selector == 0) {
            mode = MODEL_GRANT_INVALID;
        } else if (selector == 1) {
            mode = MODEL_GRANT_REJECT_TRANSFER;
        }
        return model_submit_read(
            model,
            1 + ((random >> 24) % 32),
            mode
        );
    }
    case 2: {
        uint8_t bytes[MODEL_WRITE_SOURCE_CAPACITY];
        enum model_grant_mode mode = MODEL_GRANT_VALID;
        size_t count = 1 + (size_t)((random >> 24) % sizeof(bytes));
        uint64_t selector = (random >> 16) % 8;
        size_t index;

        if (selector == 0) {
            mode = MODEL_GRANT_INVALID;
        } else if (selector == 1) {
            mode = MODEL_GRANT_REJECT_TRANSFER;
        }
        for (index = 0; index < count; ++index) {
            uint64_t value = model_random(model);

            bytes[index] = (
                (value & 7) == 0
                ? '\n'
                : (uint8_t)('A' + value % 26)
            );
        }
        return model_submit_write(model, bytes, count, mode);
    }
    case 3: {
        uint64_t request_id = (
            model->reference.pending_read
            && ((random >> 8) & 1) != 0
            ? model->reference.pending_read_request_id
            : model->reference.last_accepted_request_id + 1
        );

        return model_cancel(model, request_id);
    }
    case 4:
        return model_collect(
            model,
            model_collect_request_id(model, random >> 8)
        );
    case 5:
        if (reference_output_pending(&model->reference)) {
            return model_transmit_irq(model);
        }
        return model_input_irq(
            model,
            input_bytes[
                (random >> 8)
                % (sizeof(input_bytes) / sizeof(input_bytes[0]))
            ]
        );
    case 6:
        return model_malformed_submit(model);
    default:
        return false;
    }
}

static bool model_scripted_transition(struct tty_model *model)
{
    static const uint8_t fifo_boundary_write[] = {
        'w', 'w', 'w', 'w', 'w', 'w', 'w', 'w',
        'w', 'w', 'w', 'w', 'w', 'w', 'w', '\n',
    };
    static const uint8_t rejected_write[] = {'r', '\n'};

    switch (model->step) {
    case 0:
        return model_submit_read(model, 8, MODEL_GRANT_VALID);
    case 1:
        return model_input_irq(model, 'x');
    case 2:
        return model_cancel(
            model,
            model->reference.pending_read_request_id
        );
    case 3:
        return model_submit_read(model, 8, MODEL_GRANT_VALID);
    case 4:
        return model_input_irq(model, '\r');
    case 5:
        return model_collect(
            model,
            model->reference.read_completion.request_id
        );
    case 6:
        return model_transmit_irq(model);
    case 7:
        return model_submit_write(
            model,
            fifo_boundary_write,
            sizeof(fifo_boundary_write),
            MODEL_GRANT_VALID
        );
    case 8:
        return model_collect(
            model,
            model->reference.write_completion.request_id
        );
    case 9:
        return model_submit_write(
            model,
            rejected_write,
            sizeof(rejected_write),
            MODEL_GRANT_VALID
        );
    case 10:
        return model_transmit_irq(model);
    case 11:
        return model_input_irq(model, 'E');
    case 12:
        return model_transmit_irq(model);
    case 13:
        return model_submit_read(model, 8, MODEL_GRANT_INVALID);
    case 14:
        return model_submit_write(
            model,
            rejected_write,
            sizeof(rejected_write),
            MODEL_GRANT_REJECT_TRANSFER
        );
    case 15:
        return model_submit_read(
            model,
            8,
            MODEL_GRANT_REJECT_TRANSFER
        );
    case 16:
        return model_input_irq(model, '\r');
    case 17:
        return model_collect(
            model,
            model->reference.read_completion.request_id
        );
    case 18:
        return model_submit_read(model, 8, MODEL_GRANT_VALID);
    case 19:
        return model_collect(
            model,
            model->reference.read_completion.request_id
        );
    default:
        return false;
    }
}

static bool test_replayable_mixed_transition_model(void)
{
    const uint64_t seed = UINT64_C(0x4d4943524f535454);
    struct tty_model model;
    const struct micros_tty_uart_bus bus = {
        .read = model_bus_read,
        .write = model_bus_write,
        .context = &model,
    };
    size_t step;

    memset(&model, 0, sizeof(model));
    model.seed = seed;
    model.random_state = seed;
    model.bus.model = &model;
    model.bus.pending_interrupt = MODEL_IIR_NO_PENDING;
    model.bus.output_hash = UINT64_C(1469598103934665603);
    model.copy.model = &model;
    MODEL_EXPECT(
        &model,
        micros_tty_service_initialize(
            &model.service,
            tty_endpoint,
            vfs_endpoint
        ) == MICROS_TTY_SERVICE_OK
    );
    MODEL_EXPECT(
        &model,
        micros_tty_uart_initialize(
            &model.uart,
            &bus,
            &model.service.terminal
        ) == MICROS_TTY_UART_OK
    );
    MODEL_EXPECT(
        &model,
        micros_tty_service_commit_ownership(&model.service)
            == MICROS_TTY_SERVICE_OK
    );
    MODEL_EXPECT(&model, model_validate(&model));

    for (step = 0; step < MODEL_RANDOM_STEP_COUNT; ++step) {
        model.step = step;
        if (step < 20) {
            MODEL_EXPECT(
                &model,
                model_scripted_transition(&model)
            );
        } else {
            MODEL_EXPECT(
                &model,
                model_random_transition(
                    &model,
                    model_random(&model)
                )
            );
        }
        MODEL_EXPECT(&model, model_validate(&model));
    }
    step = MODEL_RANDOM_STEP_COUNT;
    while (reference_output_pending(&model.reference)) {
        MODEL_EXPECT(&model, step < MODEL_TRACE_CAPACITY);
        model.step = step;
        MODEL_EXPECT(&model, model_transmit_irq(&model));
        MODEL_EXPECT(&model, model_validate(&model));
        ++step;
    }
    model.step = step == 0 ? 0 : step - 1;
    MODEL_EXPECT(&model, !model.bus.expect_lf);
    MODEL_EXPECT(&model, model.accepted_read_count != 0);
    MODEL_EXPECT(&model, model.accepted_write_count != 0);
    MODEL_EXPECT(&model, model.accepted_cancel_count != 0);
    MODEL_EXPECT(&model, model.completed_read_count != 0);
    MODEL_EXPECT(&model, model.successful_collect_count != 0);
    MODEL_EXPECT(&model, model.rejected_grant_count != 0);
    MODEL_EXPECT(&model, model.rejected_transfer_count != 0);
    MODEL_EXPECT(&model, model.input_irq_count != 0);
    MODEL_EXPECT(&model, model.transmit_irq_count != 0);
    printf(
        "TTY_MODEL_PASS seed=0x%016llx steps=%zu trace=0x%016llx\n",
        (unsigned long long)seed,
        model.trace_count,
        (unsigned long long)model.bus.output_hash
    );
    return true;
}

int main(void)
{
    return test_replayable_mixed_transition_model() ? 0 : 1;
}
