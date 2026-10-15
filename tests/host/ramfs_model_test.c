#include "tests/host/ramfs_model_internal.h"

#include <stdio.h>
#include <string.h>

static bool run_model(uint64_t seed)
{
    struct ramfs_model *selected = ramfs_model_context();
    const struct ramfs_model_seed *seed_info = ramfs_model_seed_context();

    size_t step;

    memset(selected, 0, sizeof(*selected));
    selected->seed = seed;
    selected->random_state = seed;
    selected->trace_hash = MICROS_RAMFS_SEED_FNV_OFFSET;
    selected->capacity_file_slot = MICROS_RAMFS_NODE_NONE;
    ramfs_model_initialize_reference(&selected->reference);
    MODEL_EXPECT(
        selected,
        micros_ramfs_state_initialize(
            &selected->production,
            UINT32_C(0x00001005),
            seed_info->seed_image,
            seed_info->seed_image_size
        ) == MICROS_RAMFS_CORE_OK
    );
    MODEL_EXPECT(selected, ramfs_model_compare_state(selected));

    for (step = 0; step < MODEL_STEP_COUNT; ++step) {
        selected->step = step;
        if (step <= 15) {
            MODEL_EXPECT(selected, ramfs_model_execute_scripted_operation(selected));
        } else if (
            step >= MODEL_CAPACITY_PHASE
            && step < MODEL_CAPACITY_PHASE_END
        ) {
            uint8_t byte = (uint8_t)step;
            size_t logical = step - MODEL_CAPACITY_PHASE;

            MODEL_EXPECT(
                selected,
                ramfs_model_execute_write(
                    selected,
                    ramfs_model_reference_handle(
                        &selected->reference,
                        selected->capacity_file_slot
                    ),
                    logical * MICROS_RAMFS_BLOCK_SIZE,
                    &byte,
                    1
                )
            );
        } else {
            MODEL_EXPECT(selected, ramfs_model_execute_random_operation(selected));
        }
    }

    MODEL_EXPECT(selected, selected->coverage.mount >= 2);
    MODEL_EXPECT(selected, selected->coverage.lookup_absolute != 0);
    MODEL_EXPECT(selected, selected->coverage.lookup_relative != 0);
    MODEL_EXPECT(selected, selected->coverage.create != 0);
    MODEL_EXPECT(selected, selected->coverage.mkdir != 0);
    MODEL_EXPECT(selected, selected->coverage.read != 0);
    MODEL_EXPECT(selected, selected->coverage.write != 0);
    MODEL_EXPECT(selected, selected->coverage.getdents != 0);
    MODEL_EXPECT(selected, selected->coverage.putnode != 0);
    MODEL_EXPECT(selected, selected->coverage.stale != 0);
    MODEL_EXPECT(selected, selected->coverage.malformed != 0);
    MODEL_EXPECT(selected, selected->coverage.grant_failure != 0);
    MODEL_EXPECT(selected, selected->coverage.no_space != 0);
    printf(
        "RAMFS_MODEL_PASS seed=0x%016llx steps=%d "
        "trace=0x%016llx\n",
        (unsigned long long)seed,
        MODEL_STEP_COUNT,
        (unsigned long long)selected->trace_hash
    );
    return true;
}

int main(int argc, char **argv)
{
    const uint64_t seed = UINT64_C(0x52414d46534d4f44);
    const char *seed_path;

    if (argc == 1) {
        seed_path = "generated/ramfs_seed.bin";
    } else if (argc == 2) {
        seed_path = argv[1];
    } else {
        return 1;
    }
    if (!ramfs_model_load_seed(seed_path) || !run_model(seed)) {
        return 1;
    }
    return 0;
}
