#include "diagnostic_history_test.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/diagnostic/diagnostic_control_core.h"
#include "kernel/tty/tty_control_core.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

struct translation_fixture {
    _Alignas(4096) uint8_t pages[3][4096];
    unsigned calls;
    unsigned rejected_page;
    enum micros_user_address_space_error rejected_result;
    bool readonly;
    bool duplicate;
};

static struct translation_fixture translation;

enum micros_user_address_space_error micros_user_address_space_translate(
    struct micros_process_handle process,
    uint64_t address,
    uint64_t *physical_address,
    uint32_t *permissions,
    size_t *contiguous_bytes
)
{
    unsigned page = translation.calls++;
    unsigned physical_page = page == 0 ? 1 : (page == 1 ? 0 : 2);

    if (
        page >= 3 || process.slot != 3 || process.generation != 7
        || address != UINT64_C(0x40010000) + UINT64_C(4096) * page
        || physical_address == NULL || permissions == NULL || contiguous_bytes == NULL
    ) {
        abort();
    }
    if (page == translation.rejected_page && !translation.readonly) {
        return translation.rejected_result;
    }
    if (translation.duplicate) {
        physical_page = 1;
    }
    *physical_address = (uint64_t)(uintptr_t)translation.pages[physical_page];
    *permissions = MICROS_SV39_PERMISSION_READ;
    if (!translation.readonly || page != translation.rejected_page) {
        *permissions |= MICROS_SV39_PERMISSION_WRITE;
    }
    *contiguous_bytes = 4096;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

static void reset_translation(void)
{
    memset(&translation, 0, sizeof(translation));
    memset(translation.pages, 0xcd, sizeof(translation.pages));
    translation.rejected_page = 3;
    translation.rejected_result = MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
}

static struct micros_diagnostic_identity reader_identity(void)
{
    return (struct micros_diagnostic_identity){
        .service_id = 4,
        .process = {.slot = 3, .generation = 7},
        .thread = {.slot = 5, .generation = 9},
        .endpoint = UINT32_C(0x7003),
        .profile_id = 4,
        .root_physical_address = UINT64_C(0x80200000),
    };
}

static uint8_t destination_byte(size_t offset)
{
    size_t page = offset / 4096;
    size_t physical_page = page == 0 ? 1 : (page == 1 ? 0 : 2);

    return translation.pages[physical_page][offset % 4096];
}

static uint32_t destination_u32(size_t offset)
{
    return (uint32_t)destination_byte(offset)
        | (uint32_t)destination_byte(offset + 1) << 8
        | (uint32_t)destination_byte(offset + 2) << 16
        | (uint32_t)destination_byte(offset + 3) << 24;
}

static bool untouched_destination(size_t start)
{
    size_t index;

    for (index = start; index < 12288; ++index) {
        DIAGNOSTIC_EXPECT(destination_byte(index) == 0xcd);
    }
    return true;
}

static bool registration_and_three_page_wrapped_read(void)
{
    struct micros_diagnostic_identity identity = reader_identity();
    struct micros_diagnostic_reader reader = {0};
    struct micros_diagnostic_reader before;
    struct micros_diagnostic_history history = {0};
    uint32_t index;

    reset_translation();
    DIAGNOSTIC_EXPECT(micros_diagnostic_read(&reader, &identity, &history, 10000)
        == MICROS_DIAGNOSTIC_CONTROL_STATE);
    DIAGNOSTIC_EXPECT(micros_diagnostic_register(
        &reader, &identity, UINT64_C(0x40010000), 10016
    ) == MICROS_DIAGNOSTIC_CONTROL_OK);
    DIAGNOSTIC_EXPECT(reader.registered && translation.calls == 3);
    DIAGNOSTIC_EXPECT(untouched_destination(0));
    before = reader;
    DIAGNOSTIC_EXPECT(micros_diagnostic_register(&reader, &identity, 0, 0)
        == MICROS_DIAGNOSTIC_CONTROL_STATE);
    DIAGNOSTIC_EXPECT(translation.calls == 3 && memcmp(&reader, &before, sizeof(reader)) == 0);
    for (index = 0; index < 10013; ++index) {
        DIAGNOSTIC_EXPECT(micros_diagnostic_append(&history, (uint8_t)(1 + index % 251)));
    }
    DIAGNOSTIC_EXPECT(micros_diagnostic_read(&reader, &identity, &history, 1820)
        == MICROS_DIAGNOSTIC_CONTROL_OK);
    DIAGNOSTIC_EXPECT(translation.calls == 3);
    DIAGNOSTIC_EXPECT(destination_u32(0) == 1 && destination_u32(4) == 13);
    DIAGNOSTIC_EXPECT(destination_u32(8) == 8193 && destination_u32(12) == 0);
    for (index = 0; index < 8193; ++index) {
        DIAGNOSTIC_EXPECT(destination_byte(16 + index) == (uint8_t)(1 + (1820 + index) % 251));
    }
    DIAGNOSTIC_EXPECT(untouched_destination(16 + 8193));
    memset(translation.pages, 0xcd, sizeof(translation.pages));
    DIAGNOSTIC_EXPECT(micros_diagnostic_read(&reader, &identity, &history, 13)
        == MICROS_DIAGNOSTIC_CONTROL_OK);
    DIAGNOSTIC_EXPECT(destination_u32(4) == 13 && destination_u32(8) == 0);
    DIAGNOSTIC_EXPECT(translation.calls == 3 && untouched_destination(16));
    return true;
}

static bool registration_failure_is_atomic(void)
{
    struct micros_diagnostic_identity identity = reader_identity();
    struct micros_diagnostic_reader reader = {0};
    struct micros_diagnostic_reader before = reader;
    unsigned page;
    unsigned mode;
    size_t index;
    const uint64_t invalid_addresses[] = {
        0, UINT64_C(0x3ffff000), UINT64_C(0x40010001),
        UINT64_C(0x7fffc000), UINT64_C(0x7fffe000), UINT64_MAX - 4095,
    };

    reset_translation();
    for (index = 0; index < sizeof(invalid_addresses) / sizeof(invalid_addresses[0]); ++index) {
        DIAGNOSTIC_EXPECT(micros_diagnostic_register(&reader, &identity, invalid_addresses[index], 10016)
            == MICROS_DIAGNOSTIC_CONTROL_ARGUMENT);
        DIAGNOSTIC_EXPECT(translation.calls == 0 && memcmp(&reader, &before, sizeof(reader)) == 0);
    }
    DIAGNOSTIC_EXPECT(micros_diagnostic_register(&reader, &identity, UINT64_C(0x40010000), 10015)
        == MICROS_DIAGNOSTIC_CONTROL_ARGUMENT);
    for (page = 0; page < 3; ++page) {
        for (mode = 0; mode < 3; ++mode) {
            enum micros_diagnostic_control_result expected = mode == 2
                ? MICROS_DIAGNOSTIC_CONTROL_INVARIANT : MICROS_DIAGNOSTIC_CONTROL_MEMORY_FAULT;

            reset_translation();
            translation.rejected_page = page;
            translation.readonly = mode == 1;
            if (mode == 2) {
                translation.rejected_result = MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            }
            DIAGNOSTIC_EXPECT(micros_diagnostic_register(&reader, &identity, UINT64_C(0x40010000), 10016)
                == expected);
            DIAGNOSTIC_EXPECT(translation.calls == page + 1);
            DIAGNOSTIC_EXPECT(memcmp(&reader, &before, sizeof(reader)) == 0 && untouched_destination(0));
        }
    }
    reset_translation();
    translation.duplicate = true;
    DIAGNOSTIC_EXPECT(micros_diagnostic_register(&reader, &identity, UINT64_C(0x40010000), 10016)
        == MICROS_DIAGNOSTIC_CONTROL_INVARIANT);
    DIAGNOSTIC_EXPECT(memcmp(&reader, &before, sizeof(reader)) == 0 && untouched_destination(0));
    return true;
}

static bool bound_identity_and_cursor_fail_before_copy(void)
{
    struct micros_diagnostic_identity identity = reader_identity();
    struct micros_diagnostic_reader reader = {0};
    struct micros_diagnostic_history history = {0};
    unsigned mutation;

    reset_translation();
    DIAGNOSTIC_EXPECT(micros_diagnostic_register(&reader, &identity, UINT64_C(0x40010000), 10016)
        == MICROS_DIAGNOSTIC_CONTROL_OK);
    for (mutation = 0; mutation < 7; ++mutation) {
        struct micros_diagnostic_identity changed = identity;

        switch (mutation) {
        case 0: ++changed.process.generation; break;
        case 1: ++changed.thread.generation; break;
        case 2: ++changed.thread.slot; break;
        case 3: ++changed.endpoint; break;
        case 4: ++changed.profile_id; break;
        case 5: changed.root_physical_address += 4096; break;
        default: ++changed.service_id; break;
        }
        DIAGNOSTIC_EXPECT(micros_diagnostic_read(&reader, &changed, &history, 0)
            == MICROS_DIAGNOSTIC_CONTROL_INVARIANT);
        DIAGNOSTIC_EXPECT(translation.calls == 3 && untouched_destination(0));
    }
    DIAGNOSTIC_EXPECT(micros_diagnostic_read(&reader, &identity, &history, 1)
        == MICROS_DIAGNOSTIC_CONTROL_ARGUMENT);
    DIAGNOSTIC_EXPECT(micros_diagnostic_read(&reader, &identity, &history, 10000)
        == MICROS_DIAGNOSTIC_CONTROL_ARGUMENT);
    DIAGNOSTIC_EXPECT(translation.calls == 3 && untouched_destination(0));
    history.tail_position = 2000;
    DIAGNOSTIC_EXPECT(micros_diagnostic_read(&reader, &identity, &history, 0)
        == MICROS_DIAGNOSTIC_CONTROL_INVARIANT);
    DIAGNOSTIC_EXPECT(untouched_destination(0));
    return true;
}

static bool partial_binding_cannot_be_registered_again(void)
{
    struct micros_diagnostic_identity identity = reader_identity();
    struct micros_diagnostic_history history = {0};
    unsigned mutation;

    for (mutation = 0; mutation < 3; ++mutation) {
        struct micros_diagnostic_reader reader = {0};
        struct micros_diagnostic_reader before;

        reset_translation();
        if (mutation == 0) {
            reader.virtual_base = UINT64_C(0x40010000);
        } else if (mutation == 1) {
            reader.identity = identity;
        } else {
            reader.pages[0] = (uint64_t)(uintptr_t)translation.pages[0];
        }
        before = reader;
        DIAGNOSTIC_EXPECT(micros_diagnostic_register(&reader, &identity, UINT64_C(0x40010000), 10016)
            == MICROS_DIAGNOSTIC_CONTROL_INVARIANT);
        DIAGNOSTIC_EXPECT(micros_diagnostic_read(&reader, &identity, &history, 0)
            == MICROS_DIAGNOSTIC_CONTROL_INVARIANT);
        DIAGNOSTIC_EXPECT(translation.calls == 0 && memcmp(&reader, &before, sizeof(reader)) == 0);
        DIAGNOSTIC_EXPECT(untouched_destination(0));
    }
    return true;
}

static bool decoder_publishes_exact_fields(void)
{
    struct micros_syscall_arguments arguments = {
        .a0 = 3, .a1 = 1, .a2 = 4, .a3 = UINT32_C(0x1003),
        .a4 = UINT64_C(0x40010000), .a5 = 10016, .a7 = 14,
    };
    struct micros_tty_control_request request;
    struct micros_tty_control_request before;

    DIAGNOSTIC_EXPECT(micros_tty_control_decode(&arguments, &request) == 0);
    DIAGNOSTIC_EXPECT(
        request.command == 3 && request.version == 1 && request.service_id == 4
        && request.endpoint == UINT32_C(0x1003) && request.virtual_base == UINT64_C(0x40010000)
        && request.mapped_length == 10016 && request.physical_base == 0
        && request.irq_source == 0 && request.diagnostic_cursor == 0
    );
    before = request;
    arguments.a6 = 1;
    DIAGNOSTIC_EXPECT(micros_tty_control_decode(&arguments, &request) == -1);
    DIAGNOSTIC_EXPECT(memcmp(&request, &before, sizeof(request)) == 0);
    arguments = (struct micros_syscall_arguments){.a0 = 4, .a1 = 1, .a2 = 17, .a7 = 14};
    DIAGNOSTIC_EXPECT(micros_tty_control_decode(&arguments, &request) == 0);
    DIAGNOSTIC_EXPECT(
        request.command == 4 && request.version == 1 && request.diagnostic_cursor == 17
        && request.service_id == 0 && request.endpoint == 0 && request.virtual_base == 0
        && request.physical_base == 0 && request.mapped_length == 0 && request.irq_source == 0
    );
    before = request;
    arguments.a3 = 1;
    DIAGNOSTIC_EXPECT(micros_tty_control_decode(&arguments, &request) == -1);
    DIAGNOSTIC_EXPECT(memcmp(&request, &before, sizeof(request)) == 0);
    return true;
}

bool diagnostic_control_cases(void)
{
    return registration_and_three_page_wrapped_read()
        && registration_failure_is_atomic()
        && bound_identity_and_cursor_fail_before_copy()
        && partial_binding_cannot_be_registered_again()
        && decoder_publishes_exact_fields();
}
