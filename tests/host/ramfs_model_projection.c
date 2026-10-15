#include "tests/host/ramfs_model_internal.h"

#include <stdio.h>
#include <string.h>

static bool compare_header_and_scratch(
    struct ramfs_model *selected,
    const struct micros_ramfs_state *production,
    const struct reference_state *reference
)
{
    MODEL_EXPECT(
        selected,
        micros_ramfs_state_validate(production)
            == MICROS_RAMFS_CORE_OK
    );
    MODEL_EXPECT(
        selected,
        (
            production->phase == MICROS_RAMFS_PHASE_MOUNTED
        ) == reference->mounted
    );
    MODEL_EXPECT(
        selected,
        production->node_count == reference->node_count
    );
    MODEL_EXPECT(
        selected,
        production->allocated_block_count
            == reference->allocated_block_count
    );
    MODEL_EXPECT(
        selected,
        memcmp(
            production->scratch,
            (uint8_t[MICROS_RAMFS_TRANSFER_MAX]){0},
            MICROS_RAMFS_TRANSFER_MAX
        ) == 0
    );
    return true;
}

static bool compare_nodes(
    struct ramfs_model *selected,
    const struct micros_ramfs_state *production,
    const struct reference_state *reference
)
{
    size_t index;

    for (index = 0; index < MICROS_RAMFS_NODE_CAPACITY; ++index) {
        const struct micros_ramfs_node *actual =
            &production->nodes[index];
        const struct reference_node *expected =
            &reference->nodes[index];
        size_t logical;

        MODEL_EXPECT(selected, actual->live == expected->live);
        MODEL_EXPECT(
            selected,
            actual->generation == expected->generation
        );
        MODEL_EXPECT(selected, actual->mode == expected->mode);
        MODEL_EXPECT(
            selected,
            actual->link_count == expected->link_count
        );
        MODEL_EXPECT(
            selected,
            actual->reference_count == expected->reference_count
        );
        MODEL_EXPECT(
            selected,
            actual->name_length == expected->name_length
        );
        MODEL_EXPECT(
            selected,
            actual->size == expected->size
        );
        MODEL_EXPECT(
            selected,
            actual->parent == (
                expected->live
                    ? ramfs_model_reference_handle(reference, expected->parent)
                    : 0
            )
        );
        MODEL_EXPECT(
            selected,
            memcmp(
                actual->name,
                expected->name,
                MICROS_RAMFS_NAME_MAX
            ) == 0
        );
        for (
            logical = 0;
            logical < MICROS_RAMFS_BLOCK_CAPACITY;
            ++logical
        ) {
            MODEL_EXPECT(
                selected,
                actual->blocks[logical]
                    == expected->blocks[logical]
            );
        }
    }
    return true;
}

static bool compare_blocks(
    struct ramfs_model *selected,
    const struct micros_ramfs_state *production,
    const struct reference_state *reference
)
{
    size_t index;

    for (index = 0; index < MICROS_RAMFS_BLOCK_CAPACITY; ++index) {
        MODEL_EXPECT(
            selected,
            production->block_owner[index]
                == reference->block_owner[index]
        );
        MODEL_EXPECT(
            selected,
            production->block_logical[index]
                == reference->block_logical[index]
        );
        MODEL_EXPECT(
            selected,
            memcmp(
                production->data[index],
                reference->data[index],
                MICROS_RAMFS_BLOCK_SIZE
            ) == 0
        );
    }
    return true;
}

bool ramfs_model_compare_state(struct ramfs_model *selected)
{
    const struct micros_ramfs_state *production =
        &selected->production;
    const struct reference_state *reference =
        &selected->reference;

    return compare_header_and_scratch(selected, production, reference)
        && compare_nodes(selected, production, reference)
        && compare_blocks(selected, production, reference);
}
