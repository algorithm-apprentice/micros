#ifndef MICROS_KERNEL_BOOTSTRAP_TRANSITIONS_INTERNAL_H
#define MICROS_KERNEL_BOOTSTRAP_TRANSITIONS_INTERNAL_H

#include "micros/bootstrap.h"

struct bootstrap_transition_plan {
    uint16_t entry_index;
    uint16_t next_order_index;
    enum micros_bootstrap_phase phase;
    uint32_t controller_service_id;
    uint32_t starting_service_id;
    struct micros_bootstrap_runtime_entry entry;
};

_Static_assert(sizeof(struct micros_bootstrap_runtime_entry) == 48,
               "Bootstrap runtime entry layout changed");
_Static_assert(sizeof(struct micros_bootstrap_runtime) <= 448,
               "Bootstrap runtime exceeds its storage bound");
_Static_assert(sizeof(struct bootstrap_transition_plan) <= 64,
               "Bootstrap transition plan exceeds its storage bound");

const struct micros_bootstrap_runtime_entry *bootstrap_runtime_find_entry(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t service_id
);

bool bootstrap_runtime_validate_index(
    const struct micros_bootstrap_runtime *runtime
);

enum micros_bootstrap_error bootstrap_runtime_prepare_release(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint64_t now,
    uint64_t retained_deadline,
    bool retain_deadline,
    struct bootstrap_transition_plan *plan
);

enum micros_bootstrap_error bootstrap_runtime_prepare_ready(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint64_t now,
    struct bootstrap_transition_plan *plan
);

enum micros_bootstrap_error bootstrap_runtime_prepare_complete(
    const struct micros_bootstrap_runtime *runtime,
    struct bootstrap_transition_plan *plan
);

void bootstrap_runtime_commit_prevalidated(
    struct micros_bootstrap_runtime *runtime,
    const struct bootstrap_transition_plan *plan
);

#endif
