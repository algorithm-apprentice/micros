#include "kernel/vm_handoff_core.h"

#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap.h"
#include "micros/endpoint.h"
#include "micros/frame_allocator.h"

#define MICROS_VM_HANDOFF_MAGIC UINT64_C(0x4d4943524f535648)

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

static bool fits_u32(uint64_t value)
{
    return value <= UINT32_MAX;
}

static bool pointer_is_aligned(const void *pointer, size_t alignment)
{
    return (uintptr_t)pointer % alignment == 0;
}

static bool process_is_valid(struct micros_process_handle process)
{
    return (
        process.slot < MICROS_PROCESS_CAPACITY
        && process.generation != 0
        && process.generation <= MICROS_PROCESS_GENERATION_MAX
    );
}

static bool thread_is_valid(struct micros_thread_handle thread)
{
    return (
        thread.slot < MICROS_THREAD_CAPACITY
        && thread.generation != 0
    );
}

static bool endpoint_matches_process(
    uint32_t endpoint,
    struct micros_process_handle process
)
{
    return (
        process_is_valid(process)
        && endpoint
            == (
                (process.generation << MICROS_ENDPOINT_SLOT_BITS)
                | process.slot
            )
        && endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
    );
}

static bool summary_is_valid(
    const struct micros_vm_boot_summary *summary
)
{
    return (
        summary->version == MICROS_VM_BOOT_INFO_VERSION
        && summary->managed_range_count != 0
        && summary->managed_range_count <= MICROS_VM_MAX_MANAGED_RANGES
        && summary->address_space_count != 0
        && summary->address_space_count
            <= MICROS_VM_MAX_STATIC_ADDRESS_SPACES
        && summary->mapping_count != 0
        && summary->mapping_count <= MICROS_VM_MAX_STATIC_MAPPINGS
        && summary->managed_frame_count != 0
        && summary->managed_frame_count <= MICROS_VM_MAX_MANAGED_FRAMES
        && summary->free_frame_count <= summary->managed_frame_count
        && summary->vm_self_wired_frame_count != 0
        && summary->vm_self_wired_frame_count
            <= summary->managed_frame_count
    );
}

enum micros_syscall_abi_result micros_vm_handoff_decode(
    const struct micros_syscall_arguments *arguments,
    struct micros_vm_handoff_request *request
)
{
    struct micros_vm_handoff_request candidate;
    uint32_t packed_counts;

    if (
        arguments == NULL
        || request == NULL
        || !pointer_is_aligned(
            arguments,
            _Alignof(struct micros_syscall_arguments)
        )
        || !pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_handoff_request)
        )
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    if (
        arguments->a7 != MICROS_SYSCALL_ABI_VM_HANDOFF
        || !fits_u32(arguments->a0)
        || !fits_u32(arguments->a1)
        || !fits_u32(arguments->a2)
        || !fits_u32(arguments->a3)
        || !fits_u32(arguments->a4)
        || !fits_u32(arguments->a5)
        || arguments->a0 != MICROS_VM_HANDOFF_READY
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    packed_counts = (uint32_t)arguments->a2;
    clear_bytes(&candidate, sizeof(candidate));
    candidate.command = (uint32_t)arguments->a0;
    candidate.version = (uint32_t)arguments->a1;
    candidate.managed_range_count = (uint16_t)packed_counts;
    candidate.address_space_count = (uint16_t)(packed_counts >> 16);
    candidate.managed_frame_count = (uint32_t)arguments->a3;
    candidate.free_frame_count = (uint32_t)arguments->a4;
    candidate.mapping_count = (uint32_t)arguments->a5;
    candidate.digest = arguments->a6;
    copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_vm_handoff_error micros_vm_handoff_state_prepare(
    struct micros_vm_handoff_state *state,
    uint32_t service_id,
    uint32_t endpoint,
    uint32_t image_id,
    uint32_t profile_id,
    struct micros_process_handle process,
    struct micros_thread_handle thread,
    uint64_t root_physical_address,
    uint64_t boot_info_address,
    uint32_t boot_info_size,
    const struct micros_vm_boot_summary *summary
)
{
    struct micros_vm_handoff_state candidate;

    if (
        state == NULL
        || summary == NULL
        || !pointer_is_aligned(
            state,
            _Alignof(struct micros_vm_handoff_state)
        )
        || !pointer_is_aligned(
            summary,
            _Alignof(struct micros_vm_boot_summary)
        )
    ) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    if (!bytes_are_zero(state, sizeof(*state))) {
        return MICROS_VM_HANDOFF_ERROR_STORAGE;
    }
    if (
        service_id == 0
        || service_id > 63
        || !endpoint_matches_process(endpoint, process)
        || image_id == 0
        || profile_id == 0
        || profile_id >= MICROS_PRIVILEGE_PROFILE_CAPACITY
        || !thread_is_valid(thread)
        || root_physical_address == 0
        || root_physical_address % MICROS_FRAME_SIZE != 0
        || boot_info_address == 0
        || boot_info_address % MICROS_VM_BOOT_INFO_ALIGNMENT != 0
        || boot_info_size != MICROS_VM_BOOT_INFO_SIZE
        || !summary_is_valid(summary)
    ) {
        return MICROS_VM_HANDOFF_ERROR_IDENTITY;
    }
    clear_bytes(&candidate, sizeof(candidate));
    candidate.initialization_magic = MICROS_VM_HANDOFF_MAGIC;
    candidate.phase = MICROS_VM_HANDOFF_PHASE_PREPARED;
    candidate.service_id = service_id;
    candidate.endpoint = endpoint;
    candidate.image_id = image_id;
    candidate.profile_id = profile_id;
    candidate.process.slot = process.slot;
    candidate.process.generation = process.generation;
    candidate.thread.slot = thread.slot;
    candidate.thread.generation = thread.generation;
    candidate.root_physical_address = root_physical_address;
    candidate.boot_info_address = boot_info_address;
    candidate.boot_info_size = boot_info_size;
    copy_bytes(
        &candidate.summary,
        summary,
        sizeof(candidate.summary)
    );
    copy_bytes(state, &candidate, sizeof(candidate));
    return MICROS_VM_HANDOFF_OK;
}

enum micros_vm_handoff_error micros_vm_handoff_state_validate(
    const struct micros_vm_handoff_state *state
)
{
    if (
        state == NULL
        || !pointer_is_aligned(
            state,
            _Alignof(struct micros_vm_handoff_state)
        )
    ) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    if (
        state->initialization_magic != MICROS_VM_HANDOFF_MAGIC
        || (
            state->phase != MICROS_VM_HANDOFF_PHASE_PREPARED
            && state->phase
                != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        )
        || state->service_id == 0
        || state->service_id > 63
        || !endpoint_matches_process(state->endpoint, state->process)
        || state->image_id == 0
        || state->profile_id == 0
        || state->profile_id >= MICROS_PRIVILEGE_PROFILE_CAPACITY
        || !thread_is_valid(state->thread)
        || state->root_physical_address == 0
        || state->root_physical_address % MICROS_FRAME_SIZE != 0
        || state->boot_info_address == 0
        || state->boot_info_address % MICROS_VM_BOOT_INFO_ALIGNMENT != 0
        || state->boot_info_size != MICROS_VM_BOOT_INFO_SIZE
        || state->reserved != 0
        || !summary_is_valid(&state->summary)
    ) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    return MICROS_VM_HANDOFF_OK;
}

bool micros_vm_handoff_summary_matches(
    const struct micros_vm_handoff_state *state,
    const struct micros_vm_handoff_request *request
)
{
    return (
        state != NULL
        && request != NULL
        && pointer_is_aligned(
            state,
            _Alignof(struct micros_vm_handoff_state)
        )
        && pointer_is_aligned(
            request,
            _Alignof(struct micros_vm_handoff_request)
        )
        && micros_vm_handoff_state_validate(state)
            == MICROS_VM_HANDOFF_OK
        && request->command == MICROS_VM_HANDOFF_READY
        && request->version == state->summary.version
        && request->managed_range_count
            == state->summary.managed_range_count
        && request->address_space_count
            == state->summary.address_space_count
        && request->managed_frame_count
            == state->summary.managed_frame_count
        && request->free_frame_count
            == state->summary.free_frame_count
        && request->mapping_count == state->summary.mapping_count
        && request->digest == state->summary.digest
    );
}

void micros_vm_handoff_commit_prevalidated(
    struct micros_vm_handoff_state *state
)
{
    state->phase = MICROS_VM_HANDOFF_PHASE_HANDED_OFF;
}
