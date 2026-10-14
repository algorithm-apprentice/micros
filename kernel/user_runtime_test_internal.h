#ifndef MICROS_KERNEL_USER_RUNTIME_TEST_INTERNAL_H
#define MICROS_KERNEL_USER_RUNTIME_TEST_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "kernel/user_runtime_test.h"
#include "micros/endpoint.h"
#include "micros/frame_ownership.h"
#include "micros/sv39.h"
#include "tests/qemu/user_runtime_protocol.h"

#define TEST_SSTATUS_UBE (UINT64_C(1) << 6)
#define TEST_SSTATUS_VS (UINT64_C(3) << 9)
#define TEST_SSTATUS_FS (UINT64_C(3) << 13)
#define TEST_SSTATUS_XS (UINT64_C(3) << 15)
#define TEST_SSTATUS_MXR (UINT64_C(1) << 19)
#define TEST_SSTATUS_SD (UINT64_C(1) << 63)

enum {
    USER_RUNTIME_CLIENT = 0,
    USER_RUNTIME_SERVER,
    USER_RUNTIME_PEER,
    USER_RUNTIME_PROCESS_COUNT,
    USER_RUNTIME_MAX_IMAGE_PAGES = 8,
    USER_RUNTIME_PROGRAM_EXECUTE = 1,
    USER_RUNTIME_PROGRAM_WRITE = 2,
    USER_RUNTIME_PROGRAM_READ = 4,
    USER_RUNTIME_EXCEPTION_BREAKPOINT = 3,
    USER_RUNTIME_EXCEPTION_STORE_PAGE_FAULT = 15,
};

struct mapped_page {
    uint64_t virtual_address;
    uint64_t physical_address;
    uint32_t permissions;
    size_t segment_index;
};

struct process_image {
    size_t page_count;
    struct mapped_page pages[USER_RUNTIME_MAX_IMAGE_PAGES];
    uint64_t stack_physical;
};

struct user_runtime_test_context {
    struct micros_process_handle processes[USER_RUNTIME_PROCESS_COUNT];
    struct micros_thread_handle threads[USER_RUNTIME_PROCESS_COUNT];
    micros_endpoint_t endpoints[USER_RUNTIME_PROCESS_COUNT];
    struct process_image images[USER_RUNTIME_PROCESS_COUNT];
    unsigned char writable_snapshots[
        USER_RUNTIME_PROCESS_COUNT
    ][USER_RUNTIME_MAX_IMAGE_PAGES][MICROS_SV39_PAGE_SIZE];
    bool writable_snapshot_valid[
        USER_RUNTIME_PROCESS_COUNT
    ][USER_RUNTIME_MAX_IMAGE_PAGES];
    struct micros_endpoint_registry *endpoint_registry;
    struct micros_grant_registry *grant_registry;
    uint64_t baseline_owned;
    uint64_t baseline_free;
    size_t baseline_processes;
    size_t baseline_threads;
    size_t baseline_harts;
    bool rodata_fault_seen;
    bool loader_validated;
    bool final_client_send;
    size_t returned_actor;
};

extern const unsigned char micros_user_runtime_test_supervisor_resume[];

extern uintptr_t micros_user_runtime_test_saved_sp;
extern uintptr_t micros_user_runtime_test_saved_gp;
extern uintptr_t micros_user_runtime_test_saved_tp;

struct user_runtime_test_context *user_runtime_test_state(void);

void user_runtime_test_clear_bytes(void *storage, size_t size);
void user_runtime_test_fill_bytes(
    void *storage,
    unsigned char value,
    size_t size
);
void user_runtime_test_copy_bytes(
    void *destination,
    const void *source,
    size_t size
);
bool user_runtime_test_bytes_equal(
    const void *left,
    const void *right,
    size_t size
);
bool user_runtime_test_bytes_are_zero(const void *storage, size_t size);
uint64_t user_runtime_test_read_satp(void);
size_t user_runtime_test_current_actor(
    const struct user_runtime_test_context *runtime,
    const struct micros_hart *hart
);

bool user_runtime_test_allocate_image_pages(
    struct user_runtime_test_context *runtime,
    size_t actor
);
bool user_runtime_test_copy_image_segments(
    struct user_runtime_test_context *runtime,
    size_t actor
);
bool user_runtime_test_verify_loaded_pages(
    const struct user_runtime_test_context *runtime,
    size_t actor
);
bool user_runtime_test_patch_configuration(
    const struct user_runtime_test_context *runtime,
    size_t actor
);
bool user_runtime_test_snapshot_writable_pages(
    struct user_runtime_test_context *runtime,
    size_t actor
);
bool user_runtime_test_writable_pages_match_snapshot(
    const struct user_runtime_test_context *runtime,
    size_t actor
);
bool user_runtime_test_read_configuration(
    const struct user_runtime_test_context *runtime,
    size_t actor,
    struct micros_user_runtime_test_config *config
);

bool user_runtime_test_preflight(
    struct user_runtime_test_context *runtime,
    const struct micros_frame_ownership *ledger,
    const struct micros_kernel_objects *objects
);
bool user_runtime_test_initialize_mechanisms(
    struct user_runtime_test_context *runtime,
    const struct micros_privilege_profile *profiles,
    size_t profile_count
);
bool user_runtime_test_prepare_identities(
    struct user_runtime_test_context *runtime,
    struct micros_kernel_objects *objects
);
bool user_runtime_test_prepare_images(
    struct user_runtime_test_context *runtime,
    struct micros_kernel_objects *objects
);
bool user_runtime_test_admit_threads(
    const struct user_runtime_test_context *runtime,
    struct micros_kernel_objects *objects,
    uint64_t interval
);

void user_runtime_test_report_success(void);
void user_runtime_test_report_failure(
    const struct user_runtime_test_context *runtime,
    uint64_t failure_stage
);
void user_runtime_test_report_actor(
    const struct user_runtime_test_context *runtime,
    size_t actor
);
_Noreturn void user_runtime_test_report_failure_shutdown(void);
_Noreturn void user_runtime_test_report_setup_failure(void);

#endif
