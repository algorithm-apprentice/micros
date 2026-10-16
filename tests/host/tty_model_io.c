#include "tty_model_internal.h"

enum micros_tty_service_copy_result tty_model_copy_grant(
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
        endpoint != tty_model_vfs_endpoint
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
        first_line_length = tty_model_reference_first_line_length(reference);
        expected_length = first_line_length;
        if (reference->pending_read_count < expected_length) {
            expected_length = (size_t)reference->pending_read_count;
        }
        if (
            first_line_length == 0
            || length != expected_length
            || !tty_model_reference_consume_input(reference, local, length)
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

uint8_t tty_model_bus_read(void *context, uint8_t offset)
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

void tty_model_bus_write(
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
                !tty_model_reference_output_take(
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
