#include "kernel/pm_control_core.h"
#include "kernel/pm_control_syscall_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/endpoint.h"
#include "micros/user_address_space.h"

bool micros_pm_control_test_run(void);

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

static uint32_t process_endpoint(struct micros_process_handle process)
{
    return (
        process.generation << MICROS_ENDPOINT_SLOT_BITS
    ) | process.slot;
}

static bool initialize_fixture(
    struct micros_kernel_objects *objects,
    struct micros_pm_control_state *state
)
{
    struct micros_process_handle process;
    struct micros_process_handle pm_process;
    struct micros_thread_handle pm_thread;

    memset(objects, 0, sizeof(*objects));
    memset(state, 0, sizeof(*state));
    return (
        micros_kernel_objects_initialize(objects, 1, 1)
            == MICROS_KERNEL_OBJECT_OK
        && micros_process_create_at(objects, 0, &process)
            == MICROS_KERNEL_OBJECT_OK
        && micros_process_create_at(objects, 1, &process)
            == MICROS_KERNEL_OBJECT_OK
        && micros_process_create_at(objects, 2, &pm_process)
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_create(objects, pm_process, &pm_thread)
            == MICROS_KERNEL_OBJECT_OK
        && micros_pm_control_state_prepare(
            state,
            3,
            process_endpoint(pm_process),
            MICROS_PRIVILEGE_PROFILE_PM,
            pm_process,
            pm_thread
        ) == MICROS_PM_CONTROL_OK
        && micros_pm_control_state_validate(state)
            == MICROS_PM_CONTROL_OK
        && micros_pm_control_state_validate_objects(state, objects)
            == MICROS_PM_CONTROL_OK
    );
}

static bool decode_failure_preserves_request(
    struct micros_syscall_arguments arguments
)
{
    struct micros_pm_control_request request;
    struct micros_pm_control_request sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    request = sentinel;
    return (
        micros_pm_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
}

static bool test_contract_and_decode(void)
{
    struct micros_pm_control_request request;
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_PM_CONTROL_RESERVE,
        .a1 = MICROS_USER_VIRTUAL_BASE + 8,
        .a2 = MICROS_PM_RESERVATION_SIZE,
        .a7 = MICROS_SYSCALL_ABI_PM_CONTROL,
    };
    size_t index;

    EXPECT_TRUE(
        MICROS_SYSCALL_ABI_PM_CONTROL == 13
        && MICROS_PM_CONTROL_RESERVE == 1
        && MICROS_PM_CONTROL_ABORT_RESERVED == 2
        && MICROS_PM_RESERVATION_VERSION == 1
        && MICROS_PM_RESERVATION_SIZE == 32
    );
    EXPECT_TRUE(
        micros_pm_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_PM_CONTROL_RESERVE
        && request.output_address == arguments.a1
        && request.transaction == 0
    );
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_PM_CONTROL_ABORT_RESERVED,
        .a1 = UINT64_C(0xfedcba9876543210),
        .a7 = MICROS_SYSCALL_ABI_PM_CONTROL,
    };
    EXPECT_TRUE(
        micros_pm_control_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_PM_CONTROL_ABORT_RESERVED
        && request.output_address == 0
        && request.transaction == arguments.a1
    );

    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_PM_CONTROL_RESERVE,
        .a1 = MICROS_USER_VIRTUAL_BASE + 8,
        .a2 = MICROS_PM_RESERVATION_SIZE,
        .a7 = MICROS_SYSCALL_ABI_PM_CONTROL,
    };
    arguments.a1 += 1;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    arguments.a1 -= 1;
    arguments.a2 -= 1;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    arguments.a2 = MICROS_PM_RESERVATION_SIZE;
    for (index = 3; index <= 6; ++index) {
        switch (index) {
        case 3:
            arguments.a3 = 1;
            break;
        case 4:
            arguments.a4 = 1;
            break;
        case 5:
            arguments.a5 = 1;
            break;
        case 6:
            arguments.a6 = 1;
            break;
        default:
            return false;
        }
        EXPECT_TRUE(decode_failure_preserves_request(arguments));
        arguments.a3 = 0;
        arguments.a4 = 0;
        arguments.a5 = 0;
        arguments.a6 = 0;
    }
    arguments.a7 = MICROS_SYSCALL_ABI_VM_HANDOFF;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));

    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_PM_CONTROL_ABORT_RESERVED,
        .a1 = 1,
        .a7 = MICROS_SYSCALL_ABI_PM_CONTROL,
    };
    for (index = 2; index <= 6; ++index) {
        switch (index) {
        case 2:
            arguments.a2 = 1;
            break;
        case 3:
            arguments.a3 = 1;
            break;
        case 4:
            arguments.a4 = 1;
            break;
        case 5:
            arguments.a5 = 1;
            break;
        case 6:
            arguments.a6 = 1;
            break;
        default:
            return false;
        }
        EXPECT_TRUE(decode_failure_preserves_request(arguments));
        arguments.a2 = 0;
        arguments.a3 = 0;
        arguments.a4 = 0;
        arguments.a5 = 0;
        arguments.a6 = 0;
    }
    arguments.a1 = 0;
    EXPECT_TRUE(decode_failure_preserves_request(arguments));
    arguments.a0 = 3;
    arguments.a1 = 1;
    EXPECT_TRUE(
        decode_failure_preserves_request(arguments)
        && micros_pm_control_decode(NULL, &request)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && micros_pm_control_decode(&arguments, NULL)
            == MICROS_SYSCALL_ABI_ARGUMENT
    );
    return true;
}

static bool test_syscall_authority(void)
{
    struct micros_kernel_objects objects;
    struct micros_pm_control_state state;
    struct micros_bootstrap_binding binding = {0};
    struct micros_bootstrap_manifest_entry entry = {0};
    struct micros_syscall_context context = {0};
    struct micros_process process;
    struct micros_endpoint_record endpoint = {0};
    struct micros_privilege_profile profile = {0};

    EXPECT_TRUE(initialize_fixture(&objects, &state));
    binding.service_id = state.service_id;
    binding.endpoint = state.endpoint;
    binding.process = state.process;
    binding.thread = state.thread;
    entry.service_id = state.service_id;
    entry.process_slot = state.process.slot;
    entry.profile_id = MICROS_PRIVILEGE_PROFILE_PM;
    entry.role_flags = MICROS_BOOTSTRAP_ROLE_PM;
    context.objects = &objects;
    context.current = state.thread;
    context.process = state.process;
    process = objects.processes[state.process.slot];
    process.primary_endpoint = state.endpoint;
    process.privilege_profile = MICROS_PRIVILEGE_PROFILE_PM;
    endpoint.state = MICROS_ENDPOINT_STATE_ACTIVE;
    endpoint.owner = state.process;
    endpoint.value = state.endpoint;
    profile.id = MICROS_PRIVILEGE_PROFILE_PM;
    profile.kernel_operations = MICROS_KERNEL_OPERATION_PM_CONTROL;

    EXPECT_TRUE(
        micros_pm_control_syscall_authority_matches(
            &state,
            state.service_id,
            &binding,
            &entry,
            &context,
            &process,
            &endpoint,
            &profile
        )
    );
    context.process.generation += 1;
    EXPECT_TRUE(
        !micros_pm_control_syscall_authority_matches(
            &state,
            state.service_id,
            &binding,
            &entry,
            &context,
            &process,
            &endpoint,
            &profile
        )
    );
    context.process = state.process;
    context.current.generation += 1;
    EXPECT_TRUE(
        !micros_pm_control_syscall_authority_matches(
            &state,
            state.service_id,
            &binding,
            &entry,
            &context,
            &process,
            &endpoint,
            &profile
        )
    );
    context.current = state.thread;
    endpoint.owner.generation += 1;
    EXPECT_TRUE(
        !micros_pm_control_syscall_authority_matches(
            &state,
            state.service_id,
            &binding,
            &entry,
            &context,
            &process,
            &endpoint,
            &profile
        )
    );
    endpoint.owner = state.process;
    entry.role_flags = 0;
    EXPECT_TRUE(
        !micros_pm_control_syscall_authority_matches(
            &state,
            state.service_id,
            &binding,
            &entry,
            &context,
            &process,
            &endpoint,
            &profile
        )
    );
    entry.role_flags = MICROS_BOOTSTRAP_ROLE_PM;
    profile.kernel_operations |= UINT64_C(0x2);
    EXPECT_TRUE(
        !micros_pm_control_syscall_authority_matches(
            &state,
            state.service_id,
            &binding,
            &entry,
            &context,
            &process,
            &endpoint,
            &profile
        )
        && !micros_pm_control_syscall_authority_matches(
            &state,
            0,
            &binding,
            &entry,
            &context,
            &process,
            &endpoint,
            &profile
        )
    );
    return true;
}

struct output_translation_fixture {
    uint64_t base;
    size_t call_count;
    size_t chunk_count;
    size_t chunk_sizes[3];
    size_t fault_call;
    unsigned char chunks[3][sizeof(struct micros_pm_reservation_result)];
};

static enum micros_pm_control_output_error fake_output_translate(
    void *context,
    uint64_t user_address,
    size_t requested_size,
    uintptr_t *physical_address,
    size_t *contiguous_size
)
{
    struct output_translation_fixture *fixture = context;
    size_t call;
    size_t offset = 0;
    size_t index;

    if (
        fixture == NULL
        || physical_address == NULL
        || contiguous_size == NULL
    ) {
        return MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT;
    }
    call = fixture->call_count++;
    if (call == fixture->fault_call) {
        return MICROS_PM_CONTROL_OUTPUT_ERROR_MEMORY_FAULT;
    }
    if (call >= fixture->chunk_count) {
        return MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT;
    }
    for (index = 0; index < call; ++index) {
        offset += fixture->chunk_sizes[index];
    }
    if (
        user_address != fixture->base + offset
        || requested_size
            != sizeof(struct micros_pm_reservation_result) - offset
    ) {
        return MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT;
    }
    *physical_address = (uintptr_t)&fixture->chunks[call][0];
    *contiguous_size = fixture->chunk_sizes[call];
    return MICROS_PM_CONTROL_OUTPUT_OK;
}

static bool output_chunks_are(
    const struct output_translation_fixture *fixture,
    unsigned char value
)
{
    size_t chunk;
    size_t index;

    for (chunk = 0; chunk < 3; ++chunk) {
        for (
            index = 0;
            index < sizeof(fixture->chunks[chunk]);
            ++index
        ) {
            if (fixture->chunks[chunk][index] != value) {
                return false;
            }
        }
    }
    return true;
}

static void initialize_output_fixture(
    struct output_translation_fixture *fixture
)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->base = UINT64_C(0x4000);
    fixture->fault_call = SIZE_MAX;
    memset(fixture->chunks, 0xa5, sizeof(fixture->chunks));
}

static bool test_syscall_phase_and_retained_output(void)
{
    struct output_translation_fixture fixture;
    struct micros_kernel_objects objects;
    struct micros_pm_control_state state;
    struct micros_pm_control_reserve_plan reserve;
    struct micros_pm_control_output_plan plan;
    struct micros_pm_control_output_plan sentinel;
    struct micros_pm_reservation_result result = {
        .version = MICROS_PM_PROTOCOL_VERSION,
        .size = sizeof(struct micros_pm_reservation_result),
        .transaction = UINT64_C(0x0102030405060708),
        .reserved = {0, 0},
    };
    const unsigned char *result_bytes =
        (const unsigned char *)&result;

    EXPECT_TRUE(
        micros_pm_control_syscall_phase_is_ready(
            MICROS_BOOTSTRAP_PHASE_SEALED,
            MICROS_VM_HANDOFF_PHASE_HANDED_OFF,
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        )
        && !micros_pm_control_syscall_phase_is_ready(
            MICROS_BOOTSTRAP_PHASE_RUNNING,
            MICROS_VM_HANDOFF_PHASE_HANDED_OFF,
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        )
        && !micros_pm_control_syscall_phase_is_ready(
            MICROS_BOOTSTRAP_PHASE_SEALED,
            MICROS_VM_HANDOFF_PHASE_PREPARED,
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        )
        && !micros_pm_control_syscall_phase_is_ready(
            MICROS_BOOTSTRAP_PHASE_SEALED,
            MICROS_VM_HANDOFF_PHASE_HANDED_OFF,
            MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        )
    );

    initialize_output_fixture(&fixture);
    fixture.chunk_count = 1;
    fixture.chunk_sizes[0] = sizeof(result);
    EXPECT_TRUE(
        micros_pm_control_output_prepare(
            fixture.base,
            fake_output_translate,
            &fixture,
            &plan
        ) == MICROS_PM_CONTROL_OUTPUT_OK
        && plan.chunk_count == 1
        && plan.chunks[0].size == sizeof(result)
        && output_chunks_are(&fixture, 0xa5)
    );
    micros_pm_control_output_commit_prevalidated(&plan, &result);
    EXPECT_TRUE(
        memcmp(fixture.chunks[0], result_bytes, sizeof(result)) == 0
    );

    initialize_output_fixture(&fixture);
    fixture.chunk_count = 2;
    fixture.chunk_sizes[0] = 8;
    fixture.chunk_sizes[1] = sizeof(result) - 8;
    EXPECT_TRUE(
        micros_pm_control_output_prepare(
            fixture.base,
            fake_output_translate,
            &fixture,
            &plan
        ) == MICROS_PM_CONTROL_OUTPUT_OK
        && plan.chunk_count == 2
        && plan.chunks[0].size == 8
        && plan.chunks[1].size == sizeof(result) - 8
        && output_chunks_are(&fixture, 0xa5)
    );
    micros_pm_control_output_commit_prevalidated(&plan, &result);
    EXPECT_TRUE(
        memcmp(fixture.chunks[0], result_bytes, 8) == 0
        && memcmp(
            fixture.chunks[1],
            &result_bytes[8],
            sizeof(result) - 8
        ) == 0
    );

    memset(&sentinel, 0x5a, sizeof(sentinel));
    plan = sentinel;
    initialize_output_fixture(&fixture);
    fixture.chunk_count = 2;
    fixture.chunk_sizes[0] = 8;
    fixture.chunk_sizes[1] = sizeof(result) - 8;
    fixture.fault_call = 1;
    EXPECT_TRUE(
        micros_pm_control_output_prepare(
            fixture.base,
            fake_output_translate,
            &fixture,
            &plan
        ) == MICROS_PM_CONTROL_OUTPUT_ERROR_MEMORY_FAULT
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
        && output_chunks_are(&fixture, 0xa5)
    );

    plan = sentinel;
    initialize_output_fixture(&fixture);
    fixture.chunk_count = 1;
    fixture.chunk_sizes[0] = 0;
    EXPECT_TRUE(
        micros_pm_control_output_prepare(
            fixture.base,
            fake_output_translate,
            &fixture,
            &plan
        ) == MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
        && output_chunks_are(&fixture, 0xa5)
    );

    plan = sentinel;
    initialize_output_fixture(&fixture);
    fixture.chunk_count = 3;
    fixture.chunk_sizes[0] = 8;
    fixture.chunk_sizes[1] = 8;
    fixture.chunk_sizes[2] = sizeof(result) - 16;
    EXPECT_TRUE(
        micros_pm_control_output_prepare(
            fixture.base,
            fake_output_translate,
            &fixture,
            &plan
        ) == MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
        && output_chunks_are(&fixture, 0xa5)
    );

    initialize_output_fixture(&fixture);
    fixture.chunk_count = 1;
    fixture.chunk_sizes[0] = sizeof(result);
    EXPECT_TRUE(
        initialize_fixture(&objects, &state)
        && micros_pm_control_output_prepare(
            fixture.base,
            fake_output_translate,
            &fixture,
            &plan
        ) == MICROS_PM_CONTROL_OUTPUT_OK
    );
    state.last_transaction = UINT64_MAX;
    EXPECT_TRUE(
        micros_pm_control_reserve_preflight(
            &state,
            &objects,
            &reserve
        ) == MICROS_PM_CONTROL_ERROR_CAPACITY
        && output_chunks_are(&fixture, 0xa5)
    );
    return true;
}

static bool test_state_preparation(void)
{
    struct micros_kernel_objects objects;
    struct micros_pm_control_state state;
    struct micros_pm_control_state sentinel;
    struct micros_process_handle process = {
        .slot = 2,
        .generation = 1,
    };
    struct micros_thread_handle thread = {
        .slot = 4,
        .generation = 1,
    };

    EXPECT_TRUE(initialize_fixture(&objects, &state));
    sentinel = state;
    EXPECT_TRUE(
        micros_pm_control_state_prepare(
            &state,
            3,
            process_endpoint(process),
            MICROS_PRIVILEGE_PROFILE_PM,
            process,
            thread
        ) == MICROS_PM_CONTROL_ERROR_STORAGE
        && memcmp(&state, &sentinel, sizeof(state)) == 0
    );
    memset(&state, 0, sizeof(state));
    EXPECT_TRUE(
        micros_pm_control_state_prepare(
            &state,
            2,
            process_endpoint(process),
            MICROS_PRIVILEGE_PROFILE_PM,
            process,
            thread
        ) == MICROS_PM_CONTROL_ERROR_ARGUMENT
        && micros_pm_control_state_prepare(
            &state,
            3,
            MICROS_ENDPOINT_NONE,
            MICROS_PRIVILEGE_PROFILE_PM,
            process,
            thread
        ) == MICROS_PM_CONTROL_ERROR_ARGUMENT
        && micros_pm_control_state_prepare(
            &state,
            3,
            process_endpoint(process),
            MICROS_PRIVILEGE_PROFILE_APPLICATION,
            process,
            thread
        ) == MICROS_PM_CONTROL_ERROR_ARGUMENT
    );
    return true;
}

static bool test_reserve_abort_lifecycle(void)
{
    struct micros_kernel_objects objects;
    struct micros_pm_control_state state;
    struct micros_pm_control_state state_snapshot;
    struct micros_kernel_objects objects_snapshot;
    struct micros_pm_control_reserve_plan reserve;
    struct micros_pm_control_reserve_plan reserve_sentinel;
    struct micros_pm_control_abort_plan abort;
    struct micros_pm_control_abort_plan abort_sentinel;
    const struct micros_process *process;

    EXPECT_TRUE(initialize_fixture(&objects, &state));
    memset(&reserve_sentinel, 0xa5, sizeof(reserve_sentinel));
    reserve = reserve_sentinel;
    EXPECT_TRUE(
        micros_pm_control_reserve_preflight(
            &state,
            &objects,
            &reserve
        ) == MICROS_PM_CONTROL_OK
        && reserve.process.slot == 3
        && reserve.process.generation == 1
        && reserve.transaction == 1
        && reserve.result.version == MICROS_PM_RESERVATION_VERSION
        && reserve.result.size == MICROS_PM_RESERVATION_SIZE
        && reserve.result.transaction == 1
        && reserve.result.reserved[0] == 0
        && reserve.result.reserved[1] == 0
    );
    micros_pm_control_reserve_commit_prevalidated(
        &state,
        &objects,
        &reserve
    );
    EXPECT_TRUE(
        state.last_transaction == 1
        && state.active_transaction == 1
        && state.reserved_process.slot == 3
        && state.reserved_process.generation == 1
        && objects.live_process_count == 4
        && micros_process_resolve(
            &objects,
            reserve.process,
            &process
        ) == MICROS_KERNEL_OBJECT_OK
        && process->live_thread_count == 0
        && process->address_space_root == 0
        && process->primary_endpoint == MICROS_ENDPOINT_NONE
        && process->privilege_profile == 0
        && !process->endpoint_lifecycle_consumed
        && micros_pm_control_state_validate_objects(
            &state,
            &objects
        ) == MICROS_PM_CONTROL_OK
    );

    state_snapshot = state;
    objects_snapshot = objects;
    reserve = reserve_sentinel;
    EXPECT_TRUE(
        micros_pm_control_reserve_preflight(
            &state,
            &objects,
            &reserve
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && memcmp(&reserve, &reserve_sentinel, sizeof(reserve)) == 0
        && memcmp(&state, &state_snapshot, sizeof(state)) == 0
        && memcmp(&objects, &objects_snapshot, sizeof(objects)) == 0
    );

    memset(&abort_sentinel, 0xa5, sizeof(abort_sentinel));
    abort = abort_sentinel;
    EXPECT_TRUE(
        micros_pm_control_abort_preflight(
            &state,
            &objects,
            2,
            &abort
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && memcmp(&abort, &abort_sentinel, sizeof(abort)) == 0
    );
    EXPECT_TRUE(
        micros_pm_control_abort_preflight(
            &state,
            &objects,
            1,
            &abort
        ) == MICROS_PM_CONTROL_OK
    );
    EXPECT_TRUE(
        abort.process.slot == 3
        && abort.process.generation == 1
        && abort.transaction == 1
        && !abort.quarantine
    );
    micros_pm_control_abort_commit_prevalidated(
        &state,
        &objects,
        &abort
    );
    EXPECT_TRUE(
        state.last_transaction == 1
        && state.active_transaction == 0
        && state.reserved_process.generation == 0
        && objects.live_process_count == 3
        && objects.processes[3].slot_state
            == MICROS_KERNEL_OBJECT_SLOT_FREE
        && objects.processes[3].generation == 1
        && micros_pm_control_state_validate_objects(
            &state,
            &objects
        ) == MICROS_PM_CONTROL_OK
    );
    abort = abort_sentinel;
    EXPECT_TRUE(
        micros_pm_control_abort_preflight(
            &state,
            &objects,
            1,
            &abort
        ) == MICROS_PM_CONTROL_ERROR_STATE
        && memcmp(&abort, &abort_sentinel, sizeof(abort)) == 0
    );
    EXPECT_TRUE(
        micros_pm_control_reserve_preflight(
            &state,
            &objects,
            &reserve
        ) == MICROS_PM_CONTROL_OK
        && reserve.process.slot == 3
        && reserve.process.generation == 2
        && reserve.transaction == 2
    );
    return true;
}

static bool test_capacity_and_quarantine(void)
{
    struct micros_kernel_objects objects;
    struct micros_kernel_objects snapshot;
    struct micros_pm_control_state state;
    struct micros_pm_control_state state_snapshot;
    struct micros_pm_control_reserve_plan reserve;
    struct micros_pm_control_reserve_plan sentinel;
    struct micros_pm_control_abort_plan abort;
    size_t index;

    EXPECT_TRUE(initialize_fixture(&objects, &state));
    memset(&sentinel, 0xa5, sizeof(sentinel));
    reserve = sentinel;
    state.last_transaction = UINT64_MAX;
    state_snapshot = state;
    snapshot = objects;
    EXPECT_TRUE(
        micros_pm_control_reserve_preflight(
            &state,
            &objects,
            &reserve
        ) == MICROS_PM_CONTROL_ERROR_CAPACITY
        && memcmp(&reserve, &sentinel, sizeof(reserve)) == 0
        && memcmp(&state, &state_snapshot, sizeof(state)) == 0
        && memcmp(&objects, &snapshot, sizeof(objects)) == 0
    );

    EXPECT_TRUE(initialize_fixture(&objects, &state));
    for (index = 3; index < MICROS_PROCESS_CAPACITY; ++index) {
        objects.processes[index].slot_state =
            MICROS_KERNEL_OBJECT_SLOT_QUARANTINED;
        objects.processes[index].generation =
            MICROS_PROCESS_GENERATION_MAX;
    }
    EXPECT_TRUE(
        micros_kernel_objects_validate(&objects)
            == MICROS_KERNEL_OBJECT_OK
    );
    reserve = sentinel;
    EXPECT_TRUE(
        micros_pm_control_reserve_preflight(
            &state,
            &objects,
            &reserve
        ) == MICROS_PM_CONTROL_ERROR_CAPACITY
        && memcmp(&reserve, &sentinel, sizeof(reserve)) == 0
    );

    EXPECT_TRUE(initialize_fixture(&objects, &state));
    objects.processes[3].generation =
        MICROS_PROCESS_GENERATION_MAX - 1;
    EXPECT_TRUE(
        micros_pm_control_reserve_preflight(
            &state,
            &objects,
            &reserve
        ) == MICROS_PM_CONTROL_OK
        && reserve.process.slot == 3
        && reserve.process.generation
            == MICROS_PROCESS_GENERATION_MAX
    );
    micros_pm_control_reserve_commit_prevalidated(
        &state,
        &objects,
        &reserve
    );
    EXPECT_TRUE(
        micros_pm_control_abort_preflight(
            &state,
            &objects,
            reserve.transaction,
            &abort
        ) == MICROS_PM_CONTROL_OK
        && abort.quarantine
    );
    micros_pm_control_abort_commit_prevalidated(
        &state,
        &objects,
        &abort
    );
    EXPECT_TRUE(
        objects.processes[3].slot_state
            == MICROS_KERNEL_OBJECT_SLOT_QUARANTINED
        && objects.processes[3].generation
            == MICROS_PROCESS_GENERATION_MAX
        && state.last_transaction == 1
        && micros_pm_control_reserve_preflight(
            &state,
            &objects,
            &reserve
        ) == MICROS_PM_CONTROL_OK
        && reserve.process.slot == 4
        && reserve.transaction == 2
    );
    return true;
}

static bool test_invariant_rejection(void)
{
    struct micros_kernel_objects objects;
    struct micros_pm_control_state state;
    struct micros_pm_control_reserve_plan reserve;
    struct micros_pm_control_abort_plan abort;

    EXPECT_TRUE(
        initialize_fixture(&objects, &state)
        && micros_pm_control_reserve_preflight(
            &state,
            &objects,
            &reserve
        ) == MICROS_PM_CONTROL_OK
    );
    micros_pm_control_reserve_commit_prevalidated(
        &state,
        &objects,
        &reserve
    );
    objects.processes[reserve.process.slot].address_space_root = 0x1000;
    EXPECT_TRUE(
        micros_pm_control_state_validate_objects(
            &state,
            &objects
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
        && micros_pm_control_abort_preflight(
            &state,
            &objects,
            reserve.transaction,
            &abort
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
    );
    objects.processes[reserve.process.slot].address_space_root = 0;
    ++state.active_transaction;
    EXPECT_TRUE(
        micros_pm_control_state_validate(&state)
            == MICROS_PM_CONTROL_ERROR_INVARIANT
    );
    return true;
}

bool micros_pm_control_test_run(void)
{
    return (
        test_contract_and_decode()
        && test_syscall_authority()
        && test_syscall_phase_and_retained_output()
        && test_state_preparation()
        && test_reserve_abort_lifecycle()
        && test_capacity_and_quarantine()
        && test_invariant_rejection()
    );
}
