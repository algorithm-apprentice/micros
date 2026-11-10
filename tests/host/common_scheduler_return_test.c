#include "tests/host/common_scheduler_return_test.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(MICROS_COMMON_SCHEDULER_RETURN_LAUNCHER)
#include <unistd.h>
#endif

#include "kernel/scheduler/scheduler_core_internal.h"

enum fixture_mode {
    READY,
    CURRENT,
    PREEMPTED,
    PREEMPTED_BLOCKED,
};

enum fixture_fault {
    NO_FAULT,
    HEAD_CYCLE,
    PREFIX_CYCLE,
    PREFIX_CURRENT_BACKEDGE,
    STALE_SUCCESSOR,
    OUTSIDE_SUCCESSOR,
    WRONG_TAIL,
    DUPLICATE_PRIORITY,
    STALE_HART,
    THREAD_ACCOUNTING,
    IDLE_ACCOUNTING,
    NONCANONICAL_TAIL,
    OTHER_HART_CORRUPTION,
    UNUSED_ASSIGNMENT,
    EMPTY_RESCHEDULE,
    UNREACHABLE_READY,
    NULL_ENCODINGS,
};

struct fixture_spec {
    const char *name;
    size_t count;
    size_t exhausted;
    uint8_t priority;
    enum fixture_mode mode;
    size_t nonpreemptible;
    bool two_priorities;
    enum fixture_fault fault;
};

struct return_fixture {
    struct micros_kernel_objects objects;
    struct micros_kernel_objects before;
    struct micros_kernel_objects expected;
    struct micros_process_handle process;
    struct micros_hart_handle harts[2];
    struct micros_thread_handle handles[MICROS_THREAD_CAPACITY];
    struct micros_scheduler_return_plan plan;
    struct micros_scheduler_return_plan saved_plan;
    size_t renewed;
    uint64_t capture;
};

static struct return_fixture fixture;
static const char *case_name;

#if defined(MICROS_TEST_WORK_COUNTS)
static struct micros_test_work_snapshot snapshot;
#endif

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "OBJ02_FIXTURE_FAILURE case=%s line=%d expression=%s\n", \
                    case_name, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

static bool same_thread(struct micros_thread_handle left, struct micros_thread_handle right)
{
    return left.slot == right.slot && left.generation == right.generation;
}

static uint64_t bytes_digest(const void *source, size_t count)
{
    const unsigned char *bytes = source;
    uint64_t result = UINT64_C(14695981039346656037);

    for (size_t index = 0; index < count; ++index) {
        result ^= bytes[index];
        result *= UINT64_C(1099511628211);
    }
    return result;
}

static uint64_t mix_value(uint64_t digest, uint64_t value)
{
    for (size_t index = 0; index < sizeof(value); ++index) {
        digest ^= value & UINT64_C(255);
        digest *= UINT64_C(1099511628211);
        value >>= 8;
    }
    return digest;
}

static uint64_t mix_thread(uint64_t digest, struct micros_thread_handle handle)
{
    digest = mix_value(digest, handle.generation == 0 ? 0 : handle.slot);
    return mix_value(digest, handle.generation);
}

static uint64_t logical_digest(const struct micros_kernel_objects *objects)
{
    uint64_t digest = UINT64_C(14695981039346656037);

    for (size_t index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        const struct micros_thread *thread = &objects->threads[index];

        digest = mix_value(digest, thread->runtime_flags);
        digest = mix_value(digest, thread->quantum_counter_ticks);
        digest = mix_value(digest, thread->remaining_counter_ticks);
        digest = mix_value(digest, thread->ready_linked);
        digest = mix_thread(digest, thread->ready_next);
    }
    for (size_t index = 0; index < MICROS_HART_CAPACITY; ++index) {
        const struct micros_hart *hart = &objects->harts[index];

        for (size_t priority = 0; priority < MICROS_SCHEDULER_PRIORITY_COUNT; ++priority) {
            digest = mix_thread(digest, hart->ready_head[priority]);
            digest = mix_thread(digest, hart->ready_tail[priority]);
        }
        digest = mix_thread(digest, hart->current_thread);
        digest = mix_value(digest, hart->reschedule_pending);
        digest = mix_value(digest, hart->trap.primary_stack_bottom);
        digest = mix_value(digest, hart->trap.primary_stack_top);
    }
    return digest;
}

static bool setup(const struct fixture_spec *spec)
{
    struct micros_user_context context = {
        .sp = UINT64_C(0x70000000), .sepc = UINT64_C(0x40000000),
    };
    struct micros_hart *hart;

    case_name = spec->name;
    memset(&fixture.objects, 0, sizeof(fixture.objects));
    REQUIRE(spec->count <= MICROS_THREAD_CAPACITY);
    REQUIRE(spec->exhausted <= spec->count);
    REQUIRE(micros_kernel_objects_initialize(
        &fixture.objects, MICROS_THREAD_CAPACITY, 2
    ) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_process_create(&fixture.objects, &fixture.process) == MICROS_KERNEL_OBJECT_OK);
    for (size_t index = 0; index < 2; ++index) {
        uintptr_t idle = UINT64_C(0x20000000) + index * MICROS_PRIMARY_TRAP_STACK_MIN_SIZE;
        uintptr_t emergency = UINT64_C(0x30000000) + index * MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE;

        REQUIRE(micros_hart_register(&fixture.objects, index, &fixture.harts[index]) == MICROS_KERNEL_OBJECT_OK);
        REQUIRE(micros_hart_install_trap_stacks(
            &fixture.objects, fixture.harts[index],
            idle, idle + MICROS_PRIMARY_TRAP_STACK_MIN_SIZE,
            emergency, emergency + MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE
        ) == MICROS_KERNEL_OBJECT_OK);
        REQUIRE(micros_scheduler_accounting_initialize(
            &fixture.objects, fixture.harts[index], 1
        ) == MICROS_KERNEL_OBJECT_OK);
    }
    for (size_t index = 0; index < spec->count; ++index) {
        uintptr_t bottom = UINT64_C(0x10000000) + index * MICROS_THREAD_KERNEL_STACK_SIZE;
        uint8_t priority = spec->two_priorities && index != 0 ? 3 : spec->priority;

        REQUIRE(micros_thread_create(
            &fixture.objects, fixture.process, &fixture.handles[index]
        ) == MICROS_KERNEL_OBJECT_OK);
        REQUIRE(micros_thread_attach_execution_context(
            &fixture.objects, fixture.handles[index],
            bottom, bottom + MICROS_THREAD_KERNEL_STACK_SIZE, &context
        ) == MICROS_KERNEL_OBJECT_OK);
        REQUIRE(micros_thread_scheduler_admit(
            &fixture.objects, fixture.harts[0], fixture.handles[index],
            priority, 100 + index, index != spec->nonpreemptible
        ) == MICROS_KERNEL_OBJECT_OK);
        if (index < spec->exhausted) {
            fixture.objects.threads[index].remaining_counter_ticks = 0;
        }
    }
    hart = &fixture.objects.harts[0];
    if (spec->mode != READY) {
        struct micros_thread *current = &fixture.objects.threads[0];

        REQUIRE(spec->count != 0);
        hart->current_thread = fixture.handles[0];
        hart->trap.primary_stack_bottom = current->kernel_stack_bottom;
        hart->trap.primary_stack_top = current->kernel_stack_top;
        if (spec->mode == PREEMPTED || spec->mode == PREEMPTED_BLOCKED) {
            hart->ready_head[spec->priority] = current->ready_next;
            if (same_thread(hart->ready_tail[spec->priority], fixture.handles[0])) {
                hart->ready_tail[spec->priority] = (struct micros_thread_handle){0, 0};
            }
            current->ready_linked = false;
            current->ready_next = (struct micros_thread_handle){0, 0};
            current->runtime_flags = MICROS_THREAD_RTS_PREEMPTED
                | (spec->mode == PREEMPTED_BLOCKED ? MICROS_THREAD_RTS_INACTIVE : 0);
        }
    }
    REQUIRE(micros_kernel_objects_validate(&fixture.objects) == MICROS_KERNEL_OBJECT_OK);
    switch (spec->fault) {
    case NO_FAULT:
        break;
    case HEAD_CYCLE:
        fixture.objects.threads[0].ready_next = fixture.handles[0];
        break;
    case PREFIX_CYCLE:
        fixture.objects.threads[3].ready_next = fixture.handles[1];
        break;
    case PREFIX_CURRENT_BACKEDGE:
        fixture.objects.threads[3].ready_next = fixture.handles[0];
        break;
    case STALE_SUCCESSOR:
        ++fixture.objects.threads[2].ready_next.generation;
        break;
    case OUTSIDE_SUCCESSOR:
        fixture.objects.threads[2].ready_next.slot = MICROS_THREAD_CAPACITY;
        break;
    case WRONG_TAIL:
        hart->ready_tail[spec->priority] = fixture.handles[1];
        break;
    case DUPLICATE_PRIORITY:
        hart->ready_head[2] = hart->ready_head[spec->priority];
        hart->ready_tail[2] = hart->ready_tail[spec->priority];
        break;
    case STALE_HART:
        ++fixture.harts[0].generation;
        break;
    case THREAD_ACCOUNTING:
        hart->accounting_owner = MICROS_SCHEDULER_ACCOUNTING_THREAD;
        hart->accounted_thread = hart->current_thread;
        break;
    case IDLE_ACCOUNTING:
        hart->accounting_owner = MICROS_SCHEDULER_ACCOUNTING_IDLE;
        break;
    case NONCANONICAL_TAIL:
        hart->ready_tail[0].slot = UINT16_MAX;
        break;
    case OTHER_HART_CORRUPTION:
        fixture.objects.harts[1].ready_tail[0] = fixture.handles[0];
        break;
    case UNUSED_ASSIGNMENT:
        fixture.objects.threads[MICROS_THREAD_CAPACITY - 1].scheduler_assigned = true;
        fixture.objects.threads[MICROS_THREAD_CAPACITY - 1].scheduler_hart = fixture.harts[0];
        break;
    case EMPTY_RESCHEDULE:
        hart->reschedule_pending = true;
        break;
    case UNREACHABLE_READY:
        hart->ready_tail[spec->priority] = fixture.handles[0];
        fixture.objects.threads[0].ready_next = (struct micros_thread_handle){0, 0};
        break;
    case NULL_ENCODINGS:
        hart->ready_head[1].slot = UINT16_MAX;
        fixture.objects.threads[spec->count - 1].ready_next.slot = UINT16_MAX;
        break;
    }
    return true;
}

static struct micros_thread_handle model_pick(const struct micros_hart *hart)
{
    for (size_t priority = 0; priority < MICROS_SCHEDULER_PRIORITY_COUNT; ++priority) {
        if (hart->ready_head[priority].generation != 0) {
            return hart->ready_head[priority];
        }
    }
    return (struct micros_thread_handle){0, 0};
}

static void model_append(struct micros_hart *hart, struct micros_thread_handle handle)
{
    struct micros_thread *thread = &fixture.expected.threads[handle.slot];
    uint8_t priority = thread->scheduler_priority;

    if (hart->ready_head[priority].generation == 0) {
        hart->ready_head[priority] = handle;
    } else {
        fixture.expected.threads[hart->ready_tail[priority].slot].ready_next = handle;
    }
    thread->ready_linked = true;
    thread->ready_next = (struct micros_thread_handle){0, 0};
    hart->ready_tail[priority] = handle;
}

static bool reference_return(void)
{
    struct micros_hart *hart;
    struct micros_thread_handle selected;
    struct micros_thread_handle outgoing;
    bool retain;

    fixture.expected = fixture.before;
    fixture.renewed = 0;
    hart = &fixture.expected.harts[0];
    outgoing = hart->current_thread;
    retain = outgoing.generation != 0
        && fixture.expected.threads[outgoing.slot].runtime_flags == 0;
    if (outgoing.generation != 0) {
        struct micros_thread *thread = &fixture.expected.threads[outgoing.slot];

        if ((thread->runtime_flags & MICROS_THREAD_RTS_PREEMPTED) != 0) {
            thread->runtime_flags &= ~MICROS_THREAD_RTS_PREEMPTED;
            if (thread->runtime_flags == 0) {
                if (thread->remaining_counter_ticks == 0) {
                    model_append(hart, outgoing);
                } else {
                    uint8_t priority = thread->scheduler_priority;

                    thread->ready_next = hart->ready_head[priority];
                    thread->ready_linked = true;
                    if (hart->ready_head[priority].generation == 0) {
                        hart->ready_tail[priority] = outgoing;
                    }
                    hart->ready_head[priority] = outgoing;
                }
            }
        }
    }
    selected = retain ? outgoing : model_pick(hart);
    while (selected.generation != 0) {
        struct micros_thread *thread = &fixture.expected.threads[selected.slot];
        uint8_t priority = thread->scheduler_priority;

        if (thread->remaining_counter_ticks != 0) {
            break;
        }
        REQUIRE(++fixture.renewed <= MICROS_THREAD_CAPACITY);
        thread->remaining_counter_ticks = thread->quantum_counter_ticks;
        if (!thread->scheduler_preemptible) {
            break;
        }
        REQUIRE(same_thread(hart->ready_head[priority], selected));
        hart->ready_head[priority] = thread->ready_next;
        if (same_thread(hart->ready_tail[priority], selected)) {
            hart->ready_tail[priority] = (struct micros_thread_handle){0, 0};
        }
        model_append(hart, selected);
        selected = model_pick(hart);
    }
    hart->current_thread = selected;
    hart->reschedule_pending = false;
    hart->trap.primary_stack_bottom = selected.generation == 0
        ? hart->idle_primary_stack_bottom : fixture.expected.threads[selected.slot].kernel_stack_bottom;
    hart->trap.primary_stack_top = selected.generation == 0
        ? hart->idle_primary_stack_top : fixture.expected.threads[selected.slot].kernel_stack_top;
#if MICROS_OBJ02_NATIVE_STAGE == 0
    for (size_t index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        struct micros_thread *thread = &fixture.expected.threads[index];

        if (thread->scheduler_assigned && thread->scheduler_hart.slot == 0
            && thread->scheduler_hart.generation == fixture.harts[0].generation
            && thread->ready_next.generation == 0) {
            thread->ready_next = (struct micros_thread_handle){0, 0};
        }
    }
    for (size_t priority = 0; priority < MICROS_SCHEDULER_PRIORITY_COUNT; ++priority) {
        if (hart->ready_head[priority].generation == 0) {
            hart->ready_head[priority] = (struct micros_thread_handle){0, 0};
        }
    }
#endif
    return true;
}

static bool begin_capture(void)
{
    ++fixture.capture;
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(micros_test_work_begin(
        UINT64_C(0x4f424a3032000001) + UINT64_C(256) * MICROS_OBJ02_NATIVE_STAGE,
        fixture.capture,
        (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_OBJECTS)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_SCHEDULER)
    ) == MICROS_TEST_WORK_OK);
#endif
    return true;
}

static bool finish_capture(void)
{
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
#endif
    return true;
}

static void print_capture(
    const struct fixture_spec *spec,
    const char *operation,
    enum micros_kernel_object_error error
)
{
    printf(
        "MICROS_OBJ02_CASE {\"version\":1,\"case\":%" PRIu64
        ",\"name\":\"%s\",\"operation\":\"%s\",\"threads\":%zu,"
        "\"exhausted\":%zu,\"priority\":%u,\"mode\":%u,\"nonpreemptible\":%zu,"
        "\"two_priorities\":%u,\"fault\":%u,\"result\":%u,\"renewed\":%zu,"
        "\"current_slot\":%u,\"current_generation\":%" PRIu32
        ",\"plan_bytes\":%zu,\"workspace_bytes\":%zu,"
        "\"before_state\":%" PRIu64 ",\"after_state\":%" PRIu64
        ",\"logical_state\":%" PRIu64 ",\"plan_preserved\":%u}\n",
        fixture.capture, spec->name, operation, spec->count, spec->exhausted,
        (unsigned int)spec->priority, (unsigned int)spec->mode, spec->nonpreemptible,
        (unsigned int)spec->two_priorities, (unsigned int)spec->fault, (unsigned int)error,
        fixture.renewed, (unsigned int)fixture.objects.harts[0].current_thread.slot,
        fixture.objects.harts[0].current_thread.generation,
        sizeof(fixture.plan), micros_obj02_return_workspace_size(),
        bytes_digest(&fixture.before, sizeof(fixture.before)),
        bytes_digest(&fixture.objects, sizeof(fixture.objects)), logical_digest(&fixture.objects),
        (unsigned int)(memcmp(&fixture.plan, &fixture.saved_plan, sizeof(fixture.plan)) == 0)
    );
#if defined(MICROS_TEST_WORK_COUNTS)
    printf(
        "MICROS_OBJ02_WORK {\"version\":%" PRIu32 ",\"reserved0\":%" PRIu32
        ",\"producer\":%" PRIu64 ",\"case\":%" PRIu64
        ",\"owner_mask\":%" PRIu32 ",\"reserved1\":%" PRIu32 ",\"counts\":[",
        snapshot.version, snapshot.reserved0, snapshot.producer_label,
        snapshot.capture_label, snapshot.owner_mask, snapshot.reserved1
    );
    for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        printf("%s[", owner == 0 ? "" : ",");
        for (size_t kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            printf("%s%" PRIu64, kind == 0 ? "" : ",", snapshot.counts[owner][kind]);
        }
        printf("]");
    }
    printf("]}\n");
#endif
}

static bool run_case(const struct fixture_spec *spec, enum micros_kernel_object_error wanted)
{
    enum micros_kernel_object_error error;
    struct micros_thread_handle selected;
    enum micros_scheduler_return_action action;

    REQUIRE(setup(spec));
    fixture.before = fixture.objects;
    fixture.renewed = 0;
    memset(&fixture.plan, 0xa5, sizeof(fixture.plan));
    fixture.saved_plan = fixture.plan;
    if (wanted == MICROS_KERNEL_OBJECT_OK) {
        REQUIRE(reference_return());
    }
    REQUIRE(begin_capture());
    error = micros_hart_plan_user_return(&fixture.objects, fixture.harts[0], &fixture.plan);
    REQUIRE(finish_capture());
    REQUIRE(error == wanted);
    REQUIRE(memcmp(&fixture.objects, &fixture.before, sizeof(fixture.objects)) == 0);
    if (error != MICROS_KERNEL_OBJECT_OK) {
        REQUIRE(memcmp(&fixture.plan, &fixture.saved_plan, sizeof(fixture.plan)) == 0);
        print_capture(spec, "construct", error);
        return true;
    }
    selected = fixture.expected.harts[0].current_thread;
    action = selected.generation == 0 ? MICROS_SCHEDULER_RETURN_ENTER_IDLE
        : same_thread(selected, fixture.before.harts[0].current_thread)
            ? MICROS_SCHEDULER_RETURN_KEEP_CURRENT : MICROS_SCHEDULER_RETURN_SELECT_THREAD;
    REQUIRE(same_thread(fixture.plan.selected, selected));
    REQUIRE(same_thread(fixture.plan.outgoing, fixture.before.harts[0].current_thread));
    REQUIRE(fixture.plan.action == action);
    REQUIRE(fixture.plan.renew_quantum == (fixture.renewed != 0));
    REQUIRE(fixture.plan.repair_preempted == (spec->mode == PREEMPTED || spec->mode == PREEMPTED_BLOCKED));
    print_capture(spec, "construct", error);
    fixture.saved_plan = fixture.plan;
    REQUIRE(begin_capture());
    error = micros_hart_commit_user_return(&fixture.objects, &fixture.plan);
    REQUIRE(finish_capture());
    REQUIRE(error == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(memcmp(&fixture.objects, &fixture.expected, sizeof(fixture.objects)) == 0);
    REQUIRE(memcmp(&fixture.plan, &fixture.saved_plan, sizeof(fixture.plan)) == 0);
    print_capture(spec, "checked", error);
    fixture.objects = fixture.before;
    REQUIRE(begin_capture());
    micros_scheduler_apply_return_plan(&fixture.objects, &fixture.plan);
    REQUIRE(finish_capture());
    REQUIRE(memcmp(&fixture.objects, &fixture.expected, sizeof(fixture.objects)) == 0);
    REQUIRE(memcmp(&fixture.plan, &fixture.saved_plan, sizeof(fixture.plan)) == 0);
    print_capture(spec, "apply", MICROS_KERNEL_OBJECT_OK);
    return true;
}

static bool regular_cases(void)
{
    const size_t counts[] = {1, 2, 128};
    const uint8_t priorities[] = {0, 15};

    for (size_t priority = 0; priority < sizeof(priorities) / sizeof(priorities[0]); ++priority) {
        struct fixture_spec empty = {
            "empty", 0, 0, priorities[priority], READY, MICROS_THREAD_CAPACITY, false, NO_FAULT,
        };

        REQUIRE(run_case(&empty, MICROS_KERNEL_OBJECT_OK));
        for (size_t count = 0; count < sizeof(counts) / sizeof(counts[0]); ++count) {
            for (enum fixture_mode mode = READY; mode <= CURRENT; ++mode) {
                for (size_t zero = 0; zero < 2; ++zero) {
                    struct fixture_spec spec = {
                        zero == 0 ? "positive" : "all-zero",
                        counts[count], zero == 0 ? 0 : counts[count], priorities[priority],
                        mode, MICROS_THREAD_CAPACITY, false, NO_FAULT,
                    };

                    REQUIRE(run_case(&spec, MICROS_KERNEL_OBJECT_OK));
                }
            }
        }
    }
    for (size_t count = 0; count < sizeof(counts) / sizeof(counts[0]); ++count) {
        for (enum fixture_mode mode = PREEMPTED; mode <= PREEMPTED_BLOCKED; ++mode) {
            for (size_t zero = 0; zero < 2; ++zero) {
                struct fixture_spec spec = {
                    "repair", counts[count], zero == 0 ? 0 : counts[count],
                    7, mode, MICROS_THREAD_CAPACITY, false, NO_FAULT,
                };

                REQUIRE(run_case(&spec, MICROS_KERNEL_OBJECT_OK));
            }
        }
    }
    const struct fixture_spec extra[] = {
        {"mixed-prefix", 8, 3, 7, READY, 128, false, NO_FAULT},
        {"mixed-current", 128, 127, 7, CURRENT, 128, false, NO_FAULT},
        {"nonpreemptible-head", 8, 8, 7, READY, 0, false, NO_FAULT},
        {"nonpreemptible-prefix", 8, 8, 7, READY, 3, false, NO_FAULT},
        {"nonpreemptible-current", 8, 8, 7, CURRENT, 0, true, NO_FAULT},
        {"nonpreemptible-repaired", 8, 8, 7, PREEMPTED, 0, false, NO_FAULT},
        {"different-priorities", 8, 8, 7, CURRENT, 128, true, NO_FAULT},
        {"repair-different-priorities", 8, 8, 7, PREEMPTED, 128, true, NO_FAULT},
        {"repair-head-different-priorities", 8, 0, 7, PREEMPTED, 128, true, NO_FAULT},
        {"permitted-null", 8, 0, 7, CURRENT, 128, false, NULL_ENCODINGS},
        {"permitted-null-rotation", 8, 8, 7, READY, 128, false, NULL_ENCODINGS},
        {"unselected-hart", 8, 3, 7, READY, 128, false, OTHER_HART_CORRUPTION},
    };
    for (size_t index = 0; index < sizeof(extra) / sizeof(extra[0]); ++index) {
        REQUIRE(run_case(&extra[index], MICROS_KERNEL_OBJECT_OK));
    }
    return true;
}

static bool rejection_cases(void)
{
    const struct fixture_spec cases[] = {
        {"head-cycle", 8, 8, 7, READY, 128, false, HEAD_CYCLE},
        {"prefix-cycle", 8, 8, 7, READY, 128, false, PREFIX_CYCLE},
        {"current-backedge", 8, 8, 7, CURRENT, 128, false, PREFIX_CURRENT_BACKEDGE},
        {"stale-successor", 8, 8, 7, READY, 128, false, STALE_SUCCESSOR},
        {"outside-successor", 8, 8, 7, READY, 128, false, OUTSIDE_SUCCESSOR},
        {"wrong-tail", 8, 8, 7, READY, 128, false, WRONG_TAIL},
        {"duplicate-priority", 8, 8, 7, READY, 128, false, DUPLICATE_PRIORITY},
        {"stale-hart", 8, 0, 7, READY, 128, false, STALE_HART},
        {"thread-accounting", 8, 0, 7, CURRENT, 128, false, THREAD_ACCOUNTING},
        {"idle-accounting", 0, 0, 7, READY, 128, false, IDLE_ACCOUNTING},
        {"noncanonical-tail", 0, 0, 7, READY, 128, false, NONCANONICAL_TAIL},
        {"unused-assignment", 0, 0, 7, READY, 128, false, UNUSED_ASSIGNMENT},
        {"empty-reschedule", 0, 0, 7, READY, 128, false, EMPTY_RESCHEDULE},
        {"unreachable-ready", 2, 0, 7, READY, 128, false, UNREACHABLE_READY},
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        enum micros_kernel_object_error wanted = MICROS_KERNEL_OBJECT_ERROR_INVARIANT;

        if (cases[index].fault == STALE_HART) {
            wanted = MICROS_KERNEL_OBJECT_ERROR_STALE;
        } else if (cases[index].fault == THREAD_ACCOUNTING || cases[index].fault == IDLE_ACCOUNTING) {
            wanted = MICROS_KERNEL_OBJECT_ERROR_STATE;
        } else if (MICROS_OBJ02_NATIVE_STAGE == 1 && (
            cases[index].fault == UNUSED_ASSIGNMENT || cases[index].fault == EMPTY_RESCHEDULE
            || cases[index].fault == UNREACHABLE_READY
        )) {
            wanted = MICROS_KERNEL_OBJECT_OK;
        }
        REQUIRE(run_case(&cases[index], wanted));
        if (cases[index].fault >= UNUSED_ASSIGNMENT && cases[index].fault <= UNREACHABLE_READY) {
            REQUIRE(micros_kernel_objects_validate(&fixture.before) == MICROS_KERNEL_OBJECT_ERROR_INVARIANT);
        }
    }
    return true;
}

static bool stale_and_tampered_plans(void)
{
    const char *const names[] = {
        "tamper-selected", "tamper-outgoing", "tamper-action", "tamper-repair",
        "tamper-renew", "tamper-queue-count", "tamper-patch-count", "tamper-renew-count",
    };
    struct fixture_spec spec = {"stale-tail", 2, 0, 7, READY, 128, false, NO_FAULT};
    enum micros_kernel_object_error error;

    REQUIRE(setup(&spec));
    REQUIRE(micros_hart_plan_user_return(
        &fixture.objects, fixture.harts[0], &fixture.plan
    ) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_thread_scheduler_hold(
        &fixture.objects, fixture.handles[1]
    ) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(same_thread(fixture.objects.harts[0].ready_head[7], fixture.plan.selected));
    fixture.before = fixture.objects;
    fixture.saved_plan = fixture.plan;
    fixture.renewed = 0;
    REQUIRE(begin_capture());
    error = micros_hart_commit_user_return(&fixture.objects, &fixture.plan);
    REQUIRE(finish_capture());
    REQUIRE(error == MICROS_KERNEL_OBJECT_ERROR_STATE);
    REQUIRE(memcmp(&fixture.objects, &fixture.before, sizeof(fixture.objects)) == 0);
    REQUIRE(memcmp(&fixture.plan, &fixture.saved_plan, sizeof(fixture.plan)) == 0);
    print_capture(&spec, "checked", error);
    for (size_t mutation = 0; mutation < sizeof(names) / sizeof(names[0]); ++mutation) {
        spec.name = names[mutation];
        spec.exhausted = 2;
        REQUIRE(setup(&spec));
        REQUIRE(micros_hart_plan_user_return(
            &fixture.objects, fixture.harts[0], &fixture.plan
        ) == MICROS_KERNEL_OBJECT_OK);
        switch (mutation) {
        case 0: ++fixture.plan.selected.generation; break;
        case 1: ++fixture.plan.outgoing.generation; break;
        case 2: fixture.plan.action = MICROS_SCHEDULER_RETURN_ENTER_IDLE; break;
        case 3: fixture.plan.repair_preempted = !fixture.plan.repair_preempted; break;
        case 4: fixture.plan.renew_quantum = !fixture.plan.renew_quantum; break;
#if MICROS_OBJ02_NATIVE_STAGE == 0
        case 5: fixture.plan.renew_bitmap[0] ^= 1; break;
        case 6: ++fixture.plan.ready_tail[7].generation; break;
        case 7: ++fixture.plan.ready_next[0].generation; break;
#else
        case 5: fixture.plan.queue_count = UINT8_MAX; break;
        case 6: fixture.plan.queues[0].patch_count = UINT8_MAX; break;
        case 7: fixture.plan.queues[0].renew_count = UINT16_MAX; break;
#endif
        }
        fixture.before = fixture.objects;
        fixture.saved_plan = fixture.plan;
        fixture.renewed = 0;
        REQUIRE(begin_capture());
        error = micros_hart_commit_user_return(&fixture.objects, &fixture.plan);
        REQUIRE(finish_capture());
        REQUIRE(error == MICROS_KERNEL_OBJECT_ERROR_STATE);
        REQUIRE(memcmp(&fixture.objects, &fixture.before, sizeof(fixture.objects)) == 0);
        REQUIRE(memcmp(&fixture.plan, &fixture.saved_plan, sizeof(fixture.plan)) == 0);
        print_capture(&spec, "checked", error);
    }
    spec.name = "plan-padding";
    REQUIRE(setup(&spec));
    REQUIRE(micros_hart_plan_user_return(
        &fixture.objects, fixture.harts[0], &fixture.plan
    ) == MICROS_KERNEL_OBJECT_OK);
    {
        unsigned char *bytes = (unsigned char *)&fixture.plan;
#if MICROS_OBJ02_NATIVE_STAGE == 0
        size_t begin = offsetof(struct micros_scheduler_return_plan, renew_quantum)
            + sizeof(fixture.plan.renew_quantum);
        size_t end = offsetof(struct micros_scheduler_return_plan, renew_bitmap);
#else
        size_t begin = offsetof(struct micros_scheduler_return_plan, queue_count)
            + sizeof(fixture.plan.queue_count);
        size_t end = offsetof(struct micros_scheduler_return_plan, queues);
#endif

        REQUIRE(begin < end);
        for (size_t index = begin; index < end; ++index) {
            bytes[index] = 0x5a;
        }
    }
    fixture.before = fixture.objects;
    fixture.saved_plan = fixture.plan;
    REQUIRE(reference_return());
    REQUIRE(begin_capture());
    error = micros_hart_commit_user_return(&fixture.objects, &fixture.plan);
    REQUIRE(finish_capture());
    REQUIRE(error == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(memcmp(&fixture.objects, &fixture.expected, sizeof(fixture.objects)) == 0);
    REQUIRE(memcmp(&fixture.plan, &fixture.saved_plan, sizeof(fixture.plan)) == 0);
    print_capture(&spec, "checked", error);
    return true;
}

#if defined(MICROS_COMMON_SCHEDULER_RETURN_LAUNCHER)
static int launch_native_products(char **argv)
{
    const char *launcher = getenv("__PYVENV_LAUNCHER__");
    char *arguments[] = {
        MICROS_COMMON_SCHEDULER_RETURN_PYTHON, "-E", "-S", "-B",
        MICROS_COMMON_SCHEDULER_RETURN_DRIVER, "--native",
        "--executable", argv[1], "--executable", argv[2],
        "--executable", argv[3], "--executable", argv[0], NULL,
    };

    if (launcher != NULL && launcher[0] != '\0') {
        fputs("__PYVENV_LAUNCHER__ is unsupported\n", stderr);
        return 2;
    }
    if (chdir(MICROS_COMMON_SCHEDULER_RETURN_ROOT) == 0) {
        execv(MICROS_COMMON_SCHEDULER_RETURN_PYTHON, arguments);
    }
    perror("OBJ02 native orchestration");
    return 2;
}
#endif

int main(int argc, char **argv)
{
#if defined(MICROS_COMMON_SCHEDULER_RETURN_LAUNCHER)
    if (argc == 4) {
        return launch_native_products(argv);
    }
#endif
    if (argc != 3 || strcmp(argv[1], MICROS_OBJ02_NATIVE_STAGE == 0 ? "baseline" : "current") != 0) {
        return 2;
    }
#if defined(MICROS_TEST_WORK_COUNTS)
    if (strcmp(argv[2], "work") != 0) {
        return 2;
    }
#else
    if (strcmp(argv[2], "ordinary") != 0) {
        return 2;
    }
#endif
    if (!regular_cases() || !rejection_cases() || !stale_and_tampered_plans()) {
        return 1;
    }
    printf("MICROS_OBJ02_NATIVE_PASS cases=%" PRIu64 "\n", fixture.capture);
    return 0;
}
