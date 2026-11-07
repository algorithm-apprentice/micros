#include "kernel/memory/frame_ownership_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum micros_frame_ownership_error micros_frame_ownership_lookup(
    const struct micros_frame_ownership *ownership,
    uint64_t physical_address,
    struct micros_frame_owner *owner
)
{
    uint64_t frame_index;
    enum micros_frame_ownership_error error;

    if (owner == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    error = micros_frame_ownership_validate_metadata(ownership);
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
    return micros_frame_ownership_selected_record(ownership, frame_index, owner);
}

enum micros_frame_ownership_error
micros_frame_ownership_count_process(
    const struct micros_frame_ownership *ownership,
    struct micros_process_handle process,
    uint64_t *count
)
{
    uint64_t observed_count = 0;
    uint64_t frame_index;
    enum micros_frame_ownership_error error;

    if (count == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    error = micros_frame_ownership_require_initialized(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (!micros_frame_process_handle_is_valid(process)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_OWNER;
    }
    error = micros_frame_ownership_validate_metadata(ownership);
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

        if (!micros_frame_owner_is_valid(owner)) {
            return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
        }
        if (
            micros_frame_owner_kind_is_process_bound(owner.kind)
            && owner.slot == process.slot
            && owner.generation == process.generation
        ) {
            error = micros_frame_ownership_selected_record(ownership, frame_index, &owner);
            if (error != MICROS_FRAME_OWNERSHIP_OK) {
                return error;
            }
            ++observed_count;
        }
    }
    *count = observed_count;
    return MICROS_FRAME_OWNERSHIP_OK;
}

static enum micros_frame_ownership_error validate_active_frames(
    const struct micros_frame_ownership *ownership,
    uint64_t observed_counts[MICROS_FRAME_OWNER_KIND_COUNT],
    uint64_t *observed_owned_count,
    uint64_t *observed_allocated_count
)
{
    uint64_t frame_index = 0;
    size_t word_index;

    for (
        word_index = 0;
        frame_index < ownership->managed_frame_count;
        ++word_index
    ) {
        uint64_t allocated_word =
            ownership->allocator->allocated_bitmap[word_index];
        uint64_t frame_limit = frame_index + 64;
        uint64_t bit = UINT64_C(1);

        if (frame_limit > ownership->managed_frame_count) {
            frame_limit = ownership->managed_frame_count;
        }
        for (
            ;
            frame_index < frame_limit;
            ++frame_index, bit <<= 1
        ) {
            struct micros_frame_owner owner =
                ownership->owners[frame_index];
            uint8_t target =
                ownership->handoff_targets[frame_index];
            bool allocated = (allocated_word & bit) != 0;

            if (
                !micros_frame_owner_is_valid(owner)
                || allocated
                    == (
                        owner.kind
                            == MICROS_FRAME_OWNER_FREE
                    )
                || !micros_frame_ownership_handoff_target_is_valid(
                    owner,
                    target
                )
                || (
                    owner.kind == MICROS_FRAME_OWNER_FREE
                    && target != MICROS_FRAME_HANDOFF_NONE
                )
                || (
                    ownership->phase
                        == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
                    && (
                        target != MICROS_FRAME_HANDOFF_NONE
                        || owner.kind
                            == MICROS_FRAME_OWNER_KERNEL_TEMPORARY
                        || owner.kind
                            == MICROS_FRAME_OWNER_PROCESS_USER
                    )
                )
            ) {
                return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
            }
            if (allocated) {
                ++*observed_allocated_count;
            }
            if (owner.kind != MICROS_FRAME_OWNER_FREE) {
                ++*observed_owned_count;
                ++observed_counts[owner.kind];
            }
        }
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}

static bool inactive_state_is_zero(
    const struct micros_frame_ownership *ownership
)
{
    uint64_t frame_index;
    size_t word_index;
    size_t boundary_bit;

    for (
        frame_index = ownership->managed_frame_count;
        frame_index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES;
        ++frame_index
    ) {
        struct micros_frame_owner owner =
            ownership->owners[frame_index];

        if (
            owner.generation != 0
            || owner.slot != 0
            || owner.kind != MICROS_FRAME_OWNER_FREE
            || owner.reserved != 0
            || ownership->handoff_targets[frame_index]
                != MICROS_FRAME_HANDOFF_NONE
        ) {
            return false;
        }
    }

    word_index = (size_t)(ownership->managed_frame_count / 64);
    boundary_bit = (size_t)(ownership->managed_frame_count % 64);
    if (boundary_bit != 0) {
        uint64_t inactive_mask = UINT64_MAX << boundary_bit;

        if (
            (
                ownership->allocator->allocated_bitmap[word_index]
                & inactive_mask
            ) != 0
        ) {
            return false;
        }
        ++word_index;
    }
    for (
        ;
        word_index < MICROS_FRAME_ALLOCATOR_BITMAP_WORDS;
        ++word_index
    ) {
        if (ownership->allocator->allocated_bitmap[word_index] != 0) {
            return false;
        }
    }
    return true;
}

static bool allocated_prefix_is_valid(
    const struct micros_frame_ownership *ownership
)
{
    const struct micros_frame_allocator *allocator = ownership->allocator;
    uint64_t cursor = allocator->allocation_cursor;
    size_t complete_words = (size_t)(cursor / 64);
    size_t index;
    unsigned boundary = (unsigned)(cursor % 64);

    for (index = 0; index < complete_words; ++index) {
        if (allocator->allocated_bitmap[index] != UINT64_MAX) {
            return false;
        }
    }
    if (boundary != 0) {
        uint64_t mask = (UINT64_C(1) << boundary) - 1;

        if ((allocator->allocated_bitmap[complete_words] & mask) != mask) {
            return false;
        }
    }
    return true;
}

enum micros_frame_ownership_error micros_frame_ownership_validate(
    const struct micros_frame_ownership *ownership
)
{
    uint64_t observed_counts[MICROS_FRAME_OWNER_KIND_COUNT];
    uint64_t observed_owned_count = 0;
    uint64_t observed_allocated_count = 0;
    size_t kind_index;
    enum micros_frame_ownership_error error;

    micros_frame_ownership_clear_bytes(observed_counts, sizeof(observed_counts));
    error = micros_frame_ownership_require_initialized(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (
        (
            ownership->phase
                != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
            && ownership->phase
                != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        )
        || !micros_frame_ownership_geometry_matches(ownership)
        || !allocated_prefix_is_valid(ownership)
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }

    error = validate_active_frames(
        ownership,
        observed_counts,
        &observed_owned_count,
        &observed_allocated_count
    );
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (!inactive_state_is_zero(ownership)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }

    if (
        ownership->owner_counts[MICROS_FRAME_OWNER_FREE] != 0
        || observed_owned_count != ownership->owned_frame_count
        || observed_allocated_count != observed_owned_count
        || ownership->allocator->managed_frame_count
            - ownership->allocator->free_frame_count
            != observed_owned_count
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    for (
        kind_index = 0;
        kind_index < MICROS_FRAME_OWNER_KIND_COUNT;
        ++kind_index
    ) {
        if (
            ownership->owner_counts[kind_index]
                != observed_counts[kind_index]
        ) {
            return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
        }
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}

enum micros_frame_ownership_error
micros_frame_ownership_vm_transition_preflight(
    const struct micros_frame_ownership *ownership
)
{
    enum micros_frame_ownership_error error =
        micros_frame_ownership_require_initialized(ownership);

    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (
        ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    }
    return micros_frame_ownership_validate_metadata(ownership);
}

bool micros_frame_ownership_frame_index_is_duplicate(
    const uint64_t *frame_indices,
    size_t frame_index_count,
    uint64_t frame_index
)
{
    size_t index;

    for (index = 0; index < frame_index_count; ++index) {
        if (frame_indices[index] == frame_index) {
            return true;
        }
    }
    return false;
}
static struct micros_frame_owner expected_vm_frame_owner(
    enum micros_frame_owner_kind owner_kind,
    struct micros_process_handle process
)
{
    if (owner_kind == MICROS_FRAME_OWNER_VM_TRANSFERABLE) {
        return (struct micros_frame_owner){
            0,
            0,
            MICROS_FRAME_OWNER_VM_TRANSFERABLE,
            0,
        };
    }
    if (owner_kind == MICROS_FRAME_OWNER_FREE) {
        return (struct micros_frame_owner){0, 0, 0, 0};
    }
    return (struct micros_frame_owner){
        process.generation,
        process.slot,
        (uint8_t)owner_kind,
        0,
    };
}

static enum micros_frame_ownership_error resolve_vm_data_frames(
    const struct micros_frame_ownership *ownership,
    const uint64_t *data_physical_addresses,
    size_t data_physical_address_count,
    enum micros_frame_owner_kind data_owner_kind,
    struct micros_frame_owner expected_data_owner,
    uint64_t
        data_frame_indices[MICROS_APPLICATION_RESIDENT_PAGE_MAX]
)
{
    enum micros_frame_ownership_error error;
    size_t index;

    for (index = 0; index < data_physical_address_count; ++index) {
        uint64_t frame_index;
        struct micros_frame_owner owner;

        error = micros_frame_ownership_physical_address_to_index(
            ownership,
            data_physical_addresses[index],
            &frame_index
        );
        if (error != MICROS_FRAME_OWNERSHIP_OK) {
            return error;
        }
        if (micros_frame_ownership_frame_index_is_duplicate(
            data_frame_indices,
            index,
            frame_index
        )) {
            return MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION;
        }
        error = micros_frame_ownership_selected_record(ownership, frame_index, &owner);
        if (error != MICROS_FRAME_OWNERSHIP_OK) {
            return error;
        }
        if (owner.kind == MICROS_FRAME_OWNER_FREE) {
            return data_owner_kind
                    == MICROS_FRAME_OWNER_VM_TRANSFERABLE
                ? MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED
                : MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
        }
        if (!micros_frame_owner_is_equal(owner, expected_data_owner)) {
            return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
        }
        data_frame_indices[index] = frame_index;
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}

static enum micros_frame_ownership_error resolve_vm_table_frames(
    const struct micros_frame_ownership *ownership,
    const uint64_t
        table_physical_addresses[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    enum micros_frame_owner_kind table_owner_kind,
    struct micros_frame_owner expected_table_owner,
    const uint64_t
        data_frame_indices[MICROS_APPLICATION_RESIDENT_PAGE_MAX],
    size_t data_physical_address_count,
    uint64_t
        table_frame_indices[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT]
)
{
    enum micros_frame_ownership_error error;
    size_t index;

    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        uint64_t frame_index;
        struct micros_frame_owner owner;

        error = micros_frame_ownership_physical_address_to_index(
            ownership,
            table_physical_addresses[index],
            &frame_index
        );
        if (error != MICROS_FRAME_OWNERSHIP_OK) {
            return error;
        }
        if (
            micros_frame_ownership_frame_index_is_duplicate(
                data_frame_indices,
                data_physical_address_count,
                frame_index
            )
            || micros_frame_ownership_frame_index_is_duplicate(
                table_frame_indices,
                index,
                frame_index
            )
        ) {
            return MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION;
        }
        error = micros_frame_ownership_selected_record(ownership, frame_index, &owner);
        if (error != MICROS_FRAME_OWNERSHIP_OK) {
            return error;
        }
        if (
            table_owner_kind != MICROS_FRAME_OWNER_FREE
            && owner.kind == MICROS_FRAME_OWNER_FREE
        ) {
            return MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED;
        }
        if (!micros_frame_owner_is_equal(owner, expected_table_owner)) {
            return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
        }
        table_frame_indices[index] = frame_index;
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}

enum micros_frame_ownership_error
micros_frame_ownership_vm_process_set_preflight(
    const struct micros_frame_ownership *ownership,
    struct micros_process_handle process,
    const uint64_t *data_physical_addresses,
    size_t data_physical_address_count,
    const uint64_t
        table_physical_addresses[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    enum micros_frame_owner_kind data_owner_kind,
    enum micros_frame_owner_kind table_owner_kind,
    uint64_t
        data_frame_indices[MICROS_APPLICATION_RESIDENT_PAGE_MAX],
    uint64_t
        table_frame_indices[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT]
)
{
    struct micros_frame_owner expected_data_owner;
    struct micros_frame_owner expected_table_owner;
    enum micros_frame_ownership_error error;

    if (
        data_physical_addresses == NULL
        || table_physical_addresses == NULL
        || data_physical_address_count == 0
        || data_physical_address_count
            > MICROS_APPLICATION_RESIDENT_PAGE_MAX
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (!micros_frame_process_handle_is_valid(process)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_OWNER;
    }
    error = micros_frame_ownership_vm_transition_preflight(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    expected_data_owner = expected_vm_frame_owner(
        data_owner_kind,
        process
    );
    expected_table_owner = expected_vm_frame_owner(
        table_owner_kind,
        process
    );
    error = resolve_vm_data_frames(
        ownership,
        data_physical_addresses,
        data_physical_address_count,
        data_owner_kind,
        expected_data_owner,
        data_frame_indices
    );
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    return resolve_vm_table_frames(
        ownership,
        table_physical_addresses,
        table_owner_kind,
        expected_table_owner,
        data_frame_indices,
        data_physical_address_count,
        table_frame_indices
    );
}

static enum micros_frame_ownership_error validate_metadata_header(
    const struct micros_frame_ownership *ownership
)
{
    const struct micros_frame_allocator *allocator;
    uint64_t total = 0;
    size_t kind_index;
    enum micros_frame_ownership_error error =
        micros_frame_ownership_require_initialized(ownership);

    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    allocator = ownership->allocator;
    if (
        !allocator->initialized
        || (
            ownership->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
            && ownership->phase != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        )
        || ownership->managed_frame_count == 0
        || ownership->managed_frame_count > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
        || ownership->managed_range_count == 0
        || ownership->managed_range_count > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES
        || allocator->managed_frame_count != ownership->managed_frame_count
        || allocator->managed_range_count != ownership->managed_range_count
        || ownership->owned_frame_count > ownership->managed_frame_count
        || allocator->free_frame_count > ownership->managed_frame_count
        || allocator->free_frame_count != ownership->managed_frame_count - ownership->owned_frame_count
        || allocator->allocation_cursor > ownership->managed_frame_count
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    if (ownership->owner_counts[MICROS_FRAME_OWNER_FREE] != 0) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    for (kind_index = 0; kind_index < MICROS_FRAME_OWNER_KIND_COUNT; ++kind_index) {
        uint64_t count = ownership->owner_counts[kind_index];

        if (count > ownership->owned_frame_count - total) {
            return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
        }
        total += count;
    }
    if (total != ownership->owned_frame_count) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    if (ownership->phase == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF) {
        if (
            ownership->owner_counts[MICROS_FRAME_OWNER_KERNEL_TEMPORARY] != 0
            || ownership->owner_counts[MICROS_FRAME_OWNER_PROCESS_USER] != 0
        ) {
            return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
        }
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}

enum micros_frame_ownership_error micros_frame_ownership_validate_metadata(
    const struct micros_frame_ownership *ownership
)
{
    enum micros_frame_ownership_error error = validate_metadata_header(ownership);

    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    return micros_frame_ownership_active_geometry_matches(ownership)
        ? MICROS_FRAME_OWNERSHIP_OK : MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
}

enum micros_frame_ownership_error micros_frame_ownership_selected_record(
    const struct micros_frame_ownership *ownership,
    uint64_t frame_index,
    struct micros_frame_owner *owner
)
{
    struct micros_frame_owner observed;
    uint8_t target;
    bool allocated;
    enum micros_frame_ownership_error error;

    if (owner == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    error = validate_metadata_header(ownership);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (frame_index >= ownership->managed_frame_count) {
        return MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED;
    }
    observed = ownership->owners[frame_index];
    target = ownership->handoff_targets[frame_index];
    allocated = micros_frame_ownership_allocator_bit_is_set(ownership->allocator, frame_index);
    if (
        !micros_frame_owner_is_valid(observed)
        || allocated != (observed.kind != MICROS_FRAME_OWNER_FREE)
        || !micros_frame_ownership_handoff_target_is_valid(observed, target)
        || (
            observed.kind == MICROS_FRAME_OWNER_FREE
            && (target != MICROS_FRAME_HANDOFF_NONE || frame_index < ownership->allocator->allocation_cursor)
        )
        || (
            ownership->phase == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
            && (
                target != MICROS_FRAME_HANDOFF_NONE
                || observed.kind == MICROS_FRAME_OWNER_KERNEL_TEMPORARY
                || observed.kind == MICROS_FRAME_OWNER_PROCESS_USER
            )
        )
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    *owner = observed;
    return MICROS_FRAME_OWNERSHIP_OK;
}

enum micros_frame_ownership_error micros_frame_ownership_validate_active(
    const struct micros_frame_ownership *ownership
)
{
    uint64_t observed_counts[MICROS_FRAME_OWNER_KIND_COUNT] = {0};
    uint64_t observed_owned_count = 0;
    uint64_t observed_allocated_count = 0;
    size_t kind_index;
    enum micros_frame_ownership_error error = micros_frame_ownership_validate_metadata(ownership);

    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (!allocated_prefix_is_valid(ownership)) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    error = validate_active_frames(ownership, observed_counts, &observed_owned_count, &observed_allocated_count);
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return error;
    }
    if (
        observed_owned_count != ownership->owned_frame_count
        || observed_allocated_count != observed_owned_count
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    for (kind_index = 0; kind_index < MICROS_FRAME_OWNER_KIND_COUNT; ++kind_index) {
        if (ownership->owner_counts[kind_index] != observed_counts[kind_index]) {
            return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
        }
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}
