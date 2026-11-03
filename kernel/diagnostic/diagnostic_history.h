#ifndef MICROS_KERNEL_DIAGNOSTIC_HISTORY_H
#define MICROS_KERNEL_DIAGNOSTIC_HISTORY_H

#include "kernel/diagnostic/diagnostic_history_core.h"

struct micros_hart;
struct micros_trap_frame;
struct micros_syscall_context;
struct micros_tty_control_request;
struct micros_tty_handoff_runtime_state;
struct micros_bootstrap_control_state;
struct micros_endpoint_registry;

const struct micros_diagnostic_history *micros_diagnostic_history_state(void);
void micros_diagnostic_record_byte(uint8_t byte);
void micros_diagnostic_record_end(void);
void micros_diagnostic_handoff(void);
bool micros_diagnostic_handoff_cursor(uint32_t *cursor);
bool micros_diagnostic_notification_pending(void);
void micros_diagnostic_notification_committed(void);
uint64_t micros_diagnostic_control_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_tty_control_request *request,
    const struct micros_tty_handoff_runtime_state *state,
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry
);
void micros_diagnostic_notify_pending(struct micros_hart *hart, struct micros_trap_frame *frame);
int32_t micros_diagnostic_panic_size(void);
uint8_t micros_diagnostic_panic_byte_prevalidated(uint32_t index);
void micros_diagnostic_panic_report(void);

#endif
