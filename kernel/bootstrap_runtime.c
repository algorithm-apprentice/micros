#include "kernel/bootstrap_runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "kernel/endpoint_internal.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/kernel_object_runtime_internal.h"
#include "kernel/scheduler_core_internal.h"
#include "kernel/vm_handoff_runtime.h"
#include "kernel/vm_snapshot.h"
#include "micros/bootstrap_memory.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler.h"
#include "micros/scheduler_core.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"
#include "arch/riscv64/platform.h"

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

static struct micros_bootstrap_control_state bootstrap_state;
static struct micros_bootstrap_manifest_plan validation_plan;
static struct micros_bootstrap_image_info
    image_infos[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
static struct micros_bootstrap_binding
    prepared_bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
static struct preparation_record
    preparation_records[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
static uint16_t
    preparation_order[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
static struct preparation_baseline preparation_baseline;
static struct micros_bootstrap_diagnostic preparation_diagnostic;
static struct micros_vm_snapshot_result prepared_vm_snapshot;
static bool failure_record_emitted;

static void set_preparation_diagnostic(
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t detail
)
{
    preparation_diagnostic.reason = reason;
    preparation_diagnostic.service_id = service_id;
    preparation_diagnostic.endpoint = endpoint;
    preparation_diagnostic.detail = detail;
}

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static bool bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

static _Noreturn void panic_runtime(const char *reason)
{
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();

    MICROS_PANIC(hart == NULL ? 0 : hart->hardware_id, reason);
}

static const char *diagnostic_reason_name(
    enum micros_bootstrap_diagnostic_reason reason
)
{
    switch (reason) {
    case MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_HEADER:
        return "manifest-header";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY:
        return "manifest-entry";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_PROFILE:
        return "manifest-profile";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_IMAGE:
        return "manifest-image";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_CYCLE:
        return "manifest-cycle";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE:
        return "prepare";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_ORDER:
        return "release-order";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION:
        return "release-transition";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_READY_MALFORMED:
        return "ready-malformed";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_READY_FOREIGN:
        return "ready-foreign";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_READY_EARLY:
        return "ready-early";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_READY_DUPLICATE:
        return "ready-duplicate";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE:
        return "ready-role-gate";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_READY_TIMEOUT:
        return "ready-timeout";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT:
        return "service-fault";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY:
        return "authority";
    case MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION:
        return "completion";
    }
    return "authority";
}

static const char *diagnostic_phase_name(
    enum micros_bootstrap_phase phase
)
{
    switch (phase) {
    case MICROS_BOOTSTRAP_PHASE_UNINITIALIZED:
        return "uninitialized";
    case MICROS_BOOTSTRAP_PHASE_PREPARING:
        return "preparing";
    case MICROS_BOOTSTRAP_PHASE_RUNNING:
        return "running";
    case MICROS_BOOTSTRAP_PHASE_SEALED:
        return "sealed";
    case MICROS_BOOTSTRAP_PHASE_FAILED:
        return "failed";
    }
    return "failed";
}

static const char *diagnostic_state_name(uint32_t service_id)
{
    const struct micros_bootstrap_binding *binding;
    size_t index;

    if (service_id == 0) {
        return "none";
    }
    binding = micros_bootstrap_control_find_binding(
        &bootstrap_state,
        service_id
    );
    if (binding == NULL) {
        return "none";
    }
    if (
        bootstrap_state.plan.controller_service_id == service_id
    ) {
        return "controller";
    }
    for (
        index = 0;
        index < bootstrap_state.transitions.entry_count;
        ++index
    ) {
        const struct micros_bootstrap_runtime_entry *entry =
            &bootstrap_state.transitions.entries[index];

        if (entry->service_id != service_id) {
            continue;
        }
        switch (entry->state) {
        case MICROS_BOOTSTRAP_SERVICE_PREPARED:
            return "prepared";
        case MICROS_BOOTSTRAP_SERVICE_STARTING:
            return "starting";
        case MICROS_BOOTSTRAP_SERVICE_READY:
            return "ready";
        }
    }
    return "none";
}

_Noreturn void micros_bootstrap_runtime_fail(
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t detail
)
{
    const char *state_name = diagnostic_state_name(service_id);

    if (!failure_record_emitted) {
        bootstrap_state.phase = MICROS_BOOTSTRAP_PHASE_FAILED;
        bootstrap_state.transitions.phase =
            MICROS_BOOTSTRAP_PHASE_FAILED;
        uart_write("MICROS_BOOTSTRAP_FAILURE reason=");
        uart_write(diagnostic_reason_name(reason));
        uart_write(" service=");
        uart_write_hex64(service_id);
        uart_write(" endpoint=");
        uart_write_hex64(endpoint);
        uart_write(" phase=");
        uart_write(diagnostic_phase_name(bootstrap_state.phase));
        uart_write(" state=");
        uart_write(state_name);
        uart_write(" detail=");
        uart_write_hex64(detail);
        uart_write("\n");
        uart_flush();
        failure_record_emitted = true;
    }
    panic_runtime("bootstrap-failure");
}

void micros_bootstrap_runtime_check_deadline(uint64_t now)
{
    uint32_t service_id;
    const struct micros_bootstrap_binding *binding;
    size_t index;

    if (
        bootstrap_state.phase != MICROS_BOOTSTRAP_PHASE_RUNNING
        || bootstrap_state.transitions.starting_service_id == 0
    ) {
        return;
    }
    service_id = bootstrap_state.transitions.starting_service_id;
    for (
        index = 0;
        index < bootstrap_state.transitions.entry_count;
        ++index
    ) {
        const struct micros_bootstrap_runtime_entry *entry =
            &bootstrap_state.transitions.entries[index];

        if (
            entry->service_id == service_id
            && entry->state == MICROS_BOOTSTRAP_SERVICE_STARTING
            && entry->ready_deadline != 0
            && now >= entry->ready_deadline
        ) {
            binding = micros_bootstrap_control_find_binding(
                &bootstrap_state,
                service_id
            );
            micros_bootstrap_runtime_fail(
                MICROS_BOOTSTRAP_DIAGNOSTIC_READY_TIMEOUT,
                service_id,
                binding == NULL
                    ? MICROS_ENDPOINT_NONE
                    : binding->endpoint,
                entry->ready_deadline
            );
        }
    }
}

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

static uint32_t segment_permissions(uint32_t flags)
{
    uint32_t permissions = 0;

    if ((flags & MICROS_BOOTSTRAP_IMAGE_READ) != 0) {
        permissions |= MICROS_SV39_PERMISSION_READ;
    }
    if ((flags & MICROS_BOOTSTRAP_IMAGE_WRITE) != 0) {
        permissions |= MICROS_SV39_PERMISSION_WRITE;
    }
    if ((flags & MICROS_BOOTSTRAP_IMAGE_EXECUTE) != 0) {
        permissions |= MICROS_SV39_PERMISSION_EXECUTE;
    }
    return permissions;
}

static bool allocate_image(
    struct preparation_record *record
)
{
    size_t segment_index;

    for (
        segment_index = 0;
        segment_index < record->image->segment_count;
        ++segment_index
    ) {
        const struct micros_bootstrap_image_segment *segment =
            &record->image->segments[segment_index];
        uint32_t permissions = segment_permissions(segment->flags);
        uint64_t offset;

        for (
            offset = 0;
            offset < segment->memory_size;
            offset += MICROS_SV39_PAGE_SIZE
        ) {
            uint64_t physical_address;
            size_t copy_size = 0;

            if (
                micros_user_address_space_allocate_page(
                    record->process,
                    segment->virtual_address + offset,
                    permissions,
                    &physical_address
                ) != MICROS_USER_ADDRESS_SPACE_OK
            ) {
                return false;
            }
            clear_bytes(
                (void *)(uintptr_t)physical_address,
                MICROS_SV39_PAGE_SIZE
            );
            if (offset < segment->file_size) {
                uint64_t remaining = segment->file_size - offset;

                copy_size = remaining < MICROS_SV39_PAGE_SIZE
                    ? (size_t)remaining
                    : MICROS_SV39_PAGE_SIZE;
                copy_bytes(
                    (void *)(uintptr_t)physical_address,
                    segment->file_bytes + offset,
                    copy_size
                );
            }
            ++record->image_pages_allocated;
        }
    }
    return record->image_pages_allocated == record->image->page_count;
}

static bool verify_image(const struct preparation_record *record)
{
    size_t segment_index;

    for (
        segment_index = 0;
        segment_index < record->image->segment_count;
        ++segment_index
    ) {
        const struct micros_bootstrap_image_segment *segment =
            &record->image->segments[segment_index];
        uint32_t expected_permissions =
            segment_permissions(segment->flags);
        uint64_t offset;

        for (
            offset = 0;
            offset < segment->memory_size;
            offset += MICROS_SV39_PAGE_SIZE
        ) {
            uint64_t physical_address;
            uint32_t permissions;
            const unsigned char *bytes;
            size_t byte_index;

            if (
                micros_user_address_space_lookup(
                    record->process,
                    segment->virtual_address + offset,
                    &physical_address,
                    &permissions
                ) != MICROS_USER_ADDRESS_SPACE_OK
                || permissions != expected_permissions
            ) {
                return false;
            }
            bytes = (const unsigned char *)(uintptr_t)physical_address;
            for (
                byte_index = 0;
                byte_index < MICROS_SV39_PAGE_SIZE;
                ++byte_index
            ) {
                uint64_t file_offset = offset + byte_index;
                unsigned char expected = file_offset
                        < segment->file_size
                    ? segment->file_bytes[file_offset]
                    : 0;

                if (bytes[byte_index] != expected) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool allocate_stack(struct preparation_record *record)
{
    uint16_t index;

    for (index = 0; index < record->entry->stack_page_count; ++index) {
        uint64_t virtual_address = MICROS_USER_VIRTUAL_END
            - ((uint64_t)index + 1) * MICROS_SV39_PAGE_SIZE;
        uint64_t physical_address;

        if (
            micros_user_address_space_allocate_page(
                record->process,
                virtual_address,
                MICROS_SV39_PERMISSION_READ
                    | MICROS_SV39_PERMISSION_WRITE,
                &physical_address
            ) != MICROS_USER_ADDRESS_SPACE_OK
        ) {
            return false;
        }
        clear_bytes(
            (void *)(uintptr_t)physical_address,
            MICROS_SV39_PAGE_SIZE
        );
        ++record->stack_pages_allocated;
    }
    return true;
}

static bool verify_stack(const struct preparation_record *record)
{
    uint16_t index;

    for (index = 0; index < record->stack_pages_allocated; ++index) {
        uint64_t virtual_address = MICROS_USER_VIRTUAL_END
            - ((uint64_t)index + 1) * MICROS_SV39_PAGE_SIZE;
        uint64_t physical_address;
        uint32_t permissions;
        const unsigned char *bytes;
        size_t byte_index;

        if (
            micros_user_address_space_lookup(
                record->process,
                virtual_address,
                &physical_address,
                &permissions
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || permissions
                != (
                    MICROS_SV39_PERMISSION_READ
                    | MICROS_SV39_PERMISSION_WRITE
                )
        ) {
            return false;
        }
        bytes = (const unsigned char *)(uintptr_t)physical_address;
        for (
            byte_index = 0;
            byte_index < MICROS_SV39_PAGE_SIZE;
            ++byte_index
        ) {
            if (bytes[byte_index] != 0) {
                return false;
            }
        }
    }
    return true;
}

static bool allocate_manifest_view(
    struct preparation_record *record,
    const struct micros_bootstrap_manifest *manifest
)
{
    uint64_t physical_address;

    if (
        (
            record->entry->role_flags
            & MICROS_BOOTSTRAP_ROLE_CONTROLLER
        ) == 0
    ) {
        return true;
    }
    if (
        micros_user_address_space_allocate_page(
            record->process,
            MICROS_BOOTSTRAP_MANIFEST_VIEW,
            MICROS_SV39_PERMISSION_READ,
            &physical_address
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }
    clear_bytes(
        (void *)(uintptr_t)physical_address,
        MICROS_SV39_PAGE_SIZE
    );
    copy_bytes(
        (void *)(uintptr_t)physical_address,
        manifest,
        sizeof(*manifest)
    );
    record->manifest_view_allocated = true;
    return bytes_equal(
        (const void *)(uintptr_t)physical_address,
        manifest,
        sizeof(*manifest)
    );
}

static bool write_user_range(
    struct micros_process_handle process,
    uint64_t address,
    const void *source,
    size_t size
)
{
    const unsigned char *bytes = source;
    size_t copied = 0;

    while (copied < size) {
        uint64_t physical;
        uint32_t permissions;
        size_t contiguous;
        size_t chunk;

        if (
            micros_user_address_space_translate(
                process,
                address + copied,
                &physical,
                &permissions,
                &contiguous
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || (
                permissions & MICROS_SV39_PERMISSION_WRITE
            ) == 0
            || contiguous == 0
        ) {
            return false;
        }
        chunk = size - copied < contiguous
            ? size - copied
            : contiguous;
        copy_bytes(
            (void *)(uintptr_t)physical,
            bytes + copied,
            chunk
        );
        copied += chunk;
    }
    return true;
}

static bool read_user_range(
    struct micros_process_handle process,
    uint64_t address,
    void *destination,
    size_t size
)
{
    unsigned char *bytes = destination;
    size_t copied = 0;

    while (copied < size) {
        uint64_t physical;
        uint32_t permissions;
        size_t contiguous;
        size_t chunk;

        if (
            micros_user_address_space_translate(
                process,
                address + copied,
                &physical,
                &permissions,
                &contiguous
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || (
                permissions & MICROS_SV39_PERMISSION_READ
            ) == 0
            || contiguous == 0
        ) {
            return false;
        }
        chunk = size - copied < contiguous
            ? size - copied
            : contiguous;
        copy_bytes(
            bytes + copied,
            (const void *)(uintptr_t)physical,
            chunk
        );
        copied += chunk;
    }
    return true;
}

static bool user_range_matches(
    struct micros_process_handle process,
    uint64_t address,
    const void *expected,
    size_t size
)
{
    const unsigned char *bytes = expected;
    size_t compared = 0;

    while (compared < size) {
        uint64_t physical;
        uint32_t permissions;
        size_t contiguous;
        size_t chunk;

        if (
            micros_user_address_space_translate(
                process,
                address + compared,
                &physical,
                &permissions,
                &contiguous
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || (
                permissions & MICROS_SV39_PERMISSION_READ
            ) == 0
            || contiguous == 0
        ) {
            return false;
        }
        chunk = size - compared < contiguous
            ? size - compared
            : contiguous;
        if (
            !bytes_equal(
                (const void *)(uintptr_t)physical,
                bytes + compared,
                chunk
            )
        ) {
            return false;
        }
        compared += chunk;
    }
    return true;
}

static bool build_service_table(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_binding *bindings,
    struct micros_bootstrap_service_endpoint
        services[MICROS_BOOTSTRAP_SERVICE_CAPACITY]
)
{
    uint64_t selected = 0;
    size_t output_index;

    clear_bytes(
        services,
        sizeof(struct micros_bootstrap_service_endpoint)
            * MICROS_BOOTSTRAP_SERVICE_CAPACITY
    );
    for (
        output_index = 0;
        output_index < manifest->header.entry_count;
        ++output_index
    ) {
        uint32_t best_id = UINT32_MAX;
        size_t best_index = SIZE_MAX;
        size_t index;

        for (index = 0; index < manifest->header.entry_count; ++index) {
            uint32_t service_id = bindings[index].service_id;
            uint64_t bit = UINT64_C(1) << (service_id - 1);

            if (
                (selected & bit) == 0
                && service_id < best_id
            ) {
                best_id = service_id;
                best_index = index;
            }
        }
        if (best_index == SIZE_MAX) {
            return false;
        }
        services[output_index].service_id = best_id;
        services[output_index].endpoint =
            bindings[best_index].endpoint;
        selected |= UINT64_C(1) << (best_id - 1);
    }
    return true;
}

static bool patch_configurations(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_binding *bindings,
    const struct preparation_record *records
)
{
    struct micros_bootstrap_service_endpoint
        services[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    micros_endpoint_t launcher_endpoint = MICROS_ENDPOINT_NONE;
    size_t index;

    if (!build_service_table(manifest, bindings, services)) {
        return false;
    }
    for (index = 0; index < manifest->header.entry_count; ++index) {
        if (
            (
                manifest->entries[index].role_flags
                & MICROS_BOOTSTRAP_ROLE_CONTROLLER
            ) != 0
        ) {
            launcher_endpoint = bindings[index].endpoint;
            break;
        }
    }
    if (launcher_endpoint == MICROS_ENDPOINT_NONE) {
        return false;
    }
    for (index = 0; index < manifest->header.entry_count; ++index) {
        struct micros_bootstrap_service_config config;
        struct micros_bootstrap_service_config verification;

        clear_bytes(&config, sizeof(config));
        config.version = MICROS_BOOTSTRAP_MANIFEST_VERSION;
        config.manifest_version =
            MICROS_BOOTSTRAP_MANIFEST_VERSION;
        config.service_id = bindings[index].service_id;
        config.self_endpoint = bindings[index].endpoint;
        config.launcher_endpoint = launcher_endpoint;
        config.service_count = manifest->header.entry_count;
        copy_bytes(
            config.services,
            services,
            sizeof(config.services)
        );
        config.manifest_view_address = (
            manifest->entries[index].role_flags
            & MICROS_BOOTSTRAP_ROLE_CONTROLLER
        ) != 0
            ? MICROS_BOOTSTRAP_MANIFEST_VIEW
            : 0;
        if (
            !write_user_range(
                bindings[index].process,
                records[index].image->config_address,
                &config,
                sizeof(config)
            )
            || !read_user_range(
                bindings[index].process,
                records[index].image->config_address,
                &verification,
                sizeof(verification)
            )
            || !bytes_equal(
                &config,
                &verification,
                sizeof(config)
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool prepare_threads(
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

        clear_bytes(&context, sizeof(context));
        if (
            policy == NULL
            || micros_thread_create(
                micros_kernel_object_runtime_authoritative_registry(),
                prepared_bindings[index].process,
                &preparation_records[index].thread
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        preparation_records[index].thread_created = true;
        prepared_bindings[index].thread =
            preparation_records[index].thread;
        prepared_bindings[index].scheduler_priority =
            policy->priority;
        prepared_bindings[index].scheduler_preemptible =
            policy->preemptible;
        prepared_bindings[index].scheduler_quantum_counter_ticks =
            policy->quantum_counter_ticks;
        context.sp = MICROS_USER_VIRTUAL_END;
        context.sepc = preparation_records[index].image->entry;
        if (
            micros_user_execution_prepare(
                prepared_bindings[index].thread,
                &context
            ) != MICROS_USER_EXECUTION_OK
        ) {
            return false;
        }
        preparation_records[index].context_prepared = true;
    }
    __asm__ volatile("fence.i" : : : "memory");
    return true;
}

static bool release_page(
    struct micros_process_handle process,
    uint64_t virtual_address
)
{
    uint64_t physical_address;

    return micros_user_address_space_release_page(
        process,
        virtual_address,
        &physical_address
    ) == MICROS_USER_ADDRESS_SPACE_OK;
}

static bool rollback_record(
    struct preparation_record *record,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects
)
{
    uint32_t image_page;
    uint16_t stack_page;

    if (record->context_prepared) {
        if (
            micros_user_execution_detach(record->thread)
                != MICROS_USER_EXECUTION_OK
        ) {
            return false;
        }
        record->context_prepared = false;
    }
    if (record->thread_created) {
        if (
            micros_thread_release(objects, record->thread)
                != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        record->thread_created = false;
    }
    if (record->endpoint_reserved) {
        if (
            micros_endpoint_close(
                registry,
                objects,
                record->endpoint
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
        record->endpoint_reserved = false;
    }
    if (
        record->manifest_view_allocated
        && !release_page(
            record->process,
            MICROS_BOOTSTRAP_MANIFEST_VIEW
        )
    ) {
        return false;
    }
    record->manifest_view_allocated = false;
    for (
        stack_page = record->stack_pages_allocated;
        stack_page > 0;
        --stack_page
    ) {
        uint64_t address = MICROS_USER_VIRTUAL_END
            - (uint64_t)stack_page * MICROS_SV39_PAGE_SIZE;

        if (!release_page(record->process, address)) {
            return false;
        }
    }
    record->stack_pages_allocated = 0;
    for (
        image_page = record->image_pages_allocated;
        image_page > 0;
        --image_page
    ) {
        uint64_t address = MICROS_USER_VIRTUAL_BASE
            + ((uint64_t)image_page - 1)
                * MICROS_SV39_PAGE_SIZE;

        if (!release_page(record->process, address)) {
            return false;
        }
    }
    record->image_pages_allocated = 0;
    if (
        record->root_created
        && micros_user_address_space_destroy(record->process)
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }
    record->root_created = false;
    if (
        record->process_created
        && micros_process_release(objects, record->process)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    record->process_created = false;
    return true;
}

static void rollback_preparation(size_t count)
{
    struct micros_endpoint_registry *registry =
        micros_ipc_runtime_authoritative_registry();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();

    while (count > 0) {
        uint16_t manifest_index;

        --count;
        manifest_index = preparation_order[count];
        if (
            registry == NULL
            || objects == NULL
            || !rollback_record(
                &preparation_records[manifest_index],
                registry,
                objects
            )
        ) {
            panic_runtime("bootstrap-rollback-invariant");
        }
    }
    clear_bytes(preparation_records, sizeof(preparation_records));
    clear_bytes(prepared_bindings, sizeof(prepared_bindings));
    clear_bytes(preparation_order, sizeof(preparation_order));
    clear_bytes(&prepared_vm_snapshot, sizeof(prepared_vm_snapshot));
    if (
        micros_vm_handoff_runtime_reset()
            != MICROS_VM_HANDOFF_OK
    ) {
        panic_runtime("bootstrap-vm-handoff-rollback");
    }
    if (preparation_baseline.valid) {
        const struct micros_frame_allocator *allocator =
            micros_bootstrap_frame_allocator();
        const struct micros_frame_ownership *ownership =
            micros_frame_ownership_runtime_ledger();

        if (
            allocator == NULL
            || ownership == NULL
            || objects->live_process_count
                != preparation_baseline.live_process_count
            || objects->live_thread_count
                != preparation_baseline.live_thread_count
            || allocator->free_frame_count
                != preparation_baseline.free_frame_count
            || ownership->owned_frame_count
                != preparation_baseline.owned_frame_count
            || micros_kernel_objects_validate(objects)
                != MICROS_KERNEL_OBJECT_OK
            || micros_ipc_runtime_validate()
                != MICROS_ENDPOINT_OK
            || micros_frame_ownership_runtime_validate(objects)
                != MICROS_FRAME_OWNERSHIP_OK
        ) {
            panic_runtime("bootstrap-rollback-baseline");
        }
    }
}

static enum micros_bootstrap_error prepare_services(
    const struct micros_bootstrap_runtime_config *config
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    struct micros_endpoint_registry *registry =
        micros_ipc_runtime_authoritative_registry();
    size_t order_index;

    if (objects == NULL || registry == NULL) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    for (
        order_index = 0;
        order_index < validation_plan.entry_count;
        ++order_index
    ) {
        preparation_order[order_index] =
            validation_plan.ordered_manifest_indices[order_index];
    }
    for (
        order_index = 0;
        order_index < validation_plan.entry_count;
        ++order_index
    ) {
        uint16_t manifest_index =
            validation_plan.ordered_manifest_indices[order_index];
        const struct micros_bootstrap_manifest_entry *entry =
            &config->manifest->entries[manifest_index];
        struct preparation_record *record =
            &preparation_records[manifest_index];
        struct micros_bootstrap_binding *binding =
            &prepared_bindings[manifest_index];
        const struct micros_process *process;

        record->entry = entry;
        record->image = find_image(config, entry->image_id);
        if (
            record->image == NULL
            || micros_process_create_at(
                objects,
                entry->process_slot,
                &record->process
            )
                != MICROS_KERNEL_OBJECT_OK
        ) {
            set_preparation_diagnostic(
                MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
                entry->service_id,
                MICROS_ENDPOINT_NONE,
                0
            );
            rollback_preparation(config->manifest->header.entry_count);
            return MICROS_BOOTSTRAP_ERROR_STATE;
        }
        record->process_created = true;
        if (
            micros_user_address_space_create(record->process)
                != MICROS_USER_ADDRESS_SPACE_OK
        ) {
            set_preparation_diagnostic(
                MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
                entry->service_id,
                MICROS_ENDPOINT_NONE,
                0
            );
            rollback_preparation(config->manifest->header.entry_count);
            return MICROS_BOOTSTRAP_ERROR_STATE;
        }
        record->root_created = true;
        if (
            !allocate_image(record)
            || !verify_image(record)
            || !allocate_stack(record)
            || !verify_stack(record)
            || !allocate_manifest_view(record, config->manifest)
            || micros_endpoint_reserve(
                registry,
                objects,
                record->process,
                &record->endpoint
            ) != MICROS_ENDPOINT_OK
        ) {
            set_preparation_diagnostic(
                MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
                entry->service_id,
                MICROS_ENDPOINT_NONE,
                0
            );
            rollback_preparation(config->manifest->header.entry_count);
            return MICROS_BOOTSTRAP_ERROR_STATE;
        }
        record->endpoint_reserved = true;
        if (
            micros_process_resolve(
                objects,
                record->process,
                &process
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            set_preparation_diagnostic(
                MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
                entry->service_id,
                record->endpoint,
                0
            );
            rollback_preparation(config->manifest->header.entry_count);
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
    }
    if (
        !patch_configurations(
            config->manifest,
            prepared_bindings,
            preparation_records
        )
        || !prepare_threads(config)
    ) {
        set_preparation_diagnostic(
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
        rollback_preparation(config->manifest->header.entry_count);
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    for (
        order_index = 0;
        order_index < config->manifest->header.entry_count;
        ++order_index
    ) {
        if (
            micros_user_address_space_validate(
                prepared_bindings[order_index].process
            ) != MICROS_USER_ADDRESS_SPACE_OK
        ) {
            set_preparation_diagnostic(
                MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
                prepared_bindings[order_index].service_id,
                prepared_bindings[order_index].endpoint,
                0
            );
            rollback_preparation(
                config->manifest->header.entry_count
            );
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
    }
    if (
        micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        set_preparation_diagnostic(
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
        rollback_preparation(config->manifest->header.entry_count);
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error prepare_vm_snapshot(
    const struct micros_bootstrap_runtime_config *config
)
{
    struct micros_vm_snapshot_result candidate;
    const struct micros_bootstrap_binding *vm_binding;
    const struct micros_bootstrap_image *vm_image;
    enum micros_bootstrap_error error;

    if (validation_plan.vm_service_id == 0) {
        return MICROS_BOOTSTRAP_OK;
    }
    clear_bytes(&candidate, sizeof(candidate));
    error = micros_vm_snapshot_prepare(
        config->manifest,
        prepared_bindings,
        config->manifest->header.entry_count,
        &candidate
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    vm_binding = &prepared_bindings[candidate.vm_binding_index];
    vm_image = preparation_records[
        candidate.vm_binding_index
    ].image;
    if (
        vm_image == NULL
        || vm_binding->service_id != validation_plan.vm_service_id
        || vm_image->vm_boot_info_address == 0
        || vm_image->vm_boot_info_size != MICROS_VM_BOOT_INFO_SIZE
        || !write_user_range(
            vm_binding->process,
            vm_image->vm_boot_info_address,
            candidate.info,
            sizeof(*candidate.info)
        )
        || !user_range_matches(
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
    prepared_vm_snapshot = candidate;
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error micros_bootstrap_runtime_prepare(
    const struct micros_bootstrap_runtime_config *config
)
{
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
    saved_status = riscv_irq_save();
    clear_bytes(
        &preparation_diagnostic,
        sizeof(preparation_diagnostic)
    );
    if (
        bootstrap_state.phase != MICROS_BOOTSTRAP_PHASE_UNINITIALIZED
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
    clear_bytes(&prepared_vm_snapshot, sizeof(prepared_vm_snapshot));
    if (
        micros_vm_handoff_runtime_reset()
            != MICROS_VM_HANDOFF_OK
    ) {
        error = MICROS_BOOTSTRAP_ERROR_STATE;
        goto done;
    }
    allocator = micros_bootstrap_frame_allocator();
    if (allocator == NULL) {
        error = MICROS_BOOTSTRAP_ERROR_INVARIANT;
        goto done;
    }
    error = micros_bootstrap_image_catalog_validate(
        config->images,
        config->image_count,
        image_infos,
        MICROS_BOOTSTRAP_SERVICE_CAPACITY
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        set_preparation_diagnostic(
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_IMAGE,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
        goto done;
    }
    error = validate_policies(config);
    if (error != MICROS_BOOTSTRAP_OK) {
        set_preparation_diagnostic(
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
        goto done;
    }
    error = micros_bootstrap_manifest_validate_detailed(
        config->manifest,
        config->expected_services,
        config->expected_service_count,
        image_infos,
        config->image_count,
        config->profiles,
        config->profile_count,
        allocator->free_frame_count,
        &validation_plan,
        &preparation_diagnostic
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        goto done;
    }
    if (
        micros_ipc_runtime_initialize(
            config->profiles,
            config->profile_count
        ) != MICROS_ENDPOINT_OK
        || micros_grant_runtime_initialize() != MICROS_GRANT_OK
    ) {
        set_preparation_diagnostic(
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
        error = MICROS_BOOTSTRAP_ERROR_INVARIANT;
        goto done;
    }
    {
        const struct micros_kernel_objects *objects =
            micros_kernel_object_runtime_registry();
        const struct micros_frame_ownership *ownership =
            micros_frame_ownership_runtime_ledger();

        if (objects == NULL || ownership == NULL) {
            set_preparation_diagnostic(
                MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
                0,
                MICROS_ENDPOINT_NONE,
                0
            );
            error = MICROS_BOOTSTRAP_ERROR_INVARIANT;
            goto done;
        }
        preparation_baseline.live_process_count =
            objects->live_process_count;
        preparation_baseline.live_thread_count =
            objects->live_thread_count;
        preparation_baseline.free_frame_count =
            allocator->free_frame_count;
        preparation_baseline.owned_frame_count =
            ownership->owned_frame_count;
        preparation_baseline.valid = true;
    }
    error = prepare_services(config);
    if (error != MICROS_BOOTSTRAP_OK) {
        goto done;
    }
    error = prepare_vm_snapshot(config);
    if (error != MICROS_BOOTSTRAP_OK) {
        set_preparation_diagnostic(
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            validation_plan.vm_service_id,
            MICROS_ENDPOINT_NONE,
            0
        );
        rollback_preparation(config->manifest->header.entry_count);
        goto done;
    }
    if (
        micros_scheduler_initialize(
            config->scheduler_preemption_interval
        ) != MICROS_SCHEDULER_OK
    ) {
        set_preparation_diagnostic(
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
        rollback_preparation(config->manifest->header.entry_count);
        error = MICROS_BOOTSTRAP_ERROR_STATE;
        goto done;
    }
    error = micros_bootstrap_control_state_prepare(
        &bootstrap_state,
        config->manifest,
        &validation_plan,
        prepared_bindings,
        config->manifest->header.entry_count
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        set_preparation_diagnostic(
            MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
            0,
            MICROS_ENDPOINT_NONE,
            0
        );
        rollback_preparation(config->manifest->header.entry_count);
    }

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
    const struct micros_bootstrap_binding *controller;
    const struct micros_bootstrap_manifest_entry *entry;
    struct micros_user_context context;
    struct micros_bootstrap_runtime transition_preflight;
    enum micros_kernel_object_error scheduler_error;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    clear_bytes(
        &transition_preflight,
        sizeof(transition_preflight)
    );
    if (
        objects == NULL
        || registry == NULL
        || bootstrap_state.phase
            != MICROS_BOOTSTRAP_PHASE_PREPARING
    ) {
        riscv_irq_restore(saved_status);
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    controller = micros_bootstrap_control_find_binding(
        &bootstrap_state,
        bootstrap_state.plan.controller_service_id
    );
    if (controller == NULL) {
        riscv_irq_restore(saved_status);
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    entry = &bootstrap_state.manifest.entries[
        controller->manifest_index
    ];
    if (
        micros_bootstrap_control_validate(
            &bootstrap_state,
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
            &bootstrap_state.manifest,
            &bootstrap_state.plan,
            &transition_preflight
        ) != MICROS_BOOTSTRAP_OK
        || micros_scheduler_prepare_start_timer()
            != MICROS_SCHEDULER_OK
    ) {
        rollback_preparation(bootstrap_state.entry_count);
        clear_bytes(&bootstrap_state, sizeof(bootstrap_state));
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
            &bootstrap_state,
            registry,
            objects
        ) != MICROS_BOOTSTRAP_OK
    ) {
        panic_runtime("bootstrap-controller-publication");
    }
    riscv_irq_restore(saved_status);
    return MICROS_BOOTSTRAP_OK;
}

_Noreturn void micros_bootstrap_runtime_launch(
    const struct micros_bootstrap_runtime_config *config
)
{
    uintptr_t saved_status = riscv_irq_save();
    const struct micros_bootstrap_binding *controller;
    uint32_t controller_service_id = 0;
    micros_endpoint_t controller_endpoint = MICROS_ENDPOINT_NONE;
    enum micros_bootstrap_error error =
        micros_bootstrap_runtime_prepare(config);

    if (error != MICROS_BOOTSTRAP_OK) {
        micros_bootstrap_runtime_fail(
            preparation_diagnostic.reason == 0
                ? MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE
                : preparation_diagnostic.reason,
            preparation_diagnostic.service_id,
            preparation_diagnostic.endpoint == 0
                ? MICROS_ENDPOINT_NONE
                : preparation_diagnostic.endpoint,
            preparation_diagnostic.detail
        );
    }
    controller_service_id =
        bootstrap_state.plan.controller_service_id;
    controller = micros_bootstrap_control_find_binding(
        &bootstrap_state,
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
    panic_runtime("bootstrap-scheduler-returned");
}

const struct micros_bootstrap_control_state *
micros_bootstrap_runtime_state(void)
{
    return bootstrap_state.phase == MICROS_BOOTSTRAP_PHASE_UNINITIALIZED
        ? NULL
        : &bootstrap_state;
}

struct micros_bootstrap_control_state *
micros_bootstrap_runtime_authoritative_state(void)
{
    return bootstrap_state.phase == MICROS_BOOTSTRAP_PHASE_UNINITIALIZED
        ? NULL
        : &bootstrap_state;
}

enum micros_bootstrap_error micros_bootstrap_runtime_validate(void)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    struct micros_endpoint_registry *registry =
        micros_ipc_runtime_authoritative_registry();
    size_t index;

    if (
        objects == NULL
        || registry == NULL
        || bootstrap_state.phase
            == MICROS_BOOTSTRAP_PHASE_UNINITIALIZED
        || micros_bootstrap_control_validate(
            &bootstrap_state,
            registry,
            objects
        ) != MICROS_BOOTSTRAP_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    for (index = 0; index < bootstrap_state.entry_count; ++index) {
        struct micros_user_context context;

        if (
            micros_user_address_space_validate(
                bootstrap_state.bindings[index].process
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || micros_user_execution_inspect(
                bootstrap_state.bindings[index].thread,
                &context
            ) != MICROS_USER_EXECUTION_OK
            || micros_user_execution_validate_context(
                bootstrap_state.bindings[index].thread,
                &context
            ) != MICROS_USER_EXECUTION_OK
        ) {
            return MICROS_BOOTSTRAP_ERROR_INVARIANT;
        }
    }
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error
micros_bootstrap_runtime_validate_vm_prepared(void)
{
    struct micros_vm_boot_summary observed_summary;
    const struct micros_vm_handoff_state *handoff =
        micros_vm_handoff_runtime_state();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_authoritative_registry();
    struct micros_endpoint_registry *registry =
        micros_ipc_runtime_authoritative_registry();

    if (
        handoff == NULL
        || handoff->phase != MICROS_VM_HANDOFF_PHASE_PREPARED
        || prepared_vm_snapshot.info == NULL
        || objects == NULL
        || registry == NULL
        || micros_vm_handoff_runtime_validate(
            &bootstrap_state,
            registry,
            objects
        ) != MICROS_VM_HANDOFF_OK
        || micros_vm_boot_validate(
            prepared_vm_snapshot.info,
            &observed_summary
        ) != MICROS_VM_BOOT_OK
        || micros_vm_snapshot_validate_current(
            &bootstrap_state.manifest,
            bootstrap_state.bindings,
            bootstrap_state.entry_count,
            prepared_vm_snapshot.info
        ) != MICROS_BOOTSTRAP_OK
        || !bytes_equal(
            &observed_summary,
            &handoff->summary,
            sizeof(observed_summary)
        )
        || !user_range_matches(
            handoff->process,
            handoff->boot_info_address,
            prepared_vm_snapshot.info,
            handoff->boot_info_size
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    return MICROS_BOOTSTRAP_OK;
}

bool micros_bootstrap_runtime_is_active_controller(
    struct micros_process_handle process
)
{
    return (
        bootstrap_state.phase == MICROS_BOOTSTRAP_PHASE_RUNNING
        && bootstrap_state.controller_process.slot == process.slot
        && bootstrap_state.controller_process.generation
            == process.generation
    );
}
