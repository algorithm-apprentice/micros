#include "kernel/user_address_space_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "micros/sv39.h"

_Static_assert(sizeof(struct micros_user_address_space_page_table) == MICROS_SV39_PAGE_SIZE,
               "user page tables must occupy one base page");

struct micros_user_address_space_page_table *
micros_user_address_space_table_at(uint64_t physical_address) {
    return (struct micros_user_address_space_page_table *)(uintptr_t)physical_address;
}

void micros_user_address_space_clear_page(uint64_t physical_address) {
    uint64_t *words = (uint64_t *)(uintptr_t)physical_address;
    size_t index;

    for (index = 0; index < MICROS_SV39_PAGE_SIZE / sizeof(uint64_t); ++index) {
        words[index] = 0;
    }
}

bool micros_user_address_space_user_page_is_valid(uint64_t virtual_address) {
    return (virtual_address >= MICROS_USER_VIRTUAL_BASE &&
            virtual_address < MICROS_USER_VIRTUAL_END &&
            virtual_address % MICROS_SV39_PAGE_SIZE == 0 &&
            micros_sv39_virtual_address_is_canonical(virtual_address));
}

bool micros_user_address_space_user_byte_address_is_valid(uint64_t virtual_address) {
    return (virtual_address >= MICROS_USER_VIRTUAL_BASE &&
            virtual_address < MICROS_USER_VIRTUAL_END &&
            micros_sv39_virtual_address_is_canonical(virtual_address));
}

bool micros_user_address_space_permissions_are_valid(uint32_t permissions) {
    const uint32_t allowed =
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE | MICROS_SV39_PERMISSION_EXECUTE;

    return (permissions != 0 && (permissions & ~allowed) == 0 &&
            ((permissions & MICROS_SV39_PERMISSION_WRITE) == 0 ||
             (permissions & MICROS_SV39_PERMISSION_READ) != 0) &&
            ((permissions & (MICROS_SV39_PERMISSION_WRITE | MICROS_SV39_PERMISSION_EXECUTE)) !=
             (MICROS_SV39_PERMISSION_WRITE | MICROS_SV39_PERMISSION_EXECUTE)));
}

bool micros_user_address_space_frame_index(const struct micros_frame_ownership *ledger,
                                           uint64_t physical_address, uint64_t *frame_index) {
    size_t range_index;

    if (ledger == NULL || frame_index == NULL || physical_address % MICROS_SV39_PAGE_SIZE != 0) {
        return false;
    }
    for (range_index = 0; range_index < ledger->managed_range_count; ++range_index) {
        const struct micros_frame_range_snapshot *range = &ledger->managed_ranges[range_index];
        uint64_t offset;
        uint64_t index_in_range;

        if (physical_address < range->base) {
            continue;
        }
        offset = physical_address - range->base;
        index_in_range = offset / MICROS_SV39_PAGE_SIZE;
        if (index_in_range < range->frame_count) {
            *frame_index = range->bitmap_offset + index_in_range;
            return *frame_index < ledger->managed_frame_count;
        }
    }
    return false;
}

bool micros_user_address_space_physical_address(const struct micros_frame_ownership *ledger,
                                                uint64_t frame_index, uint64_t *physical_address) {
    size_t range_index;

    if (ledger == NULL || physical_address == NULL || frame_index >= ledger->managed_frame_count) {
        return false;
    }
    for (range_index = 0; range_index < ledger->managed_range_count; ++range_index) {
        const struct micros_frame_range_snapshot *range = &ledger->managed_ranges[range_index];

        if (frame_index >= range->bitmap_offset &&
            frame_index < range->bitmap_offset + range->frame_count) {
            *physical_address =
                range->base + (frame_index - range->bitmap_offset) * MICROS_SV39_PAGE_SIZE;
            return true;
        }
    }
    return false;
}

enum micros_user_address_space_error
micros_user_address_space_decode_pte(uint64_t pte, struct micros_sv39_decoded_pte *decoded) {
    if (micros_sv39_decode_pte(pte, decoded) != MICROS_SV39_OK) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error micros_user_address_space_read_page_indices(
    uint64_t virtual_address, struct micros_user_address_space_page_indices *indices) {
    if (indices == NULL ||
        micros_sv39_vpn_index(virtual_address, 2, &indices->root) != MICROS_SV39_OK ||
        indices->root != MICROS_USER_ADDRESS_SPACE_USER_ROOT_INDEX ||
        micros_sv39_vpn_index(virtual_address, 1, &indices->middle) != MICROS_SV39_OK ||
        micros_sv39_vpn_index(virtual_address, 0, &indices->leaf) != MICROS_SV39_OK) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

void micros_user_address_space_clear_bitmap(uint64_t *bitmap, size_t word_count) {
    size_t index;

    for (index = 0; index < word_count; ++index) {
        bitmap[index] = 0;
    }
}

bool micros_user_address_space_bitmap_contains(const uint64_t *bitmap, size_t word_count,
                                               uint64_t frame_index) {
    size_t word_index = (size_t)(frame_index / 64);

    return (bitmap != NULL && word_index < word_count &&
            (bitmap[word_index] & (UINT64_C(1) << (frame_index % 64))) != 0);
}

bool micros_user_address_space_bitmap_add(uint64_t *bitmap, size_t word_count,
                                          uint64_t frame_index) {
    size_t word_index = (size_t)(frame_index / 64);

    if (bitmap == NULL || word_index >= word_count) {
        return false;
    }
    bitmap[word_index] |= UINT64_C(1) << (frame_index % 64);
    return true;
}
