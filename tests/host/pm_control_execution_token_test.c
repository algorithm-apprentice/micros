#include "tests/host/pm_control_execution_test_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "arch/riscv64/trap_context.h"
#include "kernel/pm/pm_control_runtime_core.h"
#include "kernel/vm/vm_handoff_core.h"
#include "micros/endpoint.h"
#include "micros/frame_allocator.h"
#include "micros/frame_ownership.h"
#include "micros/ipc_core.h"
#include "micros/spawn.h"
#include "micros/sv39.h"

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

bool test_token_exhaustion_is_invariant(
    struct pm_control_execution_test_context *context
)
{
    struct pm_control_execution_test_context snapshot;
    struct micros_pm_control_prepare_plan prepare;
    struct micros_pm_control_prepare_plan prepare_sentinel;
    struct micros_pm_control_exit_begin_plan begin;
    struct micros_pm_control_exit_begin_plan begin_sentinel;
    struct micros_thread_handle thread;

    EXPECT_TRUE(
        pm_control_execution_test_initialize_fixture(context)
    );
    context->control.last_prepared_token = UINT64_MAX;
    memcpy(&snapshot, context, sizeof(snapshot));
    memset(&prepare_sentinel, 0xa5, sizeof(prepare_sentinel));
    prepare = prepare_sentinel;
    EXPECT_TRUE(
        micros_pm_control_prepare_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            context->reservation_token,
            context->load_token,
            context->mapping_generation,
            &prepare
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
        && memcmp(&prepare, &prepare_sentinel, sizeof(prepare)) == 0
        && memcmp(context, &snapshot, sizeof(snapshot)) == 0
    );

    EXPECT_TRUE(
        pm_control_execution_test_activate_child(context, &thread)
        && pm_control_execution_test_block_child_in_exit_call(
            context,
            thread
        )
    );
    context->control.last_exit_token = UINT64_MAX;
    memcpy(&snapshot, context, sizeof(snapshot));
    memset(&begin_sentinel, 0x5a, sizeof(begin_sentinel));
    begin = begin_sentinel;
    EXPECT_TRUE(
        micros_pm_control_exit_begin_preflight(
            &context->control,
            &context->objects,
            &context->endpoints,
            &context->vm,
            pm_control_execution_test_process_endpoint(
                context->child
            ),
            &begin
        ) == MICROS_PM_CONTROL_ERROR_INVARIANT
        && memcmp(&begin, &begin_sentinel, sizeof(begin)) == 0
        && memcmp(context, &snapshot, sizeof(snapshot)) == 0
    );
    return true;
}
