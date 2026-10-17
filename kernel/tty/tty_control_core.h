#ifndef MICROS_KERNEL_TTY_CONTROL_CORE_H
#define MICROS_KERNEL_TTY_CONTROL_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/endpoint.h"
#include "micros/syscall_abi.h"
#include "micros/tty.h"

struct micros_tty_control_request {
    uint32_t command;
    uint32_t version;
    uint32_t service_id;
    micros_endpoint_t endpoint;
    uint64_t virtual_base;
    uint64_t physical_base;
    uint32_t mapped_length;
    uint32_t irq_source;
};

enum micros_syscall_abi_result micros_tty_control_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_tty_control_request *request
);

bool micros_tty_control_commit_tuple_matches(
    const struct micros_tty_control_request *request,
    uint32_t service_id,
    micros_endpoint_t endpoint
);

bool micros_tty_control_complete_tuple_matches(
    const struct micros_tty_control_request *request
);

#endif
