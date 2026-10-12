#include "frame_ownership_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MICROS_FRAME_OWNERSHIP_MAGIC UINT64_C(0x4d4943524f53464f)

void micros_frame_ownership_clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void micros_frame_ownership_copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
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

bool micros_frame_owner_is_equal(
    struct micros_frame_owner left,
    struct micros_frame_owner right
)
{
    return (
        left.generation == right.generation
        && left.slot == right.slot
        && left.kind == right.kind
        && left.reserved == right.reserved
    );
}

bool micros_frame_process_handle_is_valid(
    struct micros_process_handle process
)
{
    return (
        process.slot < MICROS_PROCESS_CAPACITY
        && process.generation != 0
        && process.generation <= MICROS_PROCESS_GENERATION_MAX
    );
}

bool micros_frame_owner_kind_is_process_bound(uint8_t kind)
{
    return (
        kind == MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
        || kind == MICROS_FRAME_OWNER_PROCESS_USER
        || kind == MICROS_FRAME_OWNER_VM_WIRED
    );
}

bool micros_frame_owner_is_valid(struct micros_frame_owner owner)
{
    if (
        owner.kind >= MICROS_FRAME_OWNER_KIND_COUNT
        || owner.reserved != 0
    ) {
        return false;
    }
    if (micros_frame_owner_kind_is_process_bound(owner.kind)) {
        return micros_frame_process_handle_is_valid(
            (struct micros_process_handle){
                owner.slot,
                owner.generation,
            }
        );
    }
    return owner.slot == 0 && owner.generation == 0;
}

static bool range_snapshot_is_zero(
    const struct micros_frame_range_snapshot *range
)
{
    return (
        range->base == 0
        && range->frame_count == 0
        && range->bitmap_offset == 0
    );
}

static bool managed_range_is_zero(
    const struct micros_managed_frame_range *range
)
{
    return (
        range->base == 0
        && range->frame_count == 0
        && range->bitmap_offset == 0
    );
}

static bool allocator_geometry_is_valid(
    const struct micros_frame_allocator *allocator
)
{
    uint64_t expected_bitmap_offset = 0;
    uint64_t previous_end = 0;
    size_t index;

    if (
        allocator == NULL
        || !allocator->initialized
        || allocator->managed_range_count == 0
        || allocator->managed_range_count
            > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES
        || allocator->managed_frame_count == 0
        || allocator->managed_frame_count
            > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
        || allocator->free_frame_count
            > allocator->managed_frame_count
    ) {
        return false;
    }
    for (
        index = 0;
        index < allocator->managed_range_count;
        ++index
    ) {
        const struct micros_managed_frame_range *range =
            &allocator->managed_ranges[index];
        uint64_t range_size;
        uint64_t range_end;

        if (
            range->frame_count == 0
            || range->frame_count
                > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
                    - expected_bitmap_offset
            || range->base % MICROS_FRAME_SIZE != 0
            || range->bitmap_offset != expected_bitmap_offset
            || range->frame_count
                > UINT64_MAX / MICROS_FRAME_SIZE
        ) {
            return false;
        }
        range_size = range->frame_count * MICROS_FRAME_SIZE;
        if (
            range_size > UINT64_MAX - range->base
            || (index != 0 && range->base < previous_end)
        ) {
            return false;
        }
        range_end = range->base + range_size;
        expected_bitmap_offset += range->frame_count;
        previous_end = range_end;
    }
    if (expected_bitmap_offset != allocator->managed_frame_count) {
        return false;
    }
    for (
        index = allocator->managed_range_count;
        index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES;
        ++index
    ) {
        if (!managed_range_is_zero(&allocator->managed_ranges[index])) {
            return false;
        }
    }
    return true;
}

static bool allocator_is_pristine(
    const struct micros_frame_allocator *allocator
)
{
    size_t index;

    if (
        !allocator_geometry_is_valid(allocator)
        || allocator->free_frame_count
            != allocator->managed_frame_count
    ) {
        return false;
    }
    for (
        index = 0;
        index < MICROS_FRAME_ALLOCATOR_BITMAP_WORDS;
        ++index
    ) {
        if (allocator->allocated_bitmap[index] != 0) {
            return false;
        }
    }
    return true;
}

enum micros_frame_ownership_error micros_frame_ownership_require_initialized(
    const struct micros_frame_ownership *ownership
)
{
    if (ownership == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (
        ownership->initialization_magic
            != MICROS_FRAME_OWNERSHIP_MAGIC
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_NOT_INITIALIZED;
    }
    if (
        ownership->allocator == NULL
        || ownership->allocator_identity
            != (uintptr_t)ownership->allocator
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}

bool micros_frame_ownership_geometry_matches(
    const struct micros_frame_ownership *ownership
)
{
    const struct micros_frame_allocator *allocator =
        ownership->allocator;
    uint64_t expected_bitmap_offset = 0;
    uint64_t previous_end = 0;
    size_t index;

    if (
        ownership->allocator_identity != (uintptr_t)allocator
        || !allocator_geometry_is_valid(allocator)
        || ownership->managed_range_count == 0
        || ownership->managed_range_count
            > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES
        || ownership->managed_frame_count == 0
        || ownership->managed_frame_count
            > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
        || allocator->managed_range_count
            != ownership->managed_range_count
        || allocator->managed_frame_count
            != ownership->managed_frame_count
    ) {
        return false;
    }
    for (
        index = 0;
        index < ownership->managed_range_count;
        ++index
    ) {
        const struct micros_frame_range_snapshot *snapshot =
            &ownership->managed_ranges[index];
        const struct micros_managed_frame_range *current =
            &allocator->managed_ranges[index];
        uint64_t range_size;
        uint64_t range_end;

        if (
            snapshot->frame_count == 0
            || snapshot->frame_count
                > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
                    - expected_bitmap_offset
            || snapshot->base % MICROS_FRAME_SIZE != 0
            || snapshot->bitmap_offset != expected_bitmap_offset
            || snapshot->frame_count
                > UINT64_MAX / MICROS_FRAME_SIZE
            || snapshot->base != current->base
            || snapshot->frame_count != current->frame_count
            || snapshot->bitmap_offset != current->bitmap_offset
        ) {
            return false;
        }
        range_size = snapshot->frame_count * MICROS_FRAME_SIZE;
        if (
            range_size > UINT64_MAX - snapshot->base
            || (index != 0 && snapshot->base < previous_end)
        ) {
            return false;
        }
        range_end = snapshot->base + range_size;
        expected_bitmap_offset += snapshot->frame_count;
        previous_end = range_end;
    }
    if (expected_bitmap_offset != ownership->managed_frame_count) {
        return false;
    }
    for (
        index = ownership->managed_range_count;
        index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES;
        ++index
    ) {
        if (!range_snapshot_is_zero(&ownership->managed_ranges[index])) {
            return false;
        }
    }
    return true;
}

enum micros_frame_ownership_error micros_frame_ownership_physical_address_to_index(
    const struct micros_frame_ownership *ownership,
    uint64_t physical_address,
    uint64_t *frame_index
)
{
    size_t range_index;

    if (physical_address % MICROS_FRAME_SIZE != 0) {
        return MICROS_FRAME_OWNERSHIP_ERROR_UNALIGNED;
    }
    for (
        range_index = 0;
        range_index < ownership->managed_range_count;
        ++range_index
    ) {
        const struct micros_frame_range_snapshot *range =
            &ownership->managed_ranges[range_index];
        uint64_t offset;
        uint64_t index_in_range;

        if (physical_address < range->base) {
            break;
        }
        offset = physical_address - range->base;
        index_in_range = offset / MICROS_FRAME_SIZE;
        if (index_in_range < range->frame_count) {
            *frame_index = range->bitmap_offset + index_in_range;
            return MICROS_FRAME_OWNERSHIP_OK;
        }
    }
    return MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED;
}

bool micros_frame_ownership_allocator_bit_is_set(
    const struct micros_frame_allocator *allocator,
    uint64_t frame_index
)
{
    size_t word_index = (size_t)(frame_index / 64);
    uint64_t bit = UINT64_C(1) << (frame_index % 64);

    return (allocator->allocated_bitmap[word_index] & bit) != 0;
}

bool micros_frame_ownership_handoff_target_is_valid(
    struct micros_frame_owner owner,
    uint8_t target
)
{
    if (target == MICROS_FRAME_HANDOFF_NONE) {
        return true;
    }
    if (owner.kind == MICROS_FRAME_OWNER_PROCESS_USER) {
        return (
            target == MICROS_FRAME_HANDOFF_VM_WIRED
            || target == MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        );
    }
    return (
        owner.kind == MICROS_FRAME_OWNER_KERNEL_RETAINED
        && target == MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
    );
}
enum micros_frame_ownership_error micros_frame_owner_make_kernel(
    enum micros_frame_owner_kind kind,
    struct micros_frame_owner *owner
)
{
    struct micros_frame_owner result;

    if (owner == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    result = (struct micros_frame_owner){
        0,
        0,
        (uint8_t)kind,
        0,
    };
    if (
        kind != MICROS_FRAME_OWNER_KERNEL_RETAINED
        && kind != MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
        && kind != MICROS_FRAME_OWNER_KERNEL_TEMPORARY
        && kind != MICROS_FRAME_OWNER_VM_TRANSFERABLE
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_OWNER;
    }
    *owner = result;
    return MICROS_FRAME_OWNERSHIP_OK;
}

enum micros_frame_ownership_error micros_frame_owner_make_process(
    enum micros_frame_owner_kind kind,
    struct micros_process_handle process,
    struct micros_frame_owner *owner
)
{
    struct micros_frame_owner result;

    if (owner == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (
        (
            kind != MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
            && kind != MICROS_FRAME_OWNER_PROCESS_USER
            && kind != MICROS_FRAME_OWNER_VM_WIRED
        )
        || !micros_frame_process_handle_is_valid(process)
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_OWNER;
    }
    result = (struct micros_frame_owner){
        process.generation,
        process.slot,
        (uint8_t)kind,
        0,
    };
    *owner = result;
    return MICROS_FRAME_OWNERSHIP_OK;
}

enum micros_frame_ownership_error micros_frame_ownership_initialize(
    struct micros_frame_ownership *ownership,
    struct micros_frame_allocator *allocator
)
{
    size_t index;

    if (ownership == NULL || allocator == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (
        ownership->initialization_magic
            == MICROS_FRAME_OWNERSHIP_MAGIC
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ALREADY_INITIALIZED;
    }
    if (!bytes_are_zero(ownership, sizeof(*ownership))) {
        return MICROS_FRAME_OWNERSHIP_ERROR_STORAGE;
    }
    if (!allocator_is_pristine(allocator)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ALLOCATOR_STATE;
    }

    ownership->allocator = allocator;
    ownership->allocator_identity = (uintptr_t)allocator;
    ownership->phase = MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP;
    ownership->managed_range_count = allocator->managed_range_count;
    ownership->managed_frame_count = allocator->managed_frame_count;
    for (index = 0; index < allocator->managed_range_count; ++index) {
        ownership->managed_ranges[index].base =
            allocator->managed_ranges[index].base;
        ownership->managed_ranges[index].frame_count =
            allocator->managed_ranges[index].frame_count;
        ownership->managed_ranges[index].bitmap_offset =
            allocator->managed_ranges[index].bitmap_offset;
    }
    ownership->initialization_magic =
        MICROS_FRAME_OWNERSHIP_MAGIC;
    return MICROS_FRAME_OWNERSHIP_OK;
}
void micros_frame_ownership_allocator_mark_allocated(
    struct micros_frame_allocator *allocator,
    uint64_t frame_index
)
{
    size_t word_index = (size_t)(frame_index / 64);
    uint64_t bit = UINT64_C(1) << (frame_index % 64);

    allocator->allocated_bitmap[word_index] |= bit;
}

void micros_frame_ownership_allocator_mark_free(
    struct micros_frame_allocator *allocator,
    uint64_t frame_index
)
{
    size_t word_index = (size_t)(frame_index / 64);
    uint64_t bit = UINT64_C(1) << (frame_index % 64);

    allocator->allocated_bitmap[word_index] &= ~bit;
}

static struct micros_frame_owner resulting_owner(
    struct micros_frame_owner owner,
    uint8_t target
)
{
    if (target == MICROS_FRAME_HANDOFF_VM_WIRED) {
        owner.kind = MICROS_FRAME_OWNER_VM_WIRED;
    } else if (target == MICROS_FRAME_HANDOFF_VM_TRANSFERABLE) {
        owner = (struct micros_frame_owner){
            0,
            0,
            MICROS_FRAME_OWNER_VM_TRANSFERABLE,
            0,
        };
    }
    return owner;
}
enum micros_frame_ownership_error
micros_frame_ownership_prepare_handoff(
    struct micros_frame_ownership *ownership,
    uint64_t physical_address,
    struct micros_frame_owner expected_owner,
    enum micros_frame_handoff_target target
)
{
    struct micros_frame_owner stored_owner;
    uint64_t frame_index;
    enum micros_frame_ownership_error error;

    error = micros_frame_ownership_require_initialized(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (ownership->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP) {
        return MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    }
    if (!micros_frame_owner_is_valid(expected_owner)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_OWNER;
    }
    if (!micros_frame_ownership_geometry_matches(ownership)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    error = micros_frame_ownership_physical_address_to_index(
        ownership,
        physical_address,
        &frame_index
    );
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    stored_owner = ownership->owners[frame_index];
    if (stored_owner.kind == MICROS_FRAME_OWNER_FREE) {
        return MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED;
    }
    if (!micros_frame_owner_is_valid(stored_owner)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    if (!micros_frame_owner_is_equal(stored_owner, expected_owner)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
    }
    if (
        target >= MICROS_FRAME_HANDOFF_TARGET_COUNT
        || !micros_frame_ownership_handoff_target_is_valid(stored_owner, (uint8_t)target)
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION;
    }
    ownership->handoff_targets[frame_index] = (uint8_t)target;
    return MICROS_FRAME_OWNERSHIP_OK;
}

enum micros_frame_ownership_error
micros_frame_ownership_prepare_wired_process_user_set(
    struct micros_frame_ownership *ownership,
    const uint64_t *selected_bitmap,
    size_t selected_word_count
)
{
    uint64_t selected_count = 0;
    uint64_t process_user_count = 0;
    uint64_t frame_index;
    enum micros_frame_ownership_error error;

    error = micros_frame_ownership_require_initialized(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (ownership->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP) {
        return MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    }
    if (
        selected_bitmap == NULL
        || selected_word_count
            != MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    error = micros_frame_ownership_validate(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }

    for (
        frame_index = 0;
        frame_index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES;
        ++frame_index
    ) {
        size_t word_index = (size_t)(frame_index / 64);
        uint64_t bit = UINT64_C(1) << (frame_index % 64);
        bool selected =
            (selected_bitmap[word_index] & bit) != 0;
        struct micros_frame_owner owner;

        if (frame_index >= ownership->managed_frame_count) {
            if (selected) {
                return MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED;
            }
            continue;
        }
        owner = ownership->owners[frame_index];
        if (owner.kind == MICROS_FRAME_OWNER_PROCESS_USER) {
            if (
                process_user_count
                    == ownership->managed_frame_count
            ) {
                return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
            }
            ++process_user_count;
            if (!selected) {
                return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
            }
            if (
                ownership->handoff_targets[frame_index]
                    != MICROS_FRAME_HANDOFF_NONE
            ) {
                return MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION;
            }
            ++selected_count;
            continue;
        }
        if (selected) {
            return owner.kind == MICROS_FRAME_OWNER_FREE
                ? MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED
                : MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
        }
    }
    if (
        process_user_count
            != ownership->owner_counts[
                MICROS_FRAME_OWNER_PROCESS_USER
            ]
        || selected_count != process_user_count
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }

    for (
        frame_index = 0;
        frame_index < ownership->managed_frame_count;
        ++frame_index
    ) {
        if (
            ownership->owners[frame_index].kind
                == MICROS_FRAME_OWNER_PROCESS_USER
        ) {
            ownership->handoff_targets[frame_index] =
                MICROS_FRAME_HANDOFF_VM_WIRED;
        }
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}
enum micros_frame_ownership_error
micros_frame_ownership_complete_handoff(
    struct micros_frame_ownership *ownership
)
{
    uint64_t resulting_counts[MICROS_FRAME_OWNER_KIND_COUNT];
    uint64_t resulting_owned_count = 0;
    uint64_t frame_index;
    enum micros_frame_ownership_error error;

    micros_frame_ownership_clear_bytes(resulting_counts, sizeof(resulting_counts));
    error = micros_frame_ownership_require_initialized(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (ownership->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP) {
        return MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    }
    error = micros_frame_ownership_validate(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }

    for (
        frame_index = 0;
        frame_index < ownership->managed_frame_count;
        ++frame_index
    ) {
        struct micros_frame_owner owner =
            ownership->owners[frame_index];
        uint8_t target = ownership->handoff_targets[frame_index];
        struct micros_frame_owner result;

        if (
            owner.kind == MICROS_FRAME_OWNER_KERNEL_TEMPORARY
            || (
                owner.kind == MICROS_FRAME_OWNER_PROCESS_USER
                && target == MICROS_FRAME_HANDOFF_NONE
            )
        ) {
            return MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION;
        }
        result = resulting_owner(owner, target);
        if (!micros_frame_owner_is_valid(result)) {
            return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
        }
        if (result.kind != MICROS_FRAME_OWNER_FREE) {
            if (
                resulting_owned_count
                    == ownership->managed_frame_count
                || resulting_counts[result.kind]
                    == ownership->managed_frame_count
            ) {
                return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
            }
            ++resulting_owned_count;
            ++resulting_counts[result.kind];
        }
    }
    if (resulting_owned_count != ownership->owned_frame_count) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }

    for (
        frame_index = 0;
        frame_index < ownership->managed_frame_count;
        ++frame_index
    ) {
        ownership->owners[frame_index] = resulting_owner(
            ownership->owners[frame_index],
            ownership->handoff_targets[frame_index]
        );
        ownership->handoff_targets[frame_index] =
            MICROS_FRAME_HANDOFF_NONE;
    }
    for (
        frame_index = 0;
        frame_index < MICROS_FRAME_OWNER_KIND_COUNT;
        ++frame_index
    ) {
        ownership->owner_counts[frame_index] =
            resulting_counts[frame_index];
    }
    ownership->owned_frame_count = resulting_owned_count;
    ownership->phase = MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF;
    return MICROS_FRAME_OWNERSHIP_OK;
}
enum micros_frame_ownership_error
micros_frame_ownership_vm_claim_preflight(
    const struct micros_frame_ownership *ownership,
    uint64_t physical_address,
    struct micros_frame_vm_claim_plan *plan
)
{
    struct micros_frame_vm_claim_plan candidate;
    uint64_t frame_index;
    enum micros_frame_ownership_error error;

    if (plan == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    error = micros_frame_ownership_vm_transition_preflight(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    error = micros_frame_ownership_physical_address_to_index(
        ownership,
        physical_address,
        &frame_index
    );
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (
        ownership->owners[frame_index].kind
            != MICROS_FRAME_OWNER_FREE
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
    }
    if (
        ownership->allocator->free_frame_count == 0
        || ownership->owned_frame_count
            == ownership->managed_frame_count
        || ownership->owner_counts[
            MICROS_FRAME_OWNER_VM_TRANSFERABLE
        ] == ownership->managed_frame_count
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    candidate.frame_index = frame_index;
    micros_frame_ownership_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_FRAME_OWNERSHIP_OK;
}

void micros_frame_ownership_vm_claim_commit_prevalidated(
    struct micros_frame_ownership *ownership,
    const struct micros_frame_vm_claim_plan *plan
)
{
    const struct micros_frame_owner transferable = {
        0,
        0,
        MICROS_FRAME_OWNER_VM_TRANSFERABLE,
        0,
    };

    micros_frame_ownership_allocator_mark_allocated(
        ownership->allocator,
        plan->frame_index
    );
    ownership->owners[plan->frame_index] = transferable;
    ownership->handoff_targets[plan->frame_index] =
        MICROS_FRAME_HANDOFF_NONE;
    --ownership->allocator->free_frame_count;
    ++ownership->owned_frame_count;
    ++ownership->owner_counts[
        MICROS_FRAME_OWNER_VM_TRANSFERABLE
    ];
}

enum micros_frame_ownership_error
micros_frame_ownership_vm_release_transferable_preflight(
    const struct micros_frame_ownership *ownership,
    const uint64_t *physical_addresses,
    size_t physical_address_count,
    struct micros_frame_vm_transferable_release_plan *plan
)
{
    struct micros_frame_vm_transferable_release_plan candidate;
    enum micros_frame_ownership_error error;
    size_t index;

    if (
        plan == NULL
        || physical_address_count
            > MICROS_APPLICATION_RESIDENT_PAGE_MAX
        || (
            physical_address_count != 0
            && physical_addresses == NULL
        )
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    micros_frame_ownership_clear_bytes(&candidate, sizeof(candidate));
    error = micros_frame_ownership_vm_transition_preflight(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    for (index = 0; index < physical_address_count; ++index) {
        uint64_t frame_index;
        struct micros_frame_owner owner;

        error = micros_frame_ownership_physical_address_to_index(
            ownership,
            physical_addresses[index],
            &frame_index
        );
        if (error != MICROS_FRAME_OWNERSHIP_OK) {
            return error;
        }
        if (micros_frame_ownership_frame_index_is_duplicate(
            candidate.frame_indices,
            index,
            frame_index
        )) {
            return MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION;
        }
        owner = ownership->owners[frame_index];
        if (owner.kind == MICROS_FRAME_OWNER_FREE) {
            return MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED;
        }
        if (
            !micros_frame_owner_is_equal(
                owner,
                (struct micros_frame_owner){
                    0,
                    0,
                    MICROS_FRAME_OWNER_VM_TRANSFERABLE,
                    0,
                }
            )
        ) {
            return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
        }
        candidate.frame_indices[index] = frame_index;
    }
    if (
        physical_address_count > ownership->owned_frame_count
        || physical_address_count
            > ownership->owner_counts[
                MICROS_FRAME_OWNER_VM_TRANSFERABLE
            ]
        || physical_address_count
            > ownership->managed_frame_count
                - ownership->allocator->free_frame_count
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    candidate.frame_count = (uint32_t)physical_address_count;
    micros_frame_ownership_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_FRAME_OWNERSHIP_OK;
}

void micros_frame_ownership_vm_release_transferable_commit_prevalidated(
    struct micros_frame_ownership *ownership,
    const struct micros_frame_vm_transferable_release_plan *plan
)
{
    size_t index;

    for (index = 0; index < plan->frame_count; ++index) {
        uint64_t frame_index = plan->frame_indices[index];

        micros_frame_ownership_allocator_mark_free(ownership->allocator, frame_index);
        ownership->owners[frame_index] =
            (struct micros_frame_owner){0, 0, 0, 0};
        ownership->handoff_targets[frame_index] =
            MICROS_FRAME_HANDOFF_NONE;
    }
    ownership->allocator->free_frame_count += plan->frame_count;
    ownership->owned_frame_count -= plan->frame_count;
    ownership->owner_counts[
        MICROS_FRAME_OWNER_VM_TRANSFERABLE
    ] -= plan->frame_count;
}
enum micros_frame_ownership_error
micros_frame_ownership_vm_freeze_preflight(
    const struct micros_frame_ownership *ownership,
    struct micros_process_handle process,
    const uint64_t *data_physical_addresses,
    size_t data_physical_address_count,
    const uint64_t
        table_physical_addresses[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    struct micros_frame_vm_freeze_plan *plan
)
{
    struct micros_frame_vm_freeze_plan candidate;
    enum micros_frame_ownership_error error;

    if (plan == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    micros_frame_ownership_clear_bytes(&candidate, sizeof(candidate));
    error = micros_frame_ownership_vm_process_set_preflight(
        ownership,
        process,
        data_physical_addresses,
        data_physical_address_count,
        table_physical_addresses,
        MICROS_FRAME_OWNER_VM_TRANSFERABLE,
        MICROS_FRAME_OWNER_FREE,
        candidate.data_frame_indices,
        candidate.table_frame_indices
    );
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (
        data_physical_address_count
            > ownership->owner_counts[
                MICROS_FRAME_OWNER_VM_TRANSFERABLE
            ]
        || MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
            > ownership->allocator->free_frame_count
        || MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
            > ownership->managed_frame_count
                - ownership->owned_frame_count
        || data_physical_address_count
            > ownership->managed_frame_count
                - ownership->owner_counts[
                    MICROS_FRAME_OWNER_VM_WIRED
                ]
        || MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
            > ownership->managed_frame_count
                - ownership->owner_counts[
                    MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
                ]
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    candidate.process = process;
    candidate.data_frame_count =
        (uint32_t)data_physical_address_count;
    micros_frame_ownership_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_FRAME_OWNERSHIP_OK;
}

void micros_frame_ownership_vm_freeze_commit_prevalidated(
    struct micros_frame_ownership *ownership,
    const struct micros_frame_vm_freeze_plan *plan
)
{
    const struct micros_frame_owner wired = {
        plan->process.generation,
        plan->process.slot,
        MICROS_FRAME_OWNER_VM_WIRED,
        0,
    };
    const struct micros_frame_owner table = {
        plan->process.generation,
        plan->process.slot,
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        0,
    };
    size_t index;

    for (index = 0; index < plan->data_frame_count; ++index) {
        ownership->owners[plan->data_frame_indices[index]] = wired;
    }
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        uint64_t frame_index = plan->table_frame_indices[index];

        micros_frame_ownership_allocator_mark_allocated(ownership->allocator, frame_index);
        ownership->owners[frame_index] = table;
        ownership->handoff_targets[frame_index] =
            MICROS_FRAME_HANDOFF_NONE;
    }
    ownership->allocator->free_frame_count -=
        MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
    ownership->owned_frame_count +=
        MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
    ownership->owner_counts[
        MICROS_FRAME_OWNER_VM_TRANSFERABLE
    ] -= plan->data_frame_count;
    ownership->owner_counts[MICROS_FRAME_OWNER_VM_WIRED] +=
        plan->data_frame_count;
    ownership->owner_counts[
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
    ] += MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
}

enum micros_frame_ownership_error
micros_frame_ownership_vm_release_process_preflight(
    const struct micros_frame_ownership *ownership,
    struct micros_process_handle process,
    const uint64_t *data_physical_addresses,
    size_t data_physical_address_count,
    const uint64_t
        table_physical_addresses[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    struct micros_frame_vm_process_release_plan *plan
)
{
    struct micros_frame_vm_process_release_plan candidate;
    enum micros_frame_ownership_error error;
    size_t release_count;

    if (plan == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    micros_frame_ownership_clear_bytes(&candidate, sizeof(candidate));
    error = micros_frame_ownership_vm_process_set_preflight(
        ownership,
        process,
        data_physical_addresses,
        data_physical_address_count,
        table_physical_addresses,
        MICROS_FRAME_OWNER_VM_WIRED,
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        candidate.data_frame_indices,
        candidate.table_frame_indices
    );
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    release_count = data_physical_address_count
        + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
    if (
        data_physical_address_count
            > ownership->owner_counts[
                MICROS_FRAME_OWNER_VM_WIRED
            ]
        || MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
            > ownership->owner_counts[
                MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
            ]
        || release_count > ownership->owned_frame_count
        || release_count
            > ownership->managed_frame_count
                - ownership->allocator->free_frame_count
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    candidate.process = process;
    candidate.data_frame_count =
        (uint32_t)data_physical_address_count;
    micros_frame_ownership_copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_FRAME_OWNERSHIP_OK;
}

void micros_frame_ownership_vm_release_process_commit_prevalidated(
    struct micros_frame_ownership *ownership,
    const struct micros_frame_vm_process_release_plan *plan
)
{
    size_t release_count = plan->data_frame_count
        + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
    size_t index;

    for (index = 0; index < plan->data_frame_count; ++index) {
        uint64_t frame_index = plan->data_frame_indices[index];

        micros_frame_ownership_allocator_mark_free(ownership->allocator, frame_index);
        ownership->owners[frame_index] =
            (struct micros_frame_owner){0, 0, 0, 0};
        ownership->handoff_targets[frame_index] =
            MICROS_FRAME_HANDOFF_NONE;
    }
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        uint64_t frame_index = plan->table_frame_indices[index];

        micros_frame_ownership_allocator_mark_free(ownership->allocator, frame_index);
        ownership->owners[frame_index] =
            (struct micros_frame_owner){0, 0, 0, 0};
        ownership->handoff_targets[frame_index] =
            MICROS_FRAME_HANDOFF_NONE;
    }
    ownership->allocator->free_frame_count += release_count;
    ownership->owned_frame_count -= release_count;
    ownership->owner_counts[MICROS_FRAME_OWNER_VM_WIRED] -=
        plan->data_frame_count;
    ownership->owner_counts[
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
    ] -= MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
}
