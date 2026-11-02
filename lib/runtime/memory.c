#include "lib/runtime/memory.h"
#include "micros/work_observation.h"

#include <stddef.h>
#include <stdint.h>

#ifndef __has_builtin
#error "runtime memory requires the supported Clang inline-copy primitives"
#elif !__has_builtin(__builtin_memcpy_inline) || !__has_builtin(__builtin_assume_aligned)
#error "runtime memory requires constant inline copy and aligned-pointer support"
#endif

void *micros_runtime_memcpy(
    void *restrict destination,
    const void *restrict source,
    size_t length
)
{
    unsigned char *target = destination;
    const unsigned char *input = source;
    size_t remaining = length;

    if (remaining == 0) {
        return destination;
    }
    if (((uintptr_t)target & 7U) != ((uintptr_t)input & 7U)) {
        while (remaining != 0) {
            *(volatile unsigned char *)target = *(const volatile unsigned char *)input;
            MICROS_TEST_WORK_HIT("copy-unaligned-unit", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_COPY_UNIT, 1);
            MICROS_TEST_WORK_HIT("copy-unaligned-byte", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_COPIED_BYTE, 1);
            ++target;
            ++input;
            --remaining;
        }
        return destination;
    }
    while (remaining != 0 && ((uintptr_t)target & 7U) != 0) {
        *(volatile unsigned char *)target = *(const volatile unsigned char *)input;
        MICROS_TEST_WORK_HIT("copy-prefix-unit", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_COPY_UNIT, 1);
        MICROS_TEST_WORK_HIT("copy-prefix-byte", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_COPIED_BYTE, 1);
        ++target;
        ++input;
        --remaining;
    }
    while (remaining >= 8) {
        __builtin_memcpy_inline(
            (uint64_t *)__builtin_assume_aligned(target, 8),
            (const uint64_t *)__builtin_assume_aligned(input, 8), 8
        );
        MICROS_TEST_WORK_HIT("copy-word-unit", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_COPY_UNIT, 1);
        MICROS_TEST_WORK_HIT("copy-word-byte", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_COPIED_BYTE, 8);
        target += 8;
        input += 8;
        remaining -= 8;
    }
    while (remaining != 0) {
        *(volatile unsigned char *)target = *(const volatile unsigned char *)input;
        MICROS_TEST_WORK_HIT("copy-tail-unit", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_COPY_UNIT, 1);
        MICROS_TEST_WORK_HIT("copy-tail-byte", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_COPIED_BYTE, 1);
        ++target;
        ++input;
        --remaining;
    }
    return destination;
}

void *micros_runtime_memset(
    void *destination,
    int byte,
    size_t length
)
{
    unsigned char *target = destination;
    unsigned char value;
    size_t remaining = length;
    _Alignas(4) uint32_t pattern;

    if (remaining == 0) {
        return destination;
    }
    value = (unsigned char)byte;
    if (remaining < 12) {
        while (remaining != 0) {
            *(volatile unsigned char *)target = value;
            MICROS_TEST_WORK_HIT("fill-short-unit", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_CLEAR_UNIT, 1);
            MICROS_TEST_WORK_HIT("fill-short-byte", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_CLEARED_BYTE, 1);
            ++target;
            --remaining;
        }
        return destination;
    }
    pattern = value;
    pattern |= pattern << 8;
    pattern |= pattern << 16;
    while (((uintptr_t)target & 3U) != 0) {
        *(volatile unsigned char *)target = value;
        MICROS_TEST_WORK_HIT("fill-prefix-unit", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_CLEAR_UNIT, 1);
        MICROS_TEST_WORK_HIT("fill-prefix-byte", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_CLEARED_BYTE, 1);
        ++target;
        --remaining;
    }
    while (remaining >= 4) {
        __builtin_memcpy_inline((uint32_t *)__builtin_assume_aligned(target, 4), &pattern, 4);
        MICROS_TEST_WORK_HIT("fill-word-unit", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_CLEAR_UNIT, 1);
        MICROS_TEST_WORK_HIT("fill-word-byte", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_CLEARED_BYTE, 4);
        target += 4;
        remaining -= 4;
    }
    while (remaining != 0) {
        *(volatile unsigned char *)target = value;
        MICROS_TEST_WORK_HIT("fill-tail-unit", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_CLEAR_UNIT, 1);
        MICROS_TEST_WORK_HIT("fill-tail-byte", MICROS_WORK_OWNER_RUNTIME, MICROS_WORK_KIND_CLEARED_BYTE, 1);
        ++target;
        --remaining;
    }
    return destination;
}
