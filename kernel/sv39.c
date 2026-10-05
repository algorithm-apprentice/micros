#include "micros/sv39.h"

#include <stddef.h>
#include <stdint.h>

enum {
    SV39_PTE_VALID = 1U << 0,
    SV39_PTE_READ = 1U << 1,
    SV39_PTE_WRITE = 1U << 2,
    SV39_PTE_EXECUTE = 1U << 3,
    SV39_PTE_USER = 1U << 4,
    SV39_PTE_GLOBAL = 1U << 5,
    SV39_PTE_ACCESSED = 1U << 6,
    SV39_PTE_DIRTY = 1U << 7,
};

#define SV39_PAGE_OFFSET_MASK UINT64_C(0xfff)
#define SV39_PHYSICAL_LIMIT (UINT64_C(1) << 56)
#define SV39_PTE_RSW_MASK (UINT64_C(3) << 8)
#define SV39_PTE_PPN_MASK (UINT64_C(0x00000fffffffffff) << 10)
#define SV39_PTE_RESERVED_HIGH_MASK (~((UINT64_C(1) << 54) - 1))

static bool physical_page_is_valid(uint64_t physical_address)
{
    return (
        (physical_address & SV39_PAGE_OFFSET_MASK) == 0
        && physical_address < SV39_PHYSICAL_LIMIT
    );
}

static bool permissions_are_valid(uint32_t permissions)
{
    const uint32_t supported =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_EXECUTE
        | MICROS_SV39_PERMISSION_USER;
    uint32_t access = permissions & (
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_EXECUTE
    );

    return (
        (permissions & ~supported) == 0
        && access != 0
        && (
            (permissions & MICROS_SV39_PERMISSION_WRITE) == 0
            || (permissions & MICROS_SV39_PERMISSION_READ) != 0
        )
        && !(
            (permissions & MICROS_SV39_PERMISSION_WRITE) != 0
            && (permissions & MICROS_SV39_PERMISSION_EXECUTE) != 0
        )
    );
}

static uint64_t encode_physical_page_number(uint64_t physical_address)
{
    return (physical_address >> 12) << 10;
}

bool micros_sv39_virtual_address_is_canonical(uint64_t virtual_address)
{
    uint64_t upper = virtual_address >> 39;
    bool sign = ((virtual_address >> 38) & UINT64_C(1)) != 0;

    if (!sign) {
        return upper == 0;
    }
    return upper == ((UINT64_C(1) << 25) - 1);
}

enum micros_sv39_error micros_sv39_vpn_index(
    uint64_t virtual_address,
    unsigned int level,
    uint16_t *index
)
{
    if (index == NULL) {
        return MICROS_SV39_ERROR_ARGUMENT;
    }
    if (level >= MICROS_SV39_LEVEL_COUNT) {
        return MICROS_SV39_ERROR_LEVEL;
    }
    if (!micros_sv39_virtual_address_is_canonical(virtual_address)) {
        return MICROS_SV39_ERROR_NONCANONICAL;
    }

    *index = (uint16_t)(
        (virtual_address >> (12U + (9U * level)))
        & UINT64_C(0x1ff)
    );
    return MICROS_SV39_OK;
}

enum micros_sv39_error micros_sv39_make_table_pte(
    uint64_t physical_address,
    uint64_t *pte
)
{
    if (pte == NULL) {
        return MICROS_SV39_ERROR_ARGUMENT;
    }
    if ((physical_address & SV39_PAGE_OFFSET_MASK) != 0) {
        return MICROS_SV39_ERROR_UNALIGNED;
    }
    if (!physical_page_is_valid(physical_address)) {
        return MICROS_SV39_ERROR_PHYSICAL_RANGE;
    }

    *pte = encode_physical_page_number(physical_address) | SV39_PTE_VALID;
    return MICROS_SV39_OK;
}

enum micros_sv39_error micros_sv39_make_leaf_pte(
    uint64_t physical_address,
    uint32_t permissions,
    uint64_t *pte
)
{
    uint64_t encoded_permissions = SV39_PTE_VALID | SV39_PTE_ACCESSED;

    if (pte == NULL) {
        return MICROS_SV39_ERROR_ARGUMENT;
    }
    if ((physical_address & SV39_PAGE_OFFSET_MASK) != 0) {
        return MICROS_SV39_ERROR_UNALIGNED;
    }
    if (!physical_page_is_valid(physical_address)) {
        return MICROS_SV39_ERROR_PHYSICAL_RANGE;
    }
    if (!permissions_are_valid(permissions)) {
        return MICROS_SV39_ERROR_PERMISSIONS;
    }

    if ((permissions & MICROS_SV39_PERMISSION_READ) != 0) {
        encoded_permissions |= SV39_PTE_READ;
    }
    if ((permissions & MICROS_SV39_PERMISSION_WRITE) != 0) {
        encoded_permissions |= SV39_PTE_WRITE | SV39_PTE_DIRTY;
    }
    if ((permissions & MICROS_SV39_PERMISSION_EXECUTE) != 0) {
        encoded_permissions |= SV39_PTE_EXECUTE;
    }
    if ((permissions & MICROS_SV39_PERMISSION_USER) != 0) {
        encoded_permissions |= SV39_PTE_USER;
    }

    *pte = encode_physical_page_number(physical_address)
        | encoded_permissions;
    return MICROS_SV39_OK;
}

enum micros_sv39_error micros_sv39_decode_pte(
    uint64_t pte,
    struct micros_sv39_decoded_pte *decoded
)
{
    uint32_t permissions = 0;
    bool read;
    bool write;
    bool execute;
    bool user;
    bool global;
    bool accessed;
    bool dirty;

    if (decoded == NULL) {
        return MICROS_SV39_ERROR_ARGUMENT;
    }
    if (pte == 0) {
        decoded->kind = MICROS_SV39_PTE_ABSENT;
        decoded->physical_address = 0;
        decoded->permissions = 0;
        decoded->accessed = false;
        decoded->dirty = false;
        return MICROS_SV39_OK;
    }
    if (
        (pte & SV39_PTE_VALID) == 0
        || (pte & SV39_PTE_RSW_MASK) != 0
        || (pte & SV39_PTE_RESERVED_HIGH_MASK) != 0
    ) {
        return MICROS_SV39_ERROR_MALFORMED_PTE;
    }

    read = (pte & SV39_PTE_READ) != 0;
    write = (pte & SV39_PTE_WRITE) != 0;
    execute = (pte & SV39_PTE_EXECUTE) != 0;
    user = (pte & SV39_PTE_USER) != 0;
    global = (pte & SV39_PTE_GLOBAL) != 0;
    accessed = (pte & SV39_PTE_ACCESSED) != 0;
    dirty = (pte & SV39_PTE_DIRTY) != 0;

    decoded->physical_address = (
        (pte & SV39_PTE_PPN_MASK) >> 10
    ) << 12;
    decoded->permissions = 0;
    decoded->accessed = accessed;
    decoded->dirty = dirty;

    if (!read && !write && !execute) {
        if (user || global || accessed || dirty) {
            return MICROS_SV39_ERROR_MALFORMED_PTE;
        }
        decoded->kind = MICROS_SV39_PTE_TABLE;
        return MICROS_SV39_OK;
    }

    if (
        global
        || !accessed
        || write != dirty
        || (write && !read)
        || (write && execute)
    ) {
        return MICROS_SV39_ERROR_MALFORMED_PTE;
    }
    if (read) {
        permissions |= MICROS_SV39_PERMISSION_READ;
    }
    if (write) {
        permissions |= MICROS_SV39_PERMISSION_WRITE;
    }
    if (execute) {
        permissions |= MICROS_SV39_PERMISSION_EXECUTE;
    }
    if (user) {
        permissions |= MICROS_SV39_PERMISSION_USER;
    }

    decoded->kind = MICROS_SV39_PTE_LEAF;
    decoded->permissions = permissions;
    return MICROS_SV39_OK;
}
