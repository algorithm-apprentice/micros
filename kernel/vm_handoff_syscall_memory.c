#include "kernel/vm_handoff_syscall_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum vm_fixed_object_error micros_vm_handoff_syscall_prepare_fixed_object(
    struct micros_process_handle process,
    uint64_t user_address,
    size_t size,
    uint32_t required_permission,
    struct vm_fixed_object_plan *plan
)
{
    struct vm_fixed_object_plan candidate = {0};
    uint64_t current = user_address;
    size_t remaining = size;

    if (
        plan == NULL
        || size == 0
        || user_address < MICROS_USER_VIRTUAL_BASE
        || size > MICROS_USER_VIRTUAL_END - MICROS_USER_VIRTUAL_BASE
        || user_address > MICROS_USER_VIRTUAL_END - size
    ) {
        return VM_FIXED_OBJECT_ERROR_MEMORY_FAULT;
    }
    while (remaining != 0) {
        uint64_t physical_address = 0;
        uint32_t permissions = 0;
        size_t contiguous = 0;
        size_t chunk;
        enum micros_user_address_space_error error;

        if (
            candidate.chunk_count
                >= VM_FIXED_OBJECT_CHUNK_CAPACITY
        ) {
            return VM_FIXED_OBJECT_ERROR_INVARIANT;
        }
        error = micros_user_address_space_translate(
            process,
            current,
            &physical_address,
            &permissions,
            &contiguous
        );
        if (
            error == MICROS_USER_ADDRESS_SPACE_ERROR_RANGE
            || error == MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION
            || error == MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED
            || error == MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP
            || (
                error == MICROS_USER_ADDRESS_SPACE_OK
                && (
                    permissions & required_permission
                ) != required_permission
            )
        ) {
            return VM_FIXED_OBJECT_ERROR_MEMORY_FAULT;
        }
        if (
            error != MICROS_USER_ADDRESS_SPACE_OK
            || physical_address == 0
            || contiguous == 0
            || physical_address > UINTPTR_MAX
        ) {
            return VM_FIXED_OBJECT_ERROR_INVARIANT;
        }
        chunk = contiguous < remaining ? contiguous : remaining;
        if (
            physical_address > UINTPTR_MAX - (chunk - 1)
            || current > UINT64_MAX - chunk
        ) {
            return VM_FIXED_OBJECT_ERROR_INVARIANT;
        }
        candidate.chunks[candidate.chunk_count].physical_address =
            (uintptr_t)physical_address;
        candidate.chunks[candidate.chunk_count].size = chunk;
        ++candidate.chunk_count;
        current += chunk;
        remaining -= chunk;
    }
    candidate.size = size;
    *plan = candidate;
    return VM_FIXED_OBJECT_OK;
}

void micros_vm_handoff_syscall_read_fixed_object_prevalidated(
    const struct vm_fixed_object_plan *plan,
    void *output
)
{
    uint8_t *destination = output;
    size_t offset = 0;
    size_t index;

    for (index = 0; index < plan->chunk_count; ++index) {
        micros_vm_handoff_syscall_copy_bytes(
            &destination[offset],
            (const void *)plan->chunks[index].physical_address,
            plan->chunks[index].size
        );
        offset += plan->chunks[index].size;
    }
}

void micros_vm_handoff_syscall_write_fixed_object_prevalidated(
    const struct vm_fixed_object_plan *plan,
    const void *input
)
{
    const uint8_t *source = input;
    size_t offset = 0;
    size_t index;

    for (index = 0; index < plan->chunk_count; ++index) {
        micros_vm_handoff_syscall_copy_bytes(
            (void *)plan->chunks[index].physical_address,
            &source[offset],
            plan->chunks[index].size
        );
        offset += plan->chunks[index].size;
    }
}
