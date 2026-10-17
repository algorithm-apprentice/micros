#include "kernel/memory/address_space_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/platform.h"
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

static size_t candidate_table_index(const struct micros_kernel_address_space_private_state *state,
                                    uint64_t physical_address) {
    size_t index;

    for (index = 0; index < (size_t)state->table_count; ++index) {
        if (state->table_frames[index] == physical_address) {
            return index;
        }
    }
    return SIZE_MAX;
}

static bool allocator_frame_is_allocated(const struct micros_frame_allocator *allocator,
                                         uint64_t physical_address) {
    size_t range_index;

    if (allocator == NULL || (physical_address & (MICROS_FRAME_SIZE - 1)) != 0) {
        return false;
    }
    for (range_index = 0; range_index < allocator->managed_range_count; ++range_index) {
        const struct micros_managed_frame_range *range = &allocator->managed_ranges[range_index];
        uint64_t range_size = range->frame_count * MICROS_FRAME_SIZE;
        uint64_t frame_index;
        uint64_t bitmap_index;
        size_t word_index;
        uint64_t bit;

        if (physical_address < range->base) {
            break;
        }
        if (physical_address - range->base >= range_size) {
            continue;
        }
        frame_index = (physical_address - range->base) / MICROS_FRAME_SIZE;
        bitmap_index = range->bitmap_offset + frame_index;
        word_index = (size_t)(bitmap_index / 64);
        bit = UINT64_C(1) << (bitmap_index % 64);
        return (allocator->allocated_bitmap[word_index] & bit) != 0;
    }
    return false;
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

    if (allocated_count != allocated_bit_count || allocated_bit_count != state->table_count) {
        return false;
    }
    for (table_index = 0; table_index < (size_t)state->table_count; ++table_index) {
        struct micros_frame_owner observed_owner;
        uint64_t physical_address = state->table_frames[table_index];

        if (!allocator_frame_is_allocated(allocator, physical_address) ||
            micros_frame_ownership_lookup(ownership, physical_address, &observed_owner) !=
                MICROS_FRAME_OWNERSHIP_OK ||
            !frame_owners_are_equal(observed_owner, expected_owner)) {
            return false;
        }
    }
    return true;
}

static bool mark_reachable_table(const struct micros_kernel_address_space_private_state *state,
                                 uint64_t physical_address, bool *reachable) {
    size_t index = candidate_table_index(state, physical_address);

    if (index == SIZE_MAX || reachable[index]) {
        return false;
    }
    reachable[index] = true;
    return true;
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
            if ((leaf_entry.permissions & MICROS_SV39_PERMISSION_USER) != 0) {
                return false;
            }
            ++*observed_leaf_count;
        }
    }
    return true;
}

static bool middle_table_is_valid(const struct micros_kernel_address_space_private_state *state,
                                  const struct micros_kernel_page_table *middle, bool *reachable,
                                  uint64_t *observed_leaf_count) {
    size_t middle_index;

    for (middle_index = 0; middle_index < MICROS_SV39_TABLE_ENTRY_COUNT; ++middle_index) {
        struct micros_sv39_decoded_pte middle_entry;

        if (micros_sv39_decode_pte(middle->entries[middle_index], &middle_entry) !=
            MICROS_SV39_OK) {
            return false;
        }
        if (middle_entry.kind == MICROS_SV39_PTE_ABSENT) {
            continue;
        }
        if (middle_entry.kind != MICROS_SV39_PTE_TABLE ||
            !mark_reachable_table(state, middle_entry.physical_address, reachable) ||
            !leaf_table_is_valid(table_at(middle_entry.physical_address), observed_leaf_count)) {
            return false;
        }
    }
    return true;
}

static bool table_tree_is_valid(const struct micros_kernel_address_space_private_state *state,
                                bool *reachable, uint64_t *observed_leaf_count) {
    const struct micros_kernel_page_table *root;
    size_t root_index;

    if (!mark_reachable_table(state, state->root_physical_address, reachable)) {
        return false;
    }
    root = table_at(state->root_physical_address);
    for (root_index = 0; root_index < MICROS_SV39_TABLE_ENTRY_COUNT; ++root_index) {
        struct micros_sv39_decoded_pte root_entry;

        if (micros_sv39_decode_pte(root->entries[root_index], &root_entry) != MICROS_SV39_OK) {
            return false;
        }
        if (root_entry.kind == MICROS_SV39_PTE_ABSENT) {
            continue;
        }
        if (root_entry.kind != MICROS_SV39_PTE_TABLE ||
            !mark_reachable_table(state, root_entry.physical_address, reachable) ||
            !middle_table_is_valid(state, table_at(root_entry.physical_address), reachable,
                                   observed_leaf_count)) {
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

static bool lookup_mapping(const struct micros_kernel_address_space_private_state *state,
                           uint64_t virtual_address, uint64_t *physical_address,
                           uint32_t *permissions) {
    uint64_t table_physical_address = state->root_physical_address;
    int level;

    if (physical_address == NULL || permissions == NULL ||
        !micros_sv39_virtual_address_is_canonical(virtual_address)) {
        return false;
    }
    for (level = MICROS_SV39_LEVEL_COUNT - 1; level >= 0; --level) {
        const struct micros_kernel_page_table *table = table_at(table_physical_address);
        struct micros_sv39_decoded_pte decoded;
        uint16_t index;

        if (micros_sv39_vpn_index(virtual_address, (unsigned int)level, &index) != MICROS_SV39_OK ||
            micros_sv39_decode_pte(table->entries[index], &decoded) != MICROS_SV39_OK) {
            return false;
        }
        if (decoded.kind == MICROS_SV39_PTE_ABSENT) {
            return false;
        }
        if (decoded.kind == MICROS_SV39_PTE_LEAF) {
            if (level != 0) {
                return false;
            }
            *physical_address =
                decoded.physical_address | (virtual_address & (MICROS_SV39_PAGE_SIZE - 1));
            *permissions = decoded.permissions;
            return true;
        }
        if (level == 0) {
            return false;
        }
        table_physical_address = decoded.physical_address;
    }
    return false;
}

static bool range_matches(const struct micros_kernel_address_space_private_state *state,
                          uint64_t start, uint64_t end, uint32_t permissions) {
    uint64_t address;

    for (address = start; address < end; address += MICROS_SV39_PAGE_SIZE) {
        uint64_t mapped_physical_address;
        uint32_t mapped_permissions;

        if (!lookup_mapping(state, address, &mapped_physical_address, &mapped_permissions) ||
            mapped_physical_address != address || mapped_permissions != permissions) {
            return false;
        }
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
fixed_mappings_are_valid(const struct micros_kernel_address_space_private_state *state) {
    return (
        range_matches(state, (uintptr_t)__kernel_text_start, (uintptr_t)__kernel_text_end,
                      MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE) &&
        range_matches(state, (uintptr_t)__kernel_rodata_start, (uintptr_t)__kernel_rodata_end,
                      MICROS_SV39_PERMISSION_READ) &&
        range_matches(state, (uintptr_t)__kernel_writable_start, (uintptr_t)__kernel_writable_end,
                      MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE) &&
        range_matches(state, MICROS_RISCV_UART0_BASE,
                      MICROS_RISCV_UART0_BASE + MICROS_SV39_PAGE_SIZE,
                      MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE) &&
        range_matches(state, MICROS_RISCV_PLIC_PRIORITY_PAGE_BASE,
                      MICROS_RISCV_PLIC_PRIORITY_PAGE_BASE + MICROS_SV39_PAGE_SIZE,
                      MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE) &&
        range_matches(state, MICROS_RISCV_PLIC_SUPERVISOR_ENABLE_PAGE_BASE,
                      MICROS_RISCV_PLIC_SUPERVISOR_ENABLE_PAGE_BASE + MICROS_SV39_PAGE_SIZE,
                      MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE) &&
        range_matches(state, MICROS_RISCV_PLIC_SUPERVISOR_CONTEXT_PAGE_BASE,
                      MICROS_RISCV_PLIC_SUPERVISOR_CONTEXT_PAGE_BASE + MICROS_SV39_PAGE_SIZE,
                      MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE) &&
        mapping_is_absent(state, MICROS_RISCV_PLIC_PRIORITY_PAGE_BASE + MICROS_SV39_PAGE_SIZE) &&
        mapping_is_absent(state,
                          MICROS_RISCV_PLIC_SUPERVISOR_ENABLE_PAGE_BASE + MICROS_SV39_PAGE_SIZE) &&
        mapping_is_absent(state,
                          MICROS_RISCV_PLIC_SUPERVISOR_CONTEXT_PAGE_BASE - MICROS_SV39_PAGE_SIZE) &&
        mapping_is_absent(state,
                          MICROS_RISCV_PLIC_SUPERVISOR_CONTEXT_PAGE_BASE + MICROS_SV39_PAGE_SIZE));
}

static bool managed_ranges_are_mapped(const struct micros_kernel_address_space_private_state *state,
                                      const struct micros_frame_allocator *allocator) {
    size_t index;

    for (index = 0; index < allocator->managed_range_count; ++index) {
        const struct micros_managed_frame_range *range = &allocator->managed_ranges[index];
        uint64_t end = range->base + (range->frame_count * MICROS_FRAME_SIZE);

        if (!range_matches(state, range->base, end,
                           MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE)) {
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
    return (fixed_mappings_are_valid(state) && managed_ranges_are_mapped(state, allocator) &&
            table_frames_are_mapped(state));
}

bool micros_kernel_address_space_candidate_is_valid(
    const struct micros_kernel_address_space_private_state *state,
    const struct micros_frame_allocator *allocator) {
    return (tree_and_table_ownership_are_valid(state, allocator) &&
            expected_mappings_are_valid(state, allocator));
}
