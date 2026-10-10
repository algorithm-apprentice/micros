#ifndef MICROS_SERVERS_VFS_SERVICE_CORE_H
#define MICROS_SERVERS_VFS_SERVICE_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/bootstrap.h"
#include "micros/ipc.h"
#include "servers/vfs/vfs_core.h"

enum micros_vfs_service_error {
    MICROS_VFS_SERVICE_OK = 0,
    MICROS_VFS_SERVICE_ERROR_ARGUMENT,
    MICROS_VFS_SERVICE_ERROR_INVARIANT,
};

enum micros_vfs_protocol_status {
    MICROS_VFS_PROTOCOL_OK = 0,
    MICROS_VFS_PROTOCOL_BAD_TYPE,
    MICROS_VFS_PROTOCOL_BAD_VERSION,
    MICROS_VFS_PROTOCOL_MALFORMED,
    MICROS_VFS_PROTOCOL_INVARIANT,
};

struct micros_vfs_service_endpoints {
    micros_endpoint_t launcher;
    micros_endpoint_t tty;
    micros_endpoint_t ramfs;
    micros_endpoint_t self;
};

struct micros_vfs_service_reply_action {
    bool active;
    uint64_t reply_token;
    struct micros_ipc_message message;
};

bool micros_vfs_service_validate_configuration(
    const volatile struct micros_bootstrap_service_config *config,
    struct micros_vfs_service_endpoints *endpoints
);

#if defined(MICROS_BUILD_VFS_SERVICE_TEST)
bool micros_vfs_service_validate_test_configuration(
    const volatile struct micros_bootstrap_service_config *config,
    struct micros_vfs_service_endpoints *endpoints,
    micros_endpoint_t *application_endpoint
);
#endif

enum micros_vfs_protocol_status micros_vfs_service_decode_request(
    const struct micros_ipc_message *message,
    struct micros_vfs_request *request
);

enum micros_vfs_service_error micros_vfs_service_build_result(
    const struct micros_vfs_result_action *result,
    struct micros_ipc_message *message
);

enum micros_vfs_service_error micros_vfs_service_build_ramfs_call(
    const struct micros_vfs_ramfs_request *request,
    struct micros_ipc_message *message
);

enum micros_vfs_service_error micros_vfs_service_decode_ramfs_result(
    micros_endpoint_t ramfs_endpoint,
    const struct micros_vfs_ramfs_request *request,
    const struct micros_ipc_message *message,
    struct micros_vfs_ramfs_response *response
);

enum micros_vfs_service_error micros_vfs_service_build_tty_call(
    const struct micros_vfs_tty_request *request,
    struct micros_ipc_message *message
);

enum micros_vfs_service_error micros_vfs_service_decode_tty_result(
    micros_endpoint_t tty_endpoint,
    const struct micros_vfs_tty_request *request,
    const struct micros_ipc_message *message,
    struct micros_vfs_tty_response *response
);

enum micros_vfs_service_error micros_vfs_service_handle_message(
    struct micros_vfs_state *state,
    const struct micros_ipc_message *message,
    const struct micros_vfs_io *io,
    struct micros_vfs_service_reply_action *action
);

#endif
