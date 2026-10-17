#include "micros/user_address_space_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct vm_plan_builder {
    struct micros_user_address_space_vm_table_plan candidate;
    size_t image_page_count;
    bool saw_read_only;
    bool saw_writable;
    int prior_permission_group;
};

struct vm_mapping_indices {
    uint16_t root;
    uint16_t middle;
    uint16_t leaf;
};

static void clear_bytes(void *storage, size_t size) {
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size) {
    uint8_t *output = destination;
    const uint8_t *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static bool process_is_valid(struct micros_process_handle process) {
    return (process.slot < MICROS_PROCESS_CAPACITY && process.generation != 0 &&
            process.generation <= MICROS_PROCESS_GENERATION_MAX);
}

static bool frame_address_is_valid(uint64_t address) {
    return (address != 0 && address % MICROS_SV39_PAGE_SIZE == 0);
}

static bool mapping_address_is_valid(uint64_t virtual_address) {
    return (virtual_address >= MICROS_USER_VIRTUAL_BASE &&
            virtual_address < MICROS_USER_VIRTUAL_END &&
            virtual_address % MICROS_SV39_PAGE_SIZE == 0 &&
            micros_sv39_virtual_address_is_canonical(virtual_address));
}

static int permission_group(uint32_t permissions) {
    const uint32_t text =
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE | MICROS_SV39_PERMISSION_USER;
    const uint32_t read_only = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_USER;
    const uint32_t writable =
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE | MICROS_SV39_PERMISSION_USER;

    if (permissions == text) {
        return 0;
    }
    if (permissions == read_only) {
        return 1;
    }
    return permissions == writable ? 2 : -1;
}

static enum micros_user_address_space_error
prepare_table_frames(const uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
                     struct micros_user_address_space_vm_table_plan *candidate) {
    size_t index;

    for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        size_t prior_index;

        if (!frame_address_is_valid(table_frames[index])) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
        }
        for (prior_index = 0; prior_index < index; ++prior_index) {
            if (table_frames[prior_index] == table_frames[index]) {
                return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            }
        }
        candidate->table_frames[index] = table_frames[index];
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
prepare_table_links(const uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
                    struct micros_user_address_space_vm_table_plan *candidate) {
    if (micros_sv39_make_table_pte(table_frames[MICROS_USER_ADDRESS_SPACE_VM_TABLE_LEVEL_ONE],
                                   &candidate->root_pte) != MICROS_SV39_OK ||
        micros_sv39_make_table_pte(table_frames[MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE],
                                   &candidate->image_middle_pte) != MICROS_SV39_OK ||
        micros_sv39_make_table_pte(table_frames[MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK],
                                   &candidate->stack_middle_pte) != MICROS_SV39_OK) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
validate_mapping_frame_ownership(const uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
                                 const struct micros_user_address_space_vm_mapping *mappings,
                                 size_t mapping_index) {
    const uint64_t physical_address = mappings[mapping_index].physical_address;
    size_t prior_index;

    for (prior_index = 0; prior_index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++prior_index) {
        if (physical_address == table_frames[prior_index]) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
    }
    for (prior_index = 0; prior_index < mapping_index; ++prior_index) {
        if (mappings[prior_index].physical_address == physical_address) {
            return MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
        }
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
read_mapping_indices(uint64_t virtual_address, struct vm_mapping_indices *indices) {
    if (micros_sv39_vpn_index(virtual_address, 2, &indices->root) != MICROS_SV39_OK ||
        micros_sv39_vpn_index(virtual_address, 1, &indices->middle) != MICROS_SV39_OK ||
        micros_sv39_vpn_index(virtual_address, 0, &indices->leaf) != MICROS_SV39_OK ||
        indices->root != 1) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
prepare_image_leaf(struct vm_plan_builder *builder,
                   const struct micros_user_address_space_vm_mapping *mapping, size_t mapping_index,
                   int group, struct vm_mapping_indices indices) {
    if (mapping->virtual_address !=
            MICROS_APPLICATION_ENTRY + mapping_index * MICROS_SV39_PAGE_SIZE ||
        group < 0 || (mapping_index == 0 && group != 0) ||
        (mapping_index != 0 && (group < builder->prior_permission_group ||
                                group > builder->prior_permission_group + 1))) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION;
    }
    if (mapping_index == 0) {
        builder->candidate.root_index = indices.root;
        builder->candidate.image_middle_index = indices.middle;
    } else if (indices.middle != builder->candidate.image_middle_index) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;
    }
    builder->saw_read_only = builder->saw_read_only || group == 1;
    builder->saw_writable = builder->saw_writable || group == 2;
    builder->prior_permission_group = group;
    builder->candidate.leaves[mapping_index].table_role = MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
prepare_stack_leaf(struct vm_plan_builder *builder,
                   const struct micros_user_address_space_vm_mapping *mapping, size_t mapping_index,
                   int group, struct vm_mapping_indices indices) {
    if (mapping->virtual_address != MICROS_APPLICATION_STACK_BASE || group != 2 ||
        builder->prior_permission_group != 2 || !builder->saw_read_only || !builder->saw_writable ||
        indices.middle == builder->candidate.image_middle_index) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION;
    }
    builder->candidate.stack_middle_index = indices.middle;
    builder->candidate.leaves[mapping_index].table_role = MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static enum micros_user_address_space_error
prepare_mapping_leaf(struct vm_plan_builder *builder,
                     const uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
                     const struct micros_user_address_space_vm_mapping *mappings,
                     size_t mapping_index) {
    const struct micros_user_address_space_vm_mapping *mapping = &mappings[mapping_index];
    struct micros_user_address_space_vm_leaf_plan *leaf = &builder->candidate.leaves[mapping_index];
    struct vm_mapping_indices indices;
    enum micros_user_address_space_error error;
    const int group = permission_group(mapping->permissions);

    if (mapping->reserved != 0 || !mapping_address_is_valid(mapping->virtual_address) ||
        mapping->virtual_address == MICROS_VM_SCRATCH_VIRTUAL_ADDRESS ||
        !frame_address_is_valid(mapping->physical_address)) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    error = validate_mapping_frame_ownership(table_frames, mappings, mapping_index);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = read_mapping_indices(mapping->virtual_address, &indices);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    if (mapping_index < builder->image_page_count) {
        error = prepare_image_leaf(builder, mapping, mapping_index, group, indices);
    } else {
        error = prepare_stack_leaf(builder, mapping, mapping_index, group, indices);
    }
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    leaf->leaf_index = indices.leaf;
    if (micros_sv39_make_leaf_pte(mapping->physical_address, mapping->permissions, &leaf->pte) !=
        MICROS_SV39_OK) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
    }
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error micros_user_address_space_prepare_vm_table_plan(
    struct micros_process_handle process,
    const uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    const struct micros_user_address_space_vm_mapping *mappings, size_t mapping_count,
    struct micros_user_address_space_vm_table_plan *plan) {
    struct vm_plan_builder builder;
    enum micros_user_address_space_error error;
    size_t mapping_index;

    if (table_frames == NULL || mappings == NULL || plan == NULL || !process_is_valid(process) ||
        mapping_count < MICROS_APPLICATION_IMAGE_PAGE_MIN + 1 ||
        mapping_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    clear_bytes(&builder, sizeof(builder));
    builder.image_page_count = mapping_count - 1;
    error = prepare_table_frames(table_frames, &builder.candidate);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    error = prepare_table_links(table_frames, &builder.candidate);
    if (error != MICROS_USER_ADDRESS_SPACE_OK) {
        return error;
    }
    for (mapping_index = 0; mapping_index < mapping_count; ++mapping_index) {
        error = prepare_mapping_leaf(&builder, table_frames, mappings, mapping_index);
        if (error != MICROS_USER_ADDRESS_SPACE_OK) {
            return error;
        }
    }
    builder.candidate.process = process;
    builder.candidate.entry_count = (uint32_t)mapping_count;
    copy_bytes(plan, &builder.candidate, sizeof(builder.candidate));
    return MICROS_USER_ADDRESS_SPACE_OK;
}
