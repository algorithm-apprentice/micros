#include "tests/qemu/user_runtime_service_internal.h"

volatile struct micros_user_runtime_test_config
    micros_user_runtime_test_config = {
        .version = MICROS_USER_RUNTIME_TEST_VERSION,
    };
const volatile uint64_t micros_user_runtime_test_rodata =
    MICROS_USER_RUNTIME_TEST_RODATA_SENTINEL;
volatile uint64_t micros_user_runtime_test_data_sentinel =
    MICROS_USER_RUNTIME_TEST_DATA_SENTINEL;
volatile struct test_region micros_user_runtime_test_read_region = {
    .before = MICROS_USER_RUNTIME_TEST_CANARY_BEFORE,
    .bytes = {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    },
    .after = MICROS_USER_RUNTIME_TEST_CANARY_AFTER,
};
volatile struct test_region micros_user_runtime_test_write_region;
volatile struct test_region micros_user_runtime_test_server_region;
volatile uint8_t
    micros_user_runtime_test_bss_probe[TEST_BSS_PROBE_SIZE];

void *memcpy(
    void *restrict destination,
    const void *restrict source,
    size_t length
);
void *memset(void *destination, int byte, size_t length);

bool user_runtime_service_bytes_equal(
    const volatile uint8_t *left,
    const uint8_t *right,
    size_t length
)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

static bool bytes_are_zero(const volatile uint8_t *bytes, size_t length)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

void user_runtime_service_put_u32(uint8_t *bytes, uint32_t value)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

uint32_t user_runtime_service_get_u32(const uint8_t *bytes)
{
    uint32_t value = 0;
    size_t index;

    for (index = 0; index < 4; ++index) {
        value |= (uint32_t)bytes[index] << (index * 8);
    }
    return value;
}

static uint64_t get_u64(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

void user_runtime_service_fill_message(
    struct micros_ipc_message *message,
    uint32_t type,
    uint8_t seed
)
{
    size_t index;

    (void)memset(message, 0, sizeof(*message));
    message->type = type;
    for (index = 0; index < sizeof(message->payload); ++index) {
        message->payload[index] = (uint8_t)(seed + index);
    }
}

bool user_runtime_service_message_matches(
    const struct micros_ipc_message *message,
    micros_endpoint_t source,
    uint32_t type,
    uint8_t seed,
    bool require_token
)
{
    size_t index;

    if (
        message->source != source
        || message->type != type
        || (require_token
            ? message->reply_token == 0
            : message->reply_token != 0)
    ) {
        return false;
    }
    for (index = 0; index < sizeof(message->payload); ++index) {
        if (message->payload[index] != (uint8_t)(seed + index)) {
            return false;
        }
    }
    return true;
}

bool user_runtime_service_notification_matches(
    const struct micros_ipc_message *message,
    micros_endpoint_t source
)
{
    size_t index;

    if (
        message->source != source
        || message->type != MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        || message->reply_token != 0
        || get_u64(message->payload)
            != MICROS_USER_RUNTIME_TEST_NOTIFY_MASK
    ) {
        return false;
    }
    for (index = 8; index < sizeof(message->payload); ++index) {
        if (message->payload[index] != 0) {
            return false;
        }
    }
    return true;
}

bool user_runtime_service_initial_bss_is_zero(void)
{
    return (
        bytes_are_zero(
            (const volatile uint8_t *)
                &micros_user_runtime_test_write_region,
            sizeof(micros_user_runtime_test_write_region)
        )
        && bytes_are_zero(
            (const volatile uint8_t *)
                &micros_user_runtime_test_server_region,
            sizeof(micros_user_runtime_test_server_region)
        )
        && bytes_are_zero(
            micros_user_runtime_test_bss_probe,
            sizeof(micros_user_runtime_test_bss_probe)
        )
    );
}

bool user_runtime_service_memory_support_works(void)
{
    uint8_t source[67];
    uint8_t destination[69];
    size_t index;

    for (index = 0; index < sizeof(source); ++index) {
        source[index] = (uint8_t)(index + 0x31);
    }
    if (
        memset(destination, 0xa5, sizeof(destination)) != destination
        || memcpy(destination + 1, source, sizeof(source))
            != destination + 1
        || destination[0] != UINT8_C(0xa5)
        || destination[68] != UINT8_C(0xa5)
    ) {
        return false;
    }
    for (index = 0; index < sizeof(source); ++index) {
        if (destination[index + 1] != source[index]) {
            return false;
        }
    }
    return true;
}
