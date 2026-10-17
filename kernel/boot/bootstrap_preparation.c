#include "kernel/boot/bootstrap_runtime_internal.h"

#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/objects/kernel_object_runtime_internal.h"
#include "kernel/pm/pm_control_runtime.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/vm/vm_handoff_runtime.h"
#include "micros/bootstrap_memory.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/scheduler_core.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

static const struct micros_bootstrap_image *find_image(
    const struct micros_bootstrap_runtime_config *config,
    uint32_t image_id
)
{
    size_t index;

    for (index = 0; index < config->image_count; ++index) {
        if (config->images[index].image_id == image_id) {
            return &config->images[index];
        }
    }
    return NULL;
}

static const struct micros_bootstrap_scheduler_policy *find_policy(
    const struct micros_bootstrap_runtime_config *config,
    uint32_t service_id
)
{
    size_t index;

    for (index = 0; index < config->policy_count; ++index) {
        if (config->policies[index].service_id == service_id) {
            return &config->policies[index];
        }
    }
    return NULL;
}

static enum micros_bootstrap_error validate_policies(
    const struct micros_bootstrap_runtime_config *config
)
{
    size_t index;

    if (
        config->policies == NULL
        || config->policy_count != config->manifest->header.entry_count
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    for (index = 0; index < config->policy_count; ++index) {
        const struct micros_bootstrap_scheduler_policy *policy =
            &config->policies[index];
        size_t other;

        if (
            policy->service_id == 0
            || policy->priority >= MICROS_SCHEDULER_PRIORITY_COUNT
            || policy->reserved != 0
            || policy->quantum_counter_ticks == 0
        ) {
            return MICROS_BOOTSTRAP_ERROR_RANGE;
        }
        for (other = 0; other < index; ++other) {
            if (
                config->policies[other].service_id
                    == policy->service_id
            ) {
                return MICROS_BOOTSTRAP_ERROR_IDENTITY;
            }
        }
        {
            bool found = false;
            size_t expected_index;

            for (
                expected_index = 0;
                expected_index < config->expected_service_count;
                ++expected_index
            ) {
                if (
                    config->expected_services[expected_index].service_id
                        == policy->service_id
                ) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return MICROS_BOOTSTRAP_ERROR_IDENTITY;
            }
        }
    }
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error bootstrap_preparation_validate_configuration(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config,
    const struct micros_frame_allocator *allocator
)
{
    enum micros_bootstrap_error error =
        micros_bootstrap_image_catalog_validate(
            config->images,
            config->image_count,
            runtime->image_infos,
            MICROS_BOOTSTRAP_SERVICE_CAPACITY
        );

    if (error != MICROS_BOOTSTRAP_OK) {
        bootstrap_runtime_set_preparation_diagnostic(
            runtime,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_IMAGE,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
        return error;
    }
    error = validate_policies(config);
    if (error != MICROS_BOOTSTRAP_OK) {
        bootstrap_runtime_set_preparation_diagnostic(
            runtime,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
        return error;
    }
    return micros_bootstrap_manifest_validate_detailed(
        config->manifest,
        config->expected_services,
        config->expected_service_count,
        runtime->image_infos,
        config->image_count,
        config->profiles,
        config->profile_count,
        allocator->free_frame_count,
        &runtime->validation_plan,
        &runtime->preparation_diagnostic
    );
}

enum micros_bootstrap_error bootstrap_preparation_initialize_mechanisms(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config,
    const struct micros_frame_allocator *allocator
)
{
    if (
        micros_ipc_runtime_initialize(
            config->profiles,
            config->profile_count
        ) != MICROS_ENDPOINT_OK
        || micros_grant_runtime_initialize() != MICROS_GRANT_OK
    ) {
        bootstrap_runtime_set_preparation_diagnostic(
            runtime,
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    {
        const struct micros_kernel_objects *objects =
            micros_kernel_object_runtime_registry();
        const struct micros_frame_ownership *ownership =
            micros_frame_ownership_runtime_ledger();

        if (objects == NULL || ownership == NULL) {
            bootstrap_runtime_set_preparation_diagnostic(
                runtime,
                MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
                0,
                MICROS_ENDPOINT_NONE,
                0
            );
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
        runtime->preparation_baseline.live_process_count =
            objects->live_process_count;
        runtime->preparation_baseline.live_thread_count =
            objects->live_thread_count;
        runtime->preparation_baseline.free_frame_count =
            allocator->free_frame_count;
        runtime->preparation_baseline.owned_frame_count =
            ownership->owned_frame_count;
        runtime->preparation_baseline.valid = true;
    }
    return MICROS_BOOTSTRAP_OK;
}

static bool prepare_threads(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config
)
{
    size_t index;

    for (
        index = 0;
        index < config->manifest->header.entry_count;
        ++index
    ) {
        const struct micros_bootstrap_scheduler_policy *policy =
            find_policy(
                config,
                config->manifest->entries[index].service_id
            );
        struct micros_user_context context;

        bootstrap_runtime_clear_bytes(&context, sizeof(context));
        if (
            policy == NULL
            || micros_thread_create(
                micros_kernel_object_runtime_authoritative_registry(),
                runtime->prepared_bindings[index].process,
                &runtime->preparation_records[index].thread
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        runtime->preparation_records[index].thread_created = true;
        runtime->prepared_bindings[index].thread =
            runtime->preparation_records[index].thread;
        runtime->prepared_bindings[index].scheduler_priority =
            policy->priority;
        runtime->prepared_bindings[index].scheduler_preemptible =
            policy->preemptible;
        runtime->prepared_bindings[index].scheduler_quantum_counter_ticks =
            policy->quantum_counter_ticks;
        context.sp = MICROS_USER_VIRTUAL_END;
        context.sepc = runtime->preparation_records[index].image->entry;
        if (
            micros_user_execution_prepare(
                runtime->prepared_bindings[index].thread,
                &context
            ) != MICROS_USER_EXECUTION_OK
        ) {
            return false;
        }
        runtime->preparation_records[index].context_prepared = true;
    }
    __asm__ volatile("fence.i" : : : "memory");
    return true;
}

static bool prepare_record_resources(
    struct preparation_record *record,
    const struct micros_bootstrap_manifest *manifest,
    struct micros_kernel_objects *objects,
    struct micros_endpoint_registry *registry
)
{
    if (
        record->image == NULL
        || micros_process_create_at(
            objects,
            record->entry->process_slot,
            &record->process
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    record->process_created = true;
    if (
        micros_user_address_space_create(record->process)
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }
    record->root_created = true;
    if (
        !bootstrap_preparation_allocate_image(record)
        || !bootstrap_preparation_verify_image(record)
        || !bootstrap_preparation_allocate_stack(record)
        || !bootstrap_preparation_verify_stack(record)
        || !bootstrap_preparation_allocate_manifest_view(record, manifest)
        || micros_endpoint_reserve(
            registry,
            objects,
            record->process,
            &record->endpoint
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    record->endpoint_reserved = true;
    return true;
}

static enum micros_bootstrap_error prepare_service_record(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config,
    struct micros_kernel_objects *objects,
    struct micros_endpoint_registry *registry,
    uint16_t manifest_index
)
{
    const struct micros_bootstrap_manifest_entry *entry =
        &config->manifest->entries[manifest_index];
    struct preparation_record *record =
        &runtime->preparation_records[manifest_index];
    struct micros_bootstrap_binding *binding =
        &runtime->prepared_bindings[manifest_index];
    const struct micros_process *process;

    record->entry = entry;
    record->image = find_image(config, entry->image_id);
    if (
        !prepare_record_resources(
            record,
            config->manifest,
            objects,
            registry
        )
    ) {
        bootstrap_runtime_set_preparation_diagnostic(
            runtime,
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            entry->service_id,
            MICROS_ENDPOINT_NONE,
            0
        );
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (
        micros_process_resolve(
            objects,
            record->process,
            &process
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        bootstrap_runtime_set_preparation_diagnostic(
            runtime,
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            entry->service_id,
            record->endpoint,
            0
        );
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    *binding = (struct micros_bootstrap_binding){
        .manifest_index = manifest_index,
        .service_id = entry->service_id,
        .process = record->process,
        .root = process->address_space_root,
        .endpoint = record->endpoint,
        .prepared_page_count =
            record->image_pages_allocated
            + record->stack_pages_allocated
            + (record->manifest_view_allocated ? 1U : 0U),
    };
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_prepared_services(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config,
    const struct micros_kernel_objects *objects
)
{
    size_t index;

    for (index = 0; index < config->manifest->header.entry_count; ++index) {
        if (
            micros_user_address_space_validate(
                runtime->prepared_bindings[index].process
            ) != MICROS_USER_ADDRESS_SPACE_OK
        ) {
            bootstrap_runtime_set_preparation_diagnostic(
                runtime,
                MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
                runtime->prepared_bindings[index].service_id,
                runtime->prepared_bindings[index].endpoint,
                0
            );
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
    }
    if (
        micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        bootstrap_runtime_set_preparation_diagnostic(
            runtime,
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error bootstrap_preparation_prepare_services(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    struct micros_endpoint_registry *registry =
        micros_ipc_runtime_authoritative_registry();
    enum micros_bootstrap_error error;
    size_t order_index;

    if (objects == NULL || registry == NULL) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    for (
        order_index = 0;
        order_index < runtime->validation_plan.entry_count;
        ++order_index
    ) {
        runtime->preparation_order[order_index] =
            runtime->validation_plan.ordered_manifest_indices[order_index];
    }
    for (
        order_index = 0;
        order_index < runtime->validation_plan.entry_count;
        ++order_index
    ) {
        uint16_t manifest_index =
            runtime->validation_plan.ordered_manifest_indices[order_index];

        error = prepare_service_record(
            runtime,
            config,
            objects,
            registry,
            manifest_index
        );
        if (error != MICROS_BOOTSTRAP_OK) {
            bootstrap_preparation_rollback_preparation(
                runtime,
                config->manifest->header.entry_count
            );
            return error;
        }
    }
    if (
        !bootstrap_preparation_patch_configurations(
            config->manifest,
            runtime->prepared_bindings,
            runtime->preparation_records
        )
        || !prepare_threads(runtime, config)
    ) {
        bootstrap_runtime_set_preparation_diagnostic(
            runtime,
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
        bootstrap_preparation_rollback_preparation(
            runtime,
            config->manifest->header.entry_count
        );
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    error = validate_prepared_services(
        runtime,
        config,
        objects
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        bootstrap_preparation_rollback_preparation(
            runtime,
            config->manifest->header.entry_count
        );
    }
    return error;
}

static enum micros_bootstrap_error prepare_vm_snapshot(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config
)
{
    struct micros_vm_snapshot_result candidate;
    const struct micros_bootstrap_binding *vm_binding;
    const struct micros_bootstrap_image *vm_image;
    enum micros_bootstrap_error error;

    if (runtime->validation_plan.vm_service_id == 0) {
        return MICROS_BOOTSTRAP_OK;
    }
    bootstrap_runtime_clear_bytes(&candidate, sizeof(candidate));
    error = micros_vm_snapshot_prepare(
        config->manifest,
        runtime->prepared_bindings,
        config->manifest->header.entry_count,
        &candidate
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    vm_binding = &runtime->prepared_bindings[candidate.vm_binding_index];
    vm_image = runtime->preparation_records[
        candidate.vm_binding_index
    ].image;
    if (
        vm_image == NULL
        || vm_binding->service_id != runtime->validation_plan.vm_service_id
        || vm_image->vm_boot_info_address == 0
        || vm_image->vm_boot_info_size != MICROS_VM_BOOT_INFO_SIZE
        || !bootstrap_preparation_write_user_range(
            vm_binding->process,
            vm_image->vm_boot_info_address,
            candidate.info,
            sizeof(*candidate.info)
        )
        || !bootstrap_preparation_user_range_matches(
            vm_binding->process,
            vm_image->vm_boot_info_address,
            candidate.info,
            sizeof(*candidate.info)
        )
        || micros_vm_handoff_runtime_prepare(
            vm_binding,
            &config->manifest->entries[
                vm_binding->manifest_index
            ],
            vm_image,
            &candidate
        ) != MICROS_VM_HANDOFF_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    runtime->prepared_vm_snapshot = candidate;
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error prepare_pm_control(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config
)
{
    size_t index;

    if (runtime->validation_plan.pm_service_id == 0) {
        return MICROS_BOOTSTRAP_OK;
    }
    for (index = 0; index < config->manifest->header.entry_count; ++index) {
        const struct micros_bootstrap_binding *binding =
            &runtime->prepared_bindings[index];

        if (binding->service_id != runtime->validation_plan.pm_service_id) {
            continue;
        }
        return micros_pm_control_runtime_prepare(
            binding,
            &config->manifest->entries[binding->manifest_index]
        ) == MICROS_PM_CONTROL_OK
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    return MICROS_BOOTSTRAP_ERROR_INVARIANT;
}

static enum micros_bootstrap_error prepare_tty_handoff(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config
)
{
    size_t index;

    if (runtime->validation_plan.console_service_id == 0) {
        return MICROS_BOOTSTRAP_OK;
    }
    for (index = 0; index < config->manifest->header.entry_count; ++index) {
        const struct micros_bootstrap_binding *binding =
            &runtime->prepared_bindings[index];

        if (
            binding->service_id
                != runtime->validation_plan.console_service_id
        ) {
            continue;
        }
        return micros_tty_handoff_runtime_prepare(
            binding,
            &config->manifest->entries[binding->manifest_index]
        ) == MICROS_TTY_HANDOFF_OK
            ? MICROS_BOOTSTRAP_OK
            : MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    return MICROS_BOOTSTRAP_ERROR_INVARIANT;
}

enum micros_bootstrap_error bootstrap_preparation_prepare_roles(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config
)
{
    enum micros_bootstrap_error error = prepare_vm_snapshot(runtime, config);

    if (error != MICROS_BOOTSTRAP_OK) {
        bootstrap_runtime_set_preparation_diagnostic(
            runtime,
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            runtime->validation_plan.vm_service_id,
            MICROS_ENDPOINT_NONE,
            0
        );
        bootstrap_preparation_rollback_preparation(
            runtime,
            config->manifest->header.entry_count
        );
        return error;
    }
    error = prepare_pm_control(runtime, config);
    if (error != MICROS_BOOTSTRAP_OK) {
        bootstrap_runtime_set_preparation_diagnostic(
            runtime,
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            runtime->validation_plan.pm_service_id,
            MICROS_ENDPOINT_NONE,
            0
        );
        bootstrap_preparation_rollback_preparation(
            runtime,
            config->manifest->header.entry_count
        );
        return error;
    }
    error = prepare_tty_handoff(runtime, config);
    if (error != MICROS_BOOTSTRAP_OK) {
        bootstrap_runtime_set_preparation_diagnostic(
            runtime,
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            runtime->validation_plan.console_service_id,
            MICROS_ENDPOINT_NONE,
            0
        );
        bootstrap_preparation_rollback_preparation(
            runtime,
            config->manifest->header.entry_count
        );
    }
    return error;
}

enum micros_bootstrap_error bootstrap_preparation_prepare_control_state(
    struct bootstrap_runtime_context *runtime,
    const struct micros_bootstrap_runtime_config *config
)
{
    enum micros_bootstrap_error error;

    if (
        micros_scheduler_initialize(
            config->scheduler_preemption_interval
        ) != MICROS_SCHEDULER_OK
    ) {
        bootstrap_runtime_set_preparation_diagnostic(
            runtime,
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
        bootstrap_preparation_rollback_preparation(
            runtime,
            config->manifest->header.entry_count
        );
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    error = micros_bootstrap_control_state_prepare(
        &runtime->bootstrap_state,
        config->manifest,
        &runtime->validation_plan,
        runtime->prepared_bindings,
        config->manifest->header.entry_count
    );
    if (
        error == MICROS_BOOTSTRAP_OK
        && runtime->validation_plan.console_service_id != 0
        && micros_tty_handoff_runtime_bind_bootstrap(
            &runtime->bootstrap_state
        ) != MICROS_TTY_HANDOFF_OK
    ) {
        bootstrap_runtime_clear_bytes(
            &runtime->bootstrap_state,
            sizeof(runtime->bootstrap_state)
        );
        error = MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    if (error != MICROS_BOOTSTRAP_OK) {
        bootstrap_runtime_set_preparation_diagnostic(
            runtime,
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
        bootstrap_preparation_rollback_preparation(
            runtime,
            config->manifest->header.entry_count
        );
    }
    return error;
}
