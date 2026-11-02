#define _DEFAULT_SOURCE 1

#ifdef MICROS_RUNTIME_MEMORY_ARTIFACT_FIXTURE
#include <stdint.h>

void memory_artifact_copy(void *restrict target, const void *restrict source);
void memory_artifact_fill(void *target, int byte);

void memory_artifact_copy(void *restrict target, const void *restrict source)
{
    __builtin_memcpy_inline(
        (uint64_t *)__builtin_assume_aligned(target, 8),
        (const uint64_t *)__builtin_assume_aligned(source, 8),
        8
    );
}

void memory_artifact_fill(void *target, int byte)
{
    _Alignas(4) uint32_t pattern = (unsigned char)byte;

    pattern |= pattern << 8;
    pattern |= pattern << 16;
    __builtin_memcpy_inline((uint32_t *)__builtin_assume_aligned(target, 4), &pattern, 4);
}
#else

#include "lib/runtime/memory.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef MICROS_TEST_WORK_COUNTS
#include <sys/mman.h>
#include <unistd.h>
#endif

#ifdef MICROS_TEST_WORK_COUNTS
#include "tests/work/work_counts.h"
static struct micros_test_work_snapshot snapshot;
static uint64_t capture_label;
#endif

_Alignas(8) static unsigned char source[364736];
_Alignas(8) static unsigned char destination[364736];
_Alignas(8) static unsigned char expected[364736];

static bool begin_capture(void)
{
#ifdef MICROS_TEST_WORK_COUNTS
    ++capture_label;
    return micros_test_work_begin(
        1, capture_label, UINT32_C(1) << MICROS_WORK_OWNER_RUNTIME
    ) == MICROS_TEST_WORK_OK;
#else
    return true;
#endif
}

static bool finish_capture(void)
{
#ifdef MICROS_TEST_WORK_COUNTS
    return micros_test_work_finish(&snapshot) == MICROS_TEST_WORK_OK;
#else
    return true;
#endif
}

static void print_capture(void)
{
#ifdef MICROS_TEST_WORK_COUNTS
    size_t owner;
    size_t kind;

    printf(
        "{\"status\":0,\"snapshot\":{\"version\":%" PRIu32
        ",\"reserved0\":%" PRIu32 ",\"producer_label\":%" PRIu64
        ",\"capture_label\":%" PRIu64 ",\"owner_mask\":%" PRIu32
        ",\"reserved1\":%" PRIu32 ",\"counts\":[",
        snapshot.version, snapshot.reserved0, snapshot.producer_label,
        snapshot.capture_label, snapshot.owner_mask, snapshot.reserved1
    );
    for (owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        printf("%s[", owner == 0 ? "" : ",");
        for (kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            printf("%s%" PRIu64, kind == 0 ? "" : ",", snapshot.counts[owner][kind]);
        }
        printf("]");
    }
    printf("]}}");
#else
    printf("null");
#endif
}

static bool run_case(
    bool copy,
    size_t length,
    size_t target_offset,
    size_t input_offset,
    int byte
)
{
    size_t index;
    size_t span;
    void *returned;
    bool semantic;
    const unsigned char value = (unsigned char)byte;

    if (input_offset > 7 || target_offset > 7
        || length > sizeof(source) - input_offset - 16
        || length > sizeof(destination) - target_offset - 16) {
        return false;
    }
    span = length + (target_offset > input_offset ? target_offset : input_offset) + 16;
    for (index = 0; index < span; ++index) {
        source[index] = (unsigned char)(index * 37U + 11U);
        destination[index] = UINT8_C(0x5c);
        expected[index] = UINT8_C(0x5c);
    }
    for (index = 0; index < length; ++index) {
        expected[target_offset + index] = copy ? source[input_offset + index] : value;
    }
    if (!begin_capture()) {
        return false;
    }
    returned = copy
        ? micros_runtime_memcpy(destination + target_offset, source + input_offset, length)
        : micros_runtime_memset(destination + target_offset, byte, length);
    if (!finish_capture()) {
        return false;
    }
    semantic = returned == destination + target_offset
        && memcmp(destination, expected, span) == 0;
    for (index = 0; index < span; ++index) {
        semantic = semantic && source[index] == (unsigned char)(index * 37U + 11U);
    }
    printf(
        "{\"operation\":\"%s\",\"length\":%zu,\"destination_offset\":%zu,"
        "\"source_offset\":%zu,\"value\":%d,\"semantic\":%s,\"capture\":",
        copy ? "copy" : "fill", length, target_offset, input_offset,
        byte, semantic ? "true" : "false"
    );
    print_capture();
    printf("}\n");
    return semantic;
}

static bool run_matrix(void)
{
    static const size_t lengths[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
        23, 24, 31, 32, 63, 64, 65, 255, 256, 257, 4095, 4096,
    };
    static const int values[] = {421, 0, -1, 256, -128};
    size_t length_index;
    size_t target_offset;
    size_t input_offset;
    size_t value_index;

    for (length_index = 0; length_index < sizeof(lengths) / sizeof(lengths[0]); ++length_index) {
        for (target_offset = 0; target_offset < 8; ++target_offset) {
            for (input_offset = 0; input_offset < 8; ++input_offset) {
                if (!run_case(true, lengths[length_index], target_offset, input_offset, 421)) {
                    return false;
                }
            }
        }
        for (target_offset = 0; target_offset < 4; ++target_offset) {
            for (value_index = 0; value_index < sizeof(values) / sizeof(values[0]); ++value_index) {
                if (!run_case(false, lengths[length_index], target_offset, 0, values[value_index])) {
                    return false;
                }
            }
        }
    }
    return run_case(true, 364704, 0, 0, 421)
        && run_case(true, 364704, 3, 3, 421)
        && run_case(false, 364704, 0, 0, 421);
}

#ifndef MICROS_TEST_WORK_COUNTS
static bool address_orders(void)
{
    static const size_t lengths[] = {0, 1, 7, 8, 9, 64, 65, 257, 4096};
    size_t direction;
    size_t length_index;
    size_t target_offset;
    size_t input_offset;
    size_t index;

    for (direction = 0; direction < 2; ++direction) {
        unsigned char *input = direction == 0 ? source : destination;
        unsigned char *target = direction == 0 ? destination : source;

        for (length_index = 0; length_index < sizeof(lengths) / sizeof(lengths[0]); ++length_index) {
            size_t length = lengths[length_index];

            for (target_offset = 0; target_offset < 8; ++target_offset) {
                for (input_offset = 0; input_offset < 8; ++input_offset) {
                    for (index = 0; index < length + 16; ++index) {
                        input[index] = (unsigned char)(index * 31U + 9U);
                        target[index] = UINT8_C(0x5c);
                    }
                    if (micros_runtime_memcpy(target + target_offset, input + input_offset, length)
                        != target + target_offset) {
                        return false;
                    }
                    for (index = 0; index < length + 16; ++index) {
                        unsigned char wanted = index >= target_offset && index - target_offset < length
                            ? (unsigned char)((index - target_offset + input_offset) * 31U + 9U)
                            : UINT8_C(0x5c);

                        if (target[index] != wanted || input[index] != (unsigned char)(index * 31U + 9U)) {
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

static bool typed_objects(void)
{
    struct typed_object {
        uint32_t number;
        double real;
        uint16_t values[7];
    } first, second;
    size_t index;
    const unsigned char *bytes = (const unsigned char *)&second;

    memset(&first, 0, sizeof(first));
    first.number = UINT32_C(0x12345678);
    first.real = 1.25;
    for (index = 0; index < 7; ++index) {
        first.values[index] = (uint16_t)(index * 93U + 17U);
    }
    if (micros_runtime_memcpy(&second, &first, sizeof(first)) != &second
        || second.number != first.number || second.real != first.real
        || memcmp(&first, &second, sizeof(first)) != 0
        || micros_runtime_memset(&second, -1, sizeof(second)) != &second) {
        return false;
    }
    for (index = 0; index < sizeof(second); ++index) {
        if (bytes[index] != UINT8_MAX) {
            return false;
        }
    }
    return true;
}

static bool guarded_copy(unsigned char *read_page, unsigned char *write_page, size_t page, size_t length)
{
    size_t offset;
    size_t index;
    unsigned char *read_end = read_page + page - length;
    unsigned char *write_end = write_page + page - length;

    for (offset = 0; offset < 8; ++offset) {
        memset(destination, 0x5c, length + offset + 8);
        if (micros_runtime_memcpy(destination + offset, read_end, length) != destination + offset
            || memcmp(destination + offset, read_end, length) != 0) {
            return false;
        }
        memset(write_page, 0x5c, page);
        if (micros_runtime_memcpy(write_end, source + offset, length) != write_end
            || memcmp(write_end, source + offset, length) != 0) {
            return false;
        }
        for (index = 0; index < page - length; ++index) {
            if (write_page[index] != 0x5c) {
                return false;
            }
        }
    }
    return true;
}

static bool guarded_fill(unsigned char *write_page, size_t page, size_t length)
{
    size_t index;
    unsigned char *end = write_page + page - length;

    memset(write_page, 0x5c, page);
    if (micros_runtime_memset(end, 0x1a5, length) != end) {
        return false;
    }
    for (index = 0; index < page; ++index) {
        unsigned char value = index < page - length ? UINT8_C(0x5c) : UINT8_C(0xa5);
        if (write_page[index] != value) {
            return false;
        }
    }
    return true;
}

static bool protected_pages(void)
{
    long reported_page = sysconf(_SC_PAGESIZE);
    size_t page;
    size_t index;
    size_t length;
    unsigned char *first;
    unsigned char *second;
    bool result = false;

    if (reported_page < 4096 || (uint64_t)reported_page > sizeof(source) - 16) {
        return false;
    }
    page = (size_t)reported_page;
    first = mmap(NULL, 3 * page, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    second = mmap(NULL, 3 * page, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (first == MAP_FAILED || second == MAP_FAILED) {
        goto done;
    }
    if (mprotect(first + page, page, PROT_READ | PROT_WRITE) != 0
        || mprotect(second + page, page, PROT_READ | PROT_WRITE) != 0) {
        goto done;
    }
    for (index = 0; index < page; ++index) {
        first[page + index] = (unsigned char)(index * 71U + 5U);
        source[index] = (unsigned char)(index * 31U + 9U);
    }
    for (length = 0; length <= 32; ++length) {
        if (!guarded_copy(first + page, second + page, page, length)
            || !guarded_fill(second + page, page, length)) {
            goto done;
        }
    }
    result = guarded_copy(first + page, second + page, page, page)
        && guarded_fill(second + page, page, page);
done:
    if (first != MAP_FAILED) {
        result = munmap(first, 3 * page) == 0 && result;
    }
    if (second != MAP_FAILED) {
        result = munmap(second, 3 * page) == 0 && result;
    }
    return result;
}

static bool access_controls(void)
{
    bool null_zero = micros_runtime_memcpy(NULL, NULL, 0) == NULL
        && micros_runtime_memset(NULL, -257, 0) == NULL;
    bool typed = typed_objects();
    bool guarded = protected_pages();
    bool orders = address_orders();

    printf(
        "{\"null_zero\":%s,\"typed\":%s,\"guarded\":%s,\"address_orders\":%s}\n",
        null_zero ? "true" : "false", typed ? "true" : "false",
        guarded ? "true" : "false", orders ? "true" : "false"
    );
    return null_zero && typed && guarded && orders;
}
#endif

int main(int argc, char **argv)
{
#ifndef MICROS_TEST_WORK_COUNTS
    if (argc == 2 && strcmp(argv[1], "--access") == 0) {
        return access_controls() ? 0 : 1;
    }
#else
    (void)argv;
#endif
    return argc == 1 && run_matrix() ? 0 : 1;
}

#endif
