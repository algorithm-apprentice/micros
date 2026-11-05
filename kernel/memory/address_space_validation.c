#include "kernel/memory/address_space_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "micros/bootstrap_memory.h"
#include "micros/frame_allocator.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/sv39.h"

extern const unsigned char __kernel_text_start[];
extern const unsigned char __kernel_text_end[];
extern const unsigned char __kernel_rodata_start[];
extern const unsigned char __kernel_rodata_end[];
extern const unsigned char __kernel_writable_start[];
extern const unsigned char __kernel_writable_end[];

static const struct micros_kernel_page_table *table_at(uint64_t physical_address) {
    return (const struct micros_kernel_page_table *)(uintptr_t)physical_address;
}

static bool candidate_table_is_valid(
    const struct micros_kernel_address_space_private_state *state,
    const struct micros_frame_allocator *allocator,
    const struct micros_frame_ownership *ownership,
    uint64_t index
)
{
    uint64_t physical_address;
    uint16_t range_index;

    if (state == NULL || state->table_count > MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES
        || index >= state->table_count) {
        return false;
    }
    physical_address = state->table_frames[index];
    range_index = state->table_range_indices[index];
    return micros_kernel_address_space_table_frame_is_valid(
        allocator, ownership, index, range_index, physical_address
    );
}

static uint64_t allocator_allocated_bit_count(const struct micros_frame_allocator *allocator) {
    uint64_t allocated_count = 0;
    uint64_t bitmap_index;

    for (bitmap_index = 0; bitmap_index < allocator->managed_frame_count; ++bitmap_index) {
        size_t word_index = (size_t)(bitmap_index / 64);
        uint64_t bit = UINT64_C(1) << (bitmap_index % 64);

        if ((allocator->allocated_bitmap[word_index] & bit) != 0) {
            ++allocated_count;
        }
    }
    return allocated_count;
}

bool micros_kernel_address_space_allocator_is_pristine(
    const struct micros_frame_allocator *allocator) {
    uint64_t bitmap_index;

    if (allocator == NULL || !allocator->initialized || allocator->managed_frame_count == 0 ||
        allocator->free_frame_count != allocator->managed_frame_count) {
        return false;
    }
    for (bitmap_index = 0; bitmap_index < allocator->managed_frame_count; ++bitmap_index) {
        size_t word_index = (size_t)(bitmap_index / 64);
        uint64_t bit = UINT64_C(1) << (bitmap_index % 64);

        if ((allocator->allocated_bitmap[word_index] & bit) != 0) {
            return false;
        }
    }
    return true;
}

static bool frame_owners_are_equal(struct micros_frame_owner left,
                                   struct micros_frame_owner right) {
    return (left.generation == right.generation && left.slot == right.slot &&
            left.kind == right.kind && left.reserved == right.reserved);
}

bool micros_kernel_address_space_table_frame_is_valid(
    const struct micros_frame_allocator *allocator,
    const struct micros_frame_ownership *ownership,
    uint64_t frame_index,
    uint16_t range_index,
    uint64_t physical_address
)
{
    const struct micros_managed_frame_range *range;
    const struct micros_frame_range_snapshot *snapshot;
    struct micros_frame_owner owner;
    uint8_t target;
    uint64_t offset;
    uint64_t allocated_word;

    if (allocator == NULL || ownership == NULL || !allocator->initialized
        || allocator != micros_bootstrap_frame_allocator()
        || ownership != micros_frame_ownership_runtime_ledger()
        || ownership->allocator != allocator || ownership->allocator_identity != (uintptr_t)allocator
        || ownership->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || allocator->managed_frame_count == 0
        || allocator->managed_frame_count > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
        || allocator->free_frame_count > allocator->managed_frame_count
        || ownership->managed_frame_count != allocator->managed_frame_count
        || allocator->managed_range_count == 0
        || allocator->managed_range_count > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES
        || ownership->managed_range_count != allocator->managed_range_count
        || range_index >= allocator->managed_range_count
        || frame_index >= allocator->managed_frame_count
        || frame_index >= MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES) {
        return false;
    }
    range = &allocator->managed_ranges[range_index];
    snapshot = &ownership->managed_ranges[range_index];
    if (range->base != snapshot->base || range->frame_count != snapshot->frame_count
        || range->bitmap_offset != snapshot->bitmap_offset
        || range->base % MICROS_FRAME_SIZE != 0
        || range->bitmap_offset > allocator->managed_frame_count
        || range->frame_count > allocator->managed_frame_count - range->bitmap_offset
        || frame_index < range->bitmap_offset
        || frame_index - range->bitmap_offset >= range->frame_count) {
        return false;
    }
    offset = (frame_index - range->bitmap_offset) * MICROS_FRAME_SIZE;
    if (offset > UINT64_MAX - range->base || range->base + offset != physical_address) {
        return false;
    }
    owner = ownership->owners[frame_index];
    target = ownership->handoff_targets[frame_index];
    allocated_word = allocator->allocated_bitmap[frame_index / 64];
    return frame_owners_are_equal(
        owner, (struct micros_frame_owner){0, 0, MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE, 0}
    ) && target == MICROS_FRAME_HANDOFF_NONE
        && (allocated_word & (UINT64_C(1) << (frame_index % 64))) != 0;
}

static bool
validation_authority_is_valid(const struct micros_kernel_address_space_private_state *state,
                              const struct micros_frame_allocator *allocator,
                              const struct micros_frame_ownership *ownership,
                              struct micros_frame_owner *expected_owner) {
    return (state != NULL && allocator != NULL && ownership != NULL &&
            state->table_count <= MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES &&
            allocator->free_frame_count <= allocator->managed_frame_count &&
            micros_frame_owner_make_kernel(MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE, expected_owner) ==
                MICROS_FRAME_OWNERSHIP_OK &&
            micros_frame_ownership_validate(ownership) == MICROS_FRAME_OWNERSHIP_OK &&
            ownership->phase == MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP &&
            ownership->allocator == allocator &&
            ownership->owned_frame_count == state->table_count &&
            ownership->owner_counts[MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE] == state->table_count);
}

static bool
allocated_table_set_is_exact(const struct micros_kernel_address_space_private_state *state,
                             const struct micros_frame_allocator *allocator,
                             const struct micros_frame_ownership *ownership,
                             struct micros_frame_owner expected_owner) {
    uint64_t allocated_count = allocator->managed_frame_count - allocator->free_frame_count;
    uint64_t allocated_bit_count = allocator_allocated_bit_count(allocator);
    size_t table_index;

    if (expected_owner.kind != MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
        || allocated_count != allocated_bit_count || allocated_bit_count != state->table_count) {
        return false;
    }
    for (table_index = 0; table_index < (size_t)state->table_count; ++table_index) {
        if (!candidate_table_is_valid(state, allocator, ownership, table_index)) {
            return false;
        }
    }
    return true;
}

static bool mark_reachable_table(const struct micros_kernel_address_space_private_state *state,
                                 uint64_t index, bool *reachable) {
    if (!candidate_table_is_valid(
            state, micros_bootstrap_frame_allocator(), micros_frame_ownership_runtime_ledger(), index
        ) || reachable[index]) {
        return false;
    }
    reachable[index] = true;
    return true;
}

static bool child_index_from_entry(
    const struct micros_kernel_address_space_private_state *state,
    uint16_t identifier, uint64_t physical_address, uint64_t *index
)
{
    uint64_t selected;

    if (identifier == 0 || state->table_count > MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES
        || identifier > state->table_count) {
        return false;
    }
    selected = identifier - 1;
    if (state->table_frames[selected] != physical_address) {
        return false;
    }
    *index = selected;
    return true;
}

static bool mark_child_table(
    const struct micros_kernel_address_space_private_state *state,
    uint16_t identifier, uint64_t physical_address, bool *reachable
)
{
    uint64_t index;

    return child_index_from_entry(state, identifier, physical_address, &index)
        && mark_reachable_table(state, index, reachable);
}

static bool kernel_leaf_is_valid(struct micros_sv39_decoded_pte entry, unsigned level)
{
    return level < MICROS_SV39_LEVEL_COUNT
        && (entry.permissions & MICROS_SV39_PERMISSION_USER) == 0
        && entry.physical_address % (MICROS_SV39_PAGE_SIZE << (9 * level)) == 0;
}

static bool leaf_table_is_valid(const struct micros_kernel_page_table *leaf_table,
                                uint64_t *observed_leaf_count) {
    size_t leaf_index;

    for (leaf_index = 0; leaf_index < MICROS_SV39_TABLE_ENTRY_COUNT; ++leaf_index) {
        struct micros_sv39_decoded_pte leaf_entry;

        if (micros_sv39_decode_pte(leaf_table->entries[leaf_index], &leaf_entry) !=
                MICROS_SV39_OK ||
            leaf_entry.kind == MICROS_SV39_PTE_TABLE) {
            return false;
        }
        if (leaf_entry.kind == MICROS_SV39_PTE_LEAF) {
            if (!kernel_leaf_is_valid(leaf_entry, 0)) {
                return false;
            }
            ++*observed_leaf_count;
        }
    }
    return true;
}

static bool middle_table_is_valid(const struct micros_kernel_address_space_private_state *state,
                                  const struct micros_kernel_page_table *middle, uint16_t root_index, bool *reachable,
                                  uint64_t *observed_leaf_count) {
    size_t middle_index;

    for (middle_index = 0; middle_index < MICROS_SV39_TABLE_ENTRY_COUNT; ++middle_index) {
        struct micros_sv39_decoded_pte middle_entry;
        uint16_t identifier = state->level0_table_ids[root_index][middle_index];
        uint64_t before = *observed_leaf_count;

        if (micros_sv39_decode_pte(middle->entries[middle_index], &middle_entry) !=
            MICROS_SV39_OK) {
            return false;
        }
        if (middle_entry.kind == MICROS_SV39_PTE_ABSENT) {
            if (identifier != 0) {
                return false;
            }
            continue;
        }
        if (middle_entry.kind == MICROS_SV39_PTE_LEAF) {
            if (identifier != 0 || !kernel_leaf_is_valid(middle_entry, 1)) {
                return false;
            }
            ++*observed_leaf_count;
            continue;
        }
        if (!mark_child_table(state, identifier, middle_entry.physical_address, reachable)
            || !leaf_table_is_valid(table_at(middle_entry.physical_address), observed_leaf_count)
            || *observed_leaf_count == before) {
            return false;
        }
    }
    return true;
}

static bool table_tree_is_valid(const struct micros_kernel_address_space_private_state *state,
                                bool *reachable, uint64_t *observed_leaf_count) {
    const struct micros_kernel_page_table *root;
    size_t root_index;

    if (state == NULL || state->table_count == 0
        || !mark_reachable_table(state, 0, reachable)) {
        return false;
    }
    if (state->root_physical_address != state->table_frames[0]) {
        return false;
    }
    root = table_at(state->root_physical_address);
    for (root_index = 0; root_index < MICROS_SV39_TABLE_ENTRY_COUNT; ++root_index) {
        struct micros_sv39_decoded_pte root_entry;
        uint16_t identifier = state->level1_table_ids[root_index];
        uint64_t before = *observed_leaf_count;

        if (micros_sv39_decode_pte(root->entries[root_index], &root_entry) != MICROS_SV39_OK) {
            return false;
        }
        if (root_entry.kind == MICROS_SV39_PTE_ABSENT) {
            if (identifier != 0) {
                return false;
            }
            continue;
        }
        if (root_entry.kind == MICROS_SV39_PTE_LEAF) {
            if (identifier != 0 || !kernel_leaf_is_valid(root_entry, 2)) {
                return false;
            }
            ++*observed_leaf_count;
            continue;
        }
        if (!mark_child_table(state, identifier, root_entry.physical_address, reachable)
            || !middle_table_is_valid(state, table_at(root_entry.physical_address), (uint16_t)root_index, reachable,
                                      observed_leaf_count)
            || *observed_leaf_count == before) {
            return false;
        }
    }
    return true;
}

static bool
every_candidate_table_is_reachable(const struct micros_kernel_address_space_private_state *state,
                                   const bool *reachable) {
    size_t table_index;

    for (table_index = 0; table_index < (size_t)state->table_count; ++table_index) {
        if (!reachable[table_index]) {
            return false;
        }
    }
    return true;
}

static bool
tree_and_table_ownership_are_valid(const struct micros_kernel_address_space_private_state *state,
                                   const struct micros_frame_allocator *allocator) {
    bool reachable[MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES];
    const struct micros_frame_ownership *ownership = micros_frame_ownership_runtime_ledger();
    struct micros_frame_owner expected_owner;
    uint64_t observed_leaf_count = 0;
    size_t table_index;

    for (table_index = 0; table_index < MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES; ++table_index) {
        reachable[table_index] = false;
    }

    return (validation_authority_is_valid(state, allocator, ownership, &expected_owner) &&
            allocated_table_set_is_exact(state, allocator, ownership, expected_owner) &&
            table_tree_is_valid(state, reachable, &observed_leaf_count) &&
            observed_leaf_count == state->leaf_count &&
            every_candidate_table_is_reachable(state, reachable));
}

static bool lookup_mapping_details(const struct micros_kernel_address_space_private_state *state,
                                   uint64_t virtual_address, uint64_t *physical_address,
                                   uint32_t *permissions, unsigned *leaf_level) {
    uint64_t table_index = 0;
    uint16_t root_index;
    int level;

    if (state == NULL || state->table_count == 0 || state->table_count > MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES
        || physical_address == NULL || permissions == NULL || leaf_level == NULL
        || micros_sv39_vpn_index(virtual_address, 2, &root_index) != MICROS_SV39_OK) {
        return false;
    }
    for (level = 2; level >= 0; --level) {
        const struct micros_kernel_page_table *table;
        struct micros_sv39_decoded_pte decoded;
        uint64_t physical;
        uint16_t identifier;
        uint16_t index;

        if (table_index >= state->table_count) {
            return false;
        }
        physical = state->table_frames[table_index];
        if (level == 2 && physical != state->root_physical_address) {
            return false;
        }
        table = table_at(physical);
        if (micros_sv39_vpn_index(virtual_address, (unsigned int)level, &index) != MICROS_SV39_OK ||
            micros_sv39_decode_pte(table->entries[index], &decoded) != MICROS_SV39_OK) {
            return false;
        }
        if (decoded.kind == MICROS_SV39_PTE_ABSENT) {
            return false;
        }
        if (decoded.kind == MICROS_SV39_PTE_LEAF) {
            uint64_t size = MICROS_SV39_PAGE_SIZE << (9 * level);

            if (!kernel_leaf_is_valid(decoded, (unsigned)level)) {
                return false;
            }
            *physical_address = decoded.physical_address + virtual_address % size;
            *permissions = decoded.permissions;
            *leaf_level = (unsigned)level;
            return true;
        }
        if (level == 0) {
            return false;
        }
        identifier = level == 2 ? state->level1_table_ids[root_index] : state->level0_table_ids[root_index][index];
        if (!child_index_from_entry(state, identifier, decoded.physical_address, &table_index)) {
            return false;
        }
    }
    return false;
}

static bool lookup_mapping(const struct micros_kernel_address_space_private_state *state,
                           uint64_t virtual_address, uint64_t *physical_address,
                           uint32_t *permissions) {
    unsigned level;

    return lookup_mapping_details(state, virtual_address, physical_address, permissions, &level);
}

static unsigned range_leaf_level(uint64_t start, uint64_t end)
{
    if (start % UINT64_C(0x40000000) == 0 && end - start >= UINT64_C(0x40000000)) {
        return 2;
    }
    return start % UINT64_C(0x200000) == 0 && end - start >= UINT64_C(0x200000) ? 1U : 0U;
}

static bool range_matches(const struct micros_kernel_address_space_private_state *state,
                          uint64_t start, uint64_t end, uint32_t permissions,
                          uint64_t *expected_leaf_count) {
    uint64_t address;

    if (start >= end || (start | end) % MICROS_SV39_PAGE_SIZE != 0) {
        return false;
    }
    for (address = start; address < end;) {
        uint64_t mapped_physical_address;
        uint32_t mapped_permissions;
        unsigned level;
        uint64_t size;

        if (!lookup_mapping_details(state, address, &mapped_physical_address, &mapped_permissions, &level)
            || mapped_physical_address != address || mapped_permissions != permissions
            || level != range_leaf_level(address, end)) {
            return false;
        }
        size = MICROS_SV39_PAGE_SIZE << (9 * level);
        if (address % size != 0 || size > end - address || *expected_leaf_count == UINT64_MAX) {
            return false;
        }
        ++*expected_leaf_count;
        address += size;
    }
    return true;
}

static bool mapping_is_absent(const struct micros_kernel_address_space_private_state *state,
                              uint64_t virtual_address) {
    uint64_t physical_address;
    uint32_t permissions;

    return !lookup_mapping(state, virtual_address, &physical_address, &permissions);
}

static bool
fixed_mappings_are_valid(const struct micros_kernel_address_space_private_state *state,
                         uint64_t *expected_leaf_count) {
    return (
        range_matches(state, (uintptr_t)__kernel_text_start, (uintptr_t)__kernel_text_end,
                      MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE, expected_leaf_count) &&
        range_matches(state, (uintptr_t)__kernel_rodata_start, (uintptr_t)__kernel_rodata_end,
                      MICROS_SV39_PERMISSION_READ, expected_leaf_count) &&
        range_matches(state, (uintptr_t)__kernel_writable_start, (uintptr_t)__kernel_writable_end,
                      MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE, expected_leaf_count) &&
        range_matches(state, MICROS_RISCV_UART0_BASE,
                      MICROS_RISCV_UART0_BASE + MICROS_SV39_PAGE_SIZE,
                      MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE, expected_leaf_count) &&
        range_matches(state, MICROS_RISCV_PLIC_PRIORITY_PAGE_BASE,
                      MICROS_RISCV_PLIC_PRIORITY_PAGE_BASE + MICROS_SV39_PAGE_SIZE,
                      MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE, expected_leaf_count) &&
        range_matches(state, MICROS_RISCV_PLIC_SUPERVISOR_ENABLE_PAGE_BASE,
                      MICROS_RISCV_PLIC_SUPERVISOR_ENABLE_PAGE_BASE + MICROS_SV39_PAGE_SIZE,
                      MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE, expected_leaf_count) &&
        range_matches(state, MICROS_RISCV_PLIC_SUPERVISOR_CONTEXT_PAGE_BASE,
                      MICROS_RISCV_PLIC_SUPERVISOR_CONTEXT_PAGE_BASE + MICROS_SV39_PAGE_SIZE,
                      MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE, expected_leaf_count) &&
        mapping_is_absent(state, MICROS_RISCV_PLIC_PRIORITY_PAGE_BASE + MICROS_SV39_PAGE_SIZE) &&
        mapping_is_absent(state,
                          MICROS_RISCV_PLIC_SUPERVISOR_ENABLE_PAGE_BASE + MICROS_SV39_PAGE_SIZE) &&
        mapping_is_absent(state,
                          MICROS_RISCV_PLIC_SUPERVISOR_CONTEXT_PAGE_BASE - MICROS_SV39_PAGE_SIZE) &&
        mapping_is_absent(state,
                          MICROS_RISCV_PLIC_SUPERVISOR_CONTEXT_PAGE_BASE + MICROS_SV39_PAGE_SIZE));
}

static bool managed_ranges_are_mapped(const struct micros_kernel_address_space_private_state *state,
                                      const struct micros_frame_allocator *allocator,
                                      uint64_t *expected_leaf_count) {
    size_t index;

    for (index = 0; index < allocator->managed_range_count; ++index) {
        const struct micros_managed_frame_range *range = &allocator->managed_ranges[index];
        uint64_t end = range->base + (range->frame_count * MICROS_FRAME_SIZE);

        if (!range_matches(state, range->base, end,
                           MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE, expected_leaf_count)) {
            return false;
        }
    }
    return true;
}

static bool table_frames_are_mapped(const struct micros_kernel_address_space_private_state *state) {
    size_t index;

    for (index = 0; index < (size_t)state->table_count; ++index) {
        uint64_t physical_address = state->table_frames[index];
        uint64_t mapped_physical_address;
        uint32_t permissions;

        if (!lookup_mapping(state, physical_address, &mapped_physical_address, &permissions) ||
            mapped_physical_address != physical_address ||
            permissions != (MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE)) {
            return false;
        }
    }
    return true;
}

static bool
expected_mappings_are_valid(const struct micros_kernel_address_space_private_state *state,
                            const struct micros_frame_allocator *allocator) {
    uint64_t expected_leaf_count = 0;

    return (fixed_mappings_are_valid(state, &expected_leaf_count)
            && managed_ranges_are_mapped(state, allocator, &expected_leaf_count)
            && expected_leaf_count == state->leaf_count && table_frames_are_mapped(state));
}

bool micros_kernel_address_space_candidate_is_valid(
    const struct micros_kernel_address_space_private_state *state,
    const struct micros_frame_allocator *allocator) {
    return (tree_and_table_ownership_are_valid(state, allocator) &&
            expected_mappings_are_valid(state, allocator));
}
