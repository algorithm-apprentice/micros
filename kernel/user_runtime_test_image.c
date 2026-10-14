#include "kernel/user_runtime_test_internal.h"

#include "kernel/user_runtime_image.h"
#include "micros/user_address_space.h"

static uint32_t user_runtime_test_segment_permissions(uint32_t flags)
{
    uint32_t permissions = 0;

    if ((flags & USER_RUNTIME_PROGRAM_READ) != 0) {
        permissions |= MICROS_SV39_PERMISSION_READ;
    }
    if ((flags & USER_RUNTIME_PROGRAM_WRITE) != 0) {
        permissions |= MICROS_SV39_PERMISSION_WRITE;
    }
    if ((flags & USER_RUNTIME_PROGRAM_EXECUTE) != 0) {
        permissions |= MICROS_SV39_PERMISSION_EXECUTE;
    }
    return permissions;
}

static struct mapped_page *user_runtime_test_find_page(
    struct user_runtime_test_context *runtime,
    size_t actor,
    uint64_t virtual_address
)
{
    size_t index;

    for (index = 0; index < runtime->images[actor].page_count; ++index) {
        if (
            runtime->images[actor].pages[index].virtual_address
                == virtual_address
        ) {
            return &runtime->images[actor].pages[index];
        }
    }
    return NULL;
}

bool user_runtime_test_allocate_image_pages(
    struct user_runtime_test_context *runtime,
    size_t actor
)
{
    size_t segment_index;

    for (
        segment_index = 0;
        segment_index < micros_user_runtime_test_image.segment_count;
        ++segment_index
    ) {
        const struct micros_user_runtime_image_segment *segment =
            &micros_user_runtime_test_image.segments[segment_index];
        uint32_t permissions =
            user_runtime_test_segment_permissions(segment->flags);
        uint64_t offset;

        if (
            segment->virtual_address % MICROS_SV39_PAGE_SIZE != 0
            || segment->memory_size == 0
            || segment->memory_size % MICROS_SV39_PAGE_SIZE != 0
            || permissions == 0
            || (
                permissions & MICROS_SV39_PERMISSION_WRITE
                && permissions & MICROS_SV39_PERMISSION_EXECUTE
            )
        ) {
            return false;
        }
        for (
            offset = 0;
            offset < segment->memory_size;
            offset += MICROS_SV39_PAGE_SIZE
        ) {
            struct mapped_page *page;

            if (
                runtime->images[actor].page_count
                    >= USER_RUNTIME_MAX_IMAGE_PAGES
            ) {
                return false;
            }
            page = &runtime->images[actor].pages[
                runtime->images[actor].page_count
            ];
            page->virtual_address =
                segment->virtual_address + offset;
            page->permissions = permissions;
            page->segment_index = segment_index;
            if (
                micros_user_address_space_allocate_page(
                    runtime->processes[actor],
                    page->virtual_address,
                    permissions,
                    &page->physical_address
                ) != MICROS_USER_ADDRESS_SPACE_OK
            ) {
                return false;
            }
            user_runtime_test_fill_bytes(
                (void *)(uintptr_t)page->physical_address,
                (unsigned char)(UINT8_C(0xc0) + actor),
                MICROS_SV39_PAGE_SIZE
            );
            user_runtime_test_clear_bytes(
                (void *)(uintptr_t)page->physical_address,
                MICROS_SV39_PAGE_SIZE
            );
            ++runtime->images[actor].page_count;
        }
    }
    return true;
}

bool user_runtime_test_copy_image_segments(
    struct user_runtime_test_context *runtime,
    size_t actor
)
{
    size_t segment_index;

    for (
        segment_index = 0;
        segment_index < micros_user_runtime_test_image.segment_count;
        ++segment_index
    ) {
        const struct micros_user_runtime_image_segment *segment =
            &micros_user_runtime_test_image.segments[segment_index];
        uint64_t copied = 0;

        while (copied < segment->file_size) {
            uint64_t virtual_address =
                segment->virtual_address + copied;
            uint64_t page_address =
                virtual_address
                & ~(uint64_t)(MICROS_SV39_PAGE_SIZE - 1);
            size_t page_offset =
                (size_t)(virtual_address - page_address);
            size_t chunk = MICROS_SV39_PAGE_SIZE - page_offset;
            struct mapped_page *page =
                user_runtime_test_find_page(runtime, actor, page_address);

            if (segment->file_size - copied < chunk) {
                chunk = (size_t)(segment->file_size - copied);
            }
            if (page == NULL) {
                return false;
            }
            user_runtime_test_copy_bytes(
                (void *)(uintptr_t)(
                    page->physical_address + page_offset
                ),
                segment->file_bytes + copied,
                chunk
            );
            copied += chunk;
        }
    }
    return true;
}

bool user_runtime_test_verify_loaded_pages(
    const struct user_runtime_test_context *runtime,
    size_t actor
)
{
    size_t page_index;

    for (
        page_index = 0;
        page_index < runtime->images[actor].page_count;
        ++page_index
    ) {
        const struct mapped_page *page =
            &runtime->images[actor].pages[page_index];
        const struct micros_user_runtime_image_segment *segment =
            &micros_user_runtime_test_image.segments[
                page->segment_index
            ];
        const unsigned char *bytes =
            (const unsigned char *)(uintptr_t)page->physical_address;
        uint64_t segment_file_end =
            segment->virtual_address + segment->file_size;
        size_t offset;
        uint64_t physical;
        uint32_t permissions;

        if (
            micros_user_address_space_lookup(
                runtime->processes[actor],
                page->virtual_address,
                &physical,
                &permissions
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || physical != page->physical_address
            || permissions != page->permissions
        ) {
            return false;
        }
        for (offset = 0; offset < MICROS_SV39_PAGE_SIZE; ++offset) {
            uint64_t address = page->virtual_address + offset;
            unsigned char expected = 0;

            if (address < segment_file_end) {
                expected = segment->file_bytes[
                    address - segment->virtual_address
                ];
            }
            if (bytes[offset] != expected) {
                return false;
            }
        }
    }
    return true;
}

static bool user_runtime_test_translate_range(
    const struct user_runtime_test_context *runtime,
    size_t actor,
    uint64_t address,
    size_t size,
    uint32_t required_permissions,
    uint64_t *physical
)
{
    uint32_t permissions;
    size_t contiguous;

    return (
        micros_user_address_space_translate(
            runtime->processes[actor],
            address,
            physical,
            &permissions,
            &contiguous
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && contiguous >= size
        && (permissions & required_permissions)
            == required_permissions
    );
}

bool user_runtime_test_patch_configuration(
    const struct user_runtime_test_context *runtime,
    size_t actor
)
{
    struct micros_user_runtime_test_config config = {
        .version = MICROS_USER_RUNTIME_TEST_VERSION,
        .role = actor + 1,
        .client_endpoint = runtime->endpoints[USER_RUNTIME_CLIENT],
        .server_endpoint = runtime->endpoints[USER_RUNTIME_SERVER],
        .peer_endpoint = runtime->endpoints[USER_RUNTIME_PEER],
    };
    uint64_t physical;

    if (
        !user_runtime_test_translate_range(
            runtime,
            actor,
            micros_user_runtime_test_image.config_address,
            sizeof(config),
            MICROS_SV39_PERMISSION_WRITE,
            &physical
        )
    ) {
        return false;
    }
    user_runtime_test_copy_bytes(
        (void *)(uintptr_t)physical,
        &config,
        sizeof(config)
    );
    return true;
}

bool user_runtime_test_snapshot_writable_pages(
    struct user_runtime_test_context *runtime,
    size_t actor
)
{
    size_t page_index;

    for (
        page_index = 0;
        page_index < runtime->images[actor].page_count;
        ++page_index
    ) {
        const struct mapped_page *page =
            &runtime->images[actor].pages[page_index];

        if (
            (
                page->permissions
                & MICROS_SV39_PERMISSION_WRITE
            ) == 0
        ) {
            continue;
        }
        user_runtime_test_copy_bytes(
            runtime->writable_snapshots[actor][page_index],
            (const void *)(uintptr_t)page->physical_address,
            MICROS_SV39_PAGE_SIZE
        );
        runtime->writable_snapshot_valid[actor][page_index] = true;
    }
    return true;
}

bool user_runtime_test_writable_pages_match_snapshot(
    const struct user_runtime_test_context *runtime,
    size_t actor
)
{
    size_t page_index;

    for (
        page_index = 0;
        page_index < runtime->images[actor].page_count;
        ++page_index
    ) {
        if (
            runtime->writable_snapshot_valid[actor][page_index]
            && !user_runtime_test_bytes_equal(
                (const void *)(uintptr_t)
                    runtime->images[actor].pages[page_index].physical_address,
                runtime->writable_snapshots[actor][page_index],
                MICROS_SV39_PAGE_SIZE
            )
        ) {
            return false;
        }
    }
    return true;
}

bool user_runtime_test_read_configuration(
    const struct user_runtime_test_context *runtime,
    size_t actor,
    struct micros_user_runtime_test_config *config
)
{
    uint64_t physical;

    if (
        config == NULL
        || !user_runtime_test_translate_range(
            runtime,
            actor,
            micros_user_runtime_test_image.config_address,
            sizeof(*config),
            MICROS_SV39_PERMISSION_READ,
            &physical
        )
    ) {
        return false;
    }
    user_runtime_test_copy_bytes(
        config,
        (const void *)(uintptr_t)physical,
        sizeof(*config)
    );
    return true;
}
