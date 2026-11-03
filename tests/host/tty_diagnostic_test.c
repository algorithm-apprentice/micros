#include "diagnostic_history_test.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/runtime/raw_syscall.h"
#include "micros/syscall_abi.h"
#include "servers/tty/tty_diagnostic.h"
#include "servers/tty/tty_service_core.h"
#include "tests/qemu/diagnostic_history_tty.h"

struct raw_fixture {
    uint8_t *snapshot;
    uint32_t cursor;
    uint32_t length;
    unsigned registrations;
    unsigned reads;
    unsigned malformed;
    unsigned failed_mutation;
    int64_t failure;
    bool short_text;
};

static struct raw_fixture raw;

_Static_assert(sizeof(struct micros_tty_diagnostic) == 12288, "one three-page consumer");

static void put_u32(uint8_t *bytes, uint32_t value)
{
    unsigned index;

    for (index = 0; index < 4; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static uint32_t get_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8
        | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint8_t payload_byte(uint32_t index)
{
    static const uint8_t text[] = {'a', '\n', 'b'};

    return raw.short_text ? text[index] : (uint8_t)(1 + index % 251);
}

int64_t micros_runtime_raw_syscall(
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
    uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7
)
{
    uint32_t next;
    uint32_t index;

    if (a1 != 1 || a6 != 0 || a7 != 14) {
        abort();
    }
    if (a0 == 3) {
        ++raw.registrations;
        if (a2 != 4 || a3 != UINT32_C(0x7003) || a4 == 0 || a4 % 4096 != 0 || a5 != 10016) {
            abort();
        }
        if (raw.failure != 0) {
            return raw.failure;
        }
        raw.snapshot = (uint8_t *)(uintptr_t)a4;
        return raw.cursor;
    }
    if (
        a0 != 4 || a2 != raw.cursor || a3 != 0 || a4 != 0 || a5 != 0
        || raw.snapshot == NULL || raw.length >= 10000
    ) {
        abort();
    }
    ++raw.reads;
    if (raw.failure != 0) {
        struct micros_tty_diagnostic *diagnostic = (struct micros_tty_diagnostic *)(void *)raw.snapshot;

        switch (raw.failed_mutation) {
        case 0: break;
        case 1: raw.snapshot[0] ^= 1; break;
        case 2: ++diagnostic->previous; break;
        case 3: diagnostic->length = 1; break;
        case 4: diagnostic->offset = 1; break;
        case 5: diagnostic->pending = true; break;
        case 6: diagnostic->initialized = false; break;
        default: abort();
        }
        return raw.failure;
    }
    next = (raw.cursor + raw.length) % 10000;
    put_u32(raw.snapshot, raw.malformed == 1 ? 2 : 1);
    put_u32(raw.snapshot + 4, raw.malformed == 2 ? 10000 : next);
    put_u32(raw.snapshot + 8, raw.malformed == 3 ? 10000 : raw.length);
    put_u32(raw.snapshot + 12, raw.malformed == 4 ? 1 : 0);
    if (raw.malformed == 5) {
        put_u32(raw.snapshot + 4, (next + 1) % 10000);
    }
    for (index = 0; index < raw.length; ++index) {
        raw.snapshot[16 + index] = payload_byte(index);
    }
    raw.cursor = next;
    return 0;
}

static bool initialize(
    struct micros_tty_diagnostic *diagnostic,
    struct micros_tty_service_state *service
)
{
    memset(diagnostic, 0, sizeof(*diagnostic));
    memset(diagnostic->snapshot, 0xce, sizeof(diagnostic->snapshot));
    memset(&raw, 0, sizeof(raw));
    raw.cursor = 9980;
    DIAGNOSTIC_EXPECT(micros_tty_service_initialize(
        service, UINT32_C(0x7003), UINT32_C(0x1005)
    ) == MICROS_TTY_SERVICE_OK);
    DIAGNOSTIC_EXPECT(micros_tty_service_commit_ownership(service) == MICROS_TTY_SERVICE_OK);
    DIAGNOSTIC_EXPECT(micros_tty_diagnostic_initialize(diagnostic, UINT32_C(0x7003)));
    DIAGNOSTIC_EXPECT(diagnostic->previous == 9980 && raw.registrations == 1 && raw.reads == 0);
    DIAGNOSTIC_EXPECT(diagnostic->snapshot[0] == 0xce && diagnostic->snapshot[10015] == 0xce);
    return true;
}

static bool drain_payload(
    struct micros_tty_state *terminal,
    uint32_t start,
    uint32_t length,
    bool expect_writable
)
{
    uint32_t index;
    bool writable = false;

    for (index = 0; index < length; ++index) {
        uint8_t expected = payload_byte(start + index);
        uint8_t observed;
        struct micros_tty_effects effects;

        DIAGNOSTIC_EXPECT(micros_tty_output_take(terminal, &observed, &effects) == MICROS_TTY_CORE_OK);
        writable |= effects.writable;
        DIAGNOSTIC_EXPECT(observed == (expected == '\n' ? '\r' : expected));
        if (expected == '\n') {
            DIAGNOSTIC_EXPECT(micros_tty_output_take(terminal, &observed, &effects) == MICROS_TTY_CORE_OK);
            writable |= effects.writable;
            DIAGNOSTIC_EXPECT(observed == '\n');
        }
    }
    DIAGNOSTIC_EXPECT(!terminal->write_resident && writable == expect_writable);
    return true;
}

static bool occupied_spool_and_uncollected_completion(void)
{
    struct micros_tty_diagnostic diagnostic;
    struct micros_tty_service_state service;
    struct micros_tty_service_completion before;
    struct micros_tty_effects effects;
    const uint8_t ordinary[] = {'v'};
    uint8_t observed;

    DIAGNOSTIC_EXPECT(initialize(&diagnostic, &service));
    service.last_accepted_request_id = 1;
    service.write_completion = (struct micros_tty_service_completion){
        .active = true, .result = MICROS_TTY_RESULT_OK, .request_id = 1, .transferred_count = 1,
    };
    before = service.write_completion;
    DIAGNOSTIC_EXPECT(micros_tty_write_accept(&service.terminal, ordinary, 1, &effects)
        == MICROS_TTY_CORE_OK);
    DIAGNOSTIC_EXPECT(micros_tty_service_validate(&service) == MICROS_TTY_SERVICE_OK);
    raw.length = 5000;
    DIAGNOSTIC_EXPECT(micros_tty_diagnostic_progress(&diagnostic, &service.terminal, true, &effects));
    DIAGNOSTIC_EXPECT(raw.reads == 1 && diagnostic.previous == 4980);
    DIAGNOSTIC_EXPECT(diagnostic.length == 5000 && diagnostic.offset == 0);
    DIAGNOSTIC_EXPECT(service.terminal.write_length == 1 && service.terminal.write[0] == 'v');
    DIAGNOSTIC_EXPECT(micros_tty_output_take(&service.terminal, &observed, &effects) == MICROS_TTY_CORE_OK);
    DIAGNOSTIC_EXPECT(observed == 'v');
    DIAGNOSTIC_EXPECT(micros_tty_diagnostic_progress(&diagnostic, &service.terminal, false, &effects));
    DIAGNOSTIC_EXPECT(diagnostic.length == 5000 && diagnostic.offset == 4096);
    DIAGNOSTIC_EXPECT(service.terminal.write_resident && service.terminal.write_length == 4096);
    DIAGNOSTIC_EXPECT(effects.enable_transmit);
    DIAGNOSTIC_EXPECT(micros_tty_diagnostic_progress(&diagnostic, &service.terminal, true, &effects));
    DIAGNOSTIC_EXPECT(raw.reads == 1 && diagnostic.offset == 4096 && diagnostic.pending);
    DIAGNOSTIC_EXPECT(drain_payload(&service.terminal, 0, 4096, false));
    DIAGNOSTIC_EXPECT(micros_tty_diagnostic_progress(&diagnostic, &service.terminal, false, &effects));
    DIAGNOSTIC_EXPECT(raw.reads == 1 && diagnostic.offset == 5000 && diagnostic.pending);
    DIAGNOSTIC_EXPECT(drain_payload(&service.terminal, 4096, 904, false));
    raw.length = 3;
    raw.short_text = true;
    DIAGNOSTIC_EXPECT(micros_tty_diagnostic_progress(&diagnostic, &service.terminal, false, &effects));
    DIAGNOSTIC_EXPECT(raw.reads == 2 && diagnostic.previous == 4983 && !diagnostic.pending);
    DIAGNOSTIC_EXPECT(drain_payload(&service.terminal, 0, 3, false));
    DIAGNOSTIC_EXPECT(service.last_accepted_request_id == 1);
    DIAGNOSTIC_EXPECT(memcmp(&service.write_completion, &before, sizeof(before)) == 0);
    DIAGNOSTIC_EXPECT(!service.pending_read.active && !service.read_completion.active);
    DIAGNOSTIC_EXPECT(micros_tty_service_validate(&service) == MICROS_TTY_SERVICE_OK);
    return true;
}

static enum micros_tty_service_copy_result copy_vfs(
    void *context,
    enum micros_tty_service_copy_direction direction,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t offset,
    uint8_t *local,
    size_t length
)
{
    unsigned *calls = context;

    if (
        calls == NULL || direction != MICROS_TTY_SERVICE_COPY_FROM_VFS
        || endpoint != UINT32_C(0x1005) || grant != 1 || offset != 0 || length > 1
    ) {
        abort();
    }
    ++*calls;
    if (length == 1) {
        *local = (uint8_t)'v';
    }
    return MICROS_TTY_SERVICE_COPY_OK;
}

static bool diagnostic_output_retains_vfs_busy_retry(void)
{
    struct micros_tty_diagnostic diagnostic;
    struct micros_tty_service_state service;
    struct micros_tty_effects effects;
    struct micros_tty_service_reply_action action;
    struct micros_ipc_message request = {0};
    unsigned copies = 0;
    const struct micros_tty_service_io io = {.copy = copy_vfs, .context = &copies};
    uint64_t notification;
    uint8_t echoed;

    DIAGNOSTIC_EXPECT(initialize(&diagnostic, &service));
    DIAGNOSTIC_EXPECT(micros_tty_receive_byte(&service.terminal, (uint8_t)'e', &effects)
        == MICROS_TTY_CORE_OK);
    raw.length = 3;
    raw.short_text = true;
    DIAGNOSTIC_EXPECT(micros_tty_diagnostic_progress(&diagnostic, &service.terminal, true, &effects));
    request.source = service.vfs_endpoint;
    request.type = MICROS_TTY_MESSAGE_SUBMIT_WRITE;
    request.reply_token = 1;
    put_u32(request.payload, 1);
    put_u32(request.payload + 8, 1);
    put_u32(request.payload + 16, 1);
    put_u32(request.payload + 32, 1);
    DIAGNOSTIC_EXPECT(micros_tty_service_handle_call(&service, &request, &io, &action) == MICROS_TTY_SERVICE_OK);
    DIAGNOSTIC_EXPECT((int32_t)get_u32(action.message.payload + 8) == MICROS_TTY_RESULT_BUSY);
    DIAGNOSTIC_EXPECT(copies == 0 && service.last_accepted_request_id == 0 && service.terminal.write_retry_armed);
    DIAGNOSTIC_EXPECT(micros_tty_service_commit_reply(&service, &action, &notification) == MICROS_TTY_SERVICE_OK);
    DIAGNOSTIC_EXPECT(notification == 0);
    DIAGNOSTIC_EXPECT(micros_tty_output_take(&service.terminal, &echoed, &effects) == MICROS_TTY_CORE_OK);
    DIAGNOSTIC_EXPECT(echoed == 'e' && !effects.writable);
    DIAGNOSTIC_EXPECT(drain_payload(&service.terminal, 0, 3, true));
    DIAGNOSTIC_EXPECT(micros_tty_service_handle_call(&service, &request, &io, &action) == MICROS_TTY_SERVICE_OK);
    DIAGNOSTIC_EXPECT((int32_t)get_u32(action.message.payload + 8) == MICROS_TTY_RESULT_OK);
    DIAGNOSTIC_EXPECT(copies == 2 && service.last_accepted_request_id == 1 && service.write_completion.active);
    DIAGNOSTIC_EXPECT(service.terminal.write[0] == 'v');
    return true;
}

static bool malformed_control_and_snapshots_do_not_advance(void)
{
    struct micros_tty_diagnostic diagnostic;
    struct micros_tty_service_state service;
    struct micros_tty_effects effects;
    unsigned malformed;

    memset(&diagnostic, 0, sizeof(diagnostic));
    memset(&raw, 0, sizeof(raw));
    raw.failure = -6;
    DIAGNOSTIC_EXPECT(!micros_tty_diagnostic_initialize(&diagnostic, UINT32_C(0x7003)));
    DIAGNOSTIC_EXPECT(!diagnostic.initialized && diagnostic.previous == 0);
    for (malformed = 0; malformed <= 5; ++malformed) {
        DIAGNOSTIC_EXPECT(initialize(&diagnostic, &service));
        raw.length = 3;
        raw.short_text = true;
        raw.malformed = malformed;
        raw.failure = malformed == 0 ? -6 : 0;
        DIAGNOSTIC_EXPECT(!micros_tty_diagnostic_progress(&diagnostic, &service.terminal, true, &effects));
        DIAGNOSTIC_EXPECT(diagnostic.previous == 9980 && diagnostic.offset == 0);
        DIAGNOSTIC_EXPECT(!service.terminal.write_resident && service.last_accepted_request_id == 0);
    }
    return true;
}

static bool exact_kernel_envelopes(void)
{
    struct micros_ipc_message message = {0};
    uint64_t events;
    unsigned mask;
    unsigned mutation;

    message.source = MICROS_ENDPOINT_NONE;
    message.type = MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    for (mask = 0; mask <= 32; ++mask) {
        bool valid = mask == 8 || mask == 16 || mask == 24;

        events = 99;
        message.payload[0] = (uint8_t)mask;
        DIAGNOSTIC_EXPECT(micros_tty_diagnostic_notification(&message, &events) == valid);
        DIAGNOSTIC_EXPECT(events == (valid ? mask : 99));
    }
    message.payload[0] = 24;
    for (mutation = 0; mutation < 4; ++mutation) {
        struct micros_ipc_message changed = message;

        switch (mutation) {
        case 0: changed.source = UINT32_C(0x1005); break;
        case 1: changed.reply_token = 1; break;
        case 2: changed.payload[9] = 1; break;
        default: changed.type = MICROS_TTY_MESSAGE_SUBMIT_WRITE; break;
        }
        events = 99;
        DIAGNOSTIC_EXPECT(!micros_tty_diagnostic_notification(&changed, &events));
        DIAGNOSTIC_EXPECT(events == 99);
    }
    return true;
}

static bool probe_deferral_preserves_then_completes_the_same_snapshot(void)
{
    struct micros_tty_diagnostic diagnostic;
    struct micros_tty_service_state service;

    DIAGNOSTIC_EXPECT(initialize(&diagnostic, &service));
    raw.failure = MICROS_SYSCALL_ABI_STATE;
    DIAGNOSTIC_EXPECT(micros_diagnostic_test_tty_transfer(&diagnostic) == MICROS_DIAGNOSTIC_TEST_TRANSFER_DEFERRED);
    DIAGNOSTIC_EXPECT(raw.reads == 1 && diagnostic.previous == 9980);
    DIAGNOSTIC_EXPECT(diagnostic.length == 0 && diagnostic.offset == 0);
    DIAGNOSTIC_EXPECT(!diagnostic.pending && diagnostic.initialized);
    raw.failure = 0;
    raw.length = 8193;
    DIAGNOSTIC_EXPECT(micros_diagnostic_test_tty_transfer(&diagnostic) == MICROS_DIAGNOSTIC_TEST_TRANSFER_COMPLETE);
    DIAGNOSTIC_EXPECT(raw.reads == 2 && diagnostic.previous == (9980 + 8193) % 10000);
    DIAGNOSTIC_EXPECT(diagnostic.length == 8193 && diagnostic.offset == 8193 && !diagnostic.pending);
    DIAGNOSTIC_EXPECT(micros_diagnostic_test_tty_transfer(&diagnostic) == MICROS_DIAGNOSTIC_TEST_TRANSFER_FAILED);
    DIAGNOSTIC_EXPECT(raw.reads == 2 && service.last_accepted_request_id == 0);
    return true;
}

static bool probe_deferral_rejects_mutation_and_other_results(void)
{
    struct micros_tty_diagnostic diagnostic;
    struct micros_tty_service_state service;
    unsigned mutation;
    int64_t result;

    for (mutation = 1; mutation <= 6; ++mutation) {
        DIAGNOSTIC_EXPECT(initialize(&diagnostic, &service));
        raw.failure = MICROS_SYSCALL_ABI_STATE;
        raw.failed_mutation = mutation;
        DIAGNOSTIC_EXPECT(micros_diagnostic_test_tty_transfer(&diagnostic) == MICROS_DIAGNOSTIC_TEST_TRANSFER_FAILED);
        DIAGNOSTIC_EXPECT(raw.reads == 1 && service.last_accepted_request_id == 0);
    }
    for (result = -12; result <= 1; ++result) {
        if (result == 0 || result == MICROS_SYSCALL_ABI_STATE) {
            continue;
        }
        DIAGNOSTIC_EXPECT(initialize(&diagnostic, &service));
        raw.failure = result;
        DIAGNOSTIC_EXPECT(micros_diagnostic_test_tty_transfer(&diagnostic) == MICROS_DIAGNOSTIC_TEST_TRANSFER_FAILED);
        DIAGNOSTIC_EXPECT(diagnostic.previous == 9980 && diagnostic.length == 0 && diagnostic.offset == 0);
    }
    return true;
}

bool tty_diagnostic_cases(void)
{
    return occupied_spool_and_uncollected_completion()
        && diagnostic_output_retains_vfs_busy_retry()
        && malformed_control_and_snapshots_do_not_advance()
        && exact_kernel_envelopes()
        && probe_deferral_preserves_then_completes_the_same_snapshot()
        && probe_deferral_rejects_mutation_and_other_results();
}
