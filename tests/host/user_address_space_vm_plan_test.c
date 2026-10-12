#include "micros/user_address_space_core.h"
#include "user_address_space_core_test_suites.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define EXPECT_TRUE(expression)                                                                    \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: expected %s\n", __FILE__, __LINE__, #expression);              \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

#define EXPECT_ADDRESS_ERROR(expected, expression)                                                 \
    do {                                                                                           \
        enum micros_user_address_space_error actual = (expression);                                \
        if (actual != (expected)) {                                                                \
            fprintf(stderr, "%s:%d: expected %d got %d\n", __FILE__, __LINE__, (int)(expected),    \
                    (int)actual);                                                                  \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

struct vm_plan_fixture {
    struct micros_process_handle process;
    uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
    struct micros_user_address_space_vm_mapping mappings[MICROS_APPLICATION_RESIDENT_PAGE_MAX];
    struct micros_user_address_space_vm_table_plan plan;
    struct micros_user_address_space_vm_table_plan sentinel;
};

static bool bytes_equal(const void *left, const void *right, size_t size) {
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

static void initialize_vm_plan_fixture(struct vm_plan_fixture *fixture) {
    size_t index;

    *fixture = (struct vm_plan_fixture){
        .process =
            {
                .slot = 4,
                .generation = 9,
            },
        .table_frames =
            {
                UINT64_C(0x00200000),
                UINT64_C(0x00201000),
                UINT64_C(0x00202000),
                UINT64_C(0x00203000),
            },
    };
    for (index = 0; index < MICROS_APPLICATION_IMAGE_PAGE_MAX; ++index) {
        fixture->mappings[index].virtual_address =
            MICROS_APPLICATION_ENTRY + index * MICROS_SV39_PAGE_SIZE;
        fixture->mappings[index].physical_address =
            UINT64_C(0x00300000) + index * MICROS_SV39_PAGE_SIZE;
        fixture->mappings[index].permissions =
            index == 0   ? MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE |
                               MICROS_SV39_PERMISSION_USER
            : index == 1 ? MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_USER
                         : MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE |
                               MICROS_SV39_PERMISSION_USER;
    }
    fixture->mappings[MICROS_APPLICATION_IMAGE_PAGE_MAX] =
        (struct micros_user_address_space_vm_mapping){
            .virtual_address = MICROS_APPLICATION_STACK_BASE,
            .physical_address = UINT64_C(0x00400000),
            .permissions = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE |
                           MICROS_SV39_PERMISSION_USER,
    };
}

static bool plan_metadata_is_exact(const struct vm_plan_fixture *fixture) {
    const struct micros_user_address_space_vm_table_plan *plan = &fixture->plan;

    EXPECT_TRUE(plan->process.slot == fixture->process.slot &&
                plan->process.generation == fixture->process.generation &&
                plan->entry_count == MICROS_APPLICATION_RESIDENT_PAGE_MAX &&
                plan->root_index == 1 && plan->image_middle_index == 0 &&
                plan->stack_middle_index == 511);
    return true;
}

static bool table_links_are_exact(const struct vm_plan_fixture *fixture) {
    const struct micros_user_address_space_vm_table_plan *plan = &fixture->plan;
    struct micros_sv39_decoded_pte decoded;

    EXPECT_TRUE(micros_sv39_decode_pte(plan->root_pte, &decoded) == MICROS_SV39_OK &&
                decoded.kind == MICROS_SV39_PTE_TABLE &&
                decoded.physical_address ==
                    fixture->table_frames[MICROS_USER_ADDRESS_SPACE_VM_TABLE_LEVEL_ONE] &&
                micros_sv39_decode_pte(plan->image_middle_pte, &decoded) == MICROS_SV39_OK &&
                decoded.kind == MICROS_SV39_PTE_TABLE &&
                decoded.physical_address ==
                    fixture->table_frames[MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE] &&
                micros_sv39_decode_pte(plan->stack_middle_pte, &decoded) == MICROS_SV39_OK &&
                decoded.kind == MICROS_SV39_PTE_TABLE &&
                decoded.physical_address ==
                    fixture->table_frames[MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK]);
    return true;
}

static bool leaf_plans_are_exact(const struct vm_plan_fixture *fixture) {
    const struct micros_user_address_space_vm_table_plan *plan = &fixture->plan;
    struct micros_sv39_decoded_pte decoded;

    EXPECT_TRUE(plan->leaves[0].table_role == MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE &&
                plan->leaves[0].leaf_index == 0 &&
                plan->leaves[MICROS_APPLICATION_RESIDENT_PAGE_MAX - 1].table_role ==
                    MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK &&
                plan->leaves[MICROS_APPLICATION_RESIDENT_PAGE_MAX - 1].leaf_index == 511 &&
                micros_sv39_decode_pte(plan->leaves[MICROS_APPLICATION_RESIDENT_PAGE_MAX - 1].pte,
                                       &decoded) == MICROS_SV39_OK &&
                decoded.kind == MICROS_SV39_PTE_LEAF &&
                decoded.physical_address == UINT64_C(0x00400000) &&
                decoded.permissions == (MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE |
                                        MICROS_SV39_PERMISSION_USER));
    return true;
}

static bool valid_vm_table_plan_is_exact(struct vm_plan_fixture *fixture) {
    EXPECT_ADDRESS_ERROR(MICROS_USER_ADDRESS_SPACE_OK,
                         micros_user_address_space_prepare_vm_table_plan(
                             fixture->process, fixture->table_frames, fixture->mappings,
                             MICROS_APPLICATION_RESIDENT_PAGE_MAX, &fixture->plan));
    EXPECT_TRUE(plan_metadata_is_exact(fixture) && table_links_are_exact(fixture) &&
                leaf_plans_are_exact(fixture));
    fixture->sentinel = fixture->plan;
    return true;
}

static bool permission_failure_preserves_output(struct vm_plan_fixture *fixture) {
    fixture->mappings[1].permissions = fixture->mappings[0].permissions;
    EXPECT_ADDRESS_ERROR(MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION,
                         micros_user_address_space_prepare_vm_table_plan(
                             fixture->process, fixture->table_frames, fixture->mappings,
                             MICROS_APPLICATION_RESIDENT_PAGE_MAX, &fixture->plan));
    EXPECT_TRUE(bytes_equal(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)));
    fixture->mappings[1].permissions = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_USER;
    return true;
}

static bool table_alias_is_rejected_without_output_change(struct vm_plan_fixture *fixture) {
    fixture->mappings[2].physical_address = fixture->table_frames[0];
    EXPECT_ADDRESS_ERROR(MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
                         micros_user_address_space_prepare_vm_table_plan(
                             fixture->process, fixture->table_frames, fixture->mappings,
                             MICROS_APPLICATION_RESIDENT_PAGE_MAX, &fixture->plan));
    EXPECT_TRUE(bytes_equal(&fixture->plan, &fixture->sentinel, sizeof(fixture->plan)));
    return true;
}

bool micros_user_address_space_vm_plan_test_run(void) {
    struct vm_plan_fixture fixture;

    initialize_vm_plan_fixture(&fixture);
    return (valid_vm_table_plan_is_exact(&fixture) &&
            permission_failure_preserves_output(&fixture) &&
            table_alias_is_rejected_without_output_change(&fixture));
}
