#ifndef MICROS_KERNEL_VM_HANDOFF_CORE_H
#define MICROS_KERNEL_VM_HANDOFF_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/kernel_objects.h"
#include "micros/spawn.h"
#include "micros/syscall_abi.h"
#include "micros/tty.h"
#include "micros/vm.h"
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

enum micros_vm_load_phase {
    MICROS_VM_LOAD_PHASE_IDLE = 0,
    MICROS_VM_LOAD_PHASE_LOADING,
    MICROS_VM_LOAD_PHASE_FROZEN,
};

enum micros_vm_scratch_phase {
    MICROS_VM_SCRATCH_CLEAR = 0,
    MICROS_VM_SCRATCH_MAPPED,
};

enum micros_vm_kernel_mapping_phase {
    MICROS_VM_KERNEL_MAPPING_UNUSED = 0,
    MICROS_VM_KERNEL_MAPPING_LOADING,
    MICROS_VM_KERNEL_MAPPING_FROZEN,
    MICROS_VM_KERNEL_MAPPING_ACTIVATED_PENDING_COMMIT,
    MICROS_VM_KERNEL_MAPPING_ACTIVE,
    MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED,
};

struct micros_vm_load_begin_request {
    uint64_t process_reservation_token;
    uint64_t output_address;
};

struct micros_vm_scratch_map_request {
    uint64_t load_token;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint32_t permissions;
};

struct micros_vm_scratch_commit_request {
    uint64_t load_token;
    uint64_t virtual_address;
    uint64_t physical_address;
};

struct micros_vm_load_freeze_request {
    uint64_t plan_address;
    uint64_t output_address;
};

struct micros_vm_load_abort_request {
    uint64_t load_token;
    uint64_t mapping_generation;
};

struct micros_vm_load_commit_request {
    uint64_t load_token;
    uint64_t mapping_generation;
};

struct micros_vm_exit_release_request {
    uint64_t exit_token;
    uint64_t load_token;
    uint64_t mapping_generation;
};

struct micros_vm_kernel_mapping_entry {
    uint64_t virtual_address;
    uint64_t physical_address;
    uint32_t permissions;
    uint32_t reserved;
};

struct micros_vm_kernel_mapping_record {
    uint64_t load_token;
    uint64_t mapping_generation;
    uint64_t exit_token;
    struct micros_process_handle process;
    uint32_t phase;
    uint32_t entry_count;
    uint64_t root_physical_address;
    uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
    uint64_t entry;
    uint64_t stack_pointer;
    struct micros_vm_kernel_mapping_entry entries[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
};

struct micros_vm_load_transaction {
    uint32_t phase;
    uint32_t scratch_phase;
    uint64_t process_reservation_token;
    uint64_t load_token;
    struct micros_process_handle process;
    uint32_t reserved;
    struct micros_vm_kernel_mapping_entry scratch;
};

struct micros_vm_load_begin_plan {
    uint64_t process_reservation_token;
    uint64_t load_token;
    struct micros_process_handle process;
    struct micros_vm_load_begin_result result;
};

struct micros_vm_scratch_plan {
    struct micros_vm_kernel_mapping_entry entry;
};

struct micros_vm_load_freeze_action {
    struct micros_process_handle process;
    uint64_t load_token;
    uint64_t mapping_generation;
    uint64_t root_physical_address;
    uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
    uint64_t entry;
    uint64_t stack_pointer;
    uint32_t entry_count;
    struct micros_vm_kernel_mapping_entry entries[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
};

struct micros_vm_load_freeze_state_plan {
    uint64_t mapping_generation;
    struct micros_vm_load_freeze_action action;
    struct micros_vm_load_freeze_result result;
};

struct micros_vm_load_abort_action {
    bool frozen;
    bool scratch_mapped;
    struct micros_process_handle process;
    uint64_t root_physical_address;
    uint64_t scratch_physical_address;
    uint32_t entry_count;
    uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
    struct micros_vm_kernel_mapping_entry entries[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
};

struct micros_vm_exit_release_action {
    struct micros_process_handle process;
    uint64_t root_physical_address;
    uint32_t entry_count;
    uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
    struct micros_vm_kernel_mapping_entry entries[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
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
    uint64_t last_load_token;
    uint64_t last_mapping_generation;
    struct micros_vm_load_transaction load;
    struct micros_vm_kernel_mapping_record mappings[MICROS_PROCESS_CAPACITY];
};

enum micros_vm_handoff_error {
    MICROS_VM_HANDOFF_OK = 0,
    MICROS_VM_HANDOFF_ERROR_ARGUMENT,
    MICROS_VM_HANDOFF_ERROR_STORAGE,
    MICROS_VM_HANDOFF_ERROR_STATE,
    MICROS_VM_HANDOFF_ERROR_IDENTITY,
    MICROS_VM_HANDOFF_ERROR_SUMMARY,
    MICROS_VM_HANDOFF_ERROR_CAPACITY,
    MICROS_VM_HANDOFF_ERROR_INVARIANT,
};

enum micros_vm_self_fault_action {
    MICROS_VM_SELF_FAULT_INVALID = 0,
    MICROS_VM_SELF_FAULT_RUNNING_STARTING,
    MICROS_VM_SELF_FAULT_RUNNING_READY,
    MICROS_VM_SELF_FAULT_SEALED,
};

enum micros_syscall_abi_result
micros_vm_handoff_decode(const struct micros_syscall_arguments *arguments,
                         struct micros_vm_handoff_request *request);

enum micros_syscall_abi_result
micros_vm_handoff_decode_command(const struct micros_syscall_arguments *arguments,
                                 uint32_t *command);

enum micros_syscall_abi_result
micros_vm_handoff_decode_tty_mapping(const struct micros_syscall_arguments *arguments,
                                     struct micros_vm_tty_mapping_request *request);

enum micros_syscall_abi_result
micros_vm_load_begin_decode(const struct micros_syscall_arguments *arguments,
                            struct micros_vm_load_begin_request *request);

enum micros_syscall_abi_result
micros_vm_scratch_map_decode(const struct micros_syscall_arguments *arguments,
                             struct micros_vm_scratch_map_request *request);

enum micros_syscall_abi_result
micros_vm_scratch_commit_decode(const struct micros_syscall_arguments *arguments,
                                struct micros_vm_scratch_commit_request *request);

enum micros_syscall_abi_result
micros_vm_load_freeze_decode(const struct micros_syscall_arguments *arguments,
                             struct micros_vm_load_freeze_request *request);

enum micros_syscall_abi_result
micros_vm_load_abort_decode(const struct micros_syscall_arguments *arguments,
                            struct micros_vm_load_abort_request *request);

enum micros_syscall_abi_result
micros_vm_load_commit_decode(const struct micros_syscall_arguments *arguments,
                             struct micros_vm_load_commit_request *request);

enum micros_syscall_abi_result
micros_vm_exit_release_decode(const struct micros_syscall_arguments *arguments,
                              struct micros_vm_exit_release_request *request);

bool micros_vm_tty_mapping_matches(const struct micros_vm_tty_mapping_request *request,
                                   micros_endpoint_t tty_endpoint);

enum micros_vm_handoff_error micros_vm_handoff_state_prepare(
    struct micros_vm_handoff_state *state, uint32_t service_id, uint32_t endpoint,
    uint32_t image_id, uint32_t profile_id, struct micros_process_handle process,
    struct micros_thread_handle thread, uint64_t root_physical_address, uint64_t boot_info_address,
    uint32_t boot_info_size, const struct micros_vm_boot_summary *summary);

enum micros_vm_handoff_error
micros_vm_handoff_state_validate(const struct micros_vm_handoff_state *state);

bool micros_vm_handoff_summary_matches(const struct micros_vm_handoff_state *state,
                                       const struct micros_vm_handoff_request *request);

void micros_vm_handoff_commit_prevalidated(struct micros_vm_handoff_state *state);

uint64_t micros_vm_load_page_plan_digest(uint32_t image_page_count, uint32_t data_page_count,
                                         const struct micros_vm_kernel_mapping_entry *entries);

enum micros_vm_handoff_error
micros_vm_load_begin_preflight(const struct micros_vm_handoff_state *state,
                               uint64_t process_reservation_token,
                               struct micros_process_handle process, bool process_is_empty,
                               struct micros_vm_load_begin_plan *plan);

void micros_vm_load_begin_commit_prevalidated(struct micros_vm_handoff_state *state,
                                              const struct micros_vm_load_begin_plan *plan);

enum micros_vm_handoff_error
micros_vm_scratch_map_preflight(const struct micros_vm_handoff_state *state,
                                const struct micros_vm_scratch_map_request *request,
                                struct micros_vm_scratch_plan *plan);

void micros_vm_scratch_map_commit_prevalidated(struct micros_vm_handoff_state *state,
                                               const struct micros_vm_scratch_plan *plan);

void micros_vm_scratch_map_bind_prevalidated(
    struct micros_vm_handoff_state *state,
    const struct micros_vm_scratch_plan *plan
);

void micros_vm_scratch_map_finish_prevalidated(
    struct micros_vm_handoff_state *state
);

enum micros_vm_handoff_error
micros_vm_scratch_commit_preflight(const struct micros_vm_handoff_state *state,
                                   const struct micros_vm_scratch_commit_request *request,
                                   struct micros_vm_scratch_plan *plan);

void micros_vm_scratch_commit_commit_prevalidated(struct micros_vm_handoff_state *state,
                                                  const struct micros_vm_scratch_plan *plan);

enum micros_vm_handoff_error
micros_vm_load_freeze_preflight(const struct micros_vm_handoff_state *state,
                                const struct micros_vm_load_freeze_plan *request,
                                struct micros_vm_load_freeze_state_plan *plan);

void micros_vm_load_freeze_commit_prevalidated(struct micros_vm_handoff_state *state,
                                               const struct micros_vm_load_freeze_state_plan *plan);

enum micros_vm_handoff_error
micros_vm_load_abort_preflight(const struct micros_vm_handoff_state *state, uint64_t load_token,
                               uint64_t mapping_generation,
                               struct micros_vm_load_abort_action *action);

void micros_vm_load_abort_commit_prevalidated(struct micros_vm_handoff_state *state);

enum micros_vm_handoff_error micros_vm_load_mark_activated(struct micros_vm_handoff_state *state,
                                                           struct micros_process_handle process,
                                                           uint64_t process_reservation_token,
                                                           uint64_t load_token,
                                                           uint64_t mapping_generation);

enum micros_vm_handoff_error
micros_vm_load_commit_preflight(const struct micros_vm_handoff_state *state, uint64_t load_token,
                                uint64_t mapping_generation);

void micros_vm_load_commit_commit_prevalidated(struct micros_vm_handoff_state *state);

enum micros_vm_handoff_error
micros_vm_load_mark_exit_detached(struct micros_vm_handoff_state *state,
                                  struct micros_process_handle process, uint64_t exit_token,
                                  uint64_t load_token, uint64_t mapping_generation);

enum micros_vm_handoff_error
micros_vm_exit_release_preflight(const struct micros_vm_handoff_state *state, uint64_t exit_token,
                                 uint64_t load_token, uint64_t mapping_generation,
                                 struct micros_vm_exit_release_action *action);

void micros_vm_exit_release_commit_prevalidated(struct micros_vm_handoff_state *state,
                                                struct micros_process_handle process);

enum micros_vm_self_fault_action
micros_vm_self_fault_classify(enum micros_bootstrap_phase bootstrap_phase,
                              enum micros_bootstrap_service_state service_state);

#endif
