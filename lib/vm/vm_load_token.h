#ifndef MICROS_LIB_VM_LOAD_TOKEN_H
#define MICROS_LIB_VM_LOAD_TOKEN_H

#include <stdint.h>

enum {
    MICROS_VM_LOAD_TOKEN_SLOT_COUNT = 64,
};

uint16_t micros_vm_load_token_slot(uint64_t token);

#endif
