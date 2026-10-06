#include "micros/kernel_objects.h"

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
        enum micros_kernel_object_error actual_error = (expression); \
        if (actual_error != (expected)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected error %d, got %d\n", \
                __FILE__, \
                __LINE__, \
                (int)(expected), \
                (int)actual_error \
            ); \
            return false; \
        } \
    } while (false)

#define EXPECT_ERROR_UNCHANGED(expected, expression) \
    do { \
        struct micros_kernel_objects expected_objects = objects; \
        enum micros_kernel_object_error actual_error = (expression); \
        if (actual_error != (expected)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected error %d, got %d\n", \
                __FILE__, \
                __LINE__, \
                (int)(expected), \
                (int)actual_error \
            ); \
            return false; \
        } \
        if ( \
            memcmp( \
                &objects, \
                &expected_objects, \
                sizeof(objects) \
            ) != 0 \
        ) { \
            fprintf( \
                stderr, \
                "%s:%d: failed operation changed registry\n", \
                __FILE__, \
                __LINE__ \
            ); \
            return false; \
        } \
    } while (false)

static struct micros_kernel_objects objects;

static void reset_objects(void)
{
    memset(&objects, 0, sizeof(objects));
}

static bool handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

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

static bool test_initialization_is_checked_and_one_shot(void)
{
    struct micros_kernel_objects snapshot;

    reset_objects();
    snapshot = objects;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_kernel_objects_initialize(NULL, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_kernel_objects_initialize(&objects, 0, 1)
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_kernel_objects_initialize(
            &objects,
            MICROS_THREAD_CAPACITY + 1,
            1
        )
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_kernel_objects_initialize(&objects, 1, 0)
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_kernel_objects_initialize(
            &objects,
            1,
            MICROS_HART_CAPACITY + 1
        )
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);

    objects.processes[0].generation = 1;
    snapshot = objects;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STORAGE,
        micros_kernel_objects_initialize(&objects, 1, 1)
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(&objects, 1, 1)
    );
    EXPECT_TRUE(objects.max_threads_per_process == 1);
    EXPECT_TRUE(objects.max_harts == 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(&objects)
    );
    snapshot = objects;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ALREADY_INITIALIZED,
        micros_kernel_objects_initialize(&objects, 2, 2)
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);
    return true;
}

static bool test_uninitialized_registry_rejects_every_operation(void)
{
    struct micros_process_handle process = {0, 1};
    struct micros_process_handle process_output = {
        UINT16_MAX,
        UINT32_MAX,
    };
    struct micros_thread_handle thread = {0, 1};
    struct micros_thread_handle thread_output = {
        UINT16_MAX,
        UINT32_MAX,
    };
    struct micros_hart_handle hart = {0, 1};
    struct micros_hart_handle hart_output = {
        UINT16_MAX,
        UINT32_MAX,
    };
    const struct micros_process *resolved_process =
        (const struct micros_process *)(uintptr_t)1;
    const struct micros_thread *resolved_thread =
        (const struct micros_thread *)(uintptr_t)1;
    const struct micros_hart *resolved_hart =
        (const struct micros_hart *)(uintptr_t)1;

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_kernel_objects_validate(&objects)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_create(&objects, &process_output)
    );
    EXPECT_TRUE(
        process_output.slot == UINT16_MAX
        && process_output.generation == UINT32_MAX
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_release(&objects, process)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_attach_address_space(
            &objects,
            process,
            UINT64_C(0x1000)
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_detach_address_space(
            &objects,
            process,
            UINT64_C(0x1000)
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_resolve(
            &objects,
            process,
            &resolved_process
        )
    );
    EXPECT_TRUE(
        resolved_process
        == (const struct micros_process *)(uintptr_t)1
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_create(&objects, process, &thread_output)
    );
    EXPECT_TRUE(
        thread_output.slot == UINT16_MAX
        && thread_output.generation == UINT32_MAX
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_release(&objects, thread)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_resolve(&objects, thread, &resolved_thread)
    );
    EXPECT_TRUE(
        resolved_thread
        == (const struct micros_thread *)(uintptr_t)1
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_register(&objects, 0, &hart_output)
    );
    EXPECT_TRUE(
        hart_output.slot == UINT16_MAX
        && hart_output.generation == UINT32_MAX
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_resolve(&objects, hart, &resolved_hart)
    );
    EXPECT_TRUE(
        resolved_hart == (const struct micros_hart *)(uintptr_t)1
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_resolve_context(
            &objects,
            (uintptr_t)&objects.harts[0],
            &resolved_hart
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_install_trap_stacks(
            &objects,
            hart,
            UINT64_C(0x1000),
            UINT64_C(0x5000),
            UINT64_C(0x6000),
            UINT64_C(0x7000)
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_bind_thread(&objects, hart, thread)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_clear_thread(&objects, hart, thread)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_current_thread(
            &objects,
            hart,
            &thread_output
        )
    );
    return true;
}

static bool test_fixed_capacities_report_exhaustion_atomically(void)
{
    struct micros_process_handle
        processes[MICROS_PROCESS_CAPACITY];
    struct micros_thread_handle threads[MICROS_THREAD_CAPACITY];
    struct micros_hart_handle harts[MICROS_HART_CAPACITY];
    struct micros_process_handle process_output = {
        UINT16_MAX,
        UINT32_MAX,
    };
    struct micros_thread_handle thread_output = {
        UINT16_MAX,
        UINT32_MAX,
    };
    struct micros_hart_handle hart_output = {
        UINT16_MAX,
        UINT32_MAX,
    };
    size_t index;

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(&objects, 1, 1)
    );
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_process_create(&objects, &processes[index])
        );
        EXPECT_TRUE(processes[index].slot == index);
    }
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED,
        micros_process_create(&objects, &process_output)
    );
    EXPECT_TRUE(
        process_output.slot == UINT16_MAX
        && process_output.generation == UINT32_MAX
    );

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(
            &objects,
            MICROS_THREAD_CAPACITY,
            1
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &processes[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &processes[1])
    );
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        struct micros_process_handle owner =
            processes[index % 2];

        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_thread_create(&objects, owner, &threads[index])
        );
        EXPECT_TRUE(threads[index].slot == index);
    }
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED,
        micros_thread_create(
            &objects,
            processes[0],
            &thread_output
        )
    );
    EXPECT_TRUE(
        thread_output.slot == UINT16_MAX
        && thread_output.generation == UINT32_MAX
    );

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(
            &objects,
            1,
            MICROS_HART_CAPACITY
        )
    );
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_hart_register(&objects, index, &harts[index])
        );
        EXPECT_TRUE(harts[index].slot == index);
    }
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_hart_register(
            &objects,
            MICROS_HART_CAPACITY,
            &hart_output
        )
    );
    EXPECT_TRUE(
        hart_output.slot == UINT16_MAX
        && hart_output.generation == UINT32_MAX
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(&objects)
    );
    return true;
}

static bool test_process_slots_reuse_with_new_generations(void)
{
    struct micros_process_handle first = {UINT16_MAX, UINT32_MAX};
    struct micros_process_handle second = {UINT16_MAX, UINT32_MAX};
    struct micros_process_handle replacement = {
        UINT16_MAX,
        UINT32_MAX,
    };
    const struct micros_process *process = NULL;

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(&objects, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &second)
    );
    EXPECT_TRUE(first.slot == 0 && first.generation == 1);
    EXPECT_TRUE(second.slot == 1 && second.generation == 1);
    EXPECT_TRUE(objects.live_process_count == 2);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(&objects, first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &replacement)
    );
    EXPECT_TRUE(replacement.slot == first.slot);
    EXPECT_TRUE(replacement.generation == first.generation + 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_process_resolve(&objects, first, &process)
    );
    EXPECT_TRUE(process == NULL);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_resolve(&objects, replacement, &process)
    );
    EXPECT_TRUE(process == &objects.processes[replacement.slot]);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(&objects)
    );
    return true;
}

static bool test_process_address_space_lifecycle_is_exact(void)
{
    const uintptr_t root = UINT64_C(0x0000000081000000);
    struct micros_process_handle process = {0};
    struct micros_process_handle stale;
    struct micros_thread_handle thread = {0};
    struct micros_kernel_objects snapshot;

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(&objects, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &process)
    );
    stale = process;

    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_process_attach_address_space(NULL, process, root)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_process_detach_address_space(NULL, process, root)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_process_attach_address_space(&objects, process, 0)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_attach_address_space(&objects, process, root)
    );
    EXPECT_TRUE(
        objects.processes[process.slot].address_space_root == root
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(&objects)
    );

    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_attach_address_space(
            &objects,
            process,
            root + UINT64_C(0x1000)
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_detach_address_space(
            &objects,
            process,
            root + UINT64_C(0x1000)
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_release(&objects, process)
    );

    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(&objects, process, &thread)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_detach_address_space(&objects, process, root)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(&objects, thread)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_detach_address_space(&objects, process, root)
    );
    EXPECT_TRUE(
        objects.processes[process.slot].address_space_root == 0
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(&objects, process)
    );

    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &process)
    );
    snapshot = objects;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_process_attach_address_space(&objects, stale, root)
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_process_detach_address_space(&objects, stale, root)
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);
    return true;
}

static bool test_thread_slots_reuse_with_new_generations(void)
{
    struct micros_process_handle owner = {0};
    struct micros_thread_handle first = {0};
    struct micros_thread_handle replacement = {0};
    struct micros_hart_handle hart = {0};
    const struct micros_thread *resolved = NULL;

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(&objects, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &owner)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(&objects, owner, &first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(&objects, first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(&objects, owner, &replacement)
    );
    EXPECT_TRUE(replacement.slot == first.slot);
    EXPECT_TRUE(replacement.generation == first.generation + 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_thread_resolve(&objects, first, &resolved)
    );
    EXPECT_TRUE(resolved == NULL);
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_thread_release(&objects, first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(&objects, 0, &hart)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_hart_bind_thread(&objects, hart, first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_resolve(&objects, replacement, &resolved)
    );
    EXPECT_TRUE(resolved == &objects.threads[replacement.slot]);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(&objects)
    );
    return true;
}

static bool test_thread_ownership_and_production_limit(void)
{
    struct micros_process_handle owner = {0};
    struct micros_process_handle other = {0};
    struct micros_thread_handle first = {0};
    struct micros_thread_handle second = {
        UINT16_MAX,
        UINT32_MAX,
    };
    struct micros_kernel_objects snapshot;

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(&objects, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &owner)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &other)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(&objects, owner, &first)
    );
    EXPECT_TRUE(objects.processes[owner.slot].live_thread_count == 1);
    snapshot = objects;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_THREAD_LIMIT,
        micros_thread_create(&objects, owner, &second)
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);
    EXPECT_TRUE(
        second.slot == UINT16_MAX
        && second.generation == UINT32_MAX
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_release(&objects, owner)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(&objects, other, &second)
    );
    EXPECT_TRUE(second.slot == 1 && second.generation == 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(&objects, first)
    );
    EXPECT_TRUE(objects.processes[owner.slot].live_thread_count == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(&objects, owner)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_thread_create(&objects, owner, &first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(&objects)
    );
    return true;
}

static bool test_model_policy_supports_multiple_threads(void)
{
    struct micros_process_handle owner = {0};
    struct micros_thread_handle threads[4];
    struct micros_thread_handle unchanged = {UINT16_MAX, UINT32_MAX};
    size_t index;

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(&objects, 4, 2)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &owner)
    );
    for (index = 0; index < 4; ++index) {
        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_thread_create(&objects, owner, &threads[index])
        );
        EXPECT_TRUE(threads[index].slot == index);
    }
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_THREAD_LIMIT,
        micros_thread_create(&objects, owner, &unchanged)
    );
    EXPECT_TRUE(objects.processes[owner.slot].live_thread_count == 4);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(&objects)
    );
    return true;
}

static bool test_harts_route_current_threads_without_overwrite(void)
{
    struct micros_hart_handle hart_zero = {0};
    struct micros_hart_handle hart_one = {0};
    struct micros_hart_handle unchanged_hart = {
        UINT16_MAX,
        UINT32_MAX,
    };
    struct micros_process_handle owner = {0};
    struct micros_thread_handle first = {0};
    struct micros_thread_handle second = {0};
    struct micros_thread_handle current = {0};
    struct micros_kernel_objects snapshot;

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(&objects, 2, 2)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(&objects, 7, &hart_zero)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(&objects, 11, &hart_one)
    );
    EXPECT_TRUE(hart_zero.slot == 0 && hart_one.slot == 1);
    snapshot = objects;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_DUPLICATE_HART,
        micros_hart_register(&objects, 7, &unchanged_hart)
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);

    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_install_trap_stacks(
            &objects,
            hart_zero,
            UINT64_C(0x1000),
            UINT64_C(0x5000),
            UINT64_C(0x6000),
            UINT64_C(0x7000)
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_install_trap_stacks(
            &objects,
            hart_one,
            UINT64_C(0x8000),
            UINT64_C(0xc000),
            UINT64_C(0xd000),
            UINT64_C(0xe000)
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &owner)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(&objects, owner, &first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(&objects, owner, &second)
    );

    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_bind_thread(&objects, hart_zero, first)
    );
    EXPECT_TRUE(
        objects.threads[first.slot].state
        == MICROS_THREAD_STATE_RUNNING
    );
    snapshot = objects;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_hart_bind_thread(&objects, hart_zero, second)
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_hart_bind_thread(&objects, hart_one, first)
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_bind_thread(&objects, hart_one, second)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_thread_release(&objects, first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_current_thread(&objects, hart_zero, &current)
    );
    EXPECT_TRUE(handles_equal(current, first));
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_hart_clear_thread(&objects, hart_zero, second)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_clear_thread(&objects, hart_zero, first)
    );
    EXPECT_TRUE(
        objects.threads[first.slot].state
        == MICROS_THREAD_STATE_INACTIVE
    );
    current.slot = UINT16_MAX;
    current.generation = UINT32_MAX;
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_hart_current_thread(&objects, hart_zero, &current)
    );
    EXPECT_TRUE(
        current.slot == UINT16_MAX
        && current.generation == UINT32_MAX
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_clear_thread(&objects, hart_one, second)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(&objects)
    );
    return true;
}

static bool test_hart_policy_and_stack_validation_are_atomic(void)
{
    struct micros_hart_handle hart = {0};
    struct micros_hart_handle unchanged = {UINT16_MAX, UINT32_MAX};
    struct micros_kernel_objects snapshot;
    const struct micros_hart *resolved = NULL;

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(&objects, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(&objects, 0, &hart)
    );
    snapshot = objects;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_hart_register(&objects, 1, &unchanged)
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STACK,
        micros_hart_install_trap_stacks(
            &objects,
            hart,
            UINT64_C(0x1001),
            UINT64_C(0x5000),
            UINT64_C(0x6000),
            UINT64_C(0x7000)
        )
    );
    EXPECT_TRUE(memcmp(&objects, &snapshot, sizeof(objects)) == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_install_trap_stacks(
            &objects,
            hart,
            UINT64_C(0x1000),
            UINT64_C(0x5000),
            UINT64_C(0x6000),
            UINT64_C(0x7000)
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_resolve_context(
            &objects,
            (uintptr_t)&objects.harts[hart.slot],
            &resolved
        )
    );
    EXPECT_TRUE(resolved == &objects.harts[hart.slot]);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_hart_resolve_context(
            &objects,
            (uintptr_t)&objects.harts[hart.slot] + 1,
            &resolved
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(&objects)
    );
    return true;
}

static bool test_generation_boundaries_and_quarantine(void)
{
    uint32_t next = UINT32_C(0xfeedface);
    struct micros_process_handle process = {0};
    struct micros_process_handle stale_process = {0};
    struct micros_process_handle replacement = {0};
    struct micros_thread_handle thread = {0};
    struct micros_thread_handle stale_thread = {0};
    struct micros_thread_handle final_thread = {0};
    const struct micros_process *resolved_process = NULL;
    const struct micros_thread *resolved_thread = NULL;

    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_next_generation(0, 0, &next)
    );
    EXPECT_TRUE(next == 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_next_generation(
            0,
            MICROS_PROCESS_GENERATION_MAX - 1,
            &next
        )
    );
    EXPECT_TRUE(next == MICROS_PROCESS_GENERATION_MAX);
    next = UINT32_C(0xfeedface);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED,
        micros_process_next_generation(
            UINT16_C(0x0ffe),
            MICROS_PROCESS_GENERATION_MAX - 1,
            &next
        )
    );
    EXPECT_TRUE(next == UINT32_C(0xfeedface));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED,
        micros_process_next_generation(
            UINT16_C(0x0fff),
            MICROS_PROCESS_GENERATION_MAX - 1,
            &next
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED,
        micros_process_next_generation(
            0,
            MICROS_PROCESS_GENERATION_MAX,
            &next
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_process_next_generation(UINT16_C(0x1000), 0, &next)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_next_generation(UINT32_MAX - 1, &next)
    );
    EXPECT_TRUE(next == UINT32_MAX);
    next = UINT32_C(0xfeedface);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED,
        micros_thread_next_generation(UINT32_MAX, &next)
    );
    EXPECT_TRUE(next == UINT32_C(0xfeedface));

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(&objects, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &process)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(&objects, process, &thread)
    );
    objects.threads[thread.slot].generation = UINT32_MAX - 1;
    thread.generation = UINT32_MAX - 1;
    stale_thread = thread;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(&objects, thread)
    );
    EXPECT_TRUE(
        objects.threads[thread.slot].slot_state
        == MICROS_KERNEL_OBJECT_SLOT_FREE
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(&objects, process, &final_thread)
    );
    EXPECT_TRUE(final_thread.slot == stale_thread.slot);
    EXPECT_TRUE(final_thread.generation == UINT32_MAX);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_thread_resolve(
            &objects,
            stale_thread,
            &resolved_thread
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(&objects, final_thread)
    );
    EXPECT_TRUE(
        objects.threads[final_thread.slot].slot_state
        == MICROS_KERNEL_OBJECT_SLOT_QUARANTINED
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(&objects)
    );

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(&objects, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &process)
    );
    objects.processes[process.slot].generation =
        MICROS_PROCESS_GENERATION_MAX - 1;
    process.generation = MICROS_PROCESS_GENERATION_MAX - 1;
    stale_process = process;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(&objects, process)
    );
    EXPECT_TRUE(
        objects.processes[process.slot].slot_state
        == MICROS_KERNEL_OBJECT_SLOT_FREE
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &process)
    );
    EXPECT_TRUE(process.slot == stale_process.slot);
    EXPECT_TRUE(
        process.generation == MICROS_PROCESS_GENERATION_MAX
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_process_resolve(
            &objects,
            stale_process,
            &resolved_process
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(&objects, process)
    );
    EXPECT_TRUE(
        objects.processes[process.slot].slot_state
        == MICROS_KERNEL_OBJECT_SLOT_QUARANTINED
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &replacement)
    );
    EXPECT_TRUE(replacement.slot == 1 && replacement.generation == 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(&objects)
    );
    return true;
}

static bool test_invalid_arguments_and_stale_handles_preserve_outputs(void)
{
    struct micros_process_handle process = {0};
    struct micros_process_handle stale_process = {0};
    struct micros_thread_handle thread = {0};
    struct micros_thread_handle stale_thread = {0};
    struct micros_hart_handle hart = {0};
    struct micros_hart_handle stale_hart = {0};
    const struct micros_process *process_output =
        (const struct micros_process *)(uintptr_t)1;
    const struct micros_thread *thread_output =
        (const struct micros_thread *)(uintptr_t)1;
    const struct micros_hart *hart_output =
        (const struct micros_hart *)(uintptr_t)1;

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(&objects, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &process)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(&objects, process, &thread)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(&objects, 0, &hart)
    );
    stale_process = process;
    ++stale_process.generation;
    stale_thread = thread;
    ++stale_thread.generation;
    stale_hart = hart;
    ++stale_hart.generation;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_process_resolve(
            &objects,
            stale_process,
            &process_output
        )
    );
    EXPECT_TRUE(
        process_output
        == (const struct micros_process *)(uintptr_t)1
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_thread_resolve(&objects, stale_thread, &thread_output)
    );
    EXPECT_TRUE(
        thread_output
        == (const struct micros_thread *)(uintptr_t)1
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_hart_resolve(&objects, stale_hart, &hart_output)
    );
    EXPECT_TRUE(
        hart_output == (const struct micros_hart *)(uintptr_t)1
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_hart_resolve(&objects, hart, NULL)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_process_create(&objects, NULL)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_thread_create(&objects, process, NULL)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_hart_register(&objects, 1, NULL)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_process_next_generation(0, 0, NULL)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_thread_next_generation(0, NULL)
    );
    return true;
}

static uint32_t model_random(uint32_t *state)
{
    uint32_t value = *state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

struct lifecycle_model_process {
    bool live;
    uint32_t generation;
    size_t thread_count;
    uintptr_t address_space_root;
};

struct lifecycle_model_thread {
    bool live;
    uint32_t generation;
    struct micros_process_handle owner;
    enum micros_thread_state state;
};

struct lifecycle_model_hart {
    bool busy;
    struct micros_thread_handle current;
};

struct lifecycle_model {
    struct lifecycle_model_process processes[MICROS_PROCESS_CAPACITY];
    struct lifecycle_model_thread threads[MICROS_THREAD_CAPACITY];
    struct lifecycle_model_hart harts[2];
    size_t process_count;
    size_t thread_count;
};

static size_t model_lowest_free_process(
    const struct lifecycle_model *model
)
{
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (!model->processes[index].live) {
            return index;
        }
    }
    return MICROS_PROCESS_CAPACITY;
}

static size_t model_lowest_free_thread(
    const struct lifecycle_model *model
)
{
    size_t index;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        if (!model->threads[index].live) {
            return index;
        }
    }
    return MICROS_THREAD_CAPACITY;
}

static struct micros_process_handle model_process_handle(
    const struct lifecycle_model *model,
    size_t slot
)
{
    struct micros_process_handle handle = {
        (uint16_t)slot,
        model->processes[slot].generation,
    };

    if (handle.generation == 0) {
        handle.generation = 1;
    }
    return handle;
}

static struct micros_thread_handle model_thread_handle(
    const struct lifecycle_model *model,
    size_t slot
)
{
    struct micros_thread_handle handle = {
        (uint16_t)slot,
        model->threads[slot].generation,
    };

    if (handle.generation == 0) {
        handle.generation = 1;
    }
    return handle;
}

static bool model_reports_expected_error(
    enum micros_kernel_object_error expected,
    enum micros_kernel_object_error actual,
    size_t step,
    const char *operation
)
{
    if (expected == actual) {
        return true;
    }
    fprintf(
        stderr,
        "seeded model seed=0x%08x step=%zu operation=%s "
        "expected=%d actual=%d\n",
        UINT32_C(0x7a31d4c9),
        step,
        operation,
        (int)expected,
        (int)actual
    );
    return false;
}

static bool lifecycle_model_matches_registry(
    const struct lifecycle_model *model,
    size_t step
)
{
    size_t index;

    if (
        objects.live_process_count != model->process_count
        || objects.live_thread_count != model->thread_count
        || objects.registered_hart_count != 2
        || objects.max_threads_per_process != 3
        || objects.max_harts != 2
    ) {
        goto mismatch;
    }

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        const struct lifecycle_model_process *expected =
            &model->processes[index];
        const struct micros_process *actual = &objects.processes[index];

        if (
            actual->slot_state
                != (
                    expected->live
                        ? MICROS_KERNEL_OBJECT_SLOT_LIVE
                        : MICROS_KERNEL_OBJECT_SLOT_FREE
                )
            || actual->generation != expected->generation
            || actual->live_thread_count != expected->thread_count
            || actual->address_space_root
                != expected->address_space_root
            || actual->primary_endpoint
                != MICROS_PROCESS_ENDPOINT_NONE
            || actual->privilege_profile != 0
        ) {
            goto mismatch;
        }
    }

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct lifecycle_model_thread *expected =
            &model->threads[index];
        const struct micros_thread *actual = &objects.threads[index];

        if (
            actual->slot_state
                != (
                    expected->live
                        ? MICROS_KERNEL_OBJECT_SLOT_LIVE
                        : MICROS_KERNEL_OBJECT_SLOT_FREE
                )
            || actual->generation != expected->generation
        ) {
            goto mismatch;
        }
        if (
            expected->live
            && (
                !process_handles_equal(actual->owner, expected->owner)
                || actual->state != expected->state
            )
        ) {
            goto mismatch;
        }
        if (
            !expected->live
            && (
                actual->owner.generation != 0
                || actual->state != MICROS_THREAD_STATE_INACTIVE
            )
        ) {
            goto mismatch;
        }
    }

    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *actual = &objects.harts[index];

        if (index < 2) {
            const struct lifecycle_model_hart *expected =
                &model->harts[index];

            if (
                actual->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
                || actual->generation != 1
                || actual->hardware_id != index
                || actual->trap_installed
                || actual->timer.initialized
                || actual->timer.active
                || actual->timer.ticks != 0
            ) {
                goto mismatch;
            }
            if (
                expected->busy
                && !handles_equal(
                    actual->current_thread,
                    expected->current
                )
            ) {
                goto mismatch;
            }
            if (
                !expected->busy
                && actual->current_thread.generation != 0
            ) {
                goto mismatch;
            }
        } else if (
            actual->slot_state != MICROS_KERNEL_OBJECT_SLOT_FREE
            || actual->generation != 0
        ) {
            goto mismatch;
        }
    }

    if (
        micros_kernel_objects_validate(&objects)
        != MICROS_KERNEL_OBJECT_OK
    ) {
        goto mismatch;
    }
    return true;

mismatch:
    fprintf(
        stderr,
        "seeded model seed=0x%08x step=%zu registry mismatch\n",
        UINT32_C(0x7a31d4c9),
        step
    );
    return false;
}

static bool test_seeded_lifecycle_model(void)
{
    enum {
        MODEL_STEPS = 4096,
    };
    struct lifecycle_model model = {0};
    struct micros_hart_handle harts[2];
    uint32_t random_state = UINT32_C(0x7a31d4c9);
    size_t step;

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(&objects, 3, 2)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(&objects, 0, &harts[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(&objects, 1, &harts[1])
    );

    for (step = 0; step < MODEL_STEPS; ++step) {
        uint32_t value = model_random(&random_state);
        size_t slot;
        enum micros_kernel_object_error expected;
        enum micros_kernel_object_error actual;

        switch (value % 8) {
        case 0: {
            struct micros_process_handle output = {
                UINT16_MAX,
                UINT32_MAX,
            };

            slot = model_lowest_free_process(&model);
            expected = (
                slot == MICROS_PROCESS_CAPACITY
                    ? MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED
                    : MICROS_KERNEL_OBJECT_OK
            );
            actual = micros_process_create(&objects, &output);
            if (
                !model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "process-create"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                ++model.processes[slot].generation;
                model.processes[slot].live = true;
                ++model.process_count;
                if (
                    output.slot != slot
                    || output.generation
                        != model.processes[slot].generation
                ) {
                    return false;
                }
            } else if (
                output.slot != UINT16_MAX
                || output.generation != UINT32_MAX
            ) {
                return false;
            }
            break;
        }
        case 1: {
            struct micros_process_handle handle;

            slot = (value >> 8) % MICROS_PROCESS_CAPACITY;
            handle = model_process_handle(&model, slot);
            if (!model.processes[slot].live) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
            } else if (
                model.processes[slot].thread_count != 0
                || model.processes[slot].address_space_root != 0
            ) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
            } else {
                expected = MICROS_KERNEL_OBJECT_OK;
            }
            actual = micros_process_release(&objects, handle);
            if (
                !model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "process-release"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                model.processes[slot].live = false;
                model.processes[slot].address_space_root = 0;
                --model.process_count;
            }
            break;
        }
        case 2: {
            size_t owner_slot =
                (value >> 8) % MICROS_PROCESS_CAPACITY;
            struct micros_process_handle owner =
                model_process_handle(&model, owner_slot);
            struct micros_thread_handle output = {
                UINT16_MAX,
                UINT32_MAX,
            };

            slot = model_lowest_free_thread(&model);
            if (!model.processes[owner_slot].live) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
            } else if (
                model.processes[owner_slot].thread_count >= 3
            ) {
                expected = MICROS_KERNEL_OBJECT_ERROR_THREAD_LIMIT;
            } else if (slot == MICROS_THREAD_CAPACITY) {
                expected = MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED;
            } else {
                expected = MICROS_KERNEL_OBJECT_OK;
            }
            actual = micros_thread_create(&objects, owner, &output);
            if (
                !model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "thread-create"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                ++model.threads[slot].generation;
                model.threads[slot].live = true;
                model.threads[slot].owner = owner;
                model.threads[slot].state =
                    MICROS_THREAD_STATE_INACTIVE;
                ++model.processes[owner_slot].thread_count;
                ++model.thread_count;
                if (
                    output.slot != slot
                    || output.generation
                        != model.threads[slot].generation
                ) {
                    return false;
                }
            } else if (
                output.slot != UINT16_MAX
                || output.generation != UINT32_MAX
            ) {
                return false;
            }
            break;
        }
        case 3: {
            struct micros_thread_handle handle;

            slot = (value >> 8) % MICROS_THREAD_CAPACITY;
            handle = model_thread_handle(&model, slot);
            if (!model.threads[slot].live) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
            } else if (
                model.threads[slot].state
                != MICROS_THREAD_STATE_INACTIVE
            ) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
            } else {
                expected = MICROS_KERNEL_OBJECT_OK;
            }
            actual = micros_thread_release(&objects, handle);
            if (
                !model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "thread-release"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                size_t owner_slot =
                    model.threads[slot].owner.slot;

                model.threads[slot].live = false;
                model.threads[slot].owner.generation = 0;
                model.threads[slot].owner.slot = 0;
                --model.processes[owner_slot].thread_count;
                --model.thread_count;
            }
            break;
        }
        case 4: {
            size_t hart_slot = (value >> 8) % 2;
            struct micros_thread_handle handle;

            slot = (value >> 16) % MICROS_THREAD_CAPACITY;
            handle = model_thread_handle(&model, slot);
            if (!model.threads[slot].live) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
            } else if (
                model.harts[hart_slot].busy
                || model.threads[slot].state
                    != MICROS_THREAD_STATE_INACTIVE
            ) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
            } else {
                expected = MICROS_KERNEL_OBJECT_OK;
            }
            actual = micros_hart_bind_thread(
                &objects,
                harts[hart_slot],
                handle
            );
            if (
                !model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "hart-bind"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                model.harts[hart_slot].busy = true;
                model.harts[hart_slot].current = handle;
                model.threads[slot].state =
                    MICROS_THREAD_STATE_RUNNING;
            }
            break;
        }
        case 5: {
            size_t hart_slot = (value >> 8) % 2;
            struct micros_thread_handle handle;

            slot = (value >> 16) % MICROS_THREAD_CAPACITY;
            handle = model_thread_handle(&model, slot);
            if (!model.threads[slot].live) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
            } else if (
                !model.harts[hart_slot].busy
                || !handles_equal(
                    model.harts[hart_slot].current,
                    handle
                )
            ) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
            } else {
                expected = MICROS_KERNEL_OBJECT_OK;
            }
            actual = micros_hart_clear_thread(
                &objects,
                harts[hart_slot],
                handle
            );
            if (
                !model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "hart-clear"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                model.harts[hart_slot].busy = false;
                model.harts[hart_slot].current.generation = 0;
                model.harts[hart_slot].current.slot = 0;
                model.threads[slot].state =
                    MICROS_THREAD_STATE_INACTIVE;
            }
            break;
        }
        case 6: {
            struct micros_process_handle handle;
            uintptr_t root;

            slot = (value >> 8) % MICROS_PROCESS_CAPACITY;
            handle = model_process_handle(&model, slot);
            root = ((uintptr_t)slot + 1) * UINT64_C(0x1000);
            if (!model.processes[slot].live) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
            } else if (
                model.processes[slot].address_space_root != 0
            ) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
            } else {
                expected = MICROS_KERNEL_OBJECT_OK;
            }
            actual = micros_process_attach_address_space(
                &objects,
                handle,
                root
            );
            if (
                !model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "address-space-attach"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                model.processes[slot].address_space_root = root;
            }
            break;
        }
        case 7: {
            struct micros_process_handle handle;
            uintptr_t expected_root;

            slot = (value >> 8) % MICROS_PROCESS_CAPACITY;
            handle = model_process_handle(&model, slot);
            expected_root =
                model.processes[slot].address_space_root;
            if (!model.processes[slot].live) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STALE;
            } else if (
                expected_root == 0
                || model.processes[slot].thread_count != 0
            ) {
                expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
            } else {
                if (((value >> 16) & 1U) != 0) {
                    ++expected_root;
                    expected = MICROS_KERNEL_OBJECT_ERROR_STATE;
                } else {
                    expected = MICROS_KERNEL_OBJECT_OK;
                }
            }
            actual = micros_process_detach_address_space(
                &objects,
                handle,
                expected_root
            );
            if (
                !model_reports_expected_error(
                    expected,
                    actual,
                    step,
                    "address-space-detach"
                )
            ) {
                return false;
            }
            if (expected == MICROS_KERNEL_OBJECT_OK) {
                model.processes[slot].address_space_root = 0;
            }
            break;
        }
        }

        if (!lifecycle_model_matches_registry(&model, step)) {
            return false;
        }
    }
    return true;
}

static bool test_validator_rejects_corrupt_relationships(void)
{
    struct micros_process_handle owner = {0};
    struct micros_thread_handle first = {0};
    struct micros_thread_handle second = {0};
    struct micros_hart_handle hart_zero = {0};
    struct micros_hart_handle hart_one = {0};
    struct micros_kernel_objects snapshot;

#define EXPECT_CORRUPTION(statement) \
    do { \
        objects = snapshot; \
        statement; \
        EXPECT_ERROR( \
            MICROS_KERNEL_OBJECT_ERROR_INVARIANT, \
            micros_kernel_objects_validate(&objects) \
        ); \
    } while (false)

    reset_objects();
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(&objects, 2, 2)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(&objects, 0, &hart_zero)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(&objects, 1, &hart_one)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(&objects, &owner)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(&objects, owner, &first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(&objects, owner, &second)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_bind_thread(&objects, hart_zero, first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(&objects)
    );
    snapshot = objects;

    EXPECT_CORRUPTION(
        ++objects.processes[owner.slot].live_thread_count
    );
    EXPECT_CORRUPTION(
        ++objects.threads[second.slot].owner.generation
    );
    EXPECT_CORRUPTION(objects.live_process_count = 0);
    EXPECT_CORRUPTION(objects.live_thread_count = 0);
    EXPECT_CORRUPTION(
        objects.harts[hart_zero.slot].current_thread.generation = 0
    );
    EXPECT_CORRUPTION(
        objects.threads[second.slot].state =
            MICROS_THREAD_STATE_RUNNING
    );
    EXPECT_CORRUPTION(
        objects.harts[hart_one.slot].current_thread = first
    );
    EXPECT_CORRUPTION(
        objects.harts[hart_zero.slot].current_thread = second
    );
    EXPECT_CORRUPTION(
        objects.harts[hart_one.slot].hardware_id =
            objects.harts[hart_zero.slot].hardware_id
    );
    EXPECT_CORRUPTION(objects.registered_hart_count = 1);
    EXPECT_CORRUPTION(objects.max_harts = 1);
    EXPECT_CORRUPTION(objects.max_threads_per_process = 1);
    EXPECT_CORRUPTION(objects.processes[owner.slot].generation = 0);
    EXPECT_CORRUPTION(
        objects.processes[owner.slot].generation =
            MICROS_PROCESS_GENERATION_MAX + 1
    );
    EXPECT_CORRUPTION(objects.threads[second.slot].generation = 0);
    EXPECT_CORRUPTION(
        objects.threads[2].owner = owner
    );
    EXPECT_CORRUPTION(
        objects.processes[1].address_space_root =
            UINT64_C(0x1000)
    );
    EXPECT_CORRUPTION(
        objects.processes[1].slot_state =
            MICROS_KERNEL_OBJECT_SLOT_QUARANTINED;
        objects.processes[1].generation = 1;
        objects.processes[1].address_space_root =
            UINT64_C(0x1000)
    );
    EXPECT_CORRUPTION(
        objects.threads[2].slot_state =
            MICROS_KERNEL_OBJECT_SLOT_QUARANTINED;
        objects.threads[2].generation = 1;
        objects.threads[2].owner = owner
    );
    EXPECT_CORRUPTION(
        objects.harts[2].current_thread = first
    );
    EXPECT_CORRUPTION(
        objects.harts[hart_zero.slot].trap_installed = true
    );
    EXPECT_CORRUPTION(
        objects.harts[hart_one.slot].generation = 0
    );

#undef EXPECT_CORRUPTION
    return true;
}

int main(void)
{
    const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {
            "initialization is checked and one shot",
            test_initialization_is_checked_and_one_shot,
        },
        {
            "uninitialized registry rejects every operation",
            test_uninitialized_registry_rejects_every_operation,
        },
        {
            "fixed capacities report exhaustion atomically",
            test_fixed_capacities_report_exhaustion_atomically,
        },
        {
            "process slots reuse with new generations",
            test_process_slots_reuse_with_new_generations,
        },
        {
            "process address-space lifecycle is exact",
            test_process_address_space_lifecycle_is_exact,
        },
        {
            "thread slots reuse with new generations",
            test_thread_slots_reuse_with_new_generations,
        },
        {
            "thread ownership and production limit",
            test_thread_ownership_and_production_limit,
        },
        {
            "model policy supports multiple threads",
            test_model_policy_supports_multiple_threads,
        },
        {
            "harts route current threads without overwrite",
            test_harts_route_current_threads_without_overwrite,
        },
        {
            "hart policy and stack validation are atomic",
            test_hart_policy_and_stack_validation_are_atomic,
        },
        {
            "generation boundaries and quarantine",
            test_generation_boundaries_and_quarantine,
        },
        {
            "invalid arguments preserve outputs",
            test_invalid_arguments_and_stale_handles_preserve_outputs,
        },
        {
            "seeded lifecycle model",
            test_seeded_lifecycle_model,
        },
        {
            "validator rejects corrupt relationships",
            test_validator_rejects_corrupt_relationships,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(stderr, "not ok %zu - %s\n", index + 1, tests[index].name);
            return 1;
        }
        printf("ok %zu - %s\n", index + 1, tests[index].name);
    }
    return 0;
}
