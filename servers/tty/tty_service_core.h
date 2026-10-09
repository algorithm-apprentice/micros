#ifndef MICROS_SERVERS_TTY_SERVICE_CORE_H
#define MICROS_SERVERS_TTY_SERVICE_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "servers/tty/tty_core.h"

enum micros_tty_service_error {
    MICROS_TTY_SERVICE_OK = 0,
    MICROS_TTY_SERVICE_ERROR_ARGUMENT,
    MICROS_TTY_SERVICE_ERROR_STATE,
    MICROS_TTY_SERVICE_ERROR_INVARIANT,
};

enum micros_tty_service_copy_direction {
    MICROS_TTY_SERVICE_COPY_FROM_VFS = 0,
    MICROS_TTY_SERVICE_COPY_TO_VFS,
};

enum micros_tty_service_copy_result {
    MICROS_TTY_SERVICE_COPY_OK = 0,
    MICROS_TTY_SERVICE_COPY_REJECTED,
    MICROS_TTY_SERVICE_COPY_INVARIANT,
};

enum micros_tty_service_release {
    MICROS_TTY_SERVICE_RELEASE_NONE = 0,
    MICROS_TTY_SERVICE_RELEASE_READ,
    MICROS_TTY_SERVICE_RELEASE_WRITE,
};

struct micros_tty_service_request {
    bool active;
    uint64_t request_id;
    micros_grant_t grant;
    uint64_t grant_offset;
    uint64_t count;
};

struct micros_tty_service_completion {
    bool active;
    enum micros_tty_result result;
    uint64_t request_id;
    uint64_t transferred_count;
};

struct micros_tty_service_state {
    uint64_t initialization_magic;
    micros_endpoint_t self_endpoint;
    micros_endpoint_t vfs_endpoint;
    bool owned;
    uint64_t last_accepted_request_id;
    struct micros_tty_state terminal;
    struct micros_tty_service_request pending_read;
    struct micros_tty_service_completion read_completion;
    struct micros_tty_service_completion write_completion;
};

typedef enum micros_tty_service_copy_result
(*micros_tty_service_copy_fn)(
    void *context,
    enum micros_tty_service_copy_direction direction,
    micros_endpoint_t vfs_endpoint,
    micros_grant_t grant,
    uint64_t grant_offset,
    uint8_t *local,
    size_t length
);

struct micros_tty_service_io {
    micros_tty_service_copy_fn copy;
    void *context;
};

struct micros_tty_service_reply_action {
    bool active;
    uint64_t reply_token;
    struct micros_ipc_message message;
    struct micros_tty_effects uart_effects;
    uint64_t notification_after_reply;
    enum micros_tty_service_release release;
    uint64_t release_request_id;
};

enum micros_tty_service_error micros_tty_service_initialize(
    struct micros_tty_service_state *state,
    micros_endpoint_t self_endpoint,
    micros_endpoint_t vfs_endpoint
);

enum micros_tty_service_error micros_tty_service_validate(
    const struct micros_tty_service_state *state
);

enum micros_tty_service_error micros_tty_service_commit_ownership(
    struct micros_tty_service_state *state
);

enum micros_tty_service_error micros_tty_service_handle_call(
    struct micros_tty_service_state *state,
    const struct micros_ipc_message *message,
    const struct micros_tty_service_io *io,
    struct micros_tty_service_reply_action *action
);

enum micros_tty_service_error micros_tty_service_commit_reply(
    struct micros_tty_service_state *state,
    struct micros_tty_service_reply_action *action,
    uint64_t *notification
);

enum micros_tty_service_error micros_tty_service_complete_read(
    struct micros_tty_service_state *state,
    const struct micros_tty_service_io *io,
    uint64_t *notification
);

#endif
