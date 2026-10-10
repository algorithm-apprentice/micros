#ifndef MICROS_SERVERS_TTY_CORE_H
#define MICROS_SERVERS_TTY_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/grant.h"
#include "micros/ipc.h"
#include "micros/tty.h"

#define MICROS_TTY_INPUT_END_OF_LINE UINT8_C(0x01)

enum micros_tty_core_error {
    MICROS_TTY_CORE_OK = 0,
    MICROS_TTY_CORE_ERROR_ARGUMENT,
    MICROS_TTY_CORE_ERROR_STATE,
    MICROS_TTY_CORE_ERROR_BUSY,
    MICROS_TTY_CORE_ERROR_NOT_READY,
    MICROS_TTY_CORE_ERROR_EMPTY,
    MICROS_TTY_CORE_ERROR_INVARIANT,
};

enum micros_tty_protocol_status {
    MICROS_TTY_PROTOCOL_OK = 0,
    MICROS_TTY_PROTOCOL_BAD_TYPE,
    MICROS_TTY_PROTOCOL_BAD_VERSION,
    MICROS_TTY_PROTOCOL_MALFORMED,
    MICROS_TTY_PROTOCOL_INVARIANT,
};

struct micros_tty_request {
    uint32_t type;
    uint32_t flags;
    micros_endpoint_t source;
    micros_grant_t grant;
    uint64_t reply_token;
    uint64_t request_id;
    uint64_t grant_offset;
    uint64_t count;
};

struct micros_tty_effects {
    bool enable_transmit;
    bool disable_transmit;
    bool writable;
};

struct micros_tty_input_entry {
    uint8_t byte;
    uint8_t flags;
};

struct micros_tty_state {
    uint64_t initialization_magic;
    struct micros_tty_input_entry
        input[MICROS_TTY_INPUT_CAPACITY];
    uint16_t input_head;
    uint16_t input_count;
    uint16_t current_line_length;
    uint16_t completed_line_count;
    uint8_t echo[MICROS_TTY_ECHO_CAPACITY];
    uint16_t echo_head;
    uint16_t echo_count;
    uint8_t read_staging[MICROS_TTY_TRANSFER_MAX];
    uint16_t read_staging_length;
    uint8_t write[MICROS_TTY_WRITE_CAPACITY];
    uint16_t write_length;
    uint16_t write_cursor;
    bool write_resident;
    bool write_pending_lf;
    bool transmit_interrupt_enabled;
    bool write_retry_armed;
    uint64_t receive_error_count;
    uint64_t input_drop_count;
    uint64_t echo_drop_count;
};

enum micros_tty_protocol_status micros_tty_decode_request(
    const struct micros_ipc_message *message,
    struct micros_tty_request *request
);

enum micros_tty_core_error micros_tty_build_result(
    uint32_t request_type,
    enum micros_tty_result result,
    uint64_t request_id,
    uint64_t transferred_count,
    struct micros_ipc_message *message
);

enum micros_tty_core_error micros_tty_state_initialize(
    struct micros_tty_state *state
);

enum micros_tty_core_error micros_tty_state_validate(
    const struct micros_tty_state *state
);

bool micros_tty_input_has_line(const struct micros_tty_state *state);

bool micros_tty_output_pending(const struct micros_tty_state *state);

enum micros_tty_core_error micros_tty_receive_byte(
    struct micros_tty_state *state,
    uint8_t byte,
    struct micros_tty_effects *effects
);

enum micros_tty_core_error micros_tty_record_receive_error(
    struct micros_tty_state *state
);

enum micros_tty_core_error micros_tty_read_stage(
    struct micros_tty_state *state,
    uint64_t requested_count,
    uint64_t *staged_count
);

enum micros_tty_core_error micros_tty_read_discard(
    struct micros_tty_state *state
);

enum micros_tty_core_error micros_tty_read_commit(
    struct micros_tty_state *state
);

enum micros_tty_core_error micros_tty_write_accept(
    struct micros_tty_state *state,
    const uint8_t *bytes,
    uint64_t count,
    struct micros_tty_effects *effects
);

enum micros_tty_core_error micros_tty_write_arm_retry(
    struct micros_tty_state *state
);

enum micros_tty_core_error micros_tty_output_take(
    struct micros_tty_state *state,
    uint8_t *byte,
    struct micros_tty_effects *effects
);

#endif
