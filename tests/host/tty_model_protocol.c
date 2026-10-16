#include "tty_model_internal.h"

struct micros_ipc_message tty_model_submit_request(
    struct tty_model *model,
    uint32_t type,
    uint64_t request_id,
    micros_grant_t grant,
    uint64_t count
)
{
    struct micros_ipc_message message = {
        .source = tty_model_vfs_endpoint,
        .type = type,
        .reply_token = UINT64_C(0x1000) + model->step,
    };

    tty_model_write_u32_le(&message.payload[0], MICROS_TTY_PROTOCOL_VERSION);
    tty_model_write_u64_le(&message.payload[8], request_id);
    tty_model_write_u32_le(&message.payload[16], grant);
    tty_model_write_u64_le(&message.payload[24], 0);
    tty_model_write_u64_le(&message.payload[32], count);
    return message;
}

struct micros_ipc_message tty_model_identity_request(
    struct tty_model *model,
    uint32_t type,
    uint64_t request_id
)
{
    struct micros_ipc_message message = {
        .source = tty_model_vfs_endpoint,
        .type = type,
        .reply_token = UINT64_C(0x2000) + model->step,
    };

    tty_model_write_u32_le(&message.payload[0], MICROS_TTY_PROTOCOL_VERSION);
    tty_model_write_u64_le(&message.payload[8], request_id);
    return message;
}

bool tty_model_check_reply(
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
        tty_model_read_u32_le(&action->message.payload[0])
            == MICROS_TTY_PROTOCOL_VERSION
    );
    MODEL_EXPECT(
        model,
        tty_model_read_u32_le(&action->message.payload[4]) == request_type
    );
    MODEL_EXPECT(
        model,
        (int32_t)tty_model_read_u32_le(&action->message.payload[8])
            == (int32_t)expected_result
    );
    MODEL_EXPECT(
        model,
        tty_model_read_u64_le(&action->message.payload[16])
            == expected_request_id
    );
    MODEL_EXPECT(
        model,
        tty_model_read_u64_le(&action->message.payload[24])
            == expected_transferred_count
    );
    return true;
}

bool tty_model_commit_reply(
    struct tty_model *model,
    struct micros_tty_service_reply_action *action,
    uint64_t expected_notification
)
{
    const struct micros_tty_uart_bus bus = {
        .read = tty_model_bus_read,
        .write = tty_model_bus_write,
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
