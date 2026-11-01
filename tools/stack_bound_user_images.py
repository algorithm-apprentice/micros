"""Checked user observation-object addresses from actual validated ELFs."""

from dataclasses import dataclass
import hashlib
import re

from tools import check_application_elf, check_user_elf
from tools.stack_bound_types import StackEvidenceError, require_path


@dataclass(frozen=True)
class UserObserver:
    image: str
    image_id: int
    address: int
    size: int
    stack_bottom: int
    stack_top: int
    elf_sha256: str
    regions: tuple = ()


USER_IMAGE_SCOPES = {
    "riscv64-bootstrap-launcher-test": (
        (101, "bootstrap/micros-bootstrap-launcher.elf"),
        (102, "bootstrap/micros-bootstrap-probe.elf"),
        (103, "bootstrap/micros-bootstrap-probe.elf"),
    ),
    "riscv64-vm-scratch-preemption-test": (
        (101, "bootstrap/micros-bootstrap-launcher.elf"),
        (102, "bootstrap/micros-vm-server.elf"),
        (103, "bootstrap/micros-pm-service.elf"),
        (104, "bootstrap/micros-vm-scratch-preemption-driver.elf"),
    ),
    "riscv64-pm-service-test": (
        (101, "bootstrap/micros-bootstrap-launcher.elf"),
        (102, "bootstrap/micros-vm-server.elf"),
        (103, "bootstrap/micros-pm-service.elf"),
        (104, "bootstrap/micros-pm-service-probe.elf"),
        (201, "applications/micros-application-runtime-probe.elf"),
    ),
    "riscv64-tty-service-test": (
        (101, "bootstrap/micros-bootstrap-launcher.elf"),
        (102, "bootstrap/micros-vm-server.elf"),
        (103, "bootstrap/micros-pm-service.elf"),
        (104, "bootstrap/micros-tty-service.elf"),
        (105, "bootstrap/micros-tty-service-vfs.elf"),
    ),
    "riscv64-ramfs-service-test": (
        (101, "bootstrap/micros-bootstrap-launcher.elf"),
        (102, "bootstrap/micros-vm-server.elf"),
        (103, "bootstrap/micros-pm-service.elf"),
        (104, "bootstrap/micros-tty-service.elf"),
        (105, "bootstrap/micros-ramfs-service.elf"),
        (106, "bootstrap/micros-ramfs-service-vfs.elf"),
    ),
    "riscv64-vfs-service-test": (
        (101, "bootstrap/micros-bootstrap-launcher.elf"),
        (102, "bootstrap/micros-vm-server.elf"),
        (103, "bootstrap/micros-pm-service.elf"),
        (104, "bootstrap/micros-tty-service.elf"),
        (105, "bootstrap/micros-ramfs-service.elf"),
        (106, "bootstrap/micros-vfs-service.elf"),
        (107, "bootstrap/micros-vfs-service-probe.elf"),
    ),
    "riscv64-elf-spawn-test": (
        (101, "bootstrap/micros-bootstrap-launcher.elf"),
        (102, "bootstrap/micros-vm-server.elf"),
        (103, "bootstrap/micros-pm-service.elf"),
        (104, "bootstrap/micros-tty-service.elf"),
        (105, "bootstrap/micros-ramfs-service.elf"),
        (106, "bootstrap/micros-vfs-service.elf"),
        (202, "applications/micros-elf-spawn-init.elf"),
        (203, "applications/micros-elf-spawn-child.elf"),
    ),
}

STATIC_ONLY_USER_IMAGES = {
    "riscv64-elf-spawn-test": (
        (201, "applications/micros-application-runtime-probe.elf"),
    ),
}


def render_user_catalog(configure, rows):
    if configure not in USER_IMAGE_SCOPES or type(rows) is not tuple:
        raise StackEvidenceError("unimplemented user-observer catalog scope")
    if any(type(row) is not UserObserver for row in rows) or tuple(
        (row.image_id, row.image) for row in rows
    ) != USER_IMAGE_SCOPES[configure]:
        raise StackEvidenceError("user-observer image declaration is incomplete or duplicated")
    output = [
        "#ifndef MICROS_GENERATED_STACK_OBSERVER_IMAGES_H",
        "#define MICROS_GENERATED_STACK_OBSERVER_IMAGES_H",
        '#include "micros/stack_observation.h"',
        f"#define MICROS_STACK_USER_IMAGE_COUNT {len(rows)}",
        "static const struct micros_stack_user_image micros_stack_user_images[] = {",
    ]
    for row in rows:
        if (
            row.size != 64 or row.address % 8 or row.stack_bottom != 0x7ffff000
            or row.stack_top != 0x80000000 or not re.fullmatch("[0-9a-f]{64}", row.elf_sha256)
        ):
            raise StackEvidenceError("user-observer catalog has an invalid artifact contract")
        output.extend((
            "    {",
            f'        .image_id = {row.image_id}, .name = "{row.image}",',
            f"        .address = UINT64_C(0x{row.address:016x}), .size = 64,",
            f'        .elf_sha256 = "{row.elf_sha256}",',
            f"        .region_count = {len(row.regions)}, .regions = {{",
            *(f"            {{UINT64_C(0x{address:016x}), UINT64_C(0x{size:016x}), "
              f"UINT64_C(0x{digest:016x})}}," for address, size, digest in row.regions),
            "        },",
            "    },",
        ))
    return "\n".join((*output, "};", "#endif", ""))


def user_observer_descriptor(image, relative, image_id):
    require_path(relative)
    if type(image_id) is not int or not 0 < image_id <= 0xffffffff:
        raise StackEvidenceError("user observer has an invalid image identity")
    errors = (
        check_application_elf.validate_elf(image) if relative.startswith("applications/")
        else check_user_elf.validate_elf(image)
    )
    if errors:
        raise StackEvidenceError("user observer ELF failed its existing policy: " + "; ".join(errors))
    found = tuple(symbol for symbol in image.symbols if symbol.name == "micros_stack_user_observation")
    if len(found) != 1:
        raise StackEvidenceError("user ELF lacks one exact observation object")
    symbol, = found
    if (
        symbol.binding != 1 or symbol.symbol_type != 1 or symbol.size != 64
        or symbol.value % 8 or not 0 < symbol.section_index < len(image.sections)
    ):
        raise StackEvidenceError("user observer has a borrowed type, size or alignment")
    section = image.sections[symbol.section_index]
    loads = tuple(program for program in image.programs if (
        program.program_type == 1 and program.virtual_address <= symbol.value
        and symbol.value + symbol.size <= program.virtual_address + program.memory_size
    ))
    if (
        section.flags != 3 or section.section_type not in (1, 8)
        or not section.address <= symbol.value
        or symbol.value + symbol.size > section.address + section.size
        or len(loads) != 1 or loads[0].flags != 6
        or symbol.value + symbol.size > 0x7ffff000
    ):
        raise StackEvidenceError("user observer is outside its exact writable, nonexecutable image range")
    load, = loads
    start = symbol.value - load.virtual_address
    for offset in range(start, start + symbol.size):
        if offset < load.file_size and image.data[load.offset + offset] != 0:
            raise StackEvidenceError("user observer storage is not initially zero")
    return UserObserver(
        relative, image_id, symbol.value, 64, 0x7ffff000, 0x80000000,
        hashlib.sha256(image.data).hexdigest(),
        tuple(
            (program.virtual_address, program.file_size, immutable_digest(
                image.data[program.offset:program.offset + program.file_size],
            ))
            for program in sorted(image.programs, key=lambda row: row.virtual_address)
            if program.program_type == 1 and program.flags in (4, 5)
        ),
    )


def immutable_digest(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001b3) & ((1 << 64) - 1)
    return value
