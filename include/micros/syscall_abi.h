#ifndef MICROS_SYSCALL_ABI_H
#define MICROS_SYSCALL_ABI_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/grant.h"

enum micros_syscall_abi_operation {
    MICROS_SYSCALL_ABI_SEND = 1,
    MICROS_SYSCALL_ABI_RECEIVE = 2,
    MICROS_SYSCALL_ABI_CALL = 3,
    MICROS_SYSCALL_ABI_REPLY = 4,
    MICROS_SYSCALL_ABI_REPLY_RECEIVE = 5,
    MICROS_SYSCALL_ABI_NOTIFY = 6,
    MICROS_SYSCALL_ABI_GRANT_CREATE = 7,
    MICROS_SYSCALL_ABI_GRANT_REVOKE = 8,
    MICROS_SYSCALL_ABI_GRANT_COPY_FROM = 9,
    MICROS_SYSCALL_ABI_GRANT_COPY_TO = 10,
    MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL = 11,
    MICROS_SYSCALL_ABI_VM_HANDOFF = 12,
};

enum micros_syscall_abi_result {
    MICROS_SYSCALL_ABI_OK = 0,
    MICROS_SYSCALL_ABI_ARGUMENT = -1,
    MICROS_SYSCALL_ABI_DEAD_ENDPOINT = -2,
    MICROS_SYSCALL_ABI_UNAUTHORIZED = -3,
    MICROS_SYSCALL_ABI_STATE = -4,
    MICROS_SYSCALL_ABI_DEADLOCK = -5,
    MICROS_SYSCALL_ABI_MEMORY_FAULT = -6,
    MICROS_SYSCALL_ABI_REPLY_TOKEN = -7,
    MICROS_SYSCALL_ABI_REPLY_TOKEN_EXHAUSTED = -8,
    MICROS_SYSCALL_ABI_ENDPOINT_CLOSING = -9,
    MICROS_SYSCALL_ABI_CAPACITY = -10,
    MICROS_SYSCALL_ABI_STALE_GRANT = -11,
    MICROS_SYSCALL_ABI_RANGE = -12,
};

struct micros_syscall_arguments {
    uint64_t a0;
    uint64_t a1;
    uint64_t a2;
    uint64_t a3;
    uint64_t a4;
    uint64_t a5;
    uint64_t a6;
    uint64_t a7;
};

bool micros_grant_abi_map_error(
    enum micros_grant_error error,
    uint64_t *result
);

#endif
