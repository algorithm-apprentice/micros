#ifndef MICROS_KERNEL_BOOTSTRAP_RUNTIME_INTERNAL_H
#define MICROS_KERNEL_BOOTSTRAP_RUNTIME_INTERNAL_H

#include "kernel/bootstrap_runtime.h"
#include "kernel/vm/vm_snapshot.h"

struct micros_frame_allocator;

struct preparation_record {
    bool process_created;
    bool root_created;
    bool endpoint_reserved;
    bool thread_created;
    bool context_prepared;
    bool manifest_view_allocated;
    uint32_t image_pages_allocated;
    uint16_t stack_pages_allocated;
    struct micros_process_handle process;
    struct micros_thread_handle thread;
    micros_endpoint_t endpoint;
    const struct micros_bootstrap_image *image;
    const struct micros_bootstrap_manifest_entry *entry;
};

struct preparation_baseline {
    size_t live_process_count;
    size_t live_thread_count;
    uint64_t free_frame_count;
    uint64_t owned_frame_count;
    bool valid;
};

struct bootstrap_runtime_context {
    struct micros_bootstrap_control_state bootstrap_state;
    struct micros_bootstrap_manifest_plan validation_plan;
    struct micros_bootstrap_image_info
        image_infos[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct micros_bootstrap_binding
        prepared_bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct preparation_record
        preparation_records[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    uint16_t preparation_order[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct preparation_baseline preparation_baseline;
    struct micros_bootstrap_diagnostic preparation_diagnostic;
    struct micros_vm_snapshot_result prepared_vm_snapshot;
    bool failure_record_emitted;
};

struct bootstrap_runtime_context *bootstrap_runtime_context(void);
void bootstrap_runtime_set_preparation_diagnostic(
    struct bootstrap_runtime_context *runtime,
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t detail
);
void bootstrap_runtime_clear_bytes(void *storage, size_t size);
void bootstrap_runtime_copy_bytes(
    void *destination,
    const void *source,
    size_t size
);
bool bootstrap_runtime_bytes_equal(
    const void *left,
    const void *right,
    size_t size
);
_Noreturn void bootstrap_runtime_panic_runtime(const char *reason);

enum micros_bootstrap_error bootstrap_preparation_reset(
    struct bootstrap_runtime_context *runtime
);
void bootstrap_preparation_rollback_preparation(
    struct bootstrap_runtime_context *runtime,
    size_t count
);
enum micros_bootstrap_error bootstrap_preparation_validate_configuration(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config,
    const struct micros_frame_allocator *allocator
);
enum micros_bootstrap_error bootstrap_preparation_initialize_mechanisms(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config,
    const struct micros_frame_allocator *allocator
);
enum micros_bootstrap_error bootstrap_preparation_prepare_services(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config
);
enum micros_bootstrap_error bootstrap_preparation_prepare_roles(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config
);
enum micros_bootstrap_error bootstrap_preparation_prepare_control_state(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config
);

bool bootstrap_preparation_allocate_image(
    struct preparation_record *record
);
bool bootstrap_preparation_verify_image(
    const struct preparation_record *record
);
bool bootstrap_preparation_allocate_stack(
    struct preparation_record *record
);
bool bootstrap_preparation_verify_stack(
    const struct preparation_record *record
);
bool bootstrap_preparation_allocate_manifest_view(
    struct preparation_record *record,
    const struct micros_bootstrap_manifest *manifest
);
bool bootstrap_preparation_write_user_range(
    struct micros_process_handle process,
    uint64_t address,
    const void *source,
    size_t size
);
bool bootstrap_preparation_user_range_matches(
    struct micros_process_handle process,
    uint64_t address,
    const void *expected,
    size_t size
);
bool bootstrap_preparation_patch_configurations(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_binding *bindings,
    const struct preparation_record *records
);

#endif
