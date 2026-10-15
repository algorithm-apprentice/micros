#include "servers/vm/vm_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/endpoint.h"
#include "micros/sv39.h"

void vm_core_zero_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;

    while (size >= 8) {
        bytes[0] = 0;
        bytes[1] = 0;
        bytes[2] = 0;
        bytes[3] = 0;
        bytes[4] = 0;
        bytes[5] = 0;
        bytes[6] = 0;
        bytes[7] = 0;
        bytes += 8;
        size -= 8;
    }
    while (size != 0) {
        *bytes++ = 0;
        --size;
    }
}

void vm_core_copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    uint8_t *output = destination;
    const uint8_t *input = source;

    while (size >= 8) {
        output[0] = input[0];
        output[1] = input[1];
        output[2] = input[2];
        output[3] = input[3];
        output[4] = input[4];
        output[5] = input[5];
        output[6] = input[6];
        output[7] = input[7];
        output += 8;
        input += 8;
        size -= 8;
    }
    while (size != 0) {
        *output++ = *input++;
        --size;
    }
}

bool vm_core_bytes_are_zero(const void *storage, size_t size)
{
    const uint8_t *bytes = storage;

    while (size >= 8) {
        if (
            (
                bytes[0] | bytes[1] | bytes[2] | bytes[3]
                | bytes[4] | bytes[5] | bytes[6] | bytes[7]
            ) != 0
        ) {
            return false;
        }
        bytes += 8;
        size -= 8;
    }
    while (size != 0) {
        if (*bytes++ != 0) {
            return false;
        }
        --size;
    }
    return true;
}

bool vm_core_bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
    const uint8_t *left_bytes = left;
    const uint8_t *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

bool vm_core_ranges_overlap(
    const void *left,
    size_t left_size,
    const void *right,
    size_t right_size
)
{
    uintptr_t left_start = (uintptr_t)left;
    uintptr_t right_start = (uintptr_t)right;
    uintptr_t left_end;
    uintptr_t right_end;

    if (left_size == 0 || right_size == 0) {
        return false;
    }
    if (
        left_start > UINTPTR_MAX - left_size
        || right_start > UINTPTR_MAX - right_size
    ) {
        return true;
    }
    left_end = left_start + left_size;
    right_end = right_start + right_size;
    return left_start < right_end && right_start < left_end;
}

bool vm_core_endpoint_is_canonical(uint32_t endpoint)
{
    uint32_t slot_mask =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1;
    uint32_t slot = endpoint & slot_mask;
    uint32_t generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;

    return (
        endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
        && slot < MICROS_PROCESS_CAPACITY
        && generation != 0
        && generation <= MICROS_ENDPOINT_GENERATION_MAX
    );
}

bool vm_core_process_is_canonical(struct micros_process_handle process)
{
    return (
        process.slot < MICROS_PROCESS_CAPACITY
        && process.generation != 0
        && process.generation <= MICROS_PROCESS_GENERATION_MAX
    );
}

bool vm_core_user_permissions_are_canonical(uint32_t permissions)
{
    const uint32_t defined =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_EXECUTE
        | MICROS_SV39_PERMISSION_USER;

    return (
        (permissions & ~defined) == 0
        && (
            permissions & MICROS_SV39_PERMISSION_USER
        ) != 0
        && (
            permissions & MICROS_SV39_PERMISSION_READ
        ) != 0
        && (
            (permissions & MICROS_SV39_PERMISSION_WRITE) == 0
            || (
                permissions & MICROS_SV39_PERMISSION_READ
            ) != 0
        )
        && (
            permissions
            & (
                MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_EXECUTE
            )
        ) != (
            MICROS_SV39_PERMISSION_WRITE
            | MICROS_SV39_PERMISSION_EXECUTE
        )
    );
}

bool vm_core_stack_plan_equal(
    const struct micros_application_stack_plan *left,
    const struct micros_application_stack_plan *right
)
{
    return (
        left->stack_pointer == right->stack_pointer
        && left->frame_size == right->frame_size
        && left->occupied_size == right->occupied_size
        && left->argc_offset == right->argc_offset
        && left->arguments_offset == right->arguments_offset
        && left->environment_offset == right->environment_offset
        && left->auxiliary_offset == right->auxiliary_offset
        && left->strings_offset == right->strings_offset
    );
}

bool vm_core_initial_frame_is_free(
    const struct micros_vm_state *state,
    uint64_t frame_index
)
{
    size_t word_index = (size_t)(frame_index / 64);
    uint64_t bit = UINT64_C(1) << (frame_index % 64);

    return (
        state->initial_free_bitmap[word_index] & bit
    ) != 0;
}

static void vm_core_mark_initial_frame_free(
    struct micros_vm_state *state,
    uint64_t frame_index
)
{
    size_t word_index = (size_t)(frame_index / 64);
    uint64_t bit = UINT64_C(1) << (frame_index % 64);

    state->initial_free_bitmap[word_index] |= bit;
}

static uint64_t vm_core_page_digest_byte(uint64_t digest, uint8_t byte)
{
    digest ^= byte;
    return digest * MICROS_VM_PAGE_DIGEST_PRIME;
}

static uint64_t vm_core_page_digest_u32(uint64_t digest, uint32_t value)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        digest = vm_core_page_digest_byte(
            digest,
            (uint8_t)(value >> (index * 8))
        );
    }
    return digest;
}

static uint64_t vm_core_page_digest_u64(uint64_t digest, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        digest = vm_core_page_digest_byte(
            digest,
            (uint8_t)(value >> (index * 8))
        );
    }
    return digest;
}

uint64_t vm_core_load_page_digest(
    const struct micros_vm_state *state
)
{
    const struct micros_vm_service_load_transaction *transaction =
        &state->transaction;
    uint64_t digest = MICROS_VM_PAGE_DIGEST_OFFSET;
    size_t index;

    digest = vm_core_page_digest_u32(
        digest,
        transaction->elf_plan.page_count
    );
    digest = vm_core_page_digest_u32(digest, transaction->page_count);
    for (index = 0; index < transaction->page_count; ++index) {
        uint64_t virtual_address = index
                < transaction->elf_plan.page_count
            ? transaction->elf_plan.pages[index].virtual_address
            : MICROS_APPLICATION_STACK_BASE;
        uint32_t permissions = index
                < transaction->elf_plan.page_count
            ? transaction->elf_plan.pages[index].permissions
            : MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE;

        digest = vm_core_page_digest_u64(digest, virtual_address);
        digest = vm_core_page_digest_u64(
            digest,
            transaction->data_frames[index]
        );
        digest = vm_core_page_digest_u32(digest, permissions);
        digest = vm_core_page_digest_u32(digest, 0);
    }
    return digest;
}

enum micros_vm_core_error micros_vm_state_initialize(
    struct micros_vm_state *state,
    struct micros_vm_boot_info *boot_info,
    uint32_t expected_service_id,
    uint32_t expected_self_endpoint,
    uint32_t expected_address_space_count
)
{
    struct micros_vm_boot_summary summary;

    if (
        state == NULL
        || boot_info == NULL
        || expected_service_id == 0
        || !vm_core_endpoint_is_canonical(expected_self_endpoint)
        || expected_address_space_count == 0
        || expected_address_space_count
            > MICROS_VM_MAX_STATIC_ADDRESS_SPACES
        || vm_core_ranges_overlap(
            state,
            sizeof(*state),
            boot_info,
            sizeof(*boot_info)
        )
    ) {
        return MICROS_VM_CORE_ERROR_ARGUMENT;
    }
    if (
        micros_vm_boot_validate(boot_info, &summary)
            != MICROS_VM_BOOT_OK
    ) {
        return MICROS_VM_CORE_ERROR_BOOT;
    }
    if (
        boot_info->header.service_id != expected_service_id
        || boot_info->header.self_endpoint != expected_self_endpoint
        || boot_info->header.address_space_count
            != expected_address_space_count
    ) {
        return MICROS_VM_CORE_ERROR_BOOT;
    }
    vm_core_zero_bytes(state, sizeof(*state));
    state->initialization_magic = MICROS_VM_INITIALIZATION_MAGIC;
    state->phase = MICROS_VM_LOADER_IDLE;
    state->boot_info = boot_info;
    state->boot_summary = summary;
    {
        uint64_t frame_index;

        for (
            frame_index = 0;
            frame_index < summary.managed_frame_count;
            ++frame_index
        ) {
            if (
                boot_info->frame_states[frame_index]
                    == MICROS_VM_FRAME_FREE
            ) {
                vm_core_mark_initial_frame_free(
                    state,
                    frame_index
                );
            }
        }
    }
    return MICROS_VM_CORE_OK;
}
