#include "endpoint_test_internal.h"
#include "micros/scheduler_core.h"

#include <string.h>

struct endpoint_held_case {
    micros_endpoint_t endpoint;
    struct micros_user_context context;
};

struct endpoint_lifecycle_case {
    micros_endpoint_t endpoints[2];
    micros_endpoint_t stale_endpoint;
    struct micros_process_handle stale_target;
    const struct micros_endpoint_record *record;
};

static bool endpoint_test_lifecycle_hidden_reservation(
    struct endpoint_test_fixture *fixture,
    struct endpoint_lifecycle_case *work
)
{
    EXPECT_TRUE(endpoint_test_setup_lifecycle_fixture(fixture));
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_reserve(
            &fixture->registry,
            &fixture->objects,
            fixture->processes[0],
            &work->endpoints[0]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_resolve_internal(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0],
            &work->record
        )
    );
    EXPECT_TRUE(work->record->state == MICROS_ENDPOINT_STATE_RESERVED);
    work->record = (const struct micros_endpoint_record *)(uintptr_t)1;
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_resolve_active(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0],
            &work->record
        )
    );
    EXPECT_TRUE(
        work->record
            == (const struct micros_endpoint_record *)(uintptr_t)1
    );
    return true;
}

static bool endpoint_test_lifecycle_profile_publication(
    struct endpoint_test_fixture *fixture,
    struct endpoint_lifecycle_case *work
)
{
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_reserve(
            &fixture->registry,
            &fixture->objects,
            fixture->processes[1],
            &work->endpoints[1]
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(&fixture->objects, fixture->threads[0])
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_release(&fixture->objects, fixture->processes[0])
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(
            &fixture->objects,
            fixture->processes[0],
            &fixture->threads[0]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_install_profile(
            &fixture->registry,
            &fixture->objects,
            fixture->processes[0],
            1
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_install_profile(
            &fixture->registry,
            &fixture->objects,
            fixture->processes[1],
            2
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_activate(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_activate(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[1]
        )
    );
    return true;
}

static bool endpoint_test_lifecycle_asymmetric_authority(
    struct endpoint_test_fixture *fixture,
    struct endpoint_lifecycle_case *work
)
{
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_authorize_target(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0],
            MICROS_PRIVILEGE_OPERATION_CALL,
            work->endpoints[1]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_UNAUTHORIZED,
        micros_endpoint_authorize_target(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0],
            MICROS_PRIVILEGE_OPERATION_SEND,
            work->endpoints[1]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_UNAUTHORIZED,
        micros_endpoint_authorize_target(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            work->endpoints[1]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_authorize_target(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            work->endpoints[0]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_UNAUTHORIZED,
        micros_endpoint_authorize_target(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[1],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            work->endpoints[0]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_authorize_target(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[1],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            work->endpoints[1]
        )
    );
    return true;
}

static bool endpoint_test_lifecycle_stale_and_operation_precedence(
    struct endpoint_test_fixture *fixture,
    struct endpoint_lifecycle_case *work
)
{
    work->stale_target.slot = fixture->processes[1].slot;
    work->stale_target.generation = fixture->processes[1].generation + 1;
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_pack(work->stale_target, &work->stale_endpoint)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STALE,
        micros_endpoint_authorize_target(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0],
            MICROS_PRIVILEGE_OPERATION_SEND,
            work->stale_endpoint
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ARGUMENT,
        micros_endpoint_authorize_target(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0],
            0,
            work->stale_endpoint
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_authorize_operation(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[1],
            MICROS_PRIVILEGE_OPERATION_REPLY
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_authorize_kernel_operation(
            &fixture->registry,
            &fixture->objects,
            work->endpoints[0],
            3
        )
    );
    return true;
}

static bool endpoint_test_lifecycle_final_relationship(
    struct endpoint_test_fixture *fixture
)
{
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(&fixture->objects, fixture->threads[0])
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_release(&fixture->objects, fixture->processes[0])
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_validate_objects(
            &fixture->registry,
            &fixture->objects
        )
    );
    return true;
}

bool endpoint_test_lifecycle_and_authorization(void)
{
    struct endpoint_test_fixture *fixture = endpoint_test_context();
    struct endpoint_lifecycle_case work;

    work.endpoints[0] = 0;
    work.endpoints[1] = 0;
    work.record = NULL;
    return endpoint_test_lifecycle_hidden_reservation(fixture, &work)
        && endpoint_test_lifecycle_profile_publication(fixture, &work)
        && endpoint_test_lifecycle_asymmetric_authority(fixture, &work)
        && endpoint_test_lifecycle_stale_and_operation_precedence(fixture, &work)
        && endpoint_test_lifecycle_final_relationship(fixture);
}

static bool endpoint_test_held_installation_denied(
    struct endpoint_test_fixture *fixture,
    struct endpoint_held_case *work
)
{
    EXPECT_TRUE(endpoint_test_setup_lifecycle_fixture(fixture));
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_reserve(
            &fixture->registry,
            &fixture->objects,
            fixture->processes[0],
            &work->endpoint
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_attach_execution_context(
            &fixture->objects,
            fixture->threads[0],
            UINT64_C(0x10000000),
            UINT64_C(0x10004000),
            &work->context
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects,
            fixture->hart,
            fixture->threads[0],
            7,
            100,
            true
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_install_profile(
            &fixture->registry,
            &fixture->objects,
            fixture->processes[0],
            1
        )
    );
    return true;
}

static bool endpoint_test_held_profile_publication(
    struct endpoint_test_fixture *fixture,
    struct endpoint_held_case *work
)
{
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&fixture->objects, fixture->threads[0])
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_remove(&fixture->objects, fixture->threads[0])
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_install_profile(
            &fixture->registry,
            &fixture->objects,
            fixture->processes[0],
            1
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_activate(
            &fixture->registry,
            &fixture->objects,
            work->endpoint
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects,
            fixture->hart,
            fixture->threads[0],
            7,
            100,
            true
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            work->endpoint
        )
    );
    return true;
}

static bool endpoint_test_held_close_and_cleanup(
    struct endpoint_test_fixture *fixture,
    struct endpoint_held_case *work
)
{
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&fixture->objects, fixture->threads[0])
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            work->endpoint
        )
    );
    EXPECT_TRUE(
        fixture->objects.processes[fixture->processes[0].slot].primary_endpoint
            == MICROS_PROCESS_ENDPOINT_NONE
        && fixture->objects.processes[fixture->processes[0].slot].privilege_profile == 0
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_reserve(
            &fixture->registry,
            &fixture->objects,
            fixture->processes[0],
            &work->endpoint
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_remove(&fixture->objects, fixture->threads[0])
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_detach_execution_context(&fixture->objects, fixture->threads[0])
    );
    return true;
}

bool endpoint_test_profile_install_and_close_require_held_threads(void)
{
    struct endpoint_test_fixture *fixture = endpoint_test_context();
    struct endpoint_held_case work;

    work.context = endpoint_test_context_pattern(UINT64_C(0x1000));
    return endpoint_test_held_installation_denied(fixture, &work)
        && endpoint_test_held_profile_publication(fixture, &work)
        && endpoint_test_held_close_and_cleanup(fixture, &work);
}

bool endpoint_test_reuse_rejects_stale_generation(void)
{
    struct endpoint_test_fixture *fixture = endpoint_test_context();
    micros_endpoint_t first_endpoint;
    micros_endpoint_t replacement_endpoint;
    struct micros_process_handle replacement;
    const struct micros_endpoint_record *record = NULL;

    EXPECT_TRUE(endpoint_test_setup_lifecycle_fixture(fixture));
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_reserve(
            &fixture->registry,
            &fixture->objects,
            fixture->processes[0],
            &first_endpoint
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_close(
            &fixture->registry,
            &fixture->objects,
            first_endpoint
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(&fixture->objects, fixture->threads[0])
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(&fixture->objects, fixture->processes[0])
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&fixture->objects, &replacement)
    );
    EXPECT_TRUE(
        replacement.slot == fixture->processes[0].slot
        && replacement.generation == fixture->processes[0].generation + 1
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_reserve(
            &fixture->registry,
            &fixture->objects,
            replacement,
            &replacement_endpoint
        )
    );
    EXPECT_TRUE(replacement_endpoint != first_endpoint);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STALE,
        micros_endpoint_resolve_internal(
            &fixture->registry,
            &fixture->objects,
            first_endpoint,
            &record
        )
    );
    return true;
}
