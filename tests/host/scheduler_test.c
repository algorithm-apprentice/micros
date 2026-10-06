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

static bool test_current_selection_and_preemption_repair(void)
{
    struct micros_scheduler_return_plan plan;
    struct micros_thread_handle current;

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
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_TRUE(
        plan.action == MICROS_SCHEDULER_RETURN_SELECT_THREAD
        && handles_equal(plan.selected, threads[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_current_thread(&objects, harts[0], &current)
    );
    EXPECT_TRUE(
        handles_equal(current, threads[0])
        && handles_equal(
            objects.harts[harts[0].slot].ready_head[7],
            threads[0]
        )
    );

    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[2], 3, 100, true
        )
    );
    EXPECT_TRUE(
        (
            objects.threads[threads[0].slot].runtime_flags
            & MICROS_THREAD_RTS_PREEMPTED
        ) != 0
        && !objects.threads[threads[0].slot].ready_linked
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_TRUE(
        plan.repair_preempted
        && handles_equal(plan.selected, threads[2])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_TRUE(
        handles_equal(
            objects.harts[harts[0].slot].current_thread,
            threads[2]
        )
        && handles_equal(
            objects.harts[harts[0].slot].ready_head[7],
            threads[0]
        )
        && objects.threads[threads[0].slot].remaining_counter_ticks
            == 100
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&objects, threads[2])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_TRUE(handles_equal(plan.selected, threads[0]));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_core_validate(&objects)
    );
    return true;
}

static bool test_quantum_rotation_and_nonpreemptible_current(void)
{
    struct micros_scheduler_return_plan plan;

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
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    objects.threads[threads[0].slot].remaining_counter_ticks = 0;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_TRUE(
        plan.renew_quantum
        && handles_equal(plan.selected, threads[1])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_TRUE(
        handles_equal(
            objects.harts[harts[0].slot].current_thread,
            threads[1]
        )
        && handles_equal(
            objects.harts[harts[0].slot].ready_tail[7],
            threads[0]
        )
        && objects.threads[threads[0].slot].remaining_counter_ticks
            == 100
    );

    EXPECT_TRUE(setup_fixture());
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[0], 7, 100, false
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[1], 1, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_TRUE(
        plan.action == MICROS_SCHEDULER_RETURN_KEEP_CURRENT
        && handles_equal(plan.selected, threads[0])
    );
    objects.threads[threads[0].slot].remaining_counter_ticks = 0;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_TRUE(
        plan.action == MICROS_SCHEDULER_RETURN_KEEP_CURRENT
        && plan.renew_quantum
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_TRUE(
        objects.threads[threads[0].slot].remaining_counter_ticks == 100
    );
    return true;
}

static bool test_current_block_and_stale_plan_are_atomic(void)
{
    struct micros_scheduler_return_plan plan;

    EXPECT_TRUE(setup_fixture());
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[0], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_set(
            &objects,
            threads[0],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_thread_runtime_flags_unset(
            &objects,
            threads[0],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_TRUE(plan.action == MICROS_SCHEDULER_RETURN_ENTER_IDLE);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_TRUE(
        objects.harts[harts[0].slot].current_thread.generation == 0
        && objects.harts[harts[0].slot].trap.primary_stack_bottom
            == objects.harts[harts[0].slot].idle_primary_stack_bottom
    );

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
    objects.harts[harts[0].slot].reschedule_pending = true;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&objects, threads[1])
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_TRUE(objects.harts[harts[0].slot].reschedule_pending);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_TRUE(!objects.harts[harts[0].slot].reschedule_pending);
    return true;
}

static bool test_accounting_owners_are_separate(void)
{
    struct micros_scheduler_return_plan plan;
    struct micros_scheduler_return_plan rejected_plan;

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
            &objects, harts[0], threads[1], 3, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&objects, threads[1])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_accounting_initialize(
            &objects, harts[0], 100
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_enter_thread(
            &objects, harts[0], threads[0], 120
        )
    );
    rejected_plan = plan;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_hart_plan_user_return(
            &objects,
            harts[0],
            &rejected_plan
        )
    );
    EXPECT_TRUE(memcmp(&rejected_plan, &plan, sizeof(plan)) == 0);
    EXPECT_TRUE(objects.harts[harts[0].slot].kernel_counter_ticks == 20);
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_thread_scheduler_hold(&objects, threads[0])
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_thread_runtime_flags_set(
            &objects,
            threads[0],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_thread_runtime_flags_unset(
            &objects,
            threads[1],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_user_trap(
            &objects, harts[0], 170
        )
    );
    EXPECT_TRUE(
        objects.threads[threads[0].slot].remaining_counter_ticks == 50
        && objects.harts[harts[0].slot].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_KERNEL
        && objects.harts[harts[0].slot].kernel_counter_ticks == 20
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_enter_thread(
            &objects, harts[0], threads[0], 200
        )
    );
    EXPECT_TRUE(objects.harts[harts[0].slot].kernel_counter_ticks == 50);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_user_trap(
            &objects, harts[0], 300
        )
    );
    EXPECT_TRUE(
        objects.threads[threads[0].slot].remaining_counter_ticks == 0
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&objects, threads[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_enter_idle(
            &objects, harts[0], 330
        )
    );
    EXPECT_TRUE(objects.harts[harts[0].slot].kernel_counter_ticks == 80);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_idle_trap(
            &objects, harts[0], 380
        )
    );
    EXPECT_TRUE(
        objects.harts[harts[0].slot].idle_counter_ticks == 50
        && objects.harts[harts[0].slot].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_KERNEL
    );

    EXPECT_TRUE(setup_fixture());
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[0], 7, 30, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_accounting_initialize(
            &objects,
            harts[0],
            UINT64_MAX - UINT64_C(29)
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_enter_thread(
            &objects,
            harts[0],
            threads[0],
            UINT64_MAX - UINT64_C(9)
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_user_trap(
            &objects, harts[0], 10
        )
    );
    EXPECT_TRUE(
        objects.threads[threads[0].slot].remaining_counter_ticks == 10
        && objects.harts[harts[0].slot].kernel_counter_ticks == 20
    );
    return true;
}

static bool test_trap_accounting_uses_minimal_route(void)
{
    struct micros_scheduler_return_plan plan;
    uint32_t unrelated_flags;

    EXPECT_TRUE(setup_fixture());
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &objects, harts[0], threads[0], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_accounting_initialize(
            &objects, harts[0], 100
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_enter_thread(
            &objects, harts[0], threads[0], 120
        )
    );

    unrelated_flags =
        objects.threads[threads[1].slot].runtime_flags;
    objects.threads[threads[1].slot].runtime_flags |=
        UINT32_C(0x80000000);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_INVARIANT,
        micros_scheduler_core_validate(&objects)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_user_trap(
            &objects, harts[0], 170
        )
    );
    EXPECT_TRUE(
        objects.threads[threads[0].slot].remaining_counter_ticks == 50
        && objects.harts[harts[0].slot].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_KERNEL
    );
    objects.threads[threads[1].slot].runtime_flags =
        unrelated_flags;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_core_validate(&objects)
    );

    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&objects, threads[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&objects, harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&objects, &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_enter_idle(
            &objects, harts[0], 200
        )
    );
    objects.threads[threads[1].slot].runtime_flags |=
        UINT32_C(0x80000000);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_INVARIANT,
        micros_scheduler_core_validate(&objects)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_idle_trap(
            &objects, harts[0], 230
        )
    );
    EXPECT_TRUE(
        objects.harts[harts[0].slot].idle_counter_ticks == 30
        && objects.harts[harts[0].slot].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_KERNEL
    );
    objects.threads[threads[1].slot].runtime_flags =
        unrelated_flags;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_core_validate(&objects)
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
        objects.harts[harts[0].slot].accounting_owner =
            MICROS_SCHEDULER_ACCOUNTING_THREAD
    );

#undef EXPECT_CORRUPTION
    objects = snapshot;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_core_validate(&objects)
    );
    return true;
}

bool micros_scheduler_model_test_run(void);

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
            "current selection repairs preemption",
            test_current_selection_and_preemption_repair,
        },
        {
            "quantum rotation retains nonpreemptible current",
            test_quantum_rotation_and_nonpreemptible_current,
        },
        {
            "current block and stale plan are atomic",
            test_current_block_and_stale_plan_are_atomic,
        },
        {
            "accounting owners are separate",
            test_accounting_owners_are_separate,
        },
        {
            "trap accounting uses minimal route",
            test_trap_accounting_uses_minimal_route,
        },
        {
            "corrupt queue state is rejected",
            test_corrupt_queue_state_is_rejected,
        },
        {
            "seeded two-hart model",
            micros_scheduler_model_test_run,
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
