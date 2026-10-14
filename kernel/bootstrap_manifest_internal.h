#ifndef MICROS_KERNEL_BOOTSTRAP_MANIFEST_INTERNAL_H
#define MICROS_KERNEL_BOOTSTRAP_MANIFEST_INTERNAL_H

#include "micros/bootstrap.h"

struct bootstrap_manifest_candidate {
    struct micros_bootstrap_manifest_plan plan;
    uint64_t active_ids;
    uint64_t total_pages;
    uint64_t prepared_mapping_count;
    uint32_t controller_count;
    uint32_t vm_count;
    uint32_t console_count;
    uint32_t pm_count;
    uint8_t controller_profile_id;
    const struct micros_bootstrap_manifest_entry *pm_entry;
    const struct micros_bootstrap_manifest_entry *pm_identity_entry;
    const struct micros_bootstrap_manifest_entry *console_entry;
    const struct micros_bootstrap_manifest_entry *console_identity_entry;
};

void bootstrap_manifest_copy_bytes(
    void *destination,
    const void *source,
    size_t size
);
void bootstrap_manifest_clear_bytes(void *storage, size_t size);
bool bootstrap_manifest_bytes_are_zero(const void *storage, size_t size);
bool bootstrap_manifest_pointer_is_aligned(
    const void *pointer,
    size_t alignment
);
bool bootstrap_manifest_fixed_name_is_canonical(
    const char name[MICROS_BOOTSTRAP_NAME_SIZE]
);
bool bootstrap_manifest_fixed_names_equal(
    const char left[MICROS_BOOTSTRAP_NAME_SIZE],
    const char right[MICROS_BOOTSTRAP_NAME_SIZE]
);
bool bootstrap_manifest_fixed_name_matches_literal(
    const char name[MICROS_BOOTSTRAP_NAME_SIZE],
    const char *literal
);

const struct micros_privilege_profile *bootstrap_manifest_find_profile(
    const struct micros_privilege_profile *profiles,
    size_t count,
    uint8_t profile_id
);
enum micros_bootstrap_error bootstrap_manifest_validate_profile_table(
    const struct micros_privilege_profile *profiles,
    size_t profile_count
);
enum micros_bootstrap_error
bootstrap_manifest_validate_profile_relationships(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    uint8_t controller_profile_id
);

#endif
