#include "tty_model_internal.h"

static uint64_t tty_model_collect_request_id(
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

static bool tty_model_random_transition(
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
        return tty_model_input_irq(model, byte);
    }
    case 1: {
        enum model_grant_mode mode = MODEL_GRANT_VALID;
        uint64_t selector = (random >> 16) % 8;

        if (selector == 0) {
            mode = MODEL_GRANT_INVALID;
        } else if (selector == 1) {
            mode = MODEL_GRANT_REJECT_TRANSFER;
        }
        return tty_model_submit_read(
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
            uint64_t value = tty_model_random(model);

            bytes[index] = (
                (value & 7) == 0
                ? '\n'
                : (uint8_t)('A' + value % 26)
            );
        }
        return tty_model_submit_write(model, bytes, count, mode);
    }
    case 3: {
        uint64_t request_id = (
            model->reference.pending_read
            && ((random >> 8) & 1) != 0
            ? model->reference.pending_read_request_id
            : model->reference.last_accepted_request_id + 1
        );

        return tty_model_cancel(model, request_id);
    }
    case 4:
        return tty_model_collect(
            model,
            tty_model_collect_request_id(model, random >> 8)
        );
    case 5:
        if (tty_model_reference_output_pending(&model->reference)) {
            return tty_model_transmit_irq(model);
        }
        return tty_model_input_irq(
            model,
            input_bytes[
                (random >> 8)
                % (sizeof(input_bytes) / sizeof(input_bytes[0]))
            ]
        );
    case 6:
        return tty_model_malformed_submit(model);
    default:
        return false;
    }
}

static bool tty_model_scripted_transition(struct tty_model *model)
{
    static const uint8_t fifo_boundary_write[] = {
        'w', 'w', 'w', 'w', 'w', 'w', 'w', 'w',
        'w', 'w', 'w', 'w', 'w', 'w', 'w', '\n',
    };
    static const uint8_t rejected_write[] = {'r', '\n'};

    switch (model->step) {
    case 0:
        return tty_model_submit_read(model, 8, MODEL_GRANT_VALID);
    case 1:
        return tty_model_input_irq(model, 'x');
    case 2:
        return tty_model_cancel(
            model,
            model->reference.pending_read_request_id
        );
    case 3:
        return tty_model_submit_read(model, 8, MODEL_GRANT_VALID);
    case 4:
        return tty_model_input_irq(model, '\r');
    case 5:
        return tty_model_collect(
            model,
            model->reference.read_completion.request_id
        );
    case 6:
        return tty_model_transmit_irq(model);
    case 7:
        return tty_model_submit_write(
            model,
            fifo_boundary_write,
            sizeof(fifo_boundary_write),
            MODEL_GRANT_VALID
        );
    case 8:
        return tty_model_collect(
            model,
            model->reference.write_completion.request_id
        );
    case 9:
        return tty_model_submit_write(
            model,
            rejected_write,
            sizeof(rejected_write),
            MODEL_GRANT_VALID
        );
    case 10:
        return tty_model_transmit_irq(model);
    case 11:
        return tty_model_input_irq(model, 'E');
    case 12:
        return tty_model_transmit_irq(model);
    case 13:
        return tty_model_submit_read(model, 8, MODEL_GRANT_INVALID);
    case 14:
        return tty_model_submit_write(
            model,
            rejected_write,
            sizeof(rejected_write),
            MODEL_GRANT_REJECT_TRANSFER
        );
    case 15:
        return tty_model_submit_read(
            model,
            8,
            MODEL_GRANT_REJECT_TRANSFER
        );
    case 16:
        return tty_model_input_irq(model, '\r');
    case 17:
        return tty_model_collect(
            model,
            model->reference.read_completion.request_id
        );
    case 18:
        return tty_model_submit_read(model, 8, MODEL_GRANT_VALID);
    case 19:
        return tty_model_collect(
            model,
            model->reference.read_completion.request_id
        );
    default:
        return false;
    }
}

static bool tty_model_test_replayable_mixed_transition_model(void)
{
    const uint64_t seed = UINT64_C(0x4d4943524f535454);
    struct tty_model model;
    const struct micros_tty_uart_bus bus = {
        .read = tty_model_bus_read,
        .write = tty_model_bus_write,
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
            tty_model_tty_endpoint,
            tty_model_vfs_endpoint
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
    MODEL_EXPECT(&model, tty_model_validate(&model));

    for (step = 0; step < MODEL_RANDOM_STEP_COUNT; ++step) {
        model.step = step;
        if (step < 20) {
            MODEL_EXPECT(
                &model,
                tty_model_scripted_transition(&model)
            );
        } else {
            MODEL_EXPECT(
                &model,
                tty_model_random_transition(
                    &model,
                    tty_model_random(&model)
                )
            );
        }
        MODEL_EXPECT(&model, tty_model_validate(&model));
    }
    step = MODEL_RANDOM_STEP_COUNT;
    while (tty_model_reference_output_pending(&model.reference)) {
        MODEL_EXPECT(&model, step < MODEL_TRACE_CAPACITY);
        model.step = step;
        MODEL_EXPECT(&model, tty_model_transmit_irq(&model));
        MODEL_EXPECT(&model, tty_model_validate(&model));
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
    return tty_model_test_replayable_mixed_transition_model() ? 0 : 1;
}
