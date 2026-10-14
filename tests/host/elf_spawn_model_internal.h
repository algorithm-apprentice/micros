#ifndef MICROS_TESTS_HOST_ELF_SPAWN_MODEL_INTERNAL_H
#define MICROS_TESTS_HOST_ELF_SPAWN_MODEL_INTERNAL_H

#include <stdio.h>
#include <string.h>

#include "servers/pm/pm_coordinator.h"
#include "servers/vfs/vfs_service_core.h"
#include "servers/vm/vm_service_core.h"
#include "tests/host/vfs_test_fixture.h"
#include "tests/host/vm_core_test_internal.h"

enum {
    ELF_MODEL_FIRST_PROCESS = 6,
    ELF_MODEL_NO_SLOT = MICROS_PROCESS_CAPACITY,
    ELF_MODEL_TRACE_CAPACITY = 8448,
    ELF_MODEL_STAGE_CAPACITY = 32,
    ELF_MODEL_PM = 0x1002,
    ELF_MODEL_VM = 0x1001,
    ELF_MODEL_VFS = 0x1005,
};

enum elf_model_operation {
    ELF_MODEL_INIT,
    ELF_MODEL_SPAWN,
    ELF_MODEL_RESERVE_FAILURE,
    ELF_MODEL_GRANT_FAILURE,
    ELF_MODEL_MISSING_PATH,
    ELF_MODEL_BAD_EXECUTABLE,
    ELF_MODEL_BAD_DESCRIPTOR,
    ELF_MODEL_PREPARE_FAILURE,
    ELF_MODEL_ACTIVATE_FAILURE,
    ELF_MODEL_CANCEL_FROZEN,
    ELF_MODEL_CANCEL_ACTIVE,
    ELF_MODEL_BAD_PARENT_GRANT,
    ELF_MODEL_EXIT,
    ELF_MODEL_WAIT,
    ELF_MODEL_MALFORMED_PM,
    ELF_MODEL_INVALID_VM,
    ELF_MODEL_INVALID_VFS,
    ELF_MODEL_OPERATION_COUNT,
};

enum elf_model_stage {
    ELF_STAGE_RESERVE = 1,
    ELF_STAGE_REQUEST_GRANT,
    ELF_STAGE_IMAGE,
    ELF_STAGE_REVOKE_REQUEST,
    ELF_STAGE_LOAD,
    ELF_STAGE_DESCRIPTORS,
    ELF_STAGE_PREPARE,
    ELF_STAGE_HIDDEN,
    ELF_STAGE_ACTIVATE,
    ELF_STAGE_COMMIT_LOAD,
    ELF_STAGE_RELEASE_IMAGE,
    ELF_STAGE_REVERT_HIDDEN,
    ELF_STAGE_ABORT_PREPARED,
    ELF_STAGE_ABORT_DESCRIPTORS,
    ELF_STAGE_ABORT_LOAD,
    ELF_STAGE_ABORT_RESERVED,
    ELF_STAGE_EXIT_BEGIN,
    ELF_STAGE_EXIT_VFS,
    ELF_STAGE_EXIT_DETACH,
    ELF_STAGE_EXIT_VM,
    ELF_STAGE_EXIT_RELEASE,
};

enum elf_model_kernel_phase {
    ELF_KERNEL_FREE,
    ELF_KERNEL_STATIC,
    ELF_KERNEL_RESERVED,
    ELF_KERNEL_LOADING,
    ELF_KERNEL_FROZEN,
    ELF_KERNEL_PREPARED,
    ELF_KERNEL_ACTIVE,
    ELF_KERNEL_STOPPED,
    ELF_KERNEL_DETACHED,
    ELF_KERNEL_EMPTY,
};

enum elf_model_frame_owner {
    ELF_FRAME_BOOT,
    ELF_FRAME_FREE,
    ELF_FRAME_TRANSFERABLE,
    ELF_FRAME_TABLE,
    ELF_FRAME_USER,
};

enum elf_reference_process_state {
    ELF_REFERENCE_FREE,
    ELF_REFERENCE_RUNNING,
    ELF_REFERENCE_ZOMBIE,
};

struct elf_model_kernel_process {
    enum elf_model_kernel_phase phase;
    uint32_t generation;
    uint64_t reservation;
    uint64_t load;
    uint64_t mapping;
    uint64_t prepared;
    uint64_t exit;
    uint32_t page_count;
    uint32_t page_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    uint64_t page_addresses[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    uint32_t page_permissions[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    uint32_t table_frames[4];
    bool scratch_mapped;
};

struct elf_reference_process {
    enum elf_reference_process_state state;
    uint32_t generation;
    uint64_t pid;
    uint16_t parent;
    uint16_t kernel_slot;
    uint16_t vfs_slot;
    uint32_t kernel_generation;
    uint64_t load;
    uint64_t mapping;
    uint32_t frames[8];
    int32_t status;
    bool waiting;
    uint32_t wait_flags;
    uint64_t wait_selector;
    uint64_t wait_token;
};

struct elf_reference_state {
    struct elf_reference_process processes[MICROS_PM_PROCESS_CAPACITY];
    uint64_t last_pid;
    uint64_t spawn_tokens;
    uint64_t image_tokens;
    uint64_t descriptor_tokens;
};

struct elf_model_spawn_plan {
    size_t child_slot;
    bool consumed;
    bool reserved;
    bool image;
    bool frozen;
    bool descriptors;
    bool prepared;
    bool hidden;
    bool active;
    bool parent_lost;
    size_t forward_count;
    enum micros_pm_result result;
};

struct elf_model_wait_reply {
    uint64_t token;
    uint64_t pid;
    int32_t status;
};

struct elf_model_trace_entry {
    enum elf_model_operation operation;
    size_t parent;
    uint64_t argument;
    uint64_t random;
};

struct elf_spawn_model_context {
    struct micros_pm_table pm;
    struct micros_pm_coordinator coordinator;
    struct vm_core_test_context *vm;
    struct micros_vm_service vm_service;
    struct micros_vfs_state vfs;
    struct vfs_test_fixture filesystem;
    struct elf_reference_state reference;
    struct elf_model_kernel_process kernel[MICROS_PROCESS_CAPACITY];
    enum elf_model_frame_owner frame_owners[TEST_MANAGED_FRAME_COUNT];
    uint8_t initial_frames[TEST_MANAGED_FRAME_COUNT];
    struct micros_vfs_spawn_transaction idle_vfs_spawn;
    struct micros_vfs_pending_operation idle_vfs_pending;
    struct micros_vfs_vnode root_vnode;
    uint64_t initial_tty_request;
    uint64_t last_reservation;
    uint64_t last_load;
    uint64_t last_mapping;
    uint64_t last_prepared;
    uint64_t last_exit;
    uint64_t last_reply;
    size_t kernel_slot;
    size_t supplied_slot;
    uint32_t supplied_generation;
    uint64_t supplied_load;
    uint64_t supplied_mapping;
    size_t executable_size;
    size_t executable_node;
    uint64_t root_node;
    enum elf_model_operation operation;
    size_t parent;
    uint64_t argument;
    uint64_t operation_reply;
    uint32_t wait_flags;
    struct elf_model_spawn_plan spawn_plan;
    uint32_t stages[ELF_MODEL_STAGE_CAPACITY];
    size_t stage_count;
    uint32_t expected_stages[ELF_MODEL_STAGE_CAPACITY];
    size_t expected_stage_count;
    struct elf_model_wait_reply replies[2];
    size_t reply_count;
    struct elf_model_wait_reply expected_replies[2];
    size_t expected_reply_count;
    uint64_t seed;
    uint64_t random;
    uint64_t trace_digest;
    struct elf_model_trace_entry trace[ELF_MODEL_TRACE_CAPACITY];
    size_t trace_count;
    uint64_t coverage[ELF_MODEL_OPERATION_COUNT];
    uint64_t stage_coverage[ELF_STAGE_EXIT_RELEASE + 1];
    size_t double_wait_completions;
    size_t vm_capacity_rejections;
    bool violation;
};

#define ELF_MODEL_EXPECT(context, expression) \
    do { \
        if (!(expression)) { \
            elf_model_report_failure((context), __FILE__, __LINE__, #expression); \
            return false; \
        } \
    } while (false)

struct elf_spawn_model_context *elf_model_context_get(void);
bool elf_model_initialize(struct elf_spawn_model_context *context);
bool elf_model_record_stage(struct elf_spawn_model_context *context, uint32_t stage);
bool elf_model_kernel_require(struct elf_spawn_model_context *context, bool valid);
uint64_t elf_model_read_word(const uint8_t *bytes);
void elf_model_report_failure(
    const struct elf_spawn_model_context *context, const char *file, int line,
    const char *expression
);
void elf_model_cancel_at(struct elf_spawn_model_context *context, uint32_t stage);
const struct micros_pm_coordinator_backend *elf_model_pm_backend(void);
const struct micros_vm_service_backend *elf_model_vm_backend(void);
micros_runtime_result_t elf_model_grant_create(
    void *context, micros_endpoint_t grantee, uintptr_t base, size_t length,
    uint32_t permissions, micros_grant_t *grant
);
micros_runtime_result_t elf_model_grant_revoke(void *context, micros_grant_t grant);
micros_runtime_result_t elf_model_parent_copy(
    void *context, micros_endpoint_t grantor, micros_grant_t grant,
    size_t offset, uintptr_t destination, size_t length
);
micros_runtime_result_t elf_model_call(
    void *context, micros_endpoint_t peer, struct micros_ipc_message *message
);
micros_runtime_result_t elf_model_reply(
    void *context, uint64_t token, const struct micros_ipc_message *message
);
bool elf_model_run_operation(
    struct elf_spawn_model_context *context, enum elf_model_operation operation,
    size_t parent, uint64_t argument, uint64_t random
);
struct elf_model_spawn_plan elf_reference_plan_spawn(
    const struct elf_spawn_model_context *context, enum elf_model_operation operation
);
void elf_reference_spawn_trace(struct elf_spawn_model_context *context);
bool elf_reference_apply_spawn(struct elf_spawn_model_context *context);
void elf_reference_apply_exit(struct elf_spawn_model_context *context);
enum micros_pm_result elf_reference_apply_wait(
    struct elf_spawn_model_context *context, bool *blocked, bool *completed
);
bool elf_model_compare(const struct elf_spawn_model_context *context);
bool elf_model_compare_response(
    const struct elf_spawn_model_context *context, const struct micros_pm_coordinator_response *response,
    enum micros_pm_coordinator_response_kind kind, uint32_t type,
    enum micros_pm_result result, uint64_t pid
);
bool elf_model_oracle_self_test(struct elf_spawn_model_context *context);

#endif
