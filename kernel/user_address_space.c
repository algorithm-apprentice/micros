#include "micros/user_address_space.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "micros/frame_allocator.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/sv39.h"
#include "micros/user_address_space_core.h"
#include "micros/user_execution.h"

enum {
    USER_ROOT_INDEX = 1,
};

struct user_page_table {
    uint64_t entries[MICROS_SV39_TABLE_ENTRY_COUNT];
};

static uint64_t
    reachable_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
static uint64_t
    release_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
static bool scratch_busy;

#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
static size_t test_fail_after_allocations = SIZE_MAX;
static size_t test_successful_allocations;
#endif

_Static_assert(
    sizeof(struct user_page_table) == MICROS_SV39_PAGE_SIZE,
    "user page tables must occupy one base page"
);
_Static_assert(
    sizeof(reachable_bitmap) * 8
        == MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES,
    "reachability bitmap must cover every managed frame"
);
_Static_assert(
    sizeof(release_bitmap) * 8
        == MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES,
    "release bitmap must cover every managed frame"
);

static struct user_page_table *table_at(uint64_t physical_address)
{
    return (struct user_page_table *)(uintptr_t)physical_address;
}

static void clear_page(uint64_t physical_address)
{
    uint64_t *words = (uint64_t *)(uintptr_t)physical_address;
    size_t index;

    for (
        index = 0;
        index < MICROS_SV39_PAGE_SIZE / sizeof(uint64_t);
        ++index
    ) {
        words[index] = 0;
    }
}

static void clear_bitmap(uint64_t *bitmap)
{
    size_t index;

    for (
        index = 0;
        index < MICROS_FRAME_ALLOCATOR_BITMAP_WORDS;
        ++index
    ) {
        bitmap[index] = 0;
    }
}

static bool bitmap_contains(
    const uint64_t *bitmap,
    uint64_t frame_index
)
{
    return (
        bitmap[frame_index / 64]
        & (UINT64_C(1) << (frame_index % 64))
    ) != 0;
}

static void bitmap_add(uint64_t *bitmap, uint64_t frame_index)
{
    bitmap[frame_index / 64]
        |= UINT64_C(1) << (frame_index % 64);
}

static bool acquire_scratch(void)
{
    if (scratch_busy) {
        return false;
    }
    scratch_busy = true;
    clear_bitmap(reachable_bitmap);
    clear_bitmap(release_bitmap);
    return true;
}

static void release_scratch(void)
{
    clear_bitmap(reachable_bitmap);
    clear_bitmap(release_bitmap);
    scratch_busy = false;
}

static uint64_t read_satp(void)
{
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

static uint64_t expected_satp(uint64_t root)
{
    return MICROS_RISCV_SATP_MODE_SV39 | (root >> 12);
}

static uint64_t active_root(void)
{
    const uint64_t ppn_mask =
        (UINT64_C(1) << 44) - UINT64_C(1);
    uint64_t satp = read_satp();

    if ((satp & MICROS_RISCV_SATP_MODE_SV39) == 0) {
        return 0;
    }
    return (satp & ppn_mask) << 12;
}

static _Noreturn void panic_invariant(const char *reason)
{
    struct micros_hart *hart =
        micros_kernel_object_runtime_boot_hart();
    uintptr_t hart_id = hart == NULL ? 0 : hart->hardware_id;

    MICROS_PANIC(hart_id, reason);
}

static enum micros_user_address_space_error
map_object_error(enum micros_kernel_object_error error)
{
    switch (error) {
    case MICROS_KERNEL_OBJECT_OK:
        return MICROS_USER_ADDRESS_SPACE_OK;
    case MICROS_KERNEL_OBJECT_ERROR_ARGUMENT:
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    case MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED:
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_INITIALIZED;
    case MICROS_KERNEL_OBJECT_ERROR_STALE:
        return MICROS_USER_ADDRESS_SPACE_ERROR_STALE;
    case MICROS_KERNEL_OBJECT_ERROR_STATE:
        return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
    default:
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
}

static enum micros_user_address_space_error
map_ownership_error(enum micros_frame_ownership_error error)
{
    switch (error) {
    case MICROS_FRAME_OWNERSHIP_OK:
        return MICROS_USER_ADDRESS_SPACE_OK;
    case MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT:
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    case MICROS_FRAME_OWNERSHIP_ERROR_NOT_INITIALIZED:
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_INITIALIZED;
    case MICROS_FRAME_OWNERSHIP_ERROR_PHASE:
        return MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
    case MICROS_FRAME_OWNERSHIP_ERROR_EXHAUSTED:
        return MICROS_USER_ADDRESS_SPACE_ERROR_ALLOCATION;
    case MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER:
    case MICROS_FRAME_OWNERSHIP_ERROR_OWNER:
    case MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED:
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    default:
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
}

static enum micros_user_address_space_error
require_bootstrap(
    const struct micros_frame_ownership **ledger,
    const struct micros_kernel_objects **objects,
    const struct micros_kernel_address_space_report **kernel_report
)
{
    const struct micros_frame_ownership *observed_ledger =
        micros_frame_ownership_runtime_ledger();
    const struct micros_kernel_objects *observed_objects =
        micros_kernel_object_runtime_registry();
    const struct micros_kernel_address_space_report *observed_report =
        micros_kernel_address_space_report();

    if (
        observed_ledger == NULL
        || observed_objects == NULL
        || observed_report == NULL
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_INITIALIZED;
    }
    if (
        observed_ledger->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
    }
    *ledger = observed_ledger;
    *objects = observed_objects;
    *kernel_report = observed_report;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
require_read_authority(
    const struct micros_frame_ownership **ledger,
    const struct micros_kernel_objects **objects,
    const struct micros_kernel_address_space_report **kernel_report,
    enum micros_frame_owner_kind *leaf_owner_kind
)
{
    const struct micros_frame_ownership *observed_ledger =
        micros_frame_ownership_runtime_ledger();
    const struct micros_kernel_objects *observed_objects =
        micros_kernel_object_runtime_registry();
    const struct micros_kernel_address_space_report *observed_report =
        micros_kernel_address_space_report();
    enum micros_frame_owner_kind observed_leaf_owner_kind;
    enum micros_user_address_space_error error;

    if (
        observed_ledger == NULL
        || observed_objects == NULL
        || observed_report == NULL
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_INITIALIZED;
    }
    error = micros_user_address_space_leaf_owner_kind(
        observed_ledger->phase,
        &observed_leaf_owner_kind
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (leaf_owner_kind != NULL) {
        *leaf_owner_kind = observed_leaf_owner_kind;
    }
    *ledger = observed_ledger;
    *objects = observed_objects;
    *kernel_report = observed_report;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error resolve_process(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    const struct micros_process **resolved
)
{
    return map_object_error(
        micros_process_resolve(objects, process, resolved)
    );
}

static enum micros_user_address_space_error resolve_process_with_root(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    const struct micros_process **resolved
)
{
    enum micros_user_address_space_error error =
        resolve_process(objects, process, resolved);

    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if ((*resolved)->address_space_root == 0) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static bool user_virtual_address_is_valid(uint64_t virtual_address)
{
    return (
        virtual_address >= MICROS_USER_VIRTUAL_BASE
        && virtual_address < MICROS_USER_VIRTUAL_END
        && virtual_address % MICROS_SV39_PAGE_SIZE == 0
        && micros_sv39_virtual_address_is_canonical(virtual_address)
    );
}

static bool user_virtual_byte_address_is_valid(uint64_t virtual_address)
{
    return (
        virtual_address >= MICROS_USER_VIRTUAL_BASE
        && virtual_address < MICROS_USER_VIRTUAL_END
        && micros_sv39_virtual_address_is_canonical(virtual_address)
    );
}

static bool user_permissions_are_valid(uint32_t permissions)
{
    const uint32_t allowed =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_EXECUTE;

    return (
        permissions != 0
        && (permissions & ~allowed) == 0
        && (
            (permissions & MICROS_SV39_PERMISSION_WRITE) == 0
            || (permissions & MICROS_SV39_PERMISSION_READ) != 0
        )
        && (
            (
                permissions
                & (
                    MICROS_SV39_PERMISSION_WRITE
                    | MICROS_SV39_PERMISSION_EXECUTE
                )
            )
            != (
                MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_EXECUTE
            )
        )
    );
}

static bool frame_index_for_physical_address(
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    uint64_t *frame_index
)
{
    size_t range_index;

    if (
        ledger == NULL
        || frame_index == NULL
        || physical_address % MICROS_SV39_PAGE_SIZE != 0
    ) {
        return false;
    }
    for (
        range_index = 0;
        range_index < ledger->managed_range_count;
        ++range_index
    ) {
        const struct micros_frame_range_snapshot *range =
            &ledger->managed_ranges[range_index];
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

static bool physical_address_for_frame_index(
    const struct micros_frame_ownership *ledger,
    uint64_t frame_index,
    uint64_t *physical_address
)
{
    size_t range_index;

    if (
        ledger == NULL
        || physical_address == NULL
        || frame_index >= ledger->managed_frame_count
    ) {
        return false;
    }
    for (
        range_index = 0;
        range_index < ledger->managed_range_count;
        ++range_index
    ) {
        const struct micros_frame_range_snapshot *range =
            &ledger->managed_ranges[range_index];

        if (
            frame_index >= range->bitmap_offset
            && frame_index
                < range->bitmap_offset + range->frame_count
        ) {
            *physical_address = range->base
                + (
                    frame_index - range->bitmap_offset
                ) * MICROS_SV39_PAGE_SIZE;
            return true;
        }
    }
    return false;
}

static enum micros_user_address_space_error mark_owned_frame(
    const struct micros_frame_ownership *ledger,
    uint64_t physical_address,
    enum micros_user_address_space_frame_role role,
    struct micros_process_handle process
)
{
    struct micros_frame_owner observed;
    uint64_t frame_index;

    if (
        micros_frame_ownership_lookup(
            ledger,
            physical_address,
            &observed
        ) != MICROS_FRAME_OWNERSHIP_OK
        || micros_user_address_space_validate_frame_owner(
            ledger->phase,
            role,
            process,
            observed
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || !frame_index_for_physical_address(
            ledger,
            physical_address,
            &frame_index
        )
        || bitmap_contains(reachable_bitmap, frame_index)
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    }
    bitmap_add(reachable_bitmap, frame_index);
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error decode_pte(
    uint64_t pte,
    struct micros_sv39_decoded_pte *decoded
)
{
    if (micros_sv39_decode_pte(pte, decoded) != MICROS_SV39_OK) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error validate_locked(
    struct micros_process_handle process,
    const struct micros_process **resolved_process
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    const struct user_page_table *kernel_root;
    const struct user_page_table *root;
    struct micros_sv39_decoded_pte root_entry;
    enum micros_frame_owner_kind leaf_owner_kind;
    enum micros_user_address_space_error error;
    size_t root_index;
    uint64_t frame_index;

    clear_bitmap(reachable_bitmap);
    error = require_read_authority(
        &ledger,
        &objects,
        &kernel_report,
        &leaf_owner_kind
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (
        micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    error = resolve_process(objects, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (resolved->address_space_root == 0) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
    }
    error = mark_owned_frame(
        ledger,
        resolved->address_space_root,
        MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE,
        process
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }

    kernel_root = table_at(kernel_report->root_physical_address);
    root = table_at(resolved->address_space_root);
    if (kernel_root->entries[USER_ROOT_INDEX] != 0) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    for (
        root_index = 0;
        root_index < MICROS_SV39_TABLE_ENTRY_COUNT;
        ++root_index
    ) {
        if (
            root_index != USER_ROOT_INDEX
            && root->entries[root_index]
                != kernel_root->entries[root_index]
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        }
    }

    error = decode_pte(
        root->entries[USER_ROOT_INDEX],
        &root_entry
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (root_entry.kind == MICROS_SV39_PTE_LEAF) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    if (root_entry.kind == MICROS_SV39_PTE_TABLE) {
        const struct user_page_table *middle =
            table_at(root_entry.physical_address);
        size_t middle_index;

        error = mark_owned_frame(
            ledger,
            root_entry.physical_address,
            MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE,
            process
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
        for (
            middle_index = 0;
            middle_index < MICROS_SV39_TABLE_ENTRY_COUNT;
            ++middle_index
        ) {
            struct micros_sv39_decoded_pte middle_entry;

            error = decode_pte(
                middle->entries[middle_index],
                &middle_entry
            );
            if (error != MICROS_USER_ADDRESS_SPACE_OK) {
                return error;
            }
            if (middle_entry.kind == MICROS_SV39_PTE_ABSENT) {
                continue;
            }
            if (middle_entry.kind != MICROS_SV39_PTE_TABLE) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
            }
            error = mark_owned_frame(
                ledger,
                middle_entry.physical_address,
                MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE,
                process
            );
            if (error != MICROS_USER_ADDRESS_SPACE_OK) {
                return error;
            }
            {
                const struct user_page_table *leaf =
                    table_at(middle_entry.physical_address);
                size_t leaf_index;

                for (
                    leaf_index = 0;
                    leaf_index < MICROS_SV39_TABLE_ENTRY_COUNT;
                    ++leaf_index
                ) {
                    struct micros_sv39_decoded_pte leaf_entry;

                    error = decode_pte(
                        leaf->entries[leaf_index],
                        &leaf_entry
                    );
                    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
                        return error;
                    }
                    if (leaf_entry.kind
                        == MICROS_SV39_PTE_ABSENT) {
                        continue;
                    }
                    if (
                        leaf_entry.kind != MICROS_SV39_PTE_LEAF
                        || (
                            leaf_entry.permissions
                            & MICROS_SV39_PERMISSION_USER
                        ) == 0
                    ) {
                        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
                    }
                    error = mark_owned_frame(
                        ledger,
                        leaf_entry.physical_address,
                        MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
                        process
                    );
                    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
                        return error;
                    }
                }
            }
        }
    }

    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        struct micros_frame_owner owner =
            ledger->owners[frame_index];
        bool exact_process = (
            owner.slot == process.slot
            && owner.generation == process.generation
        );

        if (
            exact_process
            && (
                owner.kind
                    == MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
                || owner.kind == leaf_owner_kind
            )
        ) {
            if (!bitmap_contains(reachable_bitmap, frame_index)) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            }
        } else if (
            exact_process
            && owner.kind != MICROS_FRAME_OWNER_FREE
        ) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
    }
    if (resolved_process != NULL) {
        *resolved_process = resolved;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error lookup_leaf_locked(
    const struct micros_process *process,
    uint64_t virtual_address,
    struct micros_sv39_decoded_pte *leaf_entry,
    uint64_t **leaf_pte,
    uint64_t *middle_physical_address,
    uint64_t *leaf_physical_address
)
{
    struct user_page_table *root =
        table_at(process->address_space_root);
    struct micros_sv39_decoded_pte root_entry;
    struct micros_sv39_decoded_pte middle_entry;
    struct user_page_table *middle;
    struct user_page_table *leaf;
    uint16_t root_index;
    uint16_t middle_index;
    uint16_t leaf_index;
    enum micros_user_address_space_error error;

    if (
        micros_sv39_vpn_index(
            virtual_address,
            2,
            &root_index
        ) != MICROS_SV39_OK
        || root_index != USER_ROOT_INDEX
        || micros_sv39_vpn_index(
            virtual_address,
            1,
            &middle_index
        ) != MICROS_SV39_OK
        || micros_sv39_vpn_index(
            virtual_address,
            0,
            &leaf_index
        ) != MICROS_SV39_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
    }
    error = decode_pte(root->entries[root_index], &root_entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (root_entry.kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    if (root_entry.kind != MICROS_SV39_PTE_TABLE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    middle = table_at(root_entry.physical_address);
    error = decode_pte(
        middle->entries[middle_index],
        &middle_entry
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (middle_entry.kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    if (middle_entry.kind != MICROS_SV39_PTE_TABLE) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    leaf = table_at(middle_entry.physical_address);
    error = decode_pte(leaf->entries[leaf_index], leaf_entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (leaf_entry->kind == MICROS_SV39_PTE_ABSENT) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    if (leaf_entry->kind != MICROS_SV39_PTE_LEAF) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    if (leaf_pte != NULL) {
        *leaf_pte = &leaf->entries[leaf_index];
    }
    if (middle_physical_address != NULL) {
        *middle_physical_address = root_entry.physical_address;
    }
    if (leaf_physical_address != NULL) {
        *leaf_physical_address = middle_entry.physical_address;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static bool table_has_other_entries(
    const struct user_page_table *table,
    size_t excluded_index
)
{
    size_t index;

    for (
        index = 0;
        index < MICROS_SV39_TABLE_ENTRY_COUNT;
        ++index
    ) {
        if (index != excluded_index && table->entries[index] != 0) {
            return true;
        }
    }
    return false;
}

static enum micros_user_address_space_error allocate_owned_frame(
    struct micros_frame_owner owner,
    uint64_t *physical_address
)
{
#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
    if (test_successful_allocations >= test_fail_after_allocations) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ALLOCATION;
    }
#endif
    {
        enum micros_frame_ownership_error error =
            micros_frame_ownership_runtime_allocate(
                owner,
                physical_address
            );

        if (error != MICROS_FRAME_OWNERSHIP_OK) {
            return map_ownership_error(error);
        }
    }
#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
    ++test_successful_allocations;
#endif
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static void release_owned_frame_or_panic(
    struct micros_frame_owner owner,
    uint64_t physical_address
)
{
    if (
        micros_frame_ownership_runtime_release(
            owner,
            physical_address
        ) != MICROS_FRAME_OWNERSHIP_OK
    ) {
        panic_invariant("user-address-space-rollback");
    }
}

static enum micros_user_address_space_error make_process_owner(
    enum micros_frame_owner_kind kind,
    struct micros_process_handle process,
    struct micros_frame_owner *owner
)
{
    if (
        micros_frame_owner_make_process(kind, process, owner)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_create(struct micros_process_handle process)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    const struct user_page_table *kernel_root;
    struct user_page_table *root;
    struct micros_frame_owner owner;
    uint64_t root_physical_address;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;
    size_t index;

    saved_status = riscv_irq_save();
    error = require_bootstrap(&ledger, &objects, &kernel_report);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process(objects, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (resolved->address_space_root != 0) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done;
    }
    if (!acquire_scratch()) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_BUSY;
        goto done;
    }
    if (
        micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done_with_scratch;
    }
    kernel_root = table_at(kernel_report->root_physical_address);
    if (kernel_root->entries[USER_ROOT_INDEX] != 0) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done_with_scratch;
    }
    error = make_process_owner(
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        process,
        &owner
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = allocate_owned_frame(owner, &root_physical_address);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    clear_page(root_physical_address);
    root = table_at(root_physical_address);
    for (
        index = 0;
        index < MICROS_SV39_TABLE_ENTRY_COUNT;
        ++index
    ) {
        if (
            index != USER_ROOT_INDEX
            && kernel_root->entries[index] != 0
        ) {
            root->entries[index] = kernel_root->entries[index];
        }
    }
    {
        enum micros_kernel_object_error attach_error =
            micros_kernel_object_runtime_attach_address_space(
                process,
                root_physical_address
            );

        if (attach_error != MICROS_KERNEL_OBJECT_OK) {
            clear_page(root_physical_address);
            release_owned_frame_or_panic(
                owner,
                root_physical_address
            );
            error = map_object_error(attach_error);
            goto done_with_scratch;
        }
    }
    error = validate_locked(process, NULL);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        if (
            micros_kernel_object_runtime_detach_address_space(
                process,
                root_physical_address
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            panic_invariant("user-address-space-create-detach");
        }
        clear_page(root_physical_address);
        release_owned_frame_or_panic(owner, root_physical_address);
    }

done_with_scratch:
    release_scratch();
done:
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_allocate_page(
    struct micros_process_handle process,
    uint64_t virtual_address,
    uint32_t permissions,
    uint64_t *physical_address
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct user_page_table *root;
    struct user_page_table *middle;
    struct user_page_table *leaf;
    struct micros_sv39_decoded_pte root_entry;
    struct micros_sv39_decoded_pte middle_entry;
    struct micros_sv39_decoded_pte leaf_entry;
    struct micros_frame_owner table_owner;
    struct micros_frame_owner user_owner;
    uint64_t new_middle = 0;
    uint64_t new_leaf = 0;
    uint64_t new_user = 0;
    uint64_t root_pte = 0;
    uint64_t middle_pte = 0;
    uint64_t user_pte = 0;
    uint16_t root_index;
    uint16_t middle_index;
    uint16_t leaf_index;
    bool need_middle;
    bool need_leaf;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    if (physical_address == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    saved_status = riscv_irq_save();
    error = require_bootstrap(&ledger, &objects, &kernel_report);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process(objects, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (!user_virtual_address_is_valid(virtual_address)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
        goto done;
    }
    if (!user_permissions_are_valid(permissions)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION;
        goto done;
    }
    if (!acquire_scratch()) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_BUSY;
        goto done;
    }
    error = validate_locked(process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (active_root() == resolved->address_space_root) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done_with_scratch;
    }
    if (
        micros_sv39_vpn_index(
            virtual_address,
            2,
            &root_index
        ) != MICROS_SV39_OK
        || root_index != USER_ROOT_INDEX
        || micros_sv39_vpn_index(
            virtual_address,
            1,
            &middle_index
        ) != MICROS_SV39_OK
        || micros_sv39_vpn_index(
            virtual_address,
            0,
            &leaf_index
        ) != MICROS_SV39_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
        goto done_with_scratch;
    }
    root = table_at(resolved->address_space_root);
    error = decode_pte(root->entries[root_index], &root_entry);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    need_middle = root_entry.kind == MICROS_SV39_PTE_ABSENT;
    if (!need_middle && root_entry.kind != MICROS_SV39_PTE_TABLE) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        goto done_with_scratch;
    }
    if (need_middle) {
        middle = NULL;
        need_leaf = true;
    } else {
        middle = table_at(root_entry.physical_address);
        error = decode_pte(
            middle->entries[middle_index],
            &middle_entry
        );
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto done_with_scratch;
        }
        need_leaf = middle_entry.kind == MICROS_SV39_PTE_ABSENT;
        if (
            !need_leaf
            && middle_entry.kind != MICROS_SV39_PTE_TABLE
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
            goto done_with_scratch;
        }
    }
    if (!need_leaf) {
        leaf = table_at(middle_entry.physical_address);
        error = decode_pte(leaf->entries[leaf_index], &leaf_entry);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto done_with_scratch;
        }
        if (leaf_entry.kind != MICROS_SV39_PTE_ABSENT) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT;
            goto done_with_scratch;
        }
    }

    error = make_process_owner(
        MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
        process,
        &table_owner
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = make_process_owner(
        MICROS_FRAME_OWNER_PROCESS_USER,
        process,
        &user_owner
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (need_middle) {
        error = allocate_owned_frame(table_owner, &new_middle);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto rollback;
        }
        clear_page(new_middle);
        if (
            micros_sv39_make_table_pte(new_middle, &root_pte)
                != MICROS_SV39_OK
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
            goto rollback;
        }
        middle = table_at(new_middle);
    }
    if (need_leaf) {
        error = allocate_owned_frame(table_owner, &new_leaf);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto rollback;
        }
        clear_page(new_leaf);
        if (
            micros_sv39_make_table_pte(new_leaf, &middle_pte)
                != MICROS_SV39_OK
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
            goto rollback;
        }
        leaf = table_at(new_leaf);
    }
    error = allocate_owned_frame(user_owner, &new_user);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto rollback;
    }
    clear_page(new_user);
    if (
        micros_sv39_make_leaf_pte(
            new_user,
            permissions | MICROS_SV39_PERMISSION_USER,
            &user_pte
        ) != MICROS_SV39_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
        goto rollback;
    }

    if (need_middle) {
        root->entries[root_index] = root_pte;
    }
    if (need_leaf) {
        middle->entries[middle_index] = middle_pte;
    }
    leaf->entries[leaf_index] = user_pte;
    *physical_address = new_user;
    error = MICROS_USER_ADDRESS_SPACE_OK;
    goto done_with_scratch;

rollback:
    if (new_user != 0) {
        clear_page(new_user);
        release_owned_frame_or_panic(user_owner, new_user);
    }
    if (new_leaf != 0) {
        clear_page(new_leaf);
        release_owned_frame_or_panic(table_owner, new_leaf);
    }
    if (new_middle != 0) {
        clear_page(new_middle);
        release_owned_frame_or_panic(table_owner, new_middle);
    }

done_with_scratch:
    release_scratch();
done:
    (void)ledger;
    (void)kernel_report;
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_lookup(
    struct micros_process_handle process,
    uint64_t virtual_address,
    uint64_t *physical_address,
    uint32_t *permissions
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_sv39_decoded_pte leaf_entry;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    if (physical_address == NULL || permissions == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    saved_status = riscv_irq_save();
    error = require_read_authority(
        &ledger,
        &objects,
        &kernel_report,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(objects, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (!user_virtual_address_is_valid(virtual_address)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
        goto done;
    }
    if (!acquire_scratch()) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_BUSY;
        goto done;
    }
    error = validate_locked(process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = lookup_leaf_locked(
        resolved,
        virtual_address,
        &leaf_entry,
        NULL,
        NULL,
        NULL
    );
    if (error == MICROS_USER_ADDRESS_SPACE_OK) {
        *physical_address = leaf_entry.physical_address;
        *permissions = leaf_entry.permissions
            & ~MICROS_SV39_PERMISSION_USER;
    }

done_with_scratch:
    release_scratch();
done:
    (void)ledger;
    (void)kernel_report;
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_translate(
    struct micros_process_handle process,
    uint64_t user_address,
    uint64_t *physical_address,
    uint32_t *permissions,
    size_t *contiguous_bytes
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_sv39_decoded_pte leaf_entry;
    enum micros_user_address_space_error error;
    uint64_t offset;
    uintptr_t saved_status;

    if (
        physical_address == NULL
        || permissions == NULL
        || contiguous_bytes == NULL
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    saved_status = riscv_irq_save();
    error = require_read_authority(
        &ledger,
        &objects,
        &kernel_report,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(objects, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (!user_virtual_byte_address_is_valid(user_address)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
        goto done;
    }
    if (!acquire_scratch()) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_BUSY;
        goto done;
    }
    error = validate_locked(process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    error = lookup_leaf_locked(
        resolved,
        user_address,
        &leaf_entry,
        NULL,
        NULL,
        NULL
    );
    if (error == MICROS_USER_ADDRESS_SPACE_OK) {
        offset = user_address % MICROS_SV39_PAGE_SIZE;
        *physical_address = leaf_entry.physical_address + offset;
        *permissions =
            leaf_entry.permissions & ~MICROS_SV39_PERMISSION_USER;
        *contiguous_bytes =
            (size_t)(MICROS_SV39_PAGE_SIZE - offset);
    }

done_with_scratch:
    release_scratch();
done:
    (void)ledger;
    (void)kernel_report;
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_release_page(
    struct micros_process_handle process,
    uint64_t virtual_address,
    uint64_t *physical_address
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    struct micros_sv39_decoded_pte leaf_entry;
    struct user_page_table *root;
    struct user_page_table *middle;
    struct user_page_table *leaf;
    uint64_t *leaf_pte;
    uint64_t middle_physical_address;
    uint64_t leaf_physical_address;
    uint64_t frame_index;
    uint16_t middle_index;
    uint16_t leaf_index;
    bool release_leaf_table;
    bool release_middle_table;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    if (physical_address == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    saved_status = riscv_irq_save();
    error = require_bootstrap(&ledger, &objects, &kernel_report);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process(objects, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (!user_virtual_address_is_valid(virtual_address)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
        goto done;
    }
    if (!acquire_scratch()) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_BUSY;
        goto done;
    }
    error = validate_locked(process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (active_root() == resolved->address_space_root) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done_with_scratch;
    }
    error = lookup_leaf_locked(
        resolved,
        virtual_address,
        &leaf_entry,
        &leaf_pte,
        &middle_physical_address,
        &leaf_physical_address
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (
        micros_sv39_vpn_index(
            virtual_address,
            1,
            &middle_index
        ) != MICROS_SV39_OK
        || micros_sv39_vpn_index(
            virtual_address,
            0,
            &leaf_index
        ) != MICROS_SV39_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
        goto done_with_scratch;
    }
    root = table_at(resolved->address_space_root);
    middle = table_at(middle_physical_address);
    leaf = table_at(leaf_physical_address);
    release_leaf_table = !table_has_other_entries(leaf, leaf_index);
    release_middle_table = (
        release_leaf_table
        && !table_has_other_entries(middle, middle_index)
    );

    clear_bitmap(release_bitmap);
    if (
        !frame_index_for_physical_address(
            ledger,
            leaf_entry.physical_address,
            &frame_index
        )
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done_with_scratch;
    }
    bitmap_add(release_bitmap, frame_index);
    if (release_leaf_table) {
        if (
            !frame_index_for_physical_address(
                ledger,
                leaf_physical_address,
                &frame_index
            )
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
            goto done_with_scratch;
        }
        bitmap_add(release_bitmap, frame_index);
    }
    if (release_middle_table) {
        if (
            !frame_index_for_physical_address(
                ledger,
                middle_physical_address,
                &frame_index
            )
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
            goto done_with_scratch;
        }
        bitmap_add(release_bitmap, frame_index);
    }
    error = map_ownership_error(
        micros_frame_ownership_runtime_release_process_set(
            process,
            release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }

    *leaf_pte = 0;
    if (release_leaf_table) {
        clear_page(leaf_physical_address);
        middle->entries[middle_index] = 0;
    }
    if (release_middle_table) {
        clear_page(middle_physical_address);
        root->entries[USER_ROOT_INDEX] = 0;
    }
    *physical_address = leaf_entry.physical_address;

done_with_scratch:
    release_scratch();
done:
    (void)kernel_report;
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_destroy(struct micros_process_handle process)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    uint64_t root_physical_address;
    uint64_t frame_index;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    error = require_bootstrap(&ledger, &objects, &kernel_report);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process(objects, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(objects, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (!acquire_scratch()) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_BUSY;
        goto done;
    }
    error = validate_locked(process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    if (
        resolved->live_thread_count != 0
        || active_root() == resolved->address_space_root
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
        goto done_with_scratch;
    }
    root_physical_address = resolved->address_space_root;
    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        if (bitmap_contains(reachable_bitmap, frame_index)) {
            bitmap_add(release_bitmap, frame_index);
        }
    }
    error = map_ownership_error(
        micros_frame_ownership_runtime_release_process_set(
            process,
            release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        )
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        uint64_t physical_address;

        if (!bitmap_contains(release_bitmap, frame_index)) {
            continue;
        }
        if (
            !physical_address_for_frame_index(
                ledger,
                frame_index,
                &physical_address
            )
        ) {
            panic_invariant("user-address-space-destroy-index");
        }
        clear_page(physical_address);
    }
    if (
        micros_kernel_object_runtime_detach_address_space(
            process,
            root_physical_address
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        panic_invariant("user-address-space-destroy-detach");
    }
    error = MICROS_USER_ADDRESS_SPACE_OK;

done_with_scratch:
    release_scratch();
done:
    (void)kernel_report;
    riscv_irq_restore(saved_status);
    return error;
}

static bool process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool find_one_prepared_thread(
    const struct micros_kernel_objects *objects,
    struct micros_process_handle process,
    struct micros_thread_handle *thread_handle
)
{
    size_t count = 0;
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        if (
            thread->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || !process_handles_equal(thread->owner, process)
        ) {
            continue;
        }
        if (!thread->context_attached) {
            return false;
        }
        *thread_handle = (struct micros_thread_handle){
            .slot = (uint16_t)index,
            .generation = thread->generation,
        };
        ++count;
    }
    return count == 1;
}

static bool process_has_no_owned_frames(
    const struct micros_frame_ownership *ledger,
    struct micros_process_handle process
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        const struct micros_frame_owner *owner =
            &ledger->owners[frame_index];

        if (
            owner->slot == process.slot
            && owner->generation == process.generation
            && owner->kind != MICROS_FRAME_OWNER_FREE
        ) {
            return false;
        }
    }
    return true;
}

static bool reachable_user_targets_are_wired(
    const struct micros_frame_ownership *ledger,
    struct micros_process_handle process
)
{
    uint64_t frame_index;

    for (
        frame_index = 0;
        frame_index < ledger->managed_frame_count;
        ++frame_index
    ) {
        const struct micros_frame_owner *owner =
            &ledger->owners[frame_index];

        if (
            bitmap_contains(reachable_bitmap, frame_index)
            && owner->kind == MICROS_FRAME_OWNER_PROCESS_USER
            && owner->slot == process.slot
            && owner->generation == process.generation
            && ledger->handoff_targets[frame_index]
                != MICROS_FRAME_HANDOFF_VM_WIRED
        ) {
            return false;
        }
    }
    return true;
}

enum micros_user_address_space_error
micros_user_address_space_complete_wired_handoff(void)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;
    size_t index;

    saved_status = riscv_irq_save();
    error = require_bootstrap(&ledger, &objects, &kernel_report);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (
        micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
        goto done;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_process *record =
            &objects->processes[index];
        struct micros_process_handle process;
        struct micros_thread_handle thread_handle;
        enum micros_user_execution_error execution_error;

        if (record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE) {
            continue;
        }
        process = (struct micros_process_handle){
            .slot = (uint16_t)index,
            .generation = record->generation,
        };
        if (record->address_space_root == 0) {
            if (
                record->live_thread_count != 0
                || record->primary_endpoint
                    != MICROS_PROCESS_ENDPOINT_NONE
                || !process_has_no_owned_frames(ledger, process)
            ) {
                error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
                goto done;
            }
            continue;
        }
        if (
            record->live_thread_count != 1
            || !find_one_prepared_thread(
                objects,
                process,
                &thread_handle
            )
        ) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
            goto done;
        }
        execution_error = micros_user_execution_validate_context(
            thread_handle,
            &objects->threads[thread_handle.slot].user_context
        );
        if (execution_error != MICROS_USER_EXECUTION_OK) {
            error = (
                execution_error == MICROS_USER_EXECUTION_ERROR_STATE
                || execution_error == MICROS_USER_EXECUTION_ERROR_CONTEXT
                || execution_error == MICROS_USER_EXECUTION_ERROR_MAPPING
            )
                ? MICROS_USER_ADDRESS_SPACE_ERROR_STATE
                : MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
            goto done;
        }
    }
    if (!acquire_scratch()) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_BUSY;
        goto done;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct micros_process *record =
            &objects->processes[index];
        struct micros_process_handle process;

        if (
            record->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || record->address_space_root == 0
        ) {
            continue;
        }
        process = (struct micros_process_handle){
            .slot = (uint16_t)index,
            .generation = record->generation,
        };
        error = validate_locked(process, NULL);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            goto done_with_scratch;
        }
        if (!reachable_user_targets_are_wired(ledger, process)) {
            error = MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            goto done_with_scratch;
        }
    }
    error = map_ownership_error(
        micros_frame_ownership_runtime_complete_handoff(objects)
    );

done_with_scratch:
    release_scratch();
done:
    (void)kernel_report;
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_validate(
    struct micros_process_handle process
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    error = require_read_authority(
        &ledger,
        &objects,
        &kernel_report,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(objects, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (!acquire_scratch()) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_BUSY;
        goto done;
    }
    error = validate_locked(process, NULL);
    release_scratch();

done:
    (void)ledger;
    (void)objects;
    (void)kernel_report;
    (void)resolved;
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_activate(
    struct micros_process_handle process
)
{
    const struct micros_frame_ownership *ledger;
    const struct micros_kernel_objects *objects;
    const struct micros_kernel_address_space_report *kernel_report;
    const struct micros_process *resolved;
    uint64_t observed_satp;
    enum micros_user_address_space_error error;
    uintptr_t saved_status;

    saved_status = riscv_irq_save();
    error = require_read_authority(
        &ledger,
        &objects,
        &kernel_report,
        NULL
    );
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    error = resolve_process_with_root(objects, process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done;
    }
    if (!acquire_scratch()) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_BUSY;
        goto done;
    }
    error = validate_locked(process, &resolved);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        goto done_with_scratch;
    }
    observed_satp = micros_riscv_activate_sv39(
        resolved->address_space_root
    );
    if (observed_satp != expected_satp(resolved->address_space_root)) {
        error = MICROS_USER_ADDRESS_SPACE_ERROR_SATP;
        goto done_with_scratch;
    }
    error = MICROS_USER_ADDRESS_SPACE_OK;

done_with_scratch:
    release_scratch();
done:
    (void)ledger;
    (void)objects;
    (void)kernel_report;
    riscv_irq_restore(saved_status);
    return error;
}

enum micros_user_address_space_error
micros_user_address_space_activate_kernel(void)
{
    const struct micros_kernel_address_space_report *kernel_report =
        micros_kernel_address_space_report();
    uint64_t observed_satp;
    uintptr_t saved_status;

    if (kernel_report == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_INITIALIZED;
    }
    saved_status = riscv_irq_save();
    observed_satp = micros_riscv_activate_sv39(
        kernel_report->root_physical_address
    );
    riscv_irq_restore(saved_status);
    if (
        observed_satp
            != expected_satp(kernel_report->root_physical_address)
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_SATP;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
void micros_user_address_space_test_fail_after_allocations(
    size_t successful_allocations
)
{
    test_fail_after_allocations = successful_allocations;
    test_successful_allocations = 0;
}
#endif
