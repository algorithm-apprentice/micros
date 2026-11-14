#include "lib/vm/vm_load_token.h"
#include "micros/kernel_objects.h"

_Static_assert(MICROS_PROCESS_CAPACITY == MICROS_VM_LOAD_TOKEN_SLOT_COUNT,
               "load tokens route the existing six-bit process slot");

uint16_t micros_vm_load_token_slot(uint64_t token)
{
    return token == 0 ? MICROS_VM_LOAD_TOKEN_SLOT_COUNT
        : (uint16_t)(token & (MICROS_VM_LOAD_TOKEN_SLOT_COUNT - 1));
}
