#ifndef MICROS_GRANT_SYSCALL_CORE_H
#define MICROS_GRANT_SYSCALL_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "micros/endpoint.h"
#include "micros/grant.h"
#include "micros/syscall_abi.h"

struct micros_grant_syscall_request {
    uint64_t operation;
    micros_endpoint_t endpoint;
    micros_grant_t grant;
    uint64_t offset;
    uintptr_t local_address;
    size_t length;
    uint32_t permissions;
};

enum micros_syscall_abi_result micros_grant_syscall_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_grant_syscall_request *request
);

#endif
