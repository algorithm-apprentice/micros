#ifndef MICROS_BOOTSTRAP_CONTROL_H
#define MICROS_BOOTSTRAP_CONTROL_H

#include <stdint.h>

#include "micros/bootstrap.h"
#include "micros/syscall_abi.h"

#define MICROS_BOOTSTRAP_MESSAGE_READY UINT32_C(0x00000001)
#define MICROS_BOOTSTRAP_MESSAGE_READY_ACK UINT32_C(0x00000002)

enum micros_bootstrap_command {
    MICROS_BOOTSTRAP_COMMAND_RELEASE = 1,
    MICROS_BOOTSTRAP_COMMAND_ACCEPT_READY,
    MICROS_BOOTSTRAP_COMMAND_FAIL,
    MICROS_BOOTSTRAP_COMMAND_COMPLETE,
};

enum micros_bootstrap_failure_reason {
    MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED = 1,
    MICROS_BOOTSTRAP_FAILURE_READY_FOREIGN,
    MICROS_BOOTSTRAP_FAILURE_READY_EARLY,
    MICROS_BOOTSTRAP_FAILURE_READY_DUPLICATE,
    MICROS_BOOTSTRAP_FAILURE_RELEASE_ORDER,
    MICROS_BOOTSTRAP_FAILURE_RELEASE_TRANSITION,
    MICROS_BOOTSTRAP_FAILURE_AUTHORITY,
    MICROS_BOOTSTRAP_FAILURE_COMPLETION,
    MICROS_BOOTSTRAP_FAILURE_READY_ROLE_GATE,
};

struct micros_bootstrap_control_request {
    enum micros_bootstrap_command command;
    uint32_t service_id;
    micros_endpoint_t endpoint;
    uint64_t reply_token;
    enum micros_bootstrap_failure_reason failure_reason;
    uint32_t failure_detail;
};

enum micros_syscall_abi_result micros_bootstrap_control_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_bootstrap_control_request *request
);

enum micros_syscall_abi_result
micros_bootstrap_control_validate_failure_detail(
    const struct micros_bootstrap_control_request *request
);

#endif
