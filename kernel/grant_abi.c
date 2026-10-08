#include "micros/syscall_abi.h"

#include <stdint.h>

bool micros_grant_abi_map_error(
    enum micros_grant_error error,
    uint64_t *result
)
{
    int64_t mapped;

    if (result == NULL) {
        return false;
    }
    switch (error) {
    case MICROS_GRANT_OK:
        mapped = MICROS_SYSCALL_ABI_OK;
        break;
    case MICROS_GRANT_ERROR_ARGUMENT:
        mapped = MICROS_SYSCALL_ABI_ARGUMENT;
        break;
    case MICROS_GRANT_ERROR_CAPACITY:
        mapped = MICROS_SYSCALL_ABI_CAPACITY;
        break;
    case MICROS_GRANT_ERROR_STALE_GRANT:
        mapped = MICROS_SYSCALL_ABI_STALE_GRANT;
        break;
    case MICROS_GRANT_ERROR_DEAD_ENDPOINT:
        mapped = MICROS_SYSCALL_ABI_DEAD_ENDPOINT;
        break;
    case MICROS_GRANT_ERROR_UNAUTHORIZED:
        mapped = MICROS_SYSCALL_ABI_UNAUTHORIZED;
        break;
    case MICROS_GRANT_ERROR_RANGE:
        mapped = MICROS_SYSCALL_ABI_RANGE;
        break;
    case MICROS_GRANT_ERROR_STATE:
        mapped = MICROS_SYSCALL_ABI_STATE;
        break;
    case MICROS_GRANT_ERROR_FAULT:
        mapped = MICROS_SYSCALL_ABI_MEMORY_FAULT;
        break;
    case MICROS_GRANT_ERROR_ALREADY_INITIALIZED:
    case MICROS_GRANT_ERROR_NOT_INITIALIZED:
    case MICROS_GRANT_ERROR_PHASE:
    case MICROS_GRANT_ERROR_INVARIANT:
        return false;
    default:
        return false;
    }
    *result = (uint64_t)mapped;
    return true;
}
