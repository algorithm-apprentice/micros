#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/runtime/raw_syscall.h"
#include "micros/bootstrap_control.h"
#include "micros/vm_bootstrap.h"
#include "tests/qemu/vm_handoff_protocol.h"

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;

_Alignas(MICROS_VM_BOOT_INFO_ALIGNMENT)
__attribute__((section(".data.vm_boot_info"), used))
struct micros_vm_boot_info micros_vm_boot_info = {0};

static volatile uint64_t vm_data = UINT64_C(0x564d44415441564d);
static uint8_t grant_buffer[MICROS_VM_HANDOFF_TEST_DATA_SIZE];

void micros_vm_trigger_self_fault(void);

#define VM_FNV_OFFSET UINT64_C(14695981039346656037)
#define VM_FNV_PRIME UINT64_C(1099511628211)

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool bytes_are_zero(const void *storage, size_t size)
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

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static uint64_t read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool endpoint_matches_process(
    uint32_t endpoint,
    uint16_t process_slot,
    uint32_t process_generation
)
{
    return (
        process_slot < MICROS_PROCESS_CAPACITY
        && process_generation != 0
        && process_generation <= MICROS_PROCESS_GENERATION_MAX
        && endpoint
            == (
                (process_generation << MICROS_ENDPOINT_SLOT_BITS)
                | process_slot
            )
        && endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
    );
}

static bool physical_ranges_are_canonical(
    const struct micros_vm_physical_range *ranges,
    uint32_t count
)
{
    uint64_t previous_end = 0;
    uint32_t index;

    for (index = 0; index < count; ++index) {
        uint64_t end;

        if (
            ranges[index].size == 0
            || ranges[index].base % MICROS_FRAME_SIZE != 0
            || ranges[index].size % MICROS_FRAME_SIZE != 0
            || !add_u64(
                ranges[index].base,
                ranges[index].size,
                &end
            )
            || (index != 0 && ranges[index].base <= previous_end)
        ) {
            return false;
        }
        previous_end = end;
    }
    return true;
}

static bool managed_range_matches(
    size_t managed_index,
    uint64_t base,
    uint64_t end,
    uint64_t frame_index
)
{
    const struct micros_vm_managed_range *range;

    if (
        base >= end
        || managed_index
            >= micros_vm_boot_info.header.managed_range_count
    ) {
        return false;
    }
    range = &micros_vm_boot_info.managed_ranges[managed_index];
    return (
        range->base == base
        && range->frame_count == (end - base) / MICROS_FRAME_SIZE
        && range->frame_index == frame_index
    );
}

static bool managed_ranges_match(void)
{
    size_t managed_index = 0;
    uint64_t frame_index = 0;
    uint32_t memory_index;

    for (
        memory_index = 0;
        memory_index < micros_vm_boot_info.header.memory_range_count;
        ++memory_index
    ) {
        const struct micros_vm_physical_range *memory =
            &micros_vm_boot_info.memory_ranges[memory_index];
        uint64_t cursor = memory->base;
        uint64_t memory_end = memory->base + memory->size;
        uint32_t reserved_index;

        for (
            reserved_index = 0;
            reserved_index
                < micros_vm_boot_info.header.reserved_range_count;
            ++reserved_index
        ) {
            const struct micros_vm_physical_range *reserved =
                &micros_vm_boot_info.reserved_ranges[reserved_index];
            uint64_t reserved_end =
                reserved->base + reserved->size;
            uint64_t available_end;
            uint64_t frame_count;

            if (reserved_end <= cursor) {
                continue;
            }
            if (reserved->base >= memory_end) {
                break;
            }
            available_end = reserved->base < memory_end
                ? reserved->base
                : memory_end;
            if (cursor < available_end) {
                frame_count =
                    (available_end - cursor) / MICROS_FRAME_SIZE;
                if (
                    frame_count
                        > MICROS_VM_MAX_MANAGED_FRAMES - frame_index
                    || !managed_range_matches(
                        managed_index,
                        cursor,
                        available_end,
                        frame_index
                    )
                ) {
                    return false;
                }
                ++managed_index;
                frame_index += frame_count;
            }
            if (reserved_end > cursor) {
                cursor = reserved_end;
            }
            if (cursor >= memory_end) {
                break;
            }
        }
        if (cursor < memory_end) {
            uint64_t frame_count =
                (memory_end - cursor) / MICROS_FRAME_SIZE;

            if (
                frame_count > MICROS_VM_MAX_MANAGED_FRAMES - frame_index
                || !managed_range_matches(
                    managed_index,
                    cursor,
                    memory_end,
                    frame_index
                )
            ) {
                return false;
            }
            ++managed_index;
            frame_index += frame_count;
        }
    }
    return (
        managed_index
            == micros_vm_boot_info.header.managed_range_count
        && frame_index
            == micros_vm_boot_info.header.managed_frame_count
    );
}

static bool physical_to_frame_index(
    uint64_t physical_address,
    uint64_t *frame_index
)
{
    uint32_t index;

    if (
        physical_address % MICROS_FRAME_SIZE != 0
        || frame_index == NULL
    ) {
        return false;
    }
    for (
        index = 0;
        index < micros_vm_boot_info.header.managed_range_count;
        ++index
    ) {
        const struct micros_vm_managed_range *range =
            &micros_vm_boot_info.managed_ranges[index];
        uint64_t byte_count =
            range->frame_count * MICROS_FRAME_SIZE;

        if (
            physical_address >= range->base
            && physical_address - range->base < byte_count
        ) {
            *frame_index = range->frame_index
                + (physical_address - range->base)
                    / MICROS_FRAME_SIZE;
            return true;
        }
    }
    return false;
}

static bool permissions_are_valid(uint8_t permissions)
{
    return (
        permissions != 0
        && (
            permissions & ~MICROS_VM_PERMISSION_DEFINED_MASK
        ) == 0
        && (
            (permissions & MICROS_VM_PERMISSION_WRITE) == 0
            || (permissions & MICROS_VM_PERMISSION_READ) != 0
        )
        && (
            permissions
            & (
                MICROS_VM_PERMISSION_WRITE
                | MICROS_VM_PERMISSION_EXECUTE
            )
        ) != (
            MICROS_VM_PERMISSION_WRITE
            | MICROS_VM_PERMISSION_EXECUTE
        )
    );
}

static uint64_t calculate_digest(void)
{
    const uint8_t *bytes = (const uint8_t *)&micros_vm_boot_info;
    const size_t digest_offset =
        offsetof(struct micros_vm_boot_info, header.digest);
    uint64_t digest = VM_FNV_OFFSET;
    size_t index;

    for (index = 0; index < sizeof(micros_vm_boot_info); ++index) {
        uint8_t byte = (
            index >= digest_offset
            && index
                < digest_offset
                    + sizeof(micros_vm_boot_info.header.digest)
        )
            ? 0
            : bytes[index];

        digest ^= byte;
        digest *= VM_FNV_PRIME;
    }
    return digest;
}

static bool validate_boot_info(void)
{
    uint64_t free_count = 0;
    uint64_t kernel_count = 0;
    uint64_t self_count = 0;
    uint64_t service_count = 0;
    uint64_t transferable_count = 0;
    uint64_t mapped_count = 0;
    uint32_t expected_mapping_index = 0;
    bool found_self = false;
    uint32_t space_index;
    uint64_t frame_index;

    if (
        (uintptr_t)&micros_vm_boot_info
            % MICROS_VM_BOOT_INFO_ALIGNMENT
            != 0
        || micros_vm_boot_info.header.version
            != MICROS_VM_BOOT_INFO_VERSION
        || micros_vm_boot_info.header.header_size
            != MICROS_VM_BOOT_HEADER_SIZE
        || micros_vm_boot_info.header.total_size
            != MICROS_VM_BOOT_INFO_SIZE
        || micros_vm_boot_info.header.physical_range_size
            != MICROS_VM_PHYSICAL_RANGE_SIZE
        || micros_vm_boot_info.header.managed_range_size
            != MICROS_VM_MANAGED_RANGE_SIZE
        || micros_vm_boot_info.header.memory_range_capacity
            != MICROS_VM_MAX_MEMORY_RANGES
        || micros_vm_boot_info.header.reserved_range_capacity
            != MICROS_VM_MAX_RESERVED_RANGES
        || micros_vm_boot_info.header.managed_range_capacity
            != MICROS_VM_MAX_MANAGED_RANGES
        || micros_vm_boot_info.header.frame_state_capacity
            != MICROS_VM_MAX_MANAGED_FRAMES
        || micros_vm_boot_info.header.frame_state_size != 1
        || micros_vm_boot_info.header.ownership_phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || micros_vm_boot_info.header.flags != 0
        || micros_vm_boot_info.header.reserved0 != 0
        || micros_vm_boot_info.header.address_space_entry_size
            != MICROS_VM_ADDRESS_SPACE_SIZE
        || micros_vm_boot_info.header.address_space_capacity
            != MICROS_VM_MAX_STATIC_ADDRESS_SPACES
        || micros_vm_boot_info.header.mapping_entry_size
            != MICROS_VM_MAPPING_SIZE
        || micros_vm_boot_info.header.mapping_capacity
            != MICROS_VM_MAX_STATIC_MAPPINGS
        || micros_vm_boot_info.header.service_id
            != MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID
        || micros_vm_boot_info.header.self_endpoint
            != micros_bootstrap_service_config.self_endpoint
        || !bytes_are_zero(
            micros_vm_boot_info.header.reserved,
            sizeof(micros_vm_boot_info.header.reserved)
        )
        || micros_vm_boot_info.header.memory_range_count == 0
        || micros_vm_boot_info.header.memory_range_count
            > MICROS_VM_MAX_MEMORY_RANGES
        || micros_vm_boot_info.header.reserved_range_count
            > MICROS_VM_MAX_RESERVED_RANGES
        || micros_vm_boot_info.header.managed_range_count == 0
        || micros_vm_boot_info.header.managed_range_count
            > MICROS_VM_MAX_MANAGED_RANGES
        || micros_vm_boot_info.header.address_space_count
            != MICROS_VM_HANDOFF_TEST_SERVICE_COUNT
        || micros_vm_boot_info.header.mapping_count == 0
        || micros_vm_boot_info.header.mapping_count
            > MICROS_VM_MAX_STATIC_MAPPINGS
        || micros_vm_boot_info.header.managed_frame_count == 0
        || micros_vm_boot_info.header.managed_frame_count
            > MICROS_VM_MAX_MANAGED_FRAMES
        || !physical_ranges_are_canonical(
            micros_vm_boot_info.memory_ranges,
            micros_vm_boot_info.header.memory_range_count
        )
        || !physical_ranges_are_canonical(
            micros_vm_boot_info.reserved_ranges,
            micros_vm_boot_info.header.reserved_range_count
        )
        || !managed_ranges_match()
        || !bytes_are_zero(
            &micros_vm_boot_info.memory_ranges[
                micros_vm_boot_info.header.memory_range_count
            ],
            (
                MICROS_VM_MAX_MEMORY_RANGES
                - micros_vm_boot_info.header.memory_range_count
            ) * sizeof(micros_vm_boot_info.memory_ranges[0])
        )
        || !bytes_are_zero(
            &micros_vm_boot_info.reserved_ranges[
                micros_vm_boot_info.header.reserved_range_count
            ],
            (
                MICROS_VM_MAX_RESERVED_RANGES
                - micros_vm_boot_info.header.reserved_range_count
            ) * sizeof(micros_vm_boot_info.reserved_ranges[0])
        )
        || !bytes_are_zero(
            &micros_vm_boot_info.managed_ranges[
                micros_vm_boot_info.header.managed_range_count
            ],
            (
                MICROS_VM_MAX_MANAGED_RANGES
                - micros_vm_boot_info.header.managed_range_count
            ) * sizeof(micros_vm_boot_info.managed_ranges[0])
        )
    ) {
        return false;
    }

    for (
        space_index = 0;
        space_index < micros_vm_boot_info.header.address_space_count;
        ++space_index
    ) {
        const struct micros_vm_address_space *space =
            &micros_vm_boot_info.address_spaces[space_index];
        uint64_t root_frame_index;
        uint32_t mapping_end;
        uint32_t other;

        if (
            space->service_id == 0
            || space->service_id > 63
            || !endpoint_matches_process(
                space->endpoint,
                space->process_slot,
                space->process_generation
            )
            || space->mapping_count == 0
            || space->mapping_index != expected_mapping_index
            || space->flags != 0
            || !physical_to_frame_index(
                space->root_physical_address,
                &root_frame_index
            )
            || micros_vm_boot_info.frame_states[root_frame_index]
                != MICROS_VM_FRAME_KERNEL
        ) {
            return false;
        }
        mapping_end = space->mapping_index + space->mapping_count;
        if (mapping_end > micros_vm_boot_info.header.mapping_count) {
            return false;
        }
        for (other = 0; other < space_index; ++other) {
            if (
                micros_vm_boot_info.address_spaces[other].service_id
                    == space->service_id
                || micros_vm_boot_info.address_spaces[other].endpoint
                    == space->endpoint
                || micros_vm_boot_info.address_spaces[other].process_slot
                    == space->process_slot
                || micros_vm_boot_info.address_spaces[other]
                    .root_physical_address
                    == space->root_physical_address
            ) {
                return false;
            }
        }
        if (
            space->service_id
                == MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID
        ) {
            if (
                found_self
                || space->endpoint
                    != micros_vm_boot_info.header.self_endpoint
            ) {
                return false;
            }
            found_self = true;
        }
        expected_mapping_index = mapping_end;
    }
    if (
        !found_self
        || expected_mapping_index
            != micros_vm_boot_info.header.mapping_count
        || !bytes_are_zero(
            &micros_vm_boot_info.address_spaces[
                micros_vm_boot_info.header.address_space_count
            ],
            (
                MICROS_VM_MAX_STATIC_ADDRESS_SPACES
                - micros_vm_boot_info.header.address_space_count
            ) * sizeof(micros_vm_boot_info.address_spaces[0])
        )
    ) {
        return false;
    }

    for (
        frame_index = 0;
        frame_index < micros_vm_boot_info.header.mapping_count;
        ++frame_index
    ) {
        const struct micros_vm_mapping *mapping =
            &micros_vm_boot_info.mappings[frame_index];
        const struct micros_vm_address_space *space = NULL;
        uint64_t mapped_frame_index;
        uint8_t expected_role;
        uint32_t candidate_space;
        uint64_t other;

        for (
            candidate_space = 0;
            candidate_space
                < micros_vm_boot_info.header.address_space_count;
            ++candidate_space
        ) {
            const struct micros_vm_address_space *candidate =
                &micros_vm_boot_info.address_spaces[candidate_space];

            if (
                frame_index >= candidate->mapping_index
                && frame_index
                    < candidate->mapping_index
                        + candidate->mapping_count
            ) {
                space = candidate;
                break;
            }
        }
        if (
            space == NULL
            || mapping->process_slot != space->process_slot
            || mapping->process_generation
                != space->process_generation
            || !permissions_are_valid(mapping->permissions)
            || mapping->virtual_address % MICROS_FRAME_SIZE != 0
            || mapping->virtual_address < MICROS_USER_VIRTUAL_BASE
            || mapping->virtual_address >= MICROS_USER_VIRTUAL_END
            || !physical_to_frame_index(
                mapping->physical_address,
                &mapped_frame_index
            )
        ) {
            return false;
        }
        expected_role = (
            space->service_id == MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID
        )
            ? MICROS_VM_MAPPING_SELF_WIRED
            : MICROS_VM_MAPPING_SERVICE_WIRED;
        if (
            mapping->role != expected_role
            || micros_vm_boot_info.frame_states[mapped_frame_index]
                != (
                    expected_role == MICROS_VM_MAPPING_SELF_WIRED
                    ? MICROS_VM_FRAME_SELF_WIRED
                    : MICROS_VM_FRAME_SERVICE_WIRED
                )
            || (
                frame_index != space->mapping_index
                && micros_vm_boot_info.mappings[frame_index - 1]
                    .virtual_address
                    >= mapping->virtual_address
            )
        ) {
            return false;
        }
        for (other = 0; other < frame_index; ++other) {
            if (
                micros_vm_boot_info.mappings[other].physical_address
                    == mapping->physical_address
            ) {
                return false;
            }
        }
        ++mapped_count;
    }
    if (
        !bytes_are_zero(
            &micros_vm_boot_info.mappings[
                micros_vm_boot_info.header.mapping_count
            ],
            (
                MICROS_VM_MAX_STATIC_MAPPINGS
                - micros_vm_boot_info.header.mapping_count
            ) * sizeof(micros_vm_boot_info.mappings[0])
        )
    ) {
        return false;
    }
    for (
        frame_index = 0;
        frame_index < micros_vm_boot_info.header.managed_frame_count;
        ++frame_index
    ) {
        switch (micros_vm_boot_info.frame_states[frame_index]) {
        case MICROS_VM_FRAME_FREE:
            ++free_count;
            break;
        case MICROS_VM_FRAME_KERNEL:
            ++kernel_count;
            break;
        case MICROS_VM_FRAME_SELF_WIRED:
            ++self_count;
            break;
        case MICROS_VM_FRAME_SERVICE_WIRED:
            ++service_count;
            break;
        case MICROS_VM_FRAME_TRANSFERABLE:
            ++transferable_count;
            break;
        default:
            return false;
        }
    }
    return (
        bytes_are_zero(
            &micros_vm_boot_info.frame_states[
                micros_vm_boot_info.header.managed_frame_count
            ],
            MICROS_VM_MAX_MANAGED_FRAMES
                - micros_vm_boot_info.header.managed_frame_count
        )
        && free_count == micros_vm_boot_info.header.free_frame_count
        && kernel_count == micros_vm_boot_info.header.kernel_frame_count
        && self_count
            == micros_vm_boot_info.header.vm_self_wired_frame_count
        && service_count
            == micros_vm_boot_info.header.service_wired_frame_count
        && transferable_count == 0
        && micros_vm_boot_info.header.transferable_frame_count == 0
        && self_count != 0
        && mapped_count == self_count + service_count
        && free_count + kernel_count + self_count + service_count
            == micros_vm_boot_info.header.managed_frame_count
        && calculate_digest() == micros_vm_boot_info.header.digest
    );
}

static int find_service(uint32_t service_id)
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

static bool validate_configuration(void)
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
        || !bytes_are_zero(
            (const void *)micros_bootstrap_service_config.reserved,
            sizeof(micros_bootstrap_service_config.reserved)
        )
    ) {
        return false;
    }
    self_index = find_service(MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID);
    launcher_index = find_service(
        MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID
    );
    probe_index = find_service(
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

static bool send_ready(void)
{
    struct micros_ipc_message message;

    clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    write_u32_le(
        &message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    write_u32_le(
        &message.payload[4],
        MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID
    );
    write_u32_le(
        &message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    write_u32_le(&message.payload[12], 0);
    write_u32_le(
        &message.payload[16],
        micros_bootstrap_service_config.self_endpoint
    );
    return (
        micros_runtime_call(
            micros_bootstrap_service_config.launcher_endpoint,
            &message
        ) == MICROS_SYSCALL_ABI_OK
        && message.source
            == micros_bootstrap_service_config.launcher_endpoint
        && message.type == MICROS_BOOTSTRAP_MESSAGE_READY_ACK
        && message.reply_token == 0
        && read_u32_le(&message.payload[4])
            == MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID
    );
}

static bool complete_handoff(void)
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

static bool serve_probe(void)
{
    struct micros_ipc_message request;
    struct micros_ipc_message reply;
    uint64_t raw_grant;
    micros_grant_t grant;
    uint32_t length;
    int probe_index = find_service(
        MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID
    );
    size_t index;

    if (probe_index < 0) {
        return false;
    }
    clear_bytes(&request, sizeof(request));
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
        || read_u32_le(&request.payload[12])
            != MICROS_VM_HANDOFF_TEST_PAYLOAD_MAGIC
    ) {
        return false;
    }
    raw_grant = read_u64_le(&request.payload[0]);
    length = read_u32_le(&request.payload[8]);
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
    clear_bytes(&reply, sizeof(reply));
    reply.type = MICROS_VM_HANDOFF_TEST_REPLY;
    write_u32_le(&reply.payload[0], sizeof(grant_buffer));
    write_u32_le(
        &reply.payload[4],
        MICROS_VM_HANDOFF_TEST_PAYLOAD_MAGIC
    );
    return micros_runtime_reply(request.reply_token, &reply)
        == MICROS_SYSCALL_ABI_OK;
}

void micros_service_main(void)
{
    if (
        vm_data != UINT64_C(0x564d44415441564d)
        || !validate_configuration()
        || !validate_boot_info()
        || !complete_handoff()
    ) {
        __builtin_trap();
    }
#ifdef MICROS_VM_SELF_FAULT_RUNNING
    micros_vm_trigger_self_fault();
#endif
    if (!send_ready()) {
        __builtin_trap();
    }
    if (!serve_probe()) {
        __builtin_trap();
    }
#ifdef MICROS_VM_SELF_FAULT_SEALED
    micros_vm_trigger_self_fault();
#endif
    for (;;) {
        struct micros_ipc_message message;

        clear_bytes(&message, sizeof(message));
        if (
            micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
                != MICROS_SYSCALL_ABI_OK
        ) {
            __builtin_trap();
        }
    }
}
