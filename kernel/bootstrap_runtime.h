#ifndef MICROS_KERNEL_BOOTSTRAP_RUNTIME_H
#define MICROS_KERNEL_BOOTSTRAP_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "kernel/bootstrap_control_internal.h"
#include "kernel/bootstrap_image.h"

struct micros_bootstrap_scheduler_policy {
    uint32_t service_id;
    uint8_t priority;
    bool preemptible;
    uint16_t reserved;
    uint64_t quantum_counter_ticks;
};

struct micros_bootstrap_runtime_config {
    const struct micros_bootstrap_manifest *manifest;
    const struct micros_bootstrap_expected_service *expected_services;
    size_t expected_service_count;
    const struct micros_bootstrap_image *images;
    size_t image_count;
    const struct micros_privilege_profile *profiles;
    size_t profile_count;
    const struct micros_bootstrap_scheduler_policy *policies;
    size_t policy_count;
    uint64_t scheduler_preemption_interval;
};

enum micros_bootstrap_error micros_bootstrap_runtime_prepare(
    const struct micros_bootstrap_runtime_config *config
);

enum micros_bootstrap_error
micros_bootstrap_runtime_publish_controller(void);

_Noreturn void micros_bootstrap_runtime_launch(
    const struct micros_bootstrap_runtime_config *config
);

const struct micros_bootstrap_control_state *
micros_bootstrap_runtime_state(void);

struct micros_bootstrap_control_state *
micros_bootstrap_runtime_authoritative_state(void);

enum micros_bootstrap_error micros_bootstrap_runtime_validate(void);

enum micros_bootstrap_error
micros_bootstrap_runtime_validate_vm_prepared(void);

bool micros_bootstrap_runtime_is_active_controller(
    struct micros_process_handle process
);

_Noreturn void micros_bootstrap_runtime_fail(
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t detail
);

void micros_bootstrap_runtime_check_deadline(uint64_t now);

#endif
