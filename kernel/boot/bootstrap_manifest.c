#include "kernel/boot/bootstrap_manifest_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/sv39.h"
#include "micros/tty.h"
#include "micros/vm_bootstrap.h"

static const struct micros_bootstrap_expected_service *find_expected(
    const struct micros_bootstrap_expected_service *expected,
    size_t count,
    uint32_t service_id
)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        if (expected[index].service_id == service_id) {
            return &expected[index];
        }
    }
    return NULL;
}

static const struct micros_bootstrap_image_info *find_image(
    const struct micros_bootstrap_image_info *images,
    size_t count,
    uint32_t image_id
)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        if (images[index].image_id == image_id) {
            return &images[index];
        }
    }
    return NULL;
}

static enum micros_bootstrap_error validate_header(
    const struct micros_bootstrap_manifest *manifest
)
{
    const struct micros_bootstrap_manifest_header *header =
        &manifest->header;

    if (
        header->magic != MICROS_BOOTSTRAP_MANIFEST_MAGIC
        || header->version != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || header->header_size
            != MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE
        || header->entry_size
            != MICROS_BOOTSTRAP_MANIFEST_ENTRY_SIZE
        || header->entry_capacity
            != MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || header->manifest_size != MICROS_BOOTSTRAP_MANIFEST_SIZE
        || header->flags != 0
        || header->reserved0 != 0
        || header->reserved1 != 0
        || !bootstrap_manifest_bytes_are_zero(
            header->reserved2,
            sizeof(header->reserved2)
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_SHAPE;
    }
    if (
        header->entry_count == 0
        || header->entry_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        return MICROS_BOOTSTRAP_ERROR_CAPACITY;
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_entry_shape(
    const struct micros_bootstrap_manifest_entry *entry
)
{
    uint32_t role_count = 0;

    if (
        entry->service_id == 0
        || entry->service_id > 63
        || entry->image_id == 0
        || entry->process_slot >= MICROS_PROCESS_CAPACITY
        || entry->stack_page_count == 0
        || entry->profile_id == 0
        || entry->user_page_limit == 0
        || !bootstrap_manifest_bytes_are_zero(
            entry->reserved0,
            sizeof(entry->reserved0)
        )
        || entry->reserved1 != 0
        || !bootstrap_manifest_bytes_are_zero(
            entry->reserved2,
            sizeof(entry->reserved2)
        )
        || !bootstrap_manifest_fixed_name_is_canonical(entry->service_name)
        || !bootstrap_manifest_fixed_name_is_canonical(entry->profile_name)
        || (entry->role_flags & ~MICROS_BOOTSTRAP_ROLE_DEFINED_MASK) != 0
    ) {
        return MICROS_BOOTSTRAP_ERROR_SHAPE;
    }
    role_count += (
        entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONTROLLER
    ) != 0;
    role_count += (entry->role_flags & MICROS_BOOTSTRAP_ROLE_VM) != 0;
    role_count += (
        entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
    ) != 0;
    role_count += (entry->role_flags & MICROS_BOOTSTRAP_ROLE_PM) != 0;
    if (role_count > 1) {
        return MICROS_BOOTSTRAP_ERROR_ROLE;
    }
    if (
        (entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER) != 0
    ) {
        if (
            entry->stack_page_count != 1
            || entry->device_base != MICROS_BOOTSTRAP_UART_BASE
            || entry->device_length != MICROS_BOOTSTRAP_UART_LENGTH
            || entry->irq_source != MICROS_BOOTSTRAP_UART_IRQ
        ) {
            return MICROS_BOOTSTRAP_ERROR_ROLE;
        }
    } else if (
        entry->device_base != 0
        || entry->device_length != 0
        || entry->irq_source != 0
    ) {
        return MICROS_BOOTSTRAP_ERROR_ROLE;
    }
    if (
        (entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONTROLLER) != 0
    ) {
        if (
            entry->prerequisites != 0
            || entry->ready_timeout_counter_ticks != 0
            || entry->stack_page_count != 1
        ) {
            return MICROS_BOOTSTRAP_ERROR_ROLE;
        }
    } else if (
        entry->prerequisites == 0
        || entry->ready_timeout_counter_ticks == 0
    ) {
        return MICROS_BOOTSTRAP_ERROR_TOPOLOGY;
    }
    if (
        entry->prerequisites
        & (UINT64_C(1) << (entry->service_id - 1))
    ) {
        return MICROS_BOOTSTRAP_ERROR_TOPOLOGY;
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_image_bound(
    const struct micros_bootstrap_manifest_entry *entry,
    const struct micros_bootstrap_image_info *image
)
{
    uint64_t stack_bytes;
    uint64_t stack_bottom;
    uint64_t maximum_image_end;
    uint64_t required_pages;

    if (
        image == NULL
        || image->version != MICROS_BOOTSTRAP_IMAGE_VERSION
        || image->image_id != entry->image_id
        || image->entry != MICROS_USER_VIRTUAL_BASE
        || image->config_size
            != sizeof(struct micros_bootstrap_service_config)
        || image->config_address % 8 != 0
        || !image->config_initially_zero
        || image->image_end <= MICROS_USER_VIRTUAL_BASE
        || image->image_end % MICROS_SV39_PAGE_SIZE != 0
        || image->page_count == 0
    ) {
        return MICROS_BOOTSTRAP_ERROR_IMAGE;
    }
    if (
        (entry->role_flags & MICROS_BOOTSTRAP_ROLE_VM) != 0
    ) {
        if (
            image->vm_boot_info_address
                % MICROS_VM_BOOT_INFO_ALIGNMENT
                != 0
            || image->vm_boot_info_size
                != MICROS_VM_BOOT_INFO_SIZE
            || !image->vm_boot_info_initially_zero
        ) {
            return MICROS_BOOTSTRAP_ERROR_IMAGE;
        }
    } else if (
        image->vm_boot_info_address != 0
        || image->vm_boot_info_size != 0
        || image->vm_boot_info_initially_zero
    ) {
        return MICROS_BOOTSTRAP_ERROR_IMAGE;
    }
    stack_bytes =
        (uint64_t)entry->stack_page_count * MICROS_SV39_PAGE_SIZE;
    if (
        stack_bytes / MICROS_SV39_PAGE_SIZE
            != entry->stack_page_count
        || stack_bytes > MICROS_USER_VIRTUAL_END
    ) {
        return MICROS_BOOTSTRAP_ERROR_RANGE;
    }
    stack_bottom = MICROS_USER_VIRTUAL_END - stack_bytes;
    if (
        (entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONTROLLER)
        != 0
    ) {
        maximum_image_end = MICROS_BOOTSTRAP_MANIFEST_VIEW;
    } else if (
        (entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER)
        != 0
    ) {
        maximum_image_end = MICROS_TTY_UART_VIRTUAL_BASE;
    } else {
        maximum_image_end = stack_bottom;
    }
    if (
        image->image_end > maximum_image_end
        || image->config_address < MICROS_USER_VIRTUAL_BASE
        || image->config_address
            > image->image_end - image->config_size
    ) {
        return MICROS_BOOTSTRAP_ERROR_IMAGE;
    }
    required_pages = image->page_count + entry->stack_page_count;
    if (entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONTROLLER) {
        ++required_pages;
    }
    if (
        required_pages > entry->user_page_limit
        || image->page_count
            != (
                image->image_end - MICROS_USER_VIRTUAL_BASE
            ) / MICROS_SV39_PAGE_SIZE
    ) {
        return MICROS_BOOTSTRAP_ERROR_RANGE;
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validation_failure(
    struct micros_bootstrap_diagnostic *diagnostic,
    enum micros_bootstrap_error error,
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    uint64_t detail
)
{
    if (diagnostic != NULL) {
        *diagnostic = (struct micros_bootstrap_diagnostic){
            .reason = reason,
            .service_id = service_id,
            .endpoint = MICROS_ENDPOINT_NONE,
            .detail = detail,
        };
    }
    return error;
}

static enum micros_bootstrap_error validate_arguments(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_expected_service *expected_services,
    const struct micros_bootstrap_image_info *images,
    const struct micros_privilege_profile *profiles,
    struct micros_bootstrap_manifest_plan *plan,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    if (
        manifest == NULL
        || expected_services == NULL
        || images == NULL
        || profiles == NULL
        || plan == NULL
        || !bootstrap_manifest_pointer_is_aligned(
            manifest,
            _Alignof(struct micros_bootstrap_manifest)
        )
        || !bootstrap_manifest_pointer_is_aligned(
            expected_services,
            _Alignof(struct micros_bootstrap_expected_service)
        )
        || !bootstrap_manifest_pointer_is_aligned(
            images,
            _Alignof(struct micros_bootstrap_image_info)
        )
        || !bootstrap_manifest_pointer_is_aligned(
            profiles,
            _Alignof(struct micros_privilege_profile)
        )
        || !bootstrap_manifest_pointer_is_aligned(
            plan,
            _Alignof(struct micros_bootstrap_manifest_plan)
        )
        || (
            diagnostic != NULL
            && !bootstrap_manifest_pointer_is_aligned(
                diagnostic,
                _Alignof(struct micros_bootstrap_diagnostic)
            )
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_manifest_tables(
    const struct micros_bootstrap_manifest *manifest,
    size_t expected_service_count,
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    enum micros_bootstrap_error error = validate_header(manifest);

    if (error != MICROS_BOOTSTRAP_OK) {
        return validation_failure(
            diagnostic,
            error,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_HEADER,
            0,
            0
        );
    }
    if (expected_service_count != manifest->header.entry_count) {
        return validation_failure(
            diagnostic,
            MICROS_BOOTSTRAP_ERROR_IDENTITY,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
            0,
            0
        );
    }
    error = bootstrap_manifest_validate_profile_table(profiles, profile_count);
    if (error != MICROS_BOOTSTRAP_OK) {
        return validation_failure(
            diagnostic,
            error,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_PROFILE,
            0,
            0
        );
    }
    if (
        !bootstrap_manifest_bytes_are_zero(
            &manifest->entries[manifest->header.entry_count],
            (
                MICROS_BOOTSTRAP_SERVICE_CAPACITY
                - manifest->header.entry_count
            ) * sizeof(manifest->entries[0])
        )
    ) {
        return validation_failure(
            diagnostic,
            MICROS_BOOTSTRAP_ERROR_SHAPE,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
            0,
            0
        );
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_entry_identity(
    const struct micros_bootstrap_manifest *manifest,
    size_t index,
    const struct micros_bootstrap_expected_service *expected_services,
    size_t expected_service_count,
    const struct micros_bootstrap_expected_service **expected_entry,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    const struct micros_bootstrap_manifest_entry *entry =
        &manifest->entries[index];
    const struct micros_bootstrap_expected_service *expected;
    size_t other;

    for (other = 0; other < index; ++other) {
        const struct micros_bootstrap_manifest_entry *prior =
            &manifest->entries[other];

        if (
            prior->service_id == entry->service_id
            || prior->image_id == entry->image_id
            || prior->process_slot == entry->process_slot
            || bootstrap_manifest_fixed_names_equal(
                prior->service_name,
                entry->service_name
            )
            || bootstrap_manifest_fixed_names_equal(
                prior->profile_name,
                entry->profile_name
            )
        ) {
            return validation_failure(
                diagnostic,
                MICROS_BOOTSTRAP_ERROR_IDENTITY,
                MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
                entry->service_id,
                0
            );
        }
    }
    expected = find_expected(
        expected_services,
        expected_service_count,
        entry->service_id
    );
    if (
        expected == NULL
        || expected->reserved != 0
        || expected->image_id != entry->image_id
        || expected->process_slot != entry->process_slot
        || expected->profile_id != entry->profile_id
        || expected->prerequisites != entry->prerequisites
        || expected->role_flags != entry->role_flags
        || expected->device_base != entry->device_base
        || expected->device_length != entry->device_length
        || expected->irq_source != entry->irq_source
        || !bootstrap_manifest_fixed_names_equal(
            expected->service_name,
            entry->service_name
        )
        || !bootstrap_manifest_fixed_names_equal(
            expected->profile_name,
            entry->profile_name
        )
    ) {
        return validation_failure(
            diagnostic,
            MICROS_BOOTSTRAP_ERROR_IDENTITY,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
            entry->service_id,
            0
        );
    }
    *expected_entry = expected;
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_entry_profile(
    const struct micros_bootstrap_manifest_entry *entry,
    const struct micros_bootstrap_expected_service *expected,
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    const struct micros_privilege_profile *profile =
        bootstrap_manifest_find_profile(
            profiles,
            profile_count,
            entry->profile_id
        );

    if (
        profile == NULL
        || !bootstrap_manifest_fixed_names_equal(
            profile->name,
            entry->profile_name
        )
        || profile->call_targets != expected->call_targets
    ) {
        return validation_failure(
            diagnostic,
            MICROS_BOOTSTRAP_ERROR_PROFILE,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_PROFILE,
            entry->service_id,
            0
        );
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_entry_image(
    const struct micros_bootstrap_manifest_entry *entry,
    const struct micros_bootstrap_image_info *images,
    size_t image_count,
    struct bootstrap_manifest_candidate *candidate,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    const struct micros_bootstrap_image_info *image =
        find_image(images, image_count, entry->image_id);
    enum micros_bootstrap_error error = validate_image_bound(entry, image);
    uint64_t entry_mapping_count;

    if (error != MICROS_BOOTSTRAP_OK) {
        return validation_failure(
            diagnostic,
            error,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_IMAGE,
            entry->service_id,
            0
        );
    }
    entry_mapping_count =
        (uint64_t)image->page_count + entry->stack_page_count;
    if (
        (entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONTROLLER) != 0
    ) {
        ++entry_mapping_count;
    }
    if (
        UINT64_MAX - candidate->prepared_mapping_count
            < entry_mapping_count
    ) {
        return validation_failure(
            diagnostic,
            MICROS_BOOTSTRAP_ERROR_RANGE,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_IMAGE,
            entry->service_id,
            0
        );
    }
    candidate->prepared_mapping_count += entry_mapping_count;
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error account_entry(
    const struct micros_bootstrap_manifest_entry *entry,
    struct bootstrap_manifest_candidate *candidate,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    if (UINT64_MAX - candidate->total_pages < entry->user_page_limit) {
        return validation_failure(
            diagnostic,
            MICROS_BOOTSTRAP_ERROR_RANGE,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
            entry->service_id,
            0
        );
    }
    candidate->total_pages += entry->user_page_limit;
    candidate->active_ids |= UINT64_C(1) << (entry->service_id - 1);
    candidate->controller_count += (
        entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONTROLLER
    ) != 0;
    candidate->vm_count += (
        entry->role_flags & MICROS_BOOTSTRAP_ROLE_VM
    ) != 0;
    candidate->console_count += (
        entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
    ) != 0;
    candidate->pm_count += (
        entry->role_flags & MICROS_BOOTSTRAP_ROLE_PM
    ) != 0;
    if (
        entry->service_id == 3
        && entry->process_slot == 2
        && entry->profile_id == MICROS_PRIVILEGE_PROFILE_PM
        && bootstrap_manifest_fixed_name_matches_literal(
            entry->service_name,
            "pm"
        )
        && bootstrap_manifest_fixed_name_matches_literal(
            entry->profile_name,
            "PM"
        )
    ) {
        candidate->pm_identity_entry = entry;
    }
    if (
        entry->service_id == MICROS_TTY_SERVICE_ID
        && entry->process_slot == MICROS_TTY_PROCESS_SLOT
        && entry->profile_id == MICROS_PRIVILEGE_PROFILE_TTY
        && bootstrap_manifest_fixed_name_matches_literal(
            entry->service_name,
            "tty"
        )
        && bootstrap_manifest_fixed_name_matches_literal(
            entry->profile_name,
            "TTY"
        )
    ) {
        candidate->console_identity_entry = entry;
    }
    if (entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONTROLLER) {
        candidate->plan.controller_service_id = entry->service_id;
        candidate->controller_profile_id = entry->profile_id;
    }
    if (entry->role_flags & MICROS_BOOTSTRAP_ROLE_VM) {
        candidate->plan.vm_service_id = entry->service_id;
    }
    if (entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER) {
        candidate->plan.console_service_id = entry->service_id;
        candidate->console_entry = entry;
    }
    if (entry->role_flags & MICROS_BOOTSTRAP_ROLE_PM) {
        candidate->plan.pm_service_id = entry->service_id;
        candidate->pm_entry = entry;
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_entries(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_expected_service *expected_services,
    size_t expected_service_count,
    const struct micros_bootstrap_image_info *images,
    size_t image_count,
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    struct bootstrap_manifest_candidate *candidate,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    size_t index;

    for (index = 0; index < manifest->header.entry_count; ++index) {
        const struct micros_bootstrap_manifest_entry *entry =
            &manifest->entries[index];
        const struct micros_bootstrap_expected_service *expected;
        enum micros_bootstrap_error error;

        error = validate_entry_shape(entry);
        if (error != MICROS_BOOTSTRAP_OK) {
            return validation_failure(
                diagnostic,
                error,
                MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
                entry->service_id <= 63 ? entry->service_id : 0,
                0
            );
        }
        error = validate_entry_identity(
            manifest,
            index,
            expected_services,
            expected_service_count,
            &expected,
            diagnostic
        );
        if (error != MICROS_BOOTSTRAP_OK) {
            return error;
        }
        error = validate_entry_profile(
            entry,
            expected,
            profiles,
            profile_count,
            diagnostic
        );
        if (error != MICROS_BOOTSTRAP_OK) {
            return error;
        }
        error = validate_entry_image(
            entry,
            images,
            image_count,
            candidate,
            diagnostic
        );
        if (error != MICROS_BOOTSTRAP_OK) {
            return error;
        }
        error = account_entry(entry, candidate, diagnostic);
        if (error != MICROS_BOOTSTRAP_OK) {
            return error;
        }
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_aggregate_bounds(
    const struct micros_bootstrap_manifest *manifest,
    uint64_t available_user_pages,
    const struct bootstrap_manifest_candidate *candidate,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    if (
        candidate->controller_count != 1
        || candidate->vm_count > 1
        || candidate->console_count > 1
        || candidate->pm_count > 1
        || (
            candidate->vm_count != 0
            && candidate->prepared_mapping_count
                > MICROS_VM_MAX_STATIC_MAPPINGS
        )
        || candidate->total_pages != manifest->header.total_user_page_limit
        || candidate->total_pages > available_user_pages
    ) {
        enum micros_bootstrap_error aggregate_error =
            candidate->controller_count != 1
                || candidate->vm_count > 1
                || candidate->console_count > 1
                || candidate->pm_count > 1
                ? MICROS_BOOTSTRAP_ERROR_ROLE
                : MICROS_BOOTSTRAP_ERROR_RANGE;

        return validation_failure(
            diagnostic,
            aggregate_error,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
            0,
            0
        );
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_console_role(
    const struct bootstrap_manifest_candidate *candidate,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    const struct micros_bootstrap_manifest_entry *console_entry =
        candidate->console_entry;

    if (
        (console_entry == NULL && candidate->console_identity_entry != NULL)
        || (
            console_entry != NULL
            && (
                candidate->pm_entry == NULL
                || console_entry->service_id != MICROS_TTY_SERVICE_ID
                || console_entry->process_slot != MICROS_TTY_PROCESS_SLOT
                || console_entry->profile_id != MICROS_PRIVILEGE_PROFILE_TTY
                || !bootstrap_manifest_fixed_name_matches_literal(
                    console_entry->service_name,
                    "tty"
                )
                || !bootstrap_manifest_fixed_name_matches_literal(
                    console_entry->profile_name,
                    "TTY"
                )
                || console_entry->prerequisites
                    != (
                        UINT64_C(1)
                        << (candidate->plan.pm_service_id - 1)
                    )
            )
        )
    ) {
        return validation_failure(
            diagnostic,
            MICROS_BOOTSTRAP_ERROR_ROLE,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
            console_entry != NULL
                ? console_entry->service_id
                : MICROS_TTY_SERVICE_ID,
            0
        );
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_pm_role(
    const struct bootstrap_manifest_candidate *candidate,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    const struct micros_bootstrap_manifest_entry *pm_entry =
        candidate->pm_entry;
    const struct micros_bootstrap_manifest_entry *pm_identity_entry =
        candidate->pm_identity_entry;

    if (
        (
            candidate->pm_count == 0
            && pm_identity_entry != NULL
            && candidate->vm_count == 1
            && pm_identity_entry->prerequisites
                == (
                    UINT64_C(1)
                    << (candidate->plan.vm_service_id - 1)
                )
        )
        || (
            pm_entry != NULL
            && (
                candidate->vm_count != 1
                || pm_entry->service_id != 3
                || pm_entry->process_slot != 2
                || pm_entry->profile_id != MICROS_PRIVILEGE_PROFILE_PM
                || !bootstrap_manifest_fixed_name_matches_literal(
                    pm_entry->service_name,
                    "pm"
                )
                || !bootstrap_manifest_fixed_name_matches_literal(
                    pm_entry->profile_name,
                    "PM"
                )
                || pm_entry->prerequisites
                    != (
                        UINT64_C(1)
                        << (candidate->plan.vm_service_id - 1)
                    )
            )
        )
    ) {
        return validation_failure(
            diagnostic,
            MICROS_BOOTSTRAP_ERROR_ROLE,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
            pm_entry != NULL ? pm_entry->service_id : 3,
            0
        );
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_aggregate_roles(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    uint64_t available_user_pages,
    const struct bootstrap_manifest_candidate *candidate,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    enum micros_bootstrap_error error = validate_aggregate_bounds(
        manifest,
        available_user_pages,
        candidate,
        diagnostic
    );

    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    error = validate_console_role(candidate, diagnostic);
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    error = validate_pm_role(candidate, diagnostic);
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    error = bootstrap_manifest_validate_profile_relationships(
        manifest,
        profiles,
        profile_count,
        candidate->controller_profile_id
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        return validation_failure(
            diagnostic,
            error,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_PROFILE,
            0,
            0
        );
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_topology(
    const struct micros_bootstrap_manifest *manifest,
    struct bootstrap_manifest_candidate *candidate,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    uint64_t selected_ids = 0;
    size_t index;

    for (index = 0; index < manifest->header.entry_count; ++index) {
        uint32_t best_id = UINT32_MAX;
        size_t best_index = SIZE_MAX;
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
                best_index = candidate_index;
            }
        }
        if (best_index == SIZE_MAX) {
            uint64_t unresolved = candidate->active_ids & ~selected_ids;
            uint32_t implicated_service = 0;
            size_t unresolved_index;

            for (
                unresolved_index = 0;
                unresolved_index < manifest->header.entry_count;
                ++unresolved_index
            ) {
                uint32_t candidate_id =
                    manifest->entries[unresolved_index].service_id;

                if (
                    (unresolved
                        & (
                            UINT64_C(1)
                            << (candidate_id - 1)
                        )) != 0
                    && (
                        implicated_service == 0
                        || candidate_id < implicated_service
                    )
                ) {
                    implicated_service = candidate_id;
                }
            }
            return validation_failure(
                diagnostic,
                MICROS_BOOTSTRAP_ERROR_TOPOLOGY,
                MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_CYCLE,
                implicated_service,
                unresolved
            );
        }
        candidate->plan.ordered_service_ids[index] = best_id;
        candidate->plan.ordered_manifest_indices[index] =
            (uint16_t)best_index;
        selected_ids |= UINT64_C(1) << (best_id - 1);
    }
    if (
        candidate->plan.ordered_service_ids[0]
            != candidate->plan.controller_service_id
    ) {
        return validation_failure(
            diagnostic,
            MICROS_BOOTSTRAP_ERROR_TOPOLOGY,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_CYCLE,
            candidate->plan.controller_service_id,
            UINT64_C(1)
                << (candidate->plan.controller_service_id - 1)
        );
    }
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error micros_bootstrap_manifest_validate_detailed(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_expected_service *expected_services,
    size_t expected_service_count,
    const struct micros_bootstrap_image_info *images,
    size_t image_count,
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    uint64_t available_user_pages,
    struct micros_bootstrap_manifest_plan *plan,
    struct micros_bootstrap_diagnostic *diagnostic
)
{
    struct bootstrap_manifest_candidate candidate;
    enum micros_bootstrap_error error;

    error = validate_arguments(
        manifest,
        expected_services,
        images,
        profiles,
        plan,
        diagnostic
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    bootstrap_manifest_clear_bytes(&candidate, sizeof(candidate));
    error = validate_manifest_tables(
        manifest,
        expected_service_count,
        profiles,
        profile_count,
        diagnostic
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    error = validate_entries(
        manifest,
        expected_services,
        expected_service_count,
        images,
        image_count,
        profiles,
        profile_count,
        &candidate,
        diagnostic
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    error = validate_aggregate_roles(
        manifest,
        profiles,
        profile_count,
        available_user_pages,
        &candidate,
        diagnostic
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    error = validate_topology(manifest, &candidate, diagnostic);
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    candidate.plan.entry_count = manifest->header.entry_count;
    candidate.plan.total_user_page_limit =
        manifest->header.total_user_page_limit;
    bootstrap_manifest_copy_bytes(plan, &candidate.plan, sizeof(*plan));
    if (diagnostic != NULL) {
        bootstrap_manifest_clear_bytes(diagnostic, sizeof(*diagnostic));
    }
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error micros_bootstrap_manifest_validate(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_expected_service *expected_services,
    size_t expected_service_count,
    const struct micros_bootstrap_image_info *images,
    size_t image_count,
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    uint64_t available_user_pages,
    struct micros_bootstrap_manifest_plan *plan
)
{
    return micros_bootstrap_manifest_validate_detailed(
        manifest,
        expected_services,
        expected_service_count,
        images,
        image_count,
        profiles,
        profile_count,
        available_user_pages,
        plan,
        NULL
    );
}
