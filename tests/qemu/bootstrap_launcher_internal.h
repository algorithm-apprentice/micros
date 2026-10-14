#ifndef MICROS_TESTS_QEMU_BOOTSTRAP_LAUNCHER_INTERNAL_H
#define MICROS_TESTS_QEMU_BOOTSTRAP_LAUNCHER_INTERNAL_H

#include "micros/runtime.h"
#include "micros/bootstrap_control.h"
#include "tests/qemu/bootstrap_protocol.h"

extern volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;
extern const volatile uint64_t micros_bootstrap_launcher_rodata;
extern volatile uint64_t micros_bootstrap_launcher_data;

enum launcher_service_state {
    LAUNCHER_SERVICE_PREPARED = 1,
    LAUNCHER_SERVICE_STARTING,
    LAUNCHER_SERVICE_READY,
};

struct bootstrap_launcher_context {
    const struct micros_bootstrap_manifest *manifest;
    uint32_t order[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    uint8_t state[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
};

uint32_t bootstrap_launcher_read_u32_le(const uint8_t *bytes);
uint64_t bootstrap_launcher_read_u64_le(const uint8_t *bytes);
bool bootstrap_launcher_bytes_are_zero(const void *storage, size_t size);
bool bootstrap_launcher_name_is_canonical(
    const char name[MICROS_BOOTSTRAP_NAME_SIZE]
);
bool bootstrap_launcher_names_equal(
    const char left[MICROS_BOOTSTRAP_NAME_SIZE],
    const char right[MICROS_BOOTSTRAP_NAME_SIZE]
);
int bootstrap_launcher_find_config_endpoint(micros_endpoint_t endpoint);
int bootstrap_launcher_find_manifest_service(
    const struct micros_bootstrap_manifest *manifest,
    uint32_t service_id
);
bool bootstrap_launcher_validate_manifest(
    const struct micros_bootstrap_manifest *manifest,
    uint32_t order[MICROS_BOOTSTRAP_SERVICE_CAPACITY]
);
bool bootstrap_launcher_validate_configuration(
    const struct micros_bootstrap_manifest *manifest
);
bool bootstrap_launcher_console_mapped_notification_is_canonical(
    const struct micros_ipc_message *message
);
void bootstrap_launcher_validate_ready_message(
    const struct bootstrap_launcher_context *launcher,
    uint32_t service_id,
    const struct micros_ipc_message *message
);
int64_t bootstrap_launcher_bootstrap_control(
    uint32_t command,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t value,
    uint32_t detail
);
_Noreturn void bootstrap_launcher_fail(
    uint32_t service_id,
    micros_endpoint_t endpoint,
    enum micros_bootstrap_failure_reason reason,
    uint32_t detail
);

#endif
