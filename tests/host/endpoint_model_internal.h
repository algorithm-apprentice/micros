#ifndef MICROS_TESTS_HOST_ENDPOINT_MODEL_INTERNAL_H
#define MICROS_TESTS_HOST_ENDPOINT_MODEL_INTERNAL_H

#include "micros/endpoint.h"

enum {
    MODEL_SLOT_COUNT = 4,
    MODEL_STEPS = 4096,
    MODEL_TRACE_COUNT = 32,
};

enum model_endpoint_phase {
    MODEL_ENDPOINT_UNUSED = 0,
    MODEL_ENDPOINT_RESERVED,
    MODEL_ENDPOINT_PROFILED,
    MODEL_ENDPOINT_ACTIVE,
    MODEL_ENDPOINT_CLOSED,
};

enum model_operation {
    MODEL_OPERATION_CREATE = 0,
    MODEL_OPERATION_RESERVE,
    MODEL_OPERATION_INSTALL,
    MODEL_OPERATION_ACTIVATE,
    MODEL_OPERATION_AUTHORIZE,
    MODEL_OPERATION_CLOSE,
    MODEL_OPERATION_RELEASE,
    MODEL_OPERATION_STALE,
    MODEL_OPERATION_COUNT,
};

struct model_slot {
    bool process_live;
    bool thread_live;
    uint32_t generation;
    struct micros_process_handle process;
    struct micros_thread_handle thread;
    enum model_endpoint_phase endpoint_phase;
    micros_endpoint_t endpoint;
    micros_endpoint_t stale_endpoint;
    uint8_t profile;
};

struct model_coverage {
    size_t create;
    size_t release;
    size_t reserve;
    size_t reserve_rejected;
    size_t install_client;
    size_t install_server;
    size_t install_invalid;
    size_t activate;
    size_t close;
    size_t authorize_allowed;
    size_t authorize_denied;
    size_t authorize_state;
    size_t notify_allowed;
    size_t notify_denied;
    size_t reply_allowed;
    size_t reply_denied;
    size_t reply_receive_allowed;
    size_t reply_receive_denied;
    size_t stale;
};

struct model_trace_entry {
    size_t step;
    enum model_operation operation;
    size_t slot;
    uint32_t value;
};

struct endpoint_model_fixture {
    struct micros_endpoint_registry registry;
    struct micros_kernel_objects objects;
    struct micros_hart_handle hart;
    struct model_trace_entry trace[MODEL_TRACE_COUNT];
    size_t trace_count;
};

struct endpoint_model_fixture *endpoint_model_context(void);

uint32_t endpoint_model_next_random(uint32_t *state);

void endpoint_model_record_transition(
    struct endpoint_model_fixture *fixture,
    size_t step,
    enum model_operation operation,
    size_t slot,
    uint32_t value
);

bool endpoint_model_model_fail(
    const struct endpoint_model_fixture *fixture,
    size_t step,
    enum model_operation operation,
    uint32_t value,
    const char *message
);

bool endpoint_model_setup_fixture(struct endpoint_model_fixture *fixture);

bool endpoint_model_phase_has_endpoint(enum model_endpoint_phase phase);

size_t endpoint_model_find_slot(
    const struct model_slot model[MODEL_SLOT_COUNT],
    uint32_t random,
    bool (*matches)(const struct model_slot *slot)
);

bool endpoint_model_slot_is_live(const struct model_slot *slot);

bool endpoint_model_slot_has_endpoint(const struct model_slot *slot);

bool endpoint_model_slot_has_nonactive_endpoint(const struct model_slot *slot);

bool endpoint_model_slot_is_releasable(const struct model_slot *slot);

bool endpoint_model_slot_has_stale_endpoint(const struct model_slot *slot);

size_t endpoint_model_find_active_profile(
    const struct model_slot model[MODEL_SLOT_COUNT],
    uint32_t random,
    uint8_t profile
);

bool endpoint_model_target_is_allowed(
    uint8_t source_profile,
    uint32_t operation,
    uint8_t target_profile
);

bool endpoint_model_validate_model(
    const struct endpoint_model_fixture *fixture,
    const struct model_slot model[MODEL_SLOT_COUNT],
    size_t step,
    enum model_operation operation,
    uint32_t value
);

bool micros_endpoint_model_test_run(void);

#endif
