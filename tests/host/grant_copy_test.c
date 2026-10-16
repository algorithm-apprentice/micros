#include "micros/grant_copy.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/user_address_space.h"

enum {
    COPY_PROCESS_COUNT = 3,
};

static struct micros_endpoint_registry endpoint_registry;
static struct micros_grant_registry grant_registry;
static struct micros_kernel_objects objects;
static struct micros_process_handle processes[COPY_PROCESS_COUNT];
static struct micros_thread_handle threads[COPY_PROCESS_COUNT];
static micros_endpoint_t endpoints[COPY_PROCESS_COUNT];

bool micros_grant_copy_test_run(void);

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

#define EXPECT_GRANT_ERROR(expected, expression) \
    do { \
        enum micros_grant_error actual = (expression); \
        if (actual != (expected)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %d got %d\n", \
                __FILE__, \
                __LINE__, \
                (int)(expected), \
                (int)actual \
            ); \
            return false; \
        } \
    } while (false)

static bool process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool authority_equal(
    const struct micros_grant_copy_authority *left,
    const struct micros_grant_copy_authority *right
)
{
    return (
        process_handles_equal(left->grantor, right->grantor)
        && process_handles_equal(left->grantee, right->grantee)
        && left->remote_address == right->remote_address
        && left->length == right->length
        && left->required_permission == right->required_permission
    );
}

static bool setup_fixture(void)
{
    static const struct micros_privilege_profile profile = {
        .id = 1,
        .name = "COPY_TEST",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
    };
    size_t index;

    memset(&endpoint_registry, 0, sizeof(endpoint_registry));
    memset(&grant_registry, 0, sizeof(grant_registry));
    memset(&objects, 0, sizeof(objects));
    memset(processes, 0, sizeof(processes));
    memset(threads, 0, sizeof(threads));
    memset(endpoints, 0, sizeof(endpoints));
    if (
        micros_endpoint_registry_initialize(
            &endpoint_registry,
            &profile,
            1
        ) != MICROS_ENDPOINT_OK
        || micros_grant_registry_initialize(&grant_registry)
            != MICROS_GRANT_OK
        || micros_kernel_objects_initialize(&objects, 1, 1)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    for (index = 0; index < COPY_PROCESS_COUNT; ++index) {
        if (
            micros_process_create(&objects, &processes[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_thread_create(
                &objects,
                processes[index],
                &threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                &endpoint_registry,
                &objects,
                processes[index],
                &endpoints[index]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                &endpoint_registry,
                &objects,
                processes[index],
                1
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                &endpoint_registry,
                &objects,
                endpoints[index]
            ) != MICROS_ENDPOINT_OK
        ) {
            return false;
        }
    }
    return true;
}

static bool create_grant(
    size_t grantor,
    size_t grantee,
    uintptr_t base,
    size_t length,
    uint32_t permissions,
    micros_grant_t *grant
)
{
    return (
        micros_grant_create(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[grantor],
            endpoints[grantee],
            base,
            length,
            permissions,
            grant
        ) == MICROS_GRANT_OK
    );
}

static bool expect_authority_failure_unchanged(
    enum micros_grant_error expected,
    struct micros_process_handle grantee,
    micros_endpoint_t grantor_endpoint,
    micros_grant_t grant,
    size_t grant_offset,
    size_t length,
    uint32_t permission
)
{
    struct micros_grant_copy_authority sentinel;
    struct micros_grant_copy_authority observed;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    observed = sentinel;
    EXPECT_GRANT_ERROR(
        expected,
        micros_grant_prepare_copy_authority(
            &grant_registry,
            &endpoint_registry,
            &objects,
            grantee,
            grantor_endpoint,
            grant,
            grant_offset,
            length,
            permission,
            &observed
        )
    );
    EXPECT_TRUE(authority_equal(&observed, &sentinel));
    return true;
}

struct grant_copy_authority_case {
    struct micros_grant_copy_authority authority;
    micros_grant_t read_grant;
    micros_grant_t write_grant;
    micros_grant_t both_grant;
};

static bool grant_copy_test_directional_grants(struct grant_copy_authority_case *work)
{
    EXPECT_TRUE(
        setup_fixture()
        && create_grant(
            0,
            1,
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x1000),
            MICROS_GRANT_COPY_MAX * 2,
            MICROS_GRANT_PERMISSION_READ,
            &work->read_grant
        )
        && create_grant(
            0,
            1,
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x4000),
            MICROS_GRANT_COPY_MAX,
            MICROS_GRANT_PERMISSION_WRITE,
            &work->write_grant
        )
        && create_grant(
            0,
            1,
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x6000),
            MICROS_GRANT_COPY_MAX,
            MICROS_GRANT_PERMISSION_READ
                | MICROS_GRANT_PERMISSION_WRITE,
            &work->both_grant
        )
    );
    return true;
}

static bool grant_copy_test_authority_and_end_zero(struct grant_copy_authority_case *work)
{
    memset(&work->authority, 0, sizeof(work->authority));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_prepare_copy_authority(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[1],
            endpoints[0],
            work->read_grant,
            128,
            MICROS_GRANT_COPY_MAX,
            MICROS_GRANT_PERMISSION_READ,
            &work->authority
        )
    );
    EXPECT_TRUE(
        process_handles_equal(work->authority.grantor, processes[0])
        && process_handles_equal(work->authority.grantee, processes[1])
        && work->authority.remote_address
            == MICROS_USER_VIRTUAL_BASE + UINT64_C(0x1080)
        && work->authority.length == MICROS_GRANT_COPY_MAX
        && work->authority.required_permission
            == MICROS_GRANT_PERMISSION_READ
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_prepare_copy_authority(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[1],
            endpoints[0],
            work->both_grant,
            MICROS_GRANT_COPY_MAX,
            0,
            MICROS_GRANT_PERMISSION_WRITE,
            &work->authority
        )
    );
    EXPECT_TRUE(
        work->authority.remote_address
            == MICROS_USER_VIRTUAL_BASE + UINT64_C(0x7000)
        && work->authority.length == 0
    );
    return true;
}

static bool grant_copy_test_participant_bounds_shape(struct grant_copy_authority_case *work)
{
    EXPECT_TRUE(expect_authority_failure_unchanged(
        MICROS_GRANT_ERROR_UNAUTHORIZED,
        processes[1],
        endpoints[0],
        work->read_grant,
        0,
        1,
        MICROS_GRANT_PERMISSION_WRITE
    ));
    EXPECT_TRUE(expect_authority_failure_unchanged(
        MICROS_GRANT_ERROR_UNAUTHORIZED,
        processes[2],
        endpoints[0]
            + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS),
        work->read_grant,
        0,
        1,
        MICROS_GRANT_PERMISSION_READ
    ));
    EXPECT_TRUE(expect_authority_failure_unchanged(
        MICROS_GRANT_ERROR_UNAUTHORIZED,
        processes[1],
        endpoints[2],
        work->read_grant,
        0,
        1,
        MICROS_GRANT_PERMISSION_READ
    ));
    EXPECT_TRUE(expect_authority_failure_unchanged(
        MICROS_GRANT_ERROR_RANGE,
        processes[1],
        endpoints[0],
        work->write_grant,
        MICROS_GRANT_COPY_MAX,
        1,
        MICROS_GRANT_PERMISSION_WRITE
    ));
    EXPECT_TRUE(expect_authority_failure_unchanged(
        MICROS_GRANT_ERROR_RANGE,
        processes[1],
        endpoints[0],
        work->read_grant,
        0,
        MICROS_GRANT_COPY_MAX + 1,
        MICROS_GRANT_PERMISSION_READ
    ));
    EXPECT_TRUE(expect_authority_failure_unchanged(
        MICROS_GRANT_ERROR_RANGE,
        processes[1],
        endpoints[0],
        work->read_grant,
        SIZE_MAX,
        1,
        MICROS_GRANT_PERMISSION_READ
    ));
    EXPECT_TRUE(expect_authority_failure_unchanged(
        MICROS_GRANT_ERROR_ARGUMENT,
        processes[1],
        endpoints[0],
        MICROS_GRANT_NONE,
        0,
        1,
        MICROS_GRANT_PERMISSION_READ
    ));
    EXPECT_TRUE(expect_authority_failure_unchanged(
        MICROS_GRANT_ERROR_ARGUMENT,
        processes[1],
        MICROS_ENDPOINT_NONE,
        work->read_grant,
        0,
        1,
        MICROS_GRANT_PERMISSION_READ
    ));
    return true;
}

static bool grant_copy_test_revoked_precedence(struct grant_copy_authority_case *work)
{
    EXPECT_TRUE(
        micros_grant_revoke(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            work->read_grant
        ) == MICROS_GRANT_OK
        && expect_authority_failure_unchanged(
            MICROS_GRANT_ERROR_STALE_GRANT,
            processes[1],
            endpoints[0],
            work->read_grant,
            0,
            1,
            MICROS_GRANT_PERMISSION_READ
        )
        && expect_authority_failure_unchanged(
            MICROS_GRANT_ERROR_RANGE,
            processes[1],
            endpoints[0],
            work->read_grant,
            SIZE_MAX,
            1,
            MICROS_GRANT_PERMISSION_READ
        )
        && expect_authority_failure_unchanged(
            MICROS_GRANT_ERROR_STALE_GRANT,
            processes[1],
            endpoints[0],
            work->read_grant,
            0,
            0,
            MICROS_GRANT_PERMISSION_READ
        )
    );
    return true;
}

static bool test_copy_authority_and_bounds(void)
{
    struct grant_copy_authority_case work;

    if (!grant_copy_test_directional_grants(&work)) {
        return false;
    }
    if (!grant_copy_test_authority_and_end_zero(&work)) {
        return false;
    }
    if (!grant_copy_test_participant_bounds_shape(&work)) {
        return false;
    }
    return grant_copy_test_revoked_precedence(&work);
}

struct grant_copy_plan_case {
    unsigned char source[16];
    unsigned char destination[16];
    unsigned char unchanged[16];
    struct micros_grant_copy_range_plan source_plan;
    struct micros_grant_copy_range_plan destination_plan;
    size_t index;
};

static bool grant_copy_test_prepare_chunks(struct grant_copy_plan_case *work)
{
    for (work->index = 0; work->index < sizeof(work->source); ++work->index) {
        work->source[work->index] = (unsigned char)(UINT8_C(0x20) + work->index);
        work->destination[work->index] = UINT8_C(0xcc);
        work->unchanged[work->index] = work->destination[work->index];
    }
    work->source_plan.chunk_count = 2;
    work->source_plan.chunks[0] = (struct micros_grant_copy_chunk){
        .physical_address = (uintptr_t)&work->source[0],
        .length = 5,
    };
    work->source_plan.chunks[1] = (struct micros_grant_copy_chunk){
        .physical_address = (uintptr_t)&work->source[5],
        .length = 7,
    };
    work->destination_plan.chunk_count = 2;
    work->destination_plan.chunks[0] = (struct micros_grant_copy_chunk){
        .physical_address = (uintptr_t)&work->destination[0],
        .length = 3,
    };
    work->destination_plan.chunks[1] = (struct micros_grant_copy_chunk){
        .physical_address = (uintptr_t)&work->destination[3],
        .length = 9,
    };
    return true;
}

static bool grant_copy_test_commit_bytes(struct grant_copy_plan_case *work)
{
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_copy_plan_validate(
            &work->source_plan,
            &work->destination_plan,
            12
        )
    );
    micros_grant_copy_commit(
        &work->source_plan,
        &work->destination_plan,
        12
    );
    EXPECT_TRUE(
        memcmp(work->source, work->destination, 12) == 0
        && work->destination[12] == work->unchanged[12]
    );
    return true;
}

static bool grant_copy_test_reject_physical_plans(struct grant_copy_plan_case *work)
{
    memset(&work->source_plan, 0, sizeof(work->source_plan));
    memset(&work->destination_plan, 0, sizeof(work->destination_plan));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_copy_plan_validate(
            &work->source_plan,
            &work->destination_plan,
            0
        )
    );
    work->source_plan.chunk_count = 1;
    work->source_plan.chunks[0] = (struct micros_grant_copy_chunk){
        .physical_address = (uintptr_t)&work->source[0],
        .length = 8,
    };
    work->destination_plan.chunk_count = 1;
    work->destination_plan.chunks[0] = (struct micros_grant_copy_chunk){
        .physical_address = (uintptr_t)&work->source[4],
        .length = 8,
    };
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_INVARIANT,
        micros_grant_copy_plan_validate(
            &work->source_plan,
            &work->destination_plan,
            8
        )
    );
    work->destination_plan.chunks[0].physical_address =
        (uintptr_t)&work->destination[0];
    work->source_plan.chunks[0].length = 7;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_INVARIANT,
        micros_grant_copy_plan_validate(
            &work->source_plan,
            &work->destination_plan,
            8
        )
    );
    work->source_plan.chunks[0].length = 8;
    work->source_plan.chunks[0].physical_address = UINT64_MAX - 3;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_INVARIANT,
        micros_grant_copy_plan_validate(
            &work->source_plan,
            &work->destination_plan,
            8
        )
    );
    return true;
}

static bool test_copy_plan_and_commit(void)
{
    struct grant_copy_plan_case work;
    work.source_plan = (struct micros_grant_copy_range_plan){0};
    work.destination_plan = (struct micros_grant_copy_range_plan){0};

    if (!grant_copy_test_prepare_chunks(&work)) {
        return false;
    }
    if (!grant_copy_test_commit_bytes(&work)) {
        return false;
    }
    return grant_copy_test_reject_physical_plans(&work);
}

bool micros_grant_copy_test_run(void)
{
    return (
        test_copy_authority_and_bounds()
        && test_copy_plan_and_commit()
    );
}
