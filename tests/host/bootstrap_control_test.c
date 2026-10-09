#include "micros/bootstrap_control.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "kernel/bootstrap_control_internal.h"
#include "kernel/bootstrap_image.h"
#include "kernel/endpoint_internal.h"
#include "micros/ipc_core.h"
#include "micros/scheduler_core.h"
#include "micros/sv39.h"
#include "micros/vm_bootstrap.h"

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            return false; \
        } \
    } while (false)

static bool decode_preserves_output_on_error(
    struct micros_syscall_arguments arguments
)
{
    struct micros_bootstrap_control_request request;
    struct micros_bootstrap_control_request sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    request = sentinel;
    return (
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
}

static bool failure_detail_result(
    struct micros_syscall_arguments arguments,
    enum micros_syscall_abi_result expected
)
{
    struct micros_bootstrap_control_request request;

    return (
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && micros_bootstrap_control_validate_failure_detail(
            &request
        ) == expected
    );
}

static bool test_valid_commands(void)
{
    struct micros_bootstrap_control_request request;
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_BOOTSTRAP_COMMAND_RELEASE,
        .a1 = 2,
        .a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL,
    };

    EXPECT_TRUE(
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_BOOTSTRAP_COMMAND_RELEASE
        && request.service_id == 2
        && request.endpoint == 0
        && request.reply_token == 0
    );
    arguments.a0 = MICROS_BOOTSTRAP_COMMAND_ACCEPT_READY;
    arguments.a2 = UINT32_C(0x12345678);
    arguments.a3 = UINT64_C(0xfedcba9876543210);
    EXPECT_TRUE(
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command
            == MICROS_BOOTSTRAP_COMMAND_ACCEPT_READY
        && request.service_id == 2
        && request.endpoint == UINT32_C(0x12345678)
        && request.reply_token
            == UINT64_C(0xfedcba9876543210)
    );
    arguments.a0 = MICROS_BOOTSTRAP_COMMAND_FAIL;
    arguments.a1 = 0;
    arguments.a2 = MICROS_ENDPOINT_NONE;
    arguments.a3 = MICROS_BOOTSTRAP_FAILURE_AUTHORITY;
    EXPECT_TRUE(
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_BOOTSTRAP_COMMAND_FAIL
        && request.service_id == 0
        && request.endpoint == MICROS_ENDPOINT_NONE
        && request.failure_reason
            == MICROS_BOOTSTRAP_FAILURE_AUTHORITY
        && request.failure_detail == 0
        && micros_bootstrap_control_validate_failure_detail(
            &request
        ) == MICROS_SYSCALL_ABI_OK
    );
    arguments.a1 = 2;
    arguments.a2 = UINT32_C(0x12345678);
    arguments.a3 = MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED;
    arguments.a4 = 7;
    EXPECT_TRUE(
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.failure_detail == 7
        && micros_bootstrap_control_validate_failure_detail(
            &request
        ) == MICROS_SYSCALL_ABI_OK
    );
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_BOOTSTRAP_COMMAND_COMPLETE,
        .a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL,
    };
    EXPECT_TRUE(
        micros_bootstrap_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_BOOTSTRAP_COMMAND_COMPLETE
    );
    return true;
}

static bool test_malformed_commands(void)
{
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_BOOTSTRAP_COMMAND_RELEASE,
        .a1 = 2,
        .a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL,
    };

    EXPECT_TRUE(
        decode_preserves_output_on_error(
            (struct micros_syscall_arguments){0}
        )
    );
    arguments.a0 = UINT64_C(1) << 32;
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    arguments.a0 = MICROS_BOOTSTRAP_COMMAND_RELEASE;
    arguments.a1 = UINT64_C(1) << 32;
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    arguments.a1 = 2;
    arguments.a2 = UINT64_C(1) << 32;
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    arguments.a2 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    arguments.a2 = 0;
    arguments.a3 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    arguments.a3 = 0;
    arguments.a4 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    arguments.a4 = 0;
    arguments.a5 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    arguments.a5 = 0;
    arguments.a6 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    arguments.a6 = 0;
    arguments.a7 = 10;
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));

    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_BOOTSTRAP_COMMAND_FAIL,
        .a2 = MICROS_ENDPOINT_NONE,
        .a3 = UINT64_C(1) << 32,
        .a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL,
    };
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    arguments.a3 = MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED;
    arguments.a4 = UINT64_C(1) << 32;
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    arguments.a3 = 0;
    arguments.a4 = 0;
    EXPECT_TRUE(
        failure_detail_result(
            arguments,
            MICROS_SYSCALL_ABI_ARGUMENT
        )
    );
    arguments.a3 = MICROS_BOOTSTRAP_FAILURE_READY_ROLE_GATE + 1;
    EXPECT_TRUE(
        failure_detail_result(
            arguments,
            MICROS_SYSCALL_ABI_ARGUMENT
        )
    );
    arguments.a3 = MICROS_BOOTSTRAP_FAILURE_READY_MALFORMED;
    for (arguments.a4 = 1; arguments.a4 <= 9; ++arguments.a4) {
        EXPECT_TRUE(
            failure_detail_result(
                arguments,
                MICROS_SYSCALL_ABI_OK
            )
        );
    }
    arguments.a4 = 0;
    EXPECT_TRUE(
        failure_detail_result(
            arguments,
            MICROS_SYSCALL_ABI_ARGUMENT
        )
    );
    arguments.a4 = 10;
    EXPECT_TRUE(
        failure_detail_result(
            arguments,
            MICROS_SYSCALL_ABI_ARGUMENT
        )
    );
    arguments.a3 = MICROS_BOOTSTRAP_FAILURE_AUTHORITY;
    arguments.a4 = 1;
    EXPECT_TRUE(
        failure_detail_result(
            arguments,
            MICROS_SYSCALL_ABI_ARGUMENT
        )
    );

    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_BOOTSTRAP_COMMAND_COMPLETE,
        .a1 = 1,
        .a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL,
    };
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    arguments.a1 = 0;
    arguments.a2 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    arguments.a2 = 0;
    arguments.a3 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    arguments.a3 = 0;
    arguments.a4 = 1;
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    arguments.a0 = 5;
    arguments.a4 = 0;
    EXPECT_TRUE(decode_preserves_output_on_error(arguments));
    return true;
}

static void set_name(
    char output[MICROS_BOOTSTRAP_NAME_SIZE],
    const char *name
)
{
    size_t length = strlen(name);

    memset(output, 0, MICROS_BOOTSTRAP_NAME_SIZE);
    memcpy(output, name, length);
}

static struct micros_privilege_profile profile(
    uint8_t id,
    const char *name
)
{
    struct micros_privilege_profile result;

    memset(&result, 0, sizeof(result));
    result.id = id;
    set_name(result.name, name);
    if (id == 1) {
        result.operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_REPLY;
        result.kernel_operations =
            MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL;
    } else {
        result.operations =
            MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL;
        result.call_targets = UINT32_C(1) << 1;
        if (id == MICROS_PRIVILEGE_PROFILE_PM) {
            result.operations |=
                MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE;
            result.kernel_operations =
                MICROS_KERNEL_OPERATION_PM_CONTROL;
        }
    }
    return result;
}

static void initialize_manifest(
    struct micros_bootstrap_manifest *manifest,
    bool include_pm
)
{
    struct micros_bootstrap_manifest_entry *launcher;
    struct micros_bootstrap_manifest_entry *pm;
    struct micros_bootstrap_manifest_entry *vm;

    memset(manifest, 0, sizeof(*manifest));
    manifest->header.magic = MICROS_BOOTSTRAP_MANIFEST_MAGIC;
    manifest->header.version = MICROS_BOOTSTRAP_MANIFEST_VERSION;
    manifest->header.header_size =
        MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE;
    manifest->header.entry_size =
        MICROS_BOOTSTRAP_MANIFEST_ENTRY_SIZE;
    manifest->header.entry_capacity =
        MICROS_BOOTSTRAP_SERVICE_CAPACITY;
    manifest->header.entry_count = include_pm ? 3 : 2;
    manifest->header.total_user_page_limit =
        include_pm ? 10 : 7;
    manifest->header.manifest_size = MICROS_BOOTSTRAP_MANIFEST_SIZE;

    vm = &manifest->entries[0];
    vm->service_id = 2;
    vm->image_id = 102;
    vm->process_slot = 1;
    vm->stack_page_count = 1;
    vm->profile_id = 2;
    set_name(vm->service_name, "vm");
    set_name(vm->profile_name, "VM");
    vm->prerequisites = UINT64_C(1);
    vm->ready_timeout_counter_ticks = 100;
    vm->user_page_limit = 3;
    vm->role_flags = MICROS_BOOTSTRAP_ROLE_VM;

    launcher = &manifest->entries[1];
    launcher->service_id = 1;
    launcher->image_id = 101;
    launcher->process_slot = 0;
    launcher->stack_page_count = 1;
    launcher->profile_id = 1;
    set_name(launcher->service_name, "bootstrap-launcher");
    set_name(launcher->profile_name, "BOOTSTRAP_LAUNCHER");
    launcher->user_page_limit = 4;
    launcher->role_flags = MICROS_BOOTSTRAP_ROLE_CONTROLLER;

    if (!include_pm) {
        return;
    }
    pm = &manifest->entries[2];
    pm->service_id = 3;
    pm->image_id = 103;
    pm->process_slot = 2;
    pm->stack_page_count = 1;
    pm->profile_id = MICROS_PRIVILEGE_PROFILE_PM;
    set_name(pm->service_name, "pm");
    set_name(pm->profile_name, "PM");
    pm->prerequisites = UINT64_C(1) << 1;
    pm->ready_timeout_counter_ticks = 100;
    pm->user_page_limit = 3;
    pm->role_flags = MICROS_BOOTSTRAP_ROLE_PM;
}

static struct micros_user_context context_pattern(uint64_t base)
{
    struct micros_user_context context;
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    for (
        index = 0;
        index < sizeof(context) / sizeof(uint64_t);
        ++index
    ) {
        words[index] = base + index;
    }
    memcpy(&context, words, sizeof(context));
    return context;
}

static bool setup_control_fixture(
    struct micros_bootstrap_control_state *state,
    struct micros_bootstrap_manifest *manifest,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle *hart,
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY],
    bool include_pm
)
{
    struct micros_bootstrap_manifest_plan plan = {
        .entry_count = include_pm ? 3 : 2,
        .total_user_page_limit = include_pm ? 10 : 7,
        .controller_service_id = 1,
        .vm_service_id = 2,
        .pm_service_id = include_pm ? 3 : 0,
        .ordered_service_ids = {1, 2, 3},
        .ordered_manifest_indices = {1, 0, 2},
    };
    struct micros_privilege_profile profiles[3];
    struct micros_process_handle processes[3];
    struct micros_thread_handle threads[3];
    micros_endpoint_t endpoints[3];
    size_t service_count = include_pm ? 3 : 2;
    size_t index;

    memset(state, 0, sizeof(*state));
    memset(registry, 0, sizeof(*registry));
    memset(objects, 0, sizeof(*objects));
    initialize_manifest(manifest, include_pm);
    profiles[0] = profile(1, "BOOTSTRAP_LAUNCHER");
    profiles[1] = profile(2, "VM");
    profiles[2] = profile(MICROS_PRIVILEGE_PROFILE_PM, "PM");
    if (
        micros_kernel_objects_initialize(objects, 1, 1)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(objects, 0, hart)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            objects,
            *hart,
            UINT64_C(0x01000000),
            UINT64_C(0x01004000),
            UINT64_C(0x02000000),
            UINT64_C(0x02001000)
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_endpoint_registry_initialize(
            registry,
            profiles,
            service_count
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    for (index = 0; index < service_count; ++index) {
        struct micros_user_context context =
            context_pattern(UINT64_C(0x1000) + index * 0x100);
        uintptr_t root = UINT64_C(0x10000000)
            + index * UINT64_C(0x1000);
        uintptr_t stack = UINT64_C(0x20000000)
            + index * UINT64_C(0x8000);

        if (
            micros_process_create(objects, &processes[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_process_attach_address_space(
                objects,
                processes[index],
                root
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                registry,
                objects,
                processes[index],
                &endpoints[index]
            ) != MICROS_ENDPOINT_OK
            || micros_thread_create(
                objects,
                processes[index],
                &threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_attach_execution_context(
                objects,
                threads[index],
                stack,
                stack + MICROS_THREAD_KERNEL_STACK_SIZE,
                &context
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    bindings[0] = (struct micros_bootstrap_binding){
        .manifest_index = 0,
        .service_id = 2,
        .process = processes[1],
        .thread = threads[1],
        .root = UINT64_C(0x10001000),
        .endpoint = endpoints[1],
        .prepared_page_count = 3,
        .scheduler_priority = 7,
        .scheduler_preemptible = true,
        .scheduler_quantum_counter_ticks = 100,
    };
    bindings[1] = (struct micros_bootstrap_binding){
        .manifest_index = 1,
        .service_id = 1,
        .process = processes[0],
        .thread = threads[0],
        .root = UINT64_C(0x10000000),
        .endpoint = endpoints[0],
        .prepared_page_count = 4,
        .scheduler_priority = 8,
        .scheduler_preemptible = true,
        .scheduler_quantum_counter_ticks = 100,
    };
    if (include_pm) {
        bindings[2] = (struct micros_bootstrap_binding){
            .manifest_index = 2,
            .service_id = 3,
            .process = processes[2],
            .thread = threads[2],
            .root = UINT64_C(0x10002000),
            .endpoint = endpoints[2],
            .prepared_page_count = 3,
            .scheduler_priority = 6,
            .scheduler_preemptible = true,
            .scheduler_quantum_counter_ticks = 100,
        };
    }
    return (
        micros_bootstrap_control_state_prepare(
            state,
            manifest,
            &plan,
            bindings,
            service_count
        ) == MICROS_BOOTSTRAP_OK
        && micros_bootstrap_control_validate(
            state,
            registry,
            objects
        ) == MICROS_BOOTSTRAP_OK
        && micros_endpoint_preflight_publish(
            registry,
            objects,
            bindings[1].endpoint,
            1
        ) == MICROS_ENDPOINT_OK
        && (
            micros_endpoint_commit_publish_prevalidated(
                registry,
                objects,
                bindings[1].endpoint,
                1
            ),
            true
        )
        && micros_thread_scheduler_admit(
            objects,
            *hart,
            bindings[1].thread,
            bindings[1].scheduler_priority,
            bindings[1].scheduler_quantum_counter_ticks,
            bindings[1].scheduler_preemptible
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_bootstrap_control_publish_controller(
            state,
            registry,
            objects
        ) == MICROS_BOOTSTRAP_OK
    );
}

static bool release_and_accept_ready(
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    const struct micros_bootstrap_binding *binding,
    uint64_t release_counter,
    uint64_t ready_counter
)
{
    struct micros_bootstrap_ready_plan plan;

    if (
        micros_bootstrap_control_release(
            state,
            registry,
            objects,
            hart,
            binding->service_id,
            release_counter,
            true
        ) != MICROS_BOOTSTRAP_OK
        || micros_bootstrap_control_prepare_ready(
            state,
            registry,
            objects,
            binding->service_id,
            binding->endpoint,
            ready_counter,
            true,
            &plan
        ) != MICROS_BOOTSTRAP_OK
    ) {
        return false;
    }
    micros_bootstrap_control_commit_ready_prevalidated(state, &plan);
    return true;
}

static bool setup_ready_pm_fixture(
    struct micros_bootstrap_control_state *state,
    struct micros_bootstrap_manifest *manifest,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle *hart,
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY]
)
{
    return (
        setup_control_fixture(
            state,
            manifest,
            registry,
            objects,
            hart,
            bindings,
            true
        )
        && release_and_accept_ready(
            state,
            registry,
            objects,
            *hart,
            &bindings[0],
            10,
            109
        )
        && release_and_accept_ready(
            state,
            registry,
            objects,
            *hart,
            &bindings[2],
            110,
            209
        )
        && micros_bootstrap_control_validate(
            state,
            registry,
            objects
        ) == MICROS_BOOTSTRAP_OK
    );
}

static bool commit_complete_for_test(
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    const struct micros_bootstrap_binding *controller,
    struct micros_bootstrap_complete_plan *plan
)
{
    if (
        micros_thread_scheduler_hold(
            objects,
            controller->thread
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_scheduler_remove(
            objects,
            controller->thread
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    micros_endpoint_commit_source_only_prevalidated(
        registry,
        controller->endpoint
    );
    micros_bootstrap_control_commit_complete_prevalidated(
        state,
        registry,
        objects,
        plan
    );
    return true;
}

static bool commit_current_complete_for_test(
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    const struct micros_bootstrap_binding *controller,
    struct micros_bootstrap_complete_plan *plan
)
{
    struct micros_scheduler_current_ipc_guard guard = {0};

    if (
        micros_scheduler_begin_current_ipc(
            objects,
            hart,
            &guard
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_scheduler_remove(
            objects,
            controller->thread
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    micros_endpoint_commit_source_only_prevalidated(
        registry,
        controller->endpoint
    );
    micros_bootstrap_control_commit_complete_prevalidated(
        state,
        registry,
        objects,
        plan
    );
    return true;
}

static uint32_t read_u32_le(const unsigned char *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

static void write_u32_le(unsigned char *bytes, uint32_t value)
{
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
    bytes[2] = (unsigned char)(value >> 16);
    bytes[3] = (unsigned char)(value >> 24);
}

static uint64_t read_u64_le(const unsigned char *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < sizeof(value); ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool sealed_notification_matches(
    const struct micros_ipc_message *message
)
{
    size_t index;

    if (
        message->source != MICROS_ENDPOINT_NONE
        || message->type != MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        || message->reply_token != 0
        || read_u64_le(message->payload)
            != MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED
    ) {
        return false;
    }
    for (
        index = sizeof(uint64_t);
        index < sizeof(message->payload);
        ++index
    ) {
        if (message->payload[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool test_control_transitions(void)
{
    struct micros_bootstrap_control_state state;
    struct micros_bootstrap_control_state state_snapshot;
    struct micros_bootstrap_manifest manifest;
    struct micros_endpoint_registry registry;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects;
    struct micros_kernel_objects objects_snapshot;
    struct micros_hart_handle hart;
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct micros_bootstrap_ready_plan ready_plan;
    struct micros_bootstrap_ready_plan ready_sentinel;
    struct micros_bootstrap_complete_plan complete_plan;
    const struct micros_endpoint_record *endpoint;

    EXPECT_TRUE(
        setup_control_fixture(
            &state,
            &manifest,
            &registry,
            &objects,
            &hart,
            bindings,
            false
        )
    );
    state_snapshot = state;
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_TRUE(
        micros_bootstrap_control_release(
            &state,
            &registry,
            &objects,
            hart,
            99,
            10,
            true
        ) == MICROS_BOOTSTRAP_ERROR_ARGUMENT
        && memcmp(&state, &state_snapshot, sizeof(state)) == 0
        && memcmp(
            &registry,
            &registry_snapshot,
            sizeof(registry)
        ) == 0
        && memcmp(&objects, &objects_snapshot, sizeof(objects)) == 0
    );
    EXPECT_TRUE(
        micros_bootstrap_control_release(
            &state,
            &registry,
            &objects,
            hart,
            2,
            10,
            true
        ) == MICROS_BOOTSTRAP_OK
        && state.transitions.starting_service_id == 2
        && state.transitions.entries[0].ready_deadline == 110
        && objects.processes[bindings[0].process.slot]
            .privilege_profile == 2
        && objects.threads[bindings[0].thread.slot]
            .scheduler_assigned
        && registry.endpoints[bindings[0].process.slot].state
            == MICROS_ENDPOINT_STATE_ACTIVE
    );

    memset(&ready_sentinel, 0xa5, sizeof(ready_sentinel));
    ready_plan = ready_sentinel;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_ready(
            &state,
            &registry,
            &objects,
            99,
            bindings[0].endpoint,
            109,
            true,
            &ready_plan
        ) == MICROS_BOOTSTRAP_ERROR_ARGUMENT
        && memcmp(
            &ready_plan,
            &ready_sentinel,
            sizeof(ready_plan)
        ) == 0
    );
    ready_plan = ready_sentinel;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_ready(
            &state,
            &registry,
            &objects,
            2,
            bindings[1].endpoint,
            109,
            true,
            &ready_plan
        ) == MICROS_BOOTSTRAP_ERROR_IDENTITY
        && memcmp(
            &ready_plan,
            &ready_sentinel,
            sizeof(ready_plan)
        ) == 0
    );
    ready_plan = ready_sentinel;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_ready(
            &state,
            &registry,
            &objects,
            2,
            bindings[1].endpoint,
            109,
            false,
            &ready_plan
        ) == MICROS_BOOTSTRAP_ERROR_ROLE
        && memcmp(
            &ready_plan,
            &ready_sentinel,
            sizeof(ready_plan)
        ) == 0
    );
    ready_plan = ready_sentinel;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_ready(
            &state,
            &registry,
            &objects,
            2,
            bindings[0].endpoint,
            110,
            true,
            &ready_plan
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(
            &ready_plan,
            &ready_sentinel,
            sizeof(ready_plan)
        ) == 0
    );
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_ready(
            &state,
            &registry,
            &objects,
            2,
            bindings[0].endpoint,
            109,
            true,
            &ready_plan
        ) == MICROS_BOOTSTRAP_OK
        && ready_plan.active
        && ready_plan.acknowledgment.source
            == bindings[1].endpoint
        && ready_plan.acknowledgment.type
            == MICROS_BOOTSTRAP_MESSAGE_READY_ACK
        && ready_plan.acknowledgment.reply_token == 0
        && read_u32_le(&ready_plan.acknowledgment.payload[0]) == 1
        && read_u32_le(&ready_plan.acknowledgment.payload[4]) == 2
        && read_u32_le(&ready_plan.acknowledgment.payload[8]) == 1
        && read_u32_le(&ready_plan.acknowledgment.payload[12]) == 0
        && read_u32_le(&ready_plan.acknowledgment.payload[16])
            == bindings[0].endpoint
    );
    micros_bootstrap_control_commit_ready_prevalidated(
        &state,
        &ready_plan
    );
    EXPECT_TRUE(
        state.transitions.entries[0].state
            == MICROS_BOOTSTRAP_SERVICE_READY
        && state.transitions.starting_service_id == 0
        && !ready_plan.active
    );
    ready_plan = ready_sentinel;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_ready(
            &state,
            &registry,
            &objects,
            2,
            bindings[1].endpoint,
            109,
            true,
            &ready_plan
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(
            &ready_plan,
            &ready_sentinel,
            sizeof(ready_plan)
        ) == 0
    );
    objects.threads[bindings[0].thread.slot].ipc_receive_buffer =
        UINT64_C(0x61002000);
    objects.threads[bindings[0].thread.slot].ipc_delivery_pending =
        true;
    objects.threads[bindings[0].thread.slot]
        .ipc_inbound_message.source = bindings[1].endpoint;
    objects.threads[bindings[0].thread.slot]
        .ipc_inbound_message.type = UINT32_C(60);
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &state,
            &registry,
            &objects,
            NULL,
            &complete_plan
        ) == MICROS_BOOTSTRAP_ERROR_STATE
    );
    memset(
        &objects.threads[bindings[0].thread.slot]
            .ipc_inbound_message,
        0,
        sizeof(
            objects.threads[bindings[0].thread.slot]
                .ipc_inbound_message
        )
    );
    objects.threads[bindings[0].thread.slot]
        .ipc_inbound_message.source = bindings[1].endpoint;
    objects.threads[bindings[0].thread.slot]
        .ipc_inbound_message.type =
            MICROS_BOOTSTRAP_MESSAGE_READY_ACK;
    write_u32_le(
        &objects.threads[bindings[0].thread.slot]
            .ipc_inbound_message.payload[0],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    write_u32_le(
        &objects.threads[bindings[0].thread.slot]
            .ipc_inbound_message.payload[4],
        2
    );
    write_u32_le(
        &objects.threads[bindings[0].thread.slot]
            .ipc_inbound_message.payload[8],
        MICROS_BOOTSTRAP_MANIFEST_VERSION
    );
    write_u32_le(
        &objects.threads[bindings[0].thread.slot]
            .ipc_inbound_message.payload[16],
        bindings[0].endpoint
    );
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &state,
            &registry,
            &objects,
            NULL,
            &complete_plan
        ) == MICROS_BOOTSTRAP_OK
        && complete_plan.active
        && !complete_plan.pm_notification.active
    );
    EXPECT_TRUE(
        commit_complete_for_test(
            &state,
            &registry,
            &objects,
            &bindings[1],
            &complete_plan
        )
    );
    EXPECT_TRUE(
        micros_bootstrap_control_validate(
            &state,
            &registry,
            &objects
        ) == MICROS_BOOTSTRAP_OK
        && state.phase == MICROS_BOOTSTRAP_PHASE_SEALED
        && state.controller_process.generation == 0
        && state.controller_thread.generation == 0
        && state.controller_endpoint == 0
        && micros_endpoint_resolve_internal(
            &registry,
            &objects,
            bindings[1].endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint->state == MICROS_ENDPOINT_STATE_SOURCE_ONLY
        && micros_endpoint_resolve_active(
            &registry,
            &objects,
            bindings[1].endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_ERROR_CLOSING
    );
    write_u32_le(
        &objects.threads[bindings[0].thread.slot]
            .ipc_inbound_message.payload[4],
        1
    );
    EXPECT_TRUE(
        micros_bootstrap_control_validate(
            &state,
            &registry,
            &objects
        ) == MICROS_BOOTSTRAP_ERROR_INVARIANT
    );
    write_u32_le(
        &objects.threads[bindings[0].thread.slot]
            .ipc_inbound_message.payload[4],
        2
    );
    EXPECT_TRUE(
        micros_bootstrap_control_validate(
            &state,
            &registry,
            &objects
        ) == MICROS_BOOTSTRAP_OK
    );
    return true;
}

static bool test_pm_completion_rejections_are_atomic(void)
{
    struct micros_bootstrap_control_state state;
    struct micros_bootstrap_control_state state_snapshot;
    struct micros_bootstrap_manifest manifest;
    struct micros_endpoint_registry registry;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects;
    struct micros_kernel_objects objects_snapshot;
    struct micros_hart_handle hart;
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct micros_bootstrap_complete_plan plan;
    struct micros_bootstrap_complete_plan sentinel;
    struct micros_bootstrap_ready_plan ready_plan;

    EXPECT_TRUE(
        setup_control_fixture(
            &state,
            &manifest,
            &registry,
            &objects,
            &hart,
            bindings,
            true
        )
        && release_and_accept_ready(
            &state,
            &registry,
            &objects,
            hart,
            &bindings[0],
            10,
            109
        )
        && micros_bootstrap_control_release(
            &state,
            &registry,
            &objects,
            hart,
            bindings[2].service_id,
            110,
            true
        ) == MICROS_BOOTSTRAP_OK
    );
    memset(&sentinel, 0xa5, sizeof(sentinel));
    plan = sentinel;
    state_snapshot = state;
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &state,
            &registry,
            &objects,
            NULL,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
        && memcmp(&state, &state_snapshot, sizeof(state)) == 0
        && memcmp(
            &registry,
            &registry_snapshot,
            sizeof(registry)
        ) == 0
        && memcmp(&objects, &objects_snapshot, sizeof(objects)) == 0
    );
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_ready(
            &state,
            &registry,
            &objects,
            bindings[2].service_id,
            bindings[2].endpoint,
            209,
            true,
            &ready_plan
        ) == MICROS_BOOTSTRAP_OK
    );
    micros_bootstrap_control_commit_ready_prevalidated(
        &state,
        &ready_plan
    );

    registry.endpoints[bindings[2].process.slot].state =
        MICROS_ENDPOINT_STATE_SOURCE_ONLY;
    plan = sentinel;
    state_snapshot = state;
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &state,
            &registry,
            &objects,
            NULL,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_INVARIANT
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
        && memcmp(&state, &state_snapshot, sizeof(state)) == 0
        && memcmp(
            &registry,
            &registry_snapshot,
            sizeof(registry)
        ) == 0
        && memcmp(&objects, &objects_snapshot, sizeof(objects)) == 0
    );
    registry.endpoints[bindings[2].process.slot].state =
        MICROS_ENDPOINT_STATE_ACTIVE;

    objects.threads[bindings[2].thread.slot].scheduler_assigned =
        false;
    plan = sentinel;
    state_snapshot = state;
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &state,
            &registry,
            &objects,
            NULL,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_INVARIANT
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
        && memcmp(&state, &state_snapshot, sizeof(state)) == 0
        && memcmp(
            &registry,
            &registry_snapshot,
            sizeof(registry)
        ) == 0
        && memcmp(&objects, &objects_snapshot, sizeof(objects)) == 0
    );
    return true;
}

static bool test_pm_completion_queues_sealed_event_once(void)
{
    struct micros_bootstrap_control_state state;
    struct micros_bootstrap_control_state state_snapshot;
    struct micros_bootstrap_manifest manifest;
    struct micros_endpoint_registry registry;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects;
    struct micros_kernel_objects objects_snapshot;
    struct micros_hart_handle hart;
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct micros_bootstrap_complete_plan plan;
    struct micros_bootstrap_complete_plan sentinel;
    const struct micros_endpoint_record *pm_endpoint;

    EXPECT_TRUE(
        setup_ready_pm_fixture(
            &state,
            &manifest,
            &registry,
            &objects,
            &hart,
            bindings
        )
    );
    memset(&sentinel, 0xa5, sizeof(sentinel));
    plan = sentinel;
    state_snapshot = state;
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &state,
            &registry,
            &objects,
            NULL,
            &plan
        ) == MICROS_BOOTSTRAP_OK
        && plan.active
        && plan.pm_notification.active
        && !plan.pm_notification.deliver_to_receiver
        && memcmp(&state, &state_snapshot, sizeof(state)) == 0
        && memcmp(
            &registry,
            &registry_snapshot,
            sizeof(registry)
        ) == 0
        && memcmp(&objects, &objects_snapshot, sizeof(objects)) == 0
        && commit_complete_for_test(
            &state,
            &registry,
            &objects,
            &bindings[1],
            &plan
        )
        && state.phase == MICROS_BOOTSTRAP_PHASE_SEALED
        && micros_endpoint_resolve_active(
            &registry,
            &objects,
            bindings[2].endpoint,
            &pm_endpoint
        ) == MICROS_ENDPOINT_OK
        && pm_endpoint->pending_kernel_events
            == MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED
        && micros_bootstrap_control_validate(
            &state,
            &registry,
            &objects
        ) == MICROS_BOOTSTRAP_OK
    );

    plan = sentinel;
    state_snapshot = state;
    registry_snapshot = registry;
    objects_snapshot = objects;
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &state,
            &registry,
            &objects,
            NULL,
            &plan
        ) == MICROS_BOOTSTRAP_ERROR_STATE
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
        && memcmp(&state, &state_snapshot, sizeof(state)) == 0
        && memcmp(
            &registry,
            &registry_snapshot,
            sizeof(registry)
        ) == 0
        && memcmp(&objects, &objects_snapshot, sizeof(objects)) == 0
    );
    return true;
}

static bool test_pm_completion_wakes_only_any_receiver(void)
{
    struct micros_bootstrap_control_state state;
    struct micros_bootstrap_manifest manifest;
    struct micros_endpoint_registry registry;
    struct micros_kernel_objects objects;
    struct micros_hart_handle hart;
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct micros_bootstrap_complete_plan plan;
    struct micros_scheduler_return_plan return_plan;
    const struct micros_endpoint_record *pm_endpoint;
    const struct micros_thread *pm_thread;

    EXPECT_TRUE(
        setup_ready_pm_fixture(
            &state,
            &manifest,
            &registry,
            &objects,
            &hart,
            bindings
        )
        && micros_thread_scheduler_hold(
            &objects,
            bindings[2].thread
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            bindings[2].thread,
            MICROS_ENDPOINT_ANY,
            UINT64_C(0x61004000)
        ) == MICROS_IPC_OK
    );
    EXPECT_TRUE(
        micros_thread_scheduler_hold(
            &objects,
            bindings[0].thread
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_scheduler_accounting_initialize(
            &objects,
            hart,
            100
        ) == MICROS_KERNEL_OBJECT_OK
    );
    EXPECT_TRUE(
        micros_hart_plan_user_return(
            &objects,
            hart,
            &return_plan
        ) == MICROS_KERNEL_OBJECT_OK
        && return_plan.selected.slot
            == bindings[1].thread.slot
        && return_plan.selected.generation
            == bindings[1].thread.generation
    );
    EXPECT_TRUE(
        micros_hart_commit_user_return(
            &objects,
            &return_plan
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_scheduler_account_enter_thread(
            &objects,
            hart,
            bindings[1].thread,
            101
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_scheduler_account_user_trap(
            &objects,
            hart,
            102
        ) == MICROS_KERNEL_OBJECT_OK
    );
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &state,
            &registry,
            &objects,
            NULL,
            &plan
        ) == MICROS_BOOTSTRAP_OK
        && plan.pm_notification.active
        && plan.pm_notification.deliver_to_receiver
    );
    EXPECT_TRUE(
        commit_current_complete_for_test(
            &state,
            &registry,
            &objects,
            hart,
            &bindings[1],
            &plan
        )
    );
    EXPECT_TRUE(
        micros_endpoint_resolve_active(
            &registry,
            &objects,
            bindings[2].endpoint,
            &pm_endpoint
        ) == MICROS_ENDPOINT_OK
        && micros_thread_resolve(
            &objects,
            bindings[2].thread,
            &pm_thread
        ) == MICROS_KERNEL_OBJECT_OK
        && pm_endpoint->pending_kernel_events == 0
        && pm_endpoint->receiver_head.generation == 0
        && pm_endpoint->receiver_tail.generation == 0
        && pm_thread->runtime_flags == 0
        && pm_thread->ready_linked
        && pm_thread->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && pm_thread->ipc_receive_source == 0
        && pm_thread->ipc_receive_buffer == UINT64_C(0x61004000)
        && pm_thread->ipc_delivery_pending
        && sealed_notification_matches(
            &pm_thread->ipc_inbound_message
        )
        && micros_bootstrap_control_validate(
            &state,
            &registry,
            &objects
        ) == MICROS_BOOTSTRAP_OK
    );
    return true;
}

static bool test_pm_completion_preserves_specific_receiver(void)
{
    struct micros_bootstrap_control_state state;
    struct micros_bootstrap_manifest manifest;
    struct micros_endpoint_registry registry;
    struct micros_kernel_objects objects;
    struct micros_hart_handle hart;
    struct micros_bootstrap_binding
        bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct micros_bootstrap_complete_plan plan;
    const struct micros_endpoint_record *pm_endpoint;
    const struct micros_thread *pm_thread;

    EXPECT_TRUE(
        setup_ready_pm_fixture(
            &state,
            &manifest,
            &registry,
            &objects,
            &hart,
            bindings
        )
        && micros_thread_scheduler_hold(
            &objects,
            bindings[2].thread
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_ipc_receiver_enqueue(
            &registry,
            &objects,
            bindings[2].thread,
            bindings[0].endpoint,
            UINT64_C(0x61005000)
        ) == MICROS_IPC_OK
    );
    EXPECT_TRUE(
        micros_bootstrap_control_prepare_complete(
            &state,
            &registry,
            &objects,
            NULL,
            &plan
        ) == MICROS_BOOTSTRAP_OK
        && plan.pm_notification.active
        && !plan.pm_notification.deliver_to_receiver
    );
    EXPECT_TRUE(
        commit_complete_for_test(
            &state,
            &registry,
            &objects,
            &bindings[1],
            &plan
        )
    );
    EXPECT_TRUE(
        micros_endpoint_resolve_active(
            &registry,
            &objects,
            bindings[2].endpoint,
            &pm_endpoint
        ) == MICROS_ENDPOINT_OK
        && micros_thread_resolve(
            &objects,
            bindings[2].thread,
            &pm_thread
        ) == MICROS_KERNEL_OBJECT_OK
        && pm_endpoint->pending_kernel_events
            == MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED
        && pm_endpoint->receiver_head.slot
            == bindings[2].thread.slot
        && pm_endpoint->receiver_head.generation
            == bindings[2].thread.generation
        && pm_endpoint->receiver_tail.slot
            == bindings[2].thread.slot
        && pm_endpoint->receiver_tail.generation
            == bindings[2].thread.generation
        && pm_thread->runtime_flags
            == MICROS_THREAD_RTS_IPC_RECEIVE
        && !pm_thread->ready_linked
        && pm_thread->ipc_queue_kind
            == MICROS_IPC_QUEUE_RECEIVER
        && pm_thread->ipc_receive_source
            == bindings[0].endpoint
        && !pm_thread->ipc_delivery_pending
        && micros_bootstrap_control_validate(
            &state,
            &registry,
            &objects
        ) == MICROS_BOOTSTRAP_OK
    );
    return true;
}

static bool test_image_catalog(void)
{
    static unsigned char text[MICROS_SV39_PAGE_SIZE];
    static unsigned char rodata[MICROS_SV39_PAGE_SIZE];
    static unsigned char data[MICROS_SV39_PAGE_SIZE];
    struct micros_bootstrap_image images[2];
    struct micros_bootstrap_image_info infos[2];
    struct micros_bootstrap_image_info sentinel[2];

    memset(text, 0x11, sizeof(text));
    memset(rodata, 0x22, sizeof(rodata));
    memset(data, 0, sizeof(data));
    memset(images, 0, sizeof(images));
    images[0] = (struct micros_bootstrap_image){
        .version = MICROS_BOOTSTRAP_IMAGE_VERSION,
        .image_id = 101,
        .entry = MICROS_USER_VIRTUAL_BASE,
        .segment_count = MICROS_BOOTSTRAP_IMAGE_SEGMENT_COUNT,
        .segments = {
            {
                .virtual_address = MICROS_USER_VIRTUAL_BASE,
                .file_size = sizeof(text),
                .memory_size = sizeof(text),
                .flags = MICROS_BOOTSTRAP_IMAGE_READ
                    | MICROS_BOOTSTRAP_IMAGE_EXECUTE,
                .file_bytes = text,
            },
            {
                .virtual_address =
                    MICROS_USER_VIRTUAL_BASE
                    + MICROS_SV39_PAGE_SIZE,
                .file_size = sizeof(rodata),
                .memory_size = sizeof(rodata),
                .flags = MICROS_BOOTSTRAP_IMAGE_READ,
                .file_bytes = rodata,
            },
            {
                .virtual_address =
                    MICROS_USER_VIRTUAL_BASE
                    + 2 * MICROS_SV39_PAGE_SIZE,
                .file_size = sizeof(data),
                .memory_size = sizeof(data),
                .flags = MICROS_BOOTSTRAP_IMAGE_READ
                    | MICROS_BOOTSTRAP_IMAGE_WRITE,
                .file_bytes = data,
            },
        },
        .config_address =
            MICROS_USER_VIRTUAL_BASE
            + 2 * MICROS_SV39_PAGE_SIZE,
        .config_size =
            sizeof(struct micros_bootstrap_service_config),
        .page_count = 3,
        .image_end =
            MICROS_USER_VIRTUAL_BASE
            + 3 * MICROS_SV39_PAGE_SIZE,
    };
    images[1] = images[0];
    images[1].image_id = 102;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_OK
        && infos[0].image_id == 101
        && infos[0].page_count == 3
        && infos[0].config_initially_zero
        && infos[1].image_id == 102
    );

    images[0].segments[2].memory_size =
        91 * MICROS_SV39_PAGE_SIZE;
    images[0].page_count = 93;
    images[0].image_end =
        MICROS_USER_VIRTUAL_BASE + 93 * MICROS_SV39_PAGE_SIZE;
    images[0].vm_boot_info_address =
        MICROS_USER_VIRTUAL_BASE + 3 * MICROS_SV39_PAGE_SIZE;
    images[0].vm_boot_info_size = MICROS_VM_BOOT_INFO_SIZE;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_OK
        && infos[0].vm_boot_info_address
            == images[0].vm_boot_info_address
        && infos[0].vm_boot_info_size == MICROS_VM_BOOT_INFO_SIZE
        && infos[0].vm_boot_info_initially_zero
        && infos[1].vm_boot_info_address == 0
        && infos[1].vm_boot_info_size == 0
        && !infos[1].vm_boot_info_initially_zero
    );
    images[0].vm_boot_info_address += 1;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_ERROR_IMAGE
    );
    images[0].vm_boot_info_address -= 1;
    images[0].vm_boot_info_size = MICROS_VM_BOOT_INFO_SIZE - 1;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_ERROR_IMAGE
    );
    images[0].vm_boot_info_size = MICROS_VM_BOOT_INFO_SIZE;
    images[0].vm_boot_info_address = images[0].config_address;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_ERROR_IMAGE
    );
    images[0].vm_boot_info_address =
        MICROS_USER_VIRTUAL_BASE + 3 * MICROS_SV39_PAGE_SIZE;

    memset(sentinel, 0xa5, sizeof(sentinel));
    memcpy(infos, sentinel, sizeof(infos));
    images[1].image_id = 101;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_ERROR_IMAGE
        && memcmp(infos, sentinel, sizeof(infos)) == 0
    );
    images[1].image_id = 102;
    images[0].segments[0].flags |= MICROS_BOOTSTRAP_IMAGE_WRITE;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_ERROR_IMAGE
        && memcmp(infos, sentinel, sizeof(infos)) == 0
    );
    images[0].segments[0].flags =
        MICROS_BOOTSTRAP_IMAGE_READ
        | MICROS_BOOTSTRAP_IMAGE_EXECUTE;
    data[0] = 1;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_ERROR_IMAGE
        && memcmp(infos, sentinel, sizeof(infos)) == 0
    );
    data[0] = 0;
    return true;
}

int main(void)
{
    return (
        test_valid_commands()
        && test_malformed_commands()
        && test_control_transitions()
        && test_pm_completion_rejections_are_atomic()
        && test_pm_completion_queues_sealed_event_once()
        && test_pm_completion_wakes_only_any_receiver()
        && test_pm_completion_preserves_specific_receiver()
        && test_image_catalog()
    )
        ? 0
        : 1;
}
