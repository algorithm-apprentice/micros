#include "tests/host/vfs_model_internal.h"

#include <string.h>

bool model_console_read_submit(struct vfs_model *selected) {
    struct micros_vfs_request request =
        model_request(MICROS_VFS_MESSAGE_READ, VFS_MODEL_SECOND_ENDPOINT, selected->step + 1);
    struct micros_vfs_result_action action;
    size_t process_slot;
    uint16_t open_file;
    uint64_t request_id = selected->reference.next_tty_request_id;

    selected->retained_application_grant = vfs_test_fixture_add_application_grant(
        &selected->fixture, VFS_MODEL_SECOND_ENDPOINT, MICROS_GRANT_PERMISSION_WRITE, NULL, 8);
    request.descriptor = 0;
    request.grant = selected->retained_application_grant;
    request.count = 8;
    MODEL_EXPECT(
        selected,
        selected->retained_application_grant != MICROS_GRANT_NONE &&
            micros_vfs_handle_request(&selected->production, &request, &selected->fixture.io,
                                      &action) == MICROS_VFS_CORE_OK &&
            !action.active &&
            reference_find_process(&selected->reference, VFS_MODEL_SECOND_ENDPOINT, &process_slot));
    open_file = selected->reference.processes[process_slot].descriptors[0];
    selected->reference.pending = (struct micros_vfs_pending_operation){
        .state = MICROS_VFS_PENDING_TTY_READ_WAIT_COMPLETION,
        .process_slot = (uint16_t)process_slot,
        .descriptor = 0,
        .open_file = (uint16_t)(open_file - 1),
        .endpoint = VFS_MODEL_SECOND_ENDPOINT,
        .reply_token = request.reply_token,
        .application_grant = selected->retained_application_grant,
        .tty_grant = UINT32_C(0x100),
        .count = 8,
        .request_id = request_id,
    };
    selected->reference.backend_page_owner = MICROS_VFS_PAGE_TTY_READ;
    reference_advance_request_id(&selected->reference, request_id);
    ++selected->coverage.tty_submit;
    return compare_transition(selected);
}

static bool revoke_retained_grant(struct vfs_model *selected) {
    bool result = vfs_test_fixture_revoke_application_grant(&selected->fixture,
                                                            selected->retained_application_grant);

    selected->retained_application_grant = MICROS_GRANT_NONE;
    return result;
}

bool model_console_read_finish(struct vfs_model *selected, uint32_t variant) {
    static const uint8_t input[] = "x\n";
    struct micros_vfs_result_action action;
    enum micros_vfs_trusted_result result;

    if (variant == 0) {
        MODEL_EXPECT(selected,
                     vfs_test_fixture_complete_read(&selected->fixture, input, sizeof(input) - 1) &&
                         micros_vfs_handle_tty_notification(
                             &selected->production, MICROS_TTY_EVENT_COMPLETION,
                             &selected->fixture.io, &action) == MICROS_VFS_CORE_OK &&
                         action.active && action.transferred_count == sizeof(input) - 1 &&
                         revoke_retained_grant(selected));
        reference_clear_pending(&selected->reference);
        ++selected->coverage.tty_completion;
        ++selected->coverage.tty_collect;
        return compare_transition(selected);
    }
    if (variant == 1) {
        MODEL_EXPECT(selected,
                     micros_vfs_detach(&selected->production, VFS_MODEL_SECOND_ENDPOINT,
                                       &selected->fixture.io, &result) == MICROS_VFS_CORE_OK &&
                         result == MICROS_VFS_TRUSTED_OK && revoke_retained_grant(selected));
        reference_clear_pending(&selected->reference);
        MODEL_EXPECT(selected, reference_detach(&selected->reference, VFS_MODEL_SECOND_ENDPOINT));
        ++selected->coverage.tty_cancel;
        ++selected->coverage.detach;
        return compare_transition(selected);
    }
    MODEL_EXPECT(selected,
                 vfs_test_fixture_complete_read(&selected->fixture, input, sizeof(input) - 1) &&
                     micros_vfs_detach(&selected->production, VFS_MODEL_SECOND_ENDPOINT,
                                       &selected->fixture.io, &result) == MICROS_VFS_CORE_OK &&
                     result == MICROS_VFS_TRUSTED_PENDING && revoke_retained_grant(selected));
    reference_clear_pending(&selected->reference);
    selected->reference.pending.state = MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT;
    MODEL_EXPECT(selected, reference_detach(&selected->reference, VFS_MODEL_SECOND_ENDPOINT));
    ++selected->coverage.tty_cancel;
    ++selected->coverage.tty_collect;
    ++selected->coverage.detach;
    return compare_transition(selected);
}

bool model_clear_debt_or_bad_version(struct vfs_model *selected) {
    struct micros_vfs_result_action action;

    if (selected->reference.pending.state == MICROS_VFS_PENDING_TTY_COMPLETION_NOTICE_DEBT) {
        MODEL_EXPECT(selected, micros_vfs_handle_tty_notification(
                                   &selected->production, MICROS_TTY_EVENT_COMPLETION,
                                   &selected->fixture.io, &action) == MICROS_VFS_CORE_OK &&
                                   !action.active);
        reference_clear_pending(&selected->reference);
        ++selected->coverage.tty_completion;
        return compare_transition(selected);
    }
    {
        struct micros_vfs_request request =
            model_request(MICROS_VFS_MESSAGE_CLOSE, VFS_MODEL_FIRST_ENDPOINT, selected->step + 1);

        request.version = 2;
        MODEL_EXPECT(selected, micros_vfs_handle_request(&selected->production, &request,
                                                         &selected->fixture.io,
                                                         &action) == MICROS_VFS_CORE_OK &&
                                   action.result == MICROS_VFS_RESULT_BAD_VERSION);
        ++selected->coverage.malformed;
    }
    return compare_transition(selected);
}

bool model_console_write_submit(struct vfs_model *selected, bool expect_busy) {
    static const uint8_t output[] = "model";
    struct micros_vfs_request request =
        model_request(MICROS_VFS_MESSAGE_WRITE, VFS_MODEL_FIRST_ENDPOINT, selected->step + 1);
    struct micros_vfs_result_action action;
    micros_grant_t grant;
    size_t process_slot;
    uint16_t open_file;
    uint64_t request_id = selected->reference.next_tty_request_id;

    grant = vfs_test_fixture_add_application_grant(&selected->fixture, VFS_MODEL_FIRST_ENDPOINT,
                                                   MICROS_GRANT_PERMISSION_READ, output,
                                                   sizeof(output) - 1);
    request.descriptor = 1;
    request.grant = grant;
    request.count = sizeof(output) - 1;
    MODEL_EXPECT(
        selected,
        grant != MICROS_GRANT_NONE &&
            micros_vfs_handle_request(&selected->production, &request, &selected->fixture.io,
                                      &action) == MICROS_VFS_CORE_OK &&
            !action.active &&
            vfs_test_fixture_revoke_application_grant(&selected->fixture, grant) &&
            reference_find_process(&selected->reference, VFS_MODEL_FIRST_ENDPOINT, &process_slot));
    open_file = selected->reference.processes[process_slot].descriptors[1];
    selected->reference.pending = (struct micros_vfs_pending_operation){
        .state = expect_busy ? MICROS_VFS_PENDING_TTY_WRITE_WAIT_WRITABLE
                             : MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION,
        .process_slot = (uint16_t)process_slot,
        .descriptor = 1,
        .open_file = (uint16_t)(open_file - 1),
        .endpoint = VFS_MODEL_FIRST_ENDPOINT,
        .reply_token = request.reply_token,
        .application_grant = MICROS_GRANT_NONE,
        .tty_grant = expect_busy ? UINT32_C(0x100) : MICROS_GRANT_NONE,
        .count = sizeof(output) - 1,
        .request_id = request_id,
    };
    if (expect_busy) {
        selected->reference.backend_page_owner = MICROS_VFS_PAGE_TTY_WRITE_RETRY;
        memcpy(selected->pending_write_bytes, output, sizeof(output) - 1);
        selected->pending_write_length = sizeof(output) - 1;
    } else {
        reference_advance_request_id(&selected->reference, request_id);
    }
    ++selected->coverage.tty_submit;
    return compare_transition(selected);
}

bool model_console_write_completion(struct vfs_model *selected) {
    struct micros_vfs_result_action action;

    MODEL_EXPECT(selected, micros_vfs_handle_tty_notification(
                               &selected->production, MICROS_TTY_EVENT_COMPLETION,
                               &selected->fixture.io, &action) == MICROS_VFS_CORE_OK &&
                               action.active && action.result == MICROS_VFS_RESULT_OK &&
                               action.transferred_count == selected->reference.pending.count);
    reference_clear_pending(&selected->reference);
    ++selected->coverage.tty_completion;
    ++selected->coverage.tty_collect;
    return compare_transition(selected);
}

bool model_console_write_writable(struct vfs_model *selected) {
    struct micros_vfs_result_action action;
    uint64_t request_id = selected->reference.pending.request_id;
    uint64_t events = vfs_test_fixture_drain_output(&selected->fixture);

    MODEL_EXPECT(selected, events == MICROS_TTY_EVENT_WRITABLE &&
                               micros_vfs_handle_tty_notification(&selected->production, events,
                                                                  &selected->fixture.io,
                                                                  &action) == MICROS_VFS_CORE_OK &&
                               !action.active);
    selected->reference.pending.state = MICROS_VFS_PENDING_TTY_WRITE_WAIT_COMPLETION;
    selected->reference.pending.tty_grant = MICROS_GRANT_NONE;
    selected->reference.backend_page_owner = MICROS_VFS_PAGE_FREE;
    reference_advance_request_id(&selected->reference, request_id);
    memset(selected->pending_write_bytes, 0, sizeof(selected->pending_write_bytes));
    selected->pending_write_length = 0;
    ++selected->coverage.tty_writable;
    return compare_transition(selected);
}

bool model_test_drain_begin(struct vfs_model *selected) {
    struct micros_vfs_result_action action;
    uint64_t request_id = selected->reference.next_tty_request_id;

    MODEL_EXPECT(selected, micros_vfs_begin_test_drain(
                               &selected->production, VFS_MODEL_FIRST_ENDPOINT, selected->step + 1,
                               &selected->fixture.io, &action) == MICROS_VFS_CORE_OK &&
                               !action.active);
    selected->reference.pending = (struct micros_vfs_pending_operation){
        .state = MICROS_VFS_PENDING_TTY_TEST_DRAIN_WAIT_WRITABLE,
        .reply_token = selected->step + 1,
        .application_grant = MICROS_GRANT_NONE,
        .tty_grant = MICROS_GRANT_NONE,
        .request_id = request_id,
    };
    return compare_transition(selected);
}

bool model_test_drain_finish(struct vfs_model *selected) {
    struct micros_vfs_result_action action;
    uint64_t request_id = selected->reference.pending.request_id;
    uint64_t events = vfs_test_fixture_drain_output(&selected->fixture);

    MODEL_EXPECT(selected, events == MICROS_TTY_EVENT_WRITABLE &&
                               micros_vfs_handle_tty_notification(&selected->production, events,
                                                                  &selected->fixture.io,
                                                                  &action) == MICROS_VFS_CORE_OK &&
                               action.active &&
                               action.request_type == MICROS_VFS_TEST_MESSAGE_DRAIN &&
                               action.result == MICROS_VFS_RESULT_OK);
    reference_clear_pending(&selected->reference);
    reference_advance_request_id(&selected->reference, request_id);
    return compare_transition(selected);
}
