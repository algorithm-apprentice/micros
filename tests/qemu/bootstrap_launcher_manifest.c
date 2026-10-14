#include "tests/qemu/bootstrap_launcher_internal.h"

struct manifest_validation {
    uint64_t active_ids;
    uint64_t total_pages;
    uint32_t controller_id;
    uint32_t vm_count;
    uint32_t console_count;
};

static bool validate_header(const struct micros_bootstrap_manifest *manifest)
{
    return !(
        manifest->header.magic != MICROS_BOOTSTRAP_MANIFEST_MAGIC
        || manifest->header.version != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || manifest->header.header_size
            != MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE
        || manifest->header.entry_size
            != MICROS_BOOTSTRAP_MANIFEST_ENTRY_SIZE
        || manifest->header.entry_capacity
            != MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || manifest->header.entry_count == 0
        || manifest->header.entry_count
            > MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || manifest->header.flags != 0
        || manifest->header.manifest_size
            != MICROS_BOOTSTRAP_MANIFEST_SIZE
        || manifest->header.reserved0 != 0
        || manifest->header.reserved1 != 0
        || !bootstrap_launcher_bytes_are_zero(
            manifest->header.reserved2,
            sizeof(manifest->header.reserved2)
        )
        || !bootstrap_launcher_bytes_are_zero(
            &manifest->entries[manifest->header.entry_count],
            (
                MICROS_BOOTSTRAP_SERVICE_CAPACITY
                - manifest->header.entry_count
            ) * sizeof(manifest->entries[0])
        )
    );
}

static bool validate_entry_shape(
    const struct micros_bootstrap_manifest_entry *entry
)
{
    return !(
        entry->service_id == 0
        || entry->service_id > 63
        || entry->profile_id == 0
        || entry->stack_page_count == 0
        || entry->user_page_limit == 0
        || !bootstrap_launcher_name_is_canonical(entry->service_name)
        || !bootstrap_launcher_name_is_canonical(entry->profile_name)
        || !bootstrap_launcher_bytes_are_zero(
            entry->reserved0,
            sizeof(entry->reserved0)
        )
        || entry->reserved1 != 0
        || !bootstrap_launcher_bytes_are_zero(
            entry->reserved2,
            sizeof(entry->reserved2)
        )
        || (
            entry->role_flags
            & ~MICROS_BOOTSTRAP_ROLE_DEFINED_MASK
        ) != 0
        || (
            entry->prerequisites
            & (
                UINT64_C(1)
                << (entry->service_id - 1)
            )
        ) != 0
    );
}

static bool validate_entry_roles(
    const struct micros_bootstrap_manifest_entry *entry,
    struct manifest_validation *validation
)
{
    uint32_t role_count = 0;

    role_count += (
        entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONTROLLER
    ) != 0;
    validation->vm_count += (
        entry->role_flags & MICROS_BOOTSTRAP_ROLE_VM
    ) != 0;
    validation->console_count += (
        entry->role_flags
        & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
    ) != 0;
    role_count += (
        entry->role_flags & MICROS_BOOTSTRAP_ROLE_VM
    ) != 0;
    role_count += (
        entry->role_flags
        & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
    ) != 0;
    if (role_count > 1) {
        return false;
    }
    if (
        (
            entry->role_flags
            & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
        ) != 0
    ) {
        if (
            entry->device_base != MICROS_BOOTSTRAP_UART_BASE
            || entry->device_length
                != MICROS_BOOTSTRAP_UART_LENGTH
            || entry->irq_source != MICROS_BOOTSTRAP_UART_IRQ
        ) {
            return false;
        }
    } else if (
        entry->device_base != 0
        || entry->device_length != 0
        || entry->irq_source != 0
    ) {
        return false;
    }
    if (
        (
            entry->role_flags
            & MICROS_BOOTSTRAP_ROLE_CONTROLLER
        ) != 0
    ) {
        if (
            validation->controller_id != 0
            || entry->prerequisites != 0
            || entry->ready_timeout_counter_ticks != 0
            || entry->stack_page_count != 1
        ) {
            return false;
        }
        validation->controller_id = entry->service_id;
    } else if (
        entry->prerequisites == 0
        || entry->ready_timeout_counter_ticks == 0
    ) {
        return false;
    }
    return true;
}

static bool validate_entry_identity_and_budget(
    const struct micros_bootstrap_manifest *manifest,
    size_t index,
    struct manifest_validation *validation
)
{
    const struct micros_bootstrap_manifest_entry *entry =
        &manifest->entries[index];
    size_t other;

    for (other = 0; other < index; ++other) {
        if (
            manifest->entries[other].service_id
                == entry->service_id
            || manifest->entries[other].process_slot
                == entry->process_slot
            || manifest->entries[other].image_id
                == entry->image_id
            || bootstrap_launcher_names_equal(
                manifest->entries[other].service_name,
                entry->service_name
            )
            || bootstrap_launcher_names_equal(
                manifest->entries[other].profile_name,
                entry->profile_name
            )
        ) {
            return false;
        }
    }
    if (UINT64_MAX - validation->total_pages < entry->user_page_limit) {
        return false;
    }
    validation->total_pages += entry->user_page_limit;
    validation->active_ids |= UINT64_C(1) << (entry->service_id - 1);
    return true;
}

static bool order_services(
    const struct micros_bootstrap_manifest *manifest,
    const struct manifest_validation *validation,
    uint32_t order[MICROS_BOOTSTRAP_SERVICE_CAPACITY]
)
{
    uint64_t selected_ids = 0;
    size_t index;

    for (index = 0; index < manifest->header.entry_count; ++index) {
        uint32_t best_id = UINT32_MAX;
        size_t candidate_index;

        for (
            candidate_index = 0;
            candidate_index < manifest->header.entry_count;
            ++candidate_index
        ) {
            const struct micros_bootstrap_manifest_entry *entry =
                &manifest->entries[candidate_index];
            uint64_t bit = UINT64_C(1) << (entry->service_id - 1);

            if (
                (selected_ids & bit) == 0
                && (entry->prerequisites & ~selected_ids) == 0
                && entry->service_id < best_id
            ) {
                best_id = entry->service_id;
            }
        }
        if (best_id == UINT32_MAX) {
            return false;
        }
        order[index] = best_id;
        selected_ids |= UINT64_C(1) << (best_id - 1);
    }
    return (
        order[0] == validation->controller_id
        && selected_ids == validation->active_ids
    );
}

bool bootstrap_launcher_validate_manifest(
    const struct micros_bootstrap_manifest *manifest,
    uint32_t order[MICROS_BOOTSTRAP_SERVICE_CAPACITY]
)
{
    struct manifest_validation validation = {0};
    size_t index;

    if (!validate_header(manifest)) {
        return false;
    }
    for (index = 0; index < manifest->header.entry_count; ++index) {
        const struct micros_bootstrap_manifest_entry *entry =
            &manifest->entries[index];

        if (
            !validate_entry_shape(entry)
            || !validate_entry_roles(entry, &validation)
            || !validate_entry_identity_and_budget(
                manifest,
                index,
                &validation
            )
        ) {
            return false;
        }
    }
    if (
        validation.controller_id
            != MICROS_BOOTSTRAP_TEST_LAUNCHER_SERVICE_ID
        || validation.vm_count > 1
        || validation.console_count > 1
        || validation.total_pages != manifest->header.total_user_page_limit
    ) {
        return false;
    }
    return order_services(manifest, &validation, order);
}

bool bootstrap_launcher_validate_configuration(
    const struct micros_bootstrap_manifest *manifest
)
{
    size_t index;

    if (
        micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.service_id
            != MICROS_BOOTSTRAP_TEST_LAUNCHER_SERVICE_ID
        || micros_bootstrap_service_config.self_endpoint
            == MICROS_ENDPOINT_NONE
        || micros_bootstrap_service_config.launcher_endpoint
            != micros_bootstrap_service_config.self_endpoint
        || micros_bootstrap_service_config.service_count
            != manifest->header.entry_count
        || micros_bootstrap_service_config.manifest_view_address
            != MICROS_BOOTSTRAP_MANIFEST_VIEW
        || !bootstrap_launcher_bytes_are_zero(
            (const void *)micros_bootstrap_service_config.reserved,
            sizeof(micros_bootstrap_service_config.reserved)
        )
    ) {
        return false;
    }
    for (
        index = 0;
        index < micros_bootstrap_service_config.service_count;
        ++index
    ) {
        const struct micros_bootstrap_service_endpoint *service =
            (const struct micros_bootstrap_service_endpoint *)
                &micros_bootstrap_service_config.services[index];

        if (
            service->service_id == 0
            || (
                index != 0
                && service->service_id
                    <= micros_bootstrap_service_config.services[
                        index - 1
                    ].service_id
            )
            || service->endpoint == MICROS_ENDPOINT_NONE
            || bootstrap_launcher_find_manifest_service(
                manifest,
                service->service_id
            ) < 0
            || (
                service->service_id
                    == MICROS_BOOTSTRAP_TEST_LAUNCHER_SERVICE_ID
                && (
                    service->endpoint
                        != micros_bootstrap_service_config.self_endpoint
                    || service->endpoint
                        != micros_bootstrap_service_config.launcher_endpoint
                )
            )
        ) {
            return false;
        }
    }
    return bootstrap_launcher_bytes_are_zero(
        (const void *)
            &micros_bootstrap_service_config.services[
                micros_bootstrap_service_config.service_count
            ],
        (
            MICROS_BOOTSTRAP_SERVICE_CAPACITY
            - micros_bootstrap_service_config.service_count
        ) * sizeof(micros_bootstrap_service_config.services[0])
    );
}
