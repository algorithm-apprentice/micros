#include "servers/vm/vm_image_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/spawn/spawn_request.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

static enum micros_application_stack_error vm_image_plan_decoded_stack(
    const struct micros_spawn_request *request,
    struct micros_application_stack_plan *plan
)
{
    struct micros_application_stack_plan candidate = {0};
    struct micros_spawn_stack_layout layout;
    enum micros_spawn_stack_layout_error layout_error;
    size_t frame_offset;

    layout_error = micros_spawn_stack_layout_plan(
        request,
        &layout
    );
    if (layout_error == MICROS_SPAWN_STACK_LAYOUT_ERROR_ARGUMENT) {
        return MICROS_APPLICATION_STACK_ERROR_ARGUMENT;
    }
    if (layout_error != MICROS_SPAWN_STACK_LAYOUT_OK) {
        return MICROS_APPLICATION_STACK_ERROR_TOO_BIG;
    }

    frame_offset =
        MICROS_APPLICATION_STACK_PAGE_SIZE - layout.occupied_size;
    candidate.stack_pointer =
        MICROS_APPLICATION_STACK_TOP - layout.occupied_size;
    candidate.frame_size = layout.frame_size;
    candidate.occupied_size = layout.occupied_size;
    candidate.argc_offset = (uint32_t)frame_offset;
    candidate.arguments_offset =
        (uint32_t)(frame_offset + sizeof(uint64_t));
    candidate.environment_offset =
        (uint32_t)frame_offset + layout.environment_offset;
    candidate.auxiliary_offset =
        (uint32_t)frame_offset + layout.auxiliary_offset;
    candidate.strings_offset =
        (uint32_t)frame_offset + layout.strings_offset;
    *plan = candidate;
    return MICROS_APPLICATION_STACK_OK;
}

static enum micros_application_stack_error vm_image_decode_and_plan_stack(
    const uint8_t *request_bytes,
    size_t request_size,
    struct micros_spawn_request *request,
    struct micros_application_stack_plan *plan
)
{
    enum micros_spawn_request_error error =
        micros_spawn_request_decode(
            request_bytes,
            request_size,
            request
        );

    if (error == MICROS_SPAWN_REQUEST_ERROR_ARGUMENT) {
        return MICROS_APPLICATION_STACK_ERROR_ARGUMENT;
    }
    if (error != MICROS_SPAWN_REQUEST_OK) {
        return MICROS_APPLICATION_STACK_ERROR_MALFORMED;
    }
    return vm_image_plan_decoded_stack(request, plan);
}

enum micros_application_stack_error micros_application_stack_plan(
    const uint8_t *request_bytes,
    size_t request_size,
    struct micros_application_stack_plan *plan
)
{
    struct micros_spawn_request request;
    struct micros_application_stack_plan candidate;
    enum micros_application_stack_error error;

    if (plan == NULL) {
        return MICROS_APPLICATION_STACK_ERROR_ARGUMENT;
    }
    error = vm_image_decode_and_plan_stack(
        request_bytes,
        request_size,
        &request,
        &candidate
    );
    if (error != MICROS_APPLICATION_STACK_OK) {
        return error;
    }
    *plan = candidate;
    return MICROS_APPLICATION_STACK_OK;
}

enum micros_application_stack_error micros_application_stack_build(
    const uint8_t *request_bytes,
    size_t request_size,
    const struct micros_application_stack_values *values,
    uint8_t *stack_page,
    size_t stack_page_size,
    struct micros_application_stack_plan *plan
)
{
    static const uint64_t auxiliary_types[MICROS_APPLICATION_AUXV_COUNT] = {
        MICROS_AT_PM_ENDPOINT,
        MICROS_AT_VFS_ENDPOINT,
        MICROS_AT_PID,
        MICROS_AT_PAGE_SIZE,
        MICROS_AT_NULL,
    };
    struct micros_spawn_request request;
    struct micros_application_stack_plan candidate;
    uint64_t auxiliary_values[MICROS_APPLICATION_AUXV_COUNT];
    enum micros_application_stack_error error;
    size_t string_offset;
    size_t index;

    if (
        request_bytes == NULL
        || values == NULL
        || stack_page == NULL
        || plan == NULL
        || stack_page_size != MICROS_APPLICATION_STACK_PAGE_SIZE
        || values->pm_endpoint == MICROS_ENDPOINT_NONE
        || values->pm_endpoint == MICROS_ENDPOINT_ANY
        || values->vfs_endpoint == MICROS_ENDPOINT_NONE
        || values->vfs_endpoint == MICROS_ENDPOINT_ANY
        || values->pid == 0
        || vm_image_ranges_overlap(
            request_bytes,
            request_size,
            stack_page,
            stack_page_size
        )
    ) {
        return MICROS_APPLICATION_STACK_ERROR_ARGUMENT;
    }
    error = vm_image_decode_and_plan_stack(
        request_bytes,
        request_size,
        &request,
        &candidate
    );
    if (error != MICROS_APPLICATION_STACK_OK) {
        return error;
    }

    auxiliary_values[0] = values->pm_endpoint;
    auxiliary_values[1] = values->vfs_endpoint;
    auxiliary_values[2] = values->pid;
    auxiliary_values[3] = MICROS_APPLICATION_STACK_PAGE_SIZE;
    auxiliary_values[4] = 0;

    vm_image_zero_bytes(stack_page, stack_page_size);
    vm_image_write_u64_le(
        &stack_page[candidate.argc_offset],
        request.argument_count
    );
    string_offset = candidate.strings_offset;
    for (index = 0; index < request.argument_count; ++index) {
        const struct micros_spawn_string_reference *reference =
            &request.arguments[index];

        vm_image_write_u64_le(
            &stack_page[candidate.arguments_offset
                + index * sizeof(uint64_t)],
            MICROS_APPLICATION_STACK_BASE + string_offset
        );
        vm_image_copy_bytes(
            &stack_page[string_offset],
            &request_bytes[reference->offset],
            reference->size
        );
        string_offset += reference->size;
    }
    for (index = 0; index < request.environment_count; ++index) {
        const struct micros_spawn_string_reference *reference =
            &request.environment[index];

        vm_image_write_u64_le(
            &stack_page[candidate.environment_offset
                + index * sizeof(uint64_t)],
            MICROS_APPLICATION_STACK_BASE + string_offset
        );
        vm_image_copy_bytes(
            &stack_page[string_offset],
            &request_bytes[reference->offset],
            reference->size
        );
        string_offset += reference->size;
    }
    for (index = 0; index < MICROS_APPLICATION_AUXV_COUNT; ++index) {
        size_t offset = candidate.auxiliary_offset
            + index * 2 * sizeof(uint64_t);

        vm_image_write_u64_le(&stack_page[offset], auxiliary_types[index]);
        vm_image_write_u64_le(
            &stack_page[offset + sizeof(uint64_t)],
            auxiliary_values[index]
        );
    }

    *plan = candidate;
    return MICROS_APPLICATION_STACK_OK;
}
