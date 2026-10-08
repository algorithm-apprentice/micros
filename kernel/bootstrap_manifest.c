#include "micros/bootstrap.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/sv39.h"
#include "micros/vm_bootstrap.h"

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool bytes_are_zero(const void *storage, size_t size)
{
    const unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool pointer_is_aligned(const void *pointer, size_t alignment)
{
    return (uintptr_t)pointer % alignment == 0;
}

static bool fixed_name_is_canonical(
    const char name[MICROS_BOOTSTRAP_NAME_SIZE]
)
{
    bool found_nul = false;
    size_t index;

    if (name[0] == '\0') {
        return false;
    }
    for (index = 0; index < MICROS_BOOTSTRAP_NAME_SIZE; ++index) {
        if (name[index] == '\0') {
            found_nul = true;
        } else if (found_nul) {
            return false;
        }
    }
    return found_nul;
}

static bool fixed_names_equal(
    const char left[MICROS_BOOTSTRAP_NAME_SIZE],
    const char right[MICROS_BOOTSTRAP_NAME_SIZE]
)
{
    size_t index;

    for (index = 0; index < MICROS_BOOTSTRAP_NAME_SIZE; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

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

static const struct micros_privilege_profile *find_profile(
    const struct micros_privilege_profile *profiles,
    size_t count,
    uint8_t profile_id
)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        if (profiles[index].id == profile_id) {
            return &profiles[index];
        }
    }
    return NULL;
}

static enum micros_bootstrap_error validate_profile_table(
    const struct micros_privilege_profile *profiles,
    size_t profile_count
)
{
    size_t index;

    if (
        profile_count == 0
        || profile_count > MICROS_PRIVILEGE_PROFILE_CAPACITY
    ) {
        return MICROS_BOOTSTRAP_ERROR_PROFILE;
    }
    for (index = 0; index < profile_count; ++index) {
        const struct micros_privilege_profile *profile =
            &profiles[index];
        size_t character_index;
        size_t other;

        if (
            profile->id == 0
            || profile->id >= MICROS_PRIVILEGE_PROFILE_CAPACITY
            || !fixed_name_is_canonical(profile->name)
            || profile->operations == 0
            || (
                profile->operations
                & ~MICROS_PRIVILEGE_OPERATION_DEFINED_MASK
            ) != 0
            || (
                profile->call_targets != 0
                && (
                    profile->operations
                    & MICROS_PRIVILEGE_OPERATION_CALL
                ) == 0
            )
            || (
                profile->send_targets != 0
                && (
                    profile->operations
                    & MICROS_PRIVILEGE_OPERATION_SEND
                ) == 0
            )
            || (
                profile->notify_targets != 0
                && (
                    profile->operations
                    & MICROS_PRIVILEGE_OPERATION_NOTIFY
                ) == 0
            )
        ) {
            return MICROS_BOOTSTRAP_ERROR_PROFILE;
        }
        for (
            character_index = 0;
            character_index < MICROS_PRIVILEGE_PROFILE_NAME_SIZE
                && profile->name[character_index] != '\0';
            ++character_index
        ) {
            unsigned char character =
                (unsigned char)profile->name[character_index];

            if (character < 0x20 || character > 0x7e) {
                return MICROS_BOOTSTRAP_ERROR_PROFILE;
            }
        }
        for (other = 0; other < index; ++other) {
            if (
                profiles[other].id == profile->id
                || fixed_names_equal(
                    profiles[other].name,
                    profile->name
                )
            ) {
                return MICROS_BOOTSTRAP_ERROR_PROFILE;
            }
        }
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_profile_relationships(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    uint8_t controller_profile_id
)
{
    const struct micros_privilege_profile *controller =
        find_profile(
            profiles,
            profile_count,
            controller_profile_id
        );
    uint32_t controller_target;
        uint8_t vm_profile_id = 0;
        size_t index;

    if (controller == NULL) {
        return MICROS_BOOTSTRAP_ERROR_PROFILE;
    }
    controller_target = UINT32_C(1) << controller_profile_id;
    if (
        controller->operations
            != (
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_REPLY
            )
        || controller->call_targets != 0
        || controller->send_targets != 0
        || controller->notify_targets != 0
        || controller->kernel_operations
            != MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL
    ) {
        return MICROS_BOOTSTRAP_ERROR_PROFILE;
    }
    for (index = 0; index < manifest->header.entry_count; ++index) {
        if (
            (
                manifest->entries[index].role_flags
                & MICROS_BOOTSTRAP_ROLE_VM
            ) != 0
        ) {
            vm_profile_id = manifest->entries[index].profile_id;
            break;
        }
    }
    for (index = 0; index < profile_count; ++index) {
        uint64_t expected_kernel_operations =
            profiles[index].id == controller_profile_id
            ? MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL
            : (
                profiles[index].id == vm_profile_id
                ? MICROS_KERNEL_OPERATION_VM_HANDOFF
                : 0
            );

        if (
            profiles[index].kernel_operations
                != expected_kernel_operations
        ) {
            return MICROS_BOOTSTRAP_ERROR_PROFILE;
        }
    }
    for (index = 0; index < manifest->header.entry_count; ++index) {
        const struct micros_bootstrap_manifest_entry *entry =
            &manifest->entries[index];
        const struct micros_privilege_profile *profile;

        if (entry->profile_id == controller_profile_id) {
            if (
                (
                    entry->role_flags
                    & MICROS_BOOTSTRAP_ROLE_CONTROLLER
                ) == 0
            ) {
                return MICROS_BOOTSTRAP_ERROR_PROFILE;
            }
            continue;
        }
        profile = find_profile(
            profiles,
            profile_count,
            entry->profile_id
        );
        if (
            profile == NULL
            || profile->operations
                != (
                    MICROS_PRIVILEGE_OPERATION_RECEIVE
                    | MICROS_PRIVILEGE_OPERATION_CALL
                )
            || profile->call_targets != controller_target
            || profile->send_targets != 0
            || profile->notify_targets != 0
        ) {
            return MICROS_BOOTSTRAP_ERROR_PROFILE;
        }
    }
    return MICROS_BOOTSTRAP_OK;
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
        || !bytes_are_zero(
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
        || !bytes_are_zero(entry->reserved0, sizeof(entry->reserved0))
        || entry->reserved1 != 0
        || !bytes_are_zero(entry->reserved2, sizeof(entry->reserved2))
        || !fixed_name_is_canonical(entry->service_name)
        || !fixed_name_is_canonical(entry->profile_name)
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
    if (role_count > 1) {
        return MICROS_BOOTSTRAP_ERROR_ROLE;
    }
    if (
        (entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER) != 0
    ) {
        if (
            entry->device_base != MICROS_BOOTSTRAP_UART_BASE
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
    maximum_image_end = (
        entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONTROLLER
    )
        ? MICROS_BOOTSTRAP_MANIFEST_VIEW
        : stack_bottom;
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
    struct micros_bootstrap_manifest_plan candidate;
    uint64_t active_ids = 0;
    uint64_t selected_ids = 0;
    uint64_t total_pages = 0;
    uint64_t prepared_mapping_count = 0;
    uint32_t controller_count = 0;
    uint32_t vm_count = 0;
    uint32_t console_count = 0;
    uint8_t controller_profile_id = 0;
    size_t index;
    enum micros_bootstrap_error error;

#define RETURN_DIAGNOSTIC(error_value, reason_value, service_value, detail_value) \
    do { \
        if (diagnostic != NULL) { \
            *diagnostic = (struct micros_bootstrap_diagnostic){ \
                .reason = (reason_value), \
                .service_id = (service_value), \
                .endpoint = MICROS_ENDPOINT_NONE, \
                .detail = (detail_value), \
            }; \
        } \
        return (error_value); \
    } while (false)

    if (
        manifest == NULL
        || expected_services == NULL
        || images == NULL
        || profiles == NULL
        || plan == NULL
        || !pointer_is_aligned(
            manifest,
            _Alignof(struct micros_bootstrap_manifest)
        )
        || !pointer_is_aligned(
            expected_services,
            _Alignof(struct micros_bootstrap_expected_service)
        )
        || !pointer_is_aligned(
            images,
            _Alignof(struct micros_bootstrap_image_info)
        )
        || !pointer_is_aligned(
            profiles,
            _Alignof(struct micros_privilege_profile)
        )
        || !pointer_is_aligned(
            plan,
            _Alignof(struct micros_bootstrap_manifest_plan)
        )
        || (
            diagnostic != NULL
            && !pointer_is_aligned(
                diagnostic,
                _Alignof(struct micros_bootstrap_diagnostic)
            )
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    error = validate_header(manifest);
    if (error != MICROS_BOOTSTRAP_OK) {
        RETURN_DIAGNOSTIC(
            error,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_HEADER,
            0,
            0
        );
    }
    if (expected_service_count != manifest->header.entry_count) {
        RETURN_DIAGNOSTIC(
            MICROS_BOOTSTRAP_ERROR_IDENTITY,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
            0,
            0
        );
    }
    error = validate_profile_table(profiles, profile_count);
    if (error != MICROS_BOOTSTRAP_OK) {
        RETURN_DIAGNOSTIC(
            error,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_PROFILE,
            0,
            0
        );
    }
    if (
        !bytes_are_zero(
            &manifest->entries[manifest->header.entry_count],
            (
                MICROS_BOOTSTRAP_SERVICE_CAPACITY
                - manifest->header.entry_count
            ) * sizeof(manifest->entries[0])
        )
    ) {
        RETURN_DIAGNOSTIC(
            MICROS_BOOTSTRAP_ERROR_SHAPE,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
            0,
            0
        );
    }
    for (index = 0; index < manifest->header.entry_count; ++index) {
        const struct micros_bootstrap_manifest_entry *entry =
            &manifest->entries[index];
        const struct micros_bootstrap_expected_service *expected;
        const struct micros_bootstrap_image_info *image;
        const struct micros_privilege_profile *profile;
        size_t other;

        error = validate_entry_shape(entry);
        if (error != MICROS_BOOTSTRAP_OK) {
            RETURN_DIAGNOSTIC(
                error,
                MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
                entry->service_id <= 63 ? entry->service_id : 0,
                0
            );
        }
        for (other = 0; other < index; ++other) {
            const struct micros_bootstrap_manifest_entry *prior =
                &manifest->entries[other];

            if (
                prior->service_id == entry->service_id
                || prior->image_id == entry->image_id
                || prior->process_slot == entry->process_slot
                || fixed_names_equal(
                    prior->service_name,
                    entry->service_name
                )
                || fixed_names_equal(
                    prior->profile_name,
                    entry->profile_name
                )
            ) {
                RETURN_DIAGNOSTIC(
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
            || !fixed_names_equal(
                expected->service_name,
                entry->service_name
            )
            || !fixed_names_equal(
                expected->profile_name,
                entry->profile_name
            )
        ) {
            RETURN_DIAGNOSTIC(
                MICROS_BOOTSTRAP_ERROR_IDENTITY,
                MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
                entry->service_id,
                0
            );
        }
        profile = find_profile(profiles, profile_count, entry->profile_id);
        if (
            profile == NULL
            || !fixed_names_equal(profile->name, entry->profile_name)
        ) {
            RETURN_DIAGNOSTIC(
                MICROS_BOOTSTRAP_ERROR_PROFILE,
                MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_PROFILE,
                entry->service_id,
                0
            );
        }
        image = find_image(images, image_count, entry->image_id);
        error = validate_image_bound(entry, image);
        if (error != MICROS_BOOTSTRAP_OK) {
            RETURN_DIAGNOSTIC(
                error,
                MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_IMAGE,
                entry->service_id,
                0
            );
        }
        {
            uint64_t entry_mapping_count =
                (uint64_t)image->page_count
                + entry->stack_page_count;

            if (
                (entry->role_flags
                    & MICROS_BOOTSTRAP_ROLE_CONTROLLER)
                    != 0
            ) {
                ++entry_mapping_count;
            }
            if (
                UINT64_MAX - prepared_mapping_count
                    < entry_mapping_count
            ) {
                RETURN_DIAGNOSTIC(
                    MICROS_BOOTSTRAP_ERROR_RANGE,
                    MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_IMAGE,
                    entry->service_id,
                    0
                );
            }
            prepared_mapping_count += entry_mapping_count;
        }
        if (UINT64_MAX - total_pages < entry->user_page_limit) {
            RETURN_DIAGNOSTIC(
                MICROS_BOOTSTRAP_ERROR_RANGE,
                MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
                entry->service_id,
                0
            );
        }
        total_pages += entry->user_page_limit;
        active_ids |= UINT64_C(1) << (entry->service_id - 1);
        controller_count += (
            entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONTROLLER
        ) != 0;
        vm_count += (entry->role_flags & MICROS_BOOTSTRAP_ROLE_VM) != 0;
        console_count += (
            entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
        ) != 0;
        if (entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONTROLLER) {
            candidate.controller_service_id = entry->service_id;
            controller_profile_id = entry->profile_id;
        }
        if (entry->role_flags & MICROS_BOOTSTRAP_ROLE_VM) {
            candidate.vm_service_id = entry->service_id;
        }
        if (entry->role_flags & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER) {
            candidate.console_service_id = entry->service_id;
        }
    }
    if (
        controller_count != 1
        || vm_count > 1
        || console_count > 1
        || (
            vm_count != 0
            && prepared_mapping_count
                > MICROS_VM_MAX_STATIC_MAPPINGS
        )
        || total_pages != manifest->header.total_user_page_limit
        || total_pages > available_user_pages
    ) {
        enum micros_bootstrap_error aggregate_error =
            controller_count != 1 || vm_count > 1 || console_count > 1
                ? MICROS_BOOTSTRAP_ERROR_ROLE
                : MICROS_BOOTSTRAP_ERROR_RANGE;

        RETURN_DIAGNOSTIC(
            aggregate_error,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
            0,
            0
        );
    }
    error = validate_profile_relationships(
        manifest,
        profiles,
        profile_count,
        controller_profile_id
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        RETURN_DIAGNOSTIC(
            error,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_PROFILE,
            0,
            0
        );
    }
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
            uint64_t unresolved = active_ids & ~selected_ids;
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
            RETURN_DIAGNOSTIC(
                MICROS_BOOTSTRAP_ERROR_TOPOLOGY,
                MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_CYCLE,
                implicated_service,
                unresolved
            );
        }
        candidate.ordered_service_ids[index] = best_id;
        candidate.ordered_manifest_indices[index] =
            (uint16_t)best_index;
        selected_ids |= UINT64_C(1) << (best_id - 1);
    }
    if (
        candidate.ordered_service_ids[0]
            != candidate.controller_service_id
    ) {
        RETURN_DIAGNOSTIC(
            MICROS_BOOTSTRAP_ERROR_TOPOLOGY,
            MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_CYCLE,
            candidate.controller_service_id,
            UINT64_C(1)
                << (candidate.controller_service_id - 1)
        );
    }
    candidate.entry_count = manifest->header.entry_count;
    candidate.total_user_page_limit =
        manifest->header.total_user_page_limit;
    copy_bytes(plan, &candidate, sizeof(*plan));
    if (diagnostic != NULL) {
        clear_bytes(diagnostic, sizeof(*diagnostic));
    }
#undef RETURN_DIAGNOSTIC
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

static struct micros_bootstrap_runtime_entry *runtime_entry(
    struct micros_bootstrap_runtime *runtime,
    uint32_t service_id
)
{
    size_t index;

    for (index = 0; index < runtime->entry_count; ++index) {
        if (runtime->entries[index].service_id == service_id) {
            return &runtime->entries[index];
        }
    }
    return NULL;
}

static const struct micros_bootstrap_runtime_entry *runtime_entry_const(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t service_id
)
{
    size_t index;

    for (index = 0; index < runtime->entry_count; ++index) {
        if (runtime->entries[index].service_id == service_id) {
            return &runtime->entries[index];
        }
    }
    return NULL;
}

enum micros_bootstrap_error micros_bootstrap_runtime_initialize(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_manifest_plan *plan,
    struct micros_bootstrap_runtime *runtime
)
{
    struct micros_bootstrap_runtime candidate;
    size_t index;

    if (manifest == NULL || plan == NULL || runtime == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    if (
        !pointer_is_aligned(
            manifest,
            _Alignof(struct micros_bootstrap_manifest)
        )
        || !pointer_is_aligned(
            plan,
            _Alignof(struct micros_bootstrap_manifest_plan)
        )
        || !pointer_is_aligned(
            runtime,
            _Alignof(struct micros_bootstrap_runtime)
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (!bytes_are_zero(runtime, sizeof(*runtime))) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (
        plan->entry_count == 0
        || plan->entry_count != manifest->header.entry_count
        || plan->controller_service_id == 0
        || plan->ordered_service_ids[0]
            != plan->controller_service_id
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    candidate.phase = MICROS_BOOTSTRAP_PHASE_RUNNING;
    candidate.entry_count = plan->entry_count;
    candidate.next_order_index = 1;
    candidate.controller_service_id = plan->controller_service_id;
    for (index = 0; index < plan->entry_count; ++index) {
        const struct micros_bootstrap_manifest_entry *manifest_entry =
            &manifest->entries[index];
        struct micros_bootstrap_runtime_entry *entry =
            &candidate.entries[index];

        entry->service_id = manifest_entry->service_id;
        entry->prerequisites = manifest_entry->prerequisites;
        entry->ready_timeout_counter_ticks =
            manifest_entry->ready_timeout_counter_ticks;
        if (
            entry->service_id == plan->controller_service_id
        ) {
            entry->state = MICROS_BOOTSTRAP_SERVICE_READY;
            entry->endpoint_state =
                MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
            entry->profile_installed = true;
            entry->scheduler_assigned = true;
        } else {
            entry->state = MICROS_BOOTSTRAP_SERVICE_PREPARED;
            entry->endpoint_state =
                MICROS_BOOTSTRAP_ENDPOINT_RESERVED;
        }
        candidate.ordered_service_ids[index] =
            plan->ordered_service_ids[index];
    }
    copy_bytes(runtime, &candidate, sizeof(*runtime));
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error micros_bootstrap_runtime_release(
    struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint64_t now
)
{
    struct micros_bootstrap_runtime candidate;
    struct micros_bootstrap_runtime_entry *entry;
    uint64_t ready_ids = 0;
    size_t index;

    if (runtime == NULL || service_id == 0) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (runtime->phase != MICROS_BOOTSTRAP_PHASE_RUNNING) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (runtime_entry(runtime, service_id) == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (
        runtime->starting_service_id != 0
        || runtime->next_order_index >= runtime->entry_count
        || runtime->ordered_service_ids[runtime->next_order_index]
            != service_id
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    copy_bytes(&candidate, runtime, sizeof(candidate));
    entry = runtime_entry(&candidate, service_id);
    if (
        entry == NULL
        || entry->state != MICROS_BOOTSTRAP_SERVICE_PREPARED
        || entry->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_RESERVED
        || entry->profile_installed
        || entry->scheduler_assigned
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    for (index = 0; index < candidate.entry_count; ++index) {
        if (
            candidate.entries[index].state
                == MICROS_BOOTSTRAP_SERVICE_READY
        ) {
            ready_ids |= UINT64_C(1)
                << (candidate.entries[index].service_id - 1);
        }
    }
    if ((entry->prerequisites & ~ready_ids) != 0) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (
        entry->ready_timeout_counter_ticks == 0
        || UINT64_MAX - now < entry->ready_timeout_counter_ticks
    ) {
        return MICROS_BOOTSTRAP_ERROR_RANGE;
    }
    entry->profile_installed = true;
    entry->endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
    entry->scheduler_assigned = true;
    entry->state = MICROS_BOOTSTRAP_SERVICE_STARTING;
    entry->ready_deadline = now
        + entry->ready_timeout_counter_ticks;
    candidate.starting_service_id = service_id;
    copy_bytes(runtime, &candidate, sizeof(*runtime));
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error micros_bootstrap_runtime_accept_ready(
    struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint64_t now
)
{
    struct micros_bootstrap_runtime candidate;
    struct micros_bootstrap_runtime_entry *entry;

    if (runtime == NULL || service_id == 0) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (runtime->phase != MICROS_BOOTSTRAP_PHASE_RUNNING) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    if (runtime_entry(runtime, service_id) == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (runtime->starting_service_id != service_id) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    copy_bytes(&candidate, runtime, sizeof(candidate));
    entry = runtime_entry(&candidate, service_id);
    if (
        entry == NULL
        || entry->state != MICROS_BOOTSTRAP_SERVICE_STARTING
        || entry->ready_deadline == 0
        || now >= entry->ready_deadline
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    entry->state = MICROS_BOOTSTRAP_SERVICE_READY;
    entry->ready_deadline = 0;
    candidate.starting_service_id = 0;
    ++candidate.next_order_index;
    copy_bytes(runtime, &candidate, sizeof(*runtime));
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error micros_bootstrap_runtime_complete(
    struct micros_bootstrap_runtime *runtime
)
{
    struct micros_bootstrap_runtime candidate;
    struct micros_bootstrap_runtime_entry *controller;
    size_t index;

    if (runtime == NULL) {
        return MICROS_BOOTSTRAP_ERROR_ARGUMENT;
    }
    if (
        runtime->phase != MICROS_BOOTSTRAP_PHASE_RUNNING
        || runtime->starting_service_id != 0
        || runtime->next_order_index != runtime->entry_count
    ) {
        return MICROS_BOOTSTRAP_ERROR_STATE;
    }
    for (index = 0; index < runtime->entry_count; ++index) {
        if (
            runtime->entries[index].state
                != MICROS_BOOTSTRAP_SERVICE_READY
            || runtime->entries[index].ready_deadline != 0
        ) {
            return MICROS_BOOTSTRAP_ERROR_STATE;
        }
    }
    copy_bytes(&candidate, runtime, sizeof(candidate));
    controller = runtime_entry(
        &candidate,
        candidate.controller_service_id
    );
    if (
        controller == NULL
        || controller->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        || !controller->profile_installed
        || !controller->scheduler_assigned
    ) {
        return MICROS_BOOTSTRAP_ERROR_INVARIANT;
    }
    controller->endpoint_state =
        MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY;
    controller->scheduler_assigned = false;
    candidate.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    candidate.controller_service_id = 0;
    copy_bytes(runtime, &candidate, sizeof(*runtime));
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_ready_class micros_bootstrap_classify_ready(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t source_service_id,
    uint32_t payload_service_id,
    bool message_shape_valid
)
{
    const struct micros_bootstrap_runtime_entry *entry;

    if (!message_shape_valid) {
        return MICROS_BOOTSTRAP_READY_MALFORMED;
    }
    if (runtime == NULL || source_service_id == 0) {
        return MICROS_BOOTSTRAP_READY_FOREIGN;
    }
    entry = runtime_entry_const(runtime, source_service_id);
    if (entry == NULL) {
        return MICROS_BOOTSTRAP_READY_FOREIGN;
    }
    if (source_service_id == runtime->starting_service_id) {
        return payload_service_id == source_service_id
            ? MICROS_BOOTSTRAP_READY_ACCEPT
            : MICROS_BOOTSTRAP_READY_MALFORMED;
    }
    if (entry->state == MICROS_BOOTSTRAP_SERVICE_READY) {
        return MICROS_BOOTSTRAP_READY_DUPLICATE;
    }
    if (entry->state == MICROS_BOOTSTRAP_SERVICE_PREPARED) {
        return MICROS_BOOTSTRAP_READY_EARLY;
    }
    return MICROS_BOOTSTRAP_READY_FOREIGN;
}
