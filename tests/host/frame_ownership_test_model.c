#include "frame_ownership_test_internal.h"

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
        enum micros_frame_ownership_error actual_error = (expression); \
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
static uint32_t next_random(uint32_t *state)
{
    *state = (*state * UINT32_C(1664525)) + UINT32_C(1013904223);
    return *state;
}

static struct micros_frame_owner model_owner_from_selector(
    uint32_t selector
)
{
    switch (selector % 4) {
    case 0:
        return frame_ownership_test_raw_kernel_owner(MICROS_FRAME_OWNER_KERNEL_TEMPORARY);
    case 1:
        return frame_ownership_test_raw_kernel_owner(MICROS_FRAME_OWNER_KERNEL_RETAINED);
    case 2:
        return frame_ownership_test_raw_process_owner(
            MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
            1,
            2
        );
    default:
        return frame_ownership_test_raw_process_owner(
            MICROS_FRAME_OWNER_PROCESS_USER,
            2,
            3
        );
    }
}

static bool model_owner_is_free(struct micros_frame_owner owner)
{
    return owner.kind == MICROS_FRAME_OWNER_FREE;
}

static struct micros_frame_owner model_different_valid_owner(
    struct micros_frame_owner owner
)
{
    if (
        owner.kind == MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
        || owner.kind == MICROS_FRAME_OWNER_PROCESS_USER
        || owner.kind == MICROS_FRAME_OWNER_VM_WIRED
    ) {
        owner.generation = owner.generation
                == MICROS_PROCESS_GENERATION_MAX
            ? owner.generation - UINT32_C(1)
            : owner.generation + UINT32_C(1);
        return owner;
    }
    if (owner.kind == MICROS_FRAME_OWNER_KERNEL_RETAINED) {
        return frame_ownership_test_raw_kernel_owner(MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE);
    }
    return frame_ownership_test_raw_kernel_owner(MICROS_FRAME_OWNER_KERNEL_RETAINED);
}

static bool model_plan_is_valid(
    struct micros_frame_owner owner,
    uint8_t target
)
{
    if (target == MICROS_FRAME_HANDOFF_NONE) {
        return true;
    }
    if (owner.kind == MICROS_FRAME_OWNER_PROCESS_USER) {
        return (
            target == MICROS_FRAME_HANDOFF_VM_WIRED
            || target == MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        );
    }
    return (
        owner.kind == MICROS_FRAME_OWNER_KERNEL_RETAINED
        && target == MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
    );
}

static enum micros_frame_ownership_error
model_prepare_wired_set_error(
    const struct micros_frame_owner *model_owners,
    const uint8_t *model_targets,
    enum micros_frame_ownership_phase model_phase,
    size_t frame_count,
    const uint64_t *selected_bitmap
)
{
    size_t index;

    if (model_phase == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF) {
        return MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    }
    for (
        index = 0;
        index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES;
        ++index
    ) {
        bool selected = (
            selected_bitmap[index / 64]
            & (UINT64_C(1) << (index % 64))
        ) != 0;

        if (index >= frame_count) {
            if (selected) {
                return MICROS_FRAME_OWNERSHIP_ERROR_UNMANAGED;
            }
            continue;
        }
        if (
            model_owners[index].kind
                == MICROS_FRAME_OWNER_PROCESS_USER
        ) {
            if (!selected) {
                return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
            }
            if (
                model_targets[index]
                    != MICROS_FRAME_HANDOFF_NONE
            ) {
                return MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION;
            }
        } else if (selected) {
            return model_owner_is_free(model_owners[index])
                ? MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED
                : MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
        }
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}

static void model_apply_wired_set(
    const struct micros_frame_owner *model_owners,
    uint8_t *model_targets,
    size_t frame_count
)
{
    size_t index;

    for (index = 0; index < frame_count; ++index) {
        if (
            model_owners[index].kind
                == MICROS_FRAME_OWNER_PROCESS_USER
        ) {
            model_targets[index] =
                MICROS_FRAME_HANDOFF_VM_WIRED;
        }
    }
}

static bool model_handoff_is_ready(
    const struct micros_frame_owner *model_owners,
    const uint8_t *model_targets,
    size_t frame_count
)
{
    size_t index;

    for (index = 0; index < frame_count; ++index) {
        if (
            model_owners[index].kind
                == MICROS_FRAME_OWNER_KERNEL_TEMPORARY
            || (
                model_owners[index].kind
                    == MICROS_FRAME_OWNER_PROCESS_USER
                && model_targets[index]
                    == MICROS_FRAME_HANDOFF_NONE
            )
            || !model_plan_is_valid(
                model_owners[index],
                model_targets[index]
            )
        ) {
            return false;
        }
    }
    return true;
}

static void model_apply_handoff(
    struct micros_frame_owner *model_owners,
    uint8_t *model_targets,
    size_t frame_count
)
{
    size_t index;

    for (index = 0; index < frame_count; ++index) {
        if (
            model_targets[index]
                == MICROS_FRAME_HANDOFF_VM_WIRED
        ) {
            model_owners[index].kind = MICROS_FRAME_OWNER_VM_WIRED;
        } else if (
            model_targets[index]
                == MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        ) {
            model_owners[index] = frame_ownership_test_raw_kernel_owner(
                MICROS_FRAME_OWNER_VM_TRANSFERABLE
            );
        }
        model_targets[index] = MICROS_FRAME_HANDOFF_NONE;
    }
}

static bool compare_model(
    struct frame_ownership_test_context *context,
    const struct micros_frame_owner *model_owners,
    const uint8_t *model_targets,
    enum micros_frame_ownership_phase model_phase,
    size_t frame_count,
    size_t step,
    uint32_t seed
)
{
    uint64_t counts[MICROS_FRAME_OWNER_KIND_COUNT] = {0};
    uint64_t owned = 0;
    size_t index;

    if (context->ownership.phase != model_phase) {
        fprintf(
            stderr,
            "seed=0x%08x step=%zu phase mismatch\n",
            seed,
            step
        );
        return false;
    }
    for (index = 0; index < frame_count; ++index) {
        bool allocated =
            (
                context->allocator.allocated_bitmap[index / 64]
                & (UINT64_C(1) << (index % 64))
            ) != 0;

        if (
                !frame_ownership_test_owners_equal(
                    model_owners[index],
                    context->ownership.owners[index]
                )
            || model_targets[index] != context->ownership.handoff_targets[index]
            || allocated == model_owner_is_free(model_owners[index])
            || (
                model_phase
                    == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
                && (
                    model_targets[index]
                        != MICROS_FRAME_HANDOFF_NONE
                    || model_owners[index].kind
                        == MICROS_FRAME_OWNER_KERNEL_TEMPORARY
                    || model_owners[index].kind
                        == MICROS_FRAME_OWNER_PROCESS_USER
                )
            )
        ) {
            fprintf(
                stderr,
                "seed=0x%08x step=%zu frame=%zu model mismatch\n",
                seed,
                step,
                index
            );
            return false;
        }
        ++counts[model_owners[index].kind];
        if (!model_owner_is_free(model_owners[index])) {
            ++owned;
        }
    }
    if (
        context->ownership.owned_frame_count != owned
        || context->allocator.free_frame_count != frame_count - owned
    ) {
        fprintf(
            stderr,
            "seed=0x%08x step=%zu total mismatch\n",
            seed,
            step
        );
        return false;
    }
    for (index = 0; index < MICROS_FRAME_OWNER_KIND_COUNT; ++index) {
        uint64_t expected = index == MICROS_FRAME_OWNER_FREE
            ? 0
            : counts[index];

        if (context->ownership.owner_counts[index] != expected) {
            fprintf(
                stderr,
                "seed=0x%08x step=%zu kind=%zu count mismatch\n",
                seed,
                step,
                index
            );
            return false;
        }
    }
    if (
        (
            step % MODEL_FULL_VALIDATION_INTERVAL == 0
            || step + 1 == MODEL_STEPS
        )
        && micros_frame_ownership_validate(&context->ownership)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        fprintf(
            stderr,
            "seed=0x%08x step=%zu validator mismatch\n",
            seed,
            step
        );
        return false;
    }
    return true;
}

struct ownership_reference_model {
    struct micros_frame_owner owners[TEST_FRAME_COUNT];
    uint8_t targets[TEST_FRAME_COUNT];
    enum micros_frame_ownership_phase phase;
};

static bool force_model_handoff_not_ready(
    struct frame_ownership_test_context *context,
    struct ownership_reference_model *model,
    size_t frame_count
)
{
    const struct micros_frame_owner temporary = frame_ownership_test_raw_kernel_owner(
        MICROS_FRAME_OWNER_KERNEL_TEMPORARY
    );
    size_t index;

    if (model_handoff_is_ready(model->owners, model->targets, frame_count)) {
        uint64_t physical_address = UINT64_MAX;
        size_t free_index = frame_count;

        for (index = 0; index < frame_count; ++index) {
            if (model_owner_is_free(model->owners[index])) {
                free_index = index;
                break;
            }
        }
        if (free_index == frame_count) {
            if (
                micros_frame_ownership_release(
                    &context->ownership,
                    model->owners[0],
                    TEST_BASE
                ) != MICROS_FRAME_OWNERSHIP_OK
            ) {
                return false;
            }
            memset(&model->owners[0], 0, sizeof(model->owners[0]));
            model->targets[0] = MICROS_FRAME_HANDOFF_NONE;
            free_index = 0;
        }
        if (
            micros_frame_ownership_allocate(
                &context->ownership,
                temporary,
                &physical_address
            ) != MICROS_FRAME_OWNERSHIP_OK
            || physical_address
                != TEST_BASE + (free_index * MICROS_FRAME_SIZE)
        ) {
            return false;
        }
        model->owners[free_index] = temporary;
        model->targets[free_index] = MICROS_FRAME_HANDOFF_NONE;
    }
    return true;
}

static bool failed_model_handoff_is_atomic(
    struct frame_ownership_test_context *context,
    const struct ownership_reference_model *model,
    size_t frame_count,
    size_t step,
    uint32_t seed
)
{
    frame_ownership_test_snapshot(context);
    if (
        micros_frame_ownership_complete_handoff(&context->ownership)
            != MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        || !frame_ownership_test_snapshot_matches(context)
        || !compare_model(
            context,
            model->owners,
            model->targets,
            model->phase,
            frame_count,
            step,
            seed
        )
    ) {
        return false;
    }
    return true;
}

static bool prepare_model_handoff(
    struct frame_ownership_test_context *context,
    struct ownership_reference_model *model,
    size_t frame_count
)
{
    size_t index;

    for (index = 0; index < frame_count; ++index) {
        if (
            model->owners[index].kind
                == MICROS_FRAME_OWNER_KERNEL_TEMPORARY
        ) {
            if (
                micros_frame_ownership_release(
                    &context->ownership,
                    model->owners[index],
                    TEST_BASE + (index * MICROS_FRAME_SIZE)
                ) != MICROS_FRAME_OWNERSHIP_OK
            ) {
                return false;
            }
            memset(
                &model->owners[index],
                0,
                sizeof(model->owners[index])
            );
            model->targets[index] = MICROS_FRAME_HANDOFF_NONE;
        } else if (
            model->owners[index].kind
                == MICROS_FRAME_OWNER_PROCESS_USER
            && model->targets[index] == MICROS_FRAME_HANDOFF_NONE
        ) {
            enum micros_frame_handoff_target target =
                index % 2 == 0
                ? MICROS_FRAME_HANDOFF_VM_WIRED
                : MICROS_FRAME_HANDOFF_VM_TRANSFERABLE;

            if (
                micros_frame_ownership_prepare_handoff(
                    &context->ownership,
                    TEST_BASE + (index * MICROS_FRAME_SIZE),
                    model->owners[index],
                    target
                ) != MICROS_FRAME_OWNERSHIP_OK
            ) {
                return false;
            }
            model->targets[index] = (uint8_t)target;
        }
    }
    return true;
}

static bool transition_model_to_handed_off(
    struct frame_ownership_test_context *context,
    struct ownership_reference_model *model,
    size_t frame_count,
    size_t step,
    uint32_t seed
)
{
    if (
        model == NULL
        || model->phase != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        || !force_model_handoff_not_ready(context, model, frame_count)
        || !failed_model_handoff_is_atomic(
            context,
            model,
            frame_count,
            step,
            seed
        )
        || !prepare_model_handoff(context, model, frame_count)
    ) {
        return false;
    }
    if (
        !model_handoff_is_ready(
            model->owners,
            model->targets,
            frame_count
        )
        || micros_frame_ownership_complete_handoff(&context->ownership)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        return false;
    }
    model_apply_handoff(model->owners, model->targets, frame_count);
    model->phase = MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF;
    return compare_model(
        context,
        model->owners,
        model->targets,
        model->phase,
        frame_count,
        step,
        seed
    );
}

static uint32_t select_model_operation(
    enum micros_frame_ownership_phase phase,
    uint32_t value
)
{
    uint32_t operation = value % (
        phase == MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP ? 7U : 8U
    );

    if (
        phase == MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        && operation == 4
        && ((value >> 24) & 7U) != 0
    ) {
        return 5;
    }
    if (
        phase == MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
        && operation == 6
        && ((value >> 24) & 31U) != 0
    ) {
        return 5;
    }
    return operation;
}

static bool run_model_allocation(
    struct frame_ownership_test_context *context,
    struct ownership_reference_model *model,
    uint32_t value,
    uint32_t seed,
    size_t step
)
{
    struct micros_frame_owner owner =
        model_owner_from_selector(value >> 8);
    uint64_t output = UINT64_C(0xdeadbeefdeadbeef);
    size_t expected_index = TEST_FRAME_COUNT;
    enum micros_frame_ownership_error expected_error;
    size_t index;

    for (index = 0; index < TEST_FRAME_COUNT; ++index) {
        if (model_owner_is_free(model->owners[index])) {
            expected_index = index;
            break;
        }
    }
    if (model->phase == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF) {
        expected_error = MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    } else {
        expected_error = expected_index == TEST_FRAME_COUNT
            ? MICROS_FRAME_OWNERSHIP_ERROR_EXHAUSTED
            : MICROS_FRAME_OWNERSHIP_OK;
    }
    if (
        micros_frame_ownership_allocate(
            &context->ownership,
            owner,
            &output
        ) != expected_error
    ) {
        fprintf(
            stderr,
            "seed=0x%08x step=%zu allocation error mismatch\n",
            seed,
            step
        );
        return false;
    }
    if (expected_error == MICROS_FRAME_OWNERSHIP_OK) {
        if (
            output
            != TEST_BASE + (expected_index * MICROS_FRAME_SIZE)
        ) {
            fprintf(
                stderr,
                "seed=0x%08x step=%zu allocation address mismatch\n",
                seed,
                step
            );
            return false;
        }
        model->owners[expected_index] = owner;
        model->targets[expected_index] = MICROS_FRAME_HANDOFF_NONE;
    } else if (output != UINT64_C(0xdeadbeefdeadbeef)) {
        return false;
    }
    return true;
}

static bool run_model_release(
    struct frame_ownership_test_context *context,
    struct ownership_reference_model *model,
    uint32_t value,
    uint32_t seed,
    size_t step
)
{
    size_t index = (value >> 8) % TEST_FRAME_COUNT;
    struct micros_frame_owner expected = model->owners[index];
    bool use_wrong = ((value >> 16) & 1U) != 0;
    enum micros_frame_ownership_error expected_error;

    if (model->phase == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF) {
        expected_error = MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    } else if (model_owner_is_free(expected)) {
        expected_error = MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED;
    } else if (use_wrong) {
        expected = model_different_valid_owner(expected);
        expected_error = MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
    } else {
        expected_error = MICROS_FRAME_OWNERSHIP_OK;
    }
    if (
        micros_frame_ownership_release(
            &context->ownership,
            expected,
            TEST_BASE + (index * MICROS_FRAME_SIZE)
        ) != expected_error
    ) {
        fprintf(
            stderr,
            "seed=0x%08x step=%zu release mismatch\n",
            seed,
            step
        );
        return false;
    }
    if (expected_error == MICROS_FRAME_OWNERSHIP_OK) {
        memset(&model->owners[index], 0, sizeof(model->owners[index]));
        model->targets[index] = MICROS_FRAME_HANDOFF_NONE;
    }
    return true;
}

static bool run_model_lookup(
    struct frame_ownership_test_context *context,
    const struct ownership_reference_model *model,
    uint32_t value,
    uint32_t seed,
    size_t step
)
{
    size_t index = (value >> 8) % TEST_FRAME_COUNT;
    struct micros_frame_owner output =
        frame_ownership_test_raw_kernel_owner(
            MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE
        );

    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_lookup(
            &context->ownership,
            TEST_BASE + (index * MICROS_FRAME_SIZE),
            &output
        )
    );
    if (frame_ownership_test_owners_equal(output, model->owners[index])) {
        return true;
    }
    fprintf(
        stderr,
        "seed=0x%08x step=%zu lookup mismatch\n",
        seed,
        step
    );
    return false;
}

static bool run_model_handoff_plan(
    struct frame_ownership_test_context *context,
    struct ownership_reference_model *model,
    uint32_t value,
    uint32_t seed,
    size_t step
)
{
    size_t index = (value >> 8) % TEST_FRAME_COUNT;
    struct micros_frame_owner expected = model->owners[index];
    bool use_wrong = ((value >> 20) & 1U) != 0;
    enum micros_frame_handoff_target target =
        (enum micros_frame_handoff_target)(
            (value >> 16) % MICROS_FRAME_HANDOFF_TARGET_COUNT
        );
    enum micros_frame_ownership_error expected_error;

    if (model->phase == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF) {
        expected_error = MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    } else if (model_owner_is_free(expected)) {
        expected_error = MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED;
    } else if (use_wrong) {
        expected = model_different_valid_owner(expected);
        expected_error = MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
    } else if (model_plan_is_valid(expected, target)) {
        expected_error = MICROS_FRAME_OWNERSHIP_OK;
    } else {
        expected_error = MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION;
    }
    if (
        micros_frame_ownership_prepare_handoff(
            &context->ownership,
            TEST_BASE + (index * MICROS_FRAME_SIZE),
            expected,
            target
        ) != expected_error
    ) {
        fprintf(
            stderr,
            "seed=0x%08x step=%zu plan mismatch\n",
            seed,
            step
        );
        return false;
    }
    if (expected_error == MICROS_FRAME_OWNERSHIP_OK) {
        model->targets[index] = (uint8_t)target;
    }
    return true;
}

static enum micros_frame_ownership_error
expected_model_release_set_error(
    const struct frame_ownership_test_context *context,
    const struct ownership_reference_model *model,
    struct micros_process_handle process
)
{
    size_t index;

    if (model->phase == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF) {
        return MICROS_FRAME_OWNERSHIP_ERROR_PHASE;
    }
    for (index = 0; index < TEST_FRAME_COUNT; ++index) {
        struct micros_frame_owner owner = model->owners[index];

        if (
            (
                context->release_bitmap[index / 64]
                & (UINT64_C(1) << (index % 64))
            ) == 0
        ) {
            continue;
        }
        if (model_owner_is_free(owner)) {
            return MICROS_FRAME_OWNERSHIP_ERROR_NOT_ALLOCATED;
        }
        if (
            (
                owner.kind != MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
                && owner.kind != MICROS_FRAME_OWNER_PROCESS_USER
            )
            || owner.slot != process.slot
            || owner.generation != process.generation
        ) {
            return MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
        }
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}

static void apply_model_release_set(
    const struct frame_ownership_test_context *context,
    struct ownership_reference_model *model
)
{
    size_t index;

    for (index = 0; index < TEST_FRAME_COUNT; ++index) {
        if (
            (
                context->release_bitmap[index / 64]
                & (UINT64_C(1) << (index % 64))
            ) != 0
        ) {
            memset(&model->owners[index], 0, sizeof(model->owners[index]));
            model->targets[index] = MICROS_FRAME_HANDOFF_NONE;
        }
    }
}

static bool run_model_release_set(
    struct frame_ownership_test_context *context,
    struct ownership_reference_model *model,
    uint32_t value,
    uint32_t seed,
    size_t step
)
{
    struct micros_process_handle process = (
        ((value >> 8) & 1U) == 0
            ? (struct micros_process_handle){1, 2}
            : (struct micros_process_handle){2, 3}
    );
    enum micros_frame_ownership_error expected_error;
    size_t selection;

    if (((value >> 9) & 1U) != 0) {
        ++process.generation;
    }
    frame_ownership_test_clear_release_bitmap(context);
    for (selection = 0; selection < 4; ++selection) {
        size_t selected_index =
            (value >> (selection * 5)) % TEST_FRAME_COUNT;

        frame_ownership_test_select_release_index(
            context,
            selected_index
        );
    }
    expected_error = expected_model_release_set_error(
        context,
        model,
        process
    );
    if (
        micros_frame_ownership_release_process_set(
            &context->ownership,
            process,
            context->release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        ) != expected_error
    ) {
        fprintf(
            stderr,
            "seed=0x%08x step=%zu release-set mismatch\n",
            seed,
            step
        );
        return false;
    }
    if (expected_error == MICROS_FRAME_OWNERSHIP_OK) {
        apply_model_release_set(context, model);
    }
    return true;
}

static bool run_model_process_count(
    struct frame_ownership_test_context *context,
    const struct ownership_reference_model *model,
    uint32_t value,
    uint32_t seed,
    size_t step
)
{
    const struct micros_process_handle process = {
        (uint16_t)((value >> 8) & 3U),
        ((value >> 10) & 1U) != 0 ? 2U : 3U,
    };
    uint64_t expected_count = 0;
    uint64_t actual_count = UINT64_MAX;
    size_t index;

    for (index = 0; index < TEST_FRAME_COUNT; ++index) {
        struct micros_frame_owner owner = model->owners[index];

        if (
            (
                owner.kind == MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE
                || owner.kind == MICROS_FRAME_OWNER_PROCESS_USER
                || owner.kind == MICROS_FRAME_OWNER_VM_WIRED
            )
            && owner.slot == process.slot
            && owner.generation == process.generation
        ) {
            ++expected_count;
        }
    }
    EXPECT_ERROR(
        MICROS_FRAME_OWNERSHIP_OK,
        micros_frame_ownership_count_process(
            &context->ownership,
            process,
            &actual_count
        )
    );
    if (actual_count == expected_count) {
        return true;
    }
    fprintf(
        stderr,
        "seed=0x%08x step=%zu process count mismatch\n",
        seed,
        step
    );
    return false;
}

static bool run_model_wired_set(
    struct frame_ownership_test_context *context,
    struct ownership_reference_model *model,
    uint32_t value,
    uint32_t seed,
    size_t step
)
{
    enum micros_frame_ownership_error expected_error;
    size_t mutation_index =
        (value >> 8) % (TEST_FRAME_COUNT + 1);
    size_t index;

    frame_ownership_test_clear_release_bitmap(context);
    for (index = 0; index < TEST_FRAME_COUNT; ++index) {
        if (
            model->owners[index].kind
                == MICROS_FRAME_OWNER_PROCESS_USER
        ) {
            frame_ownership_test_select_release_index(context, index);
        }
    }
    if (((value >> 20) & 1U) != 0) {
        context->release_bitmap[mutation_index / 64]
            ^= UINT64_C(1) << (mutation_index % 64);
    }
    expected_error = model_prepare_wired_set_error(
        model->owners,
        model->targets,
        model->phase,
        TEST_FRAME_COUNT,
        context->release_bitmap
    );
    if (
        micros_frame_ownership_prepare_wired_process_user_set(
            &context->ownership,
            context->release_bitmap,
            MICROS_FRAME_ALLOCATOR_BITMAP_WORDS
        ) != expected_error
    ) {
        fprintf(
            stderr,
            "seed=0x%08x step=%zu wired-set mismatch\n",
            seed,
            step
        );
        return false;
    }
    if (expected_error == MICROS_FRAME_OWNERSHIP_OK) {
        model_apply_wired_set(
            model->owners,
            model->targets,
            TEST_FRAME_COUNT
        );
    }
    return true;
}

static bool run_model_handoff_completion(
    struct frame_ownership_test_context *context,
    struct ownership_reference_model *model,
    uint32_t seed,
    size_t step
)
{
    enum micros_frame_ownership_error expected_error =
        model->phase == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        ? MICROS_FRAME_OWNERSHIP_ERROR_PHASE
        : (
            model_handoff_is_ready(
                model->owners,
                model->targets,
                TEST_FRAME_COUNT
            )
            ? MICROS_FRAME_OWNERSHIP_OK
            : MICROS_FRAME_OWNERSHIP_ERROR_TRANSITION
        );

    if (
        micros_frame_ownership_complete_handoff(&context->ownership)
            != expected_error
    ) {
        fprintf(
            stderr,
            "seed=0x%08x step=%zu completion mismatch\n",
            seed,
            step
        );
        return false;
    }
    if (expected_error == MICROS_FRAME_OWNERSHIP_OK) {
        model_apply_handoff(
            model->owners,
            model->targets,
            TEST_FRAME_COUNT
        );
        model->phase = MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF;
    }
    return true;
}

static bool execute_model_operation(
    struct frame_ownership_test_context *context,
    struct ownership_reference_model *model,
    uint32_t operation,
    uint32_t value,
    uint32_t seed,
    size_t step
)
{
    switch (operation) {
    case 0:
        return run_model_allocation(context, model, value, seed, step);
    case 1:
        return run_model_release(context, model, value, seed, step);
    case 2:
        return run_model_lookup(context, model, value, seed, step);
    case 3:
        return run_model_handoff_plan(context, model, value, seed, step);
    case 4:
        return run_model_release_set(context, model, value, seed, step);
    case 5:
        return run_model_process_count(context, model, value, seed, step);
    case 6:
        return run_model_wired_set(context, model, value, seed, step);
    default:
        return run_model_handoff_completion(context, model, seed, step);
    }
}

bool frame_ownership_test_matches_seeded_reference_model(
    struct frame_ownership_test_context *context
)
{
    const uint32_t seed = UINT32_C(0x20f0a11c);
    struct ownership_reference_model model = {
        .phase = MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP,
    };
    uint32_t random_state = seed;
    size_t step;

    EXPECT_TRUE(frame_ownership_test_initialize_fixture(context, TEST_FRAME_COUNT));
    for (step = 0; step < MODEL_STEPS; ++step) {
        uint32_t value = next_random(&random_state);
        uint32_t operation;

        if (
            step == MODEL_HANDOFF_STEP
            && !transition_model_to_handed_off(
                context,
                &model,
                TEST_FRAME_COUNT,
                step,
                seed
            )
        ) {
            fprintf(
                stderr,
                "seed=0x%08x step=%zu handoff transition mismatch\n",
                seed,
                step
            );
            return false;
        }
        operation = select_model_operation(model.phase, value);
        if (
            !execute_model_operation(
                context,
                &model,
                operation,
                value,
                seed,
                step
            )
            || !compare_model(
                context,
                model.owners,
                model.targets,
                model.phase,
                TEST_FRAME_COUNT,
                step,
                seed
            )
        ) {
            return false;
        }
    }
    return true;
}
