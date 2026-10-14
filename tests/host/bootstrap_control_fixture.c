#include "bootstrap_control_test_internal.h"

#include "kernel/endpoint_internal.h"
#include "micros/scheduler_core.h"

struct control_fixture_preparation {
    struct micros_bootstrap_manifest_plan plan;
    struct micros_privilege_profile profiles[3];
    struct micros_process_handle processes[3];
    struct micros_thread_handle threads[3];
    micros_endpoint_t endpoints[3];
    size_t service_count;
};

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

static bool create_control_objects(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle *hart,
    struct control_fixture_preparation *preparation
)
{
    size_t index;

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
            preparation->profiles,
            preparation->service_count
        ) != MICROS_ENDPOINT_OK
    ) {
        return false;
    }
    for (index = 0; index < preparation->service_count; ++index) {
        struct micros_user_context context =
            context_pattern(UINT64_C(0x1000) + index * 0x100);
        uintptr_t root = UINT64_C(0x10000000)
            + index * UINT64_C(0x1000);
        uintptr_t stack = UINT64_C(0x20000000)
            + index * UINT64_C(0x8000);

        if (
            micros_process_create(objects, &preparation->processes[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_process_attach_address_space(
                objects,
                preparation->processes[index],
                root
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                registry,
                objects,
                preparation->processes[index],
                &preparation->endpoints[index]
            ) != MICROS_ENDPOINT_OK
            || micros_thread_create(
                objects,
                preparation->processes[index],
                &preparation->threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_attach_execution_context(
                objects,
                preparation->threads[index],
                stack,
                stack + MICROS_THREAD_KERNEL_STACK_SIZE,
                &context
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    return true;
}

static void initialize_control_bindings(
    struct micros_bootstrap_binding bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY],
    const struct control_fixture_preparation *preparation,
    bool include_pm
)
{
    bindings[0] = (struct micros_bootstrap_binding){
        .manifest_index = 0,
        .service_id = 2,
        .process = preparation->processes[1],
        .thread = preparation->threads[1],
        .root = UINT64_C(0x10001000),
        .endpoint = preparation->endpoints[1],
        .prepared_page_count = 3,
        .scheduler_priority = 7,
        .scheduler_preemptible = true,
        .scheduler_quantum_counter_ticks = 100,
    };
    bindings[1] = (struct micros_bootstrap_binding){
        .manifest_index = 1,
        .service_id = 1,
        .process = preparation->processes[0],
        .thread = preparation->threads[0],
        .root = UINT64_C(0x10000000),
        .endpoint = preparation->endpoints[0],
        .prepared_page_count = 4,
        .scheduler_priority = 8,
        .scheduler_preemptible = true,
        .scheduler_quantum_counter_ticks = 100,
    };
    if (include_pm) {
        bindings[2] = (struct micros_bootstrap_binding){
            .manifest_index = 2,
            .service_id = 3,
            .process = preparation->processes[2],
            .thread = preparation->threads[2],
            .root = UINT64_C(0x10002000),
            .endpoint = preparation->endpoints[2],
            .prepared_page_count = 3,
            .scheduler_priority = 6,
            .scheduler_preemptible = true,
            .scheduler_quantum_counter_ticks = 100,
        };
    }
}

static bool publish_control_fixture(
    struct micros_bootstrap_control_state *state,
    struct micros_bootstrap_manifest *manifest,
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    struct micros_hart_handle hart,
    const struct micros_bootstrap_binding bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY],
    const struct control_fixture_preparation *preparation
)
{
    return (
        micros_bootstrap_control_state_prepare(
            state,
            manifest,
            &preparation->plan,
            bindings,
            preparation->service_count
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
            hart,
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

bool bootstrap_control_test_setup_control_fixture(
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
    struct control_fixture_preparation preparation = {
        .plan = {
            .entry_count = include_pm ? 3 : 2,
            .total_user_page_limit = include_pm ? 10 : 7,
            .controller_service_id = 1,
            .vm_service_id = 2,
            .pm_service_id = include_pm ? 3 : 0,
            .ordered_service_ids = {1, 2, 3},
            .ordered_manifest_indices = {1, 0, 2},
        },
        .service_count = include_pm ? 3 : 2,
    };

    memset(state, 0, sizeof(*state));
    memset(registry, 0, sizeof(*registry));
    memset(objects, 0, sizeof(*objects));
    initialize_manifest(manifest, include_pm);
    preparation.profiles[0] = profile(1, "BOOTSTRAP_LAUNCHER");
    preparation.profiles[1] = profile(2, "VM");
    preparation.profiles[2] = profile(MICROS_PRIVILEGE_PROFILE_PM, "PM");
    if (!create_control_objects(registry, objects, hart, &preparation)) {
        return false;
    }
    initialize_control_bindings(bindings, &preparation, include_pm);
    return publish_control_fixture(
        state,
        manifest,
        registry,
        objects,
        *hart,
        bindings,
        &preparation
    );
}

bool bootstrap_control_test_release_and_accept_ready(
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

bool bootstrap_control_test_setup_ready_pm_fixture(
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
        bootstrap_control_test_setup_control_fixture(
            state,
            manifest,
            registry,
            objects,
            hart,
            bindings,
            true
        )
        && bootstrap_control_test_release_and_accept_ready(
            state,
            registry,
            objects,
            *hart,
            &bindings[0],
            10,
            109
        )
        && bootstrap_control_test_release_and_accept_ready(
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

bool bootstrap_control_test_commit_complete_for_test(
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

bool bootstrap_control_test_commit_current_complete_for_test(
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

uint32_t bootstrap_control_test_read_u32_le(const unsigned char *bytes)
{
    return (
        (uint32_t)bytes[0]
        | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16
        | (uint32_t)bytes[3] << 24
    );
}

void bootstrap_control_test_write_u32_le(unsigned char *bytes, uint32_t value)
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

bool bootstrap_control_test_sealed_notification_matches(
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
