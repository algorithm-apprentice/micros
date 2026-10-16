#include "tty_model_internal.h"

struct tty_model_validation_work {
    const struct micros_tty_state *terminal;
    const struct model_reference *reference;
    size_t index;
};

static bool tty_model_compare_resident_buffers(
    struct tty_model *model,
    struct tty_model_validation_work *work
)
{
    MODEL_EXPECT(
        model,
        work->terminal->input_head == work->reference->input_head
        && work->terminal->input_count == work->reference->input_count
        && work->terminal->current_line_length
            == work->reference->current_line_length
        && work->terminal->completed_line_count
            == work->reference->completed_line_count
        && work->terminal->input_drop_count
            == work->reference->input_drop_count
    );
    for (work->index = 0; work->index < MICROS_TTY_INPUT_CAPACITY; ++work->index) {
        MODEL_EXPECT(
            model,
            work->terminal->input[work->index].byte
                == work->reference->input[work->index].byte
            && work->terminal->input[work->index].flags
                == work->reference->input[work->index].flags
        );
    }
    MODEL_EXPECT(
        model,
        work->terminal->echo_head == work->reference->echo_head
        && work->terminal->echo_count == work->reference->echo_count
        && work->terminal->echo_drop_count == work->reference->echo_drop_count
        && memcmp(
            work->terminal->echo,
            work->reference->echo,
            sizeof(work->reference->echo)
        ) == 0
    );
    MODEL_EXPECT(
        model,
        work->terminal->write_length == work->reference->write_length
        && work->terminal->write_cursor == work->reference->write_cursor
        && work->terminal->write_resident == work->reference->write_resident
        && work->terminal->write_pending_lf
            == work->reference->write_pending_lf
        && work->terminal->transmit_interrupt_enabled
            == work->reference->transmit_interrupt_enabled
        && work->terminal->write_retry_armed
            == work->reference->write_retry_armed
    );
    if (work->reference->write_resident) {
        MODEL_EXPECT(
            model,
            memcmp(
                work->terminal->write,
                work->reference->write,
                work->reference->write_length
            ) == 0
        );
    }
    return true;
}

bool tty_model_validate(struct tty_model *model)
{
    struct tty_model_validation_work work;

    work.terminal = &model->service.terminal;
    work.reference = &model->reference;
    MODEL_EXPECT(
        model,
        micros_tty_service_validate(&model->service)
            == MICROS_TTY_SERVICE_OK
    );
    MODEL_EXPECT(
        model,
        micros_tty_uart_validate(&model->uart, work.terminal)
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
                    work.reference->transmit_interrupt_enabled
                    ? MODEL_IER_TRANSMIT_EMPTY
                    : 0
                )
            )
    );
    MODEL_EXPECT(
        model,
        model->bus.interrupt_enable == model->uart.interrupt_enable
    );
    if (!tty_model_compare_resident_buffers(model, &work)) {
        return false;
    }
    MODEL_EXPECT(
        model,
        model->service.last_accepted_request_id
            == work.reference->last_accepted_request_id
    );
    MODEL_EXPECT(
        model,
        model->service.pending_read.active
            == work.reference->pending_read
    );
    if (work.reference->pending_read) {
        MODEL_EXPECT(
            model,
            model->service.pending_read.request_id
                == work.reference->pending_read_request_id
            && model->service.pending_read.grant
                == MODEL_READ_GRANT
            && model->service.pending_read.grant_offset == 0
            && model->service.pending_read.count
                == work.reference->pending_read_count
        );
    }
    MODEL_EXPECT(
        model,
        model->service.read_completion.active
            == work.reference->read_completion.active
    );
    if (work.reference->read_completion.active) {
        MODEL_EXPECT(
            model,
            model->service.read_completion.result
                == work.reference->read_completion.result
            && model->service.read_completion.request_id
                == work.reference->read_completion.request_id
            && model->service.read_completion.transferred_count
                == work.reference->read_completion.transferred_count
        );
    }
    MODEL_EXPECT(
        model,
        model->service.write_completion.active
            == work.reference->write_completion.active
    );
    if (work.reference->write_completion.active) {
        MODEL_EXPECT(
            model,
            model->service.write_completion.result
                == work.reference->write_completion.result
            && model->service.write_completion.request_id
                == work.reference->write_completion.request_id
            && model->service.write_completion.transferred_count
                == work.reference->write_completion.transferred_count
        );
    }
    MODEL_EXPECT(
        model,
        (
            work.reference->rejected_read_request_id == 0
            || (
                work.reference->pending_read
                && work.reference->rejected_read_request_id
                    == work.reference->pending_read_request_id
            )
        )
    );
    MODEL_EXPECT(
        model,
        !work.reference->write_pending_lf || model->bus.expect_lf
    );
    MODEL_EXPECT(
        model,
        !model->bus.expect_lf || tty_model_reference_output_pending(work.reference)
    );
    return true;
}
