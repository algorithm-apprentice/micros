#include "endpoint_test_internal.h"

#include <string.h>

bool endpoint_test_encoding_boundaries(void)
{
    struct micros_process_handle process = {0, 1};
    struct micros_process_handle decoded = {UINT16_MAX, UINT32_MAX};
    micros_endpoint_t endpoint = UINT32_C(0xdeadbeef);

    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_pack(process, &endpoint)
    );
    EXPECT_TRUE(endpoint == UINT32_C(0x00001000));
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_unpack(endpoint, &decoded)
    );
    EXPECT_TRUE(decoded.slot == 0 && decoded.generation == 1);

    process.slot = UINT16_C(0x0ffd);
    process.generation = MICROS_ENDPOINT_GENERATION_MAX;
    EXPECT_ERROR(
        MICROS_ENDPOINT_OK,
        micros_endpoint_pack(process, &endpoint)
    );
    EXPECT_TRUE(endpoint == UINT32_C(0xfffffffd));

    process.slot = UINT16_C(0x0ffe);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    process.slot = UINT16_C(0x0fff);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    process.slot = UINT16_C(0x1000);
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    process.slot = 0;
    process.generation = 0;
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    process.generation = MICROS_ENDPOINT_GENERATION_MAX + 1;
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_ENDPOINT,
        micros_endpoint_pack(process, &endpoint)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STALE,
        micros_endpoint_unpack(MICROS_ENDPOINT_NONE, &decoded)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STALE,
        micros_endpoint_unpack(MICROS_ENDPOINT_ANY, &decoded)
    );
    EXPECT_ERROR(
        MICROS_ENDPOINT_ERROR_STALE,
        micros_endpoint_unpack(0, &decoded)
    );
    return true;
}

bool endpoint_test_resolution_rejects_unimplemented_slots(void)
{
    struct endpoint_test_fixture *fixture = endpoint_test_context();
    static const uint16_t slots[] = {
        MICROS_PROCESS_CAPACITY - 1,
        MICROS_PROCESS_CAPACITY,
        (UINT16_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1,
    };
    const struct micros_endpoint_record *record;
    size_t index;

    EXPECT_TRUE(endpoint_test_setup_lifecycle_fixture(fixture));
    for (index = 0; index < sizeof(slots) / sizeof(slots[0]); ++index) {
        struct micros_process_handle owner = {slots[index], 1};
        micros_endpoint_t endpoint;

        record =
            (const struct micros_endpoint_record *)(uintptr_t)1;
        EXPECT_ERROR(
            MICROS_ENDPOINT_OK,
            micros_endpoint_pack(owner, &endpoint)
        );
        EXPECT_ERROR(
            MICROS_ENDPOINT_ERROR_STALE,
            micros_endpoint_resolve_internal(
                &fixture->registry,
                &fixture->objects,
                endpoint,
                &record
            )
        );
        EXPECT_TRUE(
            record
                == (const struct micros_endpoint_record *)(uintptr_t)1
        );
    }
    return true;
}
