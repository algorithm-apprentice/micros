#include "tests/host/pm_coordinator_test_internal.h"
#include "servers/pm/pm_service_internal.h"
#include "lib/runtime/raw_syscall.h"

struct coordinator_backend_capture {
    uint64_t registers[8];
    int64_t result;
    size_t calls;
};

static struct coordinator_backend_capture backend_capture;

int64_t micros_runtime_raw_syscall(
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
    uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7
)
{
    backend_capture.registers[0] = a0;
    backend_capture.registers[1] = a1;
    backend_capture.registers[2] = a2;
    backend_capture.registers[3] = a3;
    backend_capture.registers[4] = a4;
    backend_capture.registers[5] = a5;
    backend_capture.registers[6] = a6;
    backend_capture.registers[7] = a7;
    ++backend_capture.calls;
    return backend_capture.result;
}

static bool backend_registers_are(
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
    uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7
)
{
    const uint64_t expected[8] = {a0, a1, a2, a3, a4, a5, a6, a7};

    COORDINATOR_EXPECT(memcmp(backend_capture.registers, expected, sizeof(expected)) == 0);
    return true;
}

static bool test_backend_kernel_ports(const struct micros_pm_coordinator_backend *backend)
{
    struct micros_pm_reservation_result reservation;
    struct micros_pm_prepared_result prepared;
    struct micros_pm_exit_begin_result exit;

    backend_capture.result = MICROS_SYSCALL_ABI_UNAUTHORIZED;
    COORDINATOR_EXPECT(
        backend->reserve(NULL, &reservation) == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && backend_registers_are(MICROS_PM_CONTROL_RESERVE, (uintptr_t)&reservation, 32,
                                0, 0, 0, 0, MICROS_SYSCALL_ABI_PM_CONTROL)
        && backend->abort_reserved(NULL, 101) == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && backend_registers_are(MICROS_PM_CONTROL_ABORT_RESERVED, 101, 0, 0,
                                0, 0, 0, MICROS_SYSCALL_ABI_PM_CONTROL)
        && backend->prepare_execution(NULL, 101, 201, 301, &prepared)
            == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && backend_registers_are(MICROS_PM_CONTROL_PREPARE_EXECUTION, 101, 201, 301,
                                (uintptr_t)&prepared, 32, 0, MICROS_SYSCALL_ABI_PM_CONTROL)
        && backend->abort_prepared(NULL, 101, 201, 301, 401) == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && backend_registers_are(MICROS_PM_CONTROL_ABORT_PREPARED, 101, 201, 301,
                                401, 0, 0, MICROS_SYSCALL_ABI_PM_CONTROL)
        && backend->activate(NULL, 101, 201, 301, 401) == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && backend_registers_are(MICROS_PM_CONTROL_ACTIVATE, 101, 201, 301,
                                401, 0, 0, MICROS_SYSCALL_ABI_PM_CONTROL)
        && backend->exit_begin(NULL, UINT32_C(0x1007), &exit) == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && backend_registers_are(MICROS_PM_CONTROL_EXIT_BEGIN, UINT32_C(0x1007),
                                (uintptr_t)&exit, 32, 0, 0, 0, MICROS_SYSCALL_ABI_PM_CONTROL)
        && backend->exit_detach(NULL, 701, UINT32_C(0x1007)) == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && backend_registers_are(MICROS_PM_CONTROL_EXIT_DETACH, 701, UINT32_C(0x1007), 0,
                                0, 0, 0, MICROS_SYSCALL_ABI_PM_CONTROL)
        && backend->release_exited_process(NULL, 701) == MICROS_SYSCALL_ABI_UNAUTHORIZED
        && backend_registers_are(MICROS_PM_CONTROL_RELEASE_EXITED_PROCESS, 701, 0, 0,
                                0, 0, 0, MICROS_SYSCALL_ABI_PM_CONTROL)
    );
    return true;
}

static bool test_backend_transport_ports(const struct micros_pm_coordinator_backend *backend)
{
    struct micros_ipc_message message;
    micros_grant_t grant = UINT32_MAX;
    uint8_t bytes[64];

    backend_capture.result = UINT32_C(0x101);
    COORDINATOR_EXPECT(
        backend->grant_create(NULL, UINT32_C(0x1005), (uintptr_t)bytes, sizeof(bytes),
                              MICROS_GRANT_PERMISSION_READ, &grant) == MICROS_SYSCALL_ABI_OK
        && grant == UINT32_C(0x101)
        && backend_registers_are(UINT32_C(0x1005), (uintptr_t)bytes, sizeof(bytes),
                                MICROS_GRANT_PERMISSION_READ, 0, 0, 0,
                                MICROS_SYSCALL_ABI_GRANT_CREATE)
    );
    backend_capture.result = MICROS_SYSCALL_ABI_STALE_GRANT;
    COORDINATOR_EXPECT(
        backend->grant_revoke(NULL, grant) == MICROS_SYSCALL_ABI_STALE_GRANT
        && backend_registers_are(grant, 0, 0, 0, 0, 0, 0, MICROS_SYSCALL_ABI_GRANT_REVOKE)
        && backend->grant_copy_from(NULL, UINT32_C(0x1006), grant, 9, (uintptr_t)bytes, 13)
            == MICROS_SYSCALL_ABI_STALE_GRANT
        && backend_registers_are(UINT32_C(0x1006), grant, 9, (uintptr_t)bytes,
                                13, 0, 0, MICROS_SYSCALL_ABI_GRANT_COPY_FROM)
        && backend->call(NULL, UINT32_C(0x1005), &message) == MICROS_SYSCALL_ABI_STALE_GRANT
        && backend_registers_are(UINT32_C(0x1005), (uintptr_t)&message, 0, 0,
                                0, 0, 0, MICROS_SYSCALL_ABI_CALL)
        && backend->reply(NULL, 777, &message) == MICROS_SYSCALL_ABI_STALE_GRANT
        && backend_registers_are(777, (uintptr_t)&message, 0, 0,
                                0, 0, 0, MICROS_SYSCALL_ABI_REPLY)
    );
    return true;
}

bool test_coordinator_real_runtime_backend(void)
{
    const struct micros_pm_coordinator_backend *backend = pm_service_backend_get();

    memset(&backend_capture, 0, sizeof(backend_capture));
    COORDINATOR_EXPECT(
        backend != NULL && test_backend_kernel_ports(backend)
        && test_backend_transport_ports(backend) && backend_capture.calls == 13
    );
    return true;
}
