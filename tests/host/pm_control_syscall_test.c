#include "tests/host/pm_control_test_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/endpoint.h"

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

struct authority_fixture {
    struct micros_kernel_objects objects;
    struct micros_pm_control_state state;
    struct micros_bootstrap_binding binding;
    struct micros_bootstrap_manifest_entry entry;
    struct micros_syscall_context context;
    struct micros_process process;
    struct micros_thread thread;
    struct micros_endpoint_record endpoint;
    struct micros_privilege_profile profile;
};

static bool initialize_authority_fixture(
    struct authority_fixture *fixture
)
{
    memset(fixture, 0, sizeof(*fixture));
    if (
        !pm_control_test_initialize_fixture(
            &fixture->objects,
            &fixture->state
        )
    ) {
        return false;
    }
    fixture->binding.service_id = fixture->state.service_id;
    fixture->binding.endpoint = fixture->state.endpoint;
    fixture->binding.process = fixture->state.process;
    fixture->binding.thread = fixture->state.thread;
    fixture->entry.service_id = fixture->state.service_id;
    fixture->entry.process_slot = fixture->state.process.slot;
    fixture->entry.profile_id = MICROS_PRIVILEGE_PROFILE_PM;
    fixture->entry.role_flags = MICROS_BOOTSTRAP_ROLE_PM;
    fixture->context.objects = &fixture->objects;
    fixture->context.current = fixture->state.thread;
    fixture->context.process = fixture->state.process;
    fixture->process =
        fixture->objects.processes[fixture->state.process.slot];
    fixture->thread =
        fixture->objects.threads[fixture->state.thread.slot];
    fixture->process.primary_endpoint = fixture->state.endpoint;
    fixture->process.privilege_profile =
        MICROS_PRIVILEGE_PROFILE_PM;
    fixture->endpoint.state = MICROS_ENDPOINT_STATE_ACTIVE;
    fixture->endpoint.owner = fixture->state.process;
    fixture->endpoint.value = fixture->state.endpoint;
    fixture->profile.id = MICROS_PRIVILEGE_PROFILE_PM;
    fixture->profile.kernel_operations =
        MICROS_KERNEL_OPERATION_PM_CONTROL;
    return true;
}

static enum micros_pm_control_authority_result classify_authority(
    const struct authority_fixture *fixture
)
{
    return micros_pm_control_syscall_authority_classify(
        &fixture->state,
        3,
        fixture->state.service_id,
        &fixture->binding,
        &fixture->entry,
        &fixture->context,
        &fixture->process,
        &fixture->thread,
        &fixture->endpoint,
        &fixture->profile
    );
}

static bool test_authority_baseline_and_caller_corruption(void)
{
    struct authority_fixture fixture;

    EXPECT_TRUE(initialize_authority_fixture(&fixture));
    EXPECT_TRUE(
        micros_pm_control_syscall_caller_classify(
            &fixture.state,
            &fixture.context,
            &fixture.process,
            &fixture.thread
        ) == MICROS_PM_CONTROL_AUTHORITY_AUTHORIZED
        && classify_authority(&fixture)
            == MICROS_PM_CONTROL_AUTHORITY_AUTHORIZED
    );
    ++fixture.context.process.generation;
    EXPECT_TRUE(
        classify_authority(&fixture)
            == MICROS_PM_CONTROL_AUTHORITY_INVARIANT
    );
    fixture.context.process = fixture.state.process;
    fixture.endpoint.sender_head = (struct micros_thread_handle){
        .slot = 0,
        .generation = 1,
    };
    fixture.profile.operations = UINT32_MAX;
    EXPECT_TRUE(
        classify_authority(&fixture)
            == MICROS_PM_CONTROL_AUTHORITY_AUTHORIZED
    );
    return true;
}

static bool test_authority_binding_rejections(void)
{
    struct authority_fixture fixture;

    EXPECT_TRUE(initialize_authority_fixture(&fixture));
    fixture.binding.endpoint +=
        UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS;
    fixture.state.endpoint = fixture.binding.endpoint;
    fixture.process.primary_endpoint = fixture.binding.endpoint;
    fixture.endpoint.value = fixture.binding.endpoint;
    EXPECT_TRUE(
        classify_authority(&fixture)
            == MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED
    );

    fixture.binding.endpoint -=
        UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS;
    fixture.state.endpoint = fixture.binding.endpoint;
    fixture.process.primary_endpoint = fixture.binding.endpoint;
    fixture.endpoint.value = fixture.binding.endpoint;
    ++fixture.endpoint.owner.generation;
    EXPECT_TRUE(
        classify_authority(&fixture)
            == MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED
    );
    fixture.endpoint.owner = fixture.state.process;
    fixture.entry.role_flags = 0;
    EXPECT_TRUE(
        classify_authority(&fixture)
            == MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED
    );
    fixture.entry.role_flags = MICROS_BOOTSTRAP_ROLE_PM;
    fixture.profile.kernel_operations |= UINT64_C(0x2);
    EXPECT_TRUE(
        classify_authority(&fixture)
            == MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED
        && micros_pm_control_syscall_authority_classify(
            &fixture.state,
            3,
            0,
            &fixture.binding,
            &fixture.entry,
            &fixture.context,
            &fixture.process,
            &fixture.thread,
            &fixture.endpoint,
            &fixture.profile
        ) == MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED
        && micros_pm_control_syscall_authority_classify(
            &fixture.state,
            MICROS_BOOTSTRAP_SERVICE_CAPACITY + 1,
            fixture.state.service_id,
            &fixture.binding,
            &fixture.entry,
            &fixture.context,
            &fixture.process,
            &fixture.thread,
            &fixture.endpoint,
            &fixture.profile
        ) == MICROS_PM_CONTROL_AUTHORITY_INVARIANT
    );
    return true;
}

bool test_syscall_authority(void)
{
    return (
        test_authority_baseline_and_caller_corruption()
        && test_authority_binding_rejections()
    );
}

struct authority_resolution_fixture {
    struct micros_kernel_objects objects;
    struct micros_pm_control_state state;
    struct micros_bootstrap_control_state bootstrap;
    struct micros_endpoint_registry registry;
    struct micros_syscall_context context;
    struct micros_process_handle foreign_process;
    struct micros_thread_handle foreign_thread;
};

static bool initialize_authority_resolution_fixture(
    struct authority_resolution_fixture *fixture
)
{
    struct micros_bootstrap_binding *binding;
    struct micros_bootstrap_manifest_entry *entry;
    struct micros_endpoint_record *endpoint;
    struct micros_privilege_profile *profile;

    memset(fixture, 0, sizeof(*fixture));
    if (
        !pm_control_test_initialize_fixture(
            &fixture->objects,
            &fixture->state
        )
    ) {
        return false;
    }
    fixture->foreign_process = (struct micros_process_handle){
        .slot = 1,
        .generation = fixture->objects.processes[1].generation,
    };
    if (
        micros_thread_create(
            &fixture->objects,
            fixture->foreign_process,
            &fixture->foreign_thread
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    fixture->objects.processes[fixture->state.process.slot]
        .primary_endpoint = fixture->state.endpoint;
    fixture->objects.processes[fixture->state.process.slot]
        .privilege_profile = MICROS_PRIVILEGE_PROFILE_PM;
    fixture->bootstrap.entry_count = 1;
    fixture->bootstrap.plan.pm_service_id =
        fixture->state.service_id;
    binding = &fixture->bootstrap.bindings[0];
    binding->service_id = fixture->state.service_id;
    binding->endpoint = fixture->state.endpoint;
    binding->process = fixture->state.process;
    binding->thread = fixture->state.thread;
    entry = &fixture->bootstrap.manifest.entries[0];
    entry->service_id = fixture->state.service_id;
    entry->process_slot = fixture->state.process.slot;
    entry->profile_id = MICROS_PRIVILEGE_PROFILE_PM;
    entry->role_flags = MICROS_BOOTSTRAP_ROLE_PM;
    endpoint =
        &fixture->registry.endpoints[fixture->state.process.slot];
    endpoint->state = MICROS_ENDPOINT_STATE_ACTIVE;
    endpoint->owner = fixture->state.process;
    endpoint->value = fixture->state.endpoint;
    profile =
        &fixture->registry.profiles[MICROS_PRIVILEGE_PROFILE_PM];
    profile->id = MICROS_PRIVILEGE_PROFILE_PM;
    profile->kernel_operations = MICROS_KERNEL_OPERATION_PM_CONTROL;
    fixture->registry.profile_count =
        MICROS_PRIVILEGE_PROFILE_CAPACITY + 1;
    fixture->context.objects = &fixture->objects;
    fixture->context.process = fixture->state.process;
    fixture->context.current = fixture->state.thread;
    return true;
}

static enum micros_pm_control_authority_result resolve_authority(
    struct authority_resolution_fixture *fixture
)
{
    return micros_pm_control_syscall_authority_resolve(
        &fixture->state,
        &fixture->bootstrap,
        &fixture->registry,
        &fixture->objects,
        &fixture->context
    );
}

bool test_syscall_authority_resolution(void)
{
    struct authority_resolution_fixture fixture;
    struct micros_endpoint_record *endpoint;

    EXPECT_TRUE(initialize_authority_resolution_fixture(&fixture));
    endpoint =
        &fixture.registry.endpoints[fixture.state.process.slot];
    EXPECT_TRUE(
        resolve_authority(&fixture)
            == MICROS_PM_CONTROL_AUTHORITY_AUTHORIZED
    );
    fixture.context.process = fixture.foreign_process;
    fixture.context.current = fixture.foreign_thread;
    fixture.bootstrap.entry_count =
        MICROS_BOOTSTRAP_SERVICE_CAPACITY + 1;
    endpoint->state = MICROS_ENDPOINT_STATE_RESERVED;
    EXPECT_TRUE(
        resolve_authority(&fixture)
            == MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED
    );
    fixture.bootstrap.entry_count = 1;
    EXPECT_TRUE(
        resolve_authority(&fixture)
            == MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED
    );
    fixture.objects.threads[fixture.foreign_thread.slot].owner =
        fixture.state.process;
    EXPECT_TRUE(
        resolve_authority(&fixture)
            == MICROS_PM_CONTROL_AUTHORITY_INVARIANT
    );
    fixture.objects.threads[fixture.foreign_thread.slot].owner =
        fixture.foreign_process;
    fixture.context.process = fixture.state.process;
    fixture.context.current = fixture.state.thread;
    fixture.bootstrap.entry_count =
        MICROS_BOOTSTRAP_SERVICE_CAPACITY + 1;
    EXPECT_TRUE(
        resolve_authority(&fixture)
            == MICROS_PM_CONTROL_AUTHORITY_INVARIANT
    );
    fixture.bootstrap.entry_count = 1;
    EXPECT_TRUE(
        resolve_authority(&fixture)
            == MICROS_PM_CONTROL_AUTHORITY_UNAUTHORIZED
    );
    return true;
}

static bool test_phase_classification(void)
{
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
    return true;
}

static bool test_output_publication(void)
{
    struct pm_control_output_translation_fixture fixture;
    struct micros_pm_control_output_plan plan;
    struct micros_pm_reservation_result result = {
        .version = MICROS_PM_PROTOCOL_VERSION,
        .size = sizeof(struct micros_pm_reservation_result),
        .transaction = UINT64_C(0x0102030405060708),
        .reserved = {0, 0},
    };
    const unsigned char *result_bytes =
        (const unsigned char *)&result;

    pm_control_test_initialize_output_fixture(&fixture);
    fixture.chunk_count = 1;
    fixture.chunk_sizes[0] = sizeof(result);
    EXPECT_TRUE(
        micros_pm_control_output_prepare(
            fixture.base,
            pm_control_test_fake_output_translate,
            &fixture,
            &plan
        ) == MICROS_PM_CONTROL_OUTPUT_OK
        && plan.chunk_count == 1
        && plan.chunks[0].size == sizeof(result)
        && pm_control_test_output_chunks_are(&fixture, 0xa5)
    );
    micros_pm_control_output_commit_prevalidated(&plan, &result);
    EXPECT_TRUE(
        memcmp(fixture.chunks[0], result_bytes, sizeof(result)) == 0
    );

    pm_control_test_initialize_output_fixture(&fixture);
    fixture.chunk_count = 2;
    fixture.chunk_sizes[0] = 8;
    fixture.chunk_sizes[1] = sizeof(result) - 8;
    EXPECT_TRUE(
        micros_pm_control_output_prepare(
            fixture.base,
            pm_control_test_fake_output_translate,
            &fixture,
            &plan
        ) == MICROS_PM_CONTROL_OUTPUT_OK
        && plan.chunk_count == 2
        && plan.chunks[0].size == 8
        && plan.chunks[1].size == sizeof(result) - 8
        && pm_control_test_output_chunks_are(&fixture, 0xa5)
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
    return true;
}

static bool output_failure_is_preserved(
    struct pm_control_output_translation_fixture *fixture,
    enum micros_pm_control_output_error expected
)
{
    struct micros_pm_control_output_plan plan;
    struct micros_pm_control_output_plan sentinel;

    memset(&sentinel, 0x5a, sizeof(sentinel));
    plan = sentinel;
    return (
        micros_pm_control_output_prepare(
            fixture->base,
            pm_control_test_fake_output_translate,
            fixture,
            &plan
        ) == expected
        && memcmp(&plan, &sentinel, sizeof(plan)) == 0
        && pm_control_test_output_chunks_are(fixture, 0xa5)
    );
}

static bool test_output_failure_preservation(void)
{
    struct pm_control_output_translation_fixture fixture;

    pm_control_test_initialize_output_fixture(&fixture);
    fixture.chunk_count = 2;
    fixture.chunk_sizes[0] = 8;
    fixture.chunk_sizes[1] =
        sizeof(struct micros_pm_reservation_result) - 8;
    fixture.fault_call = 1;
    EXPECT_TRUE(
        output_failure_is_preserved(
            &fixture,
            MICROS_PM_CONTROL_OUTPUT_ERROR_MEMORY_FAULT
        )
    );

    pm_control_test_initialize_output_fixture(&fixture);
    fixture.chunk_count = 1;
    fixture.chunk_sizes[0] = 0;
    EXPECT_TRUE(
        output_failure_is_preserved(
            &fixture,
            MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT
        )
    );

    pm_control_test_initialize_output_fixture(&fixture);
    fixture.chunk_count = 3;
    fixture.chunk_sizes[0] = 8;
    fixture.chunk_sizes[1] = 8;
    fixture.chunk_sizes[2] =
        sizeof(struct micros_pm_reservation_result) - 16;
    EXPECT_TRUE(
        output_failure_is_preserved(
            &fixture,
            MICROS_PM_CONTROL_OUTPUT_ERROR_INVARIANT
        )
    );
    return true;
}

static bool test_preflight_failure_preserves_output(void)
{
    struct pm_control_output_translation_fixture fixture;
    struct micros_kernel_objects objects;
    struct micros_pm_control_state state;
    struct micros_pm_control_reserve_plan reserve;
    struct micros_pm_control_output_plan plan;

    pm_control_test_initialize_output_fixture(&fixture);
    fixture.chunk_count = 1;
    fixture.chunk_sizes[0] =
        sizeof(struct micros_pm_reservation_result);
    EXPECT_TRUE(
        pm_control_test_initialize_fixture(&objects, &state)
        && micros_pm_control_output_prepare(
            fixture.base,
            pm_control_test_fake_output_translate,
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
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
        && pm_control_test_output_chunks_are(&fixture, 0xa5)
    );
    return true;
}

bool test_syscall_phase_and_retained_output(void)
{
    return (
        test_phase_classification()
        && test_output_publication()
        && test_output_failure_preservation()
        && test_preflight_failure_preserves_output()
    );
}
