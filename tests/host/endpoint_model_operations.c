#include "endpoint_model_internal.h"

#include <stdio.h>
#include <string.h>

uint32_t endpoint_model_next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}
