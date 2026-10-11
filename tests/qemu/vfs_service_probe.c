#include "tests/qemu/vfs_service_probe_internal.h"

#include "micros/bootstrap_control.h"
#include "tests/qemu/vfs_service_protocol.h"

#define MICROS_VFS_TEST_PROBE_DATA_MAGIC UINT64_C(0x56465350524f4245)
enum {
    MICROS_VFS_TEST_PROBE_DATA_WORDS = 4096 / sizeof(uint64_t),
};

volatile struct micros_bootstrap_service_config micros_bootstrap_service_config;

static volatile uint64_t probe_initialized_data[MICROS_VFS_TEST_PROBE_DATA_WORDS] = {
    MICROS_VFS_TEST_PROBE_DATA_MAGIC,
};
static _Alignas(4096) struct vfs_service_probe_context probe_context;

void micros_vfs_service_test_report(uint64_t magic);

static bool probe_endpoint_is_canonical(micros_endpoint_t endpoint, size_t expected_slot) {
    uint32_t slot_mask = (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1;
    uint32_t slot = endpoint & slot_mask;
    uint32_t generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;

    return (endpoint != MICROS_ENDPOINT_NONE && endpoint != MICROS_ENDPOINT_ANY &&
            slot == expected_slot && generation != 0 &&
            generation <= MICROS_ENDPOINT_GENERATION_MAX);
}

static bool probe_validate_configuration(void) {
    size_t index;
    size_t other;

    if (micros_bootstrap_service_config.version != MICROS_BOOTSTRAP_MANIFEST_VERSION ||
        micros_bootstrap_service_config.manifest_version != MICROS_BOOTSTRAP_MANIFEST_VERSION ||
        micros_bootstrap_service_config.service_id != MICROS_VFS_TEST_APPLICATION_SERVICE_ID ||
        micros_bootstrap_service_config.service_count != MICROS_VFS_TEST_SERVICE_COUNT ||
        micros_bootstrap_service_config.self_endpoint !=
            micros_bootstrap_service_config.services[MICROS_VFS_TEST_APPLICATION_SERVICE_ID - 1]
                .endpoint ||
        micros_bootstrap_service_config.launcher_endpoint !=
            micros_bootstrap_service_config.services[MICROS_VFS_TEST_LAUNCHER_SERVICE_ID - 1]
                .endpoint ||
        micros_bootstrap_service_config.manifest_view_address != 0) {
        return false;
    }
    for (index = 0; index < MICROS_VFS_TEST_SERVICE_COUNT; ++index) {
        if (micros_bootstrap_service_config.services[index].service_id != index + 1 ||
            !probe_endpoint_is_canonical(micros_bootstrap_service_config.services[index].endpoint,
                                         index)) {
            return false;
        }
        for (other = 0; other < index; ++other) {
            if (micros_bootstrap_service_config.services[index].endpoint ==
                micros_bootstrap_service_config.services[other].endpoint) {
                return false;
            }
        }
    }
    for (index = 0; index < sizeof(micros_bootstrap_service_config.reserved) /
                                sizeof(micros_bootstrap_service_config.reserved[0]);
         ++index) {
        if (micros_bootstrap_service_config.reserved[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool probe_send_ready(micros_endpoint_t launcher_endpoint) {
    struct micros_ipc_message message;

    probe_clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    probe_write_u32_le(&message.payload[0], MICROS_BOOTSTRAP_MANIFEST_VERSION);
    probe_write_u32_le(&message.payload[4], MICROS_VFS_TEST_APPLICATION_SERVICE_ID);
    probe_write_u32_le(&message.payload[8], MICROS_BOOTSTRAP_MANIFEST_VERSION);
    probe_write_u32_le(&message.payload[16], micros_bootstrap_service_config.self_endpoint);
    if (micros_runtime_call(launcher_endpoint, &message) != MICROS_SYSCALL_ABI_OK) {
        return false;
    }
    return (message.source == launcher_endpoint &&
            message.type == MICROS_BOOTSTRAP_MESSAGE_READY_ACK && message.reply_token == 0 &&
            probe_read_u32_le(&message.payload[0]) == MICROS_BOOTSTRAP_MANIFEST_VERSION &&
            probe_read_u32_le(&message.payload[4]) == MICROS_VFS_TEST_APPLICATION_SERVICE_ID &&
            probe_read_u32_le(&message.payload[8]) == MICROS_BOOTSTRAP_MANIFEST_VERSION &&
            probe_read_u32_le(&message.payload[12]) == 0 &&
            probe_read_u32_le(&message.payload[16]) ==
                micros_bootstrap_service_config.self_endpoint &&
            probe_bytes_are_zero(&message.payload[20], 28));
}

static void probe_report_failure(uint64_t magic) {
    micros_vfs_service_test_report(magic);
    __builtin_trap();
}

void micros_service_main(void) {
    micros_endpoint_t launcher_endpoint;
    micros_endpoint_t vfs_endpoint;

    if (probe_initialized_data[0] != MICROS_VFS_TEST_PROBE_DATA_MAGIC ||
        !probe_validate_configuration()) {
        probe_report_failure(MICROS_VFS_TEST_FAILURE_CONFIGURATION);
    }
    launcher_endpoint = micros_bootstrap_service_config.launcher_endpoint;
    vfs_endpoint =
        micros_bootstrap_service_config.services[MICROS_VFS_TEST_VFS_SERVICE_ID - 1].endpoint;
    if (!probe_send_ready(launcher_endpoint)) {
        probe_report_failure(MICROS_VFS_TEST_FAILURE_READY);
    }
    if (!probe_test_filesystem(&probe_context, vfs_endpoint)) {
        probe_report_failure(MICROS_VFS_TEST_FAILURE_FILESYSTEM);
    }
    if (!probe_enumerate_root(&probe_context, vfs_endpoint)) {
        probe_report_failure(MICROS_VFS_TEST_FAILURE_DIRECTORY);
    }
    if (!probe_test_console(&probe_context, vfs_endpoint)) {
        probe_report_failure(MICROS_VFS_TEST_FAILURE_CONSOLE);
    }
    if (!probe_request_drain(vfs_endpoint)) {
        probe_report_failure(MICROS_VFS_TEST_FAILURE_DRAIN);
    }
    micros_vfs_service_test_report(MICROS_VFS_TEST_REPORT_MAGIC);
    __builtin_trap();
}
