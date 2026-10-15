#ifndef MICROS_KERNEL_ENDPOINT_TEST_INTERNAL_H
#define MICROS_KERNEL_ENDPOINT_TEST_INTERNAL_H

#include "micros/endpoint.h"
#include "micros/grant.h"

struct endpoint_qemu_work {
    struct micros_kernel_objects *objects;
    struct micros_endpoint_registry *registry;
    struct micros_grant_registry *grant_registry;
    struct micros_grant_cancel_plan cancel_plan;
    struct micros_grant_record grant_record;
    struct micros_process_handle processes[2];
    struct micros_process_handle replacement_process;
    struct micros_thread_handle threads[2];
    struct micros_thread_handle replacement_thread;
    micros_endpoint_t endpoints[2];
    micros_endpoint_t replacement_endpoint;
    micros_grant_t read_grant;
    micros_grant_t write_grant;
    micros_grant_t revoked_grant;
    const struct micros_endpoint_record *record;
    size_t baseline_processes;
    size_t baseline_threads;
    size_t baseline_harts;
};

bool endpoint_qemu_capture_baseline(struct endpoint_qemu_work *work);

bool endpoint_qemu_initialize_runtimes(struct endpoint_qemu_work *work);

bool endpoint_qemu_reserve_clients(struct endpoint_qemu_work *work);

bool endpoint_qemu_require_held_owners(struct endpoint_qemu_work *work);

bool endpoint_qemu_publish_and_check_authority(struct endpoint_qemu_work *work);

bool endpoint_qemu_prepare_grant_cancellation(struct endpoint_qemu_work *work);

bool endpoint_qemu_close_commit_and_reuse(struct endpoint_qemu_work *work);

bool endpoint_qemu_reject_stale_and_cleanup(struct endpoint_qemu_work *work);

bool micros_endpoint_runtime_run_self_test(void);

#endif
