#include "servers/pm/pm_service_internal.h"

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

struct pm_self_test {
    struct micros_pm_reservation_result reservation;
    struct micros_pm_prepared_result first_prepared;
    struct micros_pm_prepared_result prepared;
    struct micros_pm_prepared_result preserved_prepared;
    struct micros_pm_exit_begin_result exit_begin;
    struct micros_pm_exit_begin_result preserved_exit;
    struct micros_ipc_message child_request;
    uint64_t load_token;
    uint64_t mapping_generation;
};

static void initialize_self_test_outputs(struct pm_self_test *scenario)
{
    scenario->load_token = 0;
    scenario->mapping_generation = 0;
    pm_service_clear_bytes(&scenario->reservation, sizeof(scenario->reservation));
    pm_service_clear_bytes(&scenario->first_prepared, sizeof(scenario->first_prepared));
    pm_service_clear_bytes(&scenario->prepared, sizeof(scenario->prepared));
    pm_service_fill_bytes(
        &scenario->preserved_prepared,
        sizeof(scenario->preserved_prepared),
        UINT8_C(0xa5)
    );
    pm_service_clear_bytes(&scenario->exit_begin, sizeof(scenario->exit_begin));
    pm_service_fill_bytes(
        &scenario->preserved_exit,
        sizeof(scenario->preserved_exit),
        UINT8_C(0x5a)
    );
    pm_service_clear_bytes(&scenario->child_request, sizeof(scenario->child_request));
}

static bool reserve_and_load_child(struct pm_self_test *scenario)
{
    return !(
        micros_pm_control_reserve(&scenario->reservation)
            != MICROS_SYSCALL_ABI_OK
        || scenario->reservation.version != MICROS_PM_RESERVATION_VERSION
        || scenario->reservation.size != MICROS_PM_RESERVATION_SIZE
        || scenario->reservation.transaction == 0
        || scenario->reservation.reserved[0] != 0
        || scenario->reservation.reserved[1] != 0
        || !pm_service_vm_load(
            scenario->reservation.transaction,
            &scenario->load_token,
            &scenario->mapping_generation
        )
    );
}

static bool prepare_abort_and_reprepare_child(struct pm_self_test *scenario)
{
    return !(
        micros_pm_control_prepare_execution(
            scenario->reservation.transaction + 1,
            scenario->load_token,
            scenario->mapping_generation,
            &scenario->preserved_prepared
        ) != MICROS_SYSCALL_ABI_STATE
        || !pm_service_bytes_have_value(
            &scenario->preserved_prepared,
            sizeof(scenario->preserved_prepared),
            UINT8_C(0xa5)
        )
        || micros_pm_control_prepare_execution(
            scenario->reservation.transaction,
            scenario->load_token,
            scenario->mapping_generation,
            &scenario->first_prepared
        ) != MICROS_SYSCALL_ABI_OK
        || scenario->first_prepared.version != MICROS_PM_PREPARED_VERSION
        || scenario->first_prepared.size != MICROS_PM_PREPARED_SIZE
        || scenario->first_prepared.prepared_token == 0
        || scenario->first_prepared.endpoint == MICROS_ENDPOINT_NONE
        || scenario->first_prepared.endpoint == MICROS_ENDPOINT_ANY
        || scenario->first_prepared.reserved0 != 0
        || scenario->first_prepared.reserved1 != 0
        || micros_pm_control_abort_prepared(
            scenario->reservation.transaction,
            scenario->load_token,
            scenario->mapping_generation,
            scenario->first_prepared.prepared_token + 1
        ) != MICROS_SYSCALL_ABI_STATE
        || micros_pm_control_abort_prepared(
            scenario->reservation.transaction,
            scenario->load_token,
            scenario->mapping_generation,
            scenario->first_prepared.prepared_token
        ) != MICROS_SYSCALL_ABI_OK
        || micros_pm_control_prepare_execution(
            scenario->reservation.transaction,
            scenario->load_token,
            scenario->mapping_generation,
            &scenario->prepared
        ) != MICROS_SYSCALL_ABI_OK
        || scenario->prepared.version != MICROS_PM_PREPARED_VERSION
        || scenario->prepared.size != MICROS_PM_PREPARED_SIZE
        || scenario->prepared.prepared_token
            <= scenario->first_prepared.prepared_token
        || scenario->prepared.endpoint == MICROS_ENDPOINT_NONE
        || scenario->prepared.endpoint == MICROS_ENDPOINT_ANY
        || scenario->prepared.reserved0 != 0
        || scenario->prepared.reserved1 != 0
    );
}

static bool activate_commit_and_capture_child_exit(struct pm_self_test *scenario)
{
    return !(
        micros_pm_control_activate(
            scenario->reservation.transaction,
            scenario->load_token,
            scenario->mapping_generation,
            scenario->prepared.prepared_token + 1
        ) != MICROS_SYSCALL_ABI_STATE
        || micros_pm_control_activate(
            scenario->reservation.transaction,
            scenario->load_token,
            scenario->mapping_generation,
            scenario->prepared.prepared_token
        ) != MICROS_SYSCALL_ABI_OK
        || !pm_service_vm_commit(scenario->load_token, scenario->mapping_generation)
        || micros_runtime_receive(
            scenario->prepared.endpoint,
            &scenario->child_request
        ) != MICROS_SYSCALL_ABI_OK
        || scenario->child_request.source != scenario->prepared.endpoint
        || scenario->child_request.type != MICROS_PM_MESSAGE_EXIT
        || scenario->child_request.reply_token == 0
        || !pm_service_bytes_are_zero(
            scenario->child_request.payload,
            sizeof(scenario->child_request.payload)
        )
    );
}

static bool begin_child_exit_with_preservation(struct pm_self_test *scenario)
{
    return !(
        micros_pm_control_exit_begin(
            scenario->prepared.endpoint
                + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS),
            &scenario->preserved_exit
        ) != MICROS_SYSCALL_ABI_STATE
        || !pm_service_bytes_have_value(
            &scenario->preserved_exit,
            sizeof(scenario->preserved_exit),
            UINT8_C(0x5a)
        )
        || micros_pm_control_exit_begin(
            scenario->prepared.endpoint,
            &scenario->exit_begin
        ) != MICROS_SYSCALL_ABI_OK
        || scenario->exit_begin.version != MICROS_PM_EXIT_BEGIN_VERSION
        || scenario->exit_begin.size != MICROS_PM_EXIT_BEGIN_SIZE
        || scenario->exit_begin.exit_token == 0
        || scenario->exit_begin.reserved[0] != 0
        || scenario->exit_begin.reserved[1] != 0
    );
}

static bool detach_release_and_validate_child(struct pm_service_context *context, struct pm_self_test *scenario)
{
    return !(
        micros_pm_control_exit_detach(
            scenario->exit_begin.exit_token + 1,
            scenario->prepared.endpoint
        ) != MICROS_SYSCALL_ABI_STATE
        || micros_pm_control_exit_detach(
            scenario->exit_begin.exit_token,
            scenario->prepared.endpoint
        ) != MICROS_SYSCALL_ABI_OK
        || micros_pm_control_release_exited_process(
            scenario->exit_begin.exit_token
        ) != MICROS_SYSCALL_ABI_STATE
        || !pm_service_vm_exit_release(
            scenario->exit_begin.exit_token,
            scenario->load_token,
            scenario->mapping_generation
        )
        || micros_pm_control_release_exited_process(
            scenario->exit_begin.exit_token
        ) != MICROS_SYSCALL_ABI_OK
        || micros_pm_control_abort(scenario->reservation.transaction)
            != MICROS_SYSCALL_ABI_STATE
        || micros_pm_table_validate(&context->process_table)
            != MICROS_PM_MODEL_OK
    );
}

_Noreturn void pm_service_run_self_test(struct pm_service_context *context)
{
    struct pm_self_test scenario;

    initialize_self_test_outputs(&scenario);
    if (
        !reserve_and_load_child(&scenario)
        || !prepare_abort_and_reprepare_child(&scenario)
        || !activate_commit_and_capture_child_exit(&scenario)
        || !begin_child_exit_with_preservation(&scenario)
        || !detach_release_and_validate_child(context, &scenario)
    ) {
        __builtin_trap();
    }
    micros_pm_service_report(
        MICROS_PM_TEST_REPORT_MAGIC, scenario.reservation.transaction
    );
}

#endif
