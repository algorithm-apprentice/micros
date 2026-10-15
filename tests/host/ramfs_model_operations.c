#include "tests/host/ramfs_model_internal.h"

#include <stdio.h>
#include <string.h>

static uint64_t next_random(struct ramfs_model *selected)
{
    uint64_t value = selected->random_state;

    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    selected->random_state = value;
    return value;
}

static bool build_absolute_path(
    const struct reference_state *reference,
    uint16_t slot,
    uint8_t path[MICROS_RAMFS_PATH_MAX],
    size_t *length
)
{
    uint16_t chain[MICROS_RAMFS_NODE_CAPACITY];
    size_t depth = 0;
    size_t position = 0;

    if (!reference->nodes[slot].live) {
        return false;
    }
    while (slot != 0) {
        if (depth == MICROS_RAMFS_NODE_CAPACITY) {
            return false;
        }
        chain[depth++] = slot;
        slot = reference->nodes[slot].parent;
    }
    path[position++] = '/';
    while (depth != 0) {
        const struct reference_node *node =
            &reference->nodes[chain[--depth]];

        if (
            position + node->name_length + 1
            > MICROS_RAMFS_PATH_MAX
        ) {
            return false;
        }
        memcpy(&path[position], node->name, node->name_length);
        position += node->name_length;
        if (depth != 0) {
            path[position++] = '/';
        }
    }
    path[position++] = 0;
    *length = position;
    return true;
}

static uint16_t random_live_slot(
    struct ramfs_model *selected
)
{
    uint16_t start = (uint16_t)(
        next_random(selected) % MICROS_RAMFS_NODE_CAPACITY
    );
    uint16_t offset;

    for (offset = 0; offset < MICROS_RAMFS_NODE_CAPACITY; ++offset) {
        uint16_t slot = (uint16_t)(
            (start + offset) % MICROS_RAMFS_NODE_CAPACITY
        );

        if (selected->reference.nodes[slot].live) {
            return slot;
        }
    }
    return 0;
}

static uint16_t random_referenced_nonroot_slot(
    struct ramfs_model *selected
)
{
    uint16_t start = (uint16_t)(
        1 + next_random(selected) % (MICROS_RAMFS_NODE_CAPACITY - 1)
    );
    uint16_t offset;

    for (
        offset = 0;
        offset < MICROS_RAMFS_NODE_CAPACITY - 1;
        ++offset
    ) {
        uint16_t slot = (uint16_t)(
            1
            + (
                start - 1 + offset
            ) % (MICROS_RAMFS_NODE_CAPACITY - 1)
        );

        if (
            selected->reference.nodes[slot].live
            && selected->reference.nodes[slot].reference_count != 0
            && slot != selected->capacity_file_slot
        ) {
            return slot;
        }
    }
    return MICROS_RAMFS_NODE_NONE;
}

bool ramfs_model_execute_random_operation(struct ramfs_model *selected)
{
    uint64_t random = next_random(selected);
    uint64_t root = ramfs_model_reference_handle(&selected->reference, 0);
    uint8_t path[MICROS_RAMFS_PATH_MAX];
    uint8_t bytes[64];
    size_t length;
    uint16_t slot;
    size_t index;

    switch (random % 9) {
    case 0:
        slot = random_live_slot(selected);
        MODEL_EXPECT(
            selected,
            build_absolute_path(
                &selected->reference,
                slot,
                path,
                &length
            )
        );
        return ramfs_model_execute_lookup(
            selected,
            root,
            root,
            path,
            length,
            false,
            false
        );
    case 1:
    case 2: {
        char name[8];
        uint32_t type = (
            (random & 1) == 0
                ? MICROS_RAMFS_MESSAGE_CREATE
                : MICROS_RAMFS_MESSAGE_MKDIR
        );

        (void)snprintf(
            name,
            sizeof(name),
            "%c%04u",
            type == MICROS_RAMFS_MESSAGE_CREATE ? 'f' : 'd',
            (unsigned)(selected->step % 10000)
        );
        return ramfs_model_execute_create(selected, type, root, name);
    }
    case 3:
        for (index = 0; index < sizeof(bytes); ++index) {
            bytes[index] = (uint8_t)(random >> (index % 8));
        }
        return ramfs_model_execute_write(
            selected,
            ramfs_model_reference_handle(
                &selected->reference,
                selected->capacity_file_slot
            ),
            (random >> 8) % (MICROS_RAMFS_BLOCK_SIZE * 5),
            bytes,
            (uint32_t)(1 + (random % sizeof(bytes)))
        );
    case 4:
        return ramfs_model_execute_read(
            selected,
            ramfs_model_reference_handle(
                &selected->reference,
                selected->capacity_file_slot
            ),
            (random >> 8) % (MICROS_RAMFS_BLOCK_SIZE * 5),
            (uint32_t)(1 + (random % sizeof(bytes)))
        );
    case 5:
        return ramfs_model_execute_getdents(
            selected,
            root,
            (uint32_t)(
                MICROS_RAMFS_DIRECTORY_RECORD_SIZE
                * (1 + random % 4)
            )
        );
    case 6:
        slot = random_referenced_nonroot_slot(selected);
        if (slot == MICROS_RAMFS_NODE_NONE) {
            return ramfs_model_execute_putnode(selected, root, 1);
        }
        return ramfs_model_execute_putnode(
            selected,
            ramfs_model_reference_handle(&selected->reference, slot),
            1
        );
    case 7:
        return ramfs_model_execute_stale(selected);
    case 8:
    default:
        if ((random & UINT64_C(0x100)) != 0) {
            static const uint8_t etc_path[] = "/etc";

            return ramfs_model_execute_lookup(
                selected,
                root,
                root,
                etc_path,
                sizeof(etc_path),
                false,
                true
            );
        }
        return ramfs_model_execute_malformed(selected);
    }
}

bool ramfs_model_execute_scripted_operation(struct ramfs_model *selected)
{
    static const uint8_t etc_path[] = "/etc";
    static const uint8_t motd_relative[] = "motd";
    static const uint8_t model_path[] = "/model";
    static const uint8_t grant_path[] = "/etc";
    static const uint8_t sparse_bytes[] = {'x', 'y'};
    uint64_t root = ramfs_model_reference_handle(&selected->reference, 0);

    switch (selected->step) {
    case 0:
        return ramfs_model_execute_mount(selected);
    case 1:
        return ramfs_model_execute_lookup(
            selected,
            root,
            root,
            etc_path,
            sizeof(etc_path),
            false,
            false
        );
    case 2:
        return ramfs_model_execute_lookup(
            selected,
            ramfs_model_reference_handle(&selected->reference, 1),
            ramfs_model_reference_handle(&selected->reference, 1),
            motd_relative,
            sizeof(motd_relative),
            true,
            false
        );
    case 3:
        MODEL_EXPECT(
            selected,
            ramfs_model_execute_create(
                selected,
                MICROS_RAMFS_MESSAGE_MKDIR,
                root,
                "model"
            )
        );
        selected->model_directory_slot = 3;
        return true;
    case 4:
        return ramfs_model_execute_lookup(
            selected,
            root,
            root,
            model_path,
            sizeof(model_path),
            false,
            false
        );
    case 5:
        MODEL_EXPECT(
            selected,
            ramfs_model_execute_create(
                selected,
                MICROS_RAMFS_MESSAGE_CREATE,
                ramfs_model_reference_handle(
                    &selected->reference,
                    selected->model_directory_slot
                ),
                "capacity"
            )
        );
        selected->capacity_file_slot = 4;
        return true;
    case 6:
        return ramfs_model_execute_write(
            selected,
            ramfs_model_reference_handle(
                &selected->reference,
                selected->capacity_file_slot
            ),
            8194,
            sparse_bytes,
            sizeof(sparse_bytes)
        );
    case 7:
        return ramfs_model_execute_read(
            selected,
            ramfs_model_reference_handle(
                &selected->reference,
                selected->capacity_file_slot
            ),
            8188,
            8
        );
    case 8:
    case 9:
        return ramfs_model_execute_getdents(selected, root, 128);
    case 10:
        return ramfs_model_execute_putnode(
            selected,
            ramfs_model_reference_handle(&selected->reference, 2),
            1
        );
    case 11:
        return ramfs_model_execute_stale(selected);
    case 12:
        return ramfs_model_execute_malformed(selected);
    case 13:
        return ramfs_model_execute_lookup(
            selected,
            root,
            root,
            grant_path,
            sizeof(grant_path),
            false,
            true
        );
    case 14:
        return ramfs_model_execute_mount(selected);
    case 15: {
        uint8_t path[
            sizeof("/missing/") + MICROS_RAMFS_NAME_MAX + 1
        ];

        memcpy(path, "/missing/", sizeof("/missing/") - 1);
        memset(
            &path[sizeof("/missing/") - 1],
            'a',
            MICROS_RAMFS_NAME_MAX + 1
        );
        path[sizeof(path) - 1] = 0;
        return ramfs_model_execute_lookup(
            selected,
            root,
            root,
            path,
            sizeof(path),
            false,
            false
        );
    }
    default:
        return false;
    }
}
