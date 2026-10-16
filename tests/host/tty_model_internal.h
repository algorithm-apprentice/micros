#ifndef MICROS_TESTS_HOST_TTY_MODEL_INTERNAL_H
#define MICROS_TESTS_HOST_TTY_MODEL_INTERNAL_H

#include "servers/tty/tty_service_core.h"
#include "servers/tty/tty_uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    MODEL_RANDOM_STEP_COUNT = 8192,
    MODEL_TRACE_CAPACITY = MODEL_RANDOM_STEP_COUNT + 32,
    MODEL_READ_GRANT = 1,
    MODEL_WRITE_GRANT = 2,
    MODEL_GRANT_CAPACITY = 64,
    MODEL_WRITE_SOURCE_CAPACITY = 32,
    MODEL_IIR_NO_PENDING = 0x01,
    MODEL_IIR_TRANSMIT_EMPTY = 0x02,
    MODEL_IIR_RECEIVED_DATA = 0x04,
    MODEL_IER_BASE = 0x05,
    MODEL_IER_TRANSMIT_EMPTY = 0x02,
    MODEL_LCR_DLAB = 0x80,
    MODEL_LSR_DATA_READY = 0x01,
    MODEL_LSR_TRANSMIT_EMPTY = 0x20,
};

enum model_operation {
    MODEL_OPERATION_INPUT = 1,
    MODEL_OPERATION_SUBMIT_READ,
    MODEL_OPERATION_SUBMIT_WRITE,
    MODEL_OPERATION_CANCEL,
    MODEL_OPERATION_COLLECT,
    MODEL_OPERATION_TRANSMIT_IRQ,
    MODEL_OPERATION_MALFORMED_SUBMIT,
};

enum model_grant_mode {
    MODEL_GRANT_VALID = 0,
    MODEL_GRANT_INVALID,
    MODEL_GRANT_REJECT_TRANSFER,
};

extern const micros_endpoint_t tty_model_tty_endpoint;
extern const micros_endpoint_t tty_model_vfs_endpoint;

struct model_input_entry {
    uint8_t byte;
    uint8_t flags;
};

struct model_completion {
    bool active;
    enum micros_tty_result result;
    uint64_t request_id;
    uint64_t transferred_count;
};

struct model_reference {
    struct model_input_entry input[MICROS_TTY_INPUT_CAPACITY];
    uint16_t input_head;
    uint16_t input_count;
    uint16_t current_line_length;
    uint16_t completed_line_count;
    uint8_t echo[MICROS_TTY_ECHO_CAPACITY];
    uint16_t echo_head;
    uint16_t echo_count;
    uint8_t write[MODEL_WRITE_SOURCE_CAPACITY];
    uint16_t write_length;
    uint16_t write_cursor;
    bool write_resident;
    bool write_pending_lf;
    bool transmit_interrupt_enabled;
    bool write_retry_armed;
    uint64_t input_drop_count;
    uint64_t echo_drop_count;
    uint64_t last_accepted_request_id;
    bool pending_read;
    uint64_t pending_read_request_id;
    uint64_t pending_read_count;
    uint64_t rejected_read_request_id;
    struct model_completion read_completion;
    struct model_completion write_completion;
};

struct tty_model;

struct model_bus {
    struct tty_model *model;
    uint8_t line_control;
    uint8_t interrupt_enable;
    uint8_t pending_interrupt;
    uint8_t receive_byte;
    bool receive_pending;
    bool transmit_ready;
    bool expect_lf;
    bool expected_writable;
    bool failed;
    uint64_t output_count;
    uint64_t output_hash;
};

struct model_copy {
    struct tty_model *model;
    uint8_t write_source[MODEL_WRITE_SOURCE_CAPACITY];
    size_t write_source_length;
    bool reject_write_transfer;
    bool failed;
};

struct tty_model {
    uint64_t seed;
    uint64_t random_state;
    size_t step;
    size_t trace_count;
    uint32_t operation_trace[MODEL_TRACE_CAPACITY];
    struct micros_tty_service_state service;
    struct micros_tty_uart_state uart;
    struct model_reference reference;
    struct model_bus bus;
    struct model_copy copy;
    size_t input_irq_count;
    size_t transmit_irq_count;
    size_t accepted_read_count;
    size_t accepted_write_count;
    size_t accepted_cancel_count;
    size_t completed_read_count;
    size_t successful_collect_count;
    size_t rejected_grant_count;
    size_t rejected_transfer_count;
};

#define MODEL_EXPECT(model, expression) \
    do { \
        if (!(expression)) { \
            return tty_model_failure(#expression, __LINE__, model); \
        } \
    } while (false)

struct micros_ipc_message tty_model_identity_request(
    struct tty_model *model,
    uint32_t type,
    uint64_t request_id
);

uint8_t tty_model_bus_read(void *context, uint8_t offset);

void tty_model_bus_write(
    void *context,
    uint8_t offset,
    uint8_t value
);

bool tty_model_cancel(
    struct tty_model *model,
    uint64_t request_id
);

bool tty_model_check_reply(
    struct tty_model *model,
    const struct micros_tty_service_reply_action *action,
    uint32_t request_type,
    enum micros_tty_result expected_result,
    uint64_t expected_request_id,
    uint64_t expected_transferred_count
);

bool tty_model_collect(
    struct tty_model *model,
    uint64_t request_id
);

bool tty_model_commit_reply(
    struct tty_model *model,
    struct micros_tty_service_reply_action *action,
    uint64_t expected_notification
);

enum micros_tty_service_copy_result tty_model_copy_grant(
    void *context,
    enum micros_tty_service_copy_direction direction,
    micros_endpoint_t endpoint,
    micros_grant_t grant,
    uint64_t offset,
    uint8_t *local,
    size_t length
);

bool tty_model_failure(
    const char *expression,
    int line,
    const struct tty_model *model
);

bool tty_model_input_irq(
    struct tty_model *model,
    uint8_t byte
);

bool tty_model_malformed_submit(struct tty_model *model);

uint64_t tty_model_random(struct tty_model *model);

void tty_model_record(
    struct tty_model *model,
    enum model_operation operation,
    uint32_t detail
);

bool tty_model_submit_read(
    struct tty_model *model,
    uint64_t count,
    enum model_grant_mode grant_mode
);

bool tty_model_submit_write(
    struct tty_model *model,
    const uint8_t *bytes,
    size_t count,
    enum model_grant_mode grant_mode
);

bool tty_model_transmit_irq(struct tty_model *model);

bool tty_model_validate(struct tty_model *model);

uint32_t tty_model_read_u32_le(const uint8_t *bytes);

uint64_t tty_model_read_u64_le(const uint8_t *bytes);

bool tty_model_reference_consume_input(
    struct model_reference *reference,
    const uint8_t *bytes,
    size_t count
);

size_t tty_model_reference_first_line_length(
    const struct model_reference *reference
);

bool tty_model_reference_output_pending(
    const struct model_reference *reference
);

bool tty_model_reference_output_take(
    struct model_reference *reference,
    uint8_t *byte,
    bool *writable
);

void tty_model_reference_receive_byte(
    struct model_reference *reference,
    uint8_t byte
);

bool tty_model_reference_write_accept(
    struct model_reference *reference,
    const uint8_t *bytes,
    size_t count
);

bool tty_model_reference_write_arm_retry(
    struct model_reference *reference
);

struct micros_ipc_message tty_model_submit_request(
    struct tty_model *model,
    uint32_t type,
    uint64_t request_id,
    micros_grant_t grant,
    uint64_t count
);

void tty_model_write_u32_le(uint8_t *bytes, uint32_t value);

void tty_model_write_u64_le(uint8_t *bytes, uint64_t value);

#endif
