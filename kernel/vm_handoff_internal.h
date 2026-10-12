#ifndef MICROS_KERNEL_VM_HANDOFF_INTERNAL_H
#define MICROS_KERNEL_VM_HANDOFF_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/vm_handoff_core.h"

#define MICROS_VM_HANDOFF_MAGIC UINT64_C(0x4d4943524f535648)

bool micros_vm_handoff_bytes_are_zero(
    const void *storage,
    size_t size
);

void micros_vm_handoff_clear_bytes(void *storage, size_t size);

void micros_vm_handoff_copy_bytes(
    void *destination,
    const void *source,
    size_t size
);

bool micros_vm_handoff_pointer_is_aligned(
    const void *pointer,
    size_t alignment
);

bool micros_vm_handoff_process_is_valid(
    struct micros_process_handle process
);

bool micros_vm_handoff_thread_is_valid(
    struct micros_thread_handle thread
);

bool micros_vm_handoff_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
);

bool micros_vm_handoff_address_is_page_aligned(uint64_t address);

bool micros_vm_handoff_user_page_is_valid(uint64_t virtual_address);

bool micros_vm_handoff_user_permissions_are_valid(
    uint32_t permissions
);

bool micros_vm_handoff_mapping_entry_is_valid(
    const struct micros_vm_kernel_mapping_entry *entry
);

bool micros_vm_handoff_stack_pointer_is_valid(
    uint64_t stack_pointer
);

uint64_t micros_vm_handoff_next_nonzero_token(uint64_t previous);

bool micros_vm_handoff_endpoint_matches_process(
    uint32_t endpoint,
    struct micros_process_handle process
);

bool micros_vm_handoff_summary_is_valid(
    const struct micros_vm_boot_summary *summary
);

bool micros_vm_handoff_mapping_record_is_zero(
    const struct micros_vm_kernel_mapping_record *record
);

bool micros_vm_handoff_loading_record_can_append(
    const struct micros_vm_kernel_mapping_record *record,
    const struct micros_vm_kernel_mapping_entry *entry
);

bool micros_vm_handoff_record_entries_are_frozen_shape(
    const struct micros_vm_kernel_mapping_record *record
);

bool micros_vm_handoff_frozen_record_is_valid(
    const struct micros_vm_kernel_mapping_record *record
);

bool micros_vm_handoff_record_contains_physical_address(
    const struct micros_vm_kernel_mapping_record *record,
    uint64_t physical_address
);

#endif
