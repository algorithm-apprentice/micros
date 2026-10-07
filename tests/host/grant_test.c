#include "micros/grant.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/grant_copy.h"
#include "micros/ipc_core.h"
#include "micros/scheduler_core.h"
#include "micros/user_address_space.h"

enum {
    GRANT_PROCESS_COUNT = 3,
    GRANT_MODEL_STEPS = 4096,
    GRANT_MODEL_TRACE_COUNT = 32,
};

enum grant_model_operation {
    GRANT_MODEL_CREATE = 0,
    GRANT_MODEL_AUTHORIZE_FROM,
    GRANT_MODEL_AUTHORIZE_TO,
    GRANT_MODEL_AUTHORIZE_DENIAL,
    GRANT_MODEL_AUTHORIZE_STALE,
    GRANT_MODEL_AUTHORIZE_BOUNDS,
    GRANT_MODEL_REVOKE,
    GRANT_MODEL_INSPECT,
    GRANT_MODEL_CANCEL,
    GRANT_MODEL_CLOSE_REUSE,
    GRANT_MODEL_MALFORMED_TOKEN,
    GRANT_MODEL_PLAN_FAILURE,
    GRANT_MODEL_INVALID_CREATE,
    GRANT_MODEL_OPERATION_COUNT,
};

struct grant_model_trace_entry {
    size_t step;
    enum grant_model_operation operation;
    size_t grantor;
    size_t grantee;
    micros_grant_t grant;
    size_t offset;
    size_t length;
    uint32_t permission;
    enum micros_grant_error result;
};

static struct micros_endpoint_registry endpoint_registry;
static struct micros_grant_registry grant_registry;
static struct micros_kernel_objects objects;
static struct micros_process_handle processes[GRANT_PROCESS_COUNT];
static struct micros_thread_handle threads[GRANT_PROCESS_COUNT];
static micros_endpoint_t endpoints[GRANT_PROCESS_COUNT];
static struct micros_hart_handle hart;
static struct grant_model_trace_entry
    model_trace[GRANT_MODEL_TRACE_COUNT];
static size_t model_trace_count;

bool micros_grant_test_run(void);
bool micros_grant_model_test_run(void);

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

static bool records_equal(
    const struct micros_grant_record *left,
    const struct micros_grant_record *right
)
{
    return (
        left->state == right->state
        && left->generation == right->generation
        && process_handles_equal(left->grantor, right->grantor)
        && left->grantor_endpoint == right->grantor_endpoint
        && left->grantee_endpoint == right->grantee_endpoint
        && left->base == right->base
        && left->length == right->length
        && left->permissions == right->permissions
    );
}

static bool registries_equal(
    const struct micros_grant_registry *left,
    const struct micros_grant_registry *right
)
{
    size_t index;

    if (
        left->initialization_magic != right->initialization_magic
        || left->active_count != right->active_count
    ) {
        return false;
    }
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        if (!records_equal(
            &left->grants[index],
            &right->grants[index]
        )) {
            return false;
        }
    }
    return true;
}

static bool setup_fixture(void)
{
    static const struct micros_privilege_profile profile = {
        .id = 1,
        .name = "GRANT_TEST",
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
        || micros_hart_register(&objects, 0, &hart)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            &objects,
            hart,
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
    return (
        micros_grant_registry_validate(
            &grant_registry,
            &endpoint_registry,
            &objects
        ) == MICROS_GRANT_OK
    );
}

static bool recreate_process(size_t index)
{
    return (
        micros_thread_release(&objects, threads[index])
            == MICROS_KERNEL_OBJECT_OK
        && micros_process_release(&objects, processes[index])
            == MICROS_KERNEL_OBJECT_OK
        && micros_process_create(&objects, &processes[index])
            == MICROS_KERNEL_OBJECT_OK
        && micros_thread_create(
            &objects,
            processes[index],
            &threads[index]
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_endpoint_reserve(
            &endpoint_registry,
            &objects,
            processes[index],
            &endpoints[index]
        ) == MICROS_ENDPOINT_OK
        && micros_endpoint_install_profile(
            &endpoint_registry,
            &objects,
            processes[index],
            1
        ) == MICROS_ENDPOINT_OK
        && micros_endpoint_activate(
            &endpoint_registry,
            &objects,
            endpoints[index]
        ) == MICROS_ENDPOINT_OK
    );
}

static struct micros_user_context context_pattern(uint64_t base)
{
    struct micros_user_context context;
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    for (index = 0; index < sizeof(words) / sizeof(words[0]); ++index) {
        words[index] = base + index;
    }
    memcpy(&context, words, sizeof(context));
    return context;
}

static bool test_token_boundaries(void)
{
    micros_grant_t grant = UINT32_C(0xdeadbeef);
    size_t slot = SIZE_MAX;
    uint32_t generation = UINT32_MAX;

    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_pack(0, 1, &grant)
    );
    EXPECT_TRUE(grant == UINT32_C(0x40));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_unpack(grant, &slot, &generation)
    );
    EXPECT_TRUE(slot == 0 && generation == 1);

    grant = UINT32_C(0xdeadbeef);
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_ARGUMENT,
        micros_grant_pack(MICROS_GRANT_CAPACITY, 1, &grant)
    );
    EXPECT_TRUE(grant == UINT32_C(0xdeadbeef));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_ARGUMENT,
        micros_grant_pack(0, 0, &grant)
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_ARGUMENT,
        micros_grant_pack(
            MICROS_GRANT_CAPACITY - 1,
            MICROS_GRANT_GENERATION_MAX,
            &grant
        )
    );

    slot = SIZE_MAX;
    generation = UINT32_MAX;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_ARGUMENT,
        micros_grant_unpack(
            MICROS_GRANT_NONE,
            &slot,
            &generation
        )
    );
    EXPECT_TRUE(slot == SIZE_MAX && generation == UINT32_MAX);
    return true;
}

static bool test_create_inspect_revoke_and_reuse(void)
{
    struct micros_grant_record observed;
    struct micros_grant_record sentinel;
    struct micros_grant_registry snapshot;
    micros_grant_t first = MICROS_GRANT_NONE;
    micros_grant_t second = MICROS_GRANT_NONE;

    EXPECT_TRUE(setup_fixture());
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            endpoints[1],
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x1000),
            128,
            MICROS_GRANT_PERMISSION_READ,
            &first
        )
    );
    EXPECT_TRUE(first == UINT32_C(0x40));
    memset(&observed, 0, sizeof(observed));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_inspect(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            first,
            &observed
        )
    );
    EXPECT_TRUE(
        observed.state == MICROS_GRANT_SLOT_ACTIVE
        && observed.generation == 1
        && process_handles_equal(observed.grantor, processes[0])
        && observed.grantor_endpoint == endpoints[0]
        && observed.grantee_endpoint == endpoints[1]
        && observed.base
            == MICROS_USER_VIRTUAL_BASE + UINT64_C(0x1000)
        && observed.length == 128
        && observed.permissions == MICROS_GRANT_PERMISSION_READ
    );

    memset(&sentinel, 0xa5, sizeof(sentinel));
    observed = sentinel;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_UNAUTHORIZED,
        micros_grant_inspect(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[2],
            first,
            &observed
        )
    );
    EXPECT_TRUE(memcmp(&observed, &sentinel, sizeof(observed)) == 0);

    snapshot = grant_registry;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_UNAUTHORIZED,
        micros_grant_revoke(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[2],
            first
        )
    );
    EXPECT_TRUE(registries_equal(&grant_registry, &snapshot));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_revoke(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            first
        )
    );
    EXPECT_TRUE(
        grant_registry.active_count == 0
        && grant_registry.grants[0].state
            == MICROS_GRANT_SLOT_FREE
        && grant_registry.grants[0].generation == 2
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STALE_GRANT,
        micros_grant_inspect(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            first,
            &observed
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            endpoints[1],
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x2000),
            64,
            MICROS_GRANT_PERMISSION_WRITE,
            &second
        )
    );
    EXPECT_TRUE(second != first && second == UINT32_C(0x80));
    return true;
}

static bool expect_create_failure_unchanged(
    enum micros_grant_error expected,
    struct micros_process_handle grantor,
    micros_endpoint_t grantee,
    uintptr_t base,
    size_t length,
    uint32_t permissions
)
{
    struct micros_grant_registry snapshot = grant_registry;
    micros_grant_t grant = UINT32_C(0xdeadbeef);

    EXPECT_GRANT_ERROR(
        expected,
        micros_grant_create(
            &grant_registry,
            &endpoint_registry,
            &objects,
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
        && registries_equal(&grant_registry, &snapshot)
    );
    return true;
}

static bool test_create_failures_and_capacity(void)
{
    micros_endpoint_t stale;
    size_t index;

    EXPECT_TRUE(setup_fixture());
    stale =
        endpoints[1]
        + (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS);
    EXPECT_TRUE(expect_create_failure_unchanged(
        MICROS_GRANT_ERROR_ARGUMENT,
        processes[0],
        MICROS_ENDPOINT_ANY,
        MICROS_USER_VIRTUAL_BASE,
        1,
        MICROS_GRANT_PERMISSION_READ
    ));
    EXPECT_TRUE(expect_create_failure_unchanged(
        MICROS_GRANT_ERROR_UNAUTHORIZED,
        processes[0],
        endpoints[0],
        MICROS_USER_VIRTUAL_BASE,
        1,
        MICROS_GRANT_PERMISSION_READ
    ));
    EXPECT_TRUE(expect_create_failure_unchanged(
        MICROS_GRANT_ERROR_ARGUMENT,
        processes[0],
        endpoints[1],
        MICROS_USER_VIRTUAL_BASE,
        1,
        0
    ));
    EXPECT_TRUE(expect_create_failure_unchanged(
        MICROS_GRANT_ERROR_RANGE,
        processes[0],
        endpoints[1],
        MICROS_USER_VIRTUAL_END - 1,
        2,
        MICROS_GRANT_PERMISSION_READ
    ));
    EXPECT_TRUE(expect_create_failure_unchanged(
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        processes[0],
        stale,
        MICROS_USER_VIRTUAL_BASE,
        1,
        MICROS_GRANT_PERMISSION_READ
    ));

    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        micros_grant_t grant;

        EXPECT_GRANT_ERROR(
            MICROS_GRANT_OK,
            micros_grant_create(
                &grant_registry,
                &endpoint_registry,
                &objects,
                processes[0],
                endpoints[1],
                MICROS_USER_VIRTUAL_BASE
                    + index * UINT64_C(0x100),
                16,
                MICROS_GRANT_PERMISSION_READ,
                &grant
            )
        );
    }
    EXPECT_TRUE(expect_create_failure_unchanged(
        MICROS_GRANT_ERROR_CAPACITY,
        processes[0],
        endpoints[1],
        MICROS_USER_VIRTUAL_BASE + UINT64_C(0x10000),
        16,
        MICROS_GRANT_PERMISSION_READ
    ));
    return true;
}

static bool test_terminal_generation_quarantines(void)
{
    micros_grant_t grant;
    size_t index;

    EXPECT_TRUE(setup_fixture());
    for (index = 0; index + 1 < MICROS_GRANT_CAPACITY; ++index) {
        grant_registry.grants[index].state =
            MICROS_GRANT_SLOT_QUARANTINED;
        grant_registry.grants[index].generation =
            MICROS_GRANT_GENERATION_MAX;
    }
    grant_registry.grants[MICROS_GRANT_CAPACITY - 1].generation =
        MICROS_GRANT_GENERATION_MAX - 1;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_registry_validate(
            &grant_registry,
            &endpoint_registry,
            &objects
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            endpoints[1],
            MICROS_USER_VIRTUAL_BASE,
            1,
            MICROS_GRANT_PERMISSION_READ,
            &grant
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_revoke(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            grant
        )
    );
    EXPECT_TRUE(
        grant_registry.grants[MICROS_GRANT_CAPACITY - 1].state
            == MICROS_GRANT_SLOT_QUARANTINED
        && grant_registry.grants[
            MICROS_GRANT_CAPACITY - 1
        ].generation == MICROS_GRANT_GENERATION_MAX - 1
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STALE_GRANT,
        micros_grant_revoke(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            grant
        )
    );
    return true;
}

static bool test_endpoint_cancellation_and_reuse(void)
{
    struct micros_grant_cancel_plan plan;
    struct micros_grant_cancel_plan corrupted;
    struct micros_grant_registry snapshot;
    struct micros_grant_registry foreign_registry;
    struct micros_grant_record observed;
    struct micros_process_handle old_process;
    micros_grant_t from_closing;
    micros_grant_t to_closing;
    micros_grant_t unrelated;

    EXPECT_TRUE(setup_fixture());
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            endpoints[1],
            MICROS_USER_VIRTUAL_BASE,
            64,
            MICROS_GRANT_PERMISSION_READ,
            &from_closing
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[1],
            endpoints[0],
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x1000),
            64,
            MICROS_GRANT_PERMISSION_WRITE,
            &to_closing
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[1],
            endpoints[2],
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x2000),
            64,
            MICROS_GRANT_PERMISSION_READ,
            &unrelated
        )
    );
    memset(&plan, 0xa5, sizeof(plan));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_prepare_endpoint_cancel(
            &grant_registry,
            &endpoint_registry,
            &objects,
            endpoints[0],
            &plan
        )
    );
    snapshot = grant_registry;
    corrupted = plan;
    corrupted.expected_generation[
        from_closing
            & ((UINT32_C(1) << MICROS_GRANT_SLOT_BITS) - 1)
    ]++;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_INVARIANT,
        micros_grant_commit_endpoint_cancel(
            &grant_registry,
            &corrupted
        )
    );
    EXPECT_TRUE(registries_equal(&grant_registry, &snapshot));

    foreign_registry = grant_registry;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STATE,
        micros_grant_commit_endpoint_cancel(
            &foreign_registry,
            &plan
        )
    );
    EXPECT_TRUE(registries_equal(&grant_registry, &snapshot));

    old_process = processes[0];
    EXPECT_TRUE(
        micros_ipc_endpoint_close(
            &endpoint_registry,
            &objects,
            endpoints[0]
        ) == MICROS_IPC_OK
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_commit_endpoint_cancel(
            &grant_registry,
            &plan
        )
    );
    EXPECT_TRUE(grant_registry.active_count == 1);
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STALE_GRANT,
        micros_grant_inspect(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[1],
            to_closing,
            &observed
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_inspect(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[1],
            unrelated,
            &observed
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STATE,
        micros_grant_commit_endpoint_cancel(
            &grant_registry,
            &plan
        )
    );

    EXPECT_TRUE(recreate_process(0));
    EXPECT_TRUE(
        processes[0].slot == old_process.slot
        && processes[0].generation == old_process.generation + 1
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STALE_GRANT,
        micros_grant_inspect(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            from_closing,
            &observed
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_registry_validate(
            &grant_registry,
            &endpoint_registry,
            &objects
        )
    );
    return true;
}

static bool test_failed_ipc_close_preserves_cancel_plan(void)
{
    struct micros_grant_cancel_plan plan;
    struct micros_grant_cancel_plan plan_snapshot;
    struct micros_grant_registry registry_snapshot;
    struct micros_user_context context = context_pattern(UINT64_C(0x9000));
    micros_grant_t grant;

    EXPECT_TRUE(setup_fixture());
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            endpoints[1],
            MICROS_USER_VIRTUAL_BASE,
            64,
            MICROS_GRANT_PERMISSION_READ,
            &grant
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_prepare_endpoint_cancel(
            &grant_registry,
            &endpoint_registry,
            &objects,
            endpoints[0],
            &plan
        )
    );
    EXPECT_TRUE(
        micros_thread_attach_execution_context(
            &objects,
            threads[0],
            UINT64_C(0x30000000),
            UINT64_C(0x30004000),
            &context
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_admit(
            &objects,
            hart,
            threads[0],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            100,
            true
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_thread_scheduler_hold(&objects, threads[0])
            == MICROS_KERNEL_OBJECT_OK
        && micros_ipc_stage_no_message_completion(
            &endpoint_registry,
            &objects,
            threads[0],
            MICROS_IPC_OK
        ) == MICROS_IPC_OK
    );
    registry_snapshot = grant_registry;
    plan_snapshot = plan;
    EXPECT_TRUE(
        micros_ipc_endpoint_close(
            &endpoint_registry,
            &objects,
            endpoints[0]
        ) == MICROS_IPC_ERROR_STATE
        && registries_equal(&grant_registry, &registry_snapshot)
        && memcmp(&plan, &plan_snapshot, sizeof(plan)) == 0
        && grant_registry.grants[
            grant & ((UINT32_C(1) << MICROS_GRANT_SLOT_BITS) - 1)
        ].state == MICROS_GRANT_SLOT_ACTIVE
    );
    return true;
}

static bool test_error_precedence_and_output_preservation(void)
{
    struct micros_grant_cancel_plan plan;
    struct micros_grant_cancel_plan plan_sentinel;
    struct micros_grant_record record;
    struct micros_grant_record record_sentinel;
    struct micros_grant_registry valid;
    struct micros_process_handle stale_process;
    micros_grant_t grant;
    micros_grant_t stale_grant;

    EXPECT_TRUE(setup_fixture());
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            endpoints[1],
            MICROS_USER_VIRTUAL_BASE,
            64,
            MICROS_GRANT_PERMISSION_READ,
            &grant
        )
    );
    valid = grant_registry;
    ++grant_registry.active_count;
    EXPECT_TRUE(expect_create_failure_unchanged(
        MICROS_GRANT_ERROR_RANGE,
        processes[0],
        endpoints[1],
        MICROS_USER_VIRTUAL_END - 1,
        2,
        MICROS_GRANT_PERMISSION_READ
    ));
    grant_registry = valid;

    memset(&record_sentinel, 0xa5, sizeof(record_sentinel));
    record = record_sentinel;
    ++grant_registry.active_count;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_INVARIANT,
        micros_grant_inspect(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            grant,
            &record
        )
    );
    EXPECT_TRUE(memcmp(&record, &record_sentinel, sizeof(record)) == 0);
    grant_registry = valid;

    stale_process = processes[0];
    ++stale_process.generation;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_pack(1, 1, &stale_grant)
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        micros_grant_inspect(
            &grant_registry,
            &endpoint_registry,
            &objects,
            stale_process,
            stale_grant,
            &record
        )
    );
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_ARGUMENT,
        micros_grant_inspect(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            MICROS_GRANT_NONE,
            NULL
        )
    );

    memset(&plan_sentinel, 0xa5, sizeof(plan_sentinel));
    plan = plan_sentinel;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_ARGUMENT,
        micros_grant_prepare_endpoint_cancel(
            &grant_registry,
            &endpoint_registry,
            &objects,
            MICROS_ENDPOINT_ANY,
            &plan
        )
    );
    EXPECT_TRUE(memcmp(&plan, &plan_sentinel, sizeof(plan)) == 0);

    memset(&plan, 0, sizeof(plan));
    plan.active = true;
    plan.registry_identity = (uintptr_t)&grant_registry + 1;
    plan.endpoint = endpoints[0];
    plan.selected_slots = UINT64_MAX;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_STATE,
        micros_grant_commit_endpoint_cancel(
            &grant_registry,
            &plan
        )
    );
    EXPECT_TRUE(registries_equal(&grant_registry, &valid));

    EXPECT_TRUE(setup_fixture());
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_pack(0, 1, &stale_grant)
    );
    EXPECT_TRUE(
        micros_ipc_endpoint_close(
            &endpoint_registry,
            &objects,
            endpoints[0]
        ) == MICROS_IPC_OK
    );
    record = record_sentinel;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        micros_grant_inspect(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            stale_grant,
            &record
        )
    );
    EXPECT_TRUE(memcmp(&record, &record_sentinel, sizeof(record)) == 0);
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_ERROR_DEAD_ENDPOINT,
        micros_grant_revoke(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            stale_grant
        )
    );
    return true;
}

static bool test_validator_rejects_corruption(void)
{
    struct micros_grant_registry valid;
    micros_grant_t grant;

    EXPECT_TRUE(setup_fixture());
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            endpoints[1],
            MICROS_USER_VIRTUAL_BASE,
            64,
            MICROS_GRANT_PERMISSION_READ,
            &grant
        )
    );
    valid = grant_registry;

#define EXPECT_CORRUPTION(statement) \
    do { \
        grant_registry = valid; \
        statement; \
        EXPECT_GRANT_ERROR( \
            MICROS_GRANT_ERROR_INVARIANT, \
            micros_grant_registry_validate( \
                &grant_registry, \
                &endpoint_registry, \
                &objects \
            ) \
        ); \
    } while (false)

    EXPECT_CORRUPTION(++grant_registry.active_count);
    EXPECT_CORRUPTION(
        grant_registry.grants[0].permissions = 0
    );
    EXPECT_CORRUPTION(
        grant_registry.grants[0].grantor.generation++
    );
    EXPECT_CORRUPTION(
        grant_registry.grants[0].grantee_endpoint =
            endpoints[0]
    );
    EXPECT_CORRUPTION(
        grant_registry.grants[0].length =
            MICROS_USER_VIRTUAL_END
    );
    EXPECT_CORRUPTION(
        grant_registry.grants[1].grantor.slot = 1
    );
    EXPECT_CORRUPTION(
        grant_registry.grants[1].state =
            MICROS_GRANT_SLOT_QUARANTINED
    );

#undef EXPECT_CORRUPTION
    return true;
}

static uint32_t model_next(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static const char *model_operation_name(
    enum grant_model_operation operation
)
{
    static const char *const names[GRANT_MODEL_OPERATION_COUNT] = {
        "create",
        "authorize-from",
        "authorize-to",
        "authorize-denial",
        "authorize-stale",
        "authorize-bounds",
        "revoke",
        "inspect",
        "cancel",
        "close-reuse",
        "malformed-token",
        "plan-failure",
        "invalid-create",
    };

    return (size_t)operation < GRANT_MODEL_OPERATION_COUNT
        ? names[operation]
        : "unknown";
}

static void record_model_trace(
    size_t step,
    enum grant_model_operation operation,
    size_t grantor,
    size_t grantee,
    micros_grant_t grant,
    size_t offset,
    size_t length,
    uint32_t permission,
    enum micros_grant_error result
)
{
    size_t index = model_trace_count % GRANT_MODEL_TRACE_COUNT;

    model_trace[index] = (struct grant_model_trace_entry){
        .step = step,
        .operation = operation,
        .grantor = grantor,
        .grantee = grantee,
        .grant = grant,
        .offset = offset,
        .length = length,
        .permission = permission,
        .result = result,
    };
    ++model_trace_count;
}

static bool model_fail(size_t step, const char *reason)
{
    size_t available =
        model_trace_count < GRANT_MODEL_TRACE_COUNT
            ? model_trace_count
            : GRANT_MODEL_TRACE_COUNT;
    size_t first = model_trace_count - available;
    size_t offset;

    fprintf(
        stderr,
        "grant model seed=0x38a110c5 step=%zu: %s\n",
        step,
        reason
    );
    for (offset = 0; offset < available; ++offset) {
        const struct grant_model_trace_entry *entry =
            &model_trace[
                (first + offset) % GRANT_MODEL_TRACE_COUNT
            ];

        fprintf(
            stderr,
            "  step=%zu op=%s grantor=%zu grantee=%zu "
            "grant=0x%08x offset=%zu length=%zu permission=0x%x "
            "result=%d\n",
            entry->step,
            model_operation_name(entry->operation),
            entry->grantor,
            entry->grantee,
            entry->grant,
            entry->offset,
            entry->length,
            entry->permission,
            (int)entry->result
        );
    }
    return false;
}

static bool cancel_plan_matches_reference(
    const struct micros_grant_cancel_plan *plan,
    const struct micros_grant_registry *model,
    micros_endpoint_t endpoint
)
{
    uint64_t selected_slots = 0;
    size_t index;

    if (
        !plan->active
        || plan->registry_identity != (uintptr_t)&grant_registry
        || plan->endpoint != endpoint
    ) {
        return false;
    }
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        const struct micros_grant_record *record =
            &model->grants[index];
        bool selected = (
            record->state == MICROS_GRANT_SLOT_ACTIVE
            && (
                record->grantor_endpoint == endpoint
                || record->grantee_endpoint == endpoint
            )
        );

        if (selected) {
            selected_slots |= UINT64_C(1) << index;
        }
        if (
            plan->expected_generation[index]
                != (selected ? record->generation : 0)
        ) {
            return false;
        }
    }
    return plan->selected_slots == selected_slots;
}

static bool test_grant_model_boundaries(void)
{
    struct micros_grant_registry expected;
    micros_grant_t terminal;
    size_t index;

    EXPECT_TRUE(setup_fixture());
    expected = grant_registry;
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        struct micros_grant_record *record =
            &expected.grants[index];
        micros_grant_t grant;

        EXPECT_GRANT_ERROR(
            MICROS_GRANT_OK,
            micros_grant_create(
                &grant_registry,
                &endpoint_registry,
                &objects,
                processes[0],
                endpoints[1],
                MICROS_USER_VIRTUAL_BASE
                    + index * UINT64_C(0x100),
                16,
                MICROS_GRANT_PERMISSION_READ,
                &grant
            )
        );
        *record = (struct micros_grant_record){
            .state = MICROS_GRANT_SLOT_ACTIVE,
            .generation = 1,
            .grantor = processes[0],
            .grantor_endpoint = endpoints[0],
            .grantee_endpoint = endpoints[1],
            .base = MICROS_USER_VIRTUAL_BASE
                + index * UINT64_C(0x100),
            .length = 16,
            .permissions = MICROS_GRANT_PERMISSION_READ,
        };
        ++expected.active_count;
        EXPECT_TRUE(registries_equal(&grant_registry, &expected));
    }
    {
        struct micros_grant_registry snapshot = grant_registry;
        micros_grant_t output = UINT32_C(0xdeadbeef);

        EXPECT_GRANT_ERROR(
            MICROS_GRANT_ERROR_CAPACITY,
            micros_grant_create(
                &grant_registry,
                &endpoint_registry,
                &objects,
                processes[0],
                endpoints[1],
                MICROS_USER_VIRTUAL_BASE + UINT64_C(0x10000),
                16,
                MICROS_GRANT_PERMISSION_READ,
                &output
            )
        );
        EXPECT_TRUE(
            output == UINT32_C(0xdeadbeef)
            && registries_equal(&grant_registry, &snapshot)
        );
    }

    EXPECT_TRUE(setup_fixture());
    for (index = 0; index + 1 < MICROS_GRANT_CAPACITY; ++index) {
        grant_registry.grants[index].state =
            MICROS_GRANT_SLOT_QUARANTINED;
        grant_registry.grants[index].generation =
            MICROS_GRANT_GENERATION_MAX;
    }
    grant_registry.grants[MICROS_GRANT_CAPACITY - 1].generation =
        MICROS_GRANT_GENERATION_MAX - 1;
    expected = grant_registry;
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_create(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            endpoints[1],
            MICROS_USER_VIRTUAL_BASE,
            1,
            MICROS_GRANT_PERMISSION_WRITE,
            &terminal
        )
    );
    expected.grants[MICROS_GRANT_CAPACITY - 1] =
        (struct micros_grant_record){
            .state = MICROS_GRANT_SLOT_ACTIVE,
            .generation = MICROS_GRANT_GENERATION_MAX - 1,
            .grantor = processes[0],
            .grantor_endpoint = endpoints[0],
            .grantee_endpoint = endpoints[1],
            .base = MICROS_USER_VIRTUAL_BASE,
            .length = 1,
            .permissions = MICROS_GRANT_PERMISSION_WRITE,
        };
    expected.active_count = 1;
    EXPECT_TRUE(registries_equal(&grant_registry, &expected));
    EXPECT_GRANT_ERROR(
        MICROS_GRANT_OK,
        micros_grant_revoke(
            &grant_registry,
            &endpoint_registry,
            &objects,
            processes[0],
            terminal
        )
    );
    memset(
        &expected.grants[MICROS_GRANT_CAPACITY - 1],
        0,
        sizeof(expected.grants[MICROS_GRANT_CAPACITY - 1])
    );
    expected.grants[MICROS_GRANT_CAPACITY - 1].state =
        MICROS_GRANT_SLOT_QUARANTINED;
    expected.grants[MICROS_GRANT_CAPACITY - 1].generation =
        MICROS_GRANT_GENERATION_MAX - 1;
    expected.active_count = 0;
    return (
        registries_equal(&grant_registry, &expected)
        && micros_grant_registry_validate(
            &grant_registry,
            &endpoint_registry,
            &objects
        ) == MICROS_GRANT_OK
    );
}

static bool test_grant_model(void)
{
    const uint32_t seed = UINT32_C(0x38a110c5);
    struct micros_grant_registry expected;
    uint16_t expected_process_slot[GRANT_PROCESS_COUNT];
    uint32_t expected_process_generation[GRANT_PROCESS_COUNT];
    micros_endpoint_t expected_endpoint[GRANT_PROCESS_COUNT];
    unsigned char grantor_bytes[64];
    unsigned char grantee_bytes[64];
    unsigned char expected_grantor_bytes[64];
    unsigned char expected_grantee_bytes[64];
    uint32_t random_state = seed;
    size_t coverage[GRANT_MODEL_OPERATION_COUNT] = {0};
    size_t step;

    EXPECT_TRUE(setup_fixture());
    expected = grant_registry;
    for (step = 0; step < GRANT_PROCESS_COUNT; ++step) {
        expected_process_slot[step] = processes[step].slot;
        expected_process_generation[step] =
            processes[step].generation;
        expected_endpoint[step] = endpoints[step];
    }
    memset(model_trace, 0, sizeof(model_trace));
    model_trace_count = 0;
    for (step = 0; step < GRANT_MODEL_STEPS; ++step) {
        uint32_t random = model_next(&random_state);
        enum grant_model_operation operation =
            (enum grant_model_operation)(
                (random >> 24) % GRANT_MODEL_OPERATION_COUNT
            );
        size_t grantor = (random >> 8) % GRANT_PROCESS_COUNT;
        size_t grantee = (random >> 16) % GRANT_PROCESS_COUNT;
        size_t slot = random % MICROS_GRANT_CAPACITY;
        micros_grant_t grant = (
            expected.grants[slot].generation
                << MICROS_GRANT_SLOT_BITS
        ) | (micros_grant_t)slot;
        size_t trace_offset = 0;
        size_t trace_length = 0;
        uint32_t trace_permission = 0;
        enum micros_grant_error actual;

        if (grantee == grantor) {
            grantee = (grantee + 1) % GRANT_PROCESS_COUNT;
        }
        if (step < GRANT_MODEL_OPERATION_COUNT) {
            operation = (enum grant_model_operation)step;
        }
        switch (operation) {
        case GRANT_MODEL_CREATE: {
            micros_grant_t produced = MICROS_GRANT_NONE;
            size_t expected_slot;

            actual = micros_grant_create(
                &grant_registry,
                &endpoint_registry,
                &objects,
                processes[grantor],
                endpoints[grantee],
                MICROS_USER_VIRTUAL_BASE
                    + (random & UINT32_C(0xffff)),
                1 + (random & UINT32_C(0xff)),
                step == 0
                    ? (
                        MICROS_GRANT_PERMISSION_READ
                        | MICROS_GRANT_PERMISSION_WRITE
                    )
                    : (
                        (random & 1)
                            ? MICROS_GRANT_PERMISSION_READ
                            : MICROS_GRANT_PERMISSION_WRITE
                    ),
                &produced
            );
            for (
                expected_slot = 0;
                expected_slot < MICROS_GRANT_CAPACITY;
                ++expected_slot
            ) {
                if (
                    expected.grants[expected_slot].state
                        == MICROS_GRANT_SLOT_FREE
                ) {
                    break;
                }
            }
            if (expected_slot == MICROS_GRANT_CAPACITY) {
                if (actual != MICROS_GRANT_ERROR_CAPACITY) {
                    return model_fail(step, "capacity result diverged");
                }
            } else {
                struct micros_grant_record *record =
                    &expected.grants[expected_slot];
                micros_grant_t expected_grant = (
                    record->generation << MICROS_GRANT_SLOT_BITS
                ) | (micros_grant_t)expected_slot;

                if (
                    actual != MICROS_GRANT_OK
                    || produced != expected_grant
                ) {
                    return model_fail(step, "create result diverged");
                }
                *record = (struct micros_grant_record){
                    .state = MICROS_GRANT_SLOT_ACTIVE,
                    .generation = record->generation,
                    .grantor = processes[grantor],
                    .grantor_endpoint = endpoints[grantor],
                    .grantee_endpoint = endpoints[grantee],
                    .base = MICROS_USER_VIRTUAL_BASE
                        + (random & UINT32_C(0xffff)),
                    .length = 1 + (random & UINT32_C(0xff)),
                    .permissions = step == 0
                        ? (
                            MICROS_GRANT_PERMISSION_READ
                            | MICROS_GRANT_PERMISSION_WRITE
                        )
                        : (
                            (random & 1)
                                ? MICROS_GRANT_PERMISSION_READ
                                : MICROS_GRANT_PERMISSION_WRITE
                        ),
                };
                ++expected.active_count;
            }
            break;
        }
        case GRANT_MODEL_AUTHORIZE_FROM:
        case GRANT_MODEL_AUTHORIZE_TO:
        case GRANT_MODEL_AUTHORIZE_DENIAL:
        case GRANT_MODEL_AUTHORIZE_STALE:
        case GRANT_MODEL_AUTHORIZE_BOUNDS: {
            struct micros_grant_registry snapshot = grant_registry;
            struct micros_grant_copy_authority authority;
            struct micros_grant_copy_authority sentinel;
            struct micros_grant_copy_range_plan source_plan = {0};
            struct micros_grant_copy_range_plan destination_plan = {0};
            const struct micros_grant_record *record;
            struct micros_process_handle candidate_grantee = processes[0];
            micros_endpoint_t candidate_grantor = endpoints[0];
            enum micros_grant_error expected_result;
            size_t selected_slot = MICROS_GRANT_CAPACITY;
            size_t record_grantor = GRANT_PROCESS_COUNT;
            size_t record_grantee = GRANT_PROCESS_COUNT;
            size_t index;

            if (operation == GRANT_MODEL_AUTHORIZE_STALE) {
                selected_slot = random % MICROS_GRANT_CAPACITY;
            } else {
                for (
                    index = 0;
                    index < MICROS_GRANT_CAPACITY;
                    ++index
                ) {
                    if (
                        expected.grants[index].state
                            == MICROS_GRANT_SLOT_ACTIVE
                    ) {
                        selected_slot = index;
                        break;
                    }
                }
                if (selected_slot == MICROS_GRANT_CAPACITY) {
                    selected_slot = random % MICROS_GRANT_CAPACITY;
                }
            }
            slot = selected_slot;
            record = &expected.grants[slot];
            grant = (
                record->generation << MICROS_GRANT_SLOT_BITS
            ) | (micros_grant_t)slot;
            if (
                operation == GRANT_MODEL_AUTHORIZE_STALE
                && record->state == MICROS_GRANT_SLOT_ACTIVE
            ) {
                uint32_t stale_generation =
                    record->generation == 1
                        ? 2
                        : record->generation - 1;

                grant = (
                    stale_generation << MICROS_GRANT_SLOT_BITS
                ) | (micros_grant_t)slot;
            }
            for (index = 0; index < sizeof(grantor_bytes); ++index) {
                grantor_bytes[index] =
                    (unsigned char)(random + index);
                grantee_bytes[index] =
                    (unsigned char)(UINT8_C(0xc0) - index);
                expected_grantor_bytes[index] =
                    grantor_bytes[index];
                expected_grantee_bytes[index] =
                    grantee_bytes[index];
            }
            if (
                record->state != MICROS_GRANT_SLOT_ACTIVE
                || operation == GRANT_MODEL_AUTHORIZE_STALE
            ) {
                expected_result = MICROS_GRANT_ERROR_STALE_GRANT;
                trace_permission = MICROS_GRANT_PERMISSION_READ;
            } else {
                size_t limit =
                    record->length < sizeof(grantor_bytes)
                        ? record->length
                        : sizeof(grantor_bytes);

                for (
                    index = 0;
                    index < GRANT_PROCESS_COUNT;
                    ++index
                ) {
                    if (process_handles_equal(
                        record->grantor,
                        processes[index]
                    )) {
                        record_grantor = index;
                    }
                    if (
                        record->grantee_endpoint
                            == endpoints[index]
                    ) {
                        record_grantee = index;
                    }
                }
                if (
                    record_grantor == GRANT_PROCESS_COUNT
                    || record_grantee == GRANT_PROCESS_COUNT
                    || record->grantor_endpoint
                        != endpoints[record_grantor]
                ) {
                    return model_fail(
                        step,
                        "active participant reference diverged"
                    );
                }
                grantor = record_grantor;
                grantee = record_grantee;
                candidate_grantee = processes[record_grantee];
                candidate_grantor = record->grantor_endpoint;
                trace_length = limit == 0
                    ? 0
                    : (random >> 16) % (limit + 1);
                trace_offset =
                    (random >> 8)
                    % (record->length - trace_length + 1);
                switch (operation) {
                case GRANT_MODEL_AUTHORIZE_FROM:
                    trace_permission = MICROS_GRANT_PERMISSION_READ;
                    break;
                case GRANT_MODEL_AUTHORIZE_TO:
                    trace_permission = MICROS_GRANT_PERMISSION_WRITE;
                    break;
                case GRANT_MODEL_AUTHORIZE_DENIAL:
                    trace_permission = (
                        record->permissions
                            & MICROS_GRANT_PERMISSION_READ
                    )
                        ? MICROS_GRANT_PERMISSION_READ
                        : MICROS_GRANT_PERMISSION_WRITE;
                    grantee =
                        (record_grantee + 1) % GRANT_PROCESS_COUNT;
                    candidate_grantee = processes[grantee];
                    break;
                case GRANT_MODEL_AUTHORIZE_BOUNDS:
                    trace_permission = (
                        record->permissions
                            & MICROS_GRANT_PERMISSION_READ
                    )
                        ? MICROS_GRANT_PERMISSION_READ
                        : MICROS_GRANT_PERMISSION_WRITE;
                    trace_offset = record->length;
                    trace_length = 1;
                    break;
                default:
                    return model_fail(
                        step,
                        "unexpected authorize operation"
                    );
                }
                if (operation == GRANT_MODEL_AUTHORIZE_DENIAL) {
                    expected_result = MICROS_GRANT_ERROR_UNAUTHORIZED;
                } else if (
                    (record->permissions & trace_permission) == 0
                ) {
                    expected_result = MICROS_GRANT_ERROR_UNAUTHORIZED;
                } else if (
                    operation == GRANT_MODEL_AUTHORIZE_BOUNDS
                ) {
                    expected_result = MICROS_GRANT_ERROR_RANGE;
                } else {
                    expected_result = MICROS_GRANT_OK;
                }
            }
            memset(&sentinel, 0xa5, sizeof(sentinel));
            authority = sentinel;
            actual = micros_grant_prepare_copy_authority(
                &grant_registry,
                &endpoint_registry,
                &objects,
                candidate_grantee,
                candidate_grantor,
                grant,
                trace_offset,
                trace_length,
                trace_permission,
                &authority
            );
            if (
                actual != expected_result
                || !registries_equal(&grant_registry, &snapshot)
            ) {
                return model_fail(
                    step,
                    "copy authorization result diverged"
                );
            }
            if (actual != MICROS_GRANT_OK) {
                if (
                    memcmp(&authority, &sentinel, sizeof(authority)) != 0
                    || memcmp(
                        grantor_bytes,
                        expected_grantor_bytes,
                        sizeof(grantor_bytes)
                    ) != 0
                    || memcmp(
                        grantee_bytes,
                        expected_grantee_bytes,
                        sizeof(grantee_bytes)
                    ) != 0
                ) {
                    return model_fail(
                        step,
                        "failed authorization changed output or bytes"
                    );
                }
                break;
            }
            if (
                !process_handles_equal(
                    authority.grantor,
                    record->grantor
                )
                || !process_handles_equal(
                    authority.grantee,
                    processes[record_grantee]
                )
                || authority.remote_address
                    != record->base + trace_offset
                || authority.length != trace_length
                || authority.required_permission != trace_permission
            ) {
                return model_fail(
                    step,
                    "copy authority output diverged"
                );
            }
            if (trace_length != 0) {
                source_plan.chunk_count = 1;
                destination_plan.chunk_count = 1;
                source_plan.chunks[0].length = trace_length;
                destination_plan.chunks[0].length = trace_length;
                if (
                    trace_permission == MICROS_GRANT_PERMISSION_READ
                ) {
                    source_plan.chunks[0].physical_address =
                        (uintptr_t)&grantor_bytes[0];
                    destination_plan.chunks[0].physical_address =
                        (uintptr_t)&grantee_bytes[0];
                    memcpy(
                        expected_grantee_bytes,
                        expected_grantor_bytes,
                        trace_length
                    );
                } else {
                    source_plan.chunks[0].physical_address =
                        (uintptr_t)&grantee_bytes[0];
                    destination_plan.chunks[0].physical_address =
                        (uintptr_t)&grantor_bytes[0];
                    memcpy(
                        expected_grantor_bytes,
                        expected_grantee_bytes,
                        trace_length
                    );
                }
            }
            if (
                micros_grant_copy_plan_validate(
                    &source_plan,
                    &destination_plan,
                    trace_length
                ) != MICROS_GRANT_OK
            ) {
                return model_fail(step, "copy plan rejected");
            }
            micros_grant_copy_commit(
                &source_plan,
                &destination_plan,
                trace_length
            );
            if (
                memcmp(
                    grantor_bytes,
                    expected_grantor_bytes,
                    sizeof(grantor_bytes)
                ) != 0
                || memcmp(
                    grantee_bytes,
                    expected_grantee_bytes,
                    sizeof(grantee_bytes)
                ) != 0
            ) {
                return model_fail(step, "copy bytes diverged");
            }
            break;
        }
        case GRANT_MODEL_REVOKE: {
            struct micros_grant_record *record =
                &expected.grants[slot];

            actual = micros_grant_revoke(
                &grant_registry,
                &endpoint_registry,
                &objects,
                processes[grantor],
                grant
            );
            if (
                record->state != MICROS_GRANT_SLOT_ACTIVE
                || record->generation
                    != (grant >> MICROS_GRANT_SLOT_BITS)
            ) {
                if (actual != MICROS_GRANT_ERROR_STALE_GRANT) {
                    return model_fail(step, "stale revoke diverged");
                }
            } else if (!process_handles_equal(
                record->grantor,
                processes[grantor]
            )) {
                if (actual != MICROS_GRANT_ERROR_UNAUTHORIZED) {
                    return model_fail(step, "owner revoke diverged");
                }
            } else {
                uint32_t generation = record->generation + 1;

                if (actual != MICROS_GRANT_OK) {
                    return model_fail(step, "revoke failed");
                }
                memset(record, 0, sizeof(*record));
                record->state = MICROS_GRANT_SLOT_FREE;
                record->generation = generation;
                --expected.active_count;
            }
            break;
        }
        case GRANT_MODEL_INSPECT: {
            struct micros_grant_record observed;
            const struct micros_grant_record *record =
                &expected.grants[slot];

            memset(&observed, 0, sizeof(observed));
            actual = micros_grant_inspect(
                &grant_registry,
                &endpoint_registry,
                &objects,
                processes[grantor],
                grant,
                &observed
            );
            if (record->state != MICROS_GRANT_SLOT_ACTIVE) {
                if (actual != MICROS_GRANT_ERROR_STALE_GRANT) {
                    return model_fail(step, "inspect stale diverged");
                }
            } else if (!process_handles_equal(
                record->grantor,
                processes[grantor]
            )) {
                if (actual != MICROS_GRANT_ERROR_UNAUTHORIZED) {
                    return model_fail(step, "inspect owner diverged");
                }
            } else if (
                actual != MICROS_GRANT_OK
                || !records_equal(record, &observed)
            ) {
                return model_fail(step, "inspect result diverged");
            }
            break;
        }
        case GRANT_MODEL_CANCEL: {
            struct micros_grant_cancel_plan plan;
            size_t index;

            memset(&plan, 0, sizeof(plan));
            actual = micros_grant_prepare_endpoint_cancel(
                &grant_registry,
                &endpoint_registry,
                &objects,
                endpoints[grantor],
                &plan
            );
            if (
                actual != MICROS_GRANT_OK
                || !cancel_plan_matches_reference(
                    &plan,
                    &expected,
                    endpoints[grantor]
                )
                || micros_grant_commit_endpoint_cancel(
                    &grant_registry,
                    &plan
                ) != MICROS_GRANT_OK
            ) {
                return model_fail(step, "cancel failed");
            }
            for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
                struct micros_grant_record *record =
                    &expected.grants[index];

                if (
                    record->state == MICROS_GRANT_SLOT_ACTIVE
                    && (
                        record->grantor_endpoint
                            == endpoints[grantor]
                        || record->grantee_endpoint
                            == endpoints[grantor]
                    )
                ) {
                    uint32_t generation = record->generation + 1;

                    memset(record, 0, sizeof(*record));
                    record->state = MICROS_GRANT_SLOT_FREE;
                    record->generation = generation;
                    --expected.active_count;
                }
            }
            break;
        }
        case GRANT_MODEL_CLOSE_REUSE: {
            struct micros_grant_cancel_plan plan;
            micros_endpoint_t old_endpoint = endpoints[grantor];
            uint32_t old_generation =
                processes[grantor].generation;
            size_t index;

            memset(&plan, 0, sizeof(plan));
            actual = micros_grant_prepare_endpoint_cancel(
                &grant_registry,
                &endpoint_registry,
                &objects,
                old_endpoint,
                &plan
            );
            if (
                actual != MICROS_GRANT_OK
                || !cancel_plan_matches_reference(
                    &plan,
                    &expected,
                    old_endpoint
                )
                || micros_ipc_endpoint_close(
                    &endpoint_registry,
                    &objects,
                    old_endpoint
                ) != MICROS_IPC_OK
                || micros_grant_commit_endpoint_cancel(
                    &grant_registry,
                    &plan
                ) != MICROS_GRANT_OK
                || !recreate_process(grantor)
                || processes[grantor].generation
                    != old_generation + 1
                || endpoints[grantor] == old_endpoint
            ) {
                return model_fail(step, "close reuse failed");
            }
            for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
                struct micros_grant_record *record =
                    &expected.grants[index];

                if (
                    record->state == MICROS_GRANT_SLOT_ACTIVE
                    && (
                        record->grantor_endpoint == old_endpoint
                        || record->grantee_endpoint == old_endpoint
                    )
                ) {
                    uint32_t generation = record->generation + 1;

                    memset(record, 0, sizeof(*record));
                    record->state = MICROS_GRANT_SLOT_FREE;
                    record->generation = generation;
                    --expected.active_count;
                }
            }
            expected_process_generation[grantor] =
                old_generation + 1;
            expected_endpoint[grantor] = endpoints[grantor];
            break;
        }
        case GRANT_MODEL_MALFORMED_TOKEN: {
            struct micros_grant_registry snapshot = grant_registry;
            struct micros_grant_record output;
            struct micros_grant_record sentinel;

            memset(&sentinel, 0xa5, sizeof(sentinel));
            output = sentinel;
            actual = micros_grant_inspect(
                &grant_registry,
                &endpoint_registry,
                &objects,
                processes[grantor],
                (random & 1) ? MICROS_GRANT_NONE : 0,
                &output
            );
            if (
                actual != MICROS_GRANT_ERROR_ARGUMENT
                || memcmp(&output, &sentinel, sizeof(output)) != 0
                || !registries_equal(&grant_registry, &snapshot)
            ) {
                return model_fail(
                    step,
                    "malformed token changed state or output"
                );
            }
            break;
        }
        case GRANT_MODEL_PLAN_FAILURE: {
            struct micros_grant_cancel_plan plan;
            struct micros_grant_cancel_plan plan_snapshot;
            struct micros_grant_registry snapshot = grant_registry;

            memset(&plan, 0, sizeof(plan));
            actual = micros_grant_prepare_endpoint_cancel(
                &grant_registry,
                &endpoint_registry,
                &objects,
                endpoints[grantor],
                &plan
            );
            if (
                actual != MICROS_GRANT_OK
                || !cancel_plan_matches_reference(
                    &plan,
                    &expected,
                    endpoints[grantor]
                )
            ) {
                return model_fail(step, "plan preparation diverged");
            }
            plan.registry_identity++;
            plan_snapshot = plan;
            actual = micros_grant_commit_endpoint_cancel(
                &grant_registry,
                &plan
            );
            if (
                actual != MICROS_GRANT_ERROR_STATE
                || memcmp(&plan, &plan_snapshot, sizeof(plan)) != 0
                || !registries_equal(&grant_registry, &snapshot)
            ) {
                return model_fail(
                    step,
                    "foreign plan changed state"
                );
            }
            break;
        }
        case GRANT_MODEL_INVALID_CREATE: {
            struct micros_grant_registry snapshot = grant_registry;
            micros_grant_t output = UINT32_C(0xdeadbeef);

            actual = micros_grant_create(
                &grant_registry,
                &endpoint_registry,
                &objects,
                processes[grantor],
                endpoints[grantee],
                MICROS_USER_VIRTUAL_BASE,
                1,
                0,
                &output
            );
            if (
                actual != MICROS_GRANT_ERROR_ARGUMENT
                || output != UINT32_C(0xdeadbeef)
                || !registries_equal(&grant_registry, &snapshot)
            ) {
                return model_fail(step, "invalid create changed state");
            }
            break;
        }
        default:
            return model_fail(step, "unknown operation");
        }
        ++coverage[operation];
        record_model_trace(
            step,
            operation,
            grantor,
            grantee,
            grant,
            trace_offset,
            trace_length,
            trace_permission,
            actual
        );
        if (
            !registries_equal(&grant_registry, &expected)
            || micros_grant_registry_validate(
                &grant_registry,
                &endpoint_registry,
                &objects
            ) != MICROS_GRANT_OK
        ) {
            return model_fail(step, "registry state diverged");
        }
        for (slot = 0; slot < GRANT_PROCESS_COUNT; ++slot) {
            struct micros_process_handle expected_owner = {
                .slot = expected_process_slot[slot],
                .generation = expected_process_generation[slot],
            };
            struct micros_process_handle unpacked_owner;
            const struct micros_process *process =
                &objects.processes[expected_process_slot[slot]];
            const struct micros_endpoint_record *endpoint =
                &endpoint_registry.endpoints[
                    expected_process_slot[slot]
                ];

            if (
                !process_handles_equal(
                    processes[slot],
                    expected_owner
                )
                || endpoints[slot] != expected_endpoint[slot]
                || process->slot_state
                    != MICROS_KERNEL_OBJECT_SLOT_LIVE
                || process->generation
                    != expected_process_generation[slot]
                || process->primary_endpoint
                    != expected_endpoint[slot]
                || micros_endpoint_unpack(
                    expected_endpoint[slot],
                    &unpacked_owner
                ) != MICROS_ENDPOINT_OK
                || !process_handles_equal(
                    unpacked_owner,
                    expected_owner
                )
                || endpoint->state != MICROS_ENDPOINT_STATE_ACTIVE
                || !process_handles_equal(
                    endpoint->owner,
                    expected_owner
                )
                || endpoint->value != expected_endpoint[slot]
            ) {
                return model_fail(
                    step,
                    "authoritative participant generation diverged"
                );
            }
        }
    }
    for (step = 0; step < GRANT_MODEL_OPERATION_COUNT; ++step) {
        if (coverage[step] == 0) {
            return model_fail(
                GRANT_MODEL_STEPS,
                "operation coverage incomplete"
            );
        }
    }
    printf(
        "# grant and checked copy model seed=0x%08x transitions=%u\n",
        seed,
        GRANT_MODEL_STEPS
    );
    return true;
}

bool micros_grant_test_run(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"grant token boundaries", test_token_boundaries},
        {
            "grant create inspect revoke reuse",
            test_create_inspect_revoke_and_reuse,
        },
        {
            "grant create failures and capacity",
            test_create_failures_and_capacity,
        },
        {
            "grant terminal generation quarantines",
            test_terminal_generation_quarantines,
        },
        {
            "grant endpoint cancellation and reuse",
            test_endpoint_cancellation_and_reuse,
        },
        {
            "failed IPC close preserves cancel plan",
            test_failed_ipc_close_preserves_cancel_plan,
        },
        {
            "grant error precedence and outputs",
            test_error_precedence_and_output_preservation,
        },
        {
            "grant validator rejects corruption",
            test_validator_rejects_corruption,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(
                stderr,
                "grant subtest %zu failed - %s\n",
                index + 1,
                tests[index].name
            );
            return false;
        }
    }
    return true;
}

bool micros_grant_model_test_run(void)
{
    return test_grant_model_boundaries() && test_grant_model();
}
