#include "ipc_model_test_internal.h"

static const size_t model_initial_thread_counts[MODEL_PROCESS_COUNT] = {
    2,
    2,
    2,
    1,
    1,
    1,
    1,
};

static struct ipc_model_test_context model_context;

struct ipc_model_test_context *ipc_model_test_context_get(void)
{
    return &model_context;
}

static void fill_message(
    struct micros_ipc_message *message,
    size_t step,
    bool kernel_type
)
{
    size_t index;

    memset(message, 0, sizeof(*message));
    message->source = MICROS_ENDPOINT_ANY;
    message->type =
        (uint32_t)(UINT32_C(0x1000) + (uint32_t)step);
    if (kernel_type) {
        message->type |= MICROS_IPC_TYPE_KERNEL_MASK;
    }
    message->reply_token =
        UINT64_C(0xa5a5000000000000) | (uint64_t)step;
    for (index = 0; index < sizeof(message->payload); ++index) {
        message->payload[index] =
            (uint8_t)(step + index * 3);
    }
}

struct micros_ipc_message *ipc_model_message_for_step(
    struct ipc_model_test_context *fixture,
    size_t step,
    bool kernel_type
)
{
    struct micros_ipc_message *message =
        &fixture->messages[step % MODEL_MESSAGE_COUNT];

    fill_message(message, step, kernel_type);
    return message;
}

uintptr_t ipc_model_receive_buffer_for_step(size_t step, size_t thread_slot)
{
    return (
        UINT64_C(0x40000000)
        + (uint64_t)thread_slot * UINT64_C(0x00100000)
        + (uint64_t)(step & 0xffff) * UINT64_C(0x80)
    );
}

struct acceptance_setup_state {
    struct micros_hart_handle hart;
    size_t process_slot;
    size_t next_thread_slot;
};

static void reset_acceptance_fixture_and_reference(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    struct acceptance_setup_state *setup
)
{
    memset(model, 0, sizeof(*model));
    memset(&fixture->registry, 0, sizeof(fixture->registry));
    memset(&fixture->objects, 0, sizeof(fixture->objects));
    memset(fixture->messages, 0, sizeof(fixture->messages));
    memset(fixture->model_trace, 0, sizeof(fixture->model_trace));
    fixture->model_trace_count = 0;
    fixture->model_trace_hash = UINT64_C(0x30accee57ace0001);
    for (
        setup->process_slot = 0;
        setup->process_slot < MICROS_PROCESS_CAPACITY;
        ++setup->process_slot
    ) {
        model->processes[setup->process_slot].primary_endpoint =
            MICROS_PROCESS_ENDPOINT_NONE;
    }
}

static bool initialize_acceptance_profiles_and_hart(
    struct ipc_model_test_context *fixture,
    struct acceptance_setup_state *setup
)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = MODEL_FULL_PROFILE,
            .name = "MODEL_FULL",
            .operations = MICROS_PRIVILEGE_OPERATION_DEFINED_MASK,
            .call_targets = UINT32_C(1) << MODEL_FULL_PROFILE,
            .send_targets = UINT32_C(1) << MODEL_FULL_PROFILE,
            .notify_targets = UINT32_C(1) << MODEL_FULL_PROFILE,
        },
        {
            .id = MODEL_DENIED_PROFILE,
            .name = "MODEL_DENIED",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        },
    };
    if (
        micros_endpoint_registry_initialize(
            &fixture->registry,
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || micros_kernel_objects_initialize(&fixture->objects, 2, 1)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&fixture->objects, 0, &setup->hart)
            != MICROS_KERNEL_OBJECT_OK
        || setup->hart.slot != 0
        || setup->hart.generation != 1
        || micros_hart_install_trap_stacks(
            &fixture->objects,
            setup->hart,
            UINT64_C(0x01000000),
            UINT64_C(0x01004000),
            UINT64_C(0x02000000),
            UINT64_C(0x02001000)
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    return true;
}

static bool prepare_acceptance_process_and_thread_inventory(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    struct acceptance_setup_state *setup
)
{
    model->hart_handle = setup->hart;
    for (setup->process_slot = 0; setup->process_slot < MODEL_PROCESS_COUNT; ++setup->process_slot) {
        struct micros_process_handle process;
        micros_endpoint_t endpoint;
        uint8_t profile_id =
            setup->process_slot == MODEL_DENIED_PROCESS
                ? MODEL_DENIED_PROFILE
                : MODEL_FULL_PROFILE;
        size_t local_index;

        if (
            micros_process_create(&fixture->objects, &process)
                != MICROS_KERNEL_OBJECT_OK
            || process.slot != setup->process_slot
            || process.generation != 1
            || micros_endpoint_reserve(
                &fixture->registry,
                &fixture->objects,
                process,
                &endpoint
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
        model->processes[setup->process_slot].slot_state =
            MICROS_KERNEL_OBJECT_SLOT_LIVE;
        model->processes[setup->process_slot].generation = 1;
        model->processes[setup->process_slot].primary_endpoint = endpoint;
        model->processes[setup->process_slot].privilege_profile = profile_id;
        model->processes[setup->process_slot].endpoint_lifecycle_consumed = true;
        model->endpoints[setup->process_slot].state =
            MICROS_ENDPOINT_STATE_RESERVED;
        model->endpoints[setup->process_slot].owner = process;
        model->endpoints[setup->process_slot].value = endpoint;
        for (
            local_index = 0;
            local_index < model_initial_thread_counts[setup->process_slot];
            ++local_index
        ) {
            struct micros_thread_handle thread;

            if (
                setup->next_thread_slot >= MODEL_THREAD_COUNT
                || micros_thread_create(
                    &fixture->objects,
                    process,
                    &thread
                ) != MICROS_KERNEL_OBJECT_OK
                || thread.slot != setup->next_thread_slot
                || thread.generation != 1
            ) {
                return false;
            }
            model->threads[setup->next_thread_slot].slot_state =
                MICROS_KERNEL_OBJECT_SLOT_LIVE;
            model->threads[setup->next_thread_slot].generation = 1;
            model->threads[setup->next_thread_slot].owner = process;
            model->threads[setup->next_thread_slot].runtime_flags =
                MICROS_THREAD_RTS_INACTIVE;
            model->threads[setup->next_thread_slot].ipc_staged_result =
                MICROS_IPC_OK;
            ++setup->next_thread_slot;
            ++model->processes[setup->process_slot].live_thread_count;
            ++model->live_thread_count;
        }
        if (
            micros_endpoint_install_profile(
                &fixture->registry,
                &fixture->objects,
                process,
                profile_id
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                &fixture->registry,
                &fixture->objects,
                endpoint
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
        model->endpoints[setup->process_slot].state =
            MICROS_ENDPOINT_STATE_ACTIVE;
        ++model->live_process_count;
    }
    if (setup->next_thread_slot != MODEL_THREAD_COUNT) {
        return false;
    }
    return true;
}

static bool prepare_acceptance_execution_and_scheduler(
    struct ipc_model_test_context *fixture,
    struct model_state *model,
    struct acceptance_setup_state *setup
)
{
    for (setup->next_thread_slot = 0; setup->next_thread_slot < MODEL_THREAD_COUNT;
         ++setup->next_thread_slot) {
        struct micros_thread_handle thread =
            ipc_model_thread_handle(model, setup->next_thread_slot);
        struct micros_user_context context =
            ipc_model_context_pattern(UINT64_C(0x1000) + setup->next_thread_slot * 0x100);
        uintptr_t stack_bottom =
            UINT64_C(0x10000000)
            + setup->next_thread_slot * UINT64_C(0x00008000);

        if (
            micros_thread_attach_execution_context(
                &fixture->objects,
                thread,
                stack_bottom,
                stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
                &context
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_admit(
                &fixture->objects,
                setup->hart,
                thread,
                MODEL_PRIORITY,
                100,
                true
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_scheduler_hold(&fixture->objects, thread)
                != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        model->threads[setup->next_thread_slot].context_attached = true;
        model->threads[setup->next_thread_slot].kernel_stack_bottom =
            stack_bottom;
        model->threads[setup->next_thread_slot].kernel_stack_top =
            stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE;
        model->threads[setup->next_thread_slot].scheduler_assigned = true;
        model->threads[setup->next_thread_slot].scheduler_preemptible = true;
        model->threads[setup->next_thread_slot].scheduler_priority =
            MODEL_PRIORITY;
        model->threads[setup->next_thread_slot].scheduler_hart = setup->hart;
        model->threads[setup->next_thread_slot].quantum_counter_ticks = 100;
        model->threads[setup->next_thread_slot].remaining_counter_ticks = 100;
    }
    return true;
}

bool ipc_model_setup_model(
    struct ipc_model_test_context *fixture,
    struct model_state *model
)
{
    struct acceptance_setup_state setup;

    setup.next_thread_slot = 0;
    reset_acceptance_fixture_and_reference(fixture, model, &setup);
    return (
        initialize_acceptance_profiles_and_hart(fixture, &setup)
        && prepare_acceptance_process_and_thread_inventory(fixture, model, &setup)
        && prepare_acceptance_execution_and_scheduler(fixture, model, &setup)
        && ipc_model_matches_production(fixture, model, 0)
    );
}
