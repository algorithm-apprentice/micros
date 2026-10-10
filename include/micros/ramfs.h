#ifndef MICROS_RAMFS_H
#define MICROS_RAMFS_H

#include <stddef.h>
#include <stdint.h>

enum {
    MICROS_RAMFS_PROTOCOL_VERSION = 1,
    MICROS_RAMFS_NODE_CAPACITY = 64,
    MICROS_RAMFS_BLOCK_SIZE = 4096,
    MICROS_RAMFS_BLOCK_CAPACITY = 64,
    MICROS_RAMFS_FILE_SIZE_MAX = 262144,
    MICROS_RAMFS_TRANSFER_MAX = 4096,
    MICROS_RAMFS_PATH_MAX = 4096,
    MICROS_RAMFS_NAME_MAX = 60,
    MICROS_RAMFS_DIRECTORY_RECORD_SIZE = 128,
    MICROS_RAMFS_DIRECTORY_CURSOR_END = 66,
    MICROS_RAMFS_SEED_HEADER_SIZE = 64,
    MICROS_RAMFS_SEED_ENTRY_SIZE = 128,
    MICROS_RAMFS_SEED_ENTRY_CAPACITY = 64,
    MICROS_RAMFS_SEED_DATA_OFFSET = 8256,
    MICROS_RAMFS_SEED_IMAGE_MAX = 270400,
    MICROS_RAMFS_SEED_DIGEST_OFFSET = 24,
    MICROS_RAMFS_RESIDENT_PAGE_LIMIT = 192,
    MICROS_RAMFS_SERVICE_ID = 5,
    MICROS_RAMFS_PROCESS_SLOT = 4,
};

#define MICROS_RAMFS_MODE_TYPE_MASK UINT32_C(0x0000f000)
#define MICROS_RAMFS_MODE_DIRECTORY UINT32_C(0x00004000)
#define MICROS_RAMFS_MODE_REGULAR UINT32_C(0x00008000)
#define MICROS_RAMFS_MODE_PERMISSIONS UINT32_C(0x000001ff)

#define MICROS_RAMFS_MESSAGE_MOUNT UINT32_C(0x00030001)
#define MICROS_RAMFS_MESSAGE_LOOKUP UINT32_C(0x00030002)
#define MICROS_RAMFS_MESSAGE_CREATE UINT32_C(0x00030003)
#define MICROS_RAMFS_MESSAGE_MKDIR UINT32_C(0x00030004)
#define MICROS_RAMFS_MESSAGE_READ UINT32_C(0x00030005)
#define MICROS_RAMFS_MESSAGE_WRITE UINT32_C(0x00030006)
#define MICROS_RAMFS_MESSAGE_GETDENTS UINT32_C(0x00030007)
#define MICROS_RAMFS_MESSAGE_PUTNODE UINT32_C(0x00030008)
#define MICROS_RAMFS_MESSAGE_RESULT UINT32_C(0x00030009)

#define MICROS_RAMFS_SEED_MAGIC UINT32_C(0x31534652)
#define MICROS_RAMFS_SEED_VERSION UINT16_C(1)
#define MICROS_RAMFS_SEED_FNV_OFFSET UINT64_C(14695981039346656037)
#define MICROS_RAMFS_SEED_FNV_PRIME UINT64_C(1099511628211)

enum micros_ramfs_result {
    MICROS_RAMFS_RESULT_REFERENCE = -14,
    MICROS_RAMFS_RESULT_RANGE = -13,
    MICROS_RAMFS_RESULT_GRANT = -12,
    MICROS_RAMFS_RESULT_NO_SPACE = -11,
    MICROS_RAMFS_RESULT_IS_DIRECTORY = -10,
    MICROS_RAMFS_RESULT_NOT_DIRECTORY = -9,
    MICROS_RAMFS_RESULT_EXISTS = -8,
    MICROS_RAMFS_RESULT_NOT_FOUND = -7,
    MICROS_RAMFS_RESULT_NODE = -6,
    MICROS_RAMFS_RESULT_STATE = -5,
    MICROS_RAMFS_RESULT_CALLER = -4,
    MICROS_RAMFS_RESULT_MALFORMED = -3,
    MICROS_RAMFS_RESULT_BAD_VERSION = -2,
    MICROS_RAMFS_RESULT_BAD_TYPE = -1,
    MICROS_RAMFS_RESULT_OK = 0,
};

struct micros_ramfs_seed_header {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint16_t entry_size;
    uint16_t entry_capacity;
    uint16_t entry_count;
    uint16_t reserved0;
    uint32_t data_size;
    uint32_t image_size;
    uint64_t digest;
    uint64_t reserved[4];
};

struct micros_ramfs_seed_entry {
    uint16_t parent_index;
    uint16_t name_length;
    uint32_t mode;
    uint32_t data_offset;
    uint32_t data_size;
    uint8_t name[64];
    uint64_t reserved[6];
};

_Static_assert(
    sizeof(struct micros_ramfs_seed_header)
        == MICROS_RAMFS_SEED_HEADER_SIZE,
    "RAMFS seed header ABI drift"
);
_Static_assert(
    offsetof(struct micros_ramfs_seed_header, digest)
        == MICROS_RAMFS_SEED_DIGEST_OFFSET,
    "RAMFS seed digest offset drift"
);
_Static_assert(
    sizeof(struct micros_ramfs_seed_entry)
        == MICROS_RAMFS_SEED_ENTRY_SIZE,
    "RAMFS seed entry ABI drift"
);
_Static_assert(
    offsetof(struct micros_ramfs_seed_entry, name) == 16,
    "RAMFS seed name offset drift"
);
_Static_assert(
    MICROS_RAMFS_SEED_DATA_OFFSET
        == MICROS_RAMFS_SEED_HEADER_SIZE
            + MICROS_RAMFS_SEED_ENTRY_CAPACITY
                * MICROS_RAMFS_SEED_ENTRY_SIZE,
    "RAMFS seed payload offset drift"
);
_Static_assert(
    MICROS_RAMFS_SEED_IMAGE_MAX
        == MICROS_RAMFS_SEED_DATA_OFFSET
            + MICROS_RAMFS_FILE_SIZE_MAX,
    "RAMFS seed image maximum drift"
);

#endif
