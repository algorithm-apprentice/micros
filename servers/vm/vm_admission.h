#ifndef MICROS_SERVERS_VM_ADMISSION_H
#define MICROS_SERVERS_VM_ADMISSION_H

#include <stdbool.h>
#include "servers/vm/vm_core.h"

enum micros_vm_core_error vm_core_validate_selected_state(const struct micros_vm_state *state);
bool vm_core_record_headers_are_valid(const struct micros_vm_state *state);

#endif
