#ifndef MICROS_VM_BOOTSTRAP_H
#define MICROS_VM_BOOTSTRAP_H

#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap.h"
#include "micros/frame_allocator.h"
#include "micros/frame_ownership.h"
#include "micros/user_address_space.h"

enum {
    MICROS_VM_BOOT_INFO_VERSION = 1,
    MICROS_VM_BOOT_HEADER_SIZE = 192,
    MICROS_VM_PHYSICAL_RANGE_SIZE = 16,
    MICROS_VM_MANAGED_RANGE_SIZE = 24,
    MICROS_VM_ADDRESS_SPACE_SIZE = 32,
    MICROS_VM_MAPPING_SIZE = 24,
    MICROS_VM_MAX_MEMORY_RANGES =
        MICROS_FRAME_ALLOCATOR_MAX_MEMORY_RANGES,
    MICROS_VM_MAX_RESERVED_RANGES =
        MICROS_FRAME_ALLOCATOR_MAX_RESERVED_RANGES,
    MICROS_VM_MAX_MANAGED_RANGES =
        MICROS_FRAME_ALLOCATOR_MAX_MANAGED_RANGES,
    MICROS_VM_MAX_STATIC_ADDRESS_SPACES =
        MICROS_BOOTSTRAP_SERVICE_CAPACITY,
    MICROS_VM_MAX_STATIC_MAPPINGS = 4096,
    MICROS_VM_MAX_MANAGED_FRAMES =
        MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES,
    MICROS_VM_BOOT_INFO_SIZE = 364704,
};

#define MICROS_VM_BOOT_INFO_ALIGNMENT UINT64_C(4096)
#define MICROS_VM_BOOT_FLAG_NONE UINT32_C(0)

#define MICROS_VM_PERMISSION_READ UINT8_C(0x01)
#define MICROS_VM_PERMISSION_WRITE UINT8_C(0x02)
#define MICROS_VM_PERMISSION_EXECUTE UINT8_C(0x04)
#define MICROS_VM_PERMISSION_DEFINED_MASK \
    ( \
        MICROS_VM_PERMISSION_READ \
        | MICROS_VM_PERMISSION_WRITE \
        | MICROS_VM_PERMISSION_EXECUTE \
    )

enum micros_vm_frame_state {
    MICROS_VM_FRAME_UNUSED = 0,
    MICROS_VM_FRAME_FREE,
    MICROS_VM_FRAME_KERNEL,
    MICROS_VM_FRAME_SELF_WIRED,
    MICROS_VM_FRAME_SERVICE_WIRED,
    MICROS_VM_FRAME_TRANSFERABLE,
};

enum micros_vm_mapping_role {
    MICROS_VM_MAPPING_SELF_WIRED = 1,
    MICROS_VM_MAPPING_SERVICE_WIRED,
};

struct micros_vm_boot_header {
    uint32_t version;
    uint32_t header_size;
    uint32_t total_size;
    uint32_t physical_range_size;
    uint32_t managed_range_size;
    uint32_t memory_range_capacity;
    uint32_t memory_range_count;
    uint32_t reserved_range_capacity;
    uint32_t reserved_range_count;
    uint32_t managed_range_capacity;
    uint32_t managed_range_count;
    uint32_t frame_state_capacity;
    uint32_t frame_state_size;
    uint32_t service_id;
    uint32_t self_endpoint;
    uint32_t reserved0;
    uint64_t managed_frame_count;
    uint64_t free_frame_count;
    uint64_t kernel_frame_count;
    uint64_t vm_self_wired_frame_count;
    uint64_t service_wired_frame_count;
    uint64_t transferable_frame_count;
    uint64_t digest;
    uint32_t ownership_phase;
    uint32_t flags;
    uint32_t address_space_entry_size;
    uint32_t address_space_capacity;
    uint32_t address_space_count;
    uint32_t mapping_entry_size;
    uint32_t mapping_capacity;
    uint32_t mapping_count;
    uint64_t reserved[5];
};

struct micros_vm_physical_range {
    uint64_t base;
    uint64_t size;
};

struct micros_vm_managed_range {
    uint64_t base;
    uint64_t frame_count;
    uint64_t frame_index;
};

struct micros_vm_address_space {
    uint32_t service_id;
    uint32_t endpoint;
    uint32_t process_generation;
    uint16_t process_slot;
    uint16_t mapping_count;
    uint32_t mapping_index;
    uint32_t flags;
    uint64_t root_physical_address;
};

struct micros_vm_mapping {
    uint32_t process_generation;
    uint16_t process_slot;
    uint8_t permissions;
    uint8_t role;
    uint64_t virtual_address;
    uint64_t physical_address;
};

struct micros_vm_boot_info {
    struct micros_vm_boot_header header;
    struct micros_vm_physical_range
        memory_ranges[MICROS_VM_MAX_MEMORY_RANGES];
    struct micros_vm_physical_range
        reserved_ranges[MICROS_VM_MAX_RESERVED_RANGES];
    struct micros_vm_managed_range
        managed_ranges[MICROS_VM_MAX_MANAGED_RANGES];
    struct micros_vm_address_space
        address_spaces[MICROS_VM_MAX_STATIC_ADDRESS_SPACES];
    struct micros_vm_mapping
        mappings[MICROS_VM_MAX_STATIC_MAPPINGS];
    uint8_t frame_states[MICROS_VM_MAX_MANAGED_FRAMES];
};

struct micros_vm_boot_summary {
    uint32_t version;
    uint32_t managed_range_count;
    uint32_t address_space_count;
    uint32_t mapping_count;
    uint64_t managed_frame_count;
    uint64_t free_frame_count;
    uint64_t vm_self_wired_frame_count;
    uint64_t digest;
};

enum micros_vm_boot_error {
    MICROS_VM_BOOT_OK = 0,
    MICROS_VM_BOOT_ERROR_ARGUMENT,
    MICROS_VM_BOOT_ERROR_SHAPE,
    MICROS_VM_BOOT_ERROR_RANGE,
    MICROS_VM_BOOT_ERROR_IDENTITY,
    MICROS_VM_BOOT_ERROR_MAPPING,
    MICROS_VM_BOOT_ERROR_COUNT,
    MICROS_VM_BOOT_ERROR_STATE,
    MICROS_VM_BOOT_ERROR_DIGEST,
};

enum micros_vm_boot_error micros_vm_boot_digest(
    const struct micros_vm_boot_info *info,
    uint64_t *digest
);

enum micros_vm_boot_error micros_vm_boot_validate(
    const struct micros_vm_boot_info *info,
    struct micros_vm_boot_summary *summary
);

enum micros_vm_boot_error micros_vm_boot_finalize(
    struct micros_vm_boot_info *info,
    struct micros_vm_boot_summary *summary
);

enum micros_vm_boot_error micros_vm_boot_build(
    struct micros_vm_boot_info *info,
    size_t address_space_count,
    size_t mapping_count,
    const struct micros_frame_allocator *allocator,
    const struct micros_frame_ownership *ownership,
    uint32_t vm_service_id,
    uint32_t vm_endpoint,
    struct micros_process_handle vm_process,
    struct micros_vm_boot_summary *summary
);

enum micros_vm_boot_error micros_vm_boot_validate_authority(
    const struct micros_vm_boot_info *info,
    const struct micros_frame_allocator *allocator,
    const struct micros_frame_ownership *ownership,
    struct micros_process_handle vm_process
);

_Static_assert(
    sizeof(struct micros_vm_boot_header) == MICROS_VM_BOOT_HEADER_SIZE,
    "VM boot header ABI changed"
);
_Static_assert(
    offsetof(struct micros_vm_boot_header, version) == 0
        && offsetof(struct micros_vm_boot_header, header_size) == 4
        && offsetof(struct micros_vm_boot_header, total_size) == 8
        && offsetof(
            struct micros_vm_boot_header,
            physical_range_size
        ) == 12
        && offsetof(
            struct micros_vm_boot_header,
            managed_range_size
        ) == 16
        && offsetof(
            struct micros_vm_boot_header,
            memory_range_capacity
        ) == 20
        && offsetof(
            struct micros_vm_boot_header,
            memory_range_count
        ) == 24
        && offsetof(
            struct micros_vm_boot_header,
            reserved_range_capacity
        ) == 28
        && offsetof(
            struct micros_vm_boot_header,
            reserved_range_count
        ) == 32
        && offsetof(
            struct micros_vm_boot_header,
            managed_range_capacity
        ) == 36
        && offsetof(
            struct micros_vm_boot_header,
            managed_range_count
        ) == 40
        && offsetof(
            struct micros_vm_boot_header,
            frame_state_capacity
        ) == 44
        && offsetof(
            struct micros_vm_boot_header,
            frame_state_size
        ) == 48
        && offsetof(struct micros_vm_boot_header, service_id) == 52
        && offsetof(struct micros_vm_boot_header, self_endpoint) == 56
        && offsetof(struct micros_vm_boot_header, reserved0) == 60
        && offsetof(
            struct micros_vm_boot_header,
            managed_frame_count
        ) == 64
        && offsetof(
            struct micros_vm_boot_header,
            free_frame_count
        ) == 72
        && offsetof(
            struct micros_vm_boot_header,
            kernel_frame_count
        ) == 80
        && offsetof(
            struct micros_vm_boot_header,
            vm_self_wired_frame_count
        ) == 88
        && offsetof(
            struct micros_vm_boot_header,
            service_wired_frame_count
        ) == 96
        && offsetof(
            struct micros_vm_boot_header,
            transferable_frame_count
        ) == 104
        && offsetof(struct micros_vm_boot_header, digest) == 112
        && offsetof(
            struct micros_vm_boot_header,
            ownership_phase
        ) == 120
        && offsetof(struct micros_vm_boot_header, flags) == 124
        && offsetof(
            struct micros_vm_boot_header,
            address_space_entry_size
        ) == 128
        && offsetof(
            struct micros_vm_boot_header,
            address_space_capacity
        ) == 132
        && offsetof(
            struct micros_vm_boot_header,
            address_space_count
        ) == 136
        && offsetof(
            struct micros_vm_boot_header,
            mapping_entry_size
        ) == 140
        && offsetof(
            struct micros_vm_boot_header,
            mapping_capacity
        ) == 144
        && offsetof(
            struct micros_vm_boot_header,
            mapping_count
        ) == 148
        && offsetof(struct micros_vm_boot_header, reserved) == 152,
    "VM boot header offsets changed"
);
_Static_assert(
    sizeof(struct micros_vm_physical_range)
        == MICROS_VM_PHYSICAL_RANGE_SIZE,
    "VM physical range ABI changed"
);
_Static_assert(
    offsetof(struct micros_vm_physical_range, base) == 0
        && offsetof(struct micros_vm_physical_range, size) == 8,
    "VM physical range offsets changed"
);
_Static_assert(
    sizeof(struct micros_vm_managed_range)
        == MICROS_VM_MANAGED_RANGE_SIZE,
    "VM managed range ABI changed"
);
_Static_assert(
    offsetof(struct micros_vm_managed_range, base) == 0
        && offsetof(struct micros_vm_managed_range, frame_count) == 8
        && offsetof(struct micros_vm_managed_range, frame_index) == 16,
    "VM managed range offsets changed"
);
_Static_assert(
    sizeof(struct micros_vm_address_space)
        == MICROS_VM_ADDRESS_SPACE_SIZE,
    "VM address-space ABI changed"
);
_Static_assert(
    offsetof(struct micros_vm_address_space, service_id) == 0
        && offsetof(struct micros_vm_address_space, endpoint) == 4
        && offsetof(
            struct micros_vm_address_space,
            process_generation
        ) == 8
        && offsetof(
            struct micros_vm_address_space,
            process_slot
        ) == 12
        && offsetof(struct micros_vm_address_space, mapping_count) == 14
        && offsetof(struct micros_vm_address_space, mapping_index) == 16
        && offsetof(struct micros_vm_address_space, flags) == 20
        && offsetof(
            struct micros_vm_address_space,
            root_physical_address
        ) == 24,
    "VM address-space offsets changed"
);
_Static_assert(
    sizeof(struct micros_vm_mapping) == MICROS_VM_MAPPING_SIZE,
    "VM mapping ABI changed"
);
_Static_assert(
    offsetof(struct micros_vm_mapping, process_generation) == 0
        && offsetof(struct micros_vm_mapping, process_slot) == 4
        && offsetof(struct micros_vm_mapping, permissions) == 6
        && offsetof(struct micros_vm_mapping, role) == 7
        && offsetof(struct micros_vm_mapping, virtual_address) == 8
        && offsetof(struct micros_vm_mapping, physical_address) == 16,
    "VM mapping offsets changed"
);
_Static_assert(
    sizeof(struct micros_vm_boot_info) == MICROS_VM_BOOT_INFO_SIZE,
    "VM boot information ABI changed"
);
_Static_assert(
    offsetof(struct micros_vm_boot_info, memory_ranges) == 192
        && offsetof(struct micros_vm_boot_info, reserved_ranges) == 448
        && offsetof(struct micros_vm_boot_info, managed_ranges) == 1728
        && offsetof(struct micros_vm_boot_info, address_spaces) == 4032
        && offsetof(struct micros_vm_boot_info, mappings) == 4256
        && offsetof(struct micros_vm_boot_info, frame_states) == 102560,
    "VM boot information offsets changed"
);
_Static_assert(
    MICROS_VM_BOOT_INFO_SIZE <= 90 * MICROS_FRAME_SIZE,
    "VM boot information exceeds 90 pages"
);

#endif
