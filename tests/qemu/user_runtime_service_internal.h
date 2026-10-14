#ifndef MICROS_TESTS_QEMU_USER_RUNTIME_SERVICE_INTERNAL_H
#define MICROS_TESTS_QEMU_USER_RUNTIME_SERVICE_INTERNAL_H

#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tests/qemu/user_runtime_protocol.h"

enum {
    TEST_TRANSFER_SIZE = 32,
    TEST_BSS_PROBE_SIZE = 64,
};

struct test_region {
    uint64_t before;
    uint8_t bytes[TEST_TRANSFER_SIZE];
    uint64_t after;
};

extern volatile struct micros_user_runtime_test_config
    micros_user_runtime_test_config;
extern const volatile uint64_t micros_user_runtime_test_rodata;
extern volatile uint64_t micros_user_runtime_test_data_sentinel;
extern volatile struct test_region micros_user_runtime_test_read_region;
extern volatile struct test_region micros_user_runtime_test_write_region;
extern volatile struct test_region micros_user_runtime_test_server_region;
extern volatile uint8_t
    micros_user_runtime_test_bss_probe[TEST_BSS_PROBE_SIZE];

void micros_user_runtime_test_probe_rodata(
    volatile const uint64_t *address,
    uint64_t value
);
uint64_t micros_user_runtime_test_raw_send_probe(
    micros_endpoint_t destination,
    const struct micros_ipc_message *message
);

bool user_runtime_service_bytes_equal(
    const volatile uint8_t *left,
    const uint8_t *right,
    size_t length
);
void user_runtime_service_put_u32(uint8_t *bytes, uint32_t value);
uint32_t user_runtime_service_get_u32(const uint8_t *bytes);
void user_runtime_service_fill_message(
    struct micros_ipc_message *message,
    uint32_t type,
    uint8_t seed
);
bool user_runtime_service_message_matches(
    const struct micros_ipc_message *message,
    micros_endpoint_t source,
    uint32_t type,
    uint8_t seed,
    bool require_token
);
bool user_runtime_service_notification_matches(
    const struct micros_ipc_message *message,
    micros_endpoint_t source
);
bool user_runtime_service_initial_bss_is_zero(void);
bool user_runtime_service_memory_support_works(void);

uint64_t user_runtime_service_run_client(void);
uint64_t user_runtime_service_run_server(void);
uint64_t user_runtime_service_run_peer(void);

#endif
