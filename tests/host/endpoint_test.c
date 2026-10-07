#include "micros/endpoint.h"
#include "micros/scheduler_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

#define EXPECT_ERROR(expected, expression) \
    do { \
        enum micros_endpoint_error actual = (expression); \
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

#define EXPECT_OBJECT_ERROR(expected, expression) \
    do { \
        enum micros_kernel_object_error actual = (expression); \
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

static struct micros_endpoint_registry registry;
static struct micros_kernel_objects objects;
static struct micros_process_handle processes[2];
static struct micros_thread_handle threads[2];
static struct micros_hart_handle hart;

static struct micros_privilege_profile profile(
    uint8_t id,
    const char *name,
    uint32_t operations,
    uint32_t call_targets,
    uint32_t send_targets,
    uint32_t notify_targets,
    uint64_t kernel_operations
)
{
    struct micros_privilege_profile result;
    size_t index;

    memset(&result, 0, sizeof(result));
    result.id = id;
    for (
        index = 0;
        index + 1 < sizeof(result.name) && name[index] != '\0';
        ++index
    ) {
        result.name[index] = name[index];
    }
    result.operations = operations;
    result.call_targets = call_targets;
    result.send_targets = send_targets;
    result.notify_targets = notify_targets;
    result.kernel_operations = kernel_operations;
    return result;
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

static bool setup_lifecycle_fixture(void)
{
    struct micros_privilege_profile profiles[2];
    size_t index;

    memset(&registry, 0, sizeof(registry));
    memset(&objects, 0, sizeof(objects));
    profiles[0] = profile(
        1,
        "CLIENT",
        MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_NOTIFY,
        UINT32_C(1) << 2,
        0,
        UINT32_C(1) << 1,
        UINT64_C(1) << 3
    );
    profiles[1] = profile(
        2,
        "SERVER",
        MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_SEND
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_NOTIFY,
        0,
        UINT32_C(1) << 1,
        UINT32_C(1) << 2,
        0
    );
    if (
        micros_endpoint_registry_initialize(
            &registry,
            profiles,
            2
        ) != MICROS_ENDPOINT_OK
        || micros_kernel_objects_initialize(&objects, 2, 1)
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
    for (index = 0; index < 2; ++index) {
        if (
            micros_process_create(&objects, &processes[index])
                != MICROS_KERNEL_OBJECT_OK
            || micros_thread_create(
                &objects,
                processes[index],
                &threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    return true;
}

static bool test_endpoint_encoding_boundaries(void)
{
    struct micros_process_handle process = {0, 1};
    struct micros_process_handle decoded = {UINT16_MAX, UINT32_MAX};
    micros_endpoint_t endpoint = UINT32_C(0xdeadbeef);

    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_pack(process, &endpoint)
    );
    EXPECT_TRUE(endpoint == UINT32_C(0x00001000));
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_unpack(endpoint, &decoded)
    );
    EXPECT_TRUE(decoded.slot == 0 && decoded.generation == 1);

    process.slot = UINT16_C(0x0ffd);
    process.generation = MICROS_ENDPOINT_GENERATION_MAX;
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_pack(process, &endpoint)
    );
    EXPECT_TRUE(endpoint == UINT32_C(0xfffffffd));

    process.slot = UINT16_C(0x0ffe);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    process.slot = UINT16_C(0x0fff);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    process.slot = UINT16_C(0x1000);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    process.slot = 0;
    process.generation = 0;
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    process.generation = MICROS_ENDPOINT_GENERATION_MAX + 1;
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STALE,
        micros_endpoint_unpack(MICROS_ENDPOINT_NONE, &decoded)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STALE,
        micros_endpoint_unpack(MICROS_ENDPOINT_ANY, &decoded)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STALE,
        micros_endpoint_unpack(0, &decoded)
    );
    return true;
}

static bool test_profile_table_is_immutable_and_specific(void)
{
    struct micros_privilege_profile profiles[2];
    const struct micros_privilege_profile *resolved = NULL;

    memset(&registry, 0, sizeof(registry));
    profiles[0] = profile(
        1,
        "CLIENT",
        MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_CALL
            | MICROS_PRIVILEGE_OPERATION_NOTIFY,
        UINT32_C(1) << 2,
        0,
        UINT32_C(1) << 2,
        UINT64_C(1) << 3
    );
    profiles[1] = profile(
        2,
        "SERVER",
        MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_SEND
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
        0,
        UINT32_C(1) << 1,
        0,
        0
    );

    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_initialize(
            &registry,
            profiles,
            2
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_validate(&registry)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_privilege_profile_resolve(
            &registry,
            1,
            &resolved
        )
    );
    EXPECT_TRUE(
        resolved == &registry.profiles[1]
        && strcmp(resolved->name, "CLIENT") == 0
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_privilege_profile_allows_operation(
            &registry,
            2,
            MICROS_PRIVILEGE_OPERATION_REPLY
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_privilege_profile_allows_target(
            &registry,
            1,
            MICROS_PRIVILEGE_OPERATION_CALL,
            2
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_UNAUTHORIZED,
        micros_privilege_profile_allows_target(
            &registry,
            1,
            MICROS_PRIVILEGE_OPERATION_SEND,
            2
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_privilege_profile_allows_kernel_operation(
            &registry,
            1,
            3
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_UNAUTHORIZED,
        micros_privilege_profile_allows_kernel_operation(
            &registry,
            1,
            4
        )
    );

    {
        struct micros_endpoint_registry before = registry;

        EXPECT_ERROR(
            MICROS_ENDPOINT_ERROR_ALREADY_INITIALIZED,
            micros_endpoint_registry_initialize(
                &registry,
                profiles,
                2
            )
        );
        EXPECT_TRUE(memcmp(&registry, &before, sizeof(registry)) == 0);
    }
    return true;
}

static bool test_invalid_profile_tables_preserve_storage(void)
{
    struct micros_privilege_profile profiles[2];
    struct micros_endpoint_registry before;

    profiles[0] = profile(
        1,
        "ONE",
        MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0,
        0,
        0,
        0
    );
    profiles[1] = profile(
        2,
        "TWO",
        MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0,
        0,
        0,
        0
    );

#define EXPECT_REJECTED_TABLE(expected, statement) \
    do { \
        memset(&registry, 0, sizeof(registry)); \
        before = registry; \
        statement; \
        EXPECT_ERROR( \
            (expected), \
            micros_endpoint_registry_initialize( \
                &registry, \
                profiles, \
                2 \
            ) \
        ); \
        EXPECT_TRUE(memcmp(&registry, &before, sizeof(registry)) == 0); \
    } while (false)

    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        profiles[1].id = 1
    );
    profiles[1] = profile(
        2, "TWO", MICROS_PRIVILEGE_OPERATION_RECEIVE, 0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        memcpy(profiles[1].name, profiles[0].name, sizeof(profiles[0].name))
    );
    profiles[1] = profile(
        2, "TWO", MICROS_PRIVILEGE_OPERATION_RECEIVE, 0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        profiles[0].operations |= UINT32_C(0x80000000)
    );
    profiles[0] = profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        profiles[0].id = 0
    );
    profiles[0] = profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        profiles[0].name[0] = '\0'
    );
    profiles[0] = profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        memset(
            profiles[0].name,
            'A',
            sizeof(profiles[0].name)
        )
    );
    profiles[0] = profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        profiles[0].call_targets = UINT32_C(1) << 2
    );
    profiles[0] = profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_CALL,
        UINT32_C(1) << 3, 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        (void)0
    );
    profiles[0] = profile(
        1, "ONE", MICROS_PRIVILEGE_OPERATION_CALL,
        UINT32_C(1), 0, 0, 0
    );
    EXPECT_REJECTED_TABLE(
        MICROS_ENDPOINT_ERROR_PROFILE,
        (void)0
    );

#undef EXPECT_REJECTED_TABLE

    memset(&registry, 0, sizeof(registry));
    before = registry;
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_CAPACITY,
        micros_endpoint_registry_initialize(
            &registry,
            profiles,
            0
        )
    );
    EXPECT_TRUE(memcmp(&registry, &before, sizeof(registry)) == 0);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_CAPACITY,
        micros_endpoint_registry_initialize(
            &registry,
            profiles,
            MICROS_PRIVILEGE_PROFILE_CAPACITY
        )
    );
    EXPECT_TRUE(memcmp(&registry, &before, sizeof(registry)) == 0);
    return true;
}

static bool test_validator_rejects_corruption(void)
{
    struct micros_privilege_profile profiles[2];
    struct micros_endpoint_registry snapshot;

    memset(&registry, 0, sizeof(registry));
    profiles[0] = profile(
        1, "CLIENT", MICROS_PRIVILEGE_OPERATION_CALL,
        UINT32_C(1) << 2, 0, 0, 0
    );
    profiles[1] = profile(
        2, "SERVER", MICROS_PRIVILEGE_OPERATION_RECEIVE,
        0, 0, 0, 0
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_initialize(&registry, profiles, 2)
    );
    snapshot = registry;

#define EXPECT_CORRUPTION(statement) \
    do { \
        registry = snapshot; \
        statement; \
        EXPECT_ERROR( \
            MICROS_ENDPOINT_ERROR_INVARIANT, \
            micros_endpoint_registry_validate(&registry) \
        ); \
    } while (false)

    EXPECT_CORRUPTION(registry.profile_count = 1);
    EXPECT_CORRUPTION(registry.profiles[1].id = 2);
    EXPECT_CORRUPTION(registry.profiles[1].name[0] = '\0');
    EXPECT_CORRUPTION(registry.profiles[1].name[7] = 'X');
    EXPECT_CORRUPTION(
        registry.profiles[1].call_targets =
            UINT32_C(1) << 3
    );
    EXPECT_CORRUPTION(
        registry.endpoints[0].state = MICROS_ENDPOINT_STATE_RESERVED
    );

#undef EXPECT_CORRUPTION
    return true;
}

static bool test_endpoint_lifecycle_and_authorization(void)
{
    micros_endpoint_t endpoints[2] = {0, 0};
    micros_endpoint_t stale_endpoint;
    struct micros_process_handle stale_target;
    const struct micros_endpoint_record *record = NULL;

    EXPECT_TRUE(setup_lifecycle_fixture());
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_reserve(
            &registry,
            &objects,
            processes[0],
            &endpoints[0]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_resolve_internal(
            &registry,
            &objects,
            endpoints[0],
            &record
        )
    );
    EXPECT_TRUE(record->state == MICROS_ENDPOINT_STATE_RESERVED);
    record = (const struct micros_endpoint_record *)(uintptr_t)1;
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_resolve_active(
            &registry,
            &objects,
            endpoints[0],
            &record
        )
    );
    EXPECT_TRUE(
        record
            == (const struct micros_endpoint_record *)(uintptr_t)1
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_reserve(
            &registry,
            &objects,
            processes[1],
            &endpoints[1]
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(&objects, threads[0])
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_release(&objects, processes[0])
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(
            &objects,
            processes[0],
            &threads[0]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_install_profile(
            &registry,
            &objects,
            processes[0],
            1
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_install_profile(
            &registry,
            &objects,
            processes[1],
            2
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_activate(
            &registry,
            &objects,
            endpoints[0]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_activate(
            &registry,
            &objects,
            endpoints[1]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_authorize_target(
            &registry,
            &objects,
            endpoints[0],
            MICROS_PRIVILEGE_OPERATION_CALL,
            endpoints[1]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_UNAUTHORIZED,
        micros_endpoint_authorize_target(
            &registry,
            &objects,
            endpoints[0],
            MICROS_PRIVILEGE_OPERATION_SEND,
            endpoints[1]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_UNAUTHORIZED,
        micros_endpoint_authorize_target(
            &registry,
            &objects,
            endpoints[0],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            endpoints[1]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_authorize_target(
            &registry,
            &objects,
            endpoints[0],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            endpoints[0]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_UNAUTHORIZED,
        micros_endpoint_authorize_target(
            &registry,
            &objects,
            endpoints[1],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            endpoints[0]
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_authorize_target(
            &registry,
            &objects,
            endpoints[1],
            MICROS_PRIVILEGE_OPERATION_NOTIFY,
            endpoints[1]
        )
    );
    stale_target.slot = processes[1].slot;
    stale_target.generation = processes[1].generation + 1;
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_pack(stale_target, &stale_endpoint)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STALE,
        micros_endpoint_authorize_target(
            &registry,
            &objects,
            endpoints[0],
            MICROS_PRIVILEGE_OPERATION_SEND,
            stale_endpoint
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ARGUMENT,
        micros_endpoint_authorize_target(
            &registry,
            &objects,
            endpoints[0],
            0,
            stale_endpoint
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_authorize_operation(
            &registry,
            &objects,
            endpoints[1],
            MICROS_PRIVILEGE_OPERATION_REPLY
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_authorize_kernel_operation(
            &registry,
            &objects,
            endpoints[0],
            3
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(&objects, threads[0])
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_release(&objects, processes[0])
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_registry_validate_objects(
            &registry,
            &objects
        )
    );
    return true;
}

static bool test_profile_install_and_close_require_held_threads(void)
{
    micros_endpoint_t endpoint;
    struct micros_user_context context =
        context_pattern(UINT64_C(0x1000));

    EXPECT_TRUE(setup_lifecycle_fixture());
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_reserve(
            &registry,
            &objects,
            processes[0],
            &endpoint
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_attach_execution_context(
            &objects,
            threads[0],
            UINT64_C(0x10000000),
            UINT64_C(0x10004000),
            &context
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects,
            hart,
            threads[0],
            7,
            100,
            true
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_install_profile(
            &registry,
            &objects,
            processes[0],
            1
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&objects, threads[0])
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_remove(&objects, threads[0])
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_install_profile(
            &registry,
            &objects,
            processes[0],
            1
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_activate(
            &registry,
            &objects,
            endpoint
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects,
            hart,
            threads[0],
            7,
            100,
            true
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_close(
            &registry,
            &objects,
            endpoint
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&objects, threads[0])
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_close(
            &registry,
            &objects,
            endpoint
        )
    );
    EXPECT_TRUE(
        objects.processes[processes[0].slot].primary_endpoint
            == MICROS_PROCESS_ENDPOINT_NONE
        && objects.processes[processes[0].slot].privilege_profile == 0
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STATE,
        micros_endpoint_reserve(
            &registry,
            &objects,
            processes[0],
            &endpoint
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_remove(&objects, threads[0])
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_detach_execution_context(&objects, threads[0])
    );
    return true;
}

static bool test_resolution_rejects_unimplemented_slots(void)
{
    static const uint16_t slots[] = {
        MICROS_PROCESS_CAPACITY - 1,
        MICROS_PROCESS_CAPACITY,
        (UINT16_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1,
    };
    const struct micros_endpoint_record *record;
    size_t index;

    EXPECT_TRUE(setup_lifecycle_fixture());
    for (index = 0; index < sizeof(slots) / sizeof(slots[0]); ++index) {
        struct micros_process_handle owner = {slots[index], 1};
        micros_endpoint_t endpoint;

        record =
            (const struct micros_endpoint_record *)(uintptr_t)1;
        EXPECT_ERROR(
            MICROS_ENDPOINT_OK,
            micros_endpoint_pack(owner, &endpoint)
        );
        EXPECT_ERROR(
            MICROS_ENDPOINT_ERROR_STALE,
            micros_endpoint_resolve_internal(
                &registry,
                &objects,
                endpoint,
                &record
            )
        );
        EXPECT_TRUE(
            record
                == (const struct micros_endpoint_record *)(uintptr_t)1
        );
    }
    return true;
}

static bool test_endpoint_reuse_rejects_stale_generation(void)
{
    micros_endpoint_t first_endpoint;
    micros_endpoint_t replacement_endpoint;
    struct micros_process_handle replacement;
    const struct micros_endpoint_record *record = NULL;

    EXPECT_TRUE(setup_lifecycle_fixture());
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_reserve(
            &registry,
            &objects,
            processes[0],
            &first_endpoint
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_close(
            &registry,
            &objects,
            first_endpoint
        )
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(&objects, threads[0])
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(&objects, processes[0])
    );
    EXPECT_OBJECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &replacement)
    );
    EXPECT_TRUE(
        replacement.slot == processes[0].slot
        && replacement.generation == processes[0].generation + 1
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_reserve(
            &registry,
            &objects,
            replacement,
            &replacement_endpoint
        )
    );
    EXPECT_TRUE(replacement_endpoint != first_endpoint);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STALE,
        micros_endpoint_resolve_internal(
            &registry,
            &objects,
            first_endpoint,
            &record
        )
    );
    return true;
}

static bool test_relationship_validator_rejects_corruption(void)
{
    micros_endpoint_t endpoint;
    struct micros_endpoint_registry registry_snapshot;
    struct micros_kernel_objects objects_snapshot;

    EXPECT_TRUE(setup_lifecycle_fixture());
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_reserve(
            &registry,
            &objects,
            processes[0],
            &endpoint
        )
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_install_profile(
            &registry,
            &objects,
            processes[0],
            1
        )
    );
    registry_snapshot = registry;
    objects_snapshot = objects;

#define EXPECT_RELATION_CORRUPTION(statement) \
    do { \
        registry = registry_snapshot; \
        objects = objects_snapshot; \
        statement; \
        EXPECT_ERROR( \
            MICROS_ENDPOINT_ERROR_INVARIANT, \
            micros_endpoint_registry_validate_objects( \
                &registry, \
                &objects \
            ) \
        ); \
    } while (false)

    EXPECT_RELATION_CORRUPTION(
        ++registry.endpoints[processes[0].slot].owner.generation
    );
    EXPECT_RELATION_CORRUPTION(
        registry.endpoints[processes[0].slot].value = MICROS_ENDPOINT_NONE
    );
    EXPECT_RELATION_CORRUPTION(
        objects.processes[processes[0].slot].primary_endpoint =
            MICROS_ENDPOINT_NONE
    );
    EXPECT_RELATION_CORRUPTION(
        objects.processes[processes[0].slot]
            .endpoint_lifecycle_consumed = false
    );
    EXPECT_RELATION_CORRUPTION(
        objects.processes[processes[0].slot].privilege_profile =
            UINT32_C(257)
    );
    EXPECT_RELATION_CORRUPTION(
        registry.endpoints[processes[0].slot].state =
            MICROS_ENDPOINT_STATE_ACTIVE;
        objects.processes[processes[0].slot].privilege_profile = 0
    );

#undef EXPECT_RELATION_CORRUPTION
    return true;
}

bool micros_endpoint_model_test_run(void);
bool micros_ipc_state_test_run(void);

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {
            "endpoint encoding boundaries",
            test_endpoint_encoding_boundaries,
        },
        {
            "profile table is immutable and specific",
            test_profile_table_is_immutable_and_specific,
        },
        {
            "invalid profile tables preserve storage",
            test_invalid_profile_tables_preserve_storage,
        },
        {
            "validator rejects corruption",
            test_validator_rejects_corruption,
        },
        {
            "endpoint lifecycle and authorization",
            test_endpoint_lifecycle_and_authorization,
        },
        {
            "profile install and close require held threads",
            test_profile_install_and_close_require_held_threads,
        },
        {
            "endpoint reuse rejects stale generation",
            test_endpoint_reuse_rejects_stale_generation,
        },
        {
            "resolution rejects unimplemented slots",
            test_resolution_rejects_unimplemented_slots,
        },
        {
            "relationship validator rejects corruption",
            test_relationship_validator_rejects_corruption,
        },
        {
            "seeded endpoint lifecycle model",
            micros_endpoint_model_test_run,
        },
        {
            "dormant IPC state substrate",
            micros_ipc_state_test_run,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(
                stderr,
                "not ok %zu - %s\n",
                index + 1,
                tests[index].name
            );
            return 1;
        }
        printf("ok %zu - %s\n", index + 1, tests[index].name);
    }
    return 0;
}
