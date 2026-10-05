#ifndef MICROS_SV39_H
#define MICROS_SV39_H

#include <stdbool.h>
#include <stdint.h>

enum {
    MICROS_SV39_PAGE_SIZE = 4096,
    MICROS_SV39_TABLE_ENTRY_COUNT = 512,
    MICROS_SV39_LEVEL_COUNT = 3,
};

enum micros_sv39_permission {
    MICROS_SV39_PERMISSION_READ = 1U << 0,
    MICROS_SV39_PERMISSION_WRITE = 1U << 1,
    MICROS_SV39_PERMISSION_EXECUTE = 1U << 2,
    MICROS_SV39_PERMISSION_USER = 1U << 3,
};

enum micros_sv39_error {
    MICROS_SV39_OK = 0,
    MICROS_SV39_ERROR_ARGUMENT,
    MICROS_SV39_ERROR_NONCANONICAL,
    MICROS_SV39_ERROR_LEVEL,
    MICROS_SV39_ERROR_UNALIGNED,
    MICROS_SV39_ERROR_PHYSICAL_RANGE,
    MICROS_SV39_ERROR_PERMISSIONS,
    MICROS_SV39_ERROR_MALFORMED_PTE,
};

enum micros_sv39_pte_kind {
    MICROS_SV39_PTE_ABSENT = 0,
    MICROS_SV39_PTE_TABLE,
    MICROS_SV39_PTE_LEAF,
};

struct micros_sv39_decoded_pte {
    enum micros_sv39_pte_kind kind;
    uint64_t physical_address;
    uint32_t permissions;
    bool accessed;
    bool dirty;
};

bool micros_sv39_virtual_address_is_canonical(uint64_t virtual_address);

enum micros_sv39_error micros_sv39_vpn_index(
    uint64_t virtual_address,
    unsigned int level,
    uint16_t *index
);

enum micros_sv39_error micros_sv39_make_table_pte(
    uint64_t physical_address,
    uint64_t *pte
);

enum micros_sv39_error micros_sv39_make_leaf_pte(
    uint64_t physical_address,
    uint32_t permissions,
    uint64_t *pte
);

enum micros_sv39_error micros_sv39_decode_pte(
    uint64_t pte,
    struct micros_sv39_decoded_pte *decoded
);

#endif
