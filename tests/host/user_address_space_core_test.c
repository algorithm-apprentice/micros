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
    return true;
}
