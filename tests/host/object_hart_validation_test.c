#include "tests/host/object_hart_validation_test.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(MICROS_OBJECT_HART_VALIDATION_LAUNCHER)
#include <unistd.h>
#endif

#include "kernel/scheduler/scheduler_core_private.h"

enum object_operation {
    OP_HART_CONTEXT,
    OP_IS_CURRENT,
    OP_CAPTURE,
    OP_VALIDATE,
    OP_ADMIT_PREFLIGHT,
    OP_ADMIT,
    OP_HOLD,
    OP_REMOVE,
    OP_FLAGS_SET,
    OP_FLAGS_UNSET,
    OP_POLICY,
    OP_PICK,
    OP_ACCOUNT_INITIALIZE,
    OP_ACCOUNT_THREAD,
    OP_ACCOUNT_IDLE,
    OP_IPC_PREFLIGHT,
    OP_IPC_BEGIN,
    OP_IPC_ROLLBACK,
    OP_IPC_COMMIT,
    OP_IPC_BATCH,
    OP_IPC_WAKE,
    OP_RETURN_PLAN,
};

struct object_fixture {
    struct micros_kernel_objects objects;
    struct micros_kernel_objects before;
    struct micros_kernel_objects effect;
    struct micros_process_handle process;
    struct micros_thread_handle threads[MICROS_THREAD_CAPACITY];
    struct micros_hart_handle harts[MICROS_HART_CAPACITY];
    struct micros_user_context context;
    struct micros_scheduler_return_plan plan;
    struct micros_scheduler_return_plan plan_before;
    struct micros_scheduler_current_ipc_guard guard;
    struct micros_scheduler_current_ipc_guard guard_before;
    struct micros_scheduler_ipc_wake_plan wake;
    struct micros_scheduler_ipc_wake_plan wake_before;
    struct micros_scheduler_ipc_transition requests[MICROS_THREAD_CAPACITY];
    struct micros_thread_handle selected;
    struct micros_thread_handle selected_before;
    const struct micros_hart *resolved;
    size_t thread_count;
    size_t hart_count;
    size_t request_count;
    size_t selected_thread;
    size_t selected_hart;
    size_t exceptions;
    uint8_t priority;
    uintptr_t context_address;
    uint64_t value;
};

static struct object_fixture fixture;
static uint64_t capture_label;

#if defined(MICROS_TEST_WORK_COUNTS)
static struct micros_test_work_snapshot snapshot;
#endif

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "OBJ01_FIXTURE_FAILURE line=%d expression=%s\n", __LINE__, #condition); \
            return false; \
        } \
    } while (false)

static uint64_t bytes_digest(const void *source, size_t size)
{
    const unsigned char *bytes = source;
    uint64_t value = UINT64_C(14695981039346656037);
    size_t index;

    for (index = 0; index < size; ++index) {
        value ^= bytes[index];
        value *= UINT64_C(1099511628211);
    }
    return value;
}

static bool setup_objects(size_t threads, size_t harts, bool accounting)
{
    size_t index;

    REQUIRE(threads <= MICROS_THREAD_CAPACITY);
    REQUIRE(harts <= MICROS_HART_CAPACITY);
    memset(&fixture, 0, sizeof(fixture));
    fixture.thread_count = threads;
    fixture.hart_count = harts;
    fixture.priority = 3;
    fixture.context.sp = UINT64_C(0x70000000);
    fixture.context.sepc = UINT64_C(0x40000000);
    fixture.context.a0 = UINT64_C(0x12345678);
    REQUIRE(
        micros_kernel_objects_initialize(
            &fixture.objects,
            MICROS_THREAD_CAPACITY,
            MICROS_HART_CAPACITY
        ) == MICROS_KERNEL_OBJECT_OK
    );
    if (threads != 0) {
        REQUIRE(
            micros_process_create(&fixture.objects, &fixture.process)
                == MICROS_KERNEL_OBJECT_OK
        );
    }
    for (index = 0; index < harts; ++index) {
        uintptr_t idle = UINT64_C(0x20000000)
            + index * MICROS_PRIMARY_TRAP_STACK_MIN_SIZE;
        uintptr_t emergency = UINT64_C(0x30000000)
            + index * MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE;

        REQUIRE(
            micros_hart_register(
                &fixture.objects, index, &fixture.harts[index]
            ) == MICROS_KERNEL_OBJECT_OK
        );
        REQUIRE(
            micros_hart_install_trap_stacks(
                &fixture.objects, fixture.harts[index],
                idle, idle + MICROS_PRIMARY_TRAP_STACK_MIN_SIZE,
                emergency, emergency + MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE
            ) == MICROS_KERNEL_OBJECT_OK
        );
        if (accounting) {
            REQUIRE(
                micros_scheduler_accounting_initialize(
                    &fixture.objects, fixture.harts[index], 1
                ) == MICROS_KERNEL_OBJECT_OK
            );
        }
    }
    for (index = 0; index < threads; ++index) {
        uintptr_t bottom = UINT64_C(0x10000000)
            + index * MICROS_THREAD_KERNEL_STACK_SIZE;

        REQUIRE(
            micros_thread_create(
                &fixture.objects, fixture.process, &fixture.threads[index]
            ) == MICROS_KERNEL_OBJECT_OK
        );
        REQUIRE(
            micros_thread_attach_execution_context(
                &fixture.objects, fixture.threads[index],
                bottom, bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
                &fixture.context
            ) == MICROS_KERNEL_OBJECT_OK
        );
    }
    REQUIRE(micros_kernel_objects_validate(&fixture.objects) == MICROS_KERNEL_OBJECT_OK);
    return true;
}

static bool admit_thread(size_t thread, size_t hart, uint8_t priority)
{
    REQUIRE(thread < fixture.thread_count && hart < fixture.hart_count);
    REQUIRE(
        micros_thread_scheduler_admit(
            &fixture.objects, fixture.harts[hart], fixture.threads[thread],
            priority, 100, true
        ) == MICROS_KERNEL_OBJECT_OK
    );
    return true;
}

static bool select_current(size_t hart)
{
    struct micros_scheduler_return_plan plan;

    REQUIRE(
        micros_hart_plan_user_return(
            &fixture.objects, fixture.harts[hart], &plan
        ) == MICROS_KERNEL_OBJECT_OK
    );
    REQUIRE(
        micros_hart_commit_user_return(&fixture.objects, &plan)
            == MICROS_KERNEL_OBJECT_OK
    );
    return true;
}

static const char *operation_name(enum object_operation operation)
{
    static const char *const names[] = {
        "hart-context", "is-current", "capture", "validate",
        "admit-preflight", "admit", "hold", "remove", "flags-set",
        "flags-unset", "policy", "pick", "account-initialize",
        "account-thread", "account-idle", "ipc-preflight", "ipc-begin",
        "ipc-rollback", "ipc-commit", "ipc-batch", "ipc-wake", "return-plan",
    };

    return names[operation];
}

static unsigned int invoke_operation(enum object_operation operation)
{
    struct micros_thread_handle thread =
        fixture.threads[fixture.selected_thread];
    struct micros_hart_handle hart =
        fixture.harts[fixture.selected_hart];

    switch (operation) {
    case OP_HART_CONTEXT:
        return micros_hart_resolve_context(
            &fixture.objects, fixture.context_address, &fixture.resolved
        );
    case OP_IS_CURRENT:
        fixture.value = scheduler_core_thread_is_current(&fixture.objects, thread);
        return MICROS_KERNEL_OBJECT_OK;
    case OP_CAPTURE:
        return micros_thread_capture_execution_context(
            &fixture.objects, thread, &fixture.context
        );
    case OP_VALIDATE:
        return micros_kernel_objects_validate(&fixture.objects);
    case OP_ADMIT_PREFLIGHT:
        return micros_thread_scheduler_admit_preflight(
            &fixture.objects, hart, thread, fixture.priority, 73
        );
    case OP_ADMIT:
        return micros_thread_scheduler_admit(
            &fixture.objects, hart, thread, fixture.priority, 73, true
        );
    case OP_HOLD:
        return micros_thread_scheduler_hold(&fixture.objects, thread);
    case OP_REMOVE:
        return micros_thread_scheduler_remove(&fixture.objects, thread);
    case OP_FLAGS_SET:
        return micros_thread_runtime_flags_set(
            &fixture.objects, thread, MICROS_THREAD_RTS_NO_QUANTUM
        );
    case OP_FLAGS_UNSET:
        return micros_thread_runtime_flags_unset(
            &fixture.objects, thread, MICROS_THREAD_RTS_INACTIVE
        );
    case OP_POLICY:
        return micros_thread_install_policy(&fixture.objects, thread, fixture.priority, 73);
    case OP_PICK:
        return micros_hart_pick_ready(&fixture.objects, hart, &fixture.selected);
    case OP_ACCOUNT_INITIALIZE:
        return micros_scheduler_accounting_initialize(&fixture.objects, hart, 17);
    case OP_ACCOUNT_THREAD:
        return micros_scheduler_account_enter_thread(&fixture.objects, hart, thread, 17);
    case OP_ACCOUNT_IDLE:
        return micros_scheduler_account_enter_idle(&fixture.objects, hart, 17);
    case OP_IPC_PREFLIGHT:
        return micros_scheduler_preflight_current_ipc(&fixture.objects, hart);
    case OP_IPC_BEGIN:
        return micros_scheduler_begin_current_ipc(&fixture.objects, hart, &fixture.guard);
    case OP_IPC_ROLLBACK:
        return micros_scheduler_rollback_current_ipc(&fixture.objects, &fixture.guard);
    case OP_IPC_COMMIT:
        return micros_scheduler_commit_current_ipc(&fixture.objects, &fixture.guard);
    case OP_IPC_BATCH:
        return micros_scheduler_commit_ipc_transitions(
            &fixture.objects, fixture.requests, fixture.request_count
        );
    case OP_IPC_WAKE:
        return micros_scheduler_prepare_ipc_wake(
            &fixture.objects, thread, MICROS_THREAD_RTS_INACTIVE, &fixture.wake
        );
    case OP_RETURN_PLAN:
        return micros_hart_plan_user_return(&fixture.objects, hart, &fixture.plan);
    }
    return UINT32_MAX;
}

static uint64_t output_digest(void)
{
    return bytes_digest(&fixture.plan, sizeof(fixture.plan))
        ^ bytes_digest(&fixture.guard, sizeof(fixture.guard))
        ^ bytes_digest(&fixture.wake, sizeof(fixture.wake))
        ^ bytes_digest(&fixture.selected, sizeof(fixture.selected))
        ^ fixture.value;
}

static size_t attached_threads(void)
{
    size_t index;
    size_t count = 0;

    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        if (
            fixture.objects.threads[index].slot_state == MICROS_KERNEL_OBJECT_SLOT_LIVE
            && fixture.objects.threads[index].context_attached
        ) {
            ++count;
        }
    }
    return count;
}

static void print_work(void)
{
#if defined(MICROS_TEST_WORK_COUNTS)
    size_t owner;
    size_t kind;

    printf(
        "MICROS_OBJ01_WORK {\"version\":%" PRIu32 ",\"reserved0\":%" PRIu32
        ",\"producer\":%" PRIu64 ",\"case\":%" PRIu64
        ",\"owner_mask\":%" PRIu32 ",\"reserved1\":%" PRIu32 ",\"counts\":[",
        snapshot.version, snapshot.reserved0, snapshot.producer_label,
        snapshot.capture_label, snapshot.owner_mask, snapshot.reserved1
    );
    for (owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        printf("%s[", owner == 0 ? "" : ",");
        for (kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            printf("%s%" PRIu64, kind == 0 ? "" : ",", snapshot.counts[owner][kind]);
        }
        printf("]");
    }
    printf("]}\n");
#endif
}

static bool check_declared_success(enum object_operation operation, const char *variant)
{
    if (operation == OP_CAPTURE) {
        fixture.before.threads[fixture.selected_thread].user_context = fixture.context;
        REQUIRE(memcmp(&fixture.objects, &fixture.before, sizeof(fixture.objects)) == 0);
    } else if (operation == OP_ADMIT && strcmp(variant, "unselected-process") == 0) {
        struct micros_thread *thread = &fixture.before.threads[fixture.selected_thread];
        struct micros_hart *hart = &fixture.before.harts[fixture.selected_hart];

        thread->scheduler_assigned = true;
        thread->scheduler_preemptible = true;
        thread->scheduler_priority = fixture.priority;
        thread->scheduler_hart = fixture.harts[fixture.selected_hart];
        thread->quantum_counter_ticks = 73;
        thread->remaining_counter_ticks = 73;
        thread->runtime_flags = 0;
        thread->ready_linked = true;
        thread->ready_next = (struct micros_thread_handle){0, 0};
        hart->ready_head[fixture.priority] = fixture.threads[fixture.selected_thread];
        hart->ready_tail[fixture.priority] = fixture.threads[fixture.selected_thread];
        REQUIRE(memcmp(&fixture.objects, &fixture.before, sizeof(fixture.objects)) == 0);
    } else if (operation == OP_RETURN_PLAN && strcmp(variant, "unselected-hart-queue") == 0) {
#if !defined(MICROS_OBJ02_LEGACY_PLAN)
        memset(&fixture.plan_before, 0, sizeof(fixture.plan_before));
        fixture.plan_before.hart = fixture.harts[0];
        fixture.plan_before.selected = fixture.threads[0];
        fixture.plan_before.action = MICROS_SCHEDULER_RETURN_SELECT_THREAD;
        fixture.plan_before.ready_bitmap[0] = 1;
        fixture.plan_before.ready_head[7] = fixture.threads[0];
        fixture.plan_before.ready_tail[7] = fixture.threads[0];
        REQUIRE(memcmp(&fixture.plan, &fixture.plan_before, sizeof(fixture.plan)) == 0);
#else
        REQUIRE(fixture.plan.action == MICROS_SCHEDULER_RETURN_SELECT_THREAD);
        REQUIRE(fixture.plan.selected.slot == fixture.threads[0].slot);
        REQUIRE(fixture.plan.selected.generation == fixture.threads[0].generation);
#endif
    }
    return true;
}

static uint64_t mix_effect_value(uint64_t digest, uint64_t value)
{
    for (size_t index = 0; index < sizeof(value); ++index) {
        digest ^= value & UINT64_C(255);
        digest *= UINT64_C(1099511628211);
        value >>= 8;
    }
    return digest;
}

static uint64_t mix_effect_handle(uint64_t digest, struct micros_thread_handle handle)
{
    return mix_effect_value(mix_effect_value(digest, handle.slot), handle.generation);
}

static bool observe_return_effect(const char *variant, uint64_t *result)
{
    uint64_t digest = UINT64_C(14695981039346656037);
    bool empty = strcmp(variant, "nonlive-assigned-record") == 0
        || strcmp(variant, "reschedule-without-assignment") == 0;
    bool singleton = strcmp(variant, "unreachable-assigned-ready") == 0
        || strcmp(variant, "unselected-hart-queue") == 0;

    fixture.effect = fixture.objects;
    micros_scheduler_apply_return_plan(&fixture.effect, &fixture.plan);
    if (empty || singleton) {
        struct micros_hart *expected;

        fixture.before = fixture.objects;
        expected = &fixture.before.harts[0];
        expected->current_thread = empty ? (struct micros_thread_handle){0, 0} : fixture.threads[0];
        expected->reschedule_pending = false;
        expected->trap.primary_stack_bottom = empty
            ? expected->idle_primary_stack_bottom : fixture.before.threads[0].kernel_stack_bottom;
        expected->trap.primary_stack_top = empty
            ? expected->idle_primary_stack_top : fixture.before.threads[0].kernel_stack_top;
        REQUIRE(memcmp(&fixture.effect, &fixture.before, sizeof(fixture.effect)) == 0);
        fixture.before = fixture.objects;
    }
    for (size_t index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        struct micros_thread *after = &fixture.effect.threads[index];
        const struct micros_thread *before = &fixture.objects.threads[index];

        digest = mix_effect_value(digest, after->runtime_flags);
        digest = mix_effect_value(digest, after->quantum_counter_ticks);
        digest = mix_effect_value(digest, after->remaining_counter_ticks);
        digest = mix_effect_value(digest, after->ready_linked);
        digest = mix_effect_handle(digest, after->ready_next);
        after->runtime_flags = before->runtime_flags;
        after->remaining_counter_ticks = before->remaining_counter_ticks;
        after->ready_linked = before->ready_linked;
        after->ready_next = before->ready_next;
    }
    for (size_t index = 0; index < MICROS_HART_CAPACITY; ++index) {
        struct micros_hart *after = &fixture.effect.harts[index];
        const struct micros_hart *before = &fixture.objects.harts[index];

        for (size_t priority = 0; priority < MICROS_SCHEDULER_PRIORITY_COUNT; ++priority) {
            digest = mix_effect_handle(digest, after->ready_head[priority]);
            digest = mix_effect_handle(digest, after->ready_tail[priority]);
            after->ready_head[priority] = before->ready_head[priority];
            after->ready_tail[priority] = before->ready_tail[priority];
        }
        digest = mix_effect_handle(digest, after->current_thread);
        digest = mix_effect_value(digest, after->reschedule_pending);
        digest = mix_effect_value(digest, after->trap.primary_stack_bottom);
        digest = mix_effect_value(digest, after->trap.primary_stack_top);
        after->current_thread = before->current_thread;
        after->reschedule_pending = before->reschedule_pending;
        after->trap.primary_stack_bottom = before->trap.primary_stack_bottom;
        after->trap.primary_stack_top = before->trap.primary_stack_top;
    }
    REQUIRE(memcmp(&fixture.effect, &fixture.objects, sizeof(fixture.effect)) == 0);
    *result = digest;
    return true;
}

static bool measure_operation(enum object_operation operation, const char *variant)
{
    unsigned int result;
    enum micros_kernel_object_error diagnostic;
    bool state_preserved;
    bool outputs_preserved;
    uint64_t before_output;
    uint64_t before_state;
    uint64_t before_plan;
    uint64_t effect = 0;
    bool has_effect;

    memset(&fixture.plan, 0xa5, sizeof(fixture.plan));
    memset(&fixture.wake, 0x5a, sizeof(fixture.wake));
    fixture.selected = (struct micros_thread_handle){UINT16_MAX, UINT32_MAX};
    fixture.before = fixture.objects;
    fixture.plan_before = fixture.plan;
    fixture.guard_before = fixture.guard;
    fixture.wake_before = fixture.wake;
    fixture.selected_before = fixture.selected;
    fixture.resolved = (const struct micros_hart *)UINTPTR_MAX;
    fixture.value = UINT64_MAX;
    before_output = output_digest();
    before_state = bytes_digest(&fixture.before, sizeof(fixture.before));
    before_plan = bytes_digest(&fixture.plan, sizeof(fixture.plan));
    ++capture_label;
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(
        micros_test_work_begin(
            UINT64_C(0x4f424a3031000000) + UINT64_C(256) * MICROS_OBJ01_NATIVE_STAGE
                + (uint64_t)MICROS_OBJ01_NATIVE_CHANNEL + 1,
            capture_label,
            (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_OBJECTS)
                | (MICROS_OBJ01_NATIVE_CHANNEL == 0
                    ? UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_SCHEDULER : 0)
        ) == MICROS_TEST_WORK_OK
    );
#endif
    result = invoke_operation(operation);
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
#endif
    state_preserved = memcmp(&fixture.objects, &fixture.before, sizeof(fixture.objects)) == 0;
    outputs_preserved = (
        memcmp(&fixture.plan, &fixture.plan_before, sizeof(fixture.plan)) == 0
        && memcmp(&fixture.guard, &fixture.guard_before, sizeof(fixture.guard)) == 0
        && memcmp(&fixture.wake, &fixture.wake_before, sizeof(fixture.wake)) == 0
        && memcmp(&fixture.selected, &fixture.selected_before, sizeof(fixture.selected)) == 0
        && fixture.resolved == (const struct micros_hart *)UINTPTR_MAX
    );
    if (operation == OP_HART_CONTEXT && result == MICROS_KERNEL_OBJECT_OK) {
        uintptr_t base = (uintptr_t)fixture.objects.harts;
        uintptr_t value = (uintptr_t)fixture.resolved;

        REQUIRE(value >= base && value - base < sizeof(fixture.objects.harts));
        REQUIRE((value - base) % sizeof(fixture.objects.harts[0]) == 0);
        fixture.value = (value - base) / sizeof(fixture.objects.harts[0]);
    }
    diagnostic = micros_kernel_objects_validate(&fixture.objects);
    if (result != MICROS_KERNEL_OBJECT_OK) {
        REQUIRE(state_preserved && outputs_preserved);
    } else {
        REQUIRE(check_declared_success(operation, variant));
    }
    has_effect = operation == OP_RETURN_PLAN && result == MICROS_KERNEL_OBJECT_OK;
    if (has_effect) {
        REQUIRE(observe_return_effect(variant, &effect));
    }
    printf(
        "MICROS_OBJ01_CASE {\"version\":1,\"case\":%" PRIu64
        ",\"operation\":\"%s\",\"variant\":\"%s\",\"threads\":%zu,"
        "\"harts\":%zu,\"attached\":%zu,\"requests\":%zu,\"exceptions\":%zu,"
        "\"priority\":%u,"
        "\"selected_thread\":%zu,\"selected_hart\":%zu,\"result\":%u,"
        "\"diagnostic\":%u,\"preserved\":%u,\"outputs_preserved\":%u,"
        "\"before_state\":%" PRIu64 ",\"after_state\":%" PRIu64
        ",\"before_output\":%" PRIu64 ",\"after_output\":%" PRIu64
        ",\"value\":%" PRIu64 "}\n",
        capture_label, operation_name(operation), variant,
        fixture.thread_count, fixture.hart_count, attached_threads(), fixture.request_count,
        fixture.exceptions, (unsigned int)fixture.priority, fixture.selected_thread, fixture.selected_hart,
        result, (unsigned int)diagnostic, (unsigned int)state_preserved,
        (unsigned int)outputs_preserved, before_state,
        bytes_digest(&fixture.objects, sizeof(fixture.objects)),
        before_output, output_digest(), fixture.value
    );
    printf(
        "MICROS_OBJ01_PLAN {\"version\":1,\"case\":%" PRIu64
        ",\"bytes\":%zu,\"before\":%" PRIu64 ",\"after\":%" PRIu64
        ",\"has_effect\":%u,\"effect\":%" PRIu64 "}\n",
        capture_label, sizeof(fixture.plan), before_plan,
        bytes_digest(&fixture.plan, sizeof(fixture.plan)), (unsigned int)has_effect, effect
    );
    print_work();
    return true;
}

static bool geometry_cases(void)
{
    const size_t thread_counts[] = {0, 1, 2, 16, 64, 128};
    const size_t hart_counts[] = {0, 1, 2, 8};
    const size_t exception_counts[] = {1, 2, 8};
    size_t thread_index;
    size_t hart_index;
    size_t index;

    for (thread_index = 0; thread_index < sizeof(thread_counts) / sizeof(thread_counts[0]); ++thread_index) {
        for (hart_index = 0; hart_index < sizeof(hart_counts) / sizeof(hart_counts[0]); ++hart_index) {
            REQUIRE(setup_objects(thread_counts[thread_index], hart_counts[hart_index], false));
            REQUIRE(measure_operation(OP_VALIDATE, "regular"));
        }
    }
    for (index = 0; index < sizeof(exception_counts) / sizeof(exception_counts[0]); ++index) {
        size_t selected;

        REQUIRE(setup_objects(MICROS_THREAD_CAPACITY, MICROS_HART_CAPACITY, false));
        fixture.exceptions = exception_counts[index];
        for (selected = 0; selected < fixture.exceptions; ++selected) {
            struct micros_thread *thread = &fixture.objects.threads[selected];

            thread->kernel_stack_bottom = UINT64_C(0x40000000) + selected * UINT64_C(0x10000);
            thread->kernel_stack_top = thread->kernel_stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE + 16;
        }
        REQUIRE(micros_kernel_objects_validate(&fixture.objects) == MICROS_KERNEL_OBJECT_OK);
        REQUIRE(measure_operation(OP_VALIDATE, "oversize-exceptions"));
    }
    REQUIRE(setup_objects(MICROS_THREAD_CAPACITY, 1, false));
    fixture.exceptions = 1;
    fixture.objects.threads[MICROS_THREAD_CAPACITY - 1].kernel_stack_bottom = UINT64_C(0x1000);
    fixture.objects.threads[MICROS_THREAD_CAPACITY - 1].kernel_stack_top =
        UINT64_C(0x1000) + MICROS_THREAD_KERNEL_STACK_SIZE;
    REQUIRE(measure_operation(OP_VALIDATE, "offset-underflow-exception"));
    return true;
}

static bool identity_cases(void)
{
    const size_t hart_counts[] = {1, 2, 8};
    size_t index;

    for (index = 0; index < sizeof(hart_counts) / sizeof(hart_counts[0]); ++index) {
        REQUIRE(setup_objects(MICROS_THREAD_CAPACITY, hart_counts[index], true));
        fixture.selected_hart = hart_counts[index] - 1;
        fixture.selected_thread = MICROS_THREAD_CAPACITY - 1;
        fixture.context_address = (uintptr_t)&fixture.objects.harts[fixture.selected_hart];
        REQUIRE(measure_operation(OP_HART_CONTEXT, "last-hart"));
        ++fixture.context_address;
        REQUIRE(measure_operation(OP_HART_CONTEXT, "misaligned-hart"));
        fixture.context_address = UINTPTR_MAX;
        REQUIRE(measure_operation(OP_HART_CONTEXT, "outside-hart"));
        REQUIRE(admit_thread(fixture.selected_thread, fixture.selected_hart, 7));
        REQUIRE(select_current(fixture.selected_hart));
        REQUIRE(measure_operation(OP_IS_CURRENT, "assigned-current"));
        fixture.context.a0 ^= UINT64_C(0x10101010);
        REQUIRE(measure_operation(OP_CAPTURE, "assigned-current"));
    }
    REQUIRE(setup_objects(1, 1, false));
    fixture.context_address = (uintptr_t)&fixture.objects.harts[0];
    fixture.objects.harts[0].generation = 0;
    REQUIRE(measure_operation(OP_HART_CONTEXT, "live-zero-generation"));
    REQUIRE(setup_objects(1, 1, false));
    fixture.objects.harts[0].current_thread = fixture.threads[0];
    fixture.objects.harts[0].trap.primary_stack_bottom = fixture.objects.threads[0].kernel_stack_bottom;
    fixture.objects.harts[0].trap.primary_stack_top = fixture.objects.threads[0].kernel_stack_top;
    REQUIRE(micros_kernel_objects_validate_base(&fixture.objects) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(measure_operation(OP_VALIDATE, "unscheduled-current"));
    fixture.context.a0 ^= UINT64_C(0x10101010);
    REQUIRE(measure_operation(OP_CAPTURE, "unscheduled-current"));
    return true;
}

static bool scheduler_case(enum object_operation operation, size_t threads)
{
    size_t index;
    bool current;

    REQUIRE(setup_objects(threads, 2, operation != OP_ACCOUNT_INITIALIZE));
    fixture.selected_thread = threads - 1;
    current = (
        operation == OP_ACCOUNT_THREAD || operation == OP_IPC_PREFLIGHT
        || operation == OP_IPC_BEGIN || operation == OP_IPC_ROLLBACK
        || operation == OP_IPC_COMMIT || operation == OP_RETURN_PLAN
    );
    if (current) {
        REQUIRE(admit_thread(fixture.selected_thread, 0, 7));
        REQUIRE(select_current(0));
    } else if (
        operation != OP_ADMIT && operation != OP_ADMIT_PREFLIGHT
        && operation != OP_ACCOUNT_INITIALIZE && operation != OP_ACCOUNT_IDLE
    ) {
        for (index = 0; index < threads; ++index) {
            REQUIRE(admit_thread(index, 0, 7));
        }
    }
    if (
        operation == OP_REMOVE || operation == OP_FLAGS_UNSET
        || operation == OP_IPC_WAKE || operation == OP_IPC_BATCH
    ) {
        size_t first = operation == OP_IPC_BATCH ? 0 : fixture.selected_thread;

        for (index = first; index < threads; ++index) {
            REQUIRE(
                micros_thread_scheduler_hold(&fixture.objects, fixture.threads[index])
                    == MICROS_KERNEL_OBJECT_OK
            );
        }
    }
    if (operation == OP_IPC_ROLLBACK || operation == OP_IPC_COMMIT) {
        REQUIRE(
            micros_scheduler_begin_current_ipc(&fixture.objects, fixture.harts[0], &fixture.guard)
                == MICROS_KERNEL_OBJECT_OK
        );
        if (operation == OP_IPC_COMMIT) {
            REQUIRE(
                micros_scheduler_commit_ipc_wake(
                    &fixture.objects, fixture.threads[fixture.selected_thread],
                    MICROS_THREAD_RTS_INACTIVE
                ) == MICROS_KERNEL_OBJECT_OK
            );
        }
    }
    if (operation == OP_IPC_BATCH) {
        fixture.request_count = threads;
        for (index = 0; index < threads; ++index) {
            fixture.requests[index].handle = fixture.threads[index];
            fixture.requests[index].clear_flags = MICROS_THREAD_RTS_INACTIVE;
        }
    }
    REQUIRE(measure_operation(operation, "valid"));
    return true;
}

static bool geometry_boundary_cases(void)
{
    struct micros_thread *thread;
    struct micros_hart *hart;
    size_t index;

    REQUIRE(setup_objects(3, 1, false));
    REQUIRE(
        micros_thread_detach_execution_context(&fixture.objects, fixture.threads[1])
            == MICROS_KERNEL_OBJECT_OK
    );
    hart = &fixture.objects.harts[0];
    hart->idle_primary_stack_bottom = UINT64_C(0x10000000) + MICROS_THREAD_KERNEL_STACK_SIZE;
    hart->idle_primary_stack_top = hart->idle_primary_stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE;
    hart->trap.primary_stack_bottom = hart->idle_primary_stack_bottom;
    hart->trap.primary_stack_top = hart->idle_primary_stack_top;
    REQUIRE(measure_operation(OP_VALIDATE, "regular-gap-owner"));
    REQUIRE(setup_objects(3, 1, false));
    REQUIRE(
        micros_thread_detach_execution_context(&fixture.objects, fixture.threads[0])
            == MICROS_KERNEL_OBJECT_OK
    );
    for (index = 1; index < 3; ++index) {
        thread = &fixture.objects.threads[index];
        thread->kernel_stack_bottom = index * MICROS_THREAD_KERNEL_STACK_SIZE;
        thread->kernel_stack_top = thread->kernel_stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE;
    }
    REQUIRE(measure_operation(OP_VALIDATE, "zero-cohort-base"));
    REQUIRE(setup_objects(4, 1, false));
    fixture.exceptions = 3;
    for (index = 0; index < 4; ++index) {
        thread = &fixture.objects.threads[index];
        thread->kernel_stack_bottom = UINT64_C(0x10000000) + (3 - index) * MICROS_THREAD_KERNEL_STACK_SIZE;
        thread->kernel_stack_top = thread->kernel_stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE;
    }
    REQUIRE(measure_operation(OP_VALIDATE, "permuted-portable-ranges"));
    REQUIRE(setup_objects(2, 2, true));
    REQUIRE(admit_thread(0, 0, 7) && admit_thread(1, 1, 7));
    REQUIRE(select_current(0) && select_current(1));
    REQUIRE(measure_operation(OP_VALIDATE, "current-primary-aliases"));
    fixture.objects.harts[0].trap.primary_stack_bottom = fixture.objects.threads[1].kernel_stack_bottom;
    fixture.objects.harts[0].trap.primary_stack_top = fixture.objects.threads[1].kernel_stack_top;
    REQUIRE(measure_operation(OP_VALIDATE, "wrong-primary-alias"));
    REQUIRE(setup_objects(5, 2, false));
    for (index = 1; index < 4; ++index) {
        REQUIRE(
            micros_thread_detach_execution_context(&fixture.objects, fixture.threads[index])
                == MICROS_KERNEL_OBJECT_OK
        );
    }
    for (index = 0; index < 2; ++index) {
        hart = &fixture.objects.harts[index];
        hart->idle_primary_stack_bottom = UINT64_C(0x10000000) + (index + 1) * MICROS_THREAD_KERNEL_STACK_SIZE;
        hart->idle_primary_stack_top = hart->idle_primary_stack_bottom + MICROS_PRIMARY_TRAP_STACK_MIN_SIZE;
        hart->trap.primary_stack_bottom = hart->idle_primary_stack_bottom;
        hart->trap.primary_stack_top = hart->idle_primary_stack_top;
        hart->trap.emergency_stack_bottom = UINT64_C(0x10000000)
            + 3 * MICROS_THREAD_KERNEL_STACK_SIZE + index * MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE;
        hart->trap.emergency_stack_top = hart->trap.emergency_stack_bottom + MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE;
    }
    REQUIRE(measure_operation(OP_VALIDATE, "interleaved-touching-cohorts"));
    REQUIRE(setup_objects(1, MICROS_HART_CAPACITY, false));
    fixture.objects.threads[0].kernel_stack_bottom = fixture.objects.harts[1].idle_primary_stack_bottom;
    fixture.objects.threads[0].kernel_stack_top = fixture.objects.harts[1].idle_primary_stack_top;
    memset(&fixture.objects.harts[1], 0, sizeof(fixture.objects.harts[1]));
    --fixture.objects.registered_hart_count;
    --fixture.hart_count;
    REQUIRE(measure_operation(OP_VALIDATE, "hart-cohort-gap"));
    return true;
}

static bool geometry_corruption_cases(void)
{
    size_t variant;
    const char *const names[] = {
        "thread-thread-overlap", "thread-idle-overlap", "thread-emergency-overlap",
        "idle-idle-overlap", "emergency-idle-overlap", "late-exception-overlap",
        "zero-stack", "misaligned-stack", "undersized-stack", "wrapped-stack",
        "emergency-emergency-overlap", "exception-pair-overlap",
    };

    for (variant = 0; variant < sizeof(names) / sizeof(names[0]); ++variant) {
        struct micros_thread *thread;
        struct micros_hart *hart;

        REQUIRE(setup_objects(8, 2, false));
        thread = &fixture.objects.threads[7];
        hart = &fixture.objects.harts[1];
        switch (variant) {
        case 0:
        case 5:
            thread->kernel_stack_bottom = fixture.objects.threads[0].kernel_stack_bottom;
            thread->kernel_stack_top = fixture.objects.threads[0].kernel_stack_top + (variant == 5 ? 16 : 0);
            fixture.exceptions = 1;
            break;
        case 1:
            thread->kernel_stack_bottom = hart->idle_primary_stack_bottom;
            thread->kernel_stack_top = hart->idle_primary_stack_top;
            break;
        case 2:
            thread->kernel_stack_bottom = hart->trap.emergency_stack_bottom;
            thread->kernel_stack_top = thread->kernel_stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE;
            break;
        case 3:
            hart->idle_primary_stack_bottom = fixture.objects.harts[0].idle_primary_stack_bottom;
            hart->idle_primary_stack_top = fixture.objects.harts[0].idle_primary_stack_top;
            hart->trap.primary_stack_bottom = hart->idle_primary_stack_bottom;
            hart->trap.primary_stack_top = hart->idle_primary_stack_top;
            break;
        case 4:
            hart->trap.emergency_stack_bottom = fixture.objects.harts[0].idle_primary_stack_bottom;
            hart->trap.emergency_stack_top = hart->trap.emergency_stack_bottom + MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE;
            break;
        case 6:
            thread->kernel_stack_bottom = 0;
            break;
        case 7:
            ++thread->kernel_stack_bottom;
            break;
        case 8:
            thread->kernel_stack_top = thread->kernel_stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE - 16;
            break;
        case 9:
            thread->kernel_stack_bottom = UINTPTR_MAX - 15;
            thread->kernel_stack_top = 16;
            break;
        case 10:
            hart->trap.emergency_stack_bottom = fixture.objects.harts[0].trap.emergency_stack_bottom;
            hart->trap.emergency_stack_top = fixture.objects.harts[0].trap.emergency_stack_top;
            break;
        case 11:
            fixture.exceptions = 2;
            fixture.objects.threads[6].kernel_stack_bottom = UINT64_C(0x50000000);
            fixture.objects.threads[6].kernel_stack_top = UINT64_C(0x50000000)
                + MICROS_THREAD_KERNEL_STACK_SIZE + 16;
            thread->kernel_stack_bottom = UINT64_C(0x50000000) + MICROS_THREAD_KERNEL_STACK_SIZE;
            thread->kernel_stack_top = thread->kernel_stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE + 16;
            break;
        }
        REQUIRE(measure_operation(OP_VALIDATE, names[variant]));
    }
    return true;
}

static bool identity_boundary_cases(void)
{
    struct micros_hart *other;

    REQUIRE(setup_objects(2, 2, true));
    REQUIRE(admit_thread(0, 0, 7) && select_current(0));
    other = &fixture.objects.harts[1];
    other->current_thread = fixture.threads[0];
    other->trap.primary_stack_bottom = fixture.objects.threads[0].kernel_stack_bottom;
    other->trap.primary_stack_top = fixture.objects.threads[0].kernel_stack_top;
    fixture.context.a0 ^= UINT64_C(0x20202020);
    REQUIRE(measure_operation(OP_CAPTURE, "foreign-current-duplicate"));
    REQUIRE(setup_objects(2, 1, true));
    REQUIRE(admit_thread(0, 0, 7) && select_current(0));
    ++fixture.threads[0].generation;
    REQUIRE(measure_operation(OP_CAPTURE, "stale-caller"));
    REQUIRE(measure_operation(OP_IS_CURRENT, "stale-caller"));
    REQUIRE(setup_objects(2, 1, true));
    REQUIRE(admit_thread(0, 0, 7) && select_current(0));
    ++fixture.objects.threads[0].owner.generation;
    fixture.context.a0 ^= UINT64_C(0x20202020);
    REQUIRE(measure_operation(OP_CAPTURE, "stale-selected-owner"));
    REQUIRE(setup_objects(2, 1, true));
    REQUIRE(admit_thread(0, 0, 7) && select_current(0));
    ++fixture.objects.threads[0].scheduler_hart.generation;
    fixture.context.a0 ^= UINT64_C(0x20202020);
    REQUIRE(measure_operation(OP_CAPTURE, "stale-selected-assignment"));
    REQUIRE(setup_objects(2, 1, true));
    REQUIRE(admit_thread(0, 0, 7) && select_current(0));
    fixture.selected_thread = 1;
    REQUIRE(measure_operation(OP_CAPTURE, "noncurrent-thread"));
    REQUIRE(setup_objects(1, 1, false));
    fixture.context_address = (uintptr_t)&fixture.objects.harts[0];
    REQUIRE(measure_operation(OP_HART_CONTEXT, "first-hart"));
    --fixture.context_address;
    REQUIRE(measure_operation(OP_HART_CONTEXT, "below-hart-array"));
    fixture.context_address = (uintptr_t)&fixture.objects.harts[MICROS_HART_CAPACITY];
    REQUIRE(measure_operation(OP_HART_CONTEXT, "after-hart-array"));
    fixture.context_address = (uintptr_t)&fixture.objects.harts[1];
    REQUIRE(measure_operation(OP_HART_CONTEXT, "nonlive-hart"));
    fixture.context_address = 0;
    REQUIRE(measure_operation(OP_HART_CONTEXT, "null-hart"));
    return true;
}

static bool setup_ready_queue(size_t threads, bool current)
{
    size_t index;

    REQUIRE(setup_objects(threads, 1, true));
    for (index = 0; index < threads; ++index) {
        REQUIRE(admit_thread(index, 0, 7));
    }
    if (current) {
        REQUIRE(select_current(0));
    }
    return true;
}

static bool queue_transition_cases(void)
{
    size_t index;
    const char *const positions[] = {"head-removal", "middle-removal", "tail-removal"};

    for (index = 0; index < 3; ++index) {
        REQUIRE(setup_ready_queue(3, false));
        fixture.selected_thread = index;
        REQUIRE(measure_operation(OP_HOLD, positions[index]));
        REQUIRE(!fixture.objects.threads[index].ready_linked);
        REQUIRE(fixture.objects.threads[index].runtime_flags == MICROS_THREAD_RTS_INACTIVE);
    }
    REQUIRE(setup_ready_queue(1, false));
    REQUIRE(measure_operation(OP_HOLD, "singleton-removal"));
    REQUIRE(fixture.objects.harts[0].ready_head[7].generation == 0);
    REQUIRE(setup_ready_queue(3, false));
    fixture.priority = 7;
    REQUIRE(measure_operation(OP_POLICY, "same-priority-head"));
    REQUIRE(fixture.objects.harts[0].ready_head[7].slot == 1);
    REQUIRE(fixture.objects.harts[0].ready_tail[7].slot == 0);
    REQUIRE(setup_ready_queue(3, true));
    fixture.priority = 7;
    fixture.selected_thread = 2;
    REQUIRE(measure_operation(OP_POLICY, "same-priority-tail"));
    REQUIRE(fixture.objects.harts[0].ready_tail[7].slot == 2);
    for (index = 1; index < 3; ++index) {
        REQUIRE(setup_ready_queue(3, true));
        fixture.selected_thread = index;
        REQUIRE(measure_operation(OP_POLICY, index == 1 ? "preempt-after-middle" : "preempt-after-tail"));
        REQUIRE(fixture.objects.threads[0].runtime_flags == MICROS_THREAD_RTS_PREEMPTED);
        REQUIRE(fixture.objects.threads[0].remaining_counter_ticks == 100);
        REQUIRE(fixture.objects.harts[0].ready_head[7].slot == (index == 1 ? 2 : 1));
        REQUIRE(fixture.objects.harts[0].ready_head[3].slot == index);
    }
    return true;
}

static bool queue_corruption_cases(void)
{
    size_t variant;
    const char *const names[] = {
        "stale-current-head", "stale-current-tail", "current-self-successor",
        "empty-head-live-tail", "current-primary-mismatch",
    };

    for (variant = 0; variant < sizeof(names) / sizeof(names[0]); ++variant) {
        struct micros_hart *hart;

        REQUIRE(setup_objects(3, 1, true));
        REQUIRE(admit_thread(0, 0, 7) && admit_thread(1, 0, 7) && select_current(0));
        fixture.selected_thread = 2;
        hart = &fixture.objects.harts[0];
        switch (variant) {
        case 0: ++hart->ready_head[7].generation; break;
        case 1: ++hart->ready_tail[7].generation; break;
        case 2: fixture.objects.threads[0].ready_next = fixture.threads[0]; break;
        case 3: hart->ready_tail[3] = fixture.threads[1]; break;
        case 4: hart->trap.primary_stack_bottom += 16; break;
        }
        REQUIRE(measure_operation(OP_ADMIT, names[variant]));
    }
    REQUIRE(setup_ready_queue(4, false));
    fixture.selected_thread = 2;
    fixture.objects.threads[2].ready_next = fixture.threads[0];
    REQUIRE(measure_operation(OP_HOLD, "consumed-prefix-backedge"));
    REQUIRE(setup_ready_queue(3, true));
    REQUIRE(
        micros_scheduler_begin_current_ipc(&fixture.objects, fixture.harts[0], &fixture.guard)
            == MICROS_KERNEL_OBJECT_OK
    );
    fixture.objects.threads[1].ready_next = fixture.threads[1];
    REQUIRE(measure_operation(OP_IPC_ROLLBACK, "rollback-self-successor"));
    REQUIRE(setup_ready_queue(3, true));
    REQUIRE(
        micros_scheduler_begin_current_ipc(&fixture.objects, fixture.harts[0], &fixture.guard)
            == MICROS_KERNEL_OBJECT_OK
    );
    REQUIRE(
        micros_scheduler_commit_ipc_wake(&fixture.objects, fixture.threads[0], MICROS_THREAD_RTS_INACTIVE)
            == MICROS_KERNEL_OBJECT_OK
    );
    ++fixture.objects.harts[0].ready_tail[7].generation;
    REQUIRE(measure_operation(OP_IPC_COMMIT, "commit-stale-tail"));
    REQUIRE(setup_objects(3, 1, true));
    fixture.selected_thread = 2;
    fixture.objects.processes[MICROS_PROCESS_CAPACITY - 1].generation = UINT32_MAX;
    REQUIRE(measure_operation(OP_ADMIT, "unselected-process"));
    return true;
}

static bool batch_boundary_cases(void)
{
    size_t variant;
    const char *const names[] = {
        "duplicate-handle", "different-generation", "generation-repeat",
        "outside-slot", "outside-before-duplicate", "zero-generation",
        "duplicate-zero-generation",
    };

    for (variant = 0; variant < sizeof(names) / sizeof(names[0]); ++variant) {
        size_t index;

        REQUIRE(setup_objects(3, 1, true));
        for (index = 0; index < 3; ++index) {
            REQUIRE(admit_thread(index, 0, 7));
            REQUIRE(
                micros_thread_scheduler_hold(&fixture.objects, fixture.threads[index])
                    == MICROS_KERNEL_OBJECT_OK
            );
            fixture.requests[index].handle = fixture.threads[index];
            fixture.requests[index].clear_flags = MICROS_THREAD_RTS_INACTIVE;
        }
        fixture.request_count = 2;
        switch (variant) {
        case 0: fixture.requests[1].handle = fixture.threads[0]; break;
        case 1:
        case 2:
            fixture.requests[1].handle = fixture.threads[0];
            ++fixture.requests[1].handle.generation;
            if (variant == 2) {
                fixture.request_count = 3;
                fixture.requests[2].handle = fixture.threads[0];
            }
            break;
        case 3: fixture.requests[0].handle.slot = UINT16_MAX; break;
        case 4:
            fixture.request_count = 3;
            fixture.requests[0].handle.slot = UINT16_MAX;
            fixture.requests[2].handle = fixture.requests[1].handle;
            break;
        case 5: fixture.requests[0].handle.generation = 0; break;
        case 6:
            fixture.requests[0].handle.generation = 0;
            fixture.requests[1].handle = fixture.requests[0].handle;
            break;
        }
        REQUIRE(measure_operation(OP_IPC_BATCH, names[variant]));
    }
    REQUIRE(scheduler_case(OP_IPC_BATCH, 1));
    REQUIRE(scheduler_case(OP_IPC_BATCH, 4));
    REQUIRE(scheduler_case(OP_IPC_BATCH, MICROS_THREAD_CAPACITY));
    return true;
}

static bool batch_preemption_case(size_t threads)
{
    size_t index;

    REQUIRE(setup_objects(threads, 2, true));
    REQUIRE(admit_thread(0, 0, 7) && select_current(0));
    for (index = 1; index < threads; ++index) {
        REQUIRE(admit_thread(index, index % 2, 7));
        REQUIRE(
            micros_thread_scheduler_hold(&fixture.objects, fixture.threads[index])
                == MICROS_KERNEL_OBJECT_OK
        );
        REQUIRE(
            micros_thread_install_policy(&fixture.objects, fixture.threads[index], 3, 100)
                == MICROS_KERNEL_OBJECT_OK
        );
        fixture.requests[threads - index - 1].handle = fixture.threads[index];
        fixture.requests[threads - index - 1].clear_flags = MICROS_THREAD_RTS_INACTIVE;
    }
    fixture.request_count = threads - 1;
    REQUIRE(measure_operation(OP_IPC_BATCH, "shared-queues-priority-preemption"));
    REQUIRE(fixture.objects.threads[0].runtime_flags == MICROS_THREAD_RTS_PREEMPTED);
    REQUIRE(fixture.objects.threads[0].remaining_counter_ticks == 100);
    REQUIRE(fixture.objects.harts[0].ready_head[3].slot == threads - 2);
    REQUIRE(fixture.objects.harts[1].ready_head[3].slot == threads - 1);
    REQUIRE(fixture.objects.harts[0].ready_tail[3].slot == 2);
    REQUIRE(fixture.objects.harts[1].ready_tail[3].slot == 1);
    return true;
}

static bool selected_metadata_cases(void)
{
    size_t variant;
    const char *const names[] = {
        "invalid-thread-policy", "invalid-hart-policy", "invalid-live-count",
        "stale-owner", "empty-owner-count", "unknown-flags", "invalid-priority",
        "zero-quantum", "excess-remaining", "stale-assigned-hart", "ready-state-mismatch",
        "invalid-selected-stack", "invalid-account-owner", "stray-accounted-thread",
    };

    for (variant = 0; variant < sizeof(names) / sizeof(names[0]); ++variant) {
        struct micros_thread *thread;

        REQUIRE(setup_ready_queue(2, false));
        fixture.selected_thread = 1;
        thread = &fixture.objects.threads[1];
        switch (variant) {
        case 0: fixture.objects.max_threads_per_process = 0; break;
        case 1: fixture.objects.max_harts = MICROS_HART_CAPACITY + 1; break;
        case 2: fixture.objects.live_process_count = MICROS_PROCESS_CAPACITY + 1; break;
        case 3: ++thread->owner.generation; break;
        case 4: fixture.objects.processes[0].live_thread_count = 0; break;
        case 5: thread->runtime_flags |= UINT32_C(0x80000000); break;
        case 6: thread->scheduler_priority = MICROS_SCHEDULER_PRIORITY_COUNT; break;
        case 7: thread->quantum_counter_ticks = 0; break;
        case 8: thread->remaining_counter_ticks = thread->quantum_counter_ticks + 1; break;
        case 9: ++thread->scheduler_hart.generation; break;
        case 10: thread->ready_linked = false; break;
        case 11: ++thread->kernel_stack_bottom; break;
        case 12: fixture.objects.harts[0].accounting_owner = MICROS_SCHEDULER_ACCOUNTING_IDLE + 1; break;
        case 13: fixture.objects.harts[0].accounted_thread = fixture.threads[0]; break;
        }
        REQUIRE(measure_operation(OP_HOLD, names[variant]));
    }
    REQUIRE(setup_ready_queue(2, false));
    ++fixture.threads[0].generation;
    REQUIRE(measure_operation(OP_HOLD, "stale-caller-thread"));
    REQUIRE(setup_ready_queue(2, false));
    ++fixture.harts[0].generation;
    REQUIRE(measure_operation(OP_PICK, "stale-caller-hart"));
    REQUIRE(setup_objects(2, 1, true));
    fixture.selected_thread = 1;
    REQUIRE(
        micros_thread_detach_execution_context(&fixture.objects, fixture.threads[1])
            == MICROS_KERNEL_OBJECT_OK
    );
    REQUIRE(measure_operation(OP_ADMIT, "detached-candidate"));
    return true;
}

static bool null_and_accounting_cases(void)
{
    REQUIRE(setup_ready_queue(2, false));
    REQUIRE(
        micros_thread_scheduler_hold(&fixture.objects, fixture.threads[1])
            == MICROS_KERNEL_OBJECT_OK
    );
    fixture.selected_thread = 1;
    fixture.objects.threads[1].ready_next.slot = UINT16_MAX;
    REQUIRE(measure_operation(OP_FLAGS_UNSET, "generation-only-null-next"));
    REQUIRE(setup_objects(2, 1, true));
    fixture.selected_thread = 1;
    fixture.objects.harts[0].ready_head[3].slot = UINT16_MAX;
    REQUIRE(measure_operation(OP_ADMIT, "generation-only-null-head"));
    REQUIRE(setup_objects(2, 1, true));
    fixture.selected_thread = 1;
    fixture.objects.harts[0].ready_tail[3].slot = UINT16_MAX;
    REQUIRE(measure_operation(OP_ADMIT, "noncanonical-empty-tail"));
    REQUIRE(setup_objects(0, 1, true));
    REQUIRE(measure_operation(OP_PICK, "empty-queues"));
    REQUIRE(setup_ready_queue(2, true));
    REQUIRE(
        micros_scheduler_account_enter_thread(&fixture.objects, fixture.harts[0], fixture.threads[0], 5)
            == MICROS_KERNEL_OBJECT_OK
    );
    fixture.selected_thread = 1;
    REQUIRE(measure_operation(OP_POLICY, "preemption-under-thread-accounting"));
    REQUIRE(setup_ready_queue(2, true));
    fixture.objects.harts[0].kernel_counter_ticks = UINT64_MAX;
    REQUIRE(measure_operation(OP_ACCOUNT_THREAD, "kernel-accounting-overflow"));
    REQUIRE(setup_objects(2, 1, true));
    fixture.objects.harts[0].kernel_counter_ticks = UINT64_MAX;
    REQUIRE(measure_operation(OP_ACCOUNT_IDLE, "kernel-accounting-overflow"));
    return true;
}

static bool return_scope_cases(void)
{
    REQUIRE(setup_ready_queue(3, false));
    fixture.objects.harts[0].ready_head[3] = fixture.threads[0];
    fixture.objects.harts[0].ready_tail[3] = fixture.threads[2];
    REQUIRE(measure_operation(OP_RETURN_PLAN, "duplicate-queue-membership"));
    REQUIRE(setup_ready_queue(3, false));
    fixture.objects.harts[0].ready_tail[7] = fixture.threads[1];
    REQUIRE(measure_operation(OP_RETURN_PLAN, "wrong-final-tail"));
    REQUIRE(setup_objects(1, 1, true));
    fixture.objects.threads[MICROS_THREAD_CAPACITY - 1].scheduler_assigned = true;
    fixture.objects.threads[MICROS_THREAD_CAPACITY - 1].scheduler_hart = fixture.harts[0];
    REQUIRE(measure_operation(OP_RETURN_PLAN, "nonlive-assigned-record"));
    REQUIRE(setup_objects(0, 1, true));
    fixture.objects.harts[0].reschedule_pending = true;
    REQUIRE(measure_operation(OP_RETURN_PLAN, "reschedule-without-assignment"));
    REQUIRE(setup_ready_queue(2, false));
    fixture.objects.harts[0].ready_tail[7] = fixture.threads[0];
    fixture.objects.threads[0].ready_next = (struct micros_thread_handle){0, 0};
    REQUIRE(measure_operation(OP_RETURN_PLAN, "unreachable-assigned-ready"));
    REQUIRE(setup_objects(2, 2, true));
    REQUIRE(admit_thread(0, 0, 7) && admit_thread(1, 1, 7));
    ++fixture.objects.harts[1].ready_tail[7].generation;
    REQUIRE(measure_operation(OP_RETURN_PLAN, "unselected-hart-queue"));
    return true;
}

#if defined(MICROS_OBJECT_HART_VALIDATION_LAUNCHER)
static int launch_native_products(int argc, char **argv)
{
    char *arguments[19];
    size_t count = 0;
    const char *launcher = getenv("__PYVENV_LAUNCHER__");

    if (launcher != NULL && launcher[0] != '\0') {
        fputs("__PYVENV_LAUNCHER__ is unsupported\n", stderr);
        return 2;
    }
    arguments[count++] = MICROS_OBJECT_HART_VALIDATION_PYTHON;
    arguments[count++] = "-E";
    arguments[count++] = "-S";
    arguments[count++] = "-B";
    arguments[count++] = MICROS_OBJECT_HART_VALIDATION_DRIVER;
    arguments[count++] = "--native";
    for (int index = 1; index < argc; ++index) {
        arguments[count++] = "--executable";
        arguments[count++] = argv[index];
    }
    arguments[count++] = "--executable";
    arguments[count++] = argv[0];
    arguments[count] = NULL;
    if (chdir(MICROS_OBJECT_HART_VALIDATION_ROOT) == 0) {
        execv(MICROS_OBJECT_HART_VALIDATION_PYTHON, arguments);
    }
    perror("OBJ01 native orchestration");
    return 2;
}
#endif

int main(int argc, char **argv)
{
    enum object_operation operation;

#if defined(MICROS_OBJECT_HART_VALIDATION_LAUNCHER)
    if (argc == 6) {
        return launch_native_products(argc, argv);
    }
#endif
    if (argc != 3 || strcmp(argv[1], MICROS_OBJ01_NATIVE_STAGE == 0 ? "baseline" : "current") != 0) {
        return 2;
    }
#if defined(MICROS_TEST_WORK_COUNTS)
    if (strcmp(argv[2], MICROS_OBJ01_NATIVE_CHANNEL == 0 ? "aggregate" : "stack") != 0) {
        return 2;
    }
#else
    if (strcmp(argv[2], "ordinary") != 0) {
        return 2;
    }
#endif
    if (
        !geometry_cases() || !identity_cases() || !geometry_boundary_cases()
        || !geometry_corruption_cases() || !identity_boundary_cases()
    ) {
        return 1;
    }
    if (!queue_transition_cases() || !queue_corruption_cases() || !batch_boundary_cases()) {
        return 1;
    }
    if (
        !batch_preemption_case(4) || !batch_preemption_case(MICROS_THREAD_CAPACITY)
        || !selected_metadata_cases() || !null_and_accounting_cases()
        || !return_scope_cases()
    ) {
        return 1;
    }
    for (operation = OP_ADMIT_PREFLIGHT; operation <= OP_RETURN_PLAN; ++operation) {
        if (!scheduler_case(operation, 2) || !scheduler_case(operation, 64)) {
            return 1;
        }
    }
    printf("MICROS_OBJ01_NATIVE_PASS cases=%" PRIu64 "\n", capture_label);
    return 0;
}
