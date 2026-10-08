#ifndef MICROS_RUNTIME_RAW_SYSCALL_H
#define MICROS_RUNTIME_RAW_SYSCALL_H

#include <stdint.h>

int64_t micros_runtime_raw_syscall(
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5,
    uint64_t a6,
    uint64_t a7
);

#endif
