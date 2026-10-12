#include "kernel/user_address_space_internal.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "micros/frame_allocator.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"

static uint64_t reachable_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
static uint64_t release_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
static bool scratch_busy;

#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
static size_t test_fail_after_allocations = SIZE_MAX;
static size_t test_successful_allocations;
#endif

_Static_assert(sizeof(reachable_bitmap) * 8 == MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES,
               "reachability bitmap must cover every managed frame");
_Static_assert(sizeof(release_bitmap) * 8 == MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES,
               "release bitmap must cover every managed frame");

static void clear_scratch_bitmap(uint64_t *bitmap, size_t word_count) {
    size_t index;

    for (index = 0; index < word_count; ++index) {
        bitmap[index] = 0;
    }
}

#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
void micros_user_address_space_test_set_allocation_limit(
    size_t successful_allocations
)
{
    test_fail_after_allocations = successful_allocations;
    test_successful_allocations = 0;
}
#endif

uintptr_t micros_user_address_space_runtime_lock(void) {
    return riscv_irq_save();
}

void micros_user_address_space_runtime_unlock(uintptr_t saved_status) {
    riscv_irq_restore(saved_status);
}

enum micros_user_address_space_error
micros_user_address_space_map_object_error(enum micros_kernel_object_error error) {
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

enum micros_user_address_space_error
micros_user_address_space_map_ownership_error(enum micros_frame_ownership_error error) {
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
    case MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION:
        return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
    default:
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
}

enum micros_user_address_space_error
micros_user_address_space_allocate_owned_frame(
    struct micros_frame_owner owner,
    uint64_t *physical_address
)
{
    enum micros_frame_ownership_error error;

#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
    if (test_successful_allocations >= test_fail_after_allocations) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ALLOCATION;
    }
#endif
    error = micros_frame_ownership_runtime_allocate(
        owner,
        physical_address
    );
    if (error != MICROS_FRAME_OWNERSHIP_OK) {
        return micros_user_address_space_map_ownership_error(error);
    }
#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST
    ++test_successful_allocations;
#endif
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error micros_user_address_space_runtime_require_bootstrap(
    struct micros_user_address_space_runtime_view *view) {
    const struct micros_frame_ownership *ledger = micros_frame_ownership_runtime_ledger();
    const struct micros_kernel_objects *objects = micros_kernel_object_runtime_registry();
    const struct micros_kernel_address_space_report *kernel_report =
        micros_kernel_address_space_report();

    if (view == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (ledger == NULL || objects == NULL || kernel_report == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_INITIALIZED;
    }
    if (ledger->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
    }
    *view = (struct micros_user_address_space_runtime_view){
        .ledger = ledger,
        .objects = objects,
        .kernel_report = kernel_report,
        .ownership_phase = ledger->phase,
    };
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error micros_user_address_space_runtime_require_read(
    struct micros_user_address_space_runtime_view *view) {
    const struct micros_frame_ownership *ledger = micros_frame_ownership_runtime_ledger();
    const struct micros_kernel_objects *objects = micros_kernel_object_runtime_registry();
    const struct micros_kernel_address_space_report *kernel_report =
        micros_kernel_address_space_report();
    if (view == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (ledger == NULL || objects == NULL || kernel_report == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_INITIALIZED;
    }
    if (ledger->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP &&
        ledger->phase != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
    }
    *view = (struct micros_user_address_space_runtime_view){
        .ledger = ledger,
        .objects = objects,
        .kernel_report = kernel_report,
        .ownership_phase = ledger->phase,
    };
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error micros_user_address_space_runtime_resolve_process(
    struct micros_user_address_space_runtime_view *view, struct micros_process_handle process,
    bool require_root, const struct micros_process **resolved) {
    const struct micros_process *observed;
    enum micros_user_address_space_error error;

    if (view == NULL || view->objects == NULL || resolved == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    error = micros_user_address_space_map_object_error(
        micros_process_resolve(view->objects, process, &observed));
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (require_root && observed->address_space_root == 0) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
    }
    view->process = observed;
    view->process_handle = process;
    view->process_root = observed->address_space_root;
    *resolved = observed;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_acquire_scratch(const struct micros_user_address_space_runtime_view *view,
                                          struct micros_user_address_space_scratch_lease *lease) {
    size_t word_count;

    if (view == NULL || lease == NULL || view->ledger == NULL) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (scratch_busy) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_BUSY;
    }
    if (riscv_irq_is_enabled() || view->ledger->managed_frame_count == 0 ||
        view->ledger->managed_frame_count > MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    word_count = (size_t)((view->ledger->managed_frame_count + 63) / 64);
    scratch_busy = true;
    clear_scratch_bitmap(reachable_bitmap, word_count);
    clear_scratch_bitmap(release_bitmap, word_count);
    *lease = (struct micros_user_address_space_scratch_lease){
        .active = true,
        .frame_count = view->ledger->managed_frame_count,
        .word_count = word_count,
        .reachable_bitmap = reachable_bitmap,
        .release_bitmap = release_bitmap,
    };
    return MICROS_USER_ADDRESS_SPACE_OK;
}

void micros_user_address_space_release_scratch(
    struct micros_user_address_space_scratch_lease *lease) {
    if (lease == NULL || !lease->active || !scratch_busy ||
        lease->reachable_bitmap != reachable_bitmap || lease->release_bitmap != release_bitmap ||
        lease->word_count > MICROS_FRAME_ALLOCATOR_BITMAP_WORDS) {
        micros_user_address_space_panic_invariant("user-address-space-scratch-release");
    }
    clear_scratch_bitmap(reachable_bitmap, lease->word_count);
    clear_scratch_bitmap(release_bitmap, lease->word_count);
    lease->active = false;
    lease->frame_count = 0;
    lease->word_count = 0;
    lease->reachable_bitmap = NULL;
    lease->release_bitmap = NULL;
    scratch_busy = false;
}

bool micros_user_address_space_process_handles_equal(struct micros_process_handle left,
                                                     struct micros_process_handle right) {
    return (left.slot == right.slot && left.generation == right.generation);
}

static uint64_t read_satp(void) {
    uint64_t satp;

    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}

uint64_t micros_user_address_space_expected_satp(uint64_t root) {
    return MICROS_RISCV_SATP_MODE_SV39 | (root >> 12);
}

uint64_t micros_user_address_space_active_root(void) {
    const uint64_t ppn_mask = (UINT64_C(1) << 44) - UINT64_C(1);
    uint64_t satp = read_satp();

    if ((satp & MICROS_RISCV_SATP_MODE_SV39) == 0) {
        return 0;
    }
    return (satp & ppn_mask) << 12;
}

_Noreturn void micros_user_address_space_panic_invariant(const char *reason) {
    struct micros_hart *hart = micros_kernel_object_runtime_boot_hart();
    uintptr_t hart_id = hart == NULL ? 0 : hart->hardware_id;

    MICROS_PANIC(hart_id, reason);
}
