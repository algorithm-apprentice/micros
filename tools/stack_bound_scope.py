"""Closed current static output and live-producer inventories for ADR-0084."""

from dataclasses import dataclass
from pathlib import Path

from tests.host.c_source_records import tokens
from tests.host.stack_source_projection import project_stack_source
from tools.stack_artifact_files import read_regular
from tools.stack_bound_bindings import reference_source
from tools.stack_bound_reports import Producer, producer_inventory
from tools.stack_bound_types import StackEvidenceError
from tools.stack_bound_user_images import STATIC_ONLY_USER_IMAGES, USER_IMAGE_SCOPES
from tools.user_elf_parse import load_elf


@dataclass(frozen=True)
class Scenario:
    gap: str
    workflow: str
    flag: str
    thread_generations: tuple
    payload: tuple = ()


SCENARIOS = {
    "riscv64-scheduler-test": Scenario(
        "B03-E006", "test-qemu-scheduler", "MICROS_BUILD_SCHEDULER_TEST", (1, 1),
        ("arch/riscv64/scheduler_test.S", "micros_scheduler_payload_start"),
    ),
    "riscv64-ipc-syscall-test": Scenario(
        "B04-E006", "test-qemu-ipc-syscall", "MICROS_BUILD_IPC_SYSCALL_TEST", (1, 1, 1),
        ("arch/riscv64/ipc_syscall_test.S", "micros_ipc_syscall_test_payload_start"),
    ),
    "riscv64-grant-syscall-test": Scenario(
        "B05-E006", "test-qemu-grant-syscall", "MICROS_BUILD_GRANT_SYSCALL_TEST", (1, 1, 1),
        ("arch/riscv64/grant_syscall_test.S", "micros_grant_syscall_payload_start"),
    ),
    "riscv64-bootstrap-launcher-test": Scenario(
        "B06-E005", "test-qemu-bootstrap-launcher", "MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST", (1, 1, 1),
    ),
    "riscv64-vm-scratch-preemption-test": Scenario(
        "B07-E005", "test-qemu-vm-scratch-preemption", "MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST", (1, 1, 1, 1),
    ),
    "riscv64-pm-service-test": Scenario(
        "B08-E006", "test-qemu-pm-service", "MICROS_BUILD_PM_SERVICE_TEST", (1, 1, 1, 1, 2),
    ),
    "riscv64-tty-service-test": Scenario(
        "B09-E009", "test-qemu-tty", "MICROS_BUILD_TTY_SERVICE_TEST", (1, 1, 1, 1, 1),
    ),
    "riscv64-ramfs-service-test": Scenario(
        "B10-E008", "test-qemu-ramfs", "MICROS_BUILD_RAMFS_SERVICE_TEST", (1, 1, 1, 1, 1, 1),
    ),
    "riscv64-vfs-service-test": Scenario(
        "B11-E006", "test-qemu-vfs", "MICROS_BUILD_VFS_SERVICE_TEST", (1, 1, 1, 1, 1, 1, 1),
    ),
    "riscv64-elf-spawn-test": Scenario(
        "B12-E007", "test-qemu-elf-spawn", "MICROS_BUILD_ELF_SPAWN_TEST", (1, 1, 1, 1, 1, 1, 1, 1),
    ),
}

KERNEL_ONLY = frozenset((
    "riscv64-debug", "riscv64-panic-test", "riscv64-trap-test", "riscv64-timer-test",
    "riscv64-uart-console-test", "riscv64-frame-allocator-test", "riscv64-trap-panic-test",
    "riscv64-mmu-test", "riscv64-object-model-test", "riscv64-endpoint-test",
    "riscv64-grant-test", "riscv64-ipc-test", "riscv64-ipc-ecall-core-test",
    "riscv64-ipc-syscall-panic-test", "riscv64-nested-trap-test", "riscv64-frame-ownership-test",
    "riscv64-user-address-space-test", "riscv64-address-space-handoff-test",
    "riscv64-user-execution-test", "riscv64-scheduler-invalid-outgoing-test",
    "riscv64-scheduler-invalid-next-test",
))
BOOT_PROBE = ("bootstrap/micros-bootstrap-launcher.elf", "bootstrap/micros-bootstrap-probe.elf")
VM_PROBE = (
    "bootstrap/micros-bootstrap-launcher.elf", "bootstrap/micros-vm-server.elf",
    "bootstrap/micros-vm-handoff-probe.elf",
)
OTHER_IMAGES = {
    "riscv64-user-runtime-test": ("user-runtime/micros-user-runtime-service.elf",),
    "riscv64-bootstrap-ready-timeout-test": BOOT_PROBE,
    "riscv64-bootstrap-manifest-panic-test": BOOT_PROBE,
    "riscv64-vm-handoff-test": VM_PROBE,
    "riscv64-vm-ready-early-test": VM_PROBE,
    "riscv64-vm-self-fault-test": VM_PROBE,
    "riscv64-vm-self-fault-sealed-test": VM_PROBE,
    "riscv64-tty-service-image": (*BOOT_PROBE, "bootstrap/micros-tty-service.elf"),
    "riscv64-ramfs-service-image": (*BOOT_PROBE, "bootstrap/micros-ramfs-service.elf"),
    "riscv64-vfs-service-image": (*BOOT_PROBE, "bootstrap/micros-vfs-service.elf"),
    "riscv64-application-images": (
        "applications/micros-application-minimal-probe.elf",
        "applications/micros-application-runtime-probe.elf",
        "applications/micros-application-runtime-probe.runtime.elf",
        "applications/micros-elf-spawn-init.elf",
        "applications/micros-elf-spawn-child.elf",
    ),
}
GENERATION_SOURCES = (
    "kernel/objects/kernel_object_thread.c", "kernel/objects/kernel_object_identity.c",
    "kernel/boot/bootstrap_preparation.c", "kernel/pm/pm_control_execution.c",
    "kernel/pm/pm_control_reservation.c", "kernel/pm/pm_control_exit.c",
    "kernel/tests/pm/pm_service_test_report.c",
    "tests/qemu/elf_spawn_init.c", "tests/qemu/elf_spawn_pm.c",
    "tests/qemu/vm_server_pm.c",
)


def expected_images(configure):
    if configure in SCENARIOS:
        users = tuple(path for _, path in (
            USER_IMAGE_SCOPES.get(configure, ()) + STATIC_ONLY_USER_IMAGES.get(configure, ())
        ))
    elif configure in OTHER_IMAGES:
        users = OTHER_IMAGES[configure]
    elif configure in KERNEL_ONLY:
        users = ()
    else:
        raise StackEvidenceError("unregistered static stack configuration")
    return tuple(sorted({"kernel/micros.elf", *users}))


def require_image_outputs(directory, configure):
    directory = Path(directory)
    actual = {str(path.relative_to(directory)) for path in directory.rglob("*.elf")}
    declared = expected_images(configure)
    if actual != set(declared):
        raise StackEvidenceError("missing or extra declared stack ELF outputs")
    for relative in declared:
        current = directory
        for part in Path(relative).parts:
            current /= part
            if current.is_symlink():
                raise StackEvidenceError("stack ELF output has a linked path")
        read_regular(current)
    return declared


def symbol_value(image, name):
    matches = tuple(symbol for symbol in image.symbols if (
        symbol.name == name and symbol.binding == 1 and symbol.section_index != 0
    ))
    if len(matches) != 1:
        raise StackEvidenceError("producer interval lacks its exact linked symbol")
    return matches[0].value


def producer_roles(configure):
    if configure not in SCENARIOS:
        raise StackEvidenceError("no live-producer declaration for this configuration")
    scenario = SCENARIOS[configure]
    roles = [
        ("kernel", "boot", 0, 0, True),
        ("kernel", "primary", 0, 0, configure == "riscv64-scheduler-test"),
    ]
    roles.extend(("kernel", "thread", slot, generation, True)
                 for slot, generation in enumerate(scenario.thread_generations))
    for image_id, relative in USER_IMAGE_SCOPES.get(configure, ()):
        slot, generation = (
            (image_id - 101, 1) if image_id < 200
            else {201: (4, 1), 202: (6, 1), 203: (7, 2)}[image_id]
        )
        roles.append((relative, "user", slot, generation, True))
    return tuple(roles)


def declared_producers(root, directory, proofs):
    root, directory = Path(root), Path(directory)
    configure = directory.parent.name
    if configure not in SCENARIOS:
        raise StackEvidenceError("no live-producer declaration for this configuration")
    for unit in GENERATION_SOURCES:
        actual = project_stack_source(unit, read_regular(root / unit)[0].decode())
        if tokens(actual) != tokens(reference_source(str(root), unit)):
            raise StackEvidenceError("producer-generation source contract changed")
    image = load_elf(directory / "kernel/micros.elf")
    bounds = {}
    for proof in proofs:
        identity = proof.root.function.image, proof.root.identifier
        if identity in bounds:
            raise StackEvidenceError("producer borrowed a duplicate root certificate")
        bounds[identity] = proof.bytes_used
    try:
        boot = bounds["kernel/micros.elf", "boot"]
        supervisor = bounds["kernel/micros.elf", "supervisor"]
        primary = max(supervisor, bounds["kernel/micros.elf", "primary-idle"])
        base = symbol_value(image, "__thread_kernel_stacks_start")
        if symbol_value(image, "__thread_kernel_stacks_end") - base != 128 * 16384:
            raise StackEvidenceError("producer thread pool differs from the Accepted capacity")
        result = []
        for relative, owner, slot, generation, required in producer_roles(configure):
            if owner == "boot":
                bottom, top, bound = symbol_value(image, "__boot_stack_bottom"), symbol_value(image, "__boot_stack_top"), boot
            elif owner == "primary":
                bottom, top, bound = symbol_value(image, "__trap_stack_bottom"), symbol_value(image, "__trap_stack_top"), primary
            elif owner == "thread":
                bottom, top, bound = base + slot * 16384, base + (slot + 1) * 16384, supervisor
            else:
                bottom, top, bound = 0x7ffff000, 0x80000000, bounds[relative, "user-startup"]
            result.append(Producer(relative, owner, slot, generation, bottom, top, bound, required))
    except KeyError as error:
        raise StackEvidenceError("producer lacks its complete static root proof") from error
    producer_inventory(tuple(result))
    return tuple(result)
