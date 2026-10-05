#include "micros/sv39.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

_Static_assert(
    MICROS_SV39_PAGE_SIZE == 4096,
    "Sv39 base pages must be 4096 bytes"
);
_Static_assert(
    MICROS_SV39_TABLE_ENTRY_COUNT == 512,
    "Sv39 tables must contain 512 entries"
);
_Static_assert(
    MICROS_SV39_LEVEL_COUNT == 3,
    "Sv39 must use three page-table levels"
);

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
        enum micros_sv39_error actual_error = (expression); \
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

static bool test_validates_canonical_virtual_addresses(void)
{
    EXPECT_TRUE(
        micros_sv39_virtual_address_is_canonical(
            UINT64_C(0x0000000000000000)
        )
    );
    EXPECT_TRUE(
        micros_sv39_virtual_address_is_canonical(
            UINT64_C(0x0000003fffffffff)
        )
    );
    EXPECT_TRUE(
        !micros_sv39_virtual_address_is_canonical(
            UINT64_C(0x0000004000000000)
        )
    );
    EXPECT_TRUE(
        !micros_sv39_virtual_address_is_canonical(
            UINT64_C(0xffffffbfffffffff)
        )
    );
    EXPECT_TRUE(
        micros_sv39_virtual_address_is_canonical(
            UINT64_C(0xffffffc000000000)
        )
    );
    EXPECT_TRUE(
        micros_sv39_virtual_address_is_canonical(
            UINT64_C(0xffffffffffffffff)
        )
    );
    return true;
}

static bool test_extracts_each_vpn_index(void)
{
    uint64_t virtual_address =
        (UINT64_C(0x0aa) << 30)
        | (UINT64_C(0x155) << 21)
        | (UINT64_C(0x1ab) << 12)
        | UINT64_C(0x789);
    uint16_t index = UINT16_MAX;

    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_vpn_index(virtual_address, 0, &index)
    );
    EXPECT_TRUE(index == UINT16_C(0x1ab));
    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_vpn_index(virtual_address, 1, &index)
    );
    EXPECT_TRUE(index == UINT16_C(0x155));
    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_vpn_index(virtual_address, 2, &index)
    );
    EXPECT_TRUE(index == UINT16_C(0x0aa));
    EXPECT_ERROR(
        MICROS_SV39_ERROR_LEVEL,
        micros_sv39_vpn_index(virtual_address, 3, &index)
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_ARGUMENT,
        micros_sv39_vpn_index(virtual_address, 0, NULL)
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_NONCANONICAL,
        micros_sv39_vpn_index(
            UINT64_C(0x0000004000000000),
            0,
            &index
        )
    );
    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_vpn_index(
            UINT64_C(0xffffffc000000000),
            2,
            &index
        )
    );
    EXPECT_TRUE(index == UINT16_C(0x100));
    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_vpn_index(UINT64_MAX, 0, &index)
    );
    EXPECT_TRUE(index == UINT16_C(0x1ff));
    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_vpn_index(UINT64_MAX, 1, &index)
    );
    EXPECT_TRUE(index == UINT16_C(0x1ff));
    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_vpn_index(UINT64_MAX, 2, &index)
    );
    EXPECT_TRUE(index == UINT16_C(0x1ff));
    EXPECT_ERROR(
        MICROS_SV39_ERROR_NONCANONICAL,
        micros_sv39_vpn_index(
            UINT64_C(0xffffffbfffffffff),
            2,
            &index
        )
    );
    return true;
}

static bool test_encodes_table_entries(void)
{
    uint64_t pte = UINT64_MAX;

    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_make_table_pte(UINT64_C(0x12345000), &pte)
    );
    EXPECT_TRUE(pte == UINT64_C(0x00000000048d1401));
    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_make_table_pte(
            UINT64_C(0x00fffffffffff000),
            &pte
        )
    );
    EXPECT_TRUE(pte == UINT64_C(0x003ffffffffffc01));
    EXPECT_ERROR(
        MICROS_SV39_ERROR_UNALIGNED,
        micros_sv39_make_table_pte(UINT64_C(0x12345001), &pte)
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_PHYSICAL_RANGE,
        micros_sv39_make_table_pte(
            UINT64_C(0x0100000000000000),
            &pte
        )
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_ARGUMENT,
        micros_sv39_make_table_pte(UINT64_C(0x12345000), NULL)
    );
    return true;
}

static bool test_encodes_leaf_permissions(void)
{
    static const struct {
        uint32_t permissions;
        uint64_t expected;
    } cases[] = {
        {
            MICROS_SV39_PERMISSION_READ,
            UINT64_C(0x00000000048d1443),
        },
        {
            MICROS_SV39_PERMISSION_EXECUTE,
            UINT64_C(0x00000000048d1449),
        },
        {
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE,
            UINT64_C(0x00000000048d144b),
        },
        {
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE,
            UINT64_C(0x00000000048d14c7),
        },
        {
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_USER,
            UINT64_C(0x00000000048d1453),
        },
        {
            MICROS_SV39_PERMISSION_EXECUTE
                | MICROS_SV39_PERMISSION_USER,
            UINT64_C(0x00000000048d1459),
        },
        {
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
                | MICROS_SV39_PERMISSION_USER,
            UINT64_C(0x00000000048d145b),
        },
        {
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_USER,
            UINT64_C(0x00000000048d14d7),
        },
    };
    uint64_t pte = UINT64_MAX;
    size_t index;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        EXPECT_ERROR(
            MICROS_SV39_OK,
            micros_sv39_make_leaf_pte(
                UINT64_C(0x12345000),
                cases[index].permissions,
                &pte
            )
        );
        EXPECT_TRUE(pte == cases[index].expected);
    }
    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_make_leaf_pte(
            UINT64_C(0x00fffffffffff000),
            MICROS_SV39_PERMISSION_READ,
            &pte
        )
    );
    EXPECT_TRUE(pte == UINT64_C(0x003ffffffffffc43));
    return true;
}

static bool test_rejects_invalid_leaf_permissions(void)
{
    uint64_t pte = UINT64_MAX;

    EXPECT_ERROR(
        MICROS_SV39_ERROR_PERMISSIONS,
        micros_sv39_make_leaf_pte(
            UINT64_C(0x12345000),
            0,
            &pte
        )
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_PERMISSIONS,
        micros_sv39_make_leaf_pte(
            UINT64_C(0x12345000),
            MICROS_SV39_PERMISSION_USER,
            &pte
        )
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_PERMISSIONS,
        micros_sv39_make_leaf_pte(
            UINT64_C(0x12345000),
            MICROS_SV39_PERMISSION_WRITE,
            &pte
        )
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_PERMISSIONS,
        micros_sv39_make_leaf_pte(
            UINT64_C(0x12345000),
            MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_USER,
            &pte
        )
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_PERMISSIONS,
        micros_sv39_make_leaf_pte(
            UINT64_C(0x12345000),
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_EXECUTE,
            &pte
        )
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_PERMISSIONS,
        micros_sv39_make_leaf_pte(
            UINT64_C(0x12345000),
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
                | MICROS_SV39_PERMISSION_EXECUTE
                | MICROS_SV39_PERMISSION_USER,
            &pte
        )
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_PERMISSIONS,
        micros_sv39_make_leaf_pte(
            UINT64_C(0x12345000),
            UINT32_C(1) << 4,
            &pte
        )
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_UNALIGNED,
        micros_sv39_make_leaf_pte(
            UINT64_C(0x12345001),
            MICROS_SV39_PERMISSION_READ,
            &pte
        )
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_PHYSICAL_RANGE,
        micros_sv39_make_leaf_pte(
            UINT64_C(0x0100000000000000),
            MICROS_SV39_PERMISSION_READ,
            &pte
        )
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_ARGUMENT,
        micros_sv39_make_leaf_pte(
            UINT64_C(0x12345000),
            MICROS_SV39_PERMISSION_READ,
            NULL
        )
    );
    return true;
}

static bool test_decodes_absent_table_and_leaf_entries(void)
{
    struct micros_sv39_decoded_pte decoded;

    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_decode_pte(0, &decoded)
    );
    EXPECT_TRUE(decoded.kind == MICROS_SV39_PTE_ABSENT);
    EXPECT_TRUE(decoded.physical_address == 0);
    EXPECT_TRUE(decoded.permissions == 0);
    EXPECT_TRUE(!decoded.accessed);
    EXPECT_TRUE(!decoded.dirty);

    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_decode_pte(
            UINT64_C(0x00000000048d1401),
            &decoded
        )
    );
    EXPECT_TRUE(decoded.kind == MICROS_SV39_PTE_TABLE);
    EXPECT_TRUE(decoded.physical_address == UINT64_C(0x12345000));
    EXPECT_TRUE(decoded.permissions == 0);
    EXPECT_TRUE(!decoded.accessed);
    EXPECT_TRUE(!decoded.dirty);

    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_decode_pte(
            UINT64_C(0x00000000048d145b),
            &decoded
        )
    );
    EXPECT_TRUE(decoded.kind == MICROS_SV39_PTE_LEAF);
    EXPECT_TRUE(decoded.physical_address == UINT64_C(0x12345000));
    EXPECT_TRUE(
        decoded.permissions
            == (
                MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
                | MICROS_SV39_PERMISSION_USER
            )
    );
    EXPECT_TRUE(decoded.accessed);
    EXPECT_TRUE(!decoded.dirty);

    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_decode_pte(
            UINT64_C(0x00000000048d14c7),
            &decoded
        )
    );
    EXPECT_TRUE(decoded.kind == MICROS_SV39_PTE_LEAF);
    EXPECT_TRUE(decoded.accessed);
    EXPECT_TRUE(decoded.dirty);

    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_decode_pte(
            UINT64_C(0x003ffffffffffc01),
            &decoded
        )
    );
    EXPECT_TRUE(decoded.kind == MICROS_SV39_PTE_TABLE);
    EXPECT_TRUE(
        decoded.physical_address
            == UINT64_C(0x00fffffffffff000)
    );

    EXPECT_ERROR(
        MICROS_SV39_OK,
        micros_sv39_decode_pte(
            UINT64_C(0x003ffffffffffc43),
            &decoded
        )
    );
    EXPECT_TRUE(decoded.kind == MICROS_SV39_PTE_LEAF);
    EXPECT_TRUE(
        decoded.physical_address
            == UINT64_C(0x00fffffffffff000)
    );
    EXPECT_TRUE(decoded.permissions == MICROS_SV39_PERMISSION_READ);
    EXPECT_TRUE(decoded.accessed);
    EXPECT_TRUE(!decoded.dirty);
    return true;
}

static bool test_rejects_malformed_entries(void)
{
    struct micros_sv39_decoded_pte decoded;

    EXPECT_ERROR(
        MICROS_SV39_ERROR_MALFORMED_PTE,
        micros_sv39_decode_pte(UINT64_C(0x2), &decoded)
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_MALFORMED_PTE,
        micros_sv39_decode_pte(UINT64_C(0x101), &decoded)
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_MALFORMED_PTE,
        micros_sv39_decode_pte(UINT64_C(0x201), &decoded)
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_MALFORMED_PTE,
        micros_sv39_decode_pte(UINT64_C(0x41), &decoded)
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_MALFORMED_PTE,
        micros_sv39_decode_pte(UINT64_C(0x11), &decoded)
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_MALFORMED_PTE,
        micros_sv39_decode_pte(UINT64_C(0x21), &decoded)
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_MALFORMED_PTE,
        micros_sv39_decode_pte(UINT64_C(0x81), &decoded)
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_MALFORMED_PTE,
        micros_sv39_decode_pte(UINT64_C(0x63), &decoded)
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_MALFORMED_PTE,
        micros_sv39_decode_pte(UINT64_C(0x5), &decoded)
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_MALFORMED_PTE,
        micros_sv39_decode_pte(UINT64_C(0xf), &decoded)
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_MALFORMED_PTE,
        micros_sv39_decode_pte(UINT64_C(1) << 10, &decoded)
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_MALFORMED_PTE,
        micros_sv39_decode_pte(
            (UINT64_C(1) << 54) | UINT64_C(1),
            &decoded
        )
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_MALFORMED_PTE,
        micros_sv39_decode_pte(
            (UINT64_C(1) << 63) | UINT64_C(1),
            &decoded
        )
    );
    EXPECT_ERROR(
        MICROS_SV39_ERROR_ARGUMENT,
        micros_sv39_decode_pte(UINT64_C(1), NULL)
    );
    return true;
}

struct test_case {
    const char *name;
    bool (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        {
            "validates canonical virtual addresses",
            test_validates_canonical_virtual_addresses,
        },
        {
            "extracts each VPN index",
            test_extracts_each_vpn_index,
        },
        {
            "encodes table entries",
            test_encodes_table_entries,
        },
        {
            "encodes leaf permissions",
            test_encodes_leaf_permissions,
        },
        {
            "rejects invalid leaf permissions",
            test_rejects_invalid_leaf_permissions,
        },
        {
            "decodes absent, table, and leaf entries",
            test_decodes_absent_table_and_leaf_entries,
        },
        {
            "rejects malformed entries",
            test_rejects_malformed_entries,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(stderr, "not ok %zu - %s\n", index + 1, tests[index].name);
            return 1;
        }
        printf("ok %zu - %s\n", index + 1, tests[index].name);
    }
    return 0;
}
