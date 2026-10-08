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
void micros_user_runtime_test_probe_rodata(
    volatile const uint64_t *address,
    uint64_t value
);
uint64_t micros_user_runtime_test_raw_send_probe(
    micros_endpoint_t destination,
    const struct micros_ipc_message *message
);

static bool bytes_equal(
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

static void put_u32(uint8_t *bytes, uint32_t value)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static uint32_t get_u32(const uint8_t *bytes)
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

static void fill_message(
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

static bool message_matches(
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

static bool notification_matches(
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

static bool initial_bss_is_zero(void)
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

static bool memory_support_works(void)
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

static uint64_t startup_checks(void)
{
    uintptr_t stack_pointer;
    uintptr_t global_pointer;
    uintptr_t thread_pointer;

    __asm__ volatile("mv %0, sp" : "=r"(stack_pointer));
    __asm__ volatile("mv %0, gp" : "=r"(global_pointer));
    __asm__ volatile("mv %0, tp" : "=r"(thread_pointer));
    if (
        micros_user_runtime_test_config.version
            != MICROS_USER_RUNTIME_TEST_VERSION
        || micros_user_runtime_test_config.role
            < MICROS_USER_RUNTIME_TEST_ROLE_CLIENT
        || micros_user_runtime_test_config.role
            > MICROS_USER_RUNTIME_TEST_ROLE_PEER
        || micros_user_runtime_test_config.client_endpoint
            == MICROS_ENDPOINT_NONE
        || micros_user_runtime_test_config.server_endpoint
            == MICROS_ENDPOINT_NONE
        || micros_user_runtime_test_config.peer_endpoint
            == MICROS_ENDPOINT_NONE
        || global_pointer != 0
        || thread_pointer != 0
    ) {
        return UINT64_C(0x101);
    }
    if (
        stack_pointer % 16 != 0
        || stack_pointer < UINT64_C(0x000000007ffff000)
        || stack_pointer >= UINT64_C(0x0000000080000000)
    ) {
        return UINT64_C(0x102);
    }
    if (
        micros_user_runtime_test_data_sentinel
            != MICROS_USER_RUNTIME_TEST_DATA_SENTINEL
        || micros_user_runtime_test_rodata
            != MICROS_USER_RUNTIME_TEST_RODATA_SENTINEL
        || micros_user_runtime_test_read_region.before
            != MICROS_USER_RUNTIME_TEST_CANARY_BEFORE
        || micros_user_runtime_test_read_region.after
            != MICROS_USER_RUNTIME_TEST_CANARY_AFTER
        || !initial_bss_is_zero()
    ) {
        return UINT64_C(0x103);
    }
    if (!memory_support_works()) {
        return UINT64_C(0x104);
    }
    return 0;
}

static uint64_t run_client(void)
{
    static const uint8_t read_expected[TEST_TRANSFER_SIZE] = {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    };
    struct micros_ipc_message message;
    volatile uint64_t canary_before =
        MICROS_USER_RUNTIME_TEST_CANARY_BEFORE;
    volatile uint64_t canary_after =
        MICROS_USER_RUNTIME_TEST_CANARY_AFTER;
    micros_grant_t read_grant = MICROS_GRANT_NONE;
    micros_grant_t write_grant = MICROS_GRANT_NONE;
    uint64_t probe_result;
    size_t index;

    fill_message(
        &message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_INITIAL,
        UINT8_C(0x10)
    );
    probe_result = micros_user_runtime_test_raw_send_probe(
        micros_user_runtime_test_config.server_endpoint,
        &message
    );
    if (probe_result != 0) {
        return UINT64_C(0x2100) + probe_result;
    }
    micros_user_runtime_test_config.status |=
        MICROS_USER_RUNTIME_TEST_STATUS_REGISTERS;

    fill_message(
        &message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_ONE,
        UINT8_C(0x20)
    );
    if (
        micros_runtime_call(
            micros_user_runtime_test_config.server_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || canary_before != MICROS_USER_RUNTIME_TEST_CANARY_BEFORE
        || canary_after != MICROS_USER_RUNTIME_TEST_CANARY_AFTER
        || !message_matches(
            &message,
            micros_user_runtime_test_config.server_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_ONE_REPLY,
            UINT8_C(0x30),
            false
        )
    ) {
        return UINT64_C(0x202);
    }
    fill_message(
        &message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_TWO,
        UINT8_C(0x40)
    );
    if (
        micros_runtime_call(
            micros_user_runtime_test_config.server_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || !message_matches(
            &message,
            micros_user_runtime_test_config.server_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_TWO_REPLY,
            UINT8_C(0x50),
            false
        )
    ) {
        return UINT64_C(0x203);
    }
    micros_user_runtime_test_config.status |=
        MICROS_USER_RUNTIME_TEST_STATUS_IPC;

    micros_user_runtime_test_write_region.before =
        MICROS_USER_RUNTIME_TEST_CANARY_BEFORE;
    micros_user_runtime_test_write_region.after =
        MICROS_USER_RUNTIME_TEST_CANARY_AFTER;
    for (index = 0; index < TEST_TRANSFER_SIZE; ++index) {
        micros_user_runtime_test_write_region.bytes[index] = 0;
    }
    if (
        micros_runtime_grant_create(
            micros_user_runtime_test_config.server_endpoint,
            (uintptr_t)micros_user_runtime_test_read_region.bytes,
            TEST_TRANSFER_SIZE,
            MICROS_GRANT_PERMISSION_READ,
            &read_grant
        ) != MICROS_SYSCALL_ABI_OK
        || micros_runtime_grant_create(
            micros_user_runtime_test_config.server_endpoint,
            (uintptr_t)micros_user_runtime_test_write_region.bytes,
            TEST_TRANSFER_SIZE,
            MICROS_GRANT_PERMISSION_WRITE,
            &write_grant
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x204);
    }
    fill_message(
        &message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_GRANTS,
        UINT8_C(0x60)
    );
    put_u32(message.payload, read_grant);
    put_u32(message.payload + 4, write_grant);
    if (
        micros_runtime_send(
            micros_user_runtime_test_config.server_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || micros_runtime_receive(
            micros_user_runtime_test_config.server_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || !message_matches(
            &message,
            micros_user_runtime_test_config.server_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_COPY_DONE,
            UINT8_C(0x70),
            false
        )
        || micros_user_runtime_test_write_region.before
            != MICROS_USER_RUNTIME_TEST_CANARY_BEFORE
        || micros_user_runtime_test_write_region.after
            != MICROS_USER_RUNTIME_TEST_CANARY_AFTER
        || !bytes_equal(
            micros_user_runtime_test_read_region.bytes,
            read_expected,
            TEST_TRANSFER_SIZE
        )
    ) {
        return UINT64_C(0x205);
    }
    for (index = 0; index < TEST_TRANSFER_SIZE; ++index) {
        if (
            micros_user_runtime_test_write_region.bytes[index]
                != (uint8_t)(UINT8_C(0xd0) + index)
        ) {
            return UINT64_C(0x206);
        }
    }
    if (
        micros_runtime_grant_revoke(read_grant)
            != MICROS_SYSCALL_ABI_OK
        || micros_runtime_grant_revoke(write_grant)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x207);
    }
    fill_message(
        &message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_RETRY,
        UINT8_C(0x80)
    );
    put_u32(message.payload, read_grant);
    if (
        micros_runtime_send(
            micros_user_runtime_test_config.server_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || micros_runtime_receive(
            micros_user_runtime_test_config.server_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || !message_matches(
            &message,
            micros_user_runtime_test_config.server_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_STALE_DONE,
            UINT8_C(0x90),
            false
        )
    ) {
        return UINT64_C(0x208);
    }
    micros_user_runtime_test_config.status |=
        MICROS_USER_RUNTIME_TEST_STATUS_GRANTS
        | MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE;
    fill_message(
        &message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_CLIENT_DONE,
        UINT8_C(0xb0)
    );
    if (
        micros_runtime_send(
            micros_user_runtime_test_config.server_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x209);
    }
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
            == MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x20a);
    }
    return UINT64_C(0x20b);
}

static uint64_t run_peer(void)
{
    struct micros_ipc_message message;

    if (
        micros_runtime_receive(
            micros_user_runtime_test_config.server_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || !message_matches(
            &message,
            micros_user_runtime_test_config.server_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_PEER_WAKE,
            UINT8_C(0xa0),
            false
        )
        || micros_runtime_notify(
            micros_user_runtime_test_config.server_endpoint,
            MICROS_USER_RUNTIME_TEST_NOTIFY_MASK
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x301);
    }
    micros_user_runtime_test_config.status |=
        MICROS_USER_RUNTIME_TEST_STATUS_IPC
        | MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE;
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
            == MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x302);
    }
    return UINT64_C(0x303);
}

static uint64_t run_server(void)
{
    static const uint8_t read_expected[TEST_TRANSFER_SIZE] = {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    };
    struct micros_ipc_message message;
    struct micros_ipc_message reply;
    micros_grant_t read_grant;
    micros_grant_t write_grant;
    uint64_t token;
    size_t index;

    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
            != MICROS_SYSCALL_ABI_OK
        || !message_matches(
            &message,
            micros_user_runtime_test_config.client_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_INITIAL,
            UINT8_C(0x10),
            false
        )
        || micros_runtime_receive(
            micros_user_runtime_test_config.client_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || !message_matches(
            &message,
            micros_user_runtime_test_config.client_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_ONE,
            UINT8_C(0x20),
            true
        )
    ) {
        return UINT64_C(0x401);
    }
    token = message.reply_token;
    fill_message(
        &reply,
        MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_ONE_REPLY,
        UINT8_C(0x30)
    );
    if (
        micros_runtime_reply(token, &reply)
            != MICROS_SYSCALL_ABI_OK
        || micros_runtime_receive(
            micros_user_runtime_test_config.client_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || !message_matches(
            &message,
            micros_user_runtime_test_config.client_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_TWO,
            UINT8_C(0x40),
            true
        )
    ) {
        return UINT64_C(0x402);
    }
    token = message.reply_token;
    fill_message(
        &message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_PEER_WAKE,
        UINT8_C(0xa0)
    );
    if (
        micros_runtime_send(
            micros_user_runtime_test_config.peer_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x403);
    }
    fill_message(
        &reply,
        MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_TWO_REPLY,
        UINT8_C(0x50)
    );
    if (
        micros_runtime_reply_receive(
            token,
            &reply,
            MICROS_ENDPOINT_ANY,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || !notification_matches(
            &message,
            micros_user_runtime_test_config.peer_endpoint
        )
        || micros_runtime_receive(
            micros_user_runtime_test_config.client_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || message.source
            != micros_user_runtime_test_config.client_endpoint
        || message.type != MICROS_USER_RUNTIME_TEST_MESSAGE_GRANTS
        || message.reply_token != 0
    ) {
        return UINT64_C(0x404);
    }
    read_grant = get_u32(message.payload);
    write_grant = get_u32(message.payload + 4);
    micros_user_runtime_test_server_region.before =
        MICROS_USER_RUNTIME_TEST_CANARY_BEFORE;
    micros_user_runtime_test_server_region.after =
        MICROS_USER_RUNTIME_TEST_CANARY_AFTER;
    for (index = 0; index < TEST_TRANSFER_SIZE; ++index) {
        micros_user_runtime_test_server_region.bytes[index] = 0;
    }
    if (
        micros_runtime_grant_copy_from(
            micros_user_runtime_test_config.client_endpoint,
            read_grant,
            0,
            (uintptr_t)micros_user_runtime_test_server_region.bytes,
            TEST_TRANSFER_SIZE
        ) != MICROS_SYSCALL_ABI_OK
        || micros_user_runtime_test_server_region.before
            != MICROS_USER_RUNTIME_TEST_CANARY_BEFORE
        || micros_user_runtime_test_server_region.after
            != MICROS_USER_RUNTIME_TEST_CANARY_AFTER
        || !bytes_equal(
            micros_user_runtime_test_server_region.bytes,
            read_expected,
            TEST_TRANSFER_SIZE
        )
    ) {
        return UINT64_C(0x405);
    }
    for (index = 0; index < TEST_TRANSFER_SIZE; ++index) {
        micros_user_runtime_test_server_region.bytes[index] =
            (uint8_t)(UINT8_C(0xd0) + index);
    }
    if (
        micros_runtime_grant_copy_to(
            micros_user_runtime_test_config.client_endpoint,
            write_grant,
            0,
            (uintptr_t)micros_user_runtime_test_server_region.bytes,
            TEST_TRANSFER_SIZE
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x406);
    }
    fill_message(
        &message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_COPY_DONE,
        UINT8_C(0x70)
    );
    if (
        micros_runtime_send(
            micros_user_runtime_test_config.client_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || micros_runtime_receive(
            micros_user_runtime_test_config.client_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || message.source
            != micros_user_runtime_test_config.client_endpoint
        || message.type != MICROS_USER_RUNTIME_TEST_MESSAGE_RETRY
        || message.reply_token != 0
    ) {
        return UINT64_C(0x407);
    }
    read_grant = get_u32(message.payload);
    if (
        micros_runtime_grant_copy_from(
            micros_user_runtime_test_config.client_endpoint,
            read_grant,
            0,
            (uintptr_t)micros_user_runtime_test_server_region.bytes,
            TEST_TRANSFER_SIZE
        ) != MICROS_SYSCALL_ABI_STALE_GRANT
    ) {
        return UINT64_C(0x408);
    }
    fill_message(
        &message,
        MICROS_USER_RUNTIME_TEST_MESSAGE_STALE_DONE,
        UINT8_C(0x90)
    );
    if (
        micros_runtime_send(
            micros_user_runtime_test_config.client_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || micros_runtime_receive(
            micros_user_runtime_test_config.client_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || !message_matches(
            &message,
            micros_user_runtime_test_config.client_endpoint,
            MICROS_USER_RUNTIME_TEST_MESSAGE_CLIENT_DONE,
            UINT8_C(0xb0),
            false
        )
    ) {
        return UINT64_C(0x409);
    }
    micros_user_runtime_test_config.status |=
        MICROS_USER_RUNTIME_TEST_STATUS_IPC
        | MICROS_USER_RUNTIME_TEST_STATUS_GRANTS
        | MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE;
    return 0;
}

void micros_service_main(void)
{
    uint64_t failure = startup_checks();

    if (
        failure == 0
        && micros_user_runtime_test_config.role
            == MICROS_USER_RUNTIME_TEST_ROLE_SERVER
    ) {
        micros_user_runtime_test_probe_rodata(
            &micros_user_runtime_test_rodata,
            0
        );
    }
    if (failure == 0) {
        micros_user_runtime_test_config.status |=
            MICROS_USER_RUNTIME_TEST_STATUS_STARTUP
            | MICROS_USER_RUNTIME_TEST_STATUS_STACK
            | MICROS_USER_RUNTIME_TEST_STATUS_MEMORY;
        switch (micros_user_runtime_test_config.role) {
        case MICROS_USER_RUNTIME_TEST_ROLE_CLIENT:
            failure = run_client();
            break;
        case MICROS_USER_RUNTIME_TEST_ROLE_SERVER:
            failure = run_server();
            break;
        case MICROS_USER_RUNTIME_TEST_ROLE_PEER:
            failure = run_peer();
            break;
        default:
            failure = UINT64_C(0x501);
            break;
        }
    }
    micros_user_runtime_test_config.failure = failure;
}
