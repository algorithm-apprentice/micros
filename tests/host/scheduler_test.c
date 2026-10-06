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

#define EXPECT_UNCHANGED(expected, expression) \
    do { \
        struct micros_kernel_objects before = objects; \
        EXPECT_ERROR((expected), (expression)); \
        EXPECT_TRUE(memcmp(&objects, &before, sizeof(objects)) == 0); \
    } while (false)

enum {
    TEST_THREAD_COUNT = 8,
};

static struct micros_kernel_objects objects;
static struct micros_process_handle process;
static struct micros_thread_handle threads[TEST_THREAD_COUNT];
static struct micros_hart_handle harts[2];

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

static struct micros_user_context context_pattern(uint64_t base)
{
    struct micros_user_context context;
    uint64_t words[
        sizeof(struct micros_user_context) / sizeof(uint64_t)
    ];
    size_t index;

    for (
        index = 0;
        index < sizeof(words) / sizeof(words[0]);
        ++index
    ) {
        words[index] = base + index;
    }
    memcpy(&context, words, sizeof(context));
    return context;
}

static bool setup_fixture(void)
{
    size_t index;

    memset(&objects, 0, sizeof(objects));
    if (
        micros_kernel_objects_initialize(
            &objects,
            TEST_THREAD_COUNT,
            2
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&objects, 0, &harts[0])
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&objects, 1, &harts[1])
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            &objects,
            harts[0],
            UINT64_C(0x01000000),
            UINT64_C(0x01004000),
            UINT64_C(0x02000000),
            UINT64_C(0x02001000)
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            &objects,
            harts[1],
            UINT64_C(0x03000000),
            UINT64_C(0x03004000),
            UINT64_C(0x04000000),
            UINT64_C(0x04001000)
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_process_create(&objects, &process)
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }

    for (index = 0; index < TEST_THREAD_COUNT; ++index) {
        struct micros_user_context context =
            context_pattern(UINT64_C(0x1000) + index * UINT64_C(0x100));
        uintptr_t bottom =
            UINT64_C(0x10000000)
            + index * UINT64_C(0x00008000);

        if (
            micros_thread_create(
                &objects,
                process,
                &threads[index]
            ) != MICROS_KERNEL_OBJECT_OK
            || micros_thread_attach_execution_context(
                &objects,
                threads[index],
                bottom,
                bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
                &context
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    return true;
}

static bool test_priority_queues_and_tail_order(void)
{
    struct micros_thread_handle selected = {
        UINT16_MAX,
        UINT32_MAX,
    };

    EXPECT_TRUE(setup_fixture());
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_EMPTY,
        micros_hart_pick_ready(&objects, harts[0], &selected)
    );
    EXPECT_TRUE(
        selected.slot == UINT16_MAX
        && selected.generation == UINT32_MAX
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_thread_scheduler_admit(
            &objects,
            harts[0],
            threads[0],
            MICROS_SCHEDULER_PRIORITY_COUNT,
            100,
            true
        )
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_thread_scheduler_admit(
            &objects,
            harts[0],
            threads[0],
            7,
            0,
            true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[0], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[1], 3, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[2], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_pick_ready(&objects, harts[0], &selected)
    );
    EXPECT_TRUE(
        handles_equal(selected, threads[1])
        && handles_equal(objects.harts[harts[0].slot].ready_head[7], threads[0])
        && handles_equal(objects.harts[harts[0].slot].ready_tail[7], threads[2])
        && handles_equal(
            objects.threads[threads[0].slot].ready_next,
            threads[2]
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&objects, threads[1])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_pick_ready(&objects, harts[0], &selected)
    );
    EXPECT_TRUE(handles_equal(selected, threads[0]));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_unset(
            &objects,
            threads[1],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_pick_ready(&objects, harts[0], &selected)
    );
    EXPECT_TRUE(handles_equal(selected, threads[1]));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_core_validate(&objects)
    );
    return true;
}

static bool test_runtime_flag_zero_boundary(void)
{
    EXPECT_TRUE(setup_fixture());
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[0], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_set(
            &objects,
            threads[0],
            MICROS_THREAD_RTS_NO_QUANTUM
        )
    );
    EXPECT_TRUE(
        !objects.threads[threads[0].slot].ready_linked
        && objects.harts[harts[0].slot].ready_head[7].generation == 0
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_set(
            &objects,
            threads[0],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_unset(
            &objects,
            threads[0],
            MICROS_THREAD_RTS_NO_QUANTUM
        )
    );
    EXPECT_TRUE(
        objects.threads[threads[0].slot].runtime_flags
            == MICROS_THREAD_RTS_INACTIVE
        && !objects.threads[threads[0].slot].ready_linked
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_unset(
            &objects,
            threads[0],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_TRUE(
        objects.threads[threads[0].slot].runtime_flags == 0
        && objects.threads[threads[0].slot].ready_linked
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_thread_runtime_flags_set(
            &objects,
            threads[0],
            MICROS_THREAD_RTS_PREEMPTED
        )
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_thread_runtime_flags_unset(
            &objects,
            threads[0],
            MICROS_THREAD_RTS_PREEMPTED
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&objects, threads[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_remove(&objects, threads[0])
    );
    EXPECT_TRUE(
        !objects.threads[threads[0].slot].scheduler_assigned
        && objects.threads[threads[0].slot].runtime_flags
            == MICROS_THREAD_RTS_INACTIVE
    );
    return true;
}

static bool test_policy_replacement_is_atomic(void)
{
    struct micros_thread_handle selected;

    EXPECT_TRUE(setup_fixture());
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[0], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[1], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_install_policy(
            &objects,
            threads[1],
            2,
            55
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_pick_ready(&objects, harts[0], &selected)
    );
    EXPECT_TRUE(
        handles_equal(selected, threads[1])
        && objects.threads[threads[1].slot].remaining_counter_ticks == 55
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&objects, threads[1])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_install_policy(
            &objects,
            threads[1],
            9,
            77
        )
    );
    EXPECT_TRUE(
        !objects.threads[threads[1].slot].ready_linked
        && objects.threads[threads[1].slot].scheduler_priority == 9
        && objects.threads[threads[1].slot].remaining_counter_ticks == 77
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_unset(
            &objects,
            threads[1],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_TRUE(
        handles_equal(objects.harts[harts[0].slot].ready_head[9], threads[1])
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_thread_install_policy(
            &objects,
            threads[1],
            MICROS_SCHEDULER_PRIORITY_COUNT,
            77
        )
    );
    return true;
}

static bool test_legacy_and_scheduler_authority_do_not_overlap(void)
{
    struct micros_thread_handle stale;

    EXPECT_TRUE(setup_fixture());
    stale = threads[0];
    --stale.generation;
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_thread_scheduler_admit(
            &objects, harts[0], stale, 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_bind_thread(&objects, harts[0], threads[0])
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[1], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_clear_thread(&objects, harts[0], threads[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[1], 7, 100, true
        )
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_hart_bind_thread(&objects, harts[0], threads[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&objects, threads[1])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_remove(&objects, threads[1])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_bind_thread(&objects, harts[0], threads[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_clear_thread(&objects, harts[0], threads[0])
    );
    return true;
}

static bool test_corrupt_queue_state_is_rejected(void)
{
    struct micros_kernel_objects snapshot;
    struct micros_thread_handle output = {
        UINT16_MAX,
        UINT32_MAX,
    };

    EXPECT_TRUE(setup_fixture());
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[0], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[1], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[1], threads[2], 5, 100, true
        )
    );
    snapshot = objects;

#define EXPECT_CORRUPTION(statement) \
    do { \
        struct micros_thread_handle before = output; \
        objects = snapshot; \
        statement; \
        EXPECT_ERROR( \
            MICROS_KERNEL_OBJECT_ERROR_INVARIANT, \
            micros_scheduler_core_validate(&objects) \
        ); \
        EXPECT_ERROR( \
            MICROS_KERNEL_OBJECT_ERROR_INVARIANT, \
            micros_hart_pick_ready(&objects, harts[0], &output) \
        ); \
        EXPECT_TRUE(handles_equal(output, before)); \
    } while (false)

    EXPECT_CORRUPTION(
        objects.harts[harts[0].slot].ready_head[7].generation =
            UINT32_MAX
    );
    EXPECT_CORRUPTION(
        objects.harts[harts[0].slot].ready_tail[7].generation =
            UINT32_MAX
    );
    EXPECT_CORRUPTION(
        objects.threads[threads[0].slot].ready_next.generation =
            UINT32_MAX
    );
    EXPECT_CORRUPTION(
        objects.threads[threads[0].slot].scheduler_hart = harts[1]
    );
    EXPECT_CORRUPTION(
        objects.threads[threads[0].slot].runtime_flags =
            MICROS_THREAD_RTS_PREEMPTED
    );
    EXPECT_CORRUPTION(
        objects.harts[harts[0].slot].current_thread = threads[0]
    );
    EXPECT_CORRUPTION(
        objects.harts[harts[0].slot].reschedule_pending = true
    );
    EXPECT_CORRUPTION(
        objects.harts[harts[0].slot].accounting_owner =
            MICROS_SCHEDULER_ACCOUNTING_KERNEL
    );

#undef EXPECT_CORRUPTION
    objects = snapshot;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_core_validate(&objects)
    );
    return true;
}

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {
            "priority queues preserve tail order",
            test_priority_queues_and_tail_order,
        },
        {
            "runtime flags follow zero boundary",
            test_runtime_flag_zero_boundary,
        },
        {
            "policy replacement is atomic",
            test_policy_replacement_is_atomic,
        },
        {
            "legacy and scheduler authority do not overlap",
            test_legacy_and_scheduler_authority_do_not_overlap,
        },
        {
            "corrupt queue state is rejected",
            test_corrupt_queue_state_is_rejected,
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
