#ifndef MICROS_SERVERS_VM_CORE_H
#define MICROS_SERVERS_VM_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "micros/kernel_objects.h"
#include "micros/spawn.h"
#include "micros/vm_bootstrap.h"
#include "servers/vm/vm_image.h"

enum {
    MICROS_VM_PAGE_TABLE_FRAME_COUNT = 4,
    MICROS_VM_MAPPING_RECORD_CAPACITY = MICROS_PROCESS_CAPACITY,
    MICROS_VM_MAPPING_ENTRY_CAPACITY =
        MICROS_APPLICATION_RESIDENT_PAGE_MAX
            + MICROS_VM_PAGE_TABLE_FRAME_COUNT,
    MICROS_VM_MAPPING_RECORD_SIZE = 512,
    MICROS_VM_MAPPING_RECORD_ARRAY_SIZE =
        MICROS_VM_MAPPING_RECORD_CAPACITY
            * MICROS_VM_MAPPING_RECORD_SIZE,
    MICROS_VM_INITIAL_FREE_BITMAP_WORDS =
        MICROS_VM_MAX_MANAGED_FRAMES / 64,
};

enum micros_vm_core_error {
    MICROS_VM_CORE_OK = 0,
    MICROS_VM_CORE_ERROR_ARGUMENT,
    MICROS_VM_CORE_ERROR_BOOT,
    MICROS_VM_CORE_ERROR_STATE,
    MICROS_VM_CORE_ERROR_NO_SPACE,
    MICROS_VM_CORE_ERROR_REQUEST,
    MICROS_VM_CORE_ERROR_EXECUTABLE,
    MICROS_VM_CORE_ERROR_TOO_BIG,
    MICROS_VM_CORE_ERROR_INVARIANT,
};

enum micros_vm_loader_phase {
    MICROS_VM_LOADER_UNINITIALIZED = 0,
    MICROS_VM_LOADER_IDLE,
    MICROS_VM_LOADER_INPUT_COPIED,
    MICROS_VM_LOADER_LOADING,
    MICROS_VM_LOADER_FROZEN,
    MICROS_VM_LOADER_EXIT_RELEASING,
};

enum micros_vm_mapping_record_phase {
    MICROS_VM_MAPPING_RECORD_UNUSED = 0,
    MICROS_VM_MAPPING_RECORD_LOADING,
    MICROS_VM_MAPPING_RECORD_FROZEN,
    MICROS_VM_MAPPING_RECORD_ACTIVE,
    MICROS_VM_MAPPING_RECORD_EXIT_RELEASING,
};

enum micros_vm_local_frame_state {
    MICROS_VM_LOCAL_FRAME_UNUSED = MICROS_VM_FRAME_UNUSED,
    MICROS_VM_LOCAL_FRAME_FREE = MICROS_VM_FRAME_FREE,
    MICROS_VM_LOCAL_FRAME_KERNEL = MICROS_VM_FRAME_KERNEL,
    MICROS_VM_LOCAL_FRAME_SELF_WIRED = MICROS_VM_FRAME_SELF_WIRED,
    MICROS_VM_LOCAL_FRAME_SERVICE_WIRED =
        MICROS_VM_FRAME_SERVICE_WIRED,
    MICROS_VM_LOCAL_FRAME_TRANSFERABLE =
        MICROS_VM_FRAME_TRANSFERABLE,
    MICROS_VM_LOCAL_FRAME_LOAD_RESERVED,
    MICROS_VM_LOCAL_FRAME_PROCESS_WIRED,
    MICROS_VM_LOCAL_FRAME_PROCESS_PAGE_TABLE,
};

enum micros_vm_mapping_entry_role {
    MICROS_VM_MAPPING_ENTRY_USER_PAGE = 1,
    MICROS_VM_MAPPING_ENTRY_ROOT_PAGE_TABLE,
    MICROS_VM_MAPPING_ENTRY_LEVEL1_PAGE_TABLE,
    MICROS_VM_MAPPING_ENTRY_IMAGE_PAGE_TABLE,
    MICROS_VM_MAPPING_ENTRY_STACK_PAGE_TABLE,
};

enum micros_vm_load_page_kind {
    MICROS_VM_LOAD_PAGE_IMAGE = 1,
    MICROS_VM_LOAD_PAGE_STACK,
};

struct micros_vm_mapping_entry {
    uint64_t virtual_address;
    uint64_t physical_address;
    uint32_t permissions;
    uint8_t role;
    uint8_t reserved[3];
};

struct micros_vm_mapping_record {
    uint64_t load_token;
    uint64_t mapping_generation;
    uint64_t semantic_pid;
    uint32_t process_generation;
    uint16_t process_slot;
    uint8_t phase;
    uint8_t entry_count;
    struct micros_vm_mapping_entry
        entries[MICROS_VM_MAPPING_ENTRY_CAPACITY];
};

struct micros_vm_load_transaction {
    uint64_t process_reservation_token;
    uint64_t load_token;
    uint64_t semantic_pid;
    uint64_t mapping_generation;
    struct micros_process_handle process;
    struct micros_application_stack_values stack_values;
    uint32_t executable_size;
    uint32_t request_size;
    uint32_t page_count;
    uint32_t completed_page_count;
    uint32_t scratch_page;
    uint32_t scratch_filled;
    uint64_t table_frames[MICROS_VM_PAGE_TABLE_FRAME_COUNT];
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    struct micros_vm_elf_plan elf_plan;
    struct micros_application_stack_plan stack_plan;
};

struct micros_vm_load_page_action {
    uint64_t virtual_address;
    uint64_t physical_address;
    uint32_t permissions;
    uint32_t page_index;
    uint32_t kind;
    uint32_t reserved;
};

struct micros_vm_freeze_plan {
    uint64_t load_token;
    uint64_t table_frames[MICROS_VM_PAGE_TABLE_FRAME_COUNT];
    uint64_t entry;
    uint64_t stack_pointer;
    uint32_t image_page_count;
    uint32_t data_page_count;
    uint64_t page_plan_digest;
};

struct micros_vm_exit_action {
    uint64_t load_token;
    uint64_t mapping_generation;
    uint64_t semantic_pid;
    uint32_t process_generation;
    uint16_t process_slot;
    uint16_t reserved;
};

struct micros_vm_state {
    uint64_t initialization_magic;
    uint32_t phase;
    uint32_t reserved;
    struct micros_vm_boot_info *boot_info;
    struct micros_vm_boot_summary boot_summary;
    struct micros_vm_load_transaction transaction;
    struct micros_vm_mapping_record
        mapping_records[MICROS_VM_MAPPING_RECORD_CAPACITY];
    uint64_t
        initial_free_bitmap[MICROS_VM_INITIAL_FREE_BITMAP_WORDS];
    uint8_t executable_bytes[MICROS_APPLICATION_EXECUTABLE_SIZE_MAX];
    uint8_t request_bytes[MICROS_SPAWN_REQUEST_SIZE_MAX];
};

enum micros_vm_core_error micros_vm_state_initialize(
    struct micros_vm_state *state,
    struct micros_vm_boot_info *boot_info,
    uint32_t expected_service_id,
    uint32_t expected_self_endpoint,
    uint32_t expected_address_space_count
);

enum micros_vm_core_error micros_vm_state_validate(
    const struct micros_vm_state *state
);

enum micros_vm_core_error micros_vm_load_prepare(
    struct micros_vm_state *state,
    uint64_t process_reservation_token,
    uint64_t semantic_pid,
    const uint8_t *executable_bytes,
    size_t executable_size,
    const uint8_t *request_bytes,
    size_t request_size,
    const struct micros_application_stack_values *stack_values
);

enum micros_vm_core_error micros_vm_load_bind(
    struct micros_vm_state *state,
    struct micros_process_handle process,
    uint64_t load_token
);

enum micros_vm_core_error micros_vm_load_next_page(
    const struct micros_vm_state *state,
    struct micros_vm_load_page_action *action
);

enum micros_vm_core_error micros_vm_load_scratch_mapped(
    struct micros_vm_state *state,
    uint32_t page_index
);

enum micros_vm_core_error micros_vm_load_fill_scratch(
    struct micros_vm_state *state,
    uint32_t page_index,
    uint8_t *page,
    size_t page_size
);

enum micros_vm_core_error micros_vm_load_scratch_committed(
    struct micros_vm_state *state,
    uint32_t page_index
);

enum micros_vm_core_error micros_vm_load_freeze_plan(
    const struct micros_vm_state *state,
    struct micros_vm_freeze_plan *plan
);

enum micros_vm_core_error micros_vm_load_frozen(
    struct micros_vm_state *state,
    uint64_t mapping_generation
);

enum micros_vm_core_error micros_vm_load_aborted(
    struct micros_vm_state *state,
    uint64_t load_token,
    uint64_t mapping_generation
);

enum micros_vm_core_error micros_vm_load_committed(
    struct micros_vm_state *state,
    uint64_t load_token,
    uint64_t mapping_generation,
    uint64_t semantic_pid
);

enum micros_vm_core_error micros_vm_exit_release_begin(
    struct micros_vm_state *state,
    uint64_t load_token,
    uint64_t mapping_generation,
    uint64_t semantic_pid,
    struct micros_vm_exit_action *action
);

enum micros_vm_core_error micros_vm_exit_release_complete(
    struct micros_vm_state *state,
    const struct micros_vm_exit_action *action
);

_Static_assert(
    sizeof(struct micros_vm_mapping_entry) == 24,
    "VM mapping entries must remain exactly 24 bytes"
);
_Static_assert(
    offsetof(struct micros_vm_mapping_entry, virtual_address) == 0
        && offsetof(
            struct micros_vm_mapping_entry,
            physical_address
        ) == 8
        && offsetof(struct micros_vm_mapping_entry, permissions) == 16
        && offsetof(struct micros_vm_mapping_entry, role) == 20
        && offsetof(struct micros_vm_mapping_entry, reserved) == 21,
    "VM mapping entry offsets changed"
);
_Static_assert(
    sizeof(struct micros_vm_mapping_record)
        == MICROS_VM_MAPPING_RECORD_SIZE,
    "VM mapping records must remain exactly 512 bytes"
);
_Static_assert(
    offsetof(struct micros_vm_mapping_record, load_token) == 0
        && offsetof(
            struct micros_vm_mapping_record,
            mapping_generation
        ) == 8
        && offsetof(struct micros_vm_mapping_record, semantic_pid) == 16
        && offsetof(
            struct micros_vm_mapping_record,
            process_generation
        ) == 24
        && offsetof(struct micros_vm_mapping_record, process_slot) == 28
        && offsetof(struct micros_vm_mapping_record, phase) == 30
        && offsetof(struct micros_vm_mapping_record, entry_count) == 31
        && offsetof(struct micros_vm_mapping_record, entries) == 32,
    "VM mapping record offsets changed"
);
_Static_assert(
    sizeof(((struct micros_vm_state *)0)->mapping_records)
        == MICROS_VM_MAPPING_RECORD_ARRAY_SIZE,
    "VM mapping record array must remain exactly eight pages"
);
_Static_assert(
    sizeof(((struct micros_vm_state *)0)->initial_free_bitmap)
        == 32768,
    "VM initial free-frame bitmap must remain exactly eight pages"
);
_Static_assert(
    sizeof(struct micros_vm_state)
        <= 50 * MICROS_APPLICATION_STACK_PAGE_SIZE,
    "VM portable state exceeds its resident-memory budget"
);

#endif
