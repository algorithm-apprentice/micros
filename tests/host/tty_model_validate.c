#include "tty_model_internal.h"

bool tty_model_validate(struct tty_model *model)
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
        !model->bus.expect_lf || tty_model_reference_output_pending(reference)
    );
    return true;
}
