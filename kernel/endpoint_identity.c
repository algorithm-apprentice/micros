#include "endpoint_private.h"

_Static_assert(
    MICROS_PROCESS_CAPACITY <= (1U << MICROS_ENDPOINT_SLOT_BITS),
    "implemented process capacity must fit the endpoint ABI"
);
_Static_assert(
    MICROS_PROCESS_GENERATION_MAX == MICROS_ENDPOINT_GENERATION_MAX,
    "process and endpoint generation limits must match"
);
_Static_assert(
    MICROS_PROCESS_CAPACITY == 64,
    "pending notification bitmap requires one bit per endpoint slot"
);
