#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/runtime/raw_syscall.h"
#include "micros/bootstrap_control.h"
#include "micros/sv39.h"
#include "micros/tty.h"
#include "micros/user_address_space.h"
#include "micros/vm.h"
#include "micros/vm_bootstrap.h"
#include "servers/vm/vm_core.h"
#include "tests/qemu/vm_handoff_protocol.h"
#if defined(MICROS_VM_SCRATCH_PREEMPTION_TEST)
#include "tests/qemu/vm_scratch_preemption_protocol.h"
#define MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID \
    MICROS_VM_SCRATCH_PREEMPTION_LAUNCHER_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID \
    MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID \
    MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_SERVICE_COUNT \
    MICROS_VM_SCRATCH_PREEMPTION_SERVICE_COUNT
#elif defined(MICROS_VFS_SERVICE_TEST)
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
    && !defined(MICROS_VM_TTY_HANDOFF_TEST) \
    && !defined(MICROS_VM_SCRATCH_PREEMPTION_TEST)
static uint8_t grant_buffer[MICROS_VM_HANDOFF_TEST_DATA_SIZE];
#endif

void micros_vm_trigger_self_fault(void);
static int vm_server_find_service(uint32_t service_id);

#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST
static struct micros_vm_load_begin_result scratch_begin_result;

void micros_vm_scratch_preemption_vm_hook(uint64_t magic);
_Noreturn void micros_vm_scratch_preemption_stale_load(
    uint64_t virtual_address
);
#endif

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

#if defined(MICROS_VM_SCRATCH_PREEMPTION_TEST) \
    || defined(MICROS_PM_SERVICE_TEST)
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

#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST
static bool vm_server_find_free_frame(uint64_t *physical_address)
{
    uint32_t range_index;

    if (physical_address == NULL) {
        return false;
    }
    for (
        range_index = 0;
        range_index
            < micros_vm_boot_info.header.managed_range_count;
        ++range_index
    ) {
        const struct micros_vm_managed_range *range =
            &micros_vm_boot_info.managed_ranges[range_index];
        uint64_t offset;

        for (offset = 0; offset < range->frame_count; ++offset) {
            uint64_t frame_index = range->frame_index + offset;

            if (
                frame_index
                    >= micros_vm_boot_info.header.managed_frame_count
            ) {
                return false;
            }
            if (
                micros_vm_boot_info.frame_states[frame_index]
                    == MICROS_VM_FRAME_FREE
            ) {
                *physical_address =
                    range->base + offset * MICROS_SV39_PAGE_SIZE;
                return true;
            }
        }
    }
    return false;
}

static _Noreturn void vm_server_run_scratch_preemption(void)
{
    struct micros_ipc_message request;
    volatile uint64_t *scratch =
        (volatile uint64_t *)(uintptr_t)
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS;
    uint64_t transaction;
    uint64_t physical_address = 0;
    int driver_index = vm_server_find_service(
        MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID
    );

    vm_server_clear_bytes(&request, sizeof(request));
    vm_server_clear_bytes(
        &scratch_begin_result,
        sizeof(scratch_begin_result)
    );
    if (
        driver_index < 0
        || micros_runtime_receive(
            MICROS_ENDPOINT_ANY,
            &request
        ) != MICROS_SYSCALL_ABI_OK
        || request.source
            != micros_bootstrap_service_config.services[driver_index]
                .endpoint
        || request.type != MICROS_VM_SCRATCH_PREEMPTION_START
        || request.reply_token == 0
        || vm_server_read_u32_le(&request.payload[0])
            != MICROS_VM_SCRATCH_PREEMPTION_VERSION
        || !vm_server_bytes_are_zero(
            &request.payload[16],
            sizeof(request.payload) - 16
        )
        || (
            transaction =
                vm_server_read_u64_le(&request.payload[8])
        ) == 0
        || !vm_server_find_free_frame(&physical_address)
        || micros_runtime_raw_syscall(
            MICROS_VM_LOAD_BEGIN,
            transaction,
            (uintptr_t)&scratch_begin_result,
            MICROS_VM_LOAD_BEGIN_RESULT_SIZE,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
        || scratch_begin_result.version
            != MICROS_VM_LOAD_BEGIN_RESULT_VERSION
        || scratch_begin_result.size
            != MICROS_VM_LOAD_BEGIN_RESULT_SIZE
        || scratch_begin_result.load_token == 0
        || scratch_begin_result.process_slot
            != MICROS_VM_SCRATCH_PREEMPTION_RESERVED_PROCESS_SLOT
        || scratch_begin_result.process_generation != 1
        || scratch_begin_result.reserved0 != 0
        || scratch_begin_result.reserved1 != 0
        || micros_runtime_raw_syscall(
            MICROS_VM_SCRATCH_MAP,
            scratch_begin_result.load_token,
            MICROS_USER_VIRTUAL_BASE,
            physical_address,
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE,
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            0,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    *scratch = MICROS_VM_SCRATCH_PREEMPTION_SENTINEL;
    micros_vm_scratch_preemption_vm_hook(
        MICROS_VM_SCRATCH_PREEMPTION_VM_HOOK_MAGIC
    );
    if (
        *scratch != MICROS_VM_SCRATCH_PREEMPTION_SENTINEL
        || micros_runtime_raw_syscall(
            MICROS_VM_SCRATCH_COMMIT,
            scratch_begin_result.load_token,
            MICROS_USER_VIRTUAL_BASE,
            physical_address,
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            0,
            0,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    micros_vm_scratch_preemption_stale_load(
        MICROS_VM_SCRATCH_VIRTUAL_ADDRESS
    );
}
#endif

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
    && !defined(MICROS_VM_TTY_HANDOFF_TEST) \
    && !defined(MICROS_VM_SCRATCH_PREEMPTION_TEST)
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
    && !defined(MICROS_VM_TTY_HANDOFF_TEST) \
    && !defined(MICROS_VM_SCRATCH_PREEMPTION_TEST)
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

#ifdef MICROS_PM_SERVICE_TEST
enum {
    MICROS_PM_TEST_DATA_FRAME_COUNT = 4,
    MICROS_PM_TEST_FRAME_COUNT =
        MICROS_PM_TEST_DATA_FRAME_COUNT
        + MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT,
};

#define MICROS_PM_TEST_PAGE_DIGEST_OFFSET \
    UINT64_C(14695981039346656037)
#define MICROS_PM_TEST_PAGE_DIGEST_PRIME \
    UINT64_C(1099511628211)

extern const uint8_t micros_pm_lifecycle_child_start[];
extern const uint8_t micros_pm_lifecycle_child_end[];
extern const uint8_t micros_pm_lifecycle_child_pm_endpoint[];

static uint64_t pm_test_load_token;
static uint64_t pm_test_mapping_generation;

static void pm_test_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static bool pm_test_collect_free_frames(
    uint64_t frames[MICROS_PM_TEST_FRAME_COUNT]
)
{
    uint32_t range_index;
    size_t count = 0;

    if (frames == NULL) {
        return false;
    }
    for (
        range_index = 0;
        range_index < micros_vm_boot_info.header.managed_range_count;
        ++range_index
    ) {
        const struct micros_vm_managed_range *range =
            &micros_vm_boot_info.managed_ranges[range_index];
        uint64_t offset;

        for (offset = 0; offset < range->frame_count; ++offset) {
            uint64_t frame_index = range->frame_index + offset;

            if (
                frame_index >= micros_vm_boot_info.header.managed_frame_count
            ) {
                return false;
            }
            if (
                micros_vm_boot_info.frame_states[frame_index]
                    == MICROS_VM_FRAME_FREE
            ) {
                frames[count] =
                    range->base + offset * MICROS_SV39_PAGE_SIZE;
                ++count;
                if (count == MICROS_PM_TEST_FRAME_COUNT) {
                    return true;
                }
            }
        }
    }
    return false;
}

static uint64_t pm_test_digest_byte(uint64_t digest, uint8_t byte)
{
    digest ^= byte;
    return digest * MICROS_PM_TEST_PAGE_DIGEST_PRIME;
}

static uint64_t pm_test_digest_u32(uint64_t digest, uint32_t value)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        digest = pm_test_digest_byte(
            digest,
            (uint8_t)(value >> (index * 8))
        );
    }
    return digest;
}

static uint64_t pm_test_digest_u64(uint64_t digest, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        digest = pm_test_digest_byte(
            digest,
            (uint8_t)(value >> (index * 8))
        );
    }
    return digest;
}

static uint64_t pm_test_page_plan_digest(
    const uint64_t physical_addresses[MICROS_PM_TEST_DATA_FRAME_COUNT],
    const uint32_t permissions[MICROS_PM_TEST_DATA_FRAME_COUNT]
)
{
    static const uint64_t virtual_addresses[
        MICROS_PM_TEST_DATA_FRAME_COUNT
    ] = {
        MICROS_APPLICATION_ENTRY,
        MICROS_APPLICATION_ENTRY + MICROS_SV39_PAGE_SIZE,
        MICROS_APPLICATION_ENTRY + 2 * MICROS_SV39_PAGE_SIZE,
        MICROS_APPLICATION_STACK_BASE,
    };
    uint64_t digest = MICROS_PM_TEST_PAGE_DIGEST_OFFSET;
    size_t index;

    digest = pm_test_digest_u32(digest, 3);
    digest = pm_test_digest_u32(
        digest,
        MICROS_PM_TEST_DATA_FRAME_COUNT
    );
    for (index = 0; index < MICROS_PM_TEST_DATA_FRAME_COUNT; ++index) {
        digest = pm_test_digest_u64(digest, virtual_addresses[index]);
        digest = pm_test_digest_u64(digest, physical_addresses[index]);
        digest = pm_test_digest_u32(digest, permissions[index]);
        digest = pm_test_digest_u32(digest, 0);
    }
    return digest;
}

static bool pm_test_write_child_page(
    uint32_t pm_endpoint,
    volatile uint8_t *scratch
)
{
    uintptr_t blob_start =
        (uintptr_t)micros_pm_lifecycle_child_start;
    uintptr_t blob_end =
        (uintptr_t)micros_pm_lifecycle_child_end;
    uintptr_t endpoint_address =
        (uintptr_t)micros_pm_lifecycle_child_pm_endpoint;
    size_t blob_size;
    size_t endpoint_offset;
    size_t index;

    if (
        blob_end < blob_start
        || endpoint_address < blob_start
        || endpoint_address + sizeof(uint32_t) > blob_end
    ) {
        return false;
    }
    blob_size = blob_end - blob_start;
    endpoint_offset = endpoint_address - blob_start;
    if (blob_size > MICROS_SV39_PAGE_SIZE) {
        return false;
    }
    for (index = 0; index < MICROS_SV39_PAGE_SIZE; ++index) {
        scratch[index] = 0;
    }
    for (index = 0; index < blob_size; ++index) {
        scratch[index] = micros_pm_lifecycle_child_start[index];
    }
    for (index = 0; index < sizeof(uint32_t); ++index) {
        scratch[endpoint_offset + index] =
            (uint8_t)(pm_endpoint >> (index * 8));
    }
    return true;
}

static bool pm_test_load_child(
    const struct micros_ipc_message *request,
    struct micros_ipc_message *reply,
    uint32_t pm_endpoint
)
{
    static const uint64_t virtual_addresses[
        MICROS_PM_TEST_DATA_FRAME_COUNT
    ] = {
        MICROS_APPLICATION_ENTRY,
        MICROS_APPLICATION_ENTRY + MICROS_SV39_PAGE_SIZE,
        MICROS_APPLICATION_ENTRY + 2 * MICROS_SV39_PAGE_SIZE,
        MICROS_APPLICATION_STACK_BASE,
    };
    static const uint32_t permissions[
        MICROS_PM_TEST_DATA_FRAME_COUNT
    ] = {
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_EXECUTE,
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ,
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE,
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE,
    };
    struct micros_vm_load_begin_result begin_result;
    struct micros_vm_load_freeze_plan freeze_plan;
    struct micros_vm_load_freeze_result freeze_result;
    uint64_t frames[MICROS_PM_TEST_FRAME_COUNT];
    volatile uint8_t *scratch =
        (volatile uint8_t *)(uintptr_t)
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS;
    uint64_t transaction =
        vm_server_read_u64_le(&request->payload[8]);
    size_t index;

    vm_server_clear_bytes(&begin_result, sizeof(begin_result));
    vm_server_clear_bytes(&freeze_plan, sizeof(freeze_plan));
    vm_server_clear_bytes(&freeze_result, sizeof(freeze_result));
    if (
        request->type != MICROS_PM_TEST_VM_LOAD
        || request->reply_token == 0
        || vm_server_read_u32_le(&request->payload[0])
            != MICROS_PM_TEST_PROTOCOL_VERSION
        || transaction == 0
        || pm_test_load_token != 0
        || pm_test_mapping_generation != 0
        || !pm_test_collect_free_frames(frames)
        || micros_runtime_raw_syscall(
            MICROS_VM_LOAD_BEGIN,
            transaction,
            (uintptr_t)&begin_result,
            MICROS_VM_LOAD_BEGIN_RESULT_SIZE,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
        || begin_result.version != MICROS_VM_LOAD_BEGIN_RESULT_VERSION
        || begin_result.size != MICROS_VM_LOAD_BEGIN_RESULT_SIZE
        || begin_result.load_token == 0
        || begin_result.process_slot != MICROS_PM_TEST_RESERVED_PROCESS_SLOT
        || begin_result.process_generation != 1
        || begin_result.reserved0 != 0
        || begin_result.reserved1 != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_PM_TEST_DATA_FRAME_COUNT; ++index) {
        if (
            micros_runtime_raw_syscall(
                MICROS_VM_SCRATCH_MAP,
                begin_result.load_token,
                virtual_addresses[index],
                frames[index],
                permissions[index],
                MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
                0,
                MICROS_SYSCALL_ABI_VM_HANDOFF
            ) != MICROS_SYSCALL_ABI_OK
        ) {
            return false;
        }
        if (index == 0) {
            if (!pm_test_write_child_page(pm_endpoint, scratch)) {
                return false;
            }
        } else {
            size_t byte_index;

            for (
                byte_index = 0;
                byte_index < MICROS_SV39_PAGE_SIZE;
                ++byte_index
            ) {
                scratch[byte_index] = 0;
            }
        }
        if (
            micros_runtime_raw_syscall(
                MICROS_VM_SCRATCH_COMMIT,
                begin_result.load_token,
                virtual_addresses[index],
                frames[index],
                MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
                0,
                0,
                MICROS_SYSCALL_ABI_VM_HANDOFF
            ) != MICROS_SYSCALL_ABI_OK
        ) {
            return false;
        }
    }
    freeze_plan.version = MICROS_VM_LOAD_FREEZE_PLAN_VERSION;
    freeze_plan.size = MICROS_VM_LOAD_FREEZE_PLAN_SIZE;
    freeze_plan.load_token = begin_result.load_token;
    for (
        index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        freeze_plan.table_frames[index] =
            frames[MICROS_PM_TEST_DATA_FRAME_COUNT + index];
    }
    freeze_plan.entry = MICROS_APPLICATION_ENTRY;
    freeze_plan.stack_pointer = MICROS_APPLICATION_STACK_TOP - 16;
    freeze_plan.image_page_count = 3;
    freeze_plan.data_page_count = MICROS_PM_TEST_DATA_FRAME_COUNT;
    freeze_plan.page_plan_digest =
        pm_test_page_plan_digest(frames, permissions);
    if (
        freeze_plan.page_plan_digest == 0
        || micros_runtime_raw_syscall(
            MICROS_VM_LOAD_FREEZE,
            (uintptr_t)&freeze_plan,
            MICROS_VM_LOAD_FREEZE_PLAN_SIZE,
            (uintptr_t)&freeze_result,
            MICROS_VM_LOAD_FREEZE_RESULT_SIZE,
            0,
            0,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
        || freeze_result.version != MICROS_VM_LOAD_FREEZE_RESULT_VERSION
        || freeze_result.size != MICROS_VM_LOAD_FREEZE_RESULT_SIZE
        || freeze_result.mapping_generation == 0
        || !vm_server_bytes_are_zero(
            freeze_result.reserved,
            sizeof(freeze_result.reserved)
        )
    ) {
        return false;
    }
    pm_test_load_token = begin_result.load_token;
    pm_test_mapping_generation = freeze_result.mapping_generation;
    reply->type = MICROS_PM_TEST_VM_LOADED;
    vm_server_write_u32_le(
        &reply->payload[0],
        MICROS_PM_TEST_PROTOCOL_VERSION
    );
    pm_test_write_u64_le(&reply->payload[8], transaction);
    pm_test_write_u64_le(&reply->payload[16], pm_test_load_token);
    pm_test_write_u64_le(
        &reply->payload[24],
        pm_test_mapping_generation
    );
    vm_server_write_u32_le(
        &reply->payload[32],
        begin_result.process_slot
    );
    vm_server_write_u32_le(
        &reply->payload[36],
        begin_result.process_generation
    );
    return true;
}

static bool pm_test_commit_child(
    const struct micros_ipc_message *request,
    struct micros_ipc_message *reply
)
{
    uint64_t load_token =
        vm_server_read_u64_le(&request->payload[8]);
    uint64_t mapping_generation =
        vm_server_read_u64_le(&request->payload[16]);

    if (
        request->type != MICROS_PM_TEST_VM_COMMIT
        || request->reply_token == 0
        || vm_server_read_u32_le(&request->payload[0])
            != MICROS_PM_TEST_PROTOCOL_VERSION
        || load_token == 0
        || load_token != pm_test_load_token
        || mapping_generation == 0
        || mapping_generation != pm_test_mapping_generation
        || micros_runtime_raw_syscall(
            MICROS_VM_LOAD_COMMIT,
            load_token,
            mapping_generation,
            0,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    reply->type = MICROS_PM_TEST_VM_COMMITTED;
    vm_server_write_u32_le(
        &reply->payload[0],
        MICROS_PM_TEST_PROTOCOL_VERSION
    );
    pm_test_write_u64_le(&reply->payload[8], load_token);
    pm_test_write_u64_le(&reply->payload[16], mapping_generation);
    return true;
}

static bool pm_test_release_child(
    const struct micros_ipc_message *request,
    struct micros_ipc_message *reply
)
{
    uint64_t exit_token =
        vm_server_read_u64_le(&request->payload[8]);
    uint64_t load_token =
        vm_server_read_u64_le(&request->payload[16]);
    uint64_t mapping_generation =
        vm_server_read_u64_le(&request->payload[24]);

    if (
        request->type != MICROS_PM_TEST_VM_EXIT_RELEASE
        || request->reply_token == 0
        || vm_server_read_u32_le(&request->payload[0])
            != MICROS_PM_TEST_PROTOCOL_VERSION
        || exit_token == 0
        || load_token == 0
        || load_token != pm_test_load_token
        || mapping_generation == 0
        || mapping_generation != pm_test_mapping_generation
        || micros_runtime_raw_syscall(
            MICROS_VM_EXIT_RELEASE,
            exit_token,
            load_token,
            mapping_generation,
            0,
            0,
            0,
            MICROS_SYSCALL_ABI_VM_HANDOFF
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    pm_test_load_token = 0;
    pm_test_mapping_generation = 0;
    reply->type = MICROS_PM_TEST_VM_EXIT_RELEASED;
    vm_server_write_u32_le(
        &reply->payload[0],
        MICROS_PM_TEST_PROTOCOL_VERSION
    );
    pm_test_write_u64_le(&reply->payload[8], exit_token);
    return true;
}

static bool pm_test_serve_request(
    const struct micros_ipc_message *request
)
{
    struct micros_ipc_message reply;
    int pm_index =
        vm_server_find_service(MICROS_PM_TEST_PM_SERVICE_ID);
    bool handled;

    vm_server_clear_bytes(&reply, sizeof(reply));
    if (
        pm_index < 0
        || request->source
            != micros_bootstrap_service_config.services[pm_index].endpoint
    ) {
        return false;
    }
    handled = request->type == MICROS_PM_TEST_VM_LOAD
        ? pm_test_load_child(request, &reply, request->source)
        : request->type == MICROS_PM_TEST_VM_COMMIT
            ? pm_test_commit_child(request, &reply)
            : request->type == MICROS_PM_TEST_VM_EXIT_RELEASE
                ? pm_test_release_child(request, &reply)
                : false;
    return (
        handled
        && micros_runtime_reply(request->reply_token, &reply)
            == MICROS_SYSCALL_ABI_OK
    );
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
#elif defined(MICROS_VM_SCRATCH_PREEMPTION_TEST)
    vm_server_run_scratch_preemption();
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
#ifdef MICROS_PM_SERVICE_TEST
        if (!pm_test_serve_request(&message)) {
            __builtin_trap();
        }
#elif defined(MICROS_VM_TTY_HANDOFF_TEST)
        __builtin_trap();
#endif
    }
}
