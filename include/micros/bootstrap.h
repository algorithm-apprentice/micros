#ifndef MICROS_BOOTSTRAP_H
#define MICROS_BOOTSTRAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/endpoint.h"
#include "micros/user_address_space.h"

enum {
    MICROS_BOOTSTRAP_MANIFEST_VERSION = 1,
    MICROS_BOOTSTRAP_SERVICE_CAPACITY = 6,
    MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE = 64,
    MICROS_BOOTSTRAP_MANIFEST_ENTRY_SIZE = 192,
    MICROS_BOOTSTRAP_MANIFEST_SIZE =
        MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE
        + MICROS_BOOTSTRAP_SERVICE_CAPACITY
            * MICROS_BOOTSTRAP_MANIFEST_ENTRY_SIZE,
    MICROS_BOOTSTRAP_NAME_SIZE = 32,
    MICROS_BOOTSTRAP_IMAGE_VERSION = 1,
};

#define MICROS_BOOTSTRAP_MANIFEST_MAGIC UINT32_C(0x3153424d)
#define MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL UINT64_C(0x1)
#define MICROS_BOOTSTRAP_ROLE_CONTROLLER UINT32_C(0x00000001)
#define MICROS_BOOTSTRAP_ROLE_VM UINT32_C(0x00000002)
#define MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER UINT32_C(0x00000004)
#define MICROS_BOOTSTRAP_ROLE_DEFINED_MASK \
    ( \
        MICROS_BOOTSTRAP_ROLE_CONTROLLER \
        | MICROS_BOOTSTRAP_ROLE_VM \
        | MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER \
    )
#define MICROS_BOOTSTRAP_MANIFEST_VIEW UINT64_C(0x000000007fffe000)
#define MICROS_BOOTSTRAP_UART_BASE UINT64_C(0x0000000010000000)
#define MICROS_BOOTSTRAP_UART_LENGTH UINT64_C(0x0000000000001000)
#define MICROS_BOOTSTRAP_UART_IRQ UINT32_C(10)

struct micros_bootstrap_manifest_header {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint16_t entry_size;
    uint16_t entry_capacity;
    uint16_t entry_count;
    uint16_t reserved0;
    uint32_t total_user_page_limit;
    uint32_t flags;
    uint32_t manifest_size;
    uint32_t reserved1;
    uint64_t reserved2[4];
};

struct micros_bootstrap_manifest_entry {
    uint32_t service_id;
    uint32_t image_id;
    uint16_t process_slot;
    uint16_t stack_page_count;
    uint8_t profile_id;
    uint8_t reserved0[3];
    char service_name[MICROS_BOOTSTRAP_NAME_SIZE];
    char profile_name[MICROS_BOOTSTRAP_NAME_SIZE];
    uint64_t prerequisites;
    uint64_t ready_timeout_counter_ticks;
    uint32_t user_page_limit;
    uint32_t role_flags;
    uint64_t device_base;
    uint64_t device_length;
    uint32_t irq_source;
    uint32_t reserved1;
    uint64_t reserved2[8];
};

struct micros_bootstrap_manifest {
    struct micros_bootstrap_manifest_header header;
    struct micros_bootstrap_manifest_entry
        entries[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
};

struct micros_bootstrap_expected_service {
    uint32_t service_id;
    uint32_t image_id;
    uint16_t process_slot;
    uint8_t profile_id;
    uint8_t reserved;
    char service_name[MICROS_BOOTSTRAP_NAME_SIZE];
    char profile_name[MICROS_BOOTSTRAP_NAME_SIZE];
    uint64_t prerequisites;
    uint32_t role_flags;
    uint32_t irq_source;
    uint64_t device_base;
    uint64_t device_length;
};

struct micros_bootstrap_image_info {
    uint32_t version;
    uint32_t image_id;
    uint64_t entry;
    uint64_t config_address;
    uint32_t config_size;
    uint32_t page_count;
    uint64_t image_end;
    uint64_t vm_boot_info_address;
    uint32_t vm_boot_info_size;
    bool config_initially_zero;
    bool vm_boot_info_initially_zero;
};

struct micros_bootstrap_manifest_plan {
    uint16_t entry_count;
    uint16_t reserved;
    uint32_t total_user_page_limit;
    uint32_t controller_service_id;
    uint32_t vm_service_id;
    uint32_t console_service_id;
    uint32_t ordered_service_ids[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    uint16_t ordered_manifest_indices[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
};

enum micros_bootstrap_error {
    MICROS_BOOTSTRAP_OK = 0,
    MICROS_BOOTSTRAP_ERROR_ARGUMENT,
    MICROS_BOOTSTRAP_ERROR_SHAPE,
    MICROS_BOOTSTRAP_ERROR_CAPACITY,
    MICROS_BOOTSTRAP_ERROR_IDENTITY,
    MICROS_BOOTSTRAP_ERROR_PROFILE,
    MICROS_BOOTSTRAP_ERROR_IMAGE,
    MICROS_BOOTSTRAP_ERROR_RANGE,
    MICROS_BOOTSTRAP_ERROR_ROLE,
    MICROS_BOOTSTRAP_ERROR_TOPOLOGY,
    MICROS_BOOTSTRAP_ERROR_STATE,
    MICROS_BOOTSTRAP_ERROR_INVARIANT,
};

enum micros_bootstrap_diagnostic_reason {
    MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_HEADER = 1,
    MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_ENTRY,
    MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_PROFILE,
    MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_IMAGE,
    MICROS_BOOTSTRAP_DIAGNOSTIC_MANIFEST_CYCLE,
    MICROS_BOOTSTRAP_DIAGNOSTIC_PREPARE,
    MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_ORDER,
    MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
    MICROS_BOOTSTRAP_DIAGNOSTIC_READY_MALFORMED,
    MICROS_BOOTSTRAP_DIAGNOSTIC_READY_FOREIGN,
    MICROS_BOOTSTRAP_DIAGNOSTIC_READY_EARLY,
    MICROS_BOOTSTRAP_DIAGNOSTIC_READY_DUPLICATE,
    MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE,
    MICROS_BOOTSTRAP_DIAGNOSTIC_READY_TIMEOUT,
    MICROS_BOOTSTRAP_DIAGNOSTIC_SERVICE_FAULT,
    MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
    MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION,
};

struct micros_bootstrap_diagnostic {
    enum micros_bootstrap_diagnostic_reason reason;
    uint32_t service_id;
    micros_endpoint_t endpoint;
    uint64_t detail;
};

enum micros_bootstrap_phase {
    MICROS_BOOTSTRAP_PHASE_UNINITIALIZED = 0,
    MICROS_BOOTSTRAP_PHASE_PREPARING,
    MICROS_BOOTSTRAP_PHASE_RUNNING,
    MICROS_BOOTSTRAP_PHASE_SEALED,
    MICROS_BOOTSTRAP_PHASE_FAILED,
};

enum micros_bootstrap_service_state {
    MICROS_BOOTSTRAP_SERVICE_PREPARED = 1,
    MICROS_BOOTSTRAP_SERVICE_STARTING,
    MICROS_BOOTSTRAP_SERVICE_READY,
};

enum micros_bootstrap_endpoint_state {
    MICROS_BOOTSTRAP_ENDPOINT_RESERVED = 1,
    MICROS_BOOTSTRAP_ENDPOINT_ACTIVE,
    MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY,
};

enum micros_bootstrap_ready_class {
    MICROS_BOOTSTRAP_READY_ACCEPT = 0,
    MICROS_BOOTSTRAP_READY_MALFORMED,
    MICROS_BOOTSTRAP_READY_FOREIGN,
    MICROS_BOOTSTRAP_READY_EARLY,
    MICROS_BOOTSTRAP_READY_DUPLICATE,
};

struct micros_bootstrap_runtime_entry {
    uint32_t service_id;
    uint64_t prerequisites;
    uint64_t ready_timeout_counter_ticks;
    uint64_t ready_deadline;
    enum micros_bootstrap_service_state state;
    enum micros_bootstrap_endpoint_state endpoint_state;
    bool profile_installed;
    bool scheduler_assigned;
};

struct micros_bootstrap_runtime {
    enum micros_bootstrap_phase phase;
    uint16_t entry_count;
    uint16_t next_order_index;
    uint32_t controller_service_id;
    uint32_t starting_service_id;
    struct micros_bootstrap_runtime_entry
        entries[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    uint32_t ordered_service_ids[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
};

enum micros_bootstrap_error micros_bootstrap_manifest_validate(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_expected_service *expected_services,
    size_t expected_service_count,
    const struct micros_bootstrap_image_info *images,
    size_t image_count,
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    uint64_t available_user_pages,
    struct micros_bootstrap_manifest_plan *plan
);

enum micros_bootstrap_error micros_bootstrap_manifest_validate_detailed(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_expected_service *expected_services,
    size_t expected_service_count,
    const struct micros_bootstrap_image_info *images,
    size_t image_count,
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    uint64_t available_user_pages,
    struct micros_bootstrap_manifest_plan *plan,
    struct micros_bootstrap_diagnostic *diagnostic
);

enum micros_bootstrap_error micros_bootstrap_runtime_initialize(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_bootstrap_manifest_plan *plan,
    struct micros_bootstrap_runtime *runtime
);

enum micros_bootstrap_error micros_bootstrap_runtime_release(
    struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint64_t now
);

enum micros_bootstrap_error micros_bootstrap_runtime_accept_ready(
    struct micros_bootstrap_runtime *runtime,
    uint32_t service_id,
    uint64_t now
);

enum micros_bootstrap_error micros_bootstrap_runtime_complete(
    struct micros_bootstrap_runtime *runtime
);

enum micros_bootstrap_ready_class micros_bootstrap_classify_ready(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t source_service_id,
    uint32_t payload_service_id,
    bool message_shape_valid
);

struct micros_bootstrap_service_endpoint {
    uint32_t service_id;
    uint32_t endpoint;
};

struct micros_bootstrap_service_config {
    uint32_t version;
    uint32_t manifest_version;
    uint32_t service_id;
    uint32_t self_endpoint;
    uint32_t launcher_endpoint;
    uint32_t service_count;
    struct micros_bootstrap_service_endpoint
        services[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    uint64_t manifest_view_address;
    uint64_t reserved[6];
};

_Static_assert(
    sizeof(struct micros_bootstrap_manifest_header)
        == MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE,
    "bootstrap manifest header ABI changed"
);
_Static_assert(
    offsetof(struct micros_bootstrap_manifest_header, magic) == 0
        && offsetof(
            struct micros_bootstrap_manifest_header,
            version
        ) == 4
        && offsetof(
            struct micros_bootstrap_manifest_header,
            header_size
        ) == 6
        && offsetof(
            struct micros_bootstrap_manifest_header,
            entry_size
        ) == 8
        && offsetof(
            struct micros_bootstrap_manifest_header,
            entry_capacity
        ) == 10
        && offsetof(
            struct micros_bootstrap_manifest_header,
            entry_count
        ) == 12
        && offsetof(
            struct micros_bootstrap_manifest_header,
            reserved0
        ) == 14
        && offsetof(
            struct micros_bootstrap_manifest_header,
            total_user_page_limit
        ) == 16
        && offsetof(
            struct micros_bootstrap_manifest_header,
            flags
        ) == 20
        && offsetof(
            struct micros_bootstrap_manifest_header,
            manifest_size
        ) == 24
        && offsetof(
            struct micros_bootstrap_manifest_header,
            reserved1
        ) == 28
        && offsetof(
            struct micros_bootstrap_manifest_header,
            reserved2
        ) == 32,
    "bootstrap manifest header offsets changed"
);
_Static_assert(
    sizeof(struct micros_bootstrap_manifest_entry)
        == MICROS_BOOTSTRAP_MANIFEST_ENTRY_SIZE,
    "bootstrap manifest entry ABI changed"
);
_Static_assert(
    offsetof(struct micros_bootstrap_manifest_entry, service_id) == 0
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            image_id
        ) == 4
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            process_slot
        ) == 8
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            stack_page_count
        ) == 10
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            profile_id
        ) == 12
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            reserved0
        ) == 13
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            service_name
        ) == 16
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            profile_name
        ) == 48
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            prerequisites
        ) == 80
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            ready_timeout_counter_ticks
        ) == 88
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            user_page_limit
        ) == 96
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            role_flags
        ) == 100
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            device_base
        ) == 104
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            device_length
        ) == 112
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            irq_source
        ) == 120
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            reserved1
        ) == 124
        && offsetof(
            struct micros_bootstrap_manifest_entry,
            reserved2
        ) == 128,
    "bootstrap manifest entry offsets changed"
);
_Static_assert(
    sizeof(struct micros_bootstrap_manifest)
        == MICROS_BOOTSTRAP_MANIFEST_SIZE,
    "bootstrap manifest ABI changed"
);
_Static_assert(
    offsetof(struct micros_bootstrap_manifest, header) == 0
        && offsetof(struct micros_bootstrap_manifest, entries)
            == MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE,
    "bootstrap manifest offsets changed"
);
_Static_assert(
    sizeof(struct micros_bootstrap_service_endpoint) == 8
        && offsetof(
            struct micros_bootstrap_service_endpoint,
            service_id
        ) == 0
        && offsetof(
            struct micros_bootstrap_service_endpoint,
            endpoint
        ) == 4,
    "bootstrap service endpoint ABI changed"
);
_Static_assert(
    sizeof(struct micros_bootstrap_service_config) == 128,
    "bootstrap service configuration ABI changed"
);
_Static_assert(
    offsetof(struct micros_bootstrap_service_config, version) == 0
        && offsetof(
            struct micros_bootstrap_service_config,
            manifest_version
        ) == 4
        && offsetof(
            struct micros_bootstrap_service_config,
            service_id
        ) == 8
        && offsetof(
            struct micros_bootstrap_service_config,
            self_endpoint
        ) == 12
        && offsetof(
            struct micros_bootstrap_service_config,
            launcher_endpoint
        ) == 16
        && offsetof(
            struct micros_bootstrap_service_config,
            service_count
        ) == 20
        && offsetof(
            struct micros_bootstrap_service_config,
            services
        ) == 24
        && offsetof(
            struct micros_bootstrap_service_config,
            manifest_view_address
        ) == 72
        && offsetof(
            struct micros_bootstrap_service_config,
            reserved
        ) == 80,
    "bootstrap service configuration offsets changed"
);

#endif
