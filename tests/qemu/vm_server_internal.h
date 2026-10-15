#ifndef MICROS_TESTS_QEMU_VM_SERVER_INTERNAL_H
#define MICROS_TESTS_QEMU_VM_SERVER_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "micros/runtime.h"
#include "micros/bootstrap_control.h"
#include "micros/vm_bootstrap.h"
#include "servers/vm/vm_core.h"
#include "servers/vm/vm_service_core.h"
#include "tests/qemu/vm_handoff_protocol.h"

#if defined(MICROS_VM_SCRATCH_PREEMPTION_TEST)
#include "tests/qemu/vm_scratch_preemption_protocol.h"
#define MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID \
    MICROS_VM_SCRATCH_PREEMPTION_LAUNCHER_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID \
    MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID \
    MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_SERVICE_COUNT \
    MICROS_VM_SCRATCH_PREEMPTION_SERVICE_COUNT
#elif defined(MICROS_BUILD_ELF_SPAWN_TEST)
#include "tests/qemu/elf_spawn_protocol.h"
#define MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID \
    MICROS_ELF_SPAWN_LAUNCHER_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID MICROS_ELF_SPAWN_VM_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID MICROS_ELF_SPAWN_TTY_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_SERVICE_COUNT MICROS_ELF_SPAWN_SERVICE_COUNT
#define MICROS_VM_TTY_HANDOFF_TEST
#elif defined(MICROS_VFS_SERVICE_TEST)
#include "tests/qemu/vfs_service_protocol.h"
#define MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID \
    MICROS_VFS_TEST_LAUNCHER_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID \
    MICROS_VFS_TEST_VM_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID \
    MICROS_VFS_TEST_TTY_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_SERVICE_COUNT MICROS_VFS_TEST_SERVICE_COUNT
#define MICROS_VM_TTY_HANDOFF_TEST
#elif defined(MICROS_RAMFS_SERVICE_TEST)
#include "tests/qemu/ramfs_service_protocol.h"
#define MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID \
    MICROS_RAMFS_TEST_LAUNCHER_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID MICROS_RAMFS_TEST_VM_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID MICROS_RAMFS_TEST_TTY_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_SERVICE_COUNT MICROS_RAMFS_TEST_SERVICE_COUNT
#define MICROS_VM_TTY_HANDOFF_TEST
#elif defined(MICROS_TTY_HANDOFF_TEST)
#include "tests/qemu/tty_handoff_protocol.h"
#define MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID \
    MICROS_TTY_HANDOFF_TEST_LAUNCHER_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID MICROS_TTY_HANDOFF_TEST_VM_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID MICROS_TTY_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_SERVICE_COUNT MICROS_TTY_HANDOFF_TEST_SERVICE_COUNT
#define MICROS_VM_TTY_HANDOFF_TEST
#elif defined(MICROS_PM_SERVICE_TEST)
#include "tests/qemu/pm_service_protocol.h"
#define MICROS_VM_HANDOFF_TEST_LAUNCHER_SERVICE_ID MICROS_PM_TEST_LAUNCHER_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_VM_SERVICE_ID MICROS_PM_TEST_VM_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_PROBE_SERVICE_ID MICROS_PM_TEST_PROBE_SERVICE_ID
#define MICROS_VM_HANDOFF_TEST_SERVICE_COUNT MICROS_PM_TEST_SERVICE_COUNT
#endif

#ifdef MICROS_VM_TTY_HANDOFF_TEST
struct vm_tty_mapping_descriptor {
    bool mapped;
    micros_endpoint_t endpoint;
    uint64_t virtual_base;
    uint64_t physical_base;
    uint32_t irq_source;
    uint32_t mapped_length;
};
#endif

enum vm_server_control_mode {
    VM_SERVER_CONTROL_UNINITIALIZED,
    VM_SERVER_CONTROL_STATIC_ONLY,
    VM_SERVER_CONTROL_READY,
};

struct vm_server_context {
    volatile uint64_t vm_data;
    struct micros_vm_state vm_state;
    struct micros_vm_service control;
    enum vm_server_control_mode control_mode;
#if !defined(MICROS_PM_SERVICE_TEST) \
    && !defined(MICROS_VM_TTY_HANDOFF_TEST) \
    && !defined(MICROS_VM_SCRATCH_PREEMPTION_TEST)
    uint8_t grant_buffer[MICROS_VM_HANDOFF_TEST_DATA_SIZE];
#endif
#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST
    struct micros_vm_load_begin_result scratch_begin_result;
#endif
#ifdef MICROS_VM_TTY_HANDOFF_TEST
    struct vm_tty_mapping_descriptor tty_mapping;
#endif
#ifdef MICROS_PM_SERVICE_TEST
    uint64_t pm_test_load_token;
    uint64_t pm_test_mapping_generation;
#endif
};

extern volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config;
extern struct micros_vm_boot_info micros_vm_boot_info;

struct vm_server_context *vm_server_context_get(void);
void vm_server_clear_bytes(void *storage, size_t size);
bool vm_server_bytes_are_zero(const void *storage, size_t size);
void vm_server_write_u32_le(uint8_t *bytes, uint32_t value);
uint32_t vm_server_read_u32_le(const uint8_t *bytes);
uint64_t vm_server_read_u64_le(const uint8_t *bytes);
int vm_server_find_service(uint32_t service_id);
bool vm_server_validate_configuration(void);
bool vm_server_send_ready(void);
bool vm_server_complete_handoff(void);
bool vm_server_configure_control(struct vm_server_context *context);
bool vm_server_serve_control(
    struct vm_server_context *context, const struct micros_ipc_message *request
);
void micros_vm_trigger_self_fault(void);

#if !defined(MICROS_PM_SERVICE_TEST) \
    && !defined(MICROS_VM_TTY_HANDOFF_TEST) \
    && !defined(MICROS_VM_SCRATCH_PREEMPTION_TEST)
bool vm_server_serve_probe(struct vm_server_context *context);
#endif
#ifdef MICROS_VM_TTY_HANDOFF_TEST
bool vm_server_map_tty_uart(struct vm_server_context *context);
#endif
#ifdef MICROS_VM_SCRATCH_PREEMPTION_TEST
_Noreturn void vm_server_run_scratch_preemption(struct vm_server_context *context);
void micros_vm_scratch_preemption_vm_hook(uint64_t magic);
_Noreturn void micros_vm_scratch_preemption_stale_load(uint64_t virtual_address);
#endif
#ifdef MICROS_PM_SERVICE_TEST
bool pm_test_serve_request(
    struct vm_server_context *context,
    const struct micros_ipc_message *request
);
#endif

#endif
