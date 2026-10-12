#ifndef MICROS_VM_BOOTSTRAP_TEST_INTERNAL_H
#define MICROS_VM_BOOTSTRAP_TEST_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/frame_ownership.h"
#include "micros/vm_bootstrap.h"

struct vm_bootstrap_test_context {
    _Alignas(MICROS_VM_BOOT_INFO_ALIGNMENT)
    struct micros_vm_boot_info info;
    struct micros_frame_allocator build_allocator;
    struct micros_frame_ownership build_ownership;
    uint64_t build_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
    uint64_t build_frames[8];
};

struct vm_bootstrap_test_context *vm_bootstrap_test_context(void);

void vm_bootstrap_test_initialize_valid_info(
    struct vm_bootstrap_test_context *context
);

bool vm_bootstrap_test_expect_error(
    struct vm_bootstrap_test_context *context,
    enum micros_vm_boot_error expected
);

bool vm_bootstrap_test_storage_is_zero(
    const void *storage,
    size_t size
);

bool vm_bootstrap_test_initialize_build_fixture(
    struct vm_bootstrap_test_context *context
);

bool vm_bootstrap_test_test_abi_contract(
    struct vm_bootstrap_test_context *context
);
bool vm_bootstrap_test_test_valid_snapshot(
    struct vm_bootstrap_test_context *context
);
bool vm_bootstrap_test_test_finalize_is_failure_atomic(
    struct vm_bootstrap_test_context *context
);
bool vm_bootstrap_test_test_rejections(
    struct vm_bootstrap_test_context *context
);
bool vm_bootstrap_test_test_misaligned_storage(
    struct vm_bootstrap_test_context *context
);
bool vm_bootstrap_test_test_builds_from_authoritative_state(
    struct vm_bootstrap_test_context *context
);
bool vm_bootstrap_test_test_build_failure_clears_candidate(
    struct vm_bootstrap_test_context *context
);

#endif
