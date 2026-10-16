#include "tty_model_internal.h"

bool tty_model_submit_read(
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

    tty_model_record(
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
            tty_model_reference_first_line_length(reference);

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
    message = tty_model_submit_request(
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
                .copy = tty_model_copy_grant,
                .context = model,
            },
            &action
        ) == MICROS_TTY_SERVICE_OK
    );
    MODEL_EXPECT(
        model,
        tty_model_check_reply(
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
        tty_model_commit_reply(
            model,
            &action,
            expected_notification
        )
    );
    return true;
}

bool tty_model_submit_write(
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

    tty_model_record(
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
    message = tty_model_submit_request(
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
                .copy = tty_model_copy_grant,
                .context = model,
            },
            &action
        ) == MICROS_TTY_SERVICE_OK
    );
    model->copy.reject_write_transfer = false;
    MODEL_EXPECT(
        model,
        tty_model_check_reply(
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
            tty_model_reference_write_arm_retry(reference)
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
            tty_model_reference_write_accept(reference, bytes, count)
        );
        expected_notification = MICROS_TTY_EVENT_COMPLETION;
        ++model->accepted_write_count;
    }
    MODEL_EXPECT(
        model,
        tty_model_commit_reply(
            model,
            &action,
            expected_notification
        )
    );
    return true;
}

bool tty_model_cancel(
    struct tty_model *model,
    uint64_t request_id
)
{
    struct model_reference *reference = &model->reference;
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message = tty_model_identity_request(
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

    tty_model_record(
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
                .copy = tty_model_copy_grant,
                .context = model,
            },
            &action
        ) == MICROS_TTY_SERVICE_OK
    );
    MODEL_EXPECT(
        model,
        tty_model_check_reply(
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
        tty_model_commit_reply(model, &action, 0)
    );
    return true;
}

bool tty_model_collect(
    struct tty_model *model,
    uint64_t request_id
)
{
    struct model_reference *reference = &model->reference;
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message = tty_model_identity_request(
        model,
        MICROS_TTY_MESSAGE_COLLECT,
        request_id
    );
    enum micros_tty_result expected_result;
    uint64_t expected_transferred_count = 0;
    enum micros_tty_service_release expected_release =
        MICROS_TTY_SERVICE_RELEASE_NONE;

    tty_model_record(
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
                .copy = tty_model_copy_grant,
                .context = model,
            },
            &action
        ) == MICROS_TTY_SERVICE_OK
    );
    MODEL_EXPECT(
        model,
        tty_model_check_reply(
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
        tty_model_commit_reply(model, &action, 0)
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

bool tty_model_input_irq(
    struct tty_model *model,
    uint8_t byte
)
{
    struct model_reference *reference = &model->reference;
    const struct micros_tty_uart_bus bus = {
        .read = tty_model_bus_read,
        .write = tty_model_bus_write,
        .context = model,
    };
    struct micros_tty_uart_drain_effects drain_effects;
    uint64_t notification;
    uint64_t staged_count = 0;
    bool will_complete;
    bool rejected;

    tty_model_record(model, MODEL_OPERATION_INPUT, byte);
    MODEL_EXPECT(
        model,
        !model->bus.receive_pending
        && model->bus.pending_interrupt == MODEL_IIR_NO_PENDING
    );
    tty_model_reference_receive_byte(reference, byte);
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
        staged_count = tty_model_reference_first_line_length(reference);
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
                .copy = tty_model_copy_grant,
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

bool tty_model_transmit_irq(struct tty_model *model)
{
    const struct micros_tty_uart_bus bus = {
        .read = tty_model_bus_read,
        .write = tty_model_bus_write,
        .context = model,
    };
    struct micros_tty_uart_drain_effects drain_effects;

    tty_model_record(model, MODEL_OPERATION_TRANSMIT_IRQ, 0);
    MODEL_EXPECT(
        model,
        tty_model_reference_output_pending(&model->reference)
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

bool tty_model_malformed_submit(struct tty_model *model)
{
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message message = tty_model_submit_request(
        model,
        MICROS_TTY_MESSAGE_SUBMIT_READ,
        0,
        MODEL_READ_GRANT,
        1
    );

    tty_model_record(model, MODEL_OPERATION_MALFORMED_SUBMIT, 0);
    MODEL_EXPECT(
        model,
        micros_tty_service_handle_call(
            &model->service,
            &message,
            &(const struct micros_tty_service_io){
                .copy = tty_model_copy_grant,
                .context = model,
            },
            &action
        ) == MICROS_TTY_SERVICE_OK
    );
    MODEL_EXPECT(
        model,
        tty_model_check_reply(
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
        tty_model_commit_reply(model, &action, 0)
    );
    return true;
}
