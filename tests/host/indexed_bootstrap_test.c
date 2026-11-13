#include "tests/host/bootstrap_control_test_internal.h"
#include "arch/riscv64/platform.h"
#include "kernel/boot/bootstrap_manifest_internal.h"
#include "kernel/boot/bootstrap_runtime_internal.h"
#include "kernel/tty/tty_handoff_runtime_internal.h"
#include "micros/panic.h"

#include <inttypes.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(MICROS_TEST_WORK_COUNTS)
#include "tests/work/work_counts.h"
#endif
#if defined(MICROS_INDEXED_BOOTSTRAP_LAUNCHER)
#include <unistd.h>
#endif

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "BOOT01_FIXTURE_FAILURE line=%d expression=%s\n", __LINE__, #condition); \
            return false; \
        } \
    } while (false)

static const char *const names[] = {
    "binding-first", "binding-last", "binding-missing", "binding-range",
    "classify-controller", "classify-last", "runtime-release-two", "runtime-release-many",
    "runtime-ready-many", "runtime-complete-many", "runtime-release-unknown", "runtime-ready-expired",
    "control-release", "control-ready-plan", "control-ready-commit", "control-ready-role",
    "control-complete-plan", "control-complete-commit",
    "deadline-before", "deadline-expired", "deadline-missing-index", "deadline-slot-range",
    "deadline-row-mismatch", "deadline-wrong-state", "deadline-zero", "deadline-binding-missing",
    "deadline-tty-first", "deadline-no-start", "deadline-sealed", "deadline-zero-count", "deadline-invalid-id",
    "single-service-complete", "binding-sparse", "binding-corruption", "control-index-diagnosis",
    "release-named-dependencies", "runtime-selected-corruption", "ready-counter-precedence", "selected-plan-canaries",
    "constructor-rejections", "preparing-permutation", "permuted-control-seal", "runtime-unvisited-scope",
    "control-unvisited-binding", "control-unvisited-runtime", "control-selected-tuple",
    "control-selection-guards",
    "control-complete-membership",
    "runtime-transfer-sizes", "runtime-unaligned-copy", "runtime-unaligned-clear", "image-catalog-clear",
};

static struct {
    struct micros_bootstrap_runtime runtime, before_runtime, expected_runtime;
    struct micros_bootstrap_control_state state, before_state, expected_state;
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_manifest_plan manifest_plan;
    struct micros_endpoint_registry registry, before_registry;
    struct micros_kernel_objects objects, before_objects;
    struct micros_hart_handle hart;
    struct micros_bootstrap_binding bindings[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    struct micros_bootstrap_ready_plan ready, before_ready;
    struct micros_bootstrap_complete_plan complete, before_complete;
} fixture;

static struct {
    _Alignas(8) unsigned char input[4096 + 32];
    _Alignas(8) unsigned char output[4096 + 32];
    unsigned char before[4096 + 32];
    unsigned char expected[4096 + 32];
} movement;

static struct {
    jmp_buf jump;
    struct bootstrap_runtime_context before, expected;
    bool armed, panicked;
    unsigned seize_count, flush_count;
    size_t length;
    char text[512];
} diagnostic;

void uart_write(const char *text)
{
    size_t length = strlen(text);

    if (!diagnostic.armed || length >= sizeof(diagnostic.text) - diagnostic.length) {
        abort();
    }
    memcpy(diagnostic.text + diagnostic.length, text, length + 1);
    diagnostic.length += length;
}

void uart_write_hex64(uint64_t value)
{
    char text[19];

    if (snprintf(text, sizeof(text), "0x%016" PRIx64, value) != 18) {
        abort();
    }
    uart_write(text);
}

void uart_flush(void)
{
    if (!diagnostic.armed) {
        abort();
    }
    ++diagnostic.flush_count;
}

void micros_panic_seize(void)
{
    if (!diagnostic.armed) {
        abort();
    }
    ++diagnostic.seize_count;
}

_Noreturn void bootstrap_runtime_panic_runtime(const char *reason)
{
    if (!diagnostic.armed || strcmp(reason, "bootstrap-failure") != 0) {
        abort();
    }
    diagnostic.panicked = true;
    longjmp(diagnostic.jump, 1);
}

#if defined(MICROS_TEST_WORK_COUNTS)
static struct micros_test_work_snapshot snapshot;
#endif

static bool begin(size_t index)
{
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(micros_test_work_begin(
        UINT64_C(0x424f4f5430310001), index + 1,
        (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_OBJECTS)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_SCHEDULER)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_ENDPOINT)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_IPC)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_BOOT)
            | (UINT32_C(1) << MICROS_WORK_OWNER_KERNEL_TTY)
            | (UINT32_C(1) << MICROS_WORK_OWNER_RUNTIME)
    ) == MICROS_TEST_WORK_OK);
#else
    (void)index;
#endif
    return true;
}

static bool finish(void)
{
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK);
#endif
    return true;
}

static uint64_t digest(const void *storage, size_t size)
{
    const unsigned char *bytes = storage;
    uint64_t result = UINT64_C(14695981039346656037);

    for (size_t byte = 0; byte < size; ++byte) {
        result = (result ^ bytes[byte]) * UINT64_C(1099511628211);
    }
    return result;
}

static bool report(size_t index, enum micros_bootstrap_error result, uint64_t value,
                   bool preserved, const void *before, const void *after, size_t size)
{
    REQUIRE((memcmp(before, after, size) == 0) == preserved);
    printf("MICROS_BOOT01_CASE {\"version\":1,\"case\":%zu,\"name\":\"%s\",\"result\":%u,"
           "\"value\":%" PRIu64 ",\"preserved\":%u,\"before_state\":%" PRIu64 ",\"after_state\":%" PRIu64 "}\n",
           index + 1, names[index], (unsigned)result, value, (unsigned)preserved,
           digest(before, size), digest(after, size));
#if defined(MICROS_TEST_WORK_COUNTS)
    printf("MICROS_BOOT01_WORK {\"version\":%u,\"reserved0\":%u,\"producer\":%" PRIu64
           ",\"case\":%" PRIu64 ",\"owner_mask\":%u,\"reserved1\":%u,\"counts\":[",
           snapshot.version, snapshot.reserved0, snapshot.producer_label, snapshot.capture_label,
           snapshot.owner_mask, snapshot.reserved1);
    for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        printf("%s[", owner == 0 ? "" : ",");
        for (size_t kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            printf("%s%" PRIu64, kind == 0 ? "" : ",", snapshot.counts[owner][kind]);
        }
        printf("]");
    }
    printf("]}\n");
#endif
    return true;
}

static void snapshot_control(void)
{
    fixture.before_state = fixture.state;
    fixture.expected_state = fixture.state;
    fixture.before_objects = fixture.objects;
    fixture.before_registry = fixture.registry;
}

static bool setup_control(bool ready_pm)
{
    memset(&fixture, 0, sizeof(fixture));
    if (ready_pm) {
        REQUIRE(bootstrap_control_test_setup_ready_pm_fixture(
            &fixture.state, &fixture.manifest, &fixture.registry, &fixture.objects, &fixture.hart,
            fixture.bindings
        ));
    } else {
        REQUIRE(bootstrap_control_test_setup_control_fixture(
            &fixture.state, &fixture.manifest, &fixture.registry, &fixture.objects, &fixture.hart,
            fixture.bindings, false
        ));
    }
    snapshot_control();
    return true;
}

static bool setup_runtime(size_t count)
{
    const uint32_t ids[] = {63, 1, 7, 15, 23, 31, 47};

    memset(&fixture, 0, sizeof(fixture));
    fixture.manifest.header.entry_count = (uint16_t)count;
    fixture.manifest_plan.entry_count = (uint16_t)count;
    fixture.manifest_plan.controller_service_id = 63;
    for (size_t logical = 0; logical < count; ++logical) {
        size_t slot = logical == 0 ? 0 : logical == 1 ? count - 1 : logical - 1;
        struct micros_bootstrap_manifest_entry *entry = &fixture.manifest.entries[slot];

        entry->service_id = ids[logical];
        entry->prerequisites = logical == 0 ? 0 : UINT64_C(1) << 62;
        entry->ready_timeout_counter_ticks = logical == 0 ? 0 : 100;
        fixture.manifest_plan.ordered_service_ids[logical] = ids[logical];
        fixture.manifest_plan.ordered_manifest_indices[logical] = (uint16_t)slot;
    }
    REQUIRE(micros_bootstrap_runtime_initialize(
        &fixture.manifest, &fixture.manifest_plan, &fixture.runtime
    ) == MICROS_BOOTSTRAP_OK);
    return true;
}

static bool binding_case(size_t index)
{
    const struct micros_bootstrap_binding *binding;
    uint32_t id = index == 0 ? 2 : index == 1 ? 3 : index == 2 ? 63 : 64;

    REQUIRE(setup_control(index != 0));
    REQUIRE(begin(index));
    binding = micros_bootstrap_control_find_binding(&fixture.state, id);
    REQUIRE(finish());
    if (index < 2) {
        REQUIRE(binding != NULL && binding->service_id == id);
        REQUIRE(memcmp(binding, &fixture.bindings[index == 0 ? 0 : 2], sizeof(*binding)) == 0);
    } else {
        REQUIRE(binding == NULL);
    }
    REQUIRE(memcmp(&fixture.registry, &fixture.before_registry, sizeof(fixture.registry)) == 0);
    REQUIRE(memcmp(&fixture.objects, &fixture.before_objects, sizeof(fixture.objects)) == 0);
    return report(index, MICROS_BOOTSTRAP_OK, binding == NULL ? 0 : id, true,
                  &fixture.before_state, &fixture.state, sizeof(fixture.state));
}

static bool runtime_case(size_t index)
{
    size_t count = index == 6 ? 2 : 7;
    enum micros_bootstrap_error result = MICROS_BOOTSTRAP_OK;
    enum micros_bootstrap_error expected = index == 10 ? MICROS_BOOTSTRAP_ERROR_ARGUMENT
        : index == 11 ? MICROS_BOOTSTRAP_ERROR_STATE : MICROS_BOOTSTRAP_OK;
    uint64_t value = 0;
    bool preserved = index < 6 || index >= 10;

    REQUIRE(setup_runtime(count));
    if (index == 8 || index == 11) {
        REQUIRE(micros_bootstrap_runtime_release(&fixture.runtime, 1, 10) == MICROS_BOOTSTRAP_OK);
    } else if (index == 9) {
        for (size_t order = 1; order < count; ++order) {
            uint32_t id = fixture.manifest_plan.ordered_service_ids[order];

            REQUIRE(micros_bootstrap_runtime_release(&fixture.runtime, id, order * 200) == MICROS_BOOTSTRAP_OK);
            REQUIRE(micros_bootstrap_runtime_accept_ready(&fixture.runtime, id, order * 200 + 1) == MICROS_BOOTSTRAP_OK);
        }
    }
    fixture.before_runtime = fixture.expected_runtime = fixture.runtime;
    if (index == 6 || index == 7) {
        struct micros_bootstrap_runtime_entry *entry = &fixture.expected_runtime.entries[count - 1];

        entry->state = MICROS_BOOTSTRAP_SERVICE_STARTING;
        entry->endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
        entry->profile_installed = entry->scheduler_assigned = true;
        entry->ready_deadline = 110;
        fixture.expected_runtime.starting_service_id = 1;
    } else if (index == 8) {
        fixture.expected_runtime.entries[count - 1].state = MICROS_BOOTSTRAP_SERVICE_READY;
        fixture.expected_runtime.entries[count - 1].ready_deadline = 0;
        fixture.expected_runtime.starting_service_id = 0;
        fixture.expected_runtime.next_order_index = 2;
    } else if (index == 9) {
        fixture.expected_runtime.entries[0].endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY;
        fixture.expected_runtime.entries[0].scheduler_assigned = false;
        fixture.expected_runtime.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
        fixture.expected_runtime.controller_service_id = 0;
    }
    REQUIRE(begin(index));
    if (index < 6) {
        uint32_t id = index == 4 ? 63 : 1;

        value = micros_bootstrap_classify_ready(&fixture.runtime, id, id, true);
    } else if (index == 6 || index == 7 || index == 10) {
        result = micros_bootstrap_runtime_release(&fixture.runtime, index == 10 ? 64 : 1, 10);
        value = result == MICROS_BOOTSTRAP_OK ? fixture.runtime.entries[count - 1].ready_deadline : 0;
    } else if (index == 8 || index == 11) {
        result = micros_bootstrap_runtime_accept_ready(&fixture.runtime, 1, index == 11 ? 110 : 109);
        value = result == MICROS_BOOTSTRAP_OK ? fixture.runtime.next_order_index : 0;
    } else {
        result = micros_bootstrap_runtime_complete(&fixture.runtime);
        value = fixture.runtime.phase;
    }
    REQUIRE(finish());
    REQUIRE(result == expected && (index >= 6 || value == (index == 4 ? 4 : 3)));
    REQUIRE(memcmp(&fixture.runtime, &fixture.expected_runtime, sizeof(fixture.runtime)) == 0);
    return report(index, result, value, preserved, &fixture.before_runtime, &fixture.runtime, sizeof(fixture.runtime));
}

static bool control_case(size_t index)
{
    enum micros_bootstrap_error result = MICROS_BOOTSTRAP_OK;
    uint64_t value = 0;
    bool preserved = index == 13 || index == 15 || index == 16;

    REQUIRE(setup_control(index >= 16));
    if (index >= 13 && index <= 15) {
        REQUIRE(micros_bootstrap_control_release(
            &fixture.state, &fixture.registry, &fixture.objects, fixture.hart, 2, 10, true
        ) == MICROS_BOOTSTRAP_OK);
    }
    memset(&fixture.ready, 0xa5, sizeof(fixture.ready));
    fixture.ready.active = false;
    memset(&fixture.complete, 0xa5, sizeof(fixture.complete));
    fixture.complete.active = false;
    if (index == 14) {
        REQUIRE(micros_bootstrap_control_prepare_ready(
            &fixture.state, &fixture.registry, &fixture.objects, 2, fixture.bindings[0].endpoint, 109, true,
            &fixture.ready
        ) == MICROS_BOOTSTRAP_OK);
    } else if (index == 17) {
        REQUIRE(micros_bootstrap_control_prepare_complete(
            &fixture.state, &fixture.registry, &fixture.objects, NULL, &fixture.complete
        ) == MICROS_BOOTSTRAP_OK);
    }
    fixture.before_ready = fixture.ready;
    fixture.before_complete = fixture.complete;
    snapshot_control();
    if (index == 12) {
        fixture.expected_state.transitions.entries[0].state = MICROS_BOOTSTRAP_SERVICE_STARTING;
        fixture.expected_state.transitions.entries[0].endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
        fixture.expected_state.transitions.entries[0].profile_installed = true;
        fixture.expected_state.transitions.entries[0].scheduler_assigned = true;
        fixture.expected_state.transitions.entries[0].ready_deadline = 110;
        fixture.expected_state.transitions.starting_service_id = 2;
    } else if (index == 14) {
        fixture.expected_state.transitions.entries[0].state = MICROS_BOOTSTRAP_SERVICE_READY;
        fixture.expected_state.transitions.entries[0].ready_deadline = 0;
        fixture.expected_state.transitions.starting_service_id = 0;
        fixture.expected_state.transitions.next_order_index = 2;
    } else if (index == 17) {
        fixture.expected_state.transitions.entries[1].endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY;
        fixture.expected_state.transitions.entries[1].scheduler_assigned = false;
        fixture.expected_state.transitions.phase = fixture.expected_state.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
        fixture.expected_state.transitions.controller_service_id = 0;
        fixture.expected_state.controller_process = (struct micros_process_handle){0};
        fixture.expected_state.controller_thread = (struct micros_thread_handle){0};
        fixture.expected_state.controller_endpoint = 0;
    }
    REQUIRE(begin(index));
    if (index == 12) {
        result = micros_bootstrap_control_release(
            &fixture.state, &fixture.registry, &fixture.objects, fixture.hart, 2, 10, true
        );
        value = fixture.state.transitions.entries[0].ready_deadline;
    } else if (index == 13 || index == 15) {
        result = micros_bootstrap_control_prepare_ready(
            &fixture.state, &fixture.registry, &fixture.objects, 2, fixture.bindings[index == 15 ? 1 : 0].endpoint,
            109, index != 15, &fixture.ready
        );
        value = result == MICROS_BOOTSTRAP_OK ? fixture.ready.acknowledgment.type : 0;
    } else if (index == 14) {
        micros_bootstrap_control_commit_ready_prevalidated(&fixture.state, &fixture.ready);
        value = fixture.state.transitions.next_order_index;
    } else if (index == 16) {
        result = micros_bootstrap_control_prepare_complete(
            &fixture.state, &fixture.registry, &fixture.objects, NULL, &fixture.complete
        );
        value = fixture.complete.active;
    } else {
        REQUIRE(bootstrap_control_test_commit_complete_for_test(
            &fixture.state, &fixture.registry, &fixture.objects, &fixture.bindings[1], &fixture.complete
        ));
        value = fixture.state.phase;
    }
    REQUIRE(finish());
    REQUIRE(result == (index == 15 ? MICROS_BOOTSTRAP_ERROR_ROLE : MICROS_BOOTSTRAP_OK));
    REQUIRE(memcmp(&fixture.state, &fixture.expected_state, sizeof(fixture.state)) == 0);
    if (preserved || index == 14) {
        REQUIRE(memcmp(&fixture.objects, &fixture.before_objects, sizeof(fixture.objects)) == 0);
        REQUIRE(memcmp(&fixture.registry, &fixture.before_registry, sizeof(fixture.registry)) == 0);
    }
    if (index == 15) {
        REQUIRE(memcmp(&fixture.ready, &fixture.before_ready, sizeof(fixture.ready)) == 0);
    }
    if (index == 14 || index == 17) {
        REQUIRE(index == 14 ? !fixture.ready.active : !fixture.complete.active);
    }
    REQUIRE(micros_bootstrap_control_validate(&fixture.state, &fixture.registry, &fixture.objects) == MICROS_BOOTSTRAP_OK);
    return report(index, result, value, preserved, &fixture.before_state, &fixture.state, sizeof(fixture.state));
}

static bool deadline_case(size_t index)
{
    struct bootstrap_runtime_context *runtime = bootstrap_runtime_context();
    struct micros_tty_handoff_runtime_state *tty = &tty_handoff_context()->tty_handoff_state;
    size_t variant = index - 18;
    bool fatal = variant != 0 && variant != 9 && variant != 10;
    uint32_t service_id = variant == 8 ? MICROS_TTY_SERVICE_ID : variant == 12 ? 64 : 2;
    micros_endpoint_t endpoint;
    char expected_line[512];
    const char *state_name = variant == 1 || variant == 6 ? "starting" : variant == 5 ? "ready" : "none";
    uint64_t detail = variant == 1 ? 110 : variant == 8 ? MICROS_TTY_CONSOLE_MAP_REQUESTED : 0;

    REQUIRE(setup_control(false));
    REQUIRE(micros_bootstrap_control_release(
        &fixture.state, &fixture.registry, &fixture.objects, fixture.hart, 2, 10, true
    ) == MICROS_BOOTSTRAP_OK);
    memset(runtime, 0, sizeof(*runtime));
    memset(tty_handoff_context(), 0, sizeof(*tty_handoff_context()));
    memset(&diagnostic, 0, sizeof(diagnostic));
    runtime->bootstrap_state = fixture.state;
    switch (variant) {
    case 2: runtime->bootstrap_state.transitions.service_slots[2] = 0; break;
    case 3: runtime->bootstrap_state.transitions.service_slots[2] = 255; break;
    case 4: runtime->bootstrap_state.transitions.service_slots[2] = 2; break;
    case 5: runtime->bootstrap_state.transitions.entries[0].state = MICROS_BOOTSTRAP_SERVICE_READY; break;
    case 6: runtime->bootstrap_state.transitions.entries[0].ready_deadline = 0; break;
    case 7:
        runtime->bootstrap_state.transitions.service_slots[2] = 0;
        runtime->bootstrap_state.binding_slots[2] = 0;
        break;
    case 8:
        runtime->bootstrap_state.transitions.service_slots[2] = 0;
        tty->service_id = MICROS_TTY_SERVICE_ID;
        REQUIRE(micros_tty_handoff_initialize(&tty->handoff) == MICROS_TTY_HANDOFF_OK);
        REQUIRE(micros_tty_handoff_begin(&tty->handoff, 10, 20) == MICROS_TTY_HANDOFF_OK);
        break;
    case 9: runtime->bootstrap_state.transitions.starting_service_id = 0; break;
    case 10: runtime->bootstrap_state.phase = MICROS_BOOTSTRAP_PHASE_SEALED; break;
    case 11: runtime->bootstrap_state.transitions.entry_count = 0; break;
    case 12: runtime->bootstrap_state.transitions.starting_service_id = 64; break;
    default: break;
    }
    endpoint = variant == 7 || variant == 8 || variant == 12
        ? MICROS_ENDPOINT_NONE : fixture.bindings[0].endpoint;
    diagnostic.before = diagnostic.expected = *runtime;
    if (fatal) {
        diagnostic.expected.bootstrap_state.phase = MICROS_BOOTSTRAP_PHASE_FAILED;
        diagnostic.expected.bootstrap_state.transitions.phase = MICROS_BOOTSTRAP_PHASE_FAILED;
        diagnostic.expected.failure_record_emitted = true;
    }
    REQUIRE(begin(index));
    diagnostic.armed = true;
    if (setjmp(diagnostic.jump) == 0) {
        micros_bootstrap_runtime_check_deadline(variant == 1 ? 110 : 109);
    }
    diagnostic.armed = false;
    REQUIRE(finish());
    REQUIRE(diagnostic.panicked == fatal);
    REQUIRE(memcmp(runtime, &diagnostic.expected, sizeof(*runtime)) == 0);
    if (fatal) {
        int length = snprintf(
            expected_line, sizeof(expected_line),
            "MICROS_BOOTSTRAP_FAILURE reason=%s service=0x%016" PRIx64 " endpoint=0x%016" PRIx64
            " phase=failed state=%s detail=0x%016" PRIx64 "\n",
            variant == 1 || variant == 8 ? "ready-timeout" : "authority",
            (uint64_t)service_id, (uint64_t)endpoint, state_name, detail
        );

        REQUIRE(length > 0 && (size_t)length < sizeof(expected_line));
        REQUIRE(strcmp(diagnostic.text, expected_line) == 0);
        REQUIRE(diagnostic.seize_count == 1 && diagnostic.flush_count == 1);
    } else {
        REQUIRE(diagnostic.length == 0 && diagnostic.seize_count == 0 && diagnostic.flush_count == 0);
    }
    return report(index, MICROS_BOOTSTRAP_OK, fatal, !fatal,
                  &diagnostic.before, runtime, sizeof(*runtime));
}

static bool expect_release_error(enum micros_bootstrap_error expected)
{
    fixture.expected_runtime = fixture.runtime;
    REQUIRE(micros_bootstrap_runtime_release(&fixture.runtime, 1, 10) == expected);
    REQUIRE(memcmp(&fixture.runtime, &fixture.expected_runtime, sizeof(fixture.runtime)) == 0);
    return true;
}

static void expected_release(size_t slot, uint64_t deadline)
{
    struct micros_bootstrap_runtime_entry *entry;

    fixture.expected_runtime = fixture.before_runtime;
    entry = &fixture.expected_runtime.entries[slot];
    entry->state = MICROS_BOOTSTRAP_SERVICE_STARTING;
    entry->endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
    entry->profile_installed = entry->scheduler_assigned = true;
    entry->ready_deadline = deadline;
    fixture.expected_runtime.starting_service_id = entry->service_id;
}

static bool selected_runtime_case(size_t index)
{
    REQUIRE(setup_runtime(index == 31 ? 1 : 7));
    if (index == 42) {
        fixture.runtime.service_slots[48] = 1;
        REQUIRE(!bootstrap_runtime_validate_index(&fixture.runtime));
    }
    fixture.before_runtime = fixture.runtime;
    REQUIRE(begin(index));
    if (index == 31) {
        fixture.expected_runtime = fixture.runtime;
        fixture.expected_runtime.entries[0].endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY;
        fixture.expected_runtime.entries[0].scheduler_assigned = false;
        fixture.expected_runtime.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
        fixture.expected_runtime.controller_service_id = 0;
        REQUIRE(micros_bootstrap_runtime_complete(&fixture.runtime) == MICROS_BOOTSTRAP_OK);
    } else {
        expected_release(6, 110);
        REQUIRE(micros_bootstrap_runtime_release(&fixture.runtime, 1, 10) == MICROS_BOOTSTRAP_OK);
    }
    REQUIRE(finish());
    REQUIRE(memcmp(&fixture.runtime, &fixture.expected_runtime, sizeof(fixture.runtime)) == 0);
    REQUIRE(bootstrap_runtime_validate_index(&fixture.runtime) == (index == 31));
    return report(index, MICROS_BOOTSTRAP_OK, index == 31 ? 3 : 110, false,
                  &fixture.before_runtime, &fixture.runtime, sizeof(fixture.runtime));
}

static bool indexed_binding_case(size_t index)
{
    REQUIRE(setup_control(false));
    if (index == 32) {
        fixture.state.bindings[0].service_id = 63;
        fixture.state.binding_slots[2] = 0;
        fixture.state.binding_slots[63] = 1;
    }
    snapshot_control();
    REQUIRE(begin(index));
    if (index == 32) {
        REQUIRE(micros_bootstrap_control_find_binding(&fixture.state, 63) == &fixture.state.bindings[0]);
    } else {
        for (size_t variant = 0; variant < 8; ++variant) {
            fixture.state = fixture.before_state;
            switch (variant) {
            case 0: fixture.state.entry_count = 0; break;
            case 1: fixture.state.entry_count = MICROS_BOOTSTRAP_SERVICE_CAPACITY + 1; break;
            case 2: fixture.state.binding_slots[2] = 0; break;
            case 3: fixture.state.binding_slots[2] = 255; break;
            case 4: fixture.state.binding_slots[2] = 2; break;
            case 5: fixture.state.bindings[0].service_id = 0; break;
            case 6: fixture.state.bindings[0].service_id = 64; break;
            case 7: fixture.state.bindings[0].service_id = UINT32_MAX; break;
            }
            fixture.expected_state = fixture.state;
            REQUIRE(micros_bootstrap_control_find_binding(&fixture.state, 2) == NULL);
            REQUIRE(memcmp(&fixture.state, &fixture.expected_state, sizeof(fixture.state)) == 0);
        }
        fixture.state = fixture.before_state;
        REQUIRE(micros_bootstrap_control_find_binding(NULL, 2) == NULL);
        REQUIRE(micros_bootstrap_control_find_binding(&fixture.state, 0) == NULL);
        REQUIRE(micros_bootstrap_control_find_binding(&fixture.state, 64) == NULL);
        REQUIRE(micros_bootstrap_control_find_binding(&fixture.state, UINT32_MAX) == NULL);
    }
    REQUIRE(finish());
    return report(index, MICROS_BOOTSTRAP_OK, index == 32 ? 63 : 1, true,
                  &fixture.before_state, &fixture.state, sizeof(fixture.state));
}

static bool complete_index_diagnosis(size_t index)
{
    REQUIRE(setup_control(false));
    REQUIRE(begin(index));
    for (size_t variant = 0; variant < 8; ++variant) {
        fixture.state = fixture.before_state;
        switch (variant) {
        case 0: fixture.state.binding_slots[0] = 1; break;
        case 1: fixture.state.binding_slots[63] = 1; break;
        case 2: fixture.state.binding_slots[1] = 0; break;
        case 3: fixture.state.bindings[1].service_id = 2; break;
        case 4: fixture.state.transitions.service_slots[63] = 1; break;
        case 5: fixture.state.transitions.service_slots[0] = 1; break;
        case 6: fixture.state.transitions.service_slots[1] = 0; break;
        case 7: fixture.state.transitions.entry_count = 1; break;
        }
        fixture.expected_state = fixture.state;
        REQUIRE(micros_bootstrap_control_validate(
            &fixture.state, &fixture.registry, &fixture.objects
        ) == MICROS_BOOTSTRAP_ERROR_INVARIANT);
        REQUIRE(memcmp(&fixture.state, &fixture.expected_state, sizeof(fixture.state)) == 0);
    }
    fixture.state = fixture.before_state;
    REQUIRE(finish());
    REQUIRE(memcmp(&fixture.registry, &fixture.before_registry, sizeof(fixture.registry)) == 0);
    REQUIRE(memcmp(&fixture.objects, &fixture.before_objects, sizeof(fixture.objects)) == 0);
    return report(index, MICROS_BOOTSTRAP_OK, 1, true,
                  &fixture.before_state, &fixture.state, sizeof(fixture.state));
}

static bool named_dependencies(size_t index)
{
    const uint64_t dependencies = (UINT64_C(1) << 62) | UINT64_C(1) | (UINT64_C(1) << 6)
        | (UINT64_C(1) << 14) | (UINT64_C(1) << 22) | (UINT64_C(1) << 30);

    REQUIRE(setup_runtime(7));
    for (size_t order = 1; order < 6; ++order) {
        uint32_t id = fixture.manifest_plan.ordered_service_ids[order];

        REQUIRE(micros_bootstrap_runtime_release(&fixture.runtime, id, 0) == MICROS_BOOTSTRAP_OK);
        REQUIRE(micros_bootstrap_runtime_accept_ready(&fixture.runtime, id, 1) == MICROS_BOOTSTRAP_OK);
    }
    fixture.runtime.entries[5].prerequisites = dependencies;
    fixture.before_runtime = fixture.runtime;
    REQUIRE(begin(index));
    for (size_t variant = 0; variant < 4; ++variant) {
        fixture.runtime = fixture.before_runtime;
        switch (variant) {
        case 0: fixture.runtime.entries[5].prerequisites |= UINT64_C(1) << 63; break;
        case 1: fixture.runtime.entries[5].prerequisites = UINT64_C(1) << 1; break;
        case 2: fixture.runtime.entries[6].state = MICROS_BOOTSTRAP_SERVICE_PREPARED; break;
        case 3: fixture.runtime.service_slots[63] = 255; break;
        }
        fixture.expected_runtime = fixture.runtime;
        REQUIRE(micros_bootstrap_runtime_release(&fixture.runtime, 47, 10)
                == (variant == 3 ? MICROS_BOOTSTRAP_ERROR_INVARIANT : MICROS_BOOTSTRAP_ERROR_STATE));
        REQUIRE(memcmp(&fixture.runtime, &fixture.expected_runtime, sizeof(fixture.runtime)) == 0);
    }
    fixture.runtime = fixture.before_runtime;
    expected_release(5, 110);
    REQUIRE(micros_bootstrap_runtime_release(&fixture.runtime, 47, 10) == MICROS_BOOTSTRAP_OK);
    REQUIRE(finish());
    REQUIRE(memcmp(&fixture.runtime, &fixture.expected_runtime, sizeof(fixture.runtime)) == 0);
    return report(index, MICROS_BOOTSTRAP_OK, 110, false,
                  &fixture.before_runtime, &fixture.runtime, sizeof(fixture.runtime));
}

static bool selected_corruption(size_t index)
{
    REQUIRE(setup_runtime(7));
    fixture.before_runtime = fixture.runtime;
    REQUIRE(begin(index));
    for (size_t variant = 0; variant < 6; ++variant) {
        fixture.runtime = fixture.before_runtime;
        switch (variant) {
        case 0: fixture.runtime.entry_count = 0; break;
        case 1: fixture.runtime.entry_count = MICROS_BOOTSTRAP_SERVICE_CAPACITY + 1; break;
        case 2: fixture.runtime.service_slots[1] = 0; break;
        case 3: fixture.runtime.service_slots[1] = 255; break;
        case 4: fixture.runtime.service_slots[1] = 1; break;
        case 5: fixture.runtime.entries[6].service_id = 64; break;
        }
        REQUIRE(expect_release_error(variant == 2 ? MICROS_BOOTSTRAP_ERROR_ARGUMENT : MICROS_BOOTSTRAP_ERROR_INVARIANT));
        REQUIRE(micros_bootstrap_classify_ready(&fixture.runtime, 1, 1, true) == MICROS_BOOTSTRAP_READY_FOREIGN);
    }
    fixture.runtime = fixture.before_runtime;
    REQUIRE(micros_bootstrap_runtime_release(&fixture.runtime, UINT32_MAX, 0) == MICROS_BOOTSTRAP_ERROR_ARGUMENT);
    REQUIRE(micros_bootstrap_classify_ready(NULL, 0, 0, false) == MICROS_BOOTSTRAP_READY_MALFORMED);
    REQUIRE(finish());
    return report(index, MICROS_BOOTSTRAP_OK, 1, true,
                  &fixture.before_runtime, &fixture.runtime, sizeof(fixture.runtime));
}

static bool ready_counter_precedence(size_t index)
{
    const uint16_t counters[] = {0, 7, UINT16_MAX};

    REQUIRE(setup_runtime(7));
    REQUIRE(micros_bootstrap_runtime_release(&fixture.runtime, 1, 10) == MICROS_BOOTSTRAP_OK);
    fixture.before_runtime = fixture.runtime;
    REQUIRE(begin(index));
    for (size_t variant = 0; variant < sizeof(counters) / sizeof(counters[0]); ++variant) {
        fixture.runtime.next_order_index = counters[variant];
        fixture.expected_runtime = fixture.runtime;
        REQUIRE(micros_bootstrap_runtime_accept_ready(&fixture.runtime, 1, 109) == MICROS_BOOTSTRAP_ERROR_INVARIANT);
        REQUIRE(memcmp(&fixture.runtime, &fixture.expected_runtime, sizeof(fixture.runtime)) == 0);
    }
    fixture.runtime.next_order_index = 0;
    fixture.expected_runtime = fixture.runtime;
    REQUIRE(micros_bootstrap_runtime_accept_ready(&fixture.runtime, 1, 110) == MICROS_BOOTSTRAP_ERROR_STATE);
    REQUIRE(memcmp(&fixture.runtime, &fixture.expected_runtime, sizeof(fixture.runtime)) == 0);
    fixture.runtime.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    fixture.expected_runtime = fixture.runtime;
    REQUIRE(micros_bootstrap_runtime_release(&fixture.runtime, 64, 0) == MICROS_BOOTSTRAP_ERROR_STATE);
    REQUIRE(micros_bootstrap_runtime_accept_ready(&fixture.runtime, 64, 0) == MICROS_BOOTSTRAP_ERROR_STATE);
    REQUIRE(micros_bootstrap_runtime_release(&fixture.runtime, 0, 0) == MICROS_BOOTSTRAP_ERROR_ARGUMENT);
    REQUIRE(micros_bootstrap_classify_ready(&fixture.runtime, 63, 63, true) == MICROS_BOOTSTRAP_READY_DUPLICATE);
    REQUIRE(memcmp(&fixture.runtime, &fixture.expected_runtime, sizeof(fixture.runtime)) == 0);
    fixture.runtime = fixture.before_runtime;
    REQUIRE(finish());
    return report(index, MICROS_BOOTSTRAP_OK, 1, true,
                  &fixture.before_runtime, &fixture.runtime, sizeof(fixture.runtime));
}

static bool selected_plan_canaries(size_t index)
{
    struct {
        uint64_t prefix;
        struct bootstrap_transition_plan plan;
        uint64_t suffix;
    } guarded, snapshot;
    struct bootstrap_transition_plan expected;

    REQUIRE(setup_runtime(7));
    fixture.before_runtime = fixture.runtime;
    memset(&guarded, 0xa5, sizeof(guarded));
    memset(&expected, 0, sizeof(expected));
    expected.entry_index = 6;
    expected.next_order_index = 1;
    expected.phase = MICROS_BOOTSTRAP_PHASE_RUNNING;
    expected.controller_service_id = 63;
    expected.starting_service_id = 1;
    expected.entry = fixture.runtime.entries[6];
    expected.entry.profile_installed = expected.entry.scheduler_assigned = true;
    expected.entry.endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
    expected.entry.state = MICROS_BOOTSTRAP_SERVICE_STARTING;
    expected.entry.ready_deadline = UINT64_MAX;
    REQUIRE(begin(index));
    REQUIRE(bootstrap_runtime_prepare_release(
        &fixture.runtime, 1, UINT64_MAX - 1, UINT64_MAX, true, &guarded.plan
    ) == MICROS_BOOTSTRAP_OK);
    REQUIRE(guarded.prefix == UINT64_C(0xa5a5a5a5a5a5a5a5) && guarded.suffix == guarded.prefix);
    REQUIRE(memcmp(&guarded.plan, &expected, sizeof(expected)) == 0);
    snapshot = guarded;
    REQUIRE(bootstrap_runtime_prepare_release(
        &fixture.runtime, 1, UINT64_MAX - 1, 0, false, &guarded.plan
    ) == MICROS_BOOTSTRAP_ERROR_RANGE);
    REQUIRE(memcmp(&guarded, &snapshot, sizeof(guarded)) == 0);
    REQUIRE(finish());
    return report(index, MICROS_BOOTSTRAP_OK, 1, true,
                  &fixture.before_runtime, &fixture.runtime, sizeof(fixture.runtime));
}

static bool constructor_rejections(size_t index)
{
    struct micros_bootstrap_manifest manifest;
    struct micros_bootstrap_manifest_plan plan;

    REQUIRE(setup_runtime(7));
    manifest = fixture.manifest;
    plan = fixture.manifest_plan;
    memset(&fixture.runtime, 0, sizeof(fixture.runtime));
    fixture.before_runtime = fixture.runtime;
    REQUIRE(begin(index));
    for (size_t variant = 0; variant < 10; ++variant) {
        fixture.manifest = manifest;
        fixture.manifest_plan = plan;
        switch (variant) {
        case 0: fixture.manifest_plan.entry_count = fixture.manifest.header.entry_count = 0; break;
        case 1: fixture.manifest_plan.entry_count = fixture.manifest.header.entry_count = 8; break;
        case 2: fixture.manifest.entries[0].service_id = 0; break;
        case 3: fixture.manifest.entries[0].service_id = 64; break;
        case 4: fixture.manifest.entries[0].service_id = UINT32_MAX; break;
        case 5: fixture.manifest.entries[1].service_id = 63; break;
        case 6: fixture.manifest_plan.ordered_service_ids[6] = 2; break;
        case 7: fixture.manifest_plan.ordered_service_ids[6] = 63; break;
        case 8: fixture.manifest_plan.ordered_service_ids[0] = 1; break;
        case 9:
            fixture.manifest_plan.controller_service_id = fixture.manifest_plan.ordered_service_ids[0] = 64;
            break;
        }
        REQUIRE(micros_bootstrap_runtime_initialize(
            &fixture.manifest, &fixture.manifest_plan, &fixture.runtime
        ) == MICROS_BOOTSTRAP_ERROR_INVARIANT);
        REQUIRE(memcmp(&fixture.runtime, &fixture.before_runtime, sizeof(fixture.runtime)) == 0);
    }
    REQUIRE(finish());
    return report(index, MICROS_BOOTSTRAP_OK, 1, true,
                  &fixture.before_runtime, &fixture.runtime, sizeof(fixture.runtime));
}

static bool preparing_permutation(size_t index)
{
    struct micros_bootstrap_binding binding;
    const struct micros_bootstrap_runtime zero = {0};

    REQUIRE(setup_control(false));
    fixture.manifest_plan = fixture.state.plan;
    binding = fixture.bindings[0];
    fixture.bindings[0] = fixture.bindings[1];
    fixture.bindings[1] = binding;
    memset(&fixture.state, 0, sizeof(fixture.state));
    snapshot_control();
    fixture.expected_state.phase = MICROS_BOOTSTRAP_PHASE_PREPARING;
    fixture.expected_state.entry_count = 2;
    fixture.expected_state.manifest = fixture.manifest;
    fixture.expected_state.plan = fixture.manifest_plan;
    memcpy(fixture.expected_state.bindings, fixture.bindings, 2 * sizeof(binding));
    fixture.expected_state.binding_slots[1] = 1;
    fixture.expected_state.binding_slots[2] = 2;
    REQUIRE(begin(index));
    REQUIRE(micros_bootstrap_control_state_prepare(
        &fixture.state, &fixture.manifest, &fixture.manifest_plan, fixture.bindings, 2
    ) == MICROS_BOOTSTRAP_OK);
    REQUIRE(finish());
    REQUIRE(memcmp(&fixture.state, &fixture.expected_state, sizeof(fixture.state)) == 0);
    REQUIRE(memcmp(&fixture.state.transitions, &zero, sizeof(zero)) == 0);
    REQUIRE(micros_bootstrap_control_find_binding(&fixture.state, 2) == &fixture.state.bindings[1]);
    return report(index, MICROS_BOOTSTRAP_OK, 1, false,
                  &fixture.before_state, &fixture.state, sizeof(fixture.state));
}

static bool permuted_control_seal(size_t index)
{
    struct micros_bootstrap_binding controller;

    REQUIRE(setup_control(true));
    controller = fixture.state.bindings[1];
    fixture.state.bindings[1] = fixture.state.bindings[2];
    fixture.state.bindings[2] = controller;
    fixture.state.binding_slots[1] = 3;
    fixture.state.binding_slots[3] = 2;
    REQUIRE(micros_bootstrap_control_validate(&fixture.state, &fixture.registry, &fixture.objects) == MICROS_BOOTSTRAP_OK);
    REQUIRE(micros_bootstrap_control_prepare_complete(
        &fixture.state, &fixture.registry, &fixture.objects, NULL, &fixture.complete
    ) == MICROS_BOOTSTRAP_OK);
    REQUIRE(fixture.complete.controller_binding_index == 2);
    snapshot_control();
    fixture.expected_state.transitions.entries[1].endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_SOURCE_ONLY;
    fixture.expected_state.transitions.entries[1].scheduler_assigned = false;
    fixture.expected_state.transitions.phase = fixture.expected_state.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    fixture.expected_state.transitions.controller_service_id = 0;
    fixture.expected_state.controller_process = (struct micros_process_handle){0};
    fixture.expected_state.controller_thread = (struct micros_thread_handle){0};
    fixture.expected_state.controller_endpoint = 0;
    REQUIRE(begin(index));
    REQUIRE(bootstrap_control_test_commit_complete_for_test(
        &fixture.state, &fixture.registry, &fixture.objects, &fixture.state.bindings[2], &fixture.complete
    ));
    REQUIRE(finish());
    REQUIRE(memcmp(&fixture.state, &fixture.expected_state, sizeof(fixture.state)) == 0);
    REQUIRE(micros_bootstrap_control_find_binding(&fixture.state, 1) == &fixture.state.bindings[2]);
    REQUIRE(micros_bootstrap_control_validate(&fixture.state, &fixture.registry, &fixture.objects) == MICROS_BOOTSTRAP_OK);
    return report(index, MICROS_BOOTSTRAP_OK, 3, false,
                  &fixture.before_state, &fixture.state, sizeof(fixture.state));
}

static bool selected_control_scope_case(size_t index)
{
    enum micros_bootstrap_error result;
    bool preserved = index == 45;

    REQUIRE(setup_control(false));
    if (index == 43) {
        fixture.state.binding_slots[63] = 1;
    } else if (index == 44) {
        fixture.state.transitions.service_slots[63] = 1;
    } else {
        fixture.state.transitions.entries[0].ready_timeout_counter_ticks = 0;
    }
    if (!preserved) {
        REQUIRE(micros_bootstrap_control_validate(&fixture.state, &fixture.registry, &fixture.objects)
                == MICROS_BOOTSTRAP_ERROR_INVARIANT);
    }
    snapshot_control();
    if (!preserved) {
        fixture.expected_state.transitions.entries[0].state = MICROS_BOOTSTRAP_SERVICE_STARTING;
        fixture.expected_state.transitions.entries[0].endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
        fixture.expected_state.transitions.entries[0].profile_installed = true;
        fixture.expected_state.transitions.entries[0].scheduler_assigned = true;
        fixture.expected_state.transitions.entries[0].ready_deadline = 110;
        fixture.expected_state.transitions.starting_service_id = 2;
    }
    REQUIRE(begin(index));
    result = micros_bootstrap_control_release(
        &fixture.state, &fixture.registry, &fixture.objects, fixture.hart, 2, 10, true
    );
    REQUIRE(finish());
    REQUIRE(result == (preserved ? MICROS_BOOTSTRAP_ERROR_INVARIANT : MICROS_BOOTSTRAP_OK));
    REQUIRE(memcmp(&fixture.state, &fixture.expected_state, sizeof(fixture.state)) == 0);
    if (preserved) {
        REQUIRE(memcmp(&fixture.registry, &fixture.before_registry, sizeof(fixture.registry)) == 0);
        REQUIRE(memcmp(&fixture.objects, &fixture.before_objects, sizeof(fixture.objects)) == 0);
    } else {
        REQUIRE(micros_bootstrap_control_validate(&fixture.state, &fixture.registry, &fixture.objects)
                == MICROS_BOOTSTRAP_ERROR_INVARIANT);
    }
    return report(index, result, preserved ? 0 : 110, preserved,
                  &fixture.before_state, &fixture.state, sizeof(fixture.state));
}

static bool selected_control_view_guards(size_t index)
{
    struct {
        uint64_t prefix;
        struct micros_bootstrap_service_selection view;
        uint64_t suffix;
    } guarded, sentinel;

    REQUIRE(setup_control(false));
    snapshot_control();
    memset(&sentinel, 0xa5, sizeof(sentinel));
    REQUIRE(begin(index));
    for (unsigned int variant = 0; variant < 15; ++variant) {
        uint32_t service_id = 2;
        enum micros_bootstrap_error expected = MICROS_BOOTSTRAP_ERROR_INVARIANT;

        fixture.state = fixture.before_state;
        guarded = sentinel;
        switch (variant) {
        case 0: expected = MICROS_BOOTSTRAP_OK; break;
        case 1: service_id = 63; expected = MICROS_BOOTSTRAP_ERROR_ARGUMENT; break;
        case 2: fixture.state.binding_slots[2] = 0; break;
        case 3: fixture.state.transitions.service_slots[2] = 0; break;
        case 4: fixture.state.bindings[0].manifest_index = MICROS_BOOTSTRAP_SERVICE_CAPACITY; break;
        case 5: ++fixture.state.bindings[0].process.generation; break;
        case 6: ++fixture.state.bindings[0].thread.generation; break;
        case 7: ++fixture.state.transitions.entries[0].ready_timeout_counter_ticks; break;
        case 8: fixture.state.manifest.header.entry_count = 0; break;
        case 9: service_id = 0; expected = MICROS_BOOTSTRAP_ERROR_ARGUMENT; break;
        case 10: fixture.state.binding_slots[2] = UINT8_MAX; break;
        case 11: fixture.state.transitions.service_slots[2] = UINT8_MAX; break;
        case 12: fixture.state.transitions.entries[0].service_id = 3; break;
        case 13:
            fixture.state.manifest.entries[0].role_flags =
                MICROS_BOOTSTRAP_ROLE_VM | MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER;
            break;
        case 14: fixture.state.transitions.starting_service_id = 2; break;
        }
        fixture.expected_state = fixture.state;
        REQUIRE(micros_bootstrap_control_select_service(
            &fixture.state, &fixture.registry, &fixture.objects, service_id, &guarded.view
        ) == expected);
        REQUIRE(memcmp(&fixture.state, &fixture.expected_state, sizeof(fixture.state)) == 0);
        if (expected == MICROS_BOOTSTRAP_OK) {
            const struct micros_bootstrap_binding *binding = &fixture.state.bindings[0];

            REQUIRE(guarded.prefix == sentinel.prefix && guarded.suffix == sentinel.suffix);
            REQUIRE(guarded.view.binding == binding);
            REQUIRE(guarded.view.manifest == &fixture.state.manifest.entries[0]);
            REQUIRE(guarded.view.runtime == &fixture.state.transitions.entries[0]);
            REQUIRE(guarded.view.process == &fixture.objects.processes[binding->process.slot]);
            REQUIRE(guarded.view.thread == &fixture.objects.threads[binding->thread.slot]);
            REQUIRE(guarded.view.endpoint == &fixture.registry.endpoints[binding->process.slot]);
            REQUIRE(guarded.view.binding_index == 0 && guarded.view.runtime_index == 0);
        } else {
            REQUIRE(memcmp(&guarded, &sentinel, sizeof(guarded)) == 0);
        }
    }
    fixture.state = fixture.before_state;
    REQUIRE(finish());
    REQUIRE(memcmp(&fixture.registry, &fixture.before_registry, sizeof(fixture.registry)) == 0);
    REQUIRE(memcmp(&fixture.objects, &fixture.before_objects, sizeof(fixture.objects)) == 0);
    return report(index, MICROS_BOOTSTRAP_OK, 1, true,
                  &fixture.before_state, &fixture.state, sizeof(fixture.state));
}

static bool complete_membership_guards(size_t index)
{
    struct {
        uint64_t prefix;
        struct micros_bootstrap_complete_plan plan;
        uint64_t suffix;
    } guarded, sentinel;

    REQUIRE(setup_control(true));
    snapshot_control();
    memset(&sentinel, 0xa5, sizeof(sentinel));
    sentinel.plan.active = false;
    REQUIRE(begin(index));
    for (unsigned int variant = 0; variant < 5; ++variant) {
        fixture.state = fixture.before_state;
        guarded = sentinel;
        switch (variant) {
        case 0: fixture.state.manifest.entries[2].service_id = 2; break;
        case 1: fixture.state.manifest.entries[0].service_id = 3; break;
        case 2: fixture.state.transitions.entries[2].service_id = 2; break;
        case 3: fixture.state.bindings[2].service_id = 2; break;
        case 4: ++fixture.state.bindings[2].thread.generation; break;
        }
        fixture.expected_state = fixture.state;
        REQUIRE(micros_bootstrap_control_prepare_complete(
            &fixture.state, &fixture.registry, &fixture.objects, NULL, &guarded.plan
        ) == MICROS_BOOTSTRAP_ERROR_INVARIANT);
        REQUIRE(memcmp(&guarded, &sentinel, sizeof(guarded)) == 0);
        REQUIRE(memcmp(&fixture.state, &fixture.expected_state, sizeof(fixture.state)) == 0);
    }
    fixture.state = fixture.before_state;
    REQUIRE(finish());
    REQUIRE(memcmp(&fixture.registry, &fixture.before_registry, sizeof(fixture.registry)) == 0);
    REQUIRE(memcmp(&fixture.objects, &fixture.before_objects, sizeof(fixture.objects)) == 0);
    return report(index, MICROS_BOOTSTRAP_OK, 1, true,
                  &fixture.before_state, &fixture.state, sizeof(fixture.state));
}

static bool runtime_transfer_case(size_t index)
{
    const size_t sizes[] = {
        0, 1, 7, 8, 11, 12, 13, 14, 24, 48, 56, 64, 68, 128, 136, 152,
        200, 264, 392, 448, 1408, 2416, 4096,
    };
    uint64_t value = 0;

    for (size_t byte = 0; byte < sizeof(movement.input); ++byte) {
        movement.input[byte] = (unsigned char)(byte * 29 + 7);
    }
    memset(movement.before, 0xa5, sizeof(movement.before));
    REQUIRE(begin(index));
    if (index == 48) {
        bootstrap_runtime_copy_bytes(NULL, NULL, 0);
        bootstrap_runtime_clear_bytes(NULL, 0);
        bootstrap_manifest_copy_bytes(NULL, NULL, 0);
        bootstrap_manifest_clear_bytes(NULL, 0);
        for (size_t row = 0; row < sizeof(sizes) / sizeof(sizes[0]); ++row) {
            size_t size = sizes[row];

            memset(movement.output, 0xa5, sizeof(movement.output));
            memset(movement.expected, 0xa5, sizeof(movement.expected));
            memcpy(movement.expected + 16, movement.input + 16, size);
            bootstrap_runtime_copy_bytes(movement.output + 16, movement.input + 16, size);
            REQUIRE(memcmp(movement.output, movement.expected, sizeof(movement.output)) == 0);
            memset(movement.expected + 16, 0, size);
            bootstrap_runtime_clear_bytes(movement.output + 16, size);
            REQUIRE(memcmp(movement.output, movement.expected, sizeof(movement.output)) == 0);
            value += size;
        }
    } else {
        for (size_t row = 0; row < 2; ++row) {
            size_t target = index == 49 ? (row == 0 ? 19 : 17) : (row == 0 ? 17 : 19);
            size_t source = row == 0 ? 19 : 16;
            size_t size = index == 49 ? 65 : (row == 0 ? 13 : 11);

            memset(movement.output, 0xa5, sizeof(movement.output));
            memset(movement.expected, 0xa5, sizeof(movement.expected));
            if (index == 49) {
                memcpy(movement.expected + target, movement.input + source, size);
                bootstrap_runtime_copy_bytes(movement.output + target, movement.input + source, size);
            } else {
                memset(movement.expected + target, 0, size);
                bootstrap_runtime_clear_bytes(movement.output + target, size);
            }
            REQUIRE(memcmp(movement.output, movement.expected, sizeof(movement.output)) == 0);
            value += size;
        }
    }
    REQUIRE(finish());
    for (size_t byte = 0; byte < sizeof(movement.input); ++byte) {
        REQUIRE(movement.input[byte] == (unsigned char)(byte * 29 + 7));
    }
    return report(index, MICROS_BOOTSTRAP_OK, value, false,
                  movement.before, movement.output, sizeof(movement.output));
}

static bool image_transfer_case(size_t index)
{
    fixture.before_state = fixture.state;
    REQUIRE(begin(index));
    REQUIRE(test_image_catalog());
    REQUIRE(finish());
    return report(index, MICROS_BOOTSTRAP_OK, 1, true,
                  &fixture.before_state, &fixture.state, sizeof(fixture.state));
}

static bool boundary_case(size_t index)
{
    switch (index) {
    case 31:
    case 42: return selected_runtime_case(index);
    case 32:
    case 33: return indexed_binding_case(index);
    case 34: return complete_index_diagnosis(index);
    case 35: return named_dependencies(index);
    case 36: return selected_corruption(index);
    case 37: return ready_counter_precedence(index);
    case 38: return selected_plan_canaries(index);
    case 39: return constructor_rejections(index);
    case 40: return preparing_permutation(index);
    case 41: return permuted_control_seal(index);
    case 43:
    case 44:
    case 45: return selected_control_scope_case(index);
    case 46: return selected_control_view_guards(index);
    case 47: return complete_membership_guards(index);
    case 48:
    case 49:
    case 50: return runtime_transfer_case(index);
    case 51: return image_transfer_case(index);
    default: return false;
    }
}

int main(int argc, char **argv)
{
#if defined(MICROS_INDEXED_BOOTSTRAP_LAUNCHER)
    if (argc == 2) {
        const char *launcher = getenv("__PYVENV_LAUNCHER__");
        char *const arguments[] = {
            MICROS_INDEXED_BOOTSTRAP_PYTHON, "-E", "-S", "-B", MICROS_INDEXED_BOOTSTRAP_DRIVER,
            "--executable", argv[1], "--executable", argv[0], NULL,
        };

        if (launcher != NULL && launcher[0] != '\0') {
            fputs("__PYVENV_LAUNCHER__ is unsupported\n", stderr);
            return 2;
        }
        if (chdir(MICROS_INDEXED_BOOTSTRAP_ROOT) != 0) {
            return 2;
        }
        execv(MICROS_INDEXED_BOOTSTRAP_PYTHON, arguments);
        perror("BOOT01 launcher");
        return 2;
    }
#endif
    if (argc != 3 || strcmp(argv[1], "--run") != 0
#if defined(MICROS_TEST_WORK_COUNTS)
        || strcmp(argv[2], "work") != 0
#else
        || strcmp(argv[2], "ordinary") != 0
#endif
    ) {
        return 2;
    }
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (!(index < 4 ? binding_case(index) : index < 12 ? runtime_case(index)
              : index < 18 ? control_case(index) : index < 31 ? deadline_case(index) : boundary_case(index))) {
            return 1;
        }
    }
    printf("MICROS_BOOT01_NATIVE_PASS cases=%zu\n", sizeof(names) / sizeof(names[0]));
    return 0;
}
