#ifndef MICROS_TESTS_HOST_BOOTSTRAP_RUNTIME_MODEL_INTERNAL_H
#define MICROS_TESTS_HOST_BOOTSTRAP_RUNTIME_MODEL_INTERNAL_H

#include "micros/bootstrap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct reference_runtime_entry {
    uint32_t service_id;
    uint64_t prerequisites;
    uint64_t ready_timeout_counter_ticks;
    uint64_t ready_deadline;
    enum micros_bootstrap_service_state state;
    enum micros_bootstrap_endpoint_state endpoint_state;
    bool profile_installed;
    bool scheduler_assigned;
};

struct reference_runtime {
    enum micros_bootstrap_phase phase;
    uint16_t entry_count;
    uint16_t next_order_index;
    uint32_t controller_service_id;
    uint32_t starting_service_id;
    struct reference_runtime_entry entries[2];
    uint32_t ordered_service_ids[2];
};

enum model_operation {
    MODEL_RESET = 0,
    MODEL_INITIALIZE,
    MODEL_RELEASE,
    MODEL_ACCEPT_READY,
    MODEL_COMPLETE,
};

struct model_trace_entry {
    size_t step;
    enum model_operation operation;
    uint32_t service_id;
    uint64_t now;
    enum micros_bootstrap_error expected_error;
    enum micros_bootstrap_error actual_error;
    enum micros_bootstrap_ready_class expected_class;
    enum micros_bootstrap_ready_class actual_class;
};

enum micros_bootstrap_error bootstrap_runtime_reference_initialize(
    struct reference_runtime *runtime
);
enum micros_bootstrap_error bootstrap_runtime_reference_release(
    struct reference_runtime *runtime,
    uint32_t service_id,
    uint64_t now
);
enum micros_bootstrap_error bootstrap_runtime_reference_accept_ready(
    struct reference_runtime *runtime,
    uint32_t service_id,
    uint64_t now
);
enum micros_bootstrap_error bootstrap_runtime_reference_complete(
    struct reference_runtime *runtime
);
enum micros_bootstrap_ready_class bootstrap_runtime_reference_classify_ready(
    const struct reference_runtime *runtime,
    uint32_t source_service_id,
    uint32_t payload_service_id,
    bool message_shape_valid
);
void bootstrap_runtime_project_reference(
    const struct reference_runtime *reference,
    struct micros_bootstrap_runtime *runtime
);

#endif
