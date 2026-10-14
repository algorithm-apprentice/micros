#ifndef MICROS_SERVERS_PM_SERVICE_INTERNAL_H
#define MICROS_SERVERS_PM_SERVICE_INTERNAL_H

#include "micros/runtime.h"
#include "micros/bootstrap_control.h"
#include "servers/pm/pm_control.h"
#include "servers/pm/pm_core.h"
#include "servers/pm/pm_coordinator.h"

enum {
    MICROS_PM_SERVICE_ID = 3,
};

enum pm_service_coordinator_mode {
    PM_SERVICE_COORDINATOR_STATIC_ONLY = 0,
    PM_SERVICE_COORDINATOR_READY,
};

#ifdef MICROS_PM_SERVICE_TEST
#include "tests/qemu/pm_service_protocol.h"
_Noreturn void micros_pm_service_report(uint64_t magic, uint64_t transaction);
#endif
#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST
#include "tests/qemu/vm_scratch_preemption_protocol.h"
void micros_vm_scratch_preemption_peer_hold(uint64_t magic);
void micros_vm_scratch_preemption_peer_run(uint64_t magic);
#endif

struct pm_service_context {
    struct micros_pm_table process_table;
    volatile uint64_t pm_service_data;
    struct micros_pm_coordinator coordinator;
    enum pm_service_coordinator_mode coordinator_mode;
#ifdef MICROS_PM_SERVICE_TEST
    struct micros_pm_table initial_table;
#endif
};

extern volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;

struct pm_service_context *pm_service_context_get(void);
void pm_service_clear_bytes(void *storage, size_t size);
bool pm_service_bytes_are_zero(const uint8_t *bytes, size_t size);
void pm_service_write_u32_le(uint8_t *bytes, uint32_t value);
uint32_t pm_service_read_u32_le(const uint8_t *bytes);
bool pm_service_validate_configuration(void);
void pm_service_configure_coordinator(struct pm_service_context *context);
const struct micros_pm_coordinator_backend *pm_service_backend_get(void);
bool pm_service_send_ready(void);
#if defined(MICROS_VM_SCRATCH_PREEMPTION_TEST) \
    || defined(MICROS_PM_SERVICE_TEST)
void pm_service_write_u64_le(uint8_t *bytes, uint64_t value);
#endif
#ifdef MICROS_PM_SERVICE_TEST
void pm_service_copy_bytes(void *destination, const void *source, size_t size);
bool pm_service_bytes_equal(const void *left, const void *right, size_t size);
void pm_service_fill_bytes(void *storage, size_t size, uint8_t value);
bool pm_service_bytes_have_value(const void *storage, size_t size, uint8_t value);
uint64_t pm_service_read_u64_le(const uint8_t *bytes);
_Noreturn void pm_service_run_self_test(struct pm_service_context *context);
#endif
#ifndef MICROS_VM_SCRATCH_PREEMPTION_TEST
enum micros_pm_coordinator_response_kind pm_service_serve_request(
    struct pm_service_context *context,
    const struct micros_ipc_message *request,
    struct micros_ipc_message *reply
);
#endif
#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST
_Noreturn void pm_service_run_scratch_preemption_test(
    const struct micros_ipc_message *request
);
#endif

#endif
