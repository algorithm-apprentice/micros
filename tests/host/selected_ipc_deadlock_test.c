#include "tests/host/selected_ipc_validation_test.h"
#include "kernel/ipc/ipc_internal.h"

#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(struct micros_process) <= 48, "sole index adds at most one handle per process");

enum dependency_end {
    DEPENDENCY_CLEAR,
    DEPENDENCY_CANDIDATE,
    DEPENDENCY_REPEATED,
    DEPENDENCY_ANY,
    DEPENDENCY_CLEARED,
    DEPENDENCY_INITIAL_ANY,
    DEPENDENCY_INITIAL_NONE,
    DEPENDENCY_PERMUTED,
    DEPENDENCY_BAD_CLEARED,
};

struct dependency_case {
    const char *name;
    size_t processes;
    size_t prefix;
    enum dependency_end end;
};

static const struct dependency_case dependency_cases[] = {
    {"dependency-short", 2, 0, DEPENDENCY_CLEAR},
    {"dependency-medium", 8, 0, DEPENDENCY_CLEAR},
    {"dependency-long", 64, 0, DEPENDENCY_CLEAR},
    {"dependency-cross-word", 64, 32, DEPENDENCY_CLEAR},
    {"dependency-high", 64, 64, DEPENDENCY_CLEAR},
    {"dependency-candidate-short", 2, 64, DEPENDENCY_CANDIDATE},
    {"dependency-candidate-long", 64, 64, DEPENDENCY_CANDIDATE},
    {"dependency-repeated-short", 3, 64, DEPENDENCY_REPEATED},
    {"dependency-repeated-long", 64, 32, DEPENDENCY_REPEATED},
    {"dependency-any", 8, 64, DEPENDENCY_ANY},
    {"dependency-cleared", 64, 32, DEPENDENCY_CLEARED},
    {"dependency-initial-any", 2, 64, DEPENDENCY_INITIAL_ANY},
    {"dependency-initial-none", 2, 64, DEPENDENCY_INITIAL_NONE},
    {"dependency-permuted", 64, 0, DEPENDENCY_PERMUTED},
    {"dependency-bad-cleared", 8, 32, DEPENDENCY_BAD_CLEARED},
};

enum index_case {
    INDEX_CREATE_FIRST,
    INDEX_CREATE_SECOND,
    INDEX_RELEASE_SINGLE,
    INDEX_RELEASE_PAIR_LOW,
    INDEX_RELEASE_PAIR_HIGH,
    INDEX_RELEASE_MANY,
    INDEX_REPAIR_MISSING,
    INDEX_REPAIR_DUPLICATE,
};

static const char *const index_cases[] = {
    "index-create-first", "index-create-second", "index-release-single",
    "index-release-pair-low", "index-release-pair-high", "index-release-many",
    "index-repair-missing", "index-repair-duplicate",
};

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "IPC02_FIXTURE_FAILURE line=%d expression=%s\n", __LINE__, #condition); \
            return false; \
        } \
    } while (false)

static bool prepare_dependency_threads(const struct dependency_case *test)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    struct micros_thread_handle allocated[MICROS_PROCESS_CAPACITY];

    REQUIRE(selected_ipc_setup(MICROS_THREAD_CAPACITY));
    selected_ipc_test_name(test->name);
    REQUIRE(micros_hart_register(&fixture->objects, 0, &fixture->hart) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_hart_install_trap_stacks(
        &fixture->objects, fixture->hart,
        UINT64_C(0x20000000), UINT64_C(0x20000000) + MICROS_PRIMARY_TRAP_STACK_MIN_SIZE,
        UINT64_C(0x30000000), UINT64_C(0x30000000) + MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE
    ) == MICROS_KERNEL_OBJECT_OK);
    for (size_t slot = 1; slot < MICROS_PROCESS_CAPACITY - 1; ++slot) {
        micros_endpoint_t endpoint;

        REQUIRE(micros_endpoint_reserve(
            &fixture->registry, &fixture->objects, fixture->processes[slot], &endpoint
        ) == MICROS_ENDPOINT_OK);
        REQUIRE(micros_endpoint_install_profile(
            &fixture->registry, &fixture->objects, fixture->processes[slot], 1
        ) == MICROS_ENDPOINT_OK);
        REQUIRE(micros_endpoint_activate(&fixture->registry, &fixture->objects, endpoint)
                == MICROS_ENDPOINT_OK);
    }
    for (size_t slot = 0; slot < test->prefix; ++slot) {
        struct micros_thread_handle filler;

        REQUIRE(micros_thread_create(
            &fixture->objects, fixture->processes[MICROS_PROCESS_CAPACITY - 1], &filler
        ) == MICROS_KERNEL_OBJECT_OK);
        REQUIRE(filler.slot == slot);
    }
    for (size_t slot = 0; slot < test->processes; ++slot) {
        size_t owner = test->end == DEPENDENCY_PERMUTED ? (slot + 32) % test->processes : slot;

        REQUIRE(selected_ipc_prepare_thread(owner));
        REQUIRE(fixture->threads[slot].slot == test->prefix + slot);
        allocated[owner] = fixture->threads[slot];
    }
    memcpy(fixture->threads, allocated, test->processes * sizeof(allocated[0]));
    for (size_t slot = 0; slot < test->prefix; ++slot) {
        struct micros_thread_handle filler = {
            (uint16_t)slot, fixture->objects.threads[slot].generation,
        };

        REQUIRE(micros_thread_release(&fixture->objects, filler) == MICROS_KERNEL_OBJECT_OK);
    }
    REQUIRE(micros_endpoint_registry_validate_objects(&fixture->registry, &fixture->objects)
            == MICROS_ENDPOINT_OK);
    return true;
}

static bool prepare_dependency_edges(const struct dependency_case *test)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    struct micros_ipc_message message = {.type = UINT32_C(0x5310)};

    for (size_t index = 1; index < test->processes; ++index) {
        size_t next = index + 1;
        bool last = next == test->processes;

        if (test->end == DEPENDENCY_ANY) {
            REQUIRE(micros_ipc_receiver_enqueue(
                &fixture->registry, &fixture->objects, fixture->threads[index],
                last ? MICROS_ENDPOINT_ANY : fixture->registry.endpoints[next].value,
                UINT64_C(0x50000000)
            ) == MICROS_IPC_OK);
            continue;
        }
        if (last) {
            if (test->end == DEPENDENCY_CANDIDATE || test->end == DEPENDENCY_CLEARED) {
                next = 0;
            } else if (test->end == DEPENDENCY_REPEATED) {
                next = 1;
            } else {
                continue;
            }
        }
        REQUIRE(micros_ipc_sender_enqueue(
            &fixture->registry, &fixture->objects, fixture->threads[index],
            fixture->registry.endpoints[next].value, &message, 0, 0
        ) == MICROS_IPC_OK);
    }
    REQUIRE(micros_endpoint_registry_validate_objects(&fixture->registry, &fixture->objects)
            == MICROS_ENDPOINT_OK);
    return true;
}

static bool run_dependency_case(size_t index, const struct dependency_case *test)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    struct micros_thread_handle cleared = {0, 0};
    enum micros_ipc_error expected = MICROS_IPC_OK;
    enum micros_ipc_error observed;
    enum micros_endpoint_error diagnostic = MICROS_ENDPOINT_OK;
    micros_endpoint_t dependency;
    size_t hops = test->processes - 1;

    REQUIRE(prepare_dependency_threads(test));
    REQUIRE(prepare_dependency_edges(test));
    dependency = fixture->registry.endpoints[1].value;
    if (test->end == DEPENDENCY_CANDIDATE) {
        expected = MICROS_IPC_ERROR_DEADLOCK;
        ++hops;
    } else if (test->end == DEPENDENCY_REPEATED) {
        expected = MICROS_IPC_ERROR_INVARIANT;
        ++hops;
    } else if (test->end == DEPENDENCY_CLEARED) {
        cleared = fixture->threads[test->processes - 1];
    } else if (test->end == DEPENDENCY_INITIAL_ANY) {
        dependency = MICROS_ENDPOINT_ANY;
        hops = 0;
    } else if (test->end == DEPENDENCY_INITIAL_NONE) {
        dependency = MICROS_ENDPOINT_NONE;
        expected = MICROS_IPC_ERROR_INVARIANT;
        hops = 0;
    } else if (test->end == DEPENDENCY_BAD_CLEARED) {
        cleared = fixture->threads[test->processes - 1];
        fixture->objects.processes[test->processes - 1].sole_thread = (struct micros_thread_handle){0, 0};
        expected = MICROS_IPC_ERROR_INVARIANT;
        diagnostic = MICROS_ENDPOINT_ERROR_INVARIANT;
        hops = 0;
    }
    REQUIRE(micros_endpoint_registry_validate_objects(&fixture->registry, &fixture->objects) == diagnostic);
    fixture->before_objects = fixture->objects;
    fixture->before_registry = fixture->registry;
    REQUIRE(selected_ipc_begin_capture(index));
    observed = ipc_preflight_deadlock(
        &fixture->registry, &fixture->objects, fixture->threads[0], dependency, cleared
    );
    REQUIRE(selected_ipc_finish_capture());
    REQUIRE(observed == expected);
    REQUIRE(micros_endpoint_registry_validate_objects(&fixture->registry, &fixture->objects) == diagnostic);
    return selected_ipc_report_case(index, test->name, (unsigned)observed, hops, true);
}

static bool prepare_index_case(
    enum index_case operation,
    struct micros_thread_handle *first,
    struct micros_thread_handle *second
)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    struct micros_thread_handle extra;
    struct micros_process_handle owner;

    REQUIRE(selected_ipc_setup(MICROS_THREAD_CAPACITY));
    selected_ipc_test_name(index_cases[operation]);
    owner = fixture->processes[0];
    if (operation == INDEX_CREATE_FIRST) {
        return true;
    }
    REQUIRE(micros_thread_create(&fixture->objects, owner, first) == MICROS_KERNEL_OBJECT_OK);
    if (operation == INDEX_CREATE_SECOND || operation == INDEX_RELEASE_SINGLE) {
        return true;
    }
    if (operation == INDEX_RELEASE_PAIR_HIGH) {
        for (size_t slot = 1; slot < MICROS_THREAD_CAPACITY - 1; ++slot) {
            REQUIRE(micros_thread_create(
                &fixture->objects, fixture->processes[MICROS_PROCESS_CAPACITY - 1], &extra
            ) == MICROS_KERNEL_OBJECT_OK);
            REQUIRE(extra.slot == slot);
        }
    }
    REQUIRE(micros_thread_create(&fixture->objects, owner, second) == MICROS_KERNEL_OBJECT_OK);
    if (operation == INDEX_RELEASE_PAIR_HIGH) {
        REQUIRE(second->slot == MICROS_THREAD_CAPACITY - 1);
        for (size_t slot = 1; slot < MICROS_THREAD_CAPACITY - 1; ++slot) {
            extra = (struct micros_thread_handle){(uint16_t)slot, fixture->objects.threads[slot].generation};
            REQUIRE(micros_thread_release(&fixture->objects, extra) == MICROS_KERNEL_OBJECT_OK);
        }
    } else if (operation == INDEX_RELEASE_MANY) {
        REQUIRE(micros_thread_create(&fixture->objects, owner, &extra) == MICROS_KERNEL_OBJECT_OK);
    } else if (operation == INDEX_REPAIR_MISSING) {
        fixture->objects.threads[second->slot].owner = fixture->processes[1];
    } else if (operation == INDEX_REPAIR_DUPLICATE) {
        REQUIRE(micros_thread_create(
            &fixture->objects, fixture->processes[1], &extra
        ) == MICROS_KERNEL_OBJECT_OK);
        fixture->objects.threads[extra.slot].owner = owner;
    }
    return true;
}

static bool run_index_case(size_t index, enum index_case operation)
{
    struct selected_ipc_fixture *fixture = selected_ipc_fixture_state();
    struct micros_thread_handle first = {0, 0};
    struct micros_thread_handle second = {0, 0};
    struct micros_thread_handle created = {UINT16_MAX, UINT32_MAX};
    bool invalid = operation == INDEX_REPAIR_MISSING || operation == INDEX_REPAIR_DUPLICATE;
    enum micros_endpoint_error diagnostic = invalid ? MICROS_ENDPOINT_ERROR_INVARIANT : MICROS_ENDPOINT_OK;
    enum micros_kernel_object_error error;
    struct micros_process *owner;

    REQUIRE(prepare_index_case(operation, &first, &second));
    owner = &fixture->objects.processes[0];
    REQUIRE(micros_endpoint_registry_validate_objects(&fixture->registry, &fixture->objects) == diagnostic);
    fixture->before_objects = fixture->objects;
    fixture->before_registry = fixture->registry;
    REQUIRE(selected_ipc_begin_capture(index));
    if (operation == INDEX_CREATE_FIRST || operation == INDEX_CREATE_SECOND) {
        error = micros_thread_create(&fixture->objects, fixture->processes[0], &created);
    } else {
        error = micros_thread_release(
            &fixture->objects, operation == INDEX_RELEASE_PAIR_LOW ? second : first
        );
    }
    REQUIRE(selected_ipc_finish_capture());
    REQUIRE(error == (invalid ? MICROS_KERNEL_OBJECT_ERROR_INVARIANT : MICROS_KERNEL_OBJECT_OK));
    if (!invalid) {
        struct micros_thread_handle expected = operation == INDEX_CREATE_FIRST ? created
            : operation == INDEX_RELEASE_PAIR_LOW ? first
            : operation == INDEX_RELEASE_PAIR_HIGH ? second : (struct micros_thread_handle){0, 0};

        REQUIRE(owner->sole_thread.slot == expected.slot && owner->sole_thread.generation == expected.generation);
    }
    REQUIRE(micros_endpoint_registry_validate_objects(&fixture->registry, &fixture->objects) == diagnostic);
    return selected_ipc_report_case(
        index, index_cases[operation], (unsigned)error, owner->live_thread_count, invalid
    );
}

size_t selected_ipc_deadlock_case_count(void)
{
    return sizeof(dependency_cases) / sizeof(dependency_cases[0]) + sizeof(index_cases) / sizeof(index_cases[0]);
}

bool selected_ipc_deadlock_cases(size_t first_case)
{
    size_t dependency_count = sizeof(dependency_cases) / sizeof(dependency_cases[0]);

    for (size_t index = 0; index < dependency_count; ++index) {
        if (!run_dependency_case(first_case + index, &dependency_cases[index])) {
            return false;
        }
    }
    for (size_t index = 0; index < sizeof(index_cases) / sizeof(index_cases[0]); ++index) {
        if (!run_index_case(first_case + dependency_count + index, (enum index_case)index)) {
            return false;
        }
    }
    return true;
}
