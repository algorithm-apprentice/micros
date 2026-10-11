#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/runtime/raw_syscall.h"
#include "micros/bootstrap_control.h"
#include "micros/tty.h"
#include "micros/vm_bootstrap.h"
#include "servers/vm/vm_core.h"
#include "tests/qemu/vm_handoff_protocol.h"
#if defined(MICROS_VFS_SERVICE_TEST)
#include "tests/qemu/vfs_service_protocol.h"
#define MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID \
    MICROS_VFS_TEST_LAUNCHER_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID \
    MICROS_VFS_TEST_VM_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID \
    MICROS_VFS_TEST_TTY_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_SERVICE_COUNT \
    MICROS_VFS_TEST_SERVICE_COUNT
#define MICROS_VM_TTY_HANDOFF_TEST
#elif defined(MICROS_RAMFS_SERVICE_TEST)
#include "tests/qemu/ramfs_service_protocol.h"
#define MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID \
    MICROS_RAMFS_TEST_LAUNCHER_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID \
    MICROS_RAMFS_TEST_VM_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID \
    MICROS_RAMFS_TEST_TTY_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_SERVICE_COUNT \
    MICROS_RAMFS_TEST_SERVICE_COUNT
#define MICROS_VM_TTY_HANDOFF_TEST
#elif defined(MICROS_TTY_HANDOFF_TEST)
#include "tests/qemu/tty_handoff_protocol.h"
#define MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID \
    MICROS_TTY_HANDOFF_TEST_LAUNCHER_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID \
    MICROS_TTY_HANDOFF_TEST_VM_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID \
    MICROS_TTY_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_SERVICE_COUNT \
    MICROS_TTY_HANDOFF_TEST_SERVICE_COUNT
#define MICROS_VM_TTY_HANDOFF_TEST
#elif defined(MICROS_PM_SERVICE_TEST)
#include "tests/qemu/pm_service_protocol.h"
#define MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID \
    MICROS_PM_TEST_LAUNCHER_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID \
    MICROS_PM_TEST_VM_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID \
    MICROS_PM_TEST_PROBE_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_SERVICE_COUNT \
    MICROS_PM_TEST_SERVICE_COUNT
#endif

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;

_Alignas(MICROS_VM_BOOT_INFO_ALIGNMENT)
__attribute__((section(".data.vm_boot_info"), used))
struct micros_vm_boot_info micros_vm_boot_info = {0};

static volatile uint64_t vm_data = UINT64_C(0x564d44415441564d);
static struct micros_vm_state vm_state;
#if !defined(MICROS_PM_SERVICE_TEST) \
    && !defined(MICROS_VM_TTY_HANDOFF_TEST)
static uint8_t grant_buffer[MICROS_VM_HANDOFF_TEST_DATA_SIZE];
#endif

void micros_vm_trigger_self_fault(void);
static int vm_server_find_service(uint32_t service_id);

#ifdef MICROS_VM_TTY_HANDOFF_TEST
struct vm_tty_mapping_descriptor {
    bool mapped;
    micros_endpoint_t endpoint;
    uint64_t virtual_base;
    uint64_t physical_base;
    uint32_t irq_source;
    uint32_t mapped_length;
};

static struct vm_tty_mapping_descriptor tty_mapping;
#endif

static void vm_server_clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool vm_server_bytes_are_zero(const void *storage, size_t size)
{
    const uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static void vm_server_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t vm_server_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

#ifdef MICROS_VM_TTY_HANDOFF_TEST
static bool vm_server_notification_is_canonical(
    const struct micros_ipc_message *message,
    uint64_t event_mask
)
{
    size_t index;
    uint64_t observed = 0;

    if (
        message->source != MICROS_ENDPOINT_NONE
        || message->type != MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        || message->reply_token != 0
    ) {
        return false;
    }
    for (index = 0; index < 8; ++index) {
        observed |= (uint64_t)message->payload[index]
            << (index * 8);
    }
    return (
        observed == event_mask
        && vm_server_bytes_are_zero(
            &message->payload[8],
            sizeof(message->payload) - 8
        )
    );
}

static bool vm_server_map_tty_uart(void)
{
    struct micros_ipc_message message;
    int tty_index = vm_server_find_service(MICROS_TTY_SERVICE_ID);
    uint64_t irq_and_length =
        (uint64_t)MICROS_TTY_UART_IRQ_SOURCE << 32
        | MICROS_TTY_UART_MAPPED_LENGTH;

    if (tty_index < 0 || tty_mapping.mapped) {
        return false;
    }
    vm_server_clear_bytes(&message, sizeof(message));
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
            != MICROS_SYSCALL_ABI_OK
        || !vm_server_notification_is_canonical(
            &message,
            MICROS_KERNEL_EVENT_CONSOLE_MAP_REQUEST
        )
        || micros_runtime_raw_syscall(
            MICROS_VM_HANDOFF_MAP_TTY_UART,
            MICROS_TTY_MAPPING_VERSION,
            MICROS_TTY_SERVICE_ID,
            micros_bootstrap_service_config.services[tty_index]
                .endpoint,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_PHYSICAL_BASE,
            irq_and_length,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    tty_mapping.mapped = true;
    tty_mapping.endpoint =
        micros_bootstrap_service_config.services[tty_index].endpoint;
    tty_mapping.virtual_base = MICROS_TTY_UART_VIRTUAL_BASE;
    tty_mapping.physical_base = MICROS_TTY_UART_PHYSICAL_BASE;
    tty_mapping.irq_source = MICROS_TTY_UART_IRQ_SOURCE;
    tty_mapping.mapped_length = MICROS_TTY_UART_MAPPED_LENGTH;
    return true;
}
#endif

#if !defined(MICROS_PM_SERVICE_TEST) \
    && !defined(MICROS_VM_TTY_HANDOFF_TEST)
static uint64_t vm_server_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}
#endif

static int vm_server_find_service(uint32_t service_id)
{
    size_t index;

    for (
        index = 0;
        index < micros_bootstrap_service_config.service_count;
        ++index
    ) {
        if (
            micros_bootstrap_service_config.services[index].service_id
                == service_id
        ) {
            return (int)index;
        }
    }
    return -1;
}

static bool vm_server_validate_configuration(void)
{
    int self_index;
    int launcher_index;
    int probe_index;
    size_t index;

    if (
        micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.service_id
            != MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID
        || micros_bootstrap_service_config.service_count
            != MICROS_VM_HANDOFF_TEST_SERVICE_COUNT
        || micros_bootstrap_service_config.manifest_view_address != 0
        || !vm_server_bytes_are_zero(
            (const void *)micros_bootstrap_service_config.reserved,
            sizeof(micros_bootstrap_service_config.reserved)
        )
    ) {
        return false;
    }
    self_index = vm_server_find_service(MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID);
    launcher_index = vm_server_find_service(
        MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID
    );
    probe_index = vm_server_find_service(
        MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID
    );
    if (
        self_index < 0
        || launcher_index < 0
        || probe_index < 0
        || micros_bootstrap_service_config.services[self_index].endpoint
            != micros_bootstrap_service_config.self_endpoint
        || micros_bootstrap_service_config.services[launcher_index].endpoint
            != micros_bootstrap_service_config.launcher_endpoint
    ) {
        return false;
    }
    for (
        index = 0;
        index < MICROS_VM_HANDOFF_TEST_SERVICE_COUNT;
        ++index
    ) {
        if (
            micros_bootstrap_service_config.services[index].service_id
                != index + 1
            || micros_bootstrap_service_config.services[index].endpoint
                == MICROS_ENDPOINT_NONE
        ) {
            return false;
        }
    }
    return true;
}

static bool vm_server_send_ready(void)
{
    struct micros_ipc_message message;
    size_t index;

    vm_server_clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    vm_server_write_u32_le(
        &message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    vm_server_write_u32_le(
        &message.payload[4],
        MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID
    );
    vm_server_write_u32_le(
        &message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    vm_server_write_u32_le(&message.payload[12], 0);
    vm_server_write_u32_le(
        &message.payload[16],
        micros_bootstrap_service_config.self_endpoint
    );
    if (
        micros_runtime_call(
            micros_bootstrap_service_config.launcher_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
        || message.source
            != micros_bootstrap_service_config.launcher_endpoint
        || message.type != MICROS_BOOTSTRAP_MESSAGE_READY_ACK
        || message.reply_token != 0
        || vm_server_read_u32_le(&message.payload[0])
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || vm_server_read_u32_le(&message.payload[4])
            != MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID
        || vm_server_read_u32_le(&message.payload[8])
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || vm_server_read_u32_le(&message.payload[12]) != 0
        || vm_server_read_u32_le(&message.payload[16])
            != micros_bootstrap_service_config.self_endpoint
    ) {
        return false;
    }
    for (index = 20; index < sizeof(message.payload); ++index) {
        if (message.payload[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool vm_server_complete_handoff(void)
{
    uint64_t packed_counts =
        (
            (uint64_t)micros_vm_boot_info.header.address_space_count
            << 16
        ) | micros_vm_boot_info.header.managed_range_count;

    return micros_runtime_raw_syscall(
        MICROS_VM_HANDOFF_READY,
        micros_vm_boot_info.header.version,
        packed_counts,
        micros_vm_boot_info.header.managed_frame_count,
        micros_vm_boot_info.header.free_frame_count,
        micros_vm_boot_info.header.mapping_count,
        micros_vm_boot_info.header.digest,
        MICROS_SYSCALL_ABI_VM_HANDOFF
    ) == MICROS_SYSCALL_ABI_OK;
}

#if !defined(MICROS_PM_SERVICE_TEST) \
    && !defined(MICROS_VM_TTY_HANDOFF_TEST)
static bool vm_server_serve_probe(void)
{
    struct micros_ipc_message request;
    struct micros_ipc_message reply;
    uint64_t raw_grant;
    micros_grant_t grant;
    uint32_t length;
    int probe_index = vm_server_find_service(
        MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID
    );
    size_t index;

    if (probe_index < 0) {
        return false;
    }
    vm_server_clear_bytes(&request, sizeof(request));
    if (
        micros_runtime_receive(
            micros_bootstrap_service_config.services[probe_index]
                .endpoint,
            &request
        ) != MICROS_SYSCALL_ABI_OK
        || request.source
            != micros_bootstrap_service_config.services[probe_index]
                .endpoint
        || request.type != MICROS_VM_HANDOFF_TEST_REQUEST
        || request.reply_token == 0
        || vm_server_read_u32_le(&request.payload[12])
            != MICROS_VM_HANDOFF_TEST_PAYLOAD_MAGIC
    ) {
        return false;
    }
    raw_grant = vm_server_read_u64_le(&request.payload[0]);
    length = vm_server_read_u32_le(&request.payload[8]);
    if (
        raw_grant > UINT32_MAX
        || length != MICROS_VM_HANDOFF_TEST_DATA_SIZE
    ) {
        return false;
    }
    grant = (micros_grant_t)raw_grant;
    if (
        micros_runtime_grant_copy_from(
            request.source,
            grant,
            0,
            (uintptr_t)grant_buffer,
            sizeof(grant_buffer)
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    for (index = 0; index < sizeof(grant_buffer); ++index) {
        if (grant_buffer[index] != (uint8_t)(UINT8_C(0x20) + index)) {
            return false;
        }
        grant_buffer[index] ^= UINT8_C(0x5a);
    }
    if (
        micros_runtime_grant_copy_to(
            request.source,
            grant,
            0,
            (uintptr_t)grant_buffer,
            sizeof(grant_buffer)
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    vm_server_clear_bytes(&reply, sizeof(reply));
    reply.type = MICROS_VM_HANDOFF_TEST_REPLY;
    vm_server_write_u32_le(&reply.payload[0], sizeof(grant_buffer));
    vm_server_write_u32_le(
        &reply.payload[4],
        MICROS_VM_HANDOFF_TEST_PAYLOAD_MAGIC
    );
    return micros_runtime_reply(request.reply_token, &reply)
        == MICROS_SYSCALL_ABI_OK;
}
#endif

void micros_service_main(void)
{
    if (
        vm_data != UINT64_C(0x564d44415441564d)
        || !vm_server_validate_configuration()
        || micros_vm_state_initialize(
            &vm_state,
            &micros_vm_boot_info,
            MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID,
            micros_bootstrap_service_config.self_endpoint,
            micros_bootstrap_service_config.service_count
        ) != MICROS_VM_CORE_OK
    ) {
        __builtin_trap();
    }
#ifdef MICROS_VM_READY_EARLY
    if (!vm_server_send_ready()) {
        __builtin_trap();
    }
    __builtin_trap();
#endif
    if (!vm_server_complete_handoff()) {
        __builtin_trap();
    }
#ifdef MICROS_VM_SELF_FAULT_RUNNING
    micros_vm_trigger_self_fault();
#endif
    if (!vm_server_send_ready()) {
        __builtin_trap();
    }
#ifdef MICROS_VM_TTY_HANDOFF_TEST
    if (!vm_server_map_tty_uart()) {
        __builtin_trap();
    }
#elif !defined(MICROS_PM_SERVICE_TEST)
    if (!vm_server_serve_probe()) {
        __builtin_trap();
    }
#endif
#ifdef MICROS_VM_SELF_FAULT_SEALED
    micros_vm_trigger_self_fault();
#endif
    for (;;) {
        struct micros_ipc_message message;

        vm_server_clear_bytes(&message, sizeof(message));
        if (
            micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
                != MICROS_SYSCALL_ABI_OK
        ) {
            __builtin_trap();
        }
#ifdef MICROS_VM_TTY_HANDOFF_TEST
        __builtin_trap();
#endif
    }
}
