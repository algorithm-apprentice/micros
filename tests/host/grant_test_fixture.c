#include "grant_test_internal.h"

static struct grant_test_fixture fixture_state;

struct grant_test_fixture *grant_test_context(void)
{
    return &fixture_state;
}

bool grant_test_setup_fixture(
    struct grant_test_fixture *fixture
)
{
    static const struct micros_privilege_profile profile = {
        .id = 1,
        .name = "GRANT_TEST",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
    };
    size_t index;

    memset(&fixture->endpoint_registry, 0, sizeof(fixture->endpoint_registry));
    memset(&fixture->grant_registry, 0, sizeof(fixture->grant_registry));
    memset(&fixture->objects, 0, sizeof(fixture->objects));
    memset(fixture->processes, 0, sizeof(fixture->processes));
    memset(fixture->threads, 0, sizeof(fixture->threads));
    memset(fixture->endpoints, 0, sizeof(fixture->endpoints));
    if (
        micros_endpoint_registry_initialize(
            &fixture->endpoint_registry,
            &profile,
            1
        ) != MICROS_ENDPOINT_OK
        || micros_grant_registry_initialize(&fixture->grant_registry)
            != MICROS_GRANT_OK
        || micros_kernel_objects_initialize(&fixture->objects, 1, 1)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&fixture->objects, 0, &fixture->hart)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            &fixture->objects,
            fixture->hart,
            UINT64_C(0x01000000),
            UINT64_C(0x01004000),
            UINT64_C(0x02000000),
            UINT64_C(0x02001000)
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    for (index = 0; index < GRANT_PROCESS_COUNT; ++index) {
        if (
            micros_process_create(&fixture->objects, &fixture->processes[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_thread_create(
                &fixture->objects,
                fixture->processes[index],
                &fixture->threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                &fixture->endpoint_registry,
                &fixture->objects,
                fixture->processes[index],
                &fixture->endpoints[index]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                &fixture->endpoint_registry,
                &fixture->objects,
                fixture->processes[index],
                1
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                &fixture->endpoint_registry,
                &fixture->objects,
                fixture->endpoints[index]
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
    }
    return (
        micros_grant_registry_validate(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects
        ) == MICROS_GRANT_OK
    );
}

bool grant_test_recreate_process(
    struct grant_test_fixture *fixture,
    size_t index
)
{
    return (
        micros_thread_release(&fixture->objects, fixture->threads[index])
            == MICROS_KERNEL_OBJECT_OK
        && micros_process_release(&fixture->objects, fixture->processes[index])
            == MICROS_KERNEL_OBJECT_OK
        && micros_process_create(&fixture->objects, &fixture->processes[index])
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_create(
            &fixture->objects,
            fixture->processes[index],
            &fixture->threads[index]
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_endpoint_reserve(
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[index],
            &fixture->endpoints[index]
        ) == MICROS_ENDPOINT_OK
        && micros_endpoint_install_profile(
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->processes[index],
            1
        ) == MICROS_ENDPOINT_OK
        && micros_endpoint_activate(
            &fixture->endpoint_registry,
            &fixture->objects,
            fixture->endpoints[index]
        ) == MICROS_ENDPOINT_OK
    );
}

bool grant_test_expect_create_failure_unchanged(
    struct grant_test_fixture *fixture,
    enum micros_grant_error expected,
    struct micros_process_handle grantor,
    micros_endpoint_t grantee,
    uintptr_t base,
    size_t length,
    uint32_t permissions
)
{
    struct micros_grant_registry snapshot = fixture->grant_registry;
    micros_grant_t grant = UINT32_C(0xdeadbeef);

    EXPECT_GRANT_ERROR(
        expected,
        micros_grant_create(
            &fixture->grant_registry,
            &fixture->endpoint_registry,
            &fixture->objects,
            grantor,
            grantee,
            base,
            length,
            permissions,
            &grant
        )
    );
    EXPECT_TRUE(
        grant == UINT32_C(0xdeadbeef)
        && grant_test_registries_equal(&fixture->grant_registry, &snapshot)
    );
    return true;
}
