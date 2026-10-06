#include "micros/kernel_address_space.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "micros/bootstrap_memory.h"
#include "micros/frame_allocator.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/sv39.h"

enum {
    KERNEL_PAGE_TABLE_MAX_FRAMES = 1024,
};

struct kernel_page_table {
    uint64_t entries[MICROS_SV39_TABLE_ENTRY_COUNT];
};

struct kernel_address_space_state {
    bool initialized;
    uint64_t root_physical_address;
    uint64_t table_count;
    uint64_t leaf_count;
    uint64_t table_frames[KERNEL_PAGE_TABLE_MAX_FRAMES];
    struct micros_kernel_address_space_report report;
};

_Static_assert(
    sizeof(struct kernel_page_table) == MICROS_SV39_PAGE_SIZE,
    "an Sv39 page table must occupy one base page"
);

extern const unsigned char __kernel_start[];
extern const unsigned char __kernel_text_start[];
extern const unsigned char __kernel_text_end[];
extern const unsigned char __kernel_rodata_start[];
extern const unsigned char __kernel_rodata_end[];
extern const unsigned char __kernel_writable_start[];
extern const unsigned char __kernel_writable_end[];
extern const unsigned char __kernel_end[];

static struct kernel_address_space_state kernel_address_space;

static struct kernel_page_table *table_at(uint64_t physical_address)
{
    return (struct kernel_page_table *)(uintptr_t)physical_address;
}

static size_t table_frame_index(uint64_t physical_address)
{
    size_t index;

    for (
        index = 0;
        index < (size_t)kernel_address_space.table_count;
        ++index
    ) {
        if (
            kernel_address_space.table_frames[index]
                == physical_address
        ) {
            return index;
        }
    }
    return SIZE_MAX;
}

static bool allocator_frame_is_allocated(
    const struct micros_frame_allocator *allocator,
    uint64_t physical_address
)
{
    size_t range_index;

    if (
        allocator == NULL
        || (physical_address & (MICROS_FRAME_SIZE - 1)) != 0
    ) {
        return false;
    }
    for (
        range_index = 0;
        range_index < allocator->managed_range_count;
        ++range_index
    ) {
        const struct micros_managed_frame_range *range =
            &allocator->managed_ranges[range_index];
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
        frame_index =
            (physical_address - range->base) / MICROS_FRAME_SIZE;
        bitmap_index = range->bitmap_offset + frame_index;
        word_index = (size_t)(bitmap_index / 64);
        bit = UINT64_C(1) << (bitmap_index % 64);
        return (allocator->allocated_bitmap[word_index] & bit) != 0;
    }
    return false;
}

static bool allocator_is_pristine(
    const struct micros_frame_allocator *allocator
)
{
    uint64_t bitmap_index;

    if (
        allocator == NULL
        || !allocator->initialized
        || allocator->managed_frame_count == 0
        || allocator->free_frame_count
            != allocator->managed_frame_count
    ) {
        return false;
    }
    for (
        bitmap_index = 0;
        bitmap_index < allocator->managed_frame_count;
        ++bitmap_index
    ) {
        size_t word_index = (size_t)(bitmap_index / 64);
        uint64_t bit = UINT64_C(1) << (bitmap_index % 64);

        if ((allocator->allocated_bitmap[word_index] & bit) != 0) {
            return false;
        }
    }
    return true;
}

static uint64_t allocator_allocated_bit_count(
    const struct micros_frame_allocator *allocator
)
{
    uint64_t allocated_count = 0;
    uint64_t bitmap_index;

    for (
        bitmap_index = 0;
        bitmap_index < allocator->managed_frame_count;
        ++bitmap_index
    ) {
        size_t word_index = (size_t)(bitmap_index / 64);
        uint64_t bit = UINT64_C(1) << (bitmap_index % 64);

        if ((allocator->allocated_bitmap[word_index] & bit) != 0) {
            ++allocated_count;
        }
    }
    return allocated_count;
}

static bool linker_layout_is_valid(void)
{
    uint64_t kernel_start = (uintptr_t)__kernel_start;
    uint64_t text_start = (uintptr_t)__kernel_text_start;
    uint64_t text_end = (uintptr_t)__kernel_text_end;
    uint64_t rodata_start = (uintptr_t)__kernel_rodata_start;
    uint64_t rodata_end = (uintptr_t)__kernel_rodata_end;
    uint64_t writable_start = (uintptr_t)__kernel_writable_start;
    uint64_t writable_end = (uintptr_t)__kernel_writable_end;
    uint64_t kernel_end = (uintptr_t)__kernel_end;
    uint64_t boundaries = kernel_start
        | text_start
        | text_end
        | rodata_start
        | rodata_end
        | writable_start
        | writable_end
        | kernel_end;

    return (
        (boundaries & (MICROS_SV39_PAGE_SIZE - 1)) == 0
        && kernel_start == text_start
        && text_start < text_end
        && text_end == rodata_start
        && rodata_start < rodata_end
        && rodata_end == writable_start
        && writable_start < writable_end
        && writable_end == kernel_end
    );
}

static enum micros_kernel_address_space_error allocate_table(
    uint64_t *physical_address
)
{
    struct micros_frame_owner owner;
    struct kernel_page_table *table;
    uint64_t validation_pte;
    enum micros_frame_ownership_error ownership_error;
    size_t index;

    if (physical_address == NULL) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE;
    }
    if (
        kernel_address_space.table_count
            >= KERNEL_PAGE_TABLE_MAX_FRAMES
    ) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_CAPACITY;
    }
    if (
        micros_frame_owner_make_kernel(
            MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE,
            &owner
        ) != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    ownership_error = micros_frame_ownership_runtime_allocate(
        owner,
        physical_address
    );
    if (ownership_error == MICROS_FRAME_OWNERSHIP_ERROR_EXHAUSTED) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_ALLOCATION;
    }
    if (ownership_error != MICROS_FRAME_OWNERSHIP_OK) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    if (
        micros_sv39_make_table_pte(
            *physical_address,
            &validation_pte
        ) != MICROS_SV39_OK
    ) {
        if (
            micros_frame_ownership_runtime_release(
                owner,
                *physical_address
            ) != MICROS_FRAME_OWNERSHIP_OK
        ) {
            return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INVARIANT;
        }
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE;
    }

    table = table_at(*physical_address);
    for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
        table->entries[index] = 0;
    }
    kernel_address_space.table_frames[
        kernel_address_space.table_count
    ] = *physical_address;
    ++kernel_address_space.table_count;
    return MICROS_KERNEL_ADDRESS_SPACE_OK;
}

static enum micros_kernel_address_space_error map_page(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint32_t permissions
)
{
    uint64_t table_physical_address =
        kernel_address_space.root_physical_address;
    int level;

    for (level = MICROS_SV39_LEVEL_COUNT - 1; level > 0; --level) {
        struct kernel_page_table *table =
            table_at(table_physical_address);
        struct micros_sv39_decoded_pte decoded;
        uint16_t index;
        uint64_t child_physical_address;
        uint64_t child_pte;
        enum micros_sv39_error error;

        error = micros_sv39_vpn_index(
            virtual_address,
            (unsigned int)level,
            &index
        );
        if (error != MICROS_SV39_OK) {
            return MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE;
        }
        error = micros_sv39_decode_pte(
            table->entries[index],
            &decoded
        );
        if (error != MICROS_SV39_OK) {
            return MICROS_KERNEL_ADDRESS_SPACE_ERROR_PTE;
        }
        if (decoded.kind == MICROS_SV39_PTE_LEAF) {
            return MICROS_KERNEL_ADDRESS_SPACE_ERROR_CONFLICT;
        }
        if (decoded.kind == MICROS_SV39_PTE_TABLE) {
            if (
                table_frame_index(decoded.physical_address)
                    == SIZE_MAX
            ) {
                return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INVARIANT;
            }
            table_physical_address = decoded.physical_address;
            continue;
        }

        {
            enum micros_kernel_address_space_error allocation_error =
                allocate_table(&child_physical_address);

            if (
                allocation_error
                    != MICROS_KERNEL_ADDRESS_SPACE_OK
            ) {
                return allocation_error;
            }
        }
        if (
            micros_sv39_make_table_pte(
                child_physical_address,
                &child_pte
            ) != MICROS_SV39_OK
        ) {
            return MICROS_KERNEL_ADDRESS_SPACE_ERROR_PTE;
        }
        table->entries[index] = child_pte;
        table_physical_address = child_physical_address;
    }

    {
        struct kernel_page_table *table =
            table_at(table_physical_address);
        struct micros_sv39_decoded_pte decoded;
        uint16_t index;
        uint64_t leaf_pte;

        if (
            micros_sv39_vpn_index(virtual_address, 0, &index)
                != MICROS_SV39_OK
            || micros_sv39_decode_pte(
                table->entries[index],
                &decoded
            ) != MICROS_SV39_OK
        ) {
            return MICROS_KERNEL_ADDRESS_SPACE_ERROR_PTE;
        }
        if (decoded.kind != MICROS_SV39_PTE_ABSENT) {
            return MICROS_KERNEL_ADDRESS_SPACE_ERROR_CONFLICT;
        }
        if (
            micros_sv39_make_leaf_pte(
                physical_address,
                permissions,
                &leaf_pte
            ) != MICROS_SV39_OK
        ) {
            return MICROS_KERNEL_ADDRESS_SPACE_ERROR_PTE;
        }
        table->entries[index] = leaf_pte;
        ++kernel_address_space.leaf_count;
    }
    return MICROS_KERNEL_ADDRESS_SPACE_OK;
}

static enum micros_kernel_address_space_error map_identity_range(
    uint64_t start,
    uint64_t end,
    uint32_t permissions
)
{
    uint64_t address;

    if (
        start >= end
        || ((start | end) & (MICROS_SV39_PAGE_SIZE - 1)) != 0
        || !micros_sv39_virtual_address_is_canonical(start)
        || !micros_sv39_virtual_address_is_canonical(end - 1)
    ) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE;
    }
    for (
        address = start;
        address < end;
        address += MICROS_SV39_PAGE_SIZE
    ) {
        enum micros_kernel_address_space_error error =
            map_page(address, address, permissions);

        if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) {
            return error;
        }
    }
    return MICROS_KERNEL_ADDRESS_SPACE_OK;
}

static bool lookup_mapping(
    uint64_t virtual_address,
    uint64_t *physical_address,
    uint32_t *permissions
)
{
    uint64_t table_physical_address =
        kernel_address_space.root_physical_address;
    int level;

    if (
        physical_address == NULL
        || permissions == NULL
        || !micros_sv39_virtual_address_is_canonical(virtual_address)
    ) {
        return false;
    }
    for (level = MICROS_SV39_LEVEL_COUNT - 1; level >= 0; --level) {
        const struct kernel_page_table *table =
            table_at(table_physical_address);
        struct micros_sv39_decoded_pte decoded;
        uint16_t index;

        if (
            micros_sv39_vpn_index(
                virtual_address,
                (unsigned int)level,
                &index
            ) != MICROS_SV39_OK
            || micros_sv39_decode_pte(
                table->entries[index],
                &decoded
            ) != MICROS_SV39_OK
        ) {
            return false;
        }
        if (decoded.kind == MICROS_SV39_PTE_ABSENT) {
            return false;
        }
        if (decoded.kind == MICROS_SV39_PTE_LEAF) {
            if (level != 0) {
                return false;
            }
            *physical_address = decoded.physical_address
                | (virtual_address & (MICROS_SV39_PAGE_SIZE - 1));
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

static bool range_matches(
    uint64_t start,
    uint64_t end,
    uint32_t permissions
)
{
    uint64_t address;

    for (
        address = start;
        address < end;
        address += MICROS_SV39_PAGE_SIZE
    ) {
        uint64_t mapped_physical_address;
        uint32_t mapped_permissions;

        if (
            !lookup_mapping(
                address,
                &mapped_physical_address,
                &mapped_permissions
            )
            || mapped_physical_address != address
            || mapped_permissions != permissions
        ) {
            return false;
        }
    }
    return true;
}

static bool mark_reachable_table(
    uint64_t physical_address,
    bool *reachable
)
{
    size_t index = table_frame_index(physical_address);

    if (index == SIZE_MAX || reachable[index]) {
        return false;
    }
    reachable[index] = true;
    return true;
}

static bool tree_and_table_ownership_are_valid(
    const struct micros_frame_allocator *allocator
)
{
    bool reachable[KERNEL_PAGE_TABLE_MAX_FRAMES];
    const struct micros_frame_ownership *ownership =
        micros_frame_ownership_runtime_ledger();
    struct micros_frame_owner expected_owner;
    const struct kernel_page_table *root;
    uint64_t observed_leaf_count = 0;
    uint64_t allocated_count;
    uint64_t allocated_bit_count;
    size_t root_index;
    size_t table_index;

    for (
        table_index = 0;
        table_index < KERNEL_PAGE_TABLE_MAX_FRAMES;
        ++table_index
    ) {
        reachable[table_index] = false;
    }
    if (
        allocator == NULL
        || ownership == NULL
        || allocator->free_frame_count > allocator->managed_frame_count
        || micros_frame_owner_make_kernel(
            MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE,
            &expected_owner
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_validate(ownership)
            != MICROS_FRAME_OWNERSHIP_OK
        || ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || ownership->allocator != allocator
        || ownership->owned_frame_count
            != kernel_address_space.table_count
        || ownership->owner_counts[
            MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
        ] != kernel_address_space.table_count
    ) {
        return false;
    }
    allocated_count = allocator->managed_frame_count
        - allocator->free_frame_count;
    allocated_bit_count = allocator_allocated_bit_count(allocator);
    if (
        allocated_count != allocated_bit_count
        || allocated_bit_count != kernel_address_space.table_count
    ) {
        return false;
    }
    for (
        table_index = 0;
        table_index < (size_t)kernel_address_space.table_count;
        ++table_index
    ) {
        struct micros_frame_owner observed_owner;

        if (
            !allocator_frame_is_allocated(
                allocator,
                kernel_address_space.table_frames[table_index]
            )
            || micros_frame_ownership_lookup(
                ownership,
                kernel_address_space.table_frames[table_index],
                &observed_owner
            ) != MICROS_FRAME_OWNERSHIP_OK
            || observed_owner.generation
                != expected_owner.generation
            || observed_owner.slot != expected_owner.slot
            || observed_owner.kind != expected_owner.kind
            || observed_owner.reserved != expected_owner.reserved
        ) {
            return false;
        }
    }
    if (
        !mark_reachable_table(
            kernel_address_space.root_physical_address,
            reachable
        )
    ) {
        return false;
    }

    root = table_at(kernel_address_space.root_physical_address);
    for (
        root_index = 0;
        root_index < MICROS_SV39_TABLE_ENTRY_COUNT;
        ++root_index
    ) {
        struct micros_sv39_decoded_pte root_entry;
        const struct kernel_page_table *middle;
        size_t middle_index;

        if (
            micros_sv39_decode_pte(
                root->entries[root_index],
                &root_entry
            ) != MICROS_SV39_OK
        ) {
            return false;
        }
        if (root_entry.kind == MICROS_SV39_PTE_ABSENT) {
            continue;
        }
        if (
            root_entry.kind != MICROS_SV39_PTE_TABLE
            || !mark_reachable_table(
                root_entry.physical_address,
                reachable
            )
        ) {
            return false;
        }

        middle = table_at(root_entry.physical_address);
        for (
            middle_index = 0;
            middle_index < MICROS_SV39_TABLE_ENTRY_COUNT;
            ++middle_index
        ) {
            struct micros_sv39_decoded_pte middle_entry;
            const struct kernel_page_table *leaf_table;
            size_t leaf_index;

            if (
                micros_sv39_decode_pte(
                    middle->entries[middle_index],
                    &middle_entry
                ) != MICROS_SV39_OK
            ) {
                return false;
            }
            if (middle_entry.kind == MICROS_SV39_PTE_ABSENT) {
                continue;
            }
            if (
                middle_entry.kind != MICROS_SV39_PTE_TABLE
                || !mark_reachable_table(
                    middle_entry.physical_address,
                    reachable
                )
            ) {
                return false;
            }

            leaf_table = table_at(middle_entry.physical_address);
            for (
                leaf_index = 0;
                leaf_index < MICROS_SV39_TABLE_ENTRY_COUNT;
                ++leaf_index
            ) {
                struct micros_sv39_decoded_pte leaf_entry;

                if (
                    micros_sv39_decode_pte(
                        leaf_table->entries[leaf_index],
                        &leaf_entry
                    ) != MICROS_SV39_OK
                    || leaf_entry.kind == MICROS_SV39_PTE_TABLE
                ) {
                    return false;
                }
                if (leaf_entry.kind == MICROS_SV39_PTE_LEAF) {
                    if (
                        (
                            leaf_entry.permissions
                            & MICROS_SV39_PERMISSION_USER
                        ) != 0
                    ) {
                        return false;
                    }
                    ++observed_leaf_count;
                }
            }
        }
    }

    if (observed_leaf_count != kernel_address_space.leaf_count) {
        return false;
    }
    for (
        table_index = 0;
        table_index < (size_t)kernel_address_space.table_count;
        ++table_index
    ) {
        if (!reachable[table_index]) {
            return false;
        }
    }
    return true;
}

static enum micros_kernel_address_space_error map_kernel_image(void)
{
    enum micros_kernel_address_space_error error;

    error = map_identity_range(
        (uintptr_t)__kernel_text_start,
        (uintptr_t)__kernel_text_end,
        MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_EXECUTE
    );
    if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) {
        return error;
    }
    error = map_identity_range(
        (uintptr_t)__kernel_rodata_start,
        (uintptr_t)__kernel_rodata_end,
        MICROS_SV39_PERMISSION_READ
    );
    if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) {
        return error;
    }
    return map_identity_range(
        (uintptr_t)__kernel_writable_start,
        (uintptr_t)__kernel_writable_end,
        MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE
    );
}

static enum micros_kernel_address_space_error map_managed_memory(
    const struct micros_frame_allocator *allocator
)
{
    size_t index;

    for (index = 0; index < allocator->managed_range_count; ++index) {
        const struct micros_managed_frame_range *range =
            &allocator->managed_ranges[index];
        uint64_t size;
        uint64_t end;
        enum micros_kernel_address_space_error error;

        if (
            range->frame_count > UINT64_MAX / MICROS_FRAME_SIZE
        ) {
            return MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE;
        }
        size = range->frame_count * MICROS_FRAME_SIZE;
        if (size > UINT64_MAX - range->base) {
            return MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE;
        }
        end = range->base + size;
        error = map_identity_range(
            range->base,
            end,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
        );
        if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) {
            return error;
        }
    }
    return MICROS_KERNEL_ADDRESS_SPACE_OK;
}

static bool expected_mappings_are_valid(
    const struct micros_frame_allocator *allocator
)
{
    size_t index;

    if (
        !range_matches(
            (uintptr_t)__kernel_text_start,
            (uintptr_t)__kernel_text_end,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
        )
        || !range_matches(
            (uintptr_t)__kernel_rodata_start,
            (uintptr_t)__kernel_rodata_end,
            MICROS_SV39_PERMISSION_READ
        )
        || !range_matches(
            (uintptr_t)__kernel_writable_start,
            (uintptr_t)__kernel_writable_end,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
        )
        || !range_matches(
            MICROS_RISCV_UART0_BASE,
            MICROS_RISCV_UART0_BASE + MICROS_SV39_PAGE_SIZE,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
        )
    ) {
        return false;
    }
    for (index = 0; index < allocator->managed_range_count; ++index) {
        const struct micros_managed_frame_range *range =
            &allocator->managed_ranges[index];
        uint64_t end = range->base
            + (range->frame_count * MICROS_FRAME_SIZE);

        if (
            !range_matches(
                range->base,
                end,
                MICROS_SV39_PERMISSION_READ
                    | MICROS_SV39_PERMISSION_WRITE
            )
        ) {
            return false;
        }
    }
    for (
        index = 0;
        index < (size_t)kernel_address_space.table_count;
        ++index
    ) {
        uint64_t physical_address =
            kernel_address_space.table_frames[index];
        uint64_t mapped_physical_address;
        uint32_t permissions;

        if (
            !lookup_mapping(
                physical_address,
                &mapped_physical_address,
                &permissions
            )
            || mapped_physical_address != physical_address
            || permissions
                != (
                    MICROS_SV39_PERMISSION_READ
                    | MICROS_SV39_PERMISSION_WRITE
                )
        ) {
            return false;
        }
    }
    return true;
}

enum micros_kernel_address_space_error
micros_kernel_address_space_initialize(void)
{
    const struct micros_frame_allocator *allocator =
        micros_bootstrap_frame_allocator();
    uint64_t observed_satp;
    uint64_t expected_satp;
    enum micros_kernel_address_space_error error;

    if (kernel_address_space.initialized) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_ALREADY_INITIALIZED;
    }
    if (riscv_irq_is_enabled()) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INTERRUPTS_ENABLED;
    }
    if (!linker_layout_is_valid()) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_LINKER_LAYOUT;
    }
    if (!allocator_is_pristine(allocator)) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_ALLOCATOR_STATE;
    }

    error = allocate_table(
        &kernel_address_space.root_physical_address
    );
    if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) {
        return error;
    }
    error = map_kernel_image();
    if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) {
        return error;
    }
    error = map_managed_memory(allocator);
    if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) {
        return error;
    }
    error = map_identity_range(
        MICROS_RISCV_UART0_BASE,
        MICROS_RISCV_UART0_BASE + MICROS_SV39_PAGE_SIZE,
        MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE
    );
    if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) {
        return error;
    }

    if (
        !tree_and_table_ownership_are_valid(allocator)
        || !expected_mappings_are_valid(allocator)
    ) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    if (riscv_irq_is_enabled()) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INTERRUPTS_ENABLED;
    }

    expected_satp = MICROS_RISCV_SATP_MODE_SV39
        | (kernel_address_space.root_physical_address >> 12);
    observed_satp = micros_riscv_activate_sv39(
        kernel_address_space.root_physical_address
    );
    if (observed_satp != expected_satp) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_SATP;
    }

    kernel_address_space.report.root_physical_address =
        kernel_address_space.root_physical_address;
    kernel_address_space.report.table_count =
        kernel_address_space.table_count;
    kernel_address_space.initialized = true;
    return MICROS_KERNEL_ADDRESS_SPACE_OK;
}

const struct micros_kernel_address_space_report *
micros_kernel_address_space_report(void)
{
    if (!kernel_address_space.initialized) {
        return NULL;
    }
    return &kernel_address_space.report;
}
