#include "micros/user_address_space_core.h"
#include "user_address_space_core_test_suites.h"

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

struct scratch_leaf_fixture {
    struct micros_process_handle process;
    uint64_t root;
    uint64_t physical;
    struct micros_frame_owner transferable;
    struct micros_user_address_space_vm_scratch_authority authority;
    struct micros_sv39_decoded_pte leaf;
    enum micros_user_address_space_leaf_class classification;
};

static struct micros_frame_owner owner(enum micros_frame_owner_kind kind, uint16_t slot,
                                       uint32_t generation) {
    return (struct micros_frame_owner){
        .generation = generation,
        .slot = slot,
        .kind = (uint8_t)kind,
        .reserved = 0,
    };
}

static bool leaf_owner_kind_follows_phase(void) {
    enum micros_frame_owner_kind kind = MICROS_FRAME_OWNER_KERNEL_RETAINED;

    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_leaf_owner_kind(MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP, &kind));
    EXPECT_TRUE(kind == MICROS_FRAME_OWNER_PROCESS_USER);
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_leaf_owner_kind(MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF, &kind));
    EXPECT_TRUE(kind == MICROS_FRAME_OWNER_VM_WIRED);

    kind = MICROS_FRAME_OWNER_KERNEL_RETAINED;
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT,
        micros_user_address_space_leaf_owner_kind(MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP, NULL));
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_PHASE,
        micros_user_address_space_leaf_owner_kind((enum micros_frame_ownership_phase)0, &kind));
    EXPECT_TRUE(kind == MICROS_FRAME_OWNER_KERNEL_RETAINED);
    return true;
}

static bool valid_frame_owners_are_accepted(void) {
    const struct micros_process_handle process = {
        .slot = 2,
        .generation = 7,
    };

    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP, MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE,
            process,
            owner(MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE, process.slot, process.generation)));
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF, MICROS_USER_ADDRESS_SPACE_FRAME_PRIVATE_TABLE,
            process,
            owner(MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE, process.slot, process.generation)));
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP, MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process, owner(MICROS_FRAME_OWNER_PROCESS_USER, process.slot, process.generation)));
    EXPECT_ADDRESS_ERROR(MICROS_USER_ADDRESS_SPACE_OK,
                         micros_user_address_space_validate_frame_owner(
                             MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
                             MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF, process,
                             owner(MICROS_FRAME_OWNER_VM_WIRED, process.slot, process.generation)));
    return true;
}

static bool mismatched_frame_owners_are_rejected(void) {
    const struct micros_process_handle process = {
        .slot = 2,
        .generation = 7,
    };

    EXPECT_ADDRESS_ERROR(MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
                         micros_user_address_space_validate_frame_owner(
                             MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
                             MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF, process,
                             owner(MICROS_FRAME_OWNER_VM_TRANSFERABLE, 0, 0)));
    EXPECT_ADDRESS_ERROR(MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
                         micros_user_address_space_validate_frame_owner(
                             MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
                             MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF, process,
                             owner(MICROS_FRAME_OWNER_FREE, 0, 0)));
    EXPECT_ADDRESS_ERROR(MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
                         micros_user_address_space_validate_frame_owner(
                             MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
                             MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF, process,
                             owner(MICROS_FRAME_OWNER_KERNEL_RETAINED, 0, 0)));
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF, MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process, owner(MICROS_FRAME_OWNER_PROCESS_USER, process.slot, process.generation)));
    EXPECT_ADDRESS_ERROR(MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
                         micros_user_address_space_validate_frame_owner(
                             MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP,
                             MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF, process,
                             owner(MICROS_FRAME_OWNER_VM_WIRED, process.slot, process.generation)));
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF, MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process, owner(MICROS_FRAME_OWNER_VM_WIRED, process.slot + 1, process.generation)));
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF, MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process, owner(MICROS_FRAME_OWNER_VM_WIRED, process.slot, process.generation + 1)));
    EXPECT_ADDRESS_ERROR(MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
                         micros_user_address_space_validate_frame_owner(
                             MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
                             MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF, process,
                             (struct micros_frame_owner){
                                 .generation = process.generation,
                                 .slot = process.slot,
                                 .kind = MICROS_FRAME_OWNER_VM_WIRED,
                                 .reserved = 1,
                             }));
    return true;
}

static bool invalid_owner_arguments_are_rejected(void) {
    const struct micros_process_handle process = {
        .slot = 2,
        .generation = 7,
    };

    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP, (enum micros_user_address_space_frame_role)0,
            process, owner(MICROS_FRAME_OWNER_PROCESS_USER, process.slot, process.generation)));
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_PHASE,
        micros_user_address_space_validate_frame_owner(
            (enum micros_frame_ownership_phase)0, MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            process, owner(MICROS_FRAME_OWNER_PROCESS_USER, process.slot, process.generation)));
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT,
        micros_user_address_space_validate_frame_owner(
            MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP, MICROS_USER_ADDRESS_SPACE_FRAME_USER_LEAF,
            (struct micros_process_handle){0}, owner(MICROS_FRAME_OWNER_PROCESS_USER, 0, 0)));
    return true;
}

static bool initialize_scratch_leaf_fixture(struct scratch_leaf_fixture *fixture) {
    uint64_t pte;

    *fixture = (struct scratch_leaf_fixture){
        .process =
            {
                .slot = 2,
                .generation = 7,
            },
        .root = UINT64_C(0x00200000),
        .physical = UINT64_C(0x00300000),
        .transferable =
            {
                .kind = MICROS_FRAME_OWNER_VM_TRANSFERABLE,
            },
        .authority =
            {
                .active = true,
                .load_token = 11,
                .process =
                    {
                        .slot = 2,
                        .generation = 7,
                    },
                .root_physical_address = UINT64_C(0x00200000),
                .virtual_address = MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
                .physical_address = UINT64_C(0x00300000),
            },
        .classification = MICROS_USER_ADDRESS_SPACE_LEAF_REGULAR,
    };
    EXPECT_TRUE(micros_sv39_make_leaf_pte(fixture->physical,
                                          MICROS_SV39_PERMISSION_READ |
                                              MICROS_SV39_PERMISSION_WRITE |
                                              MICROS_SV39_PERMISSION_USER,
                                          &pte) == MICROS_SV39_OK &&
                micros_sv39_decode_pte(pte, &fixture->leaf) == MICROS_SV39_OK);
    return true;
}

static bool exact_scratch_authority_is_accepted(struct scratch_leaf_fixture *fixture) {
    EXPECT_ADDRESS_ERROR(MICROS_USER_ADDRESS_SPACE_OK,
                         micros_user_address_space_classify_managed_leaf(
                             MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF, fixture->process,
                             fixture->root, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, &fixture->leaf,
                             fixture->transferable, &fixture->authority, &fixture->classification));
    EXPECT_TRUE(fixture->classification == MICROS_USER_ADDRESS_SPACE_LEAF_VM_SCRATCH);
    return true;
}

static bool mismatched_scratch_root_is_rejected(struct scratch_leaf_fixture *fixture) {
    fixture->classification = MICROS_USER_ADDRESS_SPACE_LEAF_REGULAR;
    fixture->authority.root_physical_address += MICROS_SV39_PAGE_SIZE;
    EXPECT_ADDRESS_ERROR(MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
                         micros_user_address_space_classify_managed_leaf(
                             MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF, fixture->process,
                             fixture->root, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, &fixture->leaf,
                             fixture->transferable, &fixture->authority, &fixture->classification));
    EXPECT_TRUE(fixture->classification == MICROS_USER_ADDRESS_SPACE_LEAF_REGULAR);
    fixture->authority.root_physical_address = fixture->root;
    return true;
}

static bool invalid_scratch_permissions_are_rejected(struct scratch_leaf_fixture *fixture) {
    fixture->leaf.permissions = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_USER;
    EXPECT_ADDRESS_ERROR(MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
                         micros_user_address_space_classify_managed_leaf(
                             MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF, fixture->process,
                             fixture->root, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, &fixture->leaf,
                             fixture->transferable, &fixture->authority, &fixture->classification));
    fixture->leaf.permissions =
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE | MICROS_SV39_PERMISSION_USER;
    return true;
}

static bool scratch_owner_is_exact(struct scratch_leaf_fixture *fixture) {
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP,
        micros_user_address_space_classify_managed_leaf(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF, fixture->process, fixture->root,
            MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, &fixture->leaf,
            owner(MICROS_FRAME_OWNER_VM_WIRED, fixture->process.slot, fixture->process.generation),
            &fixture->authority, &fixture->classification));
    return true;
}

static bool ordinary_wired_leaf_remains_regular(struct scratch_leaf_fixture *fixture) {
    EXPECT_ADDRESS_ERROR(
        MICROS_USER_ADDRESS_SPACE_OK,
        micros_user_address_space_classify_managed_leaf(
            MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF, fixture->process, fixture->root,
            MICROS_APPLICATION_ENTRY, &fixture->leaf,
            owner(MICROS_FRAME_OWNER_VM_WIRED, fixture->process.slot, fixture->process.generation),
            &fixture->authority, &fixture->classification));
    EXPECT_TRUE(fixture->classification == MICROS_USER_ADDRESS_SPACE_LEAF_REGULAR);
    return true;
}

static bool vm_scratch_leaf_classification_is_exact(void) {
    struct scratch_leaf_fixture fixture;

    return (initialize_scratch_leaf_fixture(&fixture) &&
            exact_scratch_authority_is_accepted(&fixture) &&
            mismatched_scratch_root_is_rejected(&fixture) &&
            invalid_scratch_permissions_are_rejected(&fixture) &&
            scratch_owner_is_exact(&fixture) && ordinary_wired_leaf_remains_regular(&fixture));
}

bool micros_user_address_space_owner_test_run(void) {
    return (leaf_owner_kind_follows_phase() && valid_frame_owners_are_accepted() &&
            mismatched_frame_owners_are_rejected() && invalid_owner_arguments_are_rejected() &&
            vm_scratch_leaf_classification_is_exact());
}
