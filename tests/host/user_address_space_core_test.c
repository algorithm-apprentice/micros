#include "micros/user_address_space_core.h"

#include <stdbool.h>
#include <stdio.h>

bool micros_user_address_space_core_test_run(void);

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

#define EXPECT_ADDRESS_ERROR(expected, expression) \
    do { \
        enum micros_user_address_space_error actual = (expression); \
        if (actual != (expected)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %d got %d\n", \
                __FILE__, \
                __LINE__, \
                (int)(expected), \
                (int)actual \
            ); \
            return false; \
        } \
    } while (false)

static struct micros_frame_owner owner(
    enum micros_frame_owner_kind kind,
    uint16_t slot,
    uint32_t generation
)
{
    return (struct micros_frame_owner){
        .generation = generation,
        .slot = slot,
        .kind = (uint8_t)kind,
        .reserved = 0,
    };
}

static bool bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
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

static bool test_classifies_exact_vm_scratch_leaf(void)
{
    const struct micros_process_handle process = {
        .slot = 2,
        .generation = 7,
    };
    const uint64_t root = UINT64_C(0x00200000);
    const uint64_t physical = UINT64_C(0x00300000);
    const struct micros_frame_owner transferable =
        owner(MICROS_FRAME_OWNER_VM_TRANSFERABLE, 0, 0);
    struct micros_user_address_space_vm_scratch_authority authority = {
        .active = true,
        .load_token = 11,
        .process = process,
        .root_physical_address = root,
        .virtual_address = MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
        .physical_address = physical,
    };
    struct micros_sv39_decoded_pte leaf;
    enum micros_user_address_space_leaf_class classification =
        MICROS_USER_ADDRESS_SPACE_LEAF_REGULAR;
    uint64_t pte;

    EXPECT_TRUE(
        micros_sv39_make_leaf_pte(
            physical,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_USER,
            &pte
        ) == MICROS_SV39_OK
        && micros_sv39_decode_pte(pte, &leaf) == MICROS_SV39_OK
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_classify_managed_leaf(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            process,
            root,
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            &leaf,
            transferable,
            &authority,
            &classification
        )
    );
    EXPECT_TRUE(
        classification
            == MICROS_USER_ADDRESS_SPACE_LEAF_VM_SCRATCH
    );

    classification = MICROS_USER_ADDRESS_SPACE_LEAF_REGULAR;
    authority.root_physical_address += MICROS_SV39_PAGE_SIZE;
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_classify_managed_leaf(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            process,
            root,
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            &leaf,
            transferable,
            &authority,
            &classification
        )
    );
    EXPECT_TRUE(
        classification == MICROS_USER_ADDRESS_SPACE_LEAF_REGULAR
    );
    authority.root_physical_address = root;

    leaf.permissions = MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_USER;
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_classify_managed_leaf(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            process,
            root,
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            &leaf,
            transferable,
            &authority,
            &classification
        )
    );
    leaf.permissions = MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_USER;

    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_classify_managed_leaf(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            process,
            root,
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
            &leaf,
            owner(
                MICROS_FRAME_OWNER_VM_WIRED,
                process.slot,
                process.generation
            ),
            &authority,
            &classification
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_classify_managed_leaf(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            process,
            root,
            MICROS_APPLICATION_ENTRY,
            &leaf,
            owner(
                MICROS_FRAME_OWNER_VM_WIRED,
                process.slot,
                process.generation
            ),
            &authority,
            &classification
        )
    );
    EXPECT_TRUE(
        classification == MICROS_USER_ADDRESS_SPACE_LEAF_REGULAR
    );
    return true;
}

static bool test_prepares_exact_vm_table_plan(void)
{
    const struct micros_process_handle process = {
        .slot = 4,
        .generation = 9,
    };
    const uint64_t table_frames[
        MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT
    ] = {
        UINT64_C(0x00200000),
        UINT64_C(0x00201000),
        UINT64_C(0x00202000),
        UINT64_C(0x00203000),
    };
    struct micros_user_address_space_vm_mapping mappings[
        MICROS_APPLICATION_RESIDENT_PAGE_MAX
    ] = {0};
    struct micros_user_address_space_vm_table_plan plan;
    struct micros_user_address_space_vm_table_plan sentinel;
    struct micros_sv39_decoded_pte decoded;
    size_t index;

    for (
        index = 0;
        index < MICROS_APPLICATION_IMAGE_PAGE_MAX;
        ++index
    ) {
        mappings[index].virtual_address =
            MICROS_APPLICATION_ENTRY
            + index * MICROS_SV39_PAGE_SIZE;
        mappings[index].physical_address =
            UINT64_C(0x00300000)
            + index * MICROS_SV39_PAGE_SIZE;
        mappings[index].permissions =
            index == 0
                ? MICROS_SV39_PERMISSION_READ
                    | MICROS_SV39_PERMISSION_EXECUTE
                    | MICROS_SV39_PERMISSION_USER
                : index == 1
                    ? MICROS_SV39_PERMISSION_READ
                        | MICROS_SV39_PERMISSION_USER
                    : MICROS_SV39_PERMISSION_READ
                        | MICROS_SV39_PERMISSION_WRITE
                        | MICROS_SV39_PERMISSION_USER;
    }
    mappings[MICROS_APPLICATION_IMAGE_PAGE_MAX] =
        (struct micros_user_address_space_vm_mapping){
            .virtual_address = MICROS_APPLICATION_STACK_BASE,
            .physical_address = UINT64_C(0x00400000),
            .permissions = MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_USER,
        };

    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_prepare_vm_table_plan(
            process,
            table_frames,
            mappings,
            MICROS_APPLICATION_RESIDENT_PAGE_MAX,
            &plan
        )
    );
    EXPECT_TRUE(
        plan.process.slot == process.slot
        && plan.process.generation == process.generation
        && plan.entry_count == MICROS_APPLICATION_RESIDENT_PAGE_MAX
        && plan.root_index == 1
        && plan.image_middle_index == 0
        && plan.stack_middle_index == 511
    );
    EXPECT_TRUE(
        micros_sv39_decode_pte(plan.root_pte, &decoded)
            == MICROS_SV39_OK
        && decoded.kind == MICROS_SV39_PTE_TABLE
        && decoded.physical_address
            == table_frames[
                MICROS_USER_ADDRESS_SPACE_VM_TABLE_LEVEL_ONE
            ]
        && micros_sv39_decode_pte(
            plan.image_middle_pte,
            &decoded
        ) == MICROS_SV39_OK
        && decoded.kind == MICROS_SV39_PTE_TABLE
        && decoded.physical_address
            == table_frames[
                MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE
            ]
        && micros_sv39_decode_pte(
            plan.stack_middle_pte,
            &decoded
        ) == MICROS_SV39_OK
        && decoded.kind == MICROS_SV39_PTE_TABLE
        && decoded.physical_address
            == table_frames[
                MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK
            ]
    );
    EXPECT_TRUE(
        plan.leaves[0].table_role
            == MICROS_USER_ADDRESS_SPACE_VM_TABLE_IMAGE
        && plan.leaves[0].leaf_index == 0
        && plan.leaves[
            MICROS_APPLICATION_RESIDENT_PAGE_MAX - 1
        ].table_role == MICROS_USER_ADDRESS_SPACE_VM_TABLE_STACK
        && plan.leaves[
            MICROS_APPLICATION_RESIDENT_PAGE_MAX - 1
        ].leaf_index == 511
        && micros_sv39_decode_pte(
            plan.leaves[
                MICROS_APPLICATION_RESIDENT_PAGE_MAX - 1
            ].pte,
            &decoded
        ) == MICROS_SV39_OK
        && decoded.kind == MICROS_SV39_PTE_LEAF
        && decoded.physical_address == UINT64_C(0x00400000)
        && decoded.permissions
            == (
                MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_USER
            )
    );

    sentinel = plan;
    mappings[1].permissions = mappings[0].permissions;
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_PERMISSION,
        micros_user_address_space_prepare_vm_table_plan(
            process,
            table_frames,
            mappings,
            MICROS_APPLICATION_RESIDENT_PAGE_MAX,
            &plan
        )
    );
    EXPECT_TRUE(
        bytes_equal(&plan, &sentinel, sizeof(plan))
    );
    mappings[1].permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_USER;

    mappings[2].physical_address = table_frames[0];
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_prepare_vm_table_plan(
            process,
            table_frames,
            mappings,
            MICROS_APPLICATION_RESIDENT_PAGE_MAX,
            &plan
        )
    );
    EXPECT_TRUE(
        bytes_equal(&plan, &sentinel, sizeof(plan))
    );
    return true;
}

bool micros_user_address_space_core_test_run(void)
{
    const struct micros_process_handle process = {
        .slot = 2,
        .generation = 7,
    };
    enum micros_frame_owner_kind kind =
        MICROS_FRAME_OWNER_KERNEL_RETAINED;

    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_leaf_owner_kind(
            MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP,
            &kind
        )
    );
    EXPECT_TRUE(kind == MICROS_FRAME_OWNER_PROCESS_USER);
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_leaf_owner_kind(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            &kind
        )
    );
    EXPECT_TRUE(kind == MICROS_FRAME_OWNER_VM_WIRED);

    kind = MICROS_FRAME_OWNER_KERNEL_RETAINED;
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT,
        micros_user_address_space_leaf_owner_kind(
            MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP,
            NULL
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_PHASE,
        micros_user_address_space_leaf_owner_kind(
            (enum micros_frame_ownership_phase)0,
            &kind
        )
    );
    EXPECT_TRUE(kind == MICROS_FRAME_OWNER_KERNEL_RETAINED);

    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP,
            MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE,
            process,
            owner(
                MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
                process.slot,
                process.generation
            )
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE,
            process,
            owner(
                MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE,
                process.slot,
                process.generation
            )
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP,
            MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process,
            owner(
                MICROS_FRAME_OWNER_PROCESS_USER,
                process.slot,
                process.generation
            )
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process,
            owner(
                MICROS_FRAME_OWNER_VM_WIRED,
                process.slot,
                process.generation
            )
        )
    );

    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process,
            owner(MICROS_FRAME_OWNER_VM_TRANSFERABLE, 0, 0)
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process,
            owner(MICROS_FRAME_OWNER_FREE, 0, 0)
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process,
            owner(MICROS_FRAME_OWNER_KERNEL_RETAINED, 0, 0)
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process,
            owner(
                MICROS_FRAME_OWNER_PROCESS_USER,
                process.slot,
                process.generation
            )
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP,
            MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process,
            owner(
                MICROS_FRAME_OWNER_VM_WIRED,
                process.slot,
                process.generation
            )
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process,
            owner(
                MICROS_FRAME_OWNER_VM_WIRED,
                process.slot + 1,
                process.generation
            )
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process,
            owner(
                MICROS_FRAME_OWNER_VM_WIRED,
                process.slot,
                process.generation + 1
            )
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
            MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process,
            (struct micros_frame_owner){
                .generation = process.generation,
                .slot = process.slot,
                .kind = MICROS_FRAME_OWNER_VM_WIRED,
                .reserved = 1,
            }
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP,
            (enum micros_user_address_space_frame_role)0,
            process,
            owner(
                MICROS_FRAME_OWNER_PROCESS_USER,
                process.slot,
                process.generation
            )
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_PHASE,
        micros_user_address_space_validate_frame_owner(
            (enum micros_frame_ownership_phase)0,
            MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process,
            owner(
                MICROS_FRAME_OWNER_PROCESS_USER,
                process.slot,
                process.generation
            )
        )
    );
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP,
            MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            (struct micros_process_handle){0},
            owner(MICROS_FRAME_OWNER_PROCESS_USER, 0, 0)
        )
    );
    return (
        test_classifies_exact_vm_scratch_leaf()
        && test_prepares_exact_vm_table_plan()
    );
}
