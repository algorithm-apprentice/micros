#include "micros/kernel_address_space.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "kernel/memory/address_space_internal.h"
#include "micros/bootstrap_memory.h"
#include "micros/frame_allocator.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/sv39.h"

_Static_assert(
    sizeof(struct micros_kernel_page_table)
        == MICROS_SV39_PAGE_SIZE,
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

static struct micros_kernel_address_space_private_state
    kernel_address_space;

static struct micros_kernel_page_table *table_at(
    uint64_t physical_address
)
{
    return (struct micros_kernel_page_table *)(uintptr_t)
        physical_address;
}

static bool allocated_table_range(uint64_t physical_address, uint16_t *range_index)
{
    const struct micros_frame_allocator *allocator = micros_bootstrap_frame_allocator();
    const struct micros_frame_ownership *ownership = micros_frame_ownership_runtime_ledger();
    size_t index;

    if (allocator == NULL || ownership == NULL || range_index == NULL
        || allocator->managed_range_count > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES
        || allocator->managed_frame_count > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES) {
        return false;
    }
    for (index = 0; index < allocator->managed_range_count; ++index) {
        const struct micros_managed_frame_range *range = &allocator->managed_ranges[index];
        uint64_t frame_index;

        if (range->bitmap_offset > allocator->managed_frame_count
            || range->frame_count > allocator->managed_frame_count - range->bitmap_offset) {
            return false;
        }
        if (physical_address < range->base
            || physical_address - range->base >= range->frame_count * MICROS_FRAME_SIZE) {
            continue;
        }
        frame_index = range->bitmap_offset + (physical_address - range->base) / MICROS_FRAME_SIZE;
        if (frame_index != kernel_address_space.table_count
            || !micros_kernel_address_space_table_frame_is_valid(
                allocator, ownership, frame_index, (uint16_t)index, physical_address
            )) {
            return false;
        }
        *range_index = (uint16_t)index;
        return true;
    }
    return false;
}

static bool selected_table_is_valid(uint64_t index)
{
    if (kernel_address_space.table_count > MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES
        || index >= kernel_address_space.table_count) {
        return false;
    }
    {
        uint64_t physical_address = kernel_address_space.table_frames[index];
        uint16_t range_index = kernel_address_space.table_range_indices[index];

        if (index == 0 && physical_address != kernel_address_space.root_physical_address) {
            return false;
        }
        return micros_kernel_address_space_table_frame_is_valid(
            micros_bootstrap_frame_allocator(), micros_frame_ownership_runtime_ledger(),
            index, range_index, physical_address
        );
    }
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
    struct micros_kernel_page_table *table;
    uint64_t validation_pte;
    uint16_t range_index;
    enum micros_frame_ownership_error ownership_error;
    size_t index;

    if (physical_address == NULL) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE;
    }
    if (
        kernel_address_space.table_count
            >= MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES
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
    if (!allocated_table_range(*physical_address, &range_index)) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INVARIANT;
    }

    table = table_at(*physical_address);
    for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
        table->entries[index] = 0;
    }
    kernel_address_space.table_frames[
        kernel_address_space.table_count
    ] = *physical_address;
    kernel_address_space.table_range_indices[kernel_address_space.table_count] = range_index;
    ++kernel_address_space.table_count;
    return MICROS_KERNEL_ADDRESS_SPACE_OK;
}

static uint16_t *child_identifier(unsigned level, uint16_t root_index, uint16_t entry_index)
{
    if (root_index >= MICROS_SV39_TABLE_ENTRY_COUNT || entry_index >= MICROS_SV39_TABLE_ENTRY_COUNT) {
        return NULL;
    }
    if (level == 2) {
        return &kernel_address_space.level1_table_ids[root_index];
    }
    return level == 1 ? &kernel_address_space.level0_table_ids[root_index][entry_index] : NULL;
}

static enum micros_kernel_address_space_error create_child_table(
    struct micros_kernel_page_table *parent, unsigned level, uint16_t root_index,
    uint16_t entry_index, uint64_t *table_index
)
{
    uint16_t *identifier = child_identifier(level, root_index, entry_index);
    uint64_t physical_address;
    uint64_t pte;
    uint64_t slot;
    enum micros_kernel_address_space_error error;
    size_t index;

    if (identifier == NULL || *identifier != 0) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    error = allocate_table(&physical_address);
    if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) {
        return error;
    }
    if (micros_sv39_make_table_pte(physical_address, &pte) != MICROS_SV39_OK) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_PTE;
    }
    slot = kernel_address_space.table_count - 1;
    if (level == 2) {
        for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
            kernel_address_space.level0_table_ids[root_index][index] = 0;
        }
    }
    *identifier = (uint16_t)(slot + 1);
    parent->entries[entry_index] = pte;
    *table_index = slot;
    return MICROS_KERNEL_ADDRESS_SPACE_OK;
}

static enum micros_kernel_address_space_error select_child_table(
    struct micros_kernel_page_table *parent, unsigned level, uint16_t root_index,
    uint16_t entry_index, struct micros_sv39_decoded_pte decoded, uint64_t *table_index
)
{
    uint64_t selected;

    if (decoded.kind == MICROS_SV39_PTE_LEAF) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_CONFLICT;
    }
    if (decoded.kind == MICROS_SV39_PTE_ABSENT) {
        return create_child_table(parent, level, root_index, entry_index, table_index);
    }
    if (child_identifier(level, root_index, entry_index) == NULL) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    {
        uint16_t identifier = *child_identifier(level, root_index, entry_index);

        if (identifier == 0 || identifier > kernel_address_space.table_count
            || kernel_address_space.table_count > MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES) {
            return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INVARIANT;
        }
        selected = identifier - 1;
    }
    if (kernel_address_space.table_frames[selected] != decoded.physical_address) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    *table_index = selected;
    return MICROS_KERNEL_ADDRESS_SPACE_OK;
}

static enum micros_kernel_address_space_error map_leaf(
    uint64_t virtual_address, uint64_t physical_address, uint32_t permissions, unsigned leaf_level
)
{
    uint64_t table_index = 0;
    uint16_t root_index;
    int level;

    if (leaf_level >= MICROS_SV39_LEVEL_COUNT
        || ((virtual_address | physical_address) & ((MICROS_SV39_PAGE_SIZE << (9 * leaf_level)) - 1)) != 0
        || micros_sv39_vpn_index(virtual_address, 2, &root_index) != MICROS_SV39_OK) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE;
    }
    for (level = 2; level >= (int)leaf_level; --level) {
        struct micros_sv39_decoded_pte decoded;
        uint16_t index;

        if (!selected_table_is_valid(table_index)) {
            return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INVARIANT;
        }
        {
            struct micros_kernel_page_table *table = table_at(kernel_address_space.table_frames[table_index]);

            if (micros_sv39_vpn_index(virtual_address, (unsigned)level, &index) != MICROS_SV39_OK) {
                return MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE;
            }
            if (micros_sv39_decode_pte(table->entries[index], &decoded) != MICROS_SV39_OK) {
                return MICROS_KERNEL_ADDRESS_SPACE_ERROR_PTE;
            }
            if (level == (int)leaf_level) {
                uint64_t leaf_pte;
                uint16_t *identifier = child_identifier((unsigned)level, root_index, index);

                if (decoded.kind != MICROS_SV39_PTE_ABSENT) {
                    return MICROS_KERNEL_ADDRESS_SPACE_ERROR_CONFLICT;
                }
                if ((identifier != NULL && *identifier != 0) || kernel_address_space.leaf_count == UINT64_MAX) {
                    return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INVARIANT;
                }
                if (micros_sv39_make_leaf_pte(physical_address, permissions, &leaf_pte) != MICROS_SV39_OK) {
                    return MICROS_KERNEL_ADDRESS_SPACE_ERROR_PTE;
                }
                table->entries[index] = leaf_pte;
                ++kernel_address_space.leaf_count;
                return MICROS_KERNEL_ADDRESS_SPACE_OK;
            }
            {
                enum micros_kernel_address_space_error error =
                    select_child_table(table, (unsigned)level, root_index, index, decoded, &table_index);

                if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) {
                    return error;
                }
            }
        }
    }
    return MICROS_KERNEL_ADDRESS_SPACE_ERROR_INVARIANT;
}

static unsigned leaf_level_for_range(uint64_t start, uint64_t end)
{
    if (start % UINT64_C(0x40000000) == 0 && end - start >= UINT64_C(0x40000000)) {
        return 2;
    }
    return start % UINT64_C(0x200000) == 0 && end - start >= UINT64_C(0x200000) ? 1U : 0U;
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
        || (start < (UINT64_C(1) << 38) && end > (UINT64_C(1) << 38))
    ) {
        return MICROS_KERNEL_ADDRESS_SPACE_ERROR_RANGE;
    }
    for (address = start; address < end;) {
        unsigned level = leaf_level_for_range(address, end);
        enum micros_kernel_address_space_error error =
            map_leaf(address, address, permissions, level);

        if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) {
            return error;
        }
        address += MICROS_SV39_PAGE_SIZE << (9 * level);
    }
    return MICROS_KERNEL_ADDRESS_SPACE_OK;
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
    if (
        !micros_kernel_address_space_allocator_is_pristine(
            allocator
        )
    ) {
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
    error = map_identity_range(
        MICROS_RISCV_PLIC_PRIORITY_PAGE_BASE,
        MICROS_RISCV_PLIC_PRIORITY_PAGE_BASE
            + MICROS_SV39_PAGE_SIZE,
        MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE
    );
    if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) {
        return error;
    }
    error = map_identity_range(
        MICROS_RISCV_PLIC_SUPERVISOR_ENABLE_PAGE_BASE,
        MICROS_RISCV_PLIC_SUPERVISOR_ENABLE_PAGE_BASE
            + MICROS_SV39_PAGE_SIZE,
        MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE
    );
    if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) {
        return error;
    }
    error = map_identity_range(
        MICROS_RISCV_PLIC_SUPERVISOR_CONTEXT_PAGE_BASE,
        MICROS_RISCV_PLIC_SUPERVISOR_CONTEXT_PAGE_BASE
            + MICROS_SV39_PAGE_SIZE,
        MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE
    );
    if (error != MICROS_KERNEL_ADDRESS_SPACE_OK) {
        return error;
    }

    if (
        !micros_kernel_address_space_candidate_is_valid(
            &kernel_address_space,
            allocator
        )
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
