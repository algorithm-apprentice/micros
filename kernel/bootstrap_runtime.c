#include "kernel/bootstrap_runtime_internal.h"

#include "arch/riscv64/interrupt.h"
#include "kernel/endpoint/endpoint_internal.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/objects/kernel_object_runtime_internal.h"
#include "kernel/scheduler/scheduler_core_internal.h"
#include "micros/bootstrap_memory.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/user_execution.h"

enum micros_bootstrap_error micros_bootstrap_runtime_prepare(
    const struct micros_bootstrap_runtime_config *config
)
{
    struct bootstrap_runtime_context *runtime;
    const struct micros_frame_allocator *allocator;
    enum micros_bootstrap_error error;
    uintptr_t saved_status;

    if (
        config == NULL
        || config->manifest == NULL
        || config->expected_services == NULL
        || config->images == NULL
        || config->profiles == NULL
        || config->scheduler_preemption_interval == 0
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    runtime = bootstrap_runtime_context();
    saved_status = riscv_irq_save();
    bootstrap_runtime_clear_bytes(
        &runtime->preparation_diagnostic,
        sizeof(runtime->preparation_diagnostic)
    );
    if (
        runtime->bootstrap_state.phase != MICROS_BOOTSTRAP_PHASE_UNINITIALIZED
        || config->expected_service_count
            != config->manifest->header.entry_count
        || config->image_count
            != config->manifest->header.entry_count
        || config->profile_count
            < config->manifest->header.entry_count
    ) {
        error = MICROS_BOOTSTRAP_ERROR_STATE;
        goto done;
    }
    error = bootstrap_preparation_reset(runtime);
    if (error != MICROS_BOOTSTRAP_OK) {
        goto done;
    }
    allocator = micros_bootstrap_frame_allocator();
    if (allocator == NULL) {
        error = MICROS_BOOTSTRAP_ERROR_INVARIANT;
        goto done;
    }
    error = bootstrap_preparation_validate_configuration(
        runtime,
        config,
        allocator
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        goto done;
    }
    error = bootstrap_preparation_initialize_mechanisms(
        runtime,
        config,
        allocator
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        goto done;
    }
    error = bootstrap_preparation_prepare_services(runtime, config);
    if (error != MICROS_BOOTSTRAP_OK) {
        goto done;
    }
    error = bootstrap_preparation_prepare_roles(runtime, config);
    if (error != MICROS_BOOTSTRAP_OK) {
        goto done;
    }
    error = bootstrap_preparation_prepare_control_state(runtime, config);

done:
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_bootstrap_error
micros_bootstrap_runtime_publish_controller(void)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    struct micros_endpoint_registry *registry =
        micros_ipc_runtime_authoritative_registry();
    struct bootstrap_runtime_context *runtime = bootstrap_runtime_context();
    const struct micros_bootstrap_binding *controller;
    const struct micros_bootstrap_manifest_entry *entry;
    struct micros_user_context context;
    struct micros_bootstrap_runtime transition_preflight;
    enum micros_kernel_object_error scheduler_error;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    bootstrap_runtime_clear_bytes(
        &transition_preflight,
        sizeof(transition_preflight)
    );
    if (
        objects == NULL
        || registry == NULL
        || runtime->bootstrap_state.phase
            != MICROS_BOOTSTRAP_PHASE_PREPARING
    ) {
        riscv_irq_restore(saved_status);
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    controller = micros_bootstrap_control_find_binding(
        &runtime->bootstrap_state,
        runtime->bootstrap_state.plan.controller_service_id
    );
    if (controller == NULL) {
        riscv_irq_restore(saved_status);
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    entry = &runtime->bootstrap_state.manifest.entries[
        controller->manifest_index
    ];
    if (
        micros_bootstrap_control_validate(
            &runtime->bootstrap_state,
            registry,
            objects
        ) != MICROS_BOOTSTRAP_OK
        || micros_endpoint_preflight_publish(
            registry,
            objects,
            controller->endpoint,
            entry->profile_id
        ) != MICROS_ENDPOINT_OK
        || micros_thread_scheduler_admit_preflight(
            objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            controller->thread,
            controller->scheduler_priority,
            controller->scheduler_quantum_counter_ticks
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_user_execution_inspect(
            controller->thread,
            &context
        ) != MICROS_USER_EXECUTION_OK
        || micros_user_execution_validate_context(
            controller->thread,
            &context
        ) != MICROS_USER_EXECUTION_OK
        || micros_bootstrap_runtime_initialize(
            &runtime->bootstrap_state.manifest,
            &runtime->bootstrap_state.plan,
            &transition_preflight
        ) != MICROS_BOOTSTRAP_OK
        || micros_scheduler_prepare_start_timer()
            != MICROS_SCHEDULER_OK
    ) {
        bootstrap_preparation_rollback_preparation(
            runtime,
            runtime->bootstrap_state.entry_count
        );
        bootstrap_runtime_clear_bytes(
            &runtime->bootstrap_state,
            sizeof(runtime->bootstrap_state)
        );
        riscv_irq_restore(saved_status);
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }

    micros_endpoint_commit_publish_prevalidated(
        registry,
        objects,
        controller->endpoint,
        entry->profile_id
    );
    scheduler_error = micros_thread_scheduler_admit(
        objects,
        micros_kernel_object_runtime_boot_hart_handle(),
        controller->thread,
        controller->scheduler_priority,
        controller->scheduler_quantum_counter_ticks,
        controller->scheduler_preemptible
    );
    if (
        scheduler_error != MICROS_KERNEL_OBJECT_OK
        || micros_bootstrap_control_publish_controller(
            &runtime->bootstrap_state,
            registry,
            objects
        ) != MICROS_BOOTSTRAP_OK
    ) {
        bootstrap_runtime_panic_runtime("bootstrap-controller-publication");
    }
    riscv_irq_restore(saved_status);
    return MICROS_BOOTSTRAP_OK;
}

_Noreturn void micros_bootstrap_runtime_launch(
    const struct micros_bootstrap_runtime_config *config
)
{
    uintptr_t saved_status = riscv_irq_save();
    const struct bootstrap_runtime_context *runtime =
        bootstrap_runtime_context();
    const struct micros_bootstrap_binding *controller;
    uint32_t controller_service_id = 0;
    micros_endpoint_t controller_endpoint = MICROS_ENDPOINT_NONE;
    enum micros_bootstrap_error error =
        micros_bootstrap_runtime_prepare(config);

    if (error != MICROS_BOOTSTRAP_OK) {
        micros_bootstrap_runtime_fail(
            runtime->preparation_diagnostic.reason == 0
                ? MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE
                : runtime->preparation_diagnostic.reason,
            runtime->preparation_diagnostic.service_id,
            runtime->preparation_diagnostic.endpoint == 0
                ? MICROS_ENDPOINT_NONE
                : runtime->preparation_diagnostic.endpoint,
            runtime->preparation_diagnostic.detail
        );
    }
    controller_service_id =
        runtime->bootstrap_state.plan.controller_service_id;
    controller = micros_bootstrap_control_find_binding(
        &runtime->bootstrap_state,
        controller_service_id
    );
    if (controller == NULL) {
        micros_bootstrap_runtime_fail(
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            controller_service_id,
            MICROS_ENDPOINT_NONE,
            0
        );
    }
    controller_endpoint = controller->endpoint;
    if (
        micros_bootstrap_runtime_publish_controller()
            != MICROS_BOOTSTRAP_OK
    ) {
        micros_bootstrap_runtime_fail(
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            controller_service_id,
            controller_endpoint,
            0
        );
    }
    if (micros_scheduler_start() != MICROS_SCHEDULER_OK) {
        micros_bootstrap_runtime_fail(
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            controller_service_id,
            controller_endpoint,
            0
        );
    }
    (void)saved_status;
    bootstrap_runtime_panic_runtime("bootstrap-scheduler-returned");
}
