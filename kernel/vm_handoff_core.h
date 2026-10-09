#ifndef MICROS_KERNEL_VM_HANDOFF_CORE_H
#define MICROS_KERNEL_VM_HANDOFF_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/kernel_objects.h"
#include "micros/syscall_abi.h"
#include "micros/tty.h"
#include "micros/vm_bootstrap.h"

enum micros_vm_handoff_phase {
    MICROS_VM_HANDOFF_PHASE_UNINITIALIZED = 0,
    MICROS_VM_HANDOFF_PHASE_PREPARED,
    MICROS_VM_HANDOFF_PHASE_HANDED_OFF,
};

struct micros_vm_handoff_request {
    uint32_t command;
    uint32_t version;
    uint16_t managed_range_count;
    uint16_t address_space_count;
    uint32_t managed_frame_count;
    uint32_t free_frame_count;
    uint32_t mapping_count;
    uint64_t digest;
};

struct micros_vm_tty_mapping_request {
    uint32_t command;
    uint32_t version;
    uint32_t service_id;
    uint32_t endpoint;
    uint64_t virtual_base;
    uint64_t physical_base;
    uint32_t irq_source;
    uint32_t mapped_length;
};

struct micros_vm_handoff_state {
    uint64_t initialization_magic;
    enum micros_vm_handoff_phase phase;
    uint32_t service_id;
    uint32_t endpoint;
    uint32_t image_id;
    uint32_t profile_id;
    struct micros_process_handle process;
    struct micros_thread_handle thread;
    uint64_t root_physical_address;
    uint64_t boot_info_address;
    uint32_t boot_info_size;
    uint32_t reserved;
    struct micros_vm_boot_summary summary;
};

enum micros_vm_handoff_error {
    MICROS_VM_HANDOFF_OK = 0,
    MICROS_VM_HANDOFF_ERROR_ARGUMENT,
    MICROS_VM_HANDOFF_ERROR_STORAGE,
    MICROS_VM_HANDOFF_ERROR_STATE,
    MICROS_VM_HANDOFF_ERROR_IDENTITY,
    MICROS_VM_HANDOFF_ERROR_SUMMARY,
    MICROS_VM_HANDOFF_ERROR_INVARIANT,
};

enum micros_vm_self_fault_action {
    MICROS_VM_SELF_FAULT_INVALID = 0,
    MICROS_VM_SELF_FAULT_RUNNING_STARTING,
    MICROS_VM_SELF_FAULT_RUNNING_READY,
    MICROS_VM_SELF_FAULT_SEALED,
};

enum micros_syscall_abi_result micros_vm_handoff_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_vm_handoff_request *request
);

enum micros_syscall_abi_result micros_vm_handoff_decode_command(
    const struct micros_syscall_arguments *arguments,
    uint32_t *command
);

enum micros_syscall_abi_result
micros_vm_handoff_decode_tty_mapping(
    const struct micros_syscall_arguments *arguments,
    struct micros_vm_tty_mapping_request *request
);

bool micros_vm_tty_mapping_matches(
    const struct micros_vm_tty_mapping_request *request,
    micros_endpoint_t tty_endpoint
);

enum micros_vm_handoff_error micros_vm_handoff_state_prepare(
    struct micros_vm_handoff_state *state,
    uint32_t service_id,
    uint32_t endpoint,
    uint32_t image_id,
    uint32_t profile_id,
    struct micros_process_handle process,
    struct micros_thread_handle thread,
    uint64_t root_physical_address,
    uint64_t boot_info_address,
    uint32_t boot_info_size,
    const struct micros_vm_boot_summary *summary
);

enum micros_vm_handoff_error micros_vm_handoff_state_validate(
    const struct micros_vm_handoff_state *state
);

bool micros_vm_handoff_summary_matches(
    const struct micros_vm_handoff_state *state,
    const struct micros_vm_handoff_request *request
);

void micros_vm_handoff_commit_prevalidated(
    struct micros_vm_handoff_state *state
);

enum micros_vm_self_fault_action micros_vm_self_fault_classify(
    enum micros_bootstrap_phase bootstrap_phase,
    enum micros_bootstrap_service_state service_state
);

#endif
