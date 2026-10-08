#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/runtime/raw_syscall.h"
#include "micros/bootstrap_control.h"
#include "tests/qemu/bootstrap_protocol.h"

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;
const volatile uint64_t micros_bootstrap_launcher_rodata =
    UINT64_C(0x4c41554e43484552);
volatile uint64_t micros_bootstrap_launcher_data =
    UINT64_C(0x4253544c41554e43);

enum launcher_service_state {
    LAUNCHER_SERVICE_PREPARED = 1,
    LAUNCHER_SERVICE_STARTING,
    LAUNCHER_SERVICE_READY,
};

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
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

static bool name_is_canonical(
    const char name[MICROS_BOOTSTRAP_NAME_SIZE]
)
{
    bool terminated = false;
    size_t index;

    if (name[0] == '\0') {
        return false;
    }
    for (index = 0; index < MICROS_BOOTSTRAP_NAME_SIZE; ++index) {
        if (name[index] == '\0') {
            terminated = true;
        } else if (terminated) {
            return false;
        }
    }
    return terminated;
}

static int64_t bootstrap_control(
    uint32_t command,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t value,
    uint32_t detail
)
{
    return micros_runtime_raw_syscall(
        command,
        service_id,
        endpoint,
        value,
        detail,
        0,
        0,
        MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL
    );
}

static _Noreturn void fail(
    uint32_t service_id,
    micros_endpoint_t endpoint,
    enum micros_bootstrap_failure_reason reason,
    uint32_t detail
)
{
    (void)bootstrap_control(
        MICROS_BOOTSTRAP_COMMAND_FAIL,
        service_id,
        endpoint,
        reason,
        detail
    );
    __builtin_trap();
}

static int find_config_endpoint(micros_endpoint_t endpoint)
{
    size_t index;

    for (
        index = 0;
        index < micros_bootstrap_service_config.service_count;
        ++index
    ) {
        if (
            micros_bootstrap_service_config.services[index].endpoint
                == endpoint
        ) {
            return (int)index;
        }
    }
    return -1;
}

static int find_manifest_service(
    const struct micros_bootstrap_manifest *manifest,
    uint32_t service_id
)
{
    size_t index;

    for (index = 0; index < manifest->header.entry_count; ++index) {
        if (manifest->entries[index].service_id == service_id) {
            return (int)index;
        }
    }
    return -1;
}

static bool validate_manifest(
    const struct micros_bootstrap_manifest *manifest,
    uint32_t order[MICROS_BOOTSTRAP_SERVICE_CAPACITY]
)
{
    uint64_t active_ids = 0;
    uint64_t selected_ids = 0;
    uint32_t controller_id = 0;
    size_t index;

    if (
        manifest->header.magic != MICROS_BOOTSTRAP_MANIFEST_MAGIC
        || manifest->header.version != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || manifest->header.header_size
            != MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE
        || manifest->header.entry_size
            != MICROS_BOOTSTRAP_MANIFEST_ENTRY_SIZE
        || manifest->header.entry_capacity
            != MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || manifest->header.entry_count
            != MICROS_BOOTSTRAP_TEST_SERVICE_COUNT
        || manifest->header.flags != 0
        || manifest->header.manifest_size
            != MICROS_BOOTSTRAP_MANIFEST_SIZE
        || manifest->header.reserved0 != 0
        || manifest->header.reserved1 != 0
        || !bytes_are_zero(
            manifest->header.reserved2,
            sizeof(manifest->header.reserved2)
        )
        || !bytes_are_zero(
            &manifest->entries[manifest->header.entry_count],
            (
                MICROS_BOOTSTRAP_SERVICE_CAPACITY
                - manifest->header.entry_count
            ) * sizeof(manifest->entries[0])
        )
    ) {
        return false;
    }
    for (index = 0; index < manifest->header.entry_count; ++index) {
        const struct micros_bootstrap_manifest_entry *entry =
            &manifest->entries[index];
        size_t other;

        if (
            entry->service_id == 0
            || entry->service_id > 63
            || entry->profile_id == 0
            || entry->stack_page_count == 0
            || entry->user_page_limit == 0
            || !name_is_canonical(entry->service_name)
            || !name_is_canonical(entry->profile_name)
            || !bytes_are_zero(
                entry->reserved0,
                sizeof(entry->reserved0)
            )
            || entry->reserved1 != 0
            || !bytes_are_zero(
                entry->reserved2,
                sizeof(entry->reserved2)
            )
            || (
                entry->role_flags
                & ~MICROS_BOOTSTRAP_ROLE_CONTROLLER
            ) != 0
            || entry->device_base != 0
            || entry->device_length != 0
            || entry->irq_source != 0
            || (
                entry->prerequisites
                & (
                    UINT64_C(1)
                    << (entry->service_id - 1)
                )
            ) != 0
        ) {
            return false;
        }
        if (
            (
                entry->role_flags
                & MICROS_BOOTSTRAP_ROLE_CONTROLLER
            ) != 0
        ) {
            if (
                controller_id != 0
                || entry->prerequisites != 0
                || entry->ready_timeout_counter_ticks != 0
                || entry->stack_page_count != 1
            ) {
                return false;
            }
            controller_id = entry->service_id;
        } else if (
            entry->prerequisites == 0
            || entry->ready_timeout_counter_ticks == 0
        ) {
            return false;
        }
        for (other = 0; other < index; ++other) {
            if (
                manifest->entries[other].service_id
                    == entry->service_id
                || manifest->entries[other].process_slot
                    == entry->process_slot
                || manifest->entries[other].image_id
                    == entry->image_id
            ) {
                return false;
            }
        }
        active_ids |= UINT64_C(1) << (entry->service_id - 1);
    }
    if (
        controller_id
            != MICROS_BOOTSTRAP_TEST_LAUNCHER_SERVICE_ID
    ) {
        return false;
    }
    for (index = 0; index < manifest->header.entry_count; ++index) {
        uint32_t best_id = UINT32_MAX;
        size_t candidate_index;

        for (
            candidate_index = 0;
            candidate_index < manifest->header.entry_count;
            ++candidate_index
        ) {
            const struct micros_bootstrap_manifest_entry *entry =
                &manifest->entries[candidate_index];
            uint64_t bit = UINT64_C(1) << (entry->service_id - 1);

            if (
                (selected_ids & bit) == 0
                && (entry->prerequisites & ~selected_ids) == 0
                && entry->service_id < best_id
            ) {
                best_id = entry->service_id;
            }
        }
        if (best_id == UINT32_MAX) {
            return false;
        }
        order[index] = best_id;
        selected_ids |= UINT64_C(1) << (best_id - 1);
    }
    return (
        order[0] == controller_id
        && selected_ids == active_ids
    );
}

static bool validate_configuration(
    const struct micros_bootstrap_manifest *manifest
)
{
    size_t index;

    if (
        micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.service_id
            != MICROS_BOOTSTRAP_TEST_LAUNCHER_SERVICE_ID
        || micros_bootstrap_service_config.self_endpoint
            == MICROS_ENDPOINT_NONE
        || micros_bootstrap_service_config.launcher_endpoint
            != micros_bootstrap_service_config.self_endpoint
        || micros_bootstrap_service_config.service_count
            != manifest->header.entry_count
        || micros_bootstrap_service_config.manifest_view_address
            != MICROS_BOOTSTRAP_MANIFEST_VIEW
        || !bytes_are_zero(
            (const void *)micros_bootstrap_service_config.reserved,
            sizeof(micros_bootstrap_service_config.reserved)
        )
    ) {
        return false;
    }
    for (
        index = 0;
        index < micros_bootstrap_service_config.service_count;
        ++index
    ) {
        const struct micros_bootstrap_service_endpoint *service =
            (const struct micros_bootstrap_service_endpoint *)
                &micros_bootstrap_service_config.services[index];

        if (
            service->service_id != index + 1
            || service->endpoint == MICROS_ENDPOINT_NONE
            || find_manifest_service(manifest, service->service_id) < 0
        ) {
            return false;
        }
    }
    return bytes_are_zero(
        (const void *)
            &micros_bootstrap_service_config.services[
                micros_bootstrap_service_config.service_count
            ],
        (
            MICROS_BOOTSTRAP_SERVICE_CAPACITY
            - micros_bootstrap_service_config.service_count
        ) * sizeof(micros_bootstrap_service_config.services[0])
    );
}

static uint32_t message_shape_detail(
    const struct micros_ipc_message *message
)
{
    size_t index;

    if (
        (
            message->type & MICROS_IPC_TYPE_KERNEL_MASK
        ) != 0
    ) {
        return 9;
    }
    if (message->type != MICROS_BOOTSTRAP_MESSAGE_READY) {
        return 1;
    }
    if (message->reply_token == 0) {
        return 2;
    }
    if (
        read_u32_le(&message->payload[0])
        != MICROS_BOOTSTRAP_MANIFEST_VERSION
    ) {
        return 3;
    }
    if (
        read_u32_le(&message->payload[8])
        != MICROS_BOOTSTRAP_MANIFEST_VERSION
    ) {
        return 5;
    }
    if (read_u32_le(&message->payload[12]) != 0) {
        return 6;
    }
    for (index = 20; index < sizeof(message->payload); ++index) {
        if (message->payload[index] != 0) {
            return 8;
        }
    }
    return 0;
}

void micros_service_main(void)
{
    const struct micros_bootstrap_manifest *manifest =
        (const struct micros_bootstrap_manifest *)(uintptr_t)
            MICROS_BOOTSTRAP_MANIFEST_VIEW;
    uint32_t order[MICROS_BOOTSTRAP_SERVICE_CAPACITY] = {0};
    uint8_t state[MICROS_BOOTSTRAP_SERVICE_CAPACITY] = {0};
    size_t order_index;

    if (
        micros_bootstrap_launcher_rodata
            != UINT64_C(0x4c41554e43484552)
        || micros_bootstrap_launcher_data
            != UINT64_C(0x4253544c41554e43)
        ||
        !validate_manifest(manifest, order)
        || !validate_configuration(manifest)
    ) {
        fail(
            0,
            MICROS_ENDPOINT_NONE,
            MICROS_BOOTSTRAP_FAILURE_AUTHORITY,
            0
        );
    }
    for (order_index = 0; order_index < manifest->header.entry_count; ++order_index) {
        int manifest_index = find_manifest_service(
            manifest,
            order[order_index]
        );

        if (manifest_index < 0) {
            fail(
                0,
                MICROS_ENDPOINT_NONE,
                MICROS_BOOTSTRAP_FAILURE_AUTHORITY,
                0
            );
        }
        state[manifest_index] = order_index == 0
            ? LAUNCHER_SERVICE_READY
            : LAUNCHER_SERVICE_PREPARED;
    }
    for (order_index = 1; order_index < manifest->header.entry_count; ++order_index) {
        uint32_t service_id = order[order_index];
        int expected_index = find_manifest_service(
            manifest,
            service_id
        );
        struct micros_ipc_message message = {0};
        int source_config_index;
        int source_manifest_index;
        uint32_t detail;
        int64_t result;

        if (
            expected_index < 0
            || bootstrap_control(
                MICROS_BOOTSTRAP_COMMAND_RELEASE,
                service_id,
                0,
                0,
                0
            ) != MICROS_SYSCALL_ABI_OK
        ) {
            fail(
                service_id,
                MICROS_ENDPOINT_NONE,
                MICROS_BOOTSTRAP_FAILURE_RELEASE_TRANSITION,
                0
            );
        }
        state[expected_index] = LAUNCHER_SERVICE_STARTING;
        result = micros_runtime_receive(MICROS_ENDPOINT_ANY, &message);
        if (result != MICROS_SYSCALL_ABI_OK) {
            fail(
                service_id,
                MICROS_ENDPOINT_NONE,
                MICROS_BOOTSTRAP_FAILURE_RELEASE_TRANSITION,
                0
            );
        }
        detail = message_shape_detail(&message);
        source_config_index = find_config_endpoint(message.source);
        source_manifest_index = source_config_index < 0
            ? -1
            : find_manifest_service(
                manifest,
                micros_bootstrap_service_config.services[
                    source_config_index
                ].service_id
            );
        if (detail != 0) {
            fail(
                source_manifest_index < 0
                    ? service_id
                    : manifest->entries[source_manifest_index].service_id,
                message.source,
                MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED,
                detail
            );
        }
        if (source_manifest_index < 0) {
            fail(
                service_id,
                message.source,
                MICROS_BOOTSTRAP_FAILURE_READY_FOREIGN,
                0
            );
        }
        if (
            state[source_manifest_index]
                == LAUNCHER_SERVICE_STARTING
            && manifest->entries[source_manifest_index].service_id
                == service_id
        ) {
            if (read_u32_le(&message.payload[4]) != service_id) {
                fail(
                    service_id,
                    message.source,
                    MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED,
                    4
                );
            }
            if (
                read_u32_le(&message.payload[16])
                != message.source
            ) {
                fail(
                    service_id,
                    message.source,
                    MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED,
                    7
                );
            }
        } else if (
            state[source_manifest_index]
                == LAUNCHER_SERVICE_READY
        ) {
            fail(
                manifest->entries[source_manifest_index].service_id,
                message.source,
                MICROS_BOOTSTRAP_FAILURE_READY_DUPLICATE,
                0
            );
        } else if (
            state[source_manifest_index]
                == LAUNCHER_SERVICE_PREPARED
        ) {
            fail(
                manifest->entries[source_manifest_index].service_id,
                message.source,
                MICROS_BOOTSTRAP_FAILURE_READY_EARLY,
                0
            );
        } else {
            fail(
                service_id,
                message.source,
                MICROS_BOOTSTRAP_FAILURE_READY_FOREIGN,
                0
            );
        }
        if (
            bootstrap_control(
                MICROS_BOOTSTRAP_COMMAND_ACCEPT_READY,
                service_id,
                message.source,
                message.reply_token,
                0
            ) != MICROS_SYSCALL_ABI_OK
        ) {
            fail(
                service_id,
                    MICROS_ENDPOINT_NONE,
                MICROS_BOOTSTRAP_FAILURE_RELEASE_TRANSITION,
                0
            );
        }
        state[expected_index] = LAUNCHER_SERVICE_READY;
    }
    (void)bootstrap_control(
        MICROS_BOOTSTRAP_COMMAND_COMPLETE,
        0,
        0,
        0,
        0
    );
    fail(
        0,
        MICROS_ENDPOINT_NONE,
        MICROS_BOOTSTRAP_FAILURE_COMPLETION,
        0
    );
}
