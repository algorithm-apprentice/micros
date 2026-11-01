"""The eight finite callback groups in Accepted ADR-0084."""

from dataclasses import dataclass


@dataclass(frozen=True)
class Carrier:
    expression: str
    parameter: str
    prefix: tuple[int, ...] = ()


@dataclass(frozen=True)
class CallbackGroup:
    identifier: str
    source_prefix: str
    header: str
    types: tuple[str, ...]
    carriers: tuple[Carrier, ...]
    members: tuple[tuple[str, int | None, tuple[tuple[str, str], ...]], ...]
    constructors: tuple[tuple[str, str], ...]


PM_UNIT = "servers/pm/pm_service_backend.c"
VM_UNIT = "servers/vm/vm_service_backend.c"
VFS_UNIT = "servers/vfs/vfs_service.c"
TTY_UNIT = "servers/tty/tty_service.c"
RAMFS_UNIT = "servers/ramfs/ramfs_service.c"

PM_PROVIDERS = (
    ("reserve", "service_backend_reserve"),
    ("abort_reserved", "service_backend_abort_reserved"),
    ("prepare_execution", "service_backend_prepare"),
    ("abort_prepared", "service_backend_abort_prepared"),
    ("activate", "service_backend_activate"),
    ("exit_begin", "service_backend_exit_begin"),
    ("exit_detach", "service_backend_exit_detach"),
    ("release_exited_process", "service_backend_release"),
    ("grant_create", "service_backend_grant_create"),
    ("grant_revoke", "service_backend_grant_revoke"),
    ("grant_copy_from", "service_backend_grant_copy"),
    ("call", "service_backend_call"),
    ("reply", "service_backend_reply"),
)
VM_PROVIDERS = (
    ("copy_from", "vm_backend_copy"), ("load_begin", "vm_backend_begin"),
    ("scratch_map", "vm_backend_map"), ("scratch_page", "vm_backend_scratch"),
    ("scratch_commit", "vm_backend_scratch_commit"), ("freeze", "vm_backend_freeze"),
    ("abort_load", "vm_backend_abort"), ("commit_load", "vm_backend_commit"),
    ("exit_release", "vm_backend_exit"),
)
VFS_PROVIDERS = (
    "client_validate", "client_copy", "backend_grant_create",
    "backend_grant_revoke", "ramfs_call", "tty_call",
)

GROUPS = (
    CallbackGroup(
        "kernel-visitor", "kernel/memory/user_address_space_mapping.c",
        "include/micros/user_address_space.h", ("user_mapping_walk", "micros_user_mapping_visitor"),
        (Carrier("walk", "walk"),),
        (("visitor", 16, (
            ("kernel/vm/vm_snapshot.c", "collect_mapping"),
            ("kernel/vm/vm_snapshot_validation.c", "validate_mapping"),
        )),),
        (
            ("kernel/vm/vm_snapshot.c", "collect_snapshot_address_spaces"),
            ("kernel/vm/vm_snapshot_validation.c", "micros_vm_snapshot_validate_current"),
        ),
    ),
    CallbackGroup(
        "pm-translation", "kernel/pm/pm_control_syscall_core.c",
        "kernel/pm/pm_control_syscall_core.h", ("micros_pm_control_output_translate",),
        (Carrier("translate", "translate"),),
        (("", None, (("kernel/pm/pm_control_syscall_runtime.c", "translate_output"),)),),
        (("kernel/pm/pm_control_syscall_runtime.c", "micros_pm_control_syscall_prepare_output"),),
    ),
    CallbackGroup(
        "pm-backend", "servers/pm/", "servers/pm/pm_coordinator.h",
        ("micros_pm_coordinator_backend", "pm_coordinator_spawn", "coordinator_exit_transaction"),
        (Carrier("backend", "backend"), Carrier("spawn->backend", "spawn", (8,)), Carrier("exit->backend", "exit", (8,))),
        tuple((member, index * 8, ((PM_UNIT, provider),)) for index, (member, provider) in enumerate(PM_PROVIDERS)),
        ((PM_UNIT, "pm_service_backend_get"),),
    ),
    CallbackGroup(
        "vm-backend", "servers/vm/", "servers/vm/vm_service_core.h",
        ("micros_vm_service_backend",), (Carrier("backend", "backend"),),
        tuple((member, index * 8, ((VM_UNIT, provider),)) for index, (member, provider) in enumerate(VM_PROVIDERS)),
        ((VM_UNIT, "micros_vm_service_runtime_backend"),),
    ),
    CallbackGroup(
        "vfs-io", "servers/vfs/", "servers/vfs/vfs_core.h",
        ("micros_vfs_io",), (Carrier("io", "io"),),
        tuple((member, index * 8, ((VFS_UNIT, member),)) for index, member in enumerate(VFS_PROVIDERS)),
        ((VFS_UNIT, "micros_service_main"),),
    ),
    CallbackGroup(
        "tty-uart", "servers/tty/", "servers/tty/tty_uart.h",
        ("micros_tty_uart_bus", "tty_service_deferred_write"),
        (Carrier("bus", "bus"), Carrier("write->source", "context", (0,))),
        (("read", 0, ((TTY_UNIT, "uart_read"),)), ("write", 8, ((TTY_UNIT, "uart_write"),))),
        ((TTY_UNIT, "micros_service_main"),),
    ),
    CallbackGroup(
        "tty-copy", "servers/tty/", "servers/tty/tty_service_core.h",
        ("micros_tty_service_io",), (Carrier("io", "io"),),
        (("copy", 0, ((TTY_UNIT, "service_copy"),)),),
        ((TTY_UNIT, "micros_service_main"),),
    ),
    CallbackGroup(
        "ramfs-copy", "servers/ramfs/", "servers/ramfs/ramfs_core.h",
        ("micros_ramfs_io",), (Carrier("io", "io"),),
        (("copy", 0, ((RAMFS_UNIT, "service_copy"),)),),
        ((RAMFS_UNIT, "micros_service_main"),),
    ),
)
