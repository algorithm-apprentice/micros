#include "tests/qemu/application_runtime_fixture.h"

#include "servers/vm/vm_image.h"
#include "tests/qemu/pm_service_protocol.h"

bool vm_application_test_fill_page(
    size_t page_index,
    uint8_t *page,
    micros_endpoint_t pm_endpoint,
    micros_endpoint_t metadata_peer
)
{
    static const uint32_t flags[MICROS_APPLICATION_ELF_SEGMENT_COUNT] = {5, 4, 6};
    const struct micros_application_test_image *image =
        &micros_application_runtime_test_image;
    size_t index;

    if (
        page == NULL || page_index > MICROS_APPLICATION_ELF_SEGMENT_COUNT
        || image->entry != MICROS_APPLICATION_ENTRY
    ) {
        return false;
    }
    if (page_index == MICROS_APPLICATION_ELF_SEGMENT_COUNT) {
        struct micros_application_stack_values values = {
            .pm_endpoint = pm_endpoint,
            .vfs_endpoint = metadata_peer,
            .pid = MICROS_PM_TEST_APPLICATION_PID,
        };
        struct micros_application_stack_plan plan;

        return (
            micros_application_stack_build(
                image->request_bytes, image->request_size, &values,
                page, MICROS_APPLICATION_STACK_PAGE_SIZE, &plan
            ) == MICROS_APPLICATION_STACK_OK
        );
    }
    {
        const struct micros_application_test_segment *segment =
            &image->segments[page_index];

        if (
            segment->virtual_address
                != MICROS_APPLICATION_ENTRY + page_index * MICROS_APPLICATION_STACK_PAGE_SIZE
            || segment->memory_size != MICROS_APPLICATION_STACK_PAGE_SIZE
            || segment->file_size > MICROS_APPLICATION_STACK_PAGE_SIZE
            || segment->bytes == NULL || segment->elf_flags != flags[page_index]
        ) {
            return false;
        }
        for (index = 0; index < MICROS_APPLICATION_STACK_PAGE_SIZE; ++index) {
            page[index] = 0;
        }
        for (index = 0; index < segment->file_size; ++index) {
            page[index] = segment->bytes[index];
        }
    }
    return true;
}

bool vm_application_test_stack_pointer(uint64_t *stack_pointer)
{
    const struct micros_application_test_image *image =
        &micros_application_runtime_test_image;
    struct micros_application_stack_plan plan;

    if (
        stack_pointer == NULL
        || micros_application_stack_plan(
            image->request_bytes, image->request_size, &plan
        ) != MICROS_APPLICATION_STACK_OK
    ) {
        return false;
    }
    *stack_pointer = plan.stack_pointer;
    return true;
}
