#ifndef MICROS_KERNEL_DIAGNOSTIC_CONTROL_CORE_H
#define MICROS_KERNEL_DIAGNOSTIC_CONTROL_CORE_H

#include "kernel/diagnostic/diagnostic_history_core.h"
#include "micros/endpoint.h"

struct micros_diagnostic_identity {
    uint32_t service_id;
    struct micros_process_handle process;
    struct micros_thread_handle thread;
    micros_endpoint_t endpoint;
    uint32_t profile_id;
    uint64_t root_physical_address;
};

struct micros_diagnostic_reader {
    bool registered;
    struct micros_diagnostic_identity identity;
    uint64_t virtual_base;
    uint64_t pages[3];
};

enum micros_diagnostic_control_result {
    MICROS_DIAGNOSTIC_CONTROL_OK = 0,
    MICROS_DIAGNOSTIC_CONTROL_ARGUMENT,
    MICROS_DIAGNOSTIC_CONTROL_STATE,
    MICROS_DIAGNOSTIC_CONTROL_MEMORY_FAULT,
    MICROS_DIAGNOSTIC_CONTROL_INVARIANT,
};

bool micros_diagnostic_reader_valid(const struct micros_diagnostic_reader *reader);
bool micros_diagnostic_reader_matches(
    const struct micros_diagnostic_reader *reader,
    const struct micros_diagnostic_identity *identity
);
enum micros_diagnostic_control_result micros_diagnostic_register(
    struct micros_diagnostic_reader *reader,
    const struct micros_diagnostic_identity *identity,
    uint64_t virtual_base,
    uint64_t length
);
enum micros_diagnostic_control_result micros_diagnostic_prepare_read(
    const struct micros_diagnostic_reader *reader,
    const struct micros_diagnostic_identity *identity,
    const struct micros_diagnostic_history *history,
    uint32_t previous,
    struct micros_diagnostic_span *span
);
void micros_diagnostic_copy_prevalidated(
    const struct micros_diagnostic_reader *reader,
    const struct micros_diagnostic_history *history,
    const struct micros_diagnostic_span *span
);
enum micros_diagnostic_control_result micros_diagnostic_read(
    const struct micros_diagnostic_reader *reader,
    const struct micros_diagnostic_identity *identity,
    const struct micros_diagnostic_history *history,
    uint32_t previous
);

#endif
