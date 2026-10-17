#include "kernel/boot/bootstrap_runtime_internal.h"

#include "micros/sv39.h"
#include "micros/user_address_space.h"

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

bool bootstrap_preparation_allocate_image(
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
            bootstrap_runtime_clear_bytes(
                (void *)(uintptr_t)physical_address,
                MICROS_SV39_PAGE_SIZE
            );
            if (offset < segment->file_size) {
                uint64_t remaining = segment->file_size - offset;

                copy_size = remaining < MICROS_SV39_PAGE_SIZE
                    ? (size_t)remaining
                    : MICROS_SV39_PAGE_SIZE;
                bootstrap_runtime_copy_bytes(
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

bool bootstrap_preparation_verify_image(
    const struct preparation_record *record
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

bool bootstrap_preparation_allocate_stack(struct preparation_record *record)
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
        bootstrap_runtime_clear_bytes(
            (void *)(uintptr_t)physical_address,
            MICROS_SV39_PAGE_SIZE
        );
        ++record->stack_pages_allocated;
    }
    return true;
}

bool bootstrap_preparation_verify_stack(const struct preparation_record *record)
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

bool bootstrap_preparation_allocate_manifest_view(
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
    bootstrap_runtime_clear_bytes(
        (void *)(uintptr_t)physical_address,
        MICROS_SV39_PAGE_SIZE
    );
    bootstrap_runtime_copy_bytes(
        (void *)(uintptr_t)physical_address,
        manifest,
        sizeof(*manifest)
    );
    record->manifest_view_allocated = true;
    return bootstrap_runtime_bytes_equal(
        (const void *)(uintptr_t)physical_address,
        manifest,
        sizeof(*manifest)
    );
}

bool bootstrap_preparation_write_user_range(
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
        bootstrap_runtime_copy_bytes(
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
        bootstrap_runtime_copy_bytes(
            bytes + copied,
            (const void *)(uintptr_t)physical,
            chunk
        );
        copied += chunk;
    }
    return true;
}

bool bootstrap_preparation_user_range_matches(
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
            !bootstrap_runtime_bytes_equal(
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

    bootstrap_runtime_clear_bytes(
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

bool bootstrap_preparation_patch_configurations(
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

        bootstrap_runtime_clear_bytes(&config, sizeof(config));
        config.version = MICROS_BOOTSTRAP_MANIFEST_VERSION;
        config.manifest_version =
            MICROS_BOOTSTRAP_MANIFEST_VERSION;
        config.service_id = bindings[index].service_id;
        config.self_endpoint = bindings[index].endpoint;
        config.launcher_endpoint = launcher_endpoint;
        config.service_count = manifest->header.entry_count;
        bootstrap_runtime_copy_bytes(
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
            !bootstrap_preparation_write_user_range(
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
            || !bootstrap_runtime_bytes_equal(
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
