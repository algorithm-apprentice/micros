#ifndef MICROS_SERVERS_TTY_SERVICE_INTERNAL_H
#define MICROS_SERVERS_TTY_SERVICE_INTERNAL_H

#include "servers/tty/tty_service_core.h"

#define MICROS_TTY_SERVICE_INITIALIZATION_MAGIC \
    UINT64_C(0x4d49435454595356)

void tty_service_request_clear_bytes(void *storage, size_t size);

enum micros_tty_service_copy_result tty_service_copy_grant(
    const struct micros_tty_service_io *io,
    enum micros_tty_service_copy_direction direction,
    const struct micros_tty_service_state *state,
    micros_grant_t grant,
    uint64_t grant_offset,
    uint8_t *local,
    size_t length
);

bool tty_service_endpoint_is_valid(micros_endpoint_t endpoint);

enum micros_tty_service_error tty_service_handle_cancel(
    struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    struct micros_tty_service_reply_action *action
);

enum micros_tty_service_error tty_service_handle_collect(
    struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    struct micros_tty_service_reply_action *action
);

enum micros_tty_service_error tty_service_handle_submit_read(
    struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    const struct micros_tty_service_io *io,
    struct micros_tty_service_reply_action *action
);

enum micros_tty_service_error tty_service_handle_submit_write(
    struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    const struct micros_tty_service_io *io,
    struct micros_tty_service_reply_action *action
);

enum micros_tty_service_error tty_service_prepare_protocol_reply(
    const struct micros_ipc_message *message,
    enum micros_tty_protocol_status status,
    struct micros_tty_service_reply_action *action
);

enum micros_tty_service_error tty_service_prepare_reply(
    uint64_t reply_token,
    uint32_t request_type,
    enum micros_tty_result result,
    uint64_t request_id,
    uint64_t transferred_count,
    struct micros_tty_service_reply_action *action
);

enum micros_tty_service_error tty_service_validate_submit(
    const struct micros_tty_service_state *state,
    const struct micros_tty_request *request,
    struct micros_tty_service_reply_action *action
);

#endif
