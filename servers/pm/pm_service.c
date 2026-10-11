#include "micros/runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap_control.h"
#include "micros/endpoint.h"
#include "micros/pm.h"
#include "servers/pm/pm_control.h"
#include "servers/pm/pm_core.h"
#ifdef MICROS_PM_SERVICE_TEST
#include "tests/qemu/pm_service_protocol.h"
#endif
#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST
#include "tests/qemu/vm_scratch_preemption_protocol.h"

void micros_vm_scratch_preemption_peer_hold(uint64_t magic);
void micros_vm_scratch_preemption_peer_run(uint64_t magic);
#endif

volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;

enum {
    MICROS_PM_SERVICE_ID = 3,
};

static struct micros_pm_table process_table;
static volatile uint64_t pm_service_data =
    UINT64_C(0x504d534552564943);
#ifdef MICROS_PM_SERVICE_TEST
static struct micros_pm_table initial_table;

_Noreturn void micros_pm_service_report(
    uint64_t magic,
    uint64_t transaction
);
#endif

static void pm_service_clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

#ifdef MICROS_PM_SERVICE_TEST
static void pm_service_copy_bytes(
    void *destination,
    const void *source,
    size_t size
)
{
    uint8_t *destination_bytes = destination;
    const uint8_t *source_bytes = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

static bool pm_service_bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
    const uint8_t *left_bytes = left;
    const uint8_t *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

static void pm_service_fill_bytes(
    void *storage,
    size_t size,
    uint8_t value
)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = value;
    }
}

static bool pm_service_bytes_have_value(
    const void *storage,
    size_t size,
    uint8_t value
)
{
    const uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != value) {
            return false;
        }
    }
    return true;
}

#endif

static bool pm_service_bytes_are_zero(
    const uint8_t *bytes,
    size_t size
)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static void pm_service_write_u32_le(
    uint8_t *bytes,
    uint32_t value
)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

#if defined(MICROS_VM_SCRATCH_PREEMPTION_TEST) \
    || defined(MICROS_PM_SERVICE_TEST)
static void pm_service_write_u64_le(
    uint8_t *bytes,
    uint64_t value
)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}
#endif

#ifdef MICROS_PM_SERVICE_TEST
static uint64_t pm_service_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}
#endif

static uint32_t pm_service_read_u32_le(const uint8_t *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static bool validate_configuration(void)
{
    size_t index;

    if (
        micros_bootstrap_service_config.version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.manifest_version
            != MICROS_BOOTSTRAP_MANIFEST_VERSION
        || micros_bootstrap_service_config.service_id
            != MICROS_PM_SERVICE_ID
        || micros_bootstrap_service_config.service_count < 3
        || micros_bootstrap_service_config.service_count
            > MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || micros_bootstrap_service_config.self_endpoint
            != micros_bootstrap_service_config.services[2].endpoint
        || micros_bootstrap_service_config.launcher_endpoint
            != micros_bootstrap_service_config.services[0].endpoint
        || micros_bootstrap_service_config.manifest_view_address != 0
    ) {
        return false;
    }
#ifdef MICROS_PM_SERVICE_TEST
    if (
        micros_bootstrap_service_config.service_count
            != MICROS_PM_TEST_SERVICE_COUNT
    ) {
        return false;
    }
#endif
#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST
    if (
        micros_bootstrap_service_config.service_count
            != MICROS_VM_SCRATCH_PREEMPTION_SERVICE_COUNT
    ) {
        return false;
    }
#endif
    for (
        index = 0;
        index < micros_bootstrap_service_config.service_count;
        ++index
    ) {
        micros_endpoint_t endpoint =
            micros_bootstrap_service_config.services[index].endpoint;

        if (
            micros_bootstrap_service_config.services[index].service_id
                != index + 1
            || endpoint == MICROS_ENDPOINT_NONE
            || endpoint == MICROS_ENDPOINT_ANY
        ) {
            return false;
        }
    }
    for (
        index = 0;
        index < sizeof(micros_bootstrap_service_config.reserved)
                / sizeof(micros_bootstrap_service_config.reserved[0]);
        ++index
    ) {
        if (micros_bootstrap_service_config.reserved[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool send_ready(void)
{
    struct micros_ipc_message message;

    pm_service_clear_bytes(&message, sizeof(message));
    message.type = MICROS_BOOTSTRAP_MESSAGE_READY;
    pm_service_write_u32_le(
        &message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    pm_service_write_u32_le(
        &message.payload[4],
        micros_bootstrap_service_config.service_id
    );
    pm_service_write_u32_le(
        &message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    pm_service_write_u32_le(
        &message.payload[16],
        micros_bootstrap_service_config.self_endpoint
    );
    if (
        micros_runtime_call(
            micros_bootstrap_service_config.launcher_endpoint,
            &message
        ) != MICROS_SYSCALL_ABI_OK
    ) {
        return false;
    }
    return (
        message.source
            == micros_bootstrap_service_config.launcher_endpoint
        && message.type == MICROS_BOOTSTRAP_MESSAGE_READY_ACK
        && message.reply_token == 0
        && pm_service_read_u32_le(&message.payload[0])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && pm_service_read_u32_le(&message.payload[4])
            == micros_bootstrap_service_config.service_id
        && pm_service_read_u32_le(&message.payload[8])
            == MICROS_BOOTSTRAP_MANIFEST_VERSION
        && pm_service_read_u32_le(&message.payload[12]) == 0
        && pm_service_read_u32_le(&message.payload[16])
            == micros_bootstrap_service_config.self_endpoint
        && pm_service_bytes_are_zero(
            &message.payload[20],
            sizeof(message.payload) - 20
        )
    );
}

#ifndef MICROS_VM_SCRATCH_PREEMPTION_TEST
static enum micros_pm_result protocol_result(
    const struct micros_ipc_message *message
)
{
    struct micros_pm_request request;
    struct micros_pm_process_handle caller;
    uint64_t caller_pid;
    enum micros_pm_protocol_status status =
        micros_pm_decode_request(message, &request);
    enum micros_pm_model_error error;

    switch (status) {
    case MICROS_PM_PROTOCOL_BAD_TYPE:
        return MICROS_PM_RESULT_BAD_TYPE;
    case MICROS_PM_PROTOCOL_BAD_VERSION:
        return MICROS_PM_RESULT_BAD_VERSION;
    case MICROS_PM_PROTOCOL_MALFORMED:
        return MICROS_PM_RESULT_MALFORMED;
    case MICROS_PM_PROTOCOL_INVARIANT:
        __builtin_trap();
    case MICROS_PM_PROTOCOL_OK:
        break;
    }
    error = micros_pm_find_running_by_endpoint(
        &process_table,
        request.source,
        &caller,
        &caller_pid
    );
    if (error == MICROS_PM_MODEL_ERROR_STALE) {
        return MICROS_PM_RESULT_CALLER;
    }
    if (error != MICROS_PM_MODEL_OK) {
        __builtin_trap();
    }
    (void)caller;
    (void)caller_pid;
    return MICROS_PM_RESULT_STATE;
}

static void serve_request(
    const struct micros_ipc_message *request,
    struct micros_ipc_message *reply
)
{
    enum micros_pm_result result;

#ifdef MICROS_PM_SERVICE_TEST
    if (
        !pm_service_bytes_equal(
            &process_table,
            &initial_table,
            sizeof(process_table)
        )
    ) {
        __builtin_trap();
    }
#endif
    result = protocol_result(request);
    if (
        micros_pm_build_result(
            request->type,
            result,
            0,
            MICROS_PM_EXIT_NONE,
            0,
            reply
        ) != MICROS_PM_MODEL_OK
        || micros_pm_table_validate(&process_table)
            != MICROS_PM_MODEL_OK
    ) {
        __builtin_trap();
    }
#ifdef MICROS_PM_SERVICE_TEST
    if (
        !pm_service_bytes_equal(
            &process_table,
            &initial_table,
            sizeof(process_table)
        )
    ) {
        __builtin_trap();
    }
#endif
}
#endif

#ifdef MICROS_PM_SERVICE_TEST
static bool pm_service_vm_load(
    uint64_t transaction,
    uint64_t *load_token,
    uint64_t *mapping_generation
)
{
    struct micros_ipc_message message;
    micros_endpoint_t vm_endpoint =
        micros_bootstrap_service_config.services[
            MICROS_PM_TEST_VM_SERVICE_ID - 1
        ].endpoint;

    if (
        transaction == 0
        || load_token == NULL
        || mapping_generation == NULL
    ) {
        return false;
    }
    pm_service_clear_bytes(&message, sizeof(message));
    message.type = MICROS_PM_TEST_VM_LOAD;
    pm_service_write_u32_le(
        &message.payload[0],
        MICROS_PM_TEST_PROTOCOL_VERSION
    );
    pm_service_write_u64_le(&message.payload[8], transaction);
    if (
        micros_runtime_call(vm_endpoint, &message)
            != MICROS_SYSCALL_ABI_OK
        || message.source != vm_endpoint
        || message.type != MICROS_PM_TEST_VM_LOADED
        || message.reply_token != 0
        || pm_service_read_u32_le(&message.payload[0])
            != MICROS_PM_TEST_PROTOCOL_VERSION
        || pm_service_read_u64_le(&message.payload[8])
            != transaction
        || pm_service_read_u32_le(&message.payload[32])
            != MICROS_PM_TEST_RESERVED_PROCESS_SLOT
        || pm_service_read_u32_le(&message.payload[36]) != 1
    ) {
        return false;
    }
    *load_token = pm_service_read_u64_le(&message.payload[16]);
    *mapping_generation =
        pm_service_read_u64_le(&message.payload[24]);
    return *load_token != 0 && *mapping_generation != 0;
}

static bool pm_service_vm_commit(
    uint64_t load_token,
    uint64_t mapping_generation
)
{
    struct micros_ipc_message message;
    micros_endpoint_t vm_endpoint =
        micros_bootstrap_service_config.services[
            MICROS_PM_TEST_VM_SERVICE_ID - 1
        ].endpoint;

    pm_service_clear_bytes(&message, sizeof(message));
    message.type = MICROS_PM_TEST_VM_COMMIT;
    pm_service_write_u32_le(
        &message.payload[0],
        MICROS_PM_TEST_PROTOCOL_VERSION
    );
    pm_service_write_u64_le(&message.payload[8], load_token);
    pm_service_write_u64_le(
        &message.payload[16],
        mapping_generation
    );
    if (
        micros_runtime_call(vm_endpoint, &message)
            != MICROS_SYSCALL_ABI_OK
        || message.source != vm_endpoint
        || message.type != MICROS_PM_TEST_VM_COMMITTED
        || message.reply_token != 0
        || pm_service_read_u32_le(&message.payload[0])
            != MICROS_PM_TEST_PROTOCOL_VERSION
        || pm_service_read_u64_le(&message.payload[8])
            != load_token
        || pm_service_read_u64_le(&message.payload[16])
            != mapping_generation
        || !pm_service_bytes_are_zero(
            &message.payload[24],
            sizeof(message.payload) - 24
        )
    ) {
        return false;
    }
    return true;
}

static bool pm_service_vm_exit_release(
    uint64_t exit_token,
    uint64_t load_token,
    uint64_t mapping_generation
)
{
    struct micros_ipc_message message;
    micros_endpoint_t vm_endpoint =
        micros_bootstrap_service_config.services[
            MICROS_PM_TEST_VM_SERVICE_ID - 1
        ].endpoint;

    pm_service_clear_bytes(&message, sizeof(message));
    message.type = MICROS_PM_TEST_VM_EXIT_RELEASE;
    pm_service_write_u32_le(
        &message.payload[0],
        MICROS_PM_TEST_PROTOCOL_VERSION
    );
    pm_service_write_u64_le(&message.payload[8], exit_token);
    pm_service_write_u64_le(&message.payload[16], load_token);
    pm_service_write_u64_le(
        &message.payload[24],
        mapping_generation
    );
    if (
        micros_runtime_call(vm_endpoint, &message)
            != MICROS_SYSCALL_ABI_OK
        || message.source != vm_endpoint
        || message.type != MICROS_PM_TEST_VM_EXIT_RELEASED
        || message.reply_token != 0
        || pm_service_read_u32_le(&message.payload[0])
            != MICROS_PM_TEST_PROTOCOL_VERSION
        || pm_service_read_u64_le(&message.payload[8])
            != exit_token
        || !pm_service_bytes_are_zero(
            &message.payload[16],
            sizeof(message.payload) - 16
        )
    ) {
        return false;
    }
    return true;
}

static _Noreturn void run_self_test(void)
{
    struct micros_pm_reservation_result reservation;
    struct micros_pm_prepared_result first_prepared;
    struct micros_pm_prepared_result prepared;
    struct micros_pm_prepared_result preserved_prepared;
    struct micros_pm_exit_begin_result exit_begin;
    struct micros_pm_exit_begin_result preserved_exit;
    struct micros_ipc_message child_request;
    uint64_t load_token = 0;
    uint64_t mapping_generation = 0;

    pm_service_clear_bytes(&reservation, sizeof(reservation));
    pm_service_clear_bytes(&first_prepared, sizeof(first_prepared));
    pm_service_clear_bytes(&prepared, sizeof(prepared));
    pm_service_fill_bytes(
        &preserved_prepared,
        sizeof(preserved_prepared),
        UINT8_C(0xa5)
    );
    pm_service_clear_bytes(&exit_begin, sizeof(exit_begin));
    pm_service_fill_bytes(
        &preserved_exit,
        sizeof(preserved_exit),
        UINT8_C(0x5a)
    );
    pm_service_clear_bytes(&child_request, sizeof(child_request));
    if (
        micros_pm_control_reserve(&reservation)
            != MICROS_SYSCALL_ABI_OK
        || reservation.version != MICROS_PM_RESERVATION_VERSION
        || reservation.size != MICROS_PM_RESERVATION_SIZE
        || reservation.transaction == 0
        || reservation.reserved[0] != 0
        || reservation.reserved[1] != 0
        || !pm_service_vm_load(
            reservation.transaction,
            &load_token,
            &mapping_generation
        )
        || micros_pm_control_prepare_execution(
            reservation.transaction + 1,
            load_token,
            mapping_generation,
            &preserved_prepared
        ) != MICROS_SYSCALL_ABI_STATE
        || !pm_service_bytes_have_value(
            &preserved_prepared,
            sizeof(preserved_prepared),
            UINT8_C(0xa5)
        )
        || micros_pm_control_prepare_execution(
            reservation.transaction,
            load_token,
            mapping_generation,
            &first_prepared
        ) != MICROS_SYSCALL_ABI_OK
        || first_prepared.version != MICROS_PM_PREPARED_VERSION
        || first_prepared.size != MICROS_PM_PREPARED_SIZE
        || first_prepared.prepared_token == 0
        || first_prepared.endpoint == MICROS_ENDPOINT_NONE
        || first_prepared.endpoint == MICROS_ENDPOINT_ANY
        || first_prepared.reserved0 != 0
        || first_prepared.reserved1 != 0
        || micros_pm_control_abort_prepared(
            reservation.transaction,
            load_token,
            mapping_generation,
            first_prepared.prepared_token + 1
        ) != MICROS_SYSCALL_ABI_STATE
        || micros_pm_control_abort_prepared(
            reservation.transaction,
            load_token,
            mapping_generation,
            first_prepared.prepared_token
        ) != MICROS_SYSCALL_ABI_OK
        || micros_pm_control_prepare_execution(
            reservation.transaction,
            load_token,
            mapping_generation,
            &prepared
        ) != MICROS_SYSCALL_ABI_OK
        || prepared.version != MICROS_PM_PREPARED_VERSION
        || prepared.size != MICROS_PM_PREPARED_SIZE
        || prepared.prepared_token
            <= first_prepared.prepared_token
        || prepared.endpoint == MICROS_ENDPOINT_NONE
        || prepared.endpoint == MICROS_ENDPOINT_ANY
        || prepared.reserved0 != 0
        || prepared.reserved1 != 0
        || micros_pm_control_activate(
            reservation.transaction,
            load_token,
            mapping_generation,
            prepared.prepared_token + 1
        ) != MICROS_SYSCALL_ABI_STATE
        || micros_pm_control_activate(
            reservation.transaction,
            load_token,
            mapping_generation,
            prepared.prepared_token
        ) != MICROS_SYSCALL_ABI_OK
        || !pm_service_vm_commit(load_token, mapping_generation)
        || micros_runtime_receive(
            prepared.endpoint,
            &child_request
        ) != MICROS_SYSCALL_ABI_OK
        || child_request.source != prepared.endpoint
        || child_request.type != MICROS_PM_MESSAGE_EXIT
        || child_request.reply_token == 0
        || !pm_service_bytes_are_zero(
            child_request.payload,
            sizeof(child_request.payload)
        )
        || micros_pm_control_exit_begin(
            prepared.endpoint
                + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS),
            &preserved_exit
        ) != MICROS_SYSCALL_ABI_STATE
        || !pm_service_bytes_have_value(
            &preserved_exit,
            sizeof(preserved_exit),
            UINT8_C(0x5a)
        )
        || micros_pm_control_exit_begin(
            prepared.endpoint,
            &exit_begin
        ) != MICROS_SYSCALL_ABI_OK
        || exit_begin.version != MICROS_PM_EXIT_BEGIN_VERSION
        || exit_begin.size != MICROS_PM_EXIT_BEGIN_SIZE
        || exit_begin.exit_token == 0
        || exit_begin.reserved[0] != 0
        || exit_begin.reserved[1] != 0
        || micros_pm_control_exit_detach(
            exit_begin.exit_token + 1,
            prepared.endpoint
        ) != MICROS_SYSCALL_ABI_STATE
        || micros_pm_control_exit_detach(
            exit_begin.exit_token,
            prepared.endpoint
        ) != MICROS_SYSCALL_ABI_OK
        || micros_pm_control_release_exited_process(
            exit_begin.exit_token
        ) != MICROS_SYSCALL_ABI_STATE
        || !pm_service_vm_exit_release(
            exit_begin.exit_token,
            load_token,
            mapping_generation
        )
        || micros_pm_control_release_exited_process(
            exit_begin.exit_token
        ) != MICROS_SYSCALL_ABI_OK
        || micros_pm_control_abort(reservation.transaction)
            != MICROS_SYSCALL_ABI_STATE
        || micros_pm_table_validate(&process_table)
            != MICROS_PM_MODEL_OK
    ) {
        __builtin_trap();
    }
    micros_pm_service_report(
        MICROS_PM_TEST_REPORT_MAGIC,
        reservation.transaction
    );
}
#endif

#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST
static _Noreturn void run_scratch_preemption_test(
    const struct micros_ipc_message *request
)
{
    struct micros_pm_reservation_result reservation;
    struct micros_ipc_message reply;
    struct micros_ipc_message blocked;
    micros_endpoint_t driver_endpoint =
        micros_bootstrap_service_config.services[
            MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID - 1
        ].endpoint;

    pm_service_clear_bytes(&reservation, sizeof(reservation));
    pm_service_clear_bytes(&reply, sizeof(reply));
    pm_service_clear_bytes(&blocked, sizeof(blocked));
    if (
        request == NULL
        || request->source != driver_endpoint
        || request->type != MICROS_VM_SCRATCH_PREEMPTION_RESERVE
        || request->reply_token == 0
        || pm_service_read_u32_le(&request->payload[0])
            != MICROS_VM_SCRATCH_PREEMPTION_VERSION
        || !pm_service_bytes_are_zero(
            &request->payload[4],
            sizeof(request->payload) - 4
        )
        || micros_pm_control_reserve(&reservation)
            != MICROS_SYSCALL_ABI_OK
        || reservation.version != MICROS_PM_RESERVATION_VERSION
        || reservation.size != MICROS_PM_RESERVATION_SIZE
        || reservation.transaction == 0
        || reservation.reserved[0] != 0
        || reservation.reserved[1] != 0
    ) {
        __builtin_trap();
    }
    reply.type = MICROS_VM_SCRATCH_PREEMPTION_RESERVATION;
    pm_service_write_u32_le(
        &reply.payload[0],
        MICROS_VM_SCRATCH_PREEMPTION_VERSION
    );
    pm_service_write_u64_le(
        &reply.payload[8],
        reservation.transaction
    );
    if (
        micros_runtime_reply(request->reply_token, &reply)
            != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    micros_vm_scratch_preemption_peer_hold(
        MICROS_VM_SCRATCH_PREEMPTION_PEER_HOLD_MAGIC
    );
    micros_vm_scratch_preemption_peer_run(
        MICROS_VM_SCRATCH_PREEMPTION_PEER_RUN_MAGIC
    );
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &blocked)
            != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    __builtin_trap();
}
#endif

void micros_service_main(void)
{
    struct micros_ipc_message message;

    if (
        pm_service_data != UINT64_C(0x504d534552564943)
        || !validate_configuration()
        || micros_pm_table_initialize(&process_table)
            != MICROS_PM_MODEL_OK
        || micros_pm_table_validate(&process_table)
            != MICROS_PM_MODEL_OK
    ) {
        __builtin_trap();
    }
#ifdef MICROS_PM_SERVICE_TEST
    pm_service_copy_bytes(
        &initial_table,
        &process_table,
        sizeof(process_table)
    );
#endif
    if (!send_ready()) {
        __builtin_trap();
    }
    pm_service_clear_bytes(&message, sizeof(message));
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &message)
            != MICROS_SYSCALL_ABI_OK
    ) {
        __builtin_trap();
    }
    for (;;) {
        if (message.type == MICROS_IPC_TYPE_KERNEL_NOTIFICATION) {
#ifdef MICROS_PM_SERVICE_TEST
            if (
                !pm_service_bytes_equal(
                    &process_table,
                    &initial_table,
                    sizeof(process_table)
                )
            ) {
                __builtin_trap();
            }
#endif
            if (
                micros_pm_enable_runtime(&process_table, &message)
                    != MICROS_PM_MODEL_OK
            ) {
                __builtin_trap();
            }
#ifdef MICROS_PM_SERVICE_TEST
            run_self_test();
#endif
            pm_service_clear_bytes(&message, sizeof(message));
            if (
                micros_runtime_receive(
                    MICROS_ENDPOINT_ANY,
                    &message
                ) != MICROS_SYSCALL_ABI_OK
            ) {
                __builtin_trap();
            }
        } else {
#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST
            run_scratch_preemption_test(&message);
#else
            struct micros_ipc_message reply;
            uint64_t reply_token = message.reply_token;

            serve_request(&message, &reply);
            pm_service_clear_bytes(&message, sizeof(message));
            if (
                micros_runtime_reply_receive(
                    reply_token,
                    &reply,
                    MICROS_ENDPOINT_ANY,
                    &message
                ) != MICROS_SYSCALL_ABI_OK
            ) {
                __builtin_trap();
            }
#endif
        }
    }
}
