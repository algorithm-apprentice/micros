#ifndef MICROS_RUNTIME_H
#define MICROS_RUNTIME_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/endpoint.h"
#include "micros/grant.h"
#include "micros/ipc.h"
#include "micros/syscall_abi.h"

typedef int64_t micros_runtime_result_t;

_Static_assert(CHAR_BIT == 8, "the user runtime requires eight-bit bytes");
_Static_assert(
    sizeof(uintptr_t) == 8,
    "the user runtime requires 64-bit pointers"
);
_Static_assert(
    sizeof(size_t) == 8,
    "the user runtime requires 64-bit sizes"
);
_Static_assert(
    sizeof(micros_runtime_result_t) == 8,
    "the user runtime result must remain 64 bits"
);
_Static_assert(
    sizeof(micros_endpoint_t) == 4,
    "the endpoint ABI must remain 32 bits"
);
_Static_assert(
    sizeof(micros_grant_t) == 4,
    "the grant ABI must remain 32 bits"
);
_Static_assert(
    sizeof(struct micros_ipc_message) == 64,
    "the IPC message ABI must remain 64 bytes"
);
_Static_assert(
    _Alignof(struct micros_ipc_message) == 8,
    "the IPC message ABI must remain eight-byte aligned"
);

void micros_service_main(void);

micros_runtime_result_t micros_runtime_send(
    micros_endpoint_t destination,
    const struct micros_ipc_message *message
);

micros_runtime_result_t micros_runtime_receive(
    micros_endpoint_t source,
    struct micros_ipc_message *message
);

micros_runtime_result_t micros_runtime_call(
    micros_endpoint_t destination,
    struct micros_ipc_message *message
);

micros_runtime_result_t micros_runtime_reply(
    uint64_t reply_token,
    const struct micros_ipc_message *message
);

micros_runtime_result_t micros_runtime_reply_receive(
    uint64_t reply_token,
    const struct micros_ipc_message *reply_message,
    micros_endpoint_t source,
    struct micros_ipc_message *receive_message
);

micros_runtime_result_t micros_runtime_notify(
    micros_endpoint_t destination,
    uint64_t event_mask
);

micros_runtime_result_t micros_runtime_grant_create(
    micros_endpoint_t grantee,
    uintptr_t base,
    size_t length,
    uint32_t permissions,
    micros_grant_t *grant_out
);

micros_runtime_result_t micros_runtime_grant_revoke(
    micros_grant_t grant
);

micros_runtime_result_t micros_runtime_grant_copy_from(
    micros_endpoint_t grantor,
    micros_grant_t grant,
    size_t grant_offset,
    uintptr_t local_address,
    size_t length
);

micros_runtime_result_t micros_runtime_grant_copy_to(
    micros_endpoint_t grantor,
    micros_grant_t grant,
    size_t grant_offset,
    uintptr_t local_address,
    size_t length
);

#endif
