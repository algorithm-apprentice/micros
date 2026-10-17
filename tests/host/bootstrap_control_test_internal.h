#ifndef MICROS_TESTS_HOST_BOOTSTRAP_CONTROL_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_BOOTSTRAP_CONTROL_TEST_INTERNAL_H

#include "kernel/boot/bootstrap_control_internal.h"
#include "kernel/boot/bootstrap_image.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            return false; \
        } \
    } while (false)

bool bootstrap_control_test_setup_control_fixture(
    struct micros_bootstrap_control_state *state,
    struct micros_bootstrap_manifest *manifest,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle *hart,
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY],
    bool include_pm
);
bool bootstrap_control_test_release_and_accept_ready(
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    const struct micros_bootstrap_binding *binding,
    uint64_t release_counter,
    uint64_t ready_counter
);
bool bootstrap_control_test_setup_ready_pm_fixture(
    struct micros_bootstrap_control_state *state,
    struct micros_bootstrap_manifest *manifest,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle *hart,
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY]
);
bool bootstrap_control_test_commit_complete_for_test(
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    const struct micros_bootstrap_binding *controller,
    struct micros_bootstrap_complete_plan *plan
);
bool bootstrap_control_test_commit_current_complete_for_test(
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    const struct micros_bootstrap_binding *controller,
    struct micros_bootstrap_complete_plan *plan
);
uint32_t bootstrap_control_test_read_u32_le(const unsigned char *bytes);
void bootstrap_control_test_write_u32_le(
    unsigned char *bytes,
    uint32_t value
);
bool bootstrap_control_test_sealed_notification_matches(
    const struct micros_ipc_message *message
);

bool test_valid_commands(void);
bool test_malformed_commands(void);
bool test_binding_lookup_rejects_oversized_count(void);
bool test_control_transitions(void);
bool test_pm_completion_rejections_are_atomic(void);
bool test_completion_rejects_launcher_ipc_dependencies(void);
bool test_pm_completion_queues_sealed_event_once(void);
bool test_pm_completion_wakes_only_any_receiver(void);
bool test_pm_completion_preserves_specific_receiver(void);
bool test_image_catalog(void);

#endif
