#include "micros/scheduler_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    GUARD_THREAD_COUNT = 3,
};

static struct micros_kernel_objects objects;
static struct micros_process_handle process;
static struct micros_thread_handle threads[GUARD_THREAD_COUNT];
static struct micros_hart_handle hart;
static struct micros_hart_handle other_hart;

bool micros_ipc_current_guard_test_run(void);

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

static bool setup_guard_fixture(void)
{
    size_t index;
    struct micros_scheduler_return_plan plan;

    memset(&objects, 0, sizeof(objects));
    if (
        micros_kernel_objects_initialize(
            &objects,
            GUARD_THREAD_COUNT,
            2
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&objects, 0, &hart)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_register(&objects, 1, &other_hart)
            != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            &objects,
            hart,
            UINT64_C(0x01000000),
            UINT64_C(0x01004000),
            UINT64_C(0x02000000),
            UINT64_C(0x02001000)
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_hart_install_trap_stacks(
            &objects,
            other_hart,
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
    for (index = 0; index < GUARD_THREAD_COUNT; ++index) {
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
            || micros_thread_scheduler_admit(
                &objects,
                hart,
                threads[index],
                MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
                100,
                true
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    return (
        micros_scheduler_accounting_initialize(&objects, hart, 100)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_plan_user_return(&objects, hart, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_hart_commit_user_return(&objects, &plan)
            == MICROS_KERNEL_OBJECT_OK
        && micros_scheduler_account_enter_thread(
            &objects,
            hart,
            threads[0],
            110
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_scheduler_account_user_trap(&objects, hart, 120)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool queue_is(
    const struct micros_thread_handle *expected,
    size_t count
)
{
    const struct micros_hart *resolved = &objects.harts[hart.slot];
    struct micros_thread_handle current =
        resolved->ready_head[MICROS_SCHEDULER_PRIORITY_DEFAULT_USER];
    size_t index;

    for (index = 0; index < count; ++index) {
        if (
            current.slot != expected[index].slot
            || current.generation != expected[index].generation
        ) {
            return false;
        }
        current = objects.threads[current.slot].ready_next;
    }
    return current.generation == 0;
}

static bool test_begin_and_rollback(void)
{
    struct micros_scheduler_current_ipc_guard guard = {0};

    EXPECT_TRUE(setup_guard_fixture());
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_begin_current_ipc(&objects, hart, &guard)
    );
    EXPECT_TRUE(
        guard.active
        && objects.harts[hart.slot].current_thread.generation == 0
        && objects.threads[threads[0].slot].runtime_flags
            == MICROS_THREAD_RTS_INACTIVE
        && !objects.threads[threads[0].slot].ready_linked
        && objects.harts[hart.slot].trap.primary_stack_bottom
            == objects.harts[hart.slot].idle_primary_stack_bottom
        && queue_is(&threads[1], 2)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_rollback_current_ipc(&objects, &guard)
    );
    EXPECT_TRUE(
        !guard.active
        && objects.harts[hart.slot].current_thread.slot
            == threads[0].slot
        && objects.harts[hart.slot].trap.primary_stack_bottom
            == objects.threads[threads[0].slot].kernel_stack_bottom
        && queue_is(threads, GUARD_THREAD_COUNT)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_scheduler_rollback_current_ipc(&objects, &guard)
    );
    return true;
}

static bool test_immediate_commit_restores_head(void)
{
    struct micros_scheduler_current_ipc_guard guard = {0};
    const struct micros_thread_handle temporary_order[] = {
        threads[1],
        threads[2],
        threads[0],
    };

    EXPECT_TRUE(setup_guard_fixture());
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_begin_current_ipc(&objects, hart, &guard)
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
        queue_is(temporary_order, GUARD_THREAD_COUNT)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_commit_current_ipc(&objects, &guard)
    );
    EXPECT_TRUE(
        !guard.active
        && objects.harts[hart.slot].current_thread.generation == 0
        && objects.harts[hart.slot].trap.primary_stack_bottom
            == objects.harts[hart.slot].idle_primary_stack_bottom
        && queue_is(threads, GUARD_THREAD_COUNT)
    );
    return true;
}

static bool test_blocked_commit_leaves_thread_detached(void)
{
    struct micros_scheduler_current_ipc_guard guard = {0};

    EXPECT_TRUE(setup_guard_fixture());
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_begin_current_ipc(&objects, hart, &guard)
    );
    objects.threads[threads[0].slot].runtime_flags =
        MICROS_THREAD_RTS_IPC_SEND;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_commit_current_ipc(&objects, &guard)
    );
    EXPECT_TRUE(
        !guard.active
        && !objects.threads[threads[0].slot].ready_linked
        && queue_is(&threads[1], 2)
        && micros_scheduler_core_validate(&objects)
            == MICROS_KERNEL_OBJECT_OK
    );
    return true;
}

static bool test_higher_priority_wakeup_still_preempts(void)
{
    struct micros_scheduler_current_ipc_guard guard = {0};
    struct micros_scheduler_return_plan plan;

    EXPECT_TRUE(setup_guard_fixture());
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&objects, threads[1])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_install_policy(
            &objects,
            threads[1],
            3,
            100
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_begin_current_ipc(&objects, hart, &guard)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_unset(
            &objects,
            threads[0],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
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
        micros_scheduler_commit_current_ipc(&objects, &guard)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, hart, &plan)
    );
    EXPECT_TRUE(
        plan.selected.slot == threads[1].slot
        && plan.selected.generation == threads[1].generation
    );
    return true;
}

static bool test_corrupt_guard_hart_is_atomic(void)
{
    struct micros_scheduler_current_ipc_guard guard = {0};
    struct micros_kernel_objects snapshot;
    struct micros_hart_handle original_hart;

    EXPECT_TRUE(setup_guard_fixture());
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_begin_current_ipc(&objects, hart, &guard)
    );
    original_hart = guard.hart;
    guard.hart = other_hart;
    snapshot = objects;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_scheduler_rollback_current_ipc(&objects, &guard)
    );
    EXPECT_TRUE(
        memcmp(&objects, &snapshot, sizeof(objects)) == 0
        && guard.active
    );
    guard.hart = original_hart;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_rollback_current_ipc(&objects, &guard)
    );
    return true;
}

static bool test_non_ipc_blocker_is_rejected_atomically(void)
{
    struct micros_scheduler_current_ipc_guard guard = {0};
    struct micros_kernel_objects snapshot;

    EXPECT_TRUE(setup_guard_fixture());
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_begin_current_ipc(&objects, hart, &guard)
    );
    objects.threads[threads[0].slot].runtime_flags =
        MICROS_THREAD_RTS_INACTIVE
        | MICROS_THREAD_RTS_NO_QUANTUM;
    snapshot = objects;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_scheduler_commit_current_ipc(&objects, &guard)
    );
    EXPECT_TRUE(
        memcmp(&objects, &snapshot, sizeof(objects)) == 0
        && guard.active
    );
    objects.threads[threads[0].slot].runtime_flags =
        MICROS_THREAD_RTS_INACTIVE;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_rollback_current_ipc(&objects, &guard)
    );
    return true;
}

bool micros_ipc_current_guard_test_run(void)
{
    return (
        test_begin_and_rollback()
        && test_immediate_commit_restores_head()
        && test_blocked_commit_leaves_thread_detached()
        && test_higher_priority_wakeup_still_preempts()
        && test_corrupt_guard_hart_is_atomic()
        && test_non_ipc_blocker_is_rejected_atomically()
    );
}
