#include "micros/kernel_address_space.h"

#include <stdint.h>

#include "arch/riscv64/trap_context.h"
#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "kernel/memory/address_space_internal.h"
#include "micros/bootstrap_memory.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/panic.h"

static enum micros_mmu_test_trap_result mapping_test_handle_trap(struct micros_trap_frame *frame);
static bool mapping_test_run(void);

#define MICROS_SCAUSE_INTERRUPT (UINT64_C(1) << 63)
#define MICROS_SCAUSE_CODE_MASK (MICROS_SCAUSE_INTERRUPT - 1)

enum {
    MMU_TEST_EXCEPTION_INSTRUCTION_PAGE_FAULT = 12,
    MMU_TEST_EXCEPTION_STORE_PAGE_FAULT = 15,
};

enum mmu_test_state {
    MMU_TEST_IDLE,
    MMU_TEST_STORE_ARMED,
    MMU_TEST_STORE_HANDLED,
    MMU_TEST_EXECUTE_ARMED,
    MMU_TEST_EXECUTE_HANDLED,
};

void micros_mmu_store_test_trigger(void);
void micros_mmu_execute_test_trigger(void);

extern const unsigned char micros_mmu_text_probe[];
extern const unsigned char micros_mmu_store_fault[];
extern const unsigned char micros_mmu_store_resume[];
extern const unsigned char micros_mmu_writable_probe[];
extern const unsigned char micros_mmu_execute_resume[];

static volatile enum mmu_test_state test_state;
static volatile uint32_t test_trap_count;

static bool frame_has_common_expected_state(
    const struct micros_trap_frame *frame
)
{
    return (
        frame != NULL
        && (frame->scause & MICROS_SCAUSE_INTERRUPT) == 0
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0
        && (frame->sstatus & MICROS_RISCV_SSTATUS_SIE) == 0
        && frame->hart_context != 0
    );
}

enum micros_mmu_test_trap_result
micros_kernel_address_space_handle_test_trap(
    struct micros_trap_frame *frame
)
{
    uint64_t cause;
    enum micros_mmu_test_trap_result mapping_result = mapping_test_handle_trap(frame);

    if (mapping_result != MICROS_MMU_TEST_TRAP_INACTIVE) {
        return mapping_result;
    }

    if (
        test_state != MMU_TEST_STORE_ARMED
        && test_state != MMU_TEST_EXECUTE_ARMED
    ) {
        return MICROS_MMU_TEST_TRAP_INACTIVE;
    }
    if (!frame_has_common_expected_state(frame)) {
        return MICROS_MMU_TEST_TRAP_MISMATCH;
    }
    cause = frame->scause & MICROS_SCAUSE_CODE_MASK;

    if (test_state == MMU_TEST_STORE_ARMED) {
        if (
            cause != MMU_TEST_EXCEPTION_STORE_PAGE_FAULT
            || frame->sepc != (uintptr_t)micros_mmu_store_fault
            || frame->stval != (uintptr_t)micros_mmu_text_probe
        ) {
            return MICROS_MMU_TEST_TRAP_MISMATCH;
        }
        ++test_trap_count;
        test_state = MMU_TEST_STORE_HANDLED;
        frame->sepc = (uintptr_t)micros_mmu_store_resume;
        return MICROS_MMU_TEST_TRAP_HANDLED;
    }

    if (
        cause != MMU_TEST_EXCEPTION_INSTRUCTION_PAGE_FAULT
        || frame->sepc != (uintptr_t)micros_mmu_writable_probe
        || frame->stval != (uintptr_t)micros_mmu_writable_probe
    ) {
        return MICROS_MMU_TEST_TRAP_MISMATCH;
    }
    ++test_trap_count;
    test_state = MMU_TEST_EXECUTE_HANDLED;
    frame->sepc = (uintptr_t)micros_mmu_execute_resume;
    return MICROS_MMU_TEST_TRAP_HANDLED;
}

bool micros_kernel_address_space_run_self_test(void)
{
    test_trap_count = 0;
    test_state = MMU_TEST_STORE_ARMED;
    micros_mmu_store_test_trigger();
    if (
        test_state != MMU_TEST_STORE_HANDLED
        || test_trap_count != 1
    ) {
        test_state = MMU_TEST_IDLE;
        return false;
    }

    test_state = MMU_TEST_EXECUTE_ARMED;
    micros_mmu_execute_test_trigger();
    if (
        test_state != MMU_TEST_EXECUTE_HANDLED
        || test_trap_count != 2
    ) {
        test_state = MMU_TEST_IDLE;
        return false;
    }
    test_state = MMU_TEST_IDLE;
    return mapping_test_run();
}

enum mapping_test_phase {
    MAPPING_TEST_IDLE,
    MAPPING_TEST_LOAD_ARMED,
    MAPPING_TEST_LOAD_HANDLED,
};

struct mapping_test_observation {
    uint64_t tables;
    uint64_t leaves[3];
    bool largest;
};

struct mapping_test_resources {
    uint64_t root;
    uint64_t tables;
    uint64_t free_frames;
    uint64_t cursor;
    uint64_t owned;
    enum micros_frame_ownership_phase phase;
};

extern const unsigned char __kernel_text_start[];
extern const unsigned char __kernel_text_end[];
extern const unsigned char __kernel_rodata_start[];
extern const unsigned char __kernel_rodata_end[];
extern const unsigned char __kernel_writable_start[];
extern const unsigned char __kernel_writable_end[];
extern const unsigned char micros_mmu_large_load_fault[];
extern const unsigned char micros_mmu_large_load_resume[];
uint64_t micros_mmu_large_load_test_trigger(uint64_t virtual_address);

static const uint64_t mapping_test_sentinel = UINT64_C(0x4d454d30324c4546);
static volatile enum mapping_test_phase mapping_phase;
static volatile uint64_t mapping_expected_address;
static volatile uint32_t mapping_fault_count;
static bool mapping_seen[MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES];

static enum micros_mmu_test_trap_result mapping_test_handle_trap(struct micros_trap_frame *frame)
{
    if (mapping_phase != MAPPING_TEST_LOAD_ARMED) {
        return MICROS_MMU_TEST_TRAP_INACTIVE;
    }
    if (test_state != MMU_TEST_IDLE || !frame_has_common_expected_state(frame)
        || frame->scause != 13
        || frame->sepc != (uintptr_t)micros_mmu_large_load_fault
        || frame->stval != mapping_expected_address) {
        return MICROS_MMU_TEST_TRAP_MISMATCH;
    }
    ++mapping_fault_count;
    mapping_phase = MAPPING_TEST_LOAD_HANDLED;
    frame->sepc = (uintptr_t)micros_mmu_large_load_resume;
    return MICROS_MMU_TEST_TRAP_HANDLED;
}

static bool mapping_kernel_frame_index(uint64_t physical, uint64_t *frame_index)
{
    const struct micros_frame_ownership *ledger = micros_frame_ownership_runtime_ledger();
    const struct micros_kernel_address_space_report *report = micros_kernel_address_space_report();
    size_t index;

    if (ledger == NULL || report == NULL || physical % MICROS_SV39_PAGE_SIZE != 0
        || ledger->managed_range_count > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES) {
        return false;
    }
    for (index = 0; index < ledger->managed_range_count; ++index) {
        const struct micros_frame_range_snapshot *range = &ledger->managed_ranges[index];
        uint64_t selected;
        struct micros_frame_owner owner;

        if (physical < range->base
            || physical - range->base >= range->frame_count * MICROS_FRAME_SIZE) {
            continue;
        }
        selected = range->bitmap_offset + (physical - range->base) / MICROS_FRAME_SIZE;
        if (selected >= report->table_count || selected >= MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES) {
            return false;
        }
        owner = ledger->owners[selected];
        if (owner.kind != MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE || owner.generation != 0
            || owner.slot != 0 || owner.reserved != 0
            || (ledger->allocator->allocated_bitmap[selected / 64]
                & (UINT64_C(1) << (selected % 64))) == 0) {
            return false;
        }
        *frame_index = selected;
        return true;
    }
    return false;
}

static const struct micros_kernel_page_table *mapping_kernel_table(uint64_t physical)
{
    uint64_t index;

    if (!mapping_kernel_frame_index(physical, &index)) {
        return NULL;
    }
    return (const struct micros_kernel_page_table *)(uintptr_t)physical;
}

static bool mapping_domain_match(
    uint64_t start, uint64_t end, uint32_t required, uint64_t address,
    uint64_t size, uint32_t permissions, bool *largest
)
{
    uint64_t wanted;

    if (address < start || address >= end || size > end - address || permissions != required) {
        return false;
    }
    wanted = address % UINT64_C(0x40000000) == 0
        && end - address >= UINT64_C(0x40000000) ? UINT64_C(0x40000000)
        : address % UINT64_C(0x200000) == 0 && end - address >= UINT64_C(0x200000)
            ? UINT64_C(0x200000) : MICROS_SV39_PAGE_SIZE;
    *largest = *largest && size == wanted;
    return true;
}

static bool mapping_leaf_domain(
    uint64_t address, unsigned level, struct micros_sv39_decoded_pte entry,
    struct mapping_test_observation *observation
)
{
    const struct micros_frame_allocator *allocator = micros_bootstrap_frame_allocator();
    const uint64_t boundaries[] = {
        (uintptr_t)__kernel_text_start, (uintptr_t)__kernel_text_end,
        (uintptr_t)__kernel_rodata_end, (uintptr_t)__kernel_writable_end,
    };
    static const uint32_t permissions[] = {
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE,
        MICROS_SV39_PERMISSION_READ,
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE,
    };
    static const uint64_t devices[] = {
        MICROS_RISCV_UART0_BASE, MICROS_RISCV_PLIC_PRIORITY_PAGE_BASE,
        MICROS_RISCV_PLIC_SUPERVISOR_ENABLE_PAGE_BASE, MICROS_RISCV_PLIC_SUPERVISOR_CONTEXT_PAGE_BASE,
    };
    uint64_t size = MICROS_SV39_PAGE_SIZE << (9 * level);
    uint32_t rw = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;
    size_t index;

    if (entry.physical_address != address || address % size != 0
        || entry.permissions & MICROS_SV39_PERMISSION_USER) {
        return false;
    }
    for (index = 0; index < 3; ++index) {
        if (mapping_domain_match(boundaries[index], boundaries[index + 1], permissions[index],
                                 address, size, entry.permissions, &observation->largest)) {
            return true;
        }
    }
    for (index = 0; index < allocator->managed_range_count; ++index) {
        const struct micros_managed_frame_range *range = &allocator->managed_ranges[index];

        if (mapping_domain_match(range->base, range->base + range->frame_count * MICROS_FRAME_SIZE,
                                 rw, address, size, entry.permissions, &observation->largest)) {
            return true;
        }
    }
    for (index = 0; index < 4; ++index) {
        if (mapping_domain_match(devices[index], devices[index] + MICROS_SV39_PAGE_SIZE,
                                 rw, address, size, entry.permissions, &observation->largest)) {
            return true;
        }
    }
    return false;
}

static bool mapping_visit_table(uint64_t physical, struct mapping_test_observation *observation)
{
    uint64_t index;

    if (!mapping_kernel_frame_index(physical, &index) || mapping_seen[index]) {
        return false;
    }
    mapping_seen[index] = true;
    ++observation->tables;
    return true;
}

static bool mapping_inspect_lower(
    uint64_t physical, uint64_t prefix, struct mapping_test_observation *observation
)
{
    const struct micros_kernel_page_table *table = mapping_kernel_table(physical);
    size_t index;

    if (table == NULL || !mapping_visit_table(physical, observation)) {
        return false;
    }
    for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
        struct micros_sv39_decoded_pte entry;

        if (micros_sv39_decode_pte(table->entries[index], &entry) != MICROS_SV39_OK
            || entry.kind == MICROS_SV39_PTE_TABLE) {
            return false;
        }
        if (entry.kind == MICROS_SV39_PTE_LEAF) {
            if (!mapping_leaf_domain(prefix + index * MICROS_SV39_PAGE_SIZE, 0, entry, observation)) {
                return false;
            }
            ++observation->leaves[0];
        }
    }
    return true;
}

static bool mapping_inspect_middle(
    uint64_t physical, uint64_t prefix, struct mapping_test_observation *observation
)
{
    const struct micros_kernel_page_table *table = mapping_kernel_table(physical);
    size_t index;

    if (table == NULL || !mapping_visit_table(physical, observation)) {
        return false;
    }
    for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
        struct micros_sv39_decoded_pte entry;
        uint64_t address = prefix + index * UINT64_C(0x200000);

        if (micros_sv39_decode_pte(table->entries[index], &entry) != MICROS_SV39_OK) {
            return false;
        }
        if (entry.kind == MICROS_SV39_PTE_LEAF) {
            if (!mapping_leaf_domain(address, 1, entry, observation)) {
                return false;
            }
            ++observation->leaves[1];
        } else if (entry.kind == MICROS_SV39_PTE_TABLE
                   && !mapping_inspect_lower(entry.physical_address, address, observation)) {
            return false;
        }
    }
    return true;
}

static bool mapping_inspect_root(uint64_t physical, struct mapping_test_observation *observation)
{
    const struct micros_kernel_page_table *root = mapping_kernel_table(physical);
    size_t index;

    *observation = (struct mapping_test_observation){.largest = true};
    for (index = 0; index < MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES; ++index) {
        mapping_seen[index] = false;
    }
    if (root == NULL || !mapping_visit_table(physical, observation)) {
        return false;
    }
    for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
        struct micros_sv39_decoded_pte entry;
        uint64_t address = index * UINT64_C(0x40000000);

        if (micros_sv39_decode_pte(root->entries[index], &entry) != MICROS_SV39_OK) {
            return false;
        }
        if (entry.kind == MICROS_SV39_PTE_LEAF) {
            if (!mapping_leaf_domain(address, 2, entry, observation)) {
                return false;
            }
            ++observation->leaves[2];
        } else if (entry.kind == MICROS_SV39_PTE_TABLE
                   && !mapping_inspect_middle(entry.physical_address, address, observation)) {
            return false;
        }
    }
    return observation->tables == micros_kernel_address_space_report()->table_count;
}

static bool mapping_lookup_page(uint64_t root, uint64_t address, uint32_t permissions)
{
    uint64_t physical = root;
    int level;

    for (level = 2; level >= 0; --level) {
        const struct micros_kernel_page_table *table = mapping_kernel_table(physical);
        struct micros_sv39_decoded_pte entry;
        size_t index = (size_t)((address >> (12 + 9 * level)) & 511);

        if (table == NULL || micros_sv39_decode_pte(table->entries[index], &entry) != MICROS_SV39_OK) {
            return false;
        }
        if (entry.kind == MICROS_SV39_PTE_LEAF) {
            uint64_t size = MICROS_SV39_PAGE_SIZE << (9 * level);

            return entry.physical_address % size == 0 && entry.physical_address + address % size == address
                && entry.permissions == permissions;
        }
        if (entry.kind != MICROS_SV39_PTE_TABLE || level == 0) {
            return false;
        }
        physical = entry.physical_address;
    }
    return false;
}

static bool mapping_range_covered(uint64_t root, uint64_t start, uint64_t end, uint32_t permissions)
{
    uint64_t address;

    for (address = start; address < end; address += MICROS_SV39_PAGE_SIZE) {
        if (!mapping_lookup_page(root, address, permissions)) {
            return false;
        }
    }
    return true;
}

static bool mapping_complete_cover(uint64_t root)
{
    const struct micros_frame_allocator *allocator = micros_bootstrap_frame_allocator();
    static const uint64_t devices[] = {
        MICROS_RISCV_UART0_BASE, MICROS_RISCV_PLIC_PRIORITY_PAGE_BASE,
        MICROS_RISCV_PLIC_SUPERVISOR_ENABLE_PAGE_BASE, MICROS_RISCV_PLIC_SUPERVISOR_CONTEXT_PAGE_BASE,
    };
    uint32_t read = MICROS_SV39_PERMISSION_READ;
    uint32_t rw = read | MICROS_SV39_PERMISSION_WRITE;
    size_t index;

    if (!mapping_range_covered(root, (uintptr_t)__kernel_text_start, (uintptr_t)__kernel_text_end,
                               read | MICROS_SV39_PERMISSION_EXECUTE)
        || !mapping_range_covered(root, (uintptr_t)__kernel_rodata_start, (uintptr_t)__kernel_rodata_end, read)
        || !mapping_range_covered(root, (uintptr_t)__kernel_writable_start, (uintptr_t)__kernel_writable_end, rw)) {
        return false;
    }
    for (index = 0; index < allocator->managed_range_count; ++index) {
        const struct micros_managed_frame_range *range = &allocator->managed_ranges[index];

        if (!mapping_range_covered(root, range->base, range->base + range->frame_count * MICROS_FRAME_SIZE, rw)) {
            return false;
        }
    }
    for (index = 0; index < 4; ++index) {
        if (!mapping_lookup_page(root, devices[index], rw)) {
            return false;
        }
    }
    return true;
}

static bool mapping_resources_equal(const struct mapping_test_resources *original)
{
    const struct micros_frame_ownership *ledger = micros_frame_ownership_runtime_ledger();
    const struct micros_frame_allocator *allocator = micros_bootstrap_frame_allocator();
    const struct micros_kernel_address_space_report *report = micros_kernel_address_space_report();
    uint64_t index;

    if (ledger == NULL || allocator == NULL || report == NULL || riscv_irq_is_enabled()
        || micros_frame_ownership_validate(ledger) != MICROS_FRAME_OWNERSHIP_OK
        || report->root_physical_address != original->root || report->table_count != original->tables
        || allocator->free_frame_count != original->free_frames || allocator->allocation_cursor != original->cursor
        || ledger->owned_frame_count != original->owned || ledger->phase != original->phase
        || ledger->owned_frame_count != original->tables
        || ledger->owner_counts[MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE] != original->tables) {
        return false;
    }
    for (index = 0; index < original->tables; ++index) {
        struct micros_frame_owner owner = ledger->owners[index];

        if (owner.kind != MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE || owner.generation != 0
            || owner.slot != 0 || owner.reserved != 0
            || (allocator->allocated_bitmap[index / 64] & (UINT64_C(1) << (index % 64))) == 0) {
            return false;
        }
    }
    return true;
}

static void mapping_activate(uint64_t root)
{
    if (riscv_irq_is_enabled()
        || micros_riscv_activate_sv39(root) != (MICROS_RISCV_SATP_MODE_SV39 | (root >> 12))) {
        MICROS_PANIC(0, "mmu-large-root-activation");
    }
}

static bool mapping_load_probe(uint64_t temporary_root, uint64_t kernel_root, uint64_t address, bool fault)
{
    uint32_t before = mapping_fault_count;
    uint64_t value;
    bool valid;

    mapping_expected_address = address;
    mapping_phase = fault ? MAPPING_TEST_LOAD_ARMED : MAPPING_TEST_IDLE;
    mapping_activate(temporary_root);
    value = micros_mmu_large_load_test_trigger(address);
    mapping_activate(kernel_root);
    valid = fault
        ? mapping_phase == MAPPING_TEST_LOAD_HANDLED && mapping_fault_count == before + 1
        : mapping_phase == MAPPING_TEST_IDLE && mapping_fault_count == before && value == mapping_test_sentinel;
    mapping_phase = MAPPING_TEST_IDLE;
    return valid;
}

static bool mapping_aliases(uint64_t root_address, uint64_t middle_address, uint64_t kernel_root)
{
    struct micros_kernel_page_table *root = (struct micros_kernel_page_table *)(uintptr_t)root_address;
    struct micros_kernel_page_table *middle = (struct micros_kernel_page_table *)(uintptr_t)middle_address;
    const struct micros_kernel_page_table *original = mapping_kernel_table(kernel_root);
    uint64_t sentinel = (uintptr_t)&mapping_test_sentinel;
    uint64_t address_2m = UINT64_C(0x100000000) + sentinel % UINT64_C(0x200000);
    uint64_t address_1g = UINT64_C(0x140000000) + sentinel % UINT64_C(0x40000000);
    uint64_t leaf_2m;
    uint64_t leaf_1g;
    uint64_t table_pte;
    size_t index;
    bool valid;

    if (original == NULL || original->entries[4] != 0 || original->entries[5] != 0
        || micros_sv39_make_table_pte(middle_address, &table_pte) != MICROS_SV39_OK
        || micros_sv39_make_leaf_pte(sentinel & ~UINT64_C(0x1fffff),
                                     MICROS_SV39_PERMISSION_READ, &leaf_2m) != MICROS_SV39_OK
        || micros_sv39_make_leaf_pte(sentinel & ~UINT64_C(0x3fffffff),
                                     MICROS_SV39_PERMISSION_READ, &leaf_1g) != MICROS_SV39_OK) {
        return false;
    }
    for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
        root->entries[index] = original->entries[index];
        middle->entries[index] = 0;
    }
    root->entries[4] = table_pte;
    root->entries[5] = leaf_1g;
    middle->entries[0] = leaf_2m;
    mapping_fault_count = 0;
    valid = mapping_load_probe(root_address, kernel_root, address_2m, false)
        && mapping_load_probe(root_address, kernel_root, address_1g, false);
    middle->entries[0] = leaf_2m ^ (UINT64_C(1) << 10);
    valid = mapping_load_probe(root_address, kernel_root, address_2m, true) && valid;
    middle->entries[0] = leaf_2m;
    root->entries[5] = leaf_1g ^ (UINT64_C(1) << 10);
    valid = mapping_load_probe(root_address, kernel_root, address_1g, true) && valid;
    root->entries[4] = 0;
    root->entries[5] = 0;
    for (index = 0; index < MICROS_SV39_TABLE_ENTRY_COUNT; ++index) {
        if (root->entries[index] != original->entries[index]) {
            valid = false;
        }
    }
    return valid && mapping_fault_count == 2 && mapping_phase == MAPPING_TEST_IDLE;
}

static bool mapping_temporary_probes(const struct mapping_test_resources *original)
{
    struct micros_frame_owner owner;
    uint64_t root;
    uint64_t middle;
    bool valid;

    mapping_activate(original->root);
    if (micros_frame_owner_make_kernel(MICROS_FRAME_OWNER_KERNEL_TEMPORARY, &owner) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_allocate(owner, &root) != MICROS_FRAME_OWNERSHIP_OK) {
        return false;
    }
    if (micros_frame_ownership_runtime_allocate(owner, &middle) != MICROS_FRAME_OWNERSHIP_OK) {
        if (micros_frame_ownership_runtime_release(owner, root) != MICROS_FRAME_OWNERSHIP_OK) {
            MICROS_PANIC(0, "mmu-large-root-release");
        }
        return false;
    }
    valid = mapping_aliases(root, middle, original->root);
    mapping_activate(original->root);
    if (micros_frame_ownership_runtime_release(owner, middle) != MICROS_FRAME_OWNERSHIP_OK
        || micros_frame_ownership_runtime_release(owner, root) != MICROS_FRAME_OWNERSHIP_OK) {
        MICROS_PANIC(0, "mmu-large-frame-release");
    }
    return valid && mapping_resources_equal(original);
}

static void mapping_print_observation(const struct mapping_test_observation *observation)
{
    uart_write("MICROS_KERNEL_MAPPING_OBSERVATION version=1 tables=");
    uart_write_hex64(observation->tables);
    uart_write(" leaves-4k=");
    uart_write_hex64(observation->leaves[0]);
    uart_write(" leaves-2m=");
    uart_write_hex64(observation->leaves[1]);
    uart_write(" leaves-1g=");
    uart_write_hex64(observation->leaves[2]);
    uart_write(observation->largest ? " largest=1\n" : " largest=0\n");
    uart_write("MICROS_KERNEL_MAPPING_GUARDS aliases=2m,1g misaligned=rejected resources=preserved\n");
    if (observation->largest) {
        uart_write(
            "MICROS_KERNEL_MAPPING_TEST_PASS topology=largest aliases=2m,1g "
            "misaligned=rejected resources=preserved\n"
        );
    }
    uart_flush();
}

static bool mapping_test_run(void)
{
    const struct micros_frame_allocator *allocator = micros_bootstrap_frame_allocator();
    const struct micros_frame_ownership *ledger = micros_frame_ownership_runtime_ledger();
    const struct micros_kernel_address_space_report *report = micros_kernel_address_space_report();
    struct mapping_test_resources original;
    struct mapping_test_observation before;
    struct mapping_test_observation after;

    if (allocator == NULL || ledger == NULL || report == NULL
        || report->table_count == 0 || report->table_count > MICROS_KERNEL_PAGE_TABLE_MAX_FRAMES
        || ledger->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP) {
        return false;
    }
    original = (struct mapping_test_resources){
        report->root_physical_address, report->table_count, allocator->free_frame_count,
        allocator->allocation_cursor, ledger->owned_frame_count, ledger->phase,
    };
    if (!mapping_resources_equal(&original) || !mapping_inspect_root(original.root, &before)
        || !mapping_complete_cover(original.root) || !mapping_temporary_probes(&original)
        || !mapping_inspect_root(original.root, &after) || !mapping_complete_cover(original.root)
        || before.tables != after.tables || before.largest != after.largest
        || before.leaves[0] != after.leaves[0] || before.leaves[1] != after.leaves[1]
        || before.leaves[2] != after.leaves[2]) {
        return false;
    }
    mapping_print_observation(&after);
    return true;
}
