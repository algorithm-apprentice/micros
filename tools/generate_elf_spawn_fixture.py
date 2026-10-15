#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools import check_application_elf, check_user_elf, generate_bootstrap_fixture
from tools.user_elf_format import ElfFormatError
from tools.user_elf_parse import load_elf


SERVICES = (
    ("launcher", 1, "BOOTSTRAP_LAUNCHER", 0, "CONTROLLER", 1),
    ("vm", 2, "VM", 1, "VM", 1),
    ("pm", 3, "PM", 2, "PM", 1),
    ("tty", 4, "TTY", 4, "CONSOLE_OWNER", 1),
    ("ramfs", 5, "RAMFS", 8, None, 1),
    ("vfs", 6, "VFS", 16, None, 1),
)
FIXED_LIMITS = {"vm": 160, "pm": 32, "ramfs": 192, "vfs": 80}
APPLICATIONS = ("init", "child")
OBJECTS = (
    ("vm", "service_context", "VM_CONTEXT"),
    ("pm", "service_context", "PM_CONTEXT"),
    ("tty", "tty_service", "TTY_STATE"),
    ("tty", "tty_uart", "TTY_UART"),
    ("ramfs", "ramfs_state", "RAMFS_STATE"),
    ("vfs", "vfs_state", "VFS_STATE"),
)
REPORT_SYMBOL = "micros_elf_spawn_report"
REPORT_BYTES = struct.pack("<II", 0x00100073, 0x00008067)


def _service_descriptors(images):
    descriptors = {}
    limits = {}
    for role, _, _, _, _, stack in SERVICES:
        descriptor = generate_bootstrap_fixture._validated_descriptor(
            images[role], "elf_spawn_" + role,
        )
        if (descriptor["vm_boot_info_address"] != 0) != (role == "vm"):
            raise ElfFormatError(f"wrong VM boot metadata for {role}")
        required = descriptor["page_count"] + stack + int(role == "launcher")
        limit = FIXED_LIMITS.get(role, required)
        if required > limit:
            raise ElfFormatError(f"{role} resident page limit exceeded")
        if role == "tty" and descriptor["image_end"] > 0x7fffe000:
            raise ElfFormatError("TTY image overlaps its fixed UART mapping")
        descriptors[role] = descriptor
        limits[role] = limit
    if sum(limits.values()) > 4096:
        raise ElfFormatError("aggregate user page limit exceeds 4096 pages")
    return descriptors, limits


def _writable_object(descriptor, role, name):
    symbol = descriptor["symbols"].get(name)
    if (
        symbol is None or symbol.symbol_type != 1
        or symbol.size == 0 or symbol.value % 8 != 0
    ):
        raise ElfFormatError(f"missing or invalid {role} state object: {name}")
    if not any(
        program.flags == check_user_elf.PROGRAM_READ | check_user_elf.PROGRAM_WRITE
        and symbol.value >= program.virtual_address
        and symbol.size <= program.virtual_address + program.memory_size - symbol.value
        for program in descriptor["loads"]
    ):
        raise ElfFormatError(f"{role} state object is not wholly writable: {name}")
    return symbol


def _application_report(image, role):
    errors = check_application_elf.validate_elf(image)
    if errors:
        raise ElfFormatError(f"{role} application: " + "; ".join(errors))
    symbol = check_user_elf.defined_symbols(image).get(REPORT_SYMBOL)
    if (
        symbol is None or symbol.symbol_type != 2
        or symbol.size != len(REPORT_BYTES) or symbol.value % 4 != 0
    ):
        raise ElfFormatError(f"missing or invalid {role} report symbol")
    if not any(
        program.flags == check_user_elf.PROGRAM_READ | check_user_elf.PROGRAM_EXECUTE
        and symbol.value >= program.virtual_address
        and symbol.size <= program.virtual_address + program.file_size - symbol.value
        for program in image.programs
    ):
        raise ElfFormatError(f"{role} report is outside file-backed RX bytes")
    if check_user_elf._program_file_bytes(image, symbol.value, symbol.size) != REPORT_BYTES:
        raise ElfFormatError(f"{role} report instructions are not exact ebreak/ret")
    return symbol.value


def _render_images(descriptors):
    output = []
    for role, _, _, _, _, _ in SERVICES:
        output.extend(generate_bootstrap_fixture._render_segment_bytes(descriptors[role]))
    output.extend((
        "const struct micros_bootstrap_image",
        "    micros_elf_spawn_test_images[MICROS_ELF_SPAWN_SERVICE_COUNT] = {",
    ))
    for role, service_id, _, _, _, _ in SERVICES:
        output.extend(generate_bootstrap_fixture._render_image(
            descriptors[role], 100 + service_id,
        ))
    return [*output, "};", ""]


def _render_manifest_entry(service, limit):
    role, service_id, profile, prerequisites, flag, stack = service
    name = "bootstrap-launcher" if role == "launcher" else role
    output = [
        "        {",
        f"            .service_id = {service_id},",
        f"            .image_id = {100 + service_id},",
        f"            .process_slot = {service_id - 1},",
        f"            .stack_page_count = {stack},",
        f"            .profile_id = MICROS_PRIVILEGE_PROFILE_{profile},",
        f'            .service_name = "{name}",',
        f'            .profile_name = "{profile}",',
        f"            .user_page_limit = {limit},",
        f"            .prerequisites = UINT64_C({prerequisites}),",
    ]
    if flag is not None:
        output.append(f"            .role_flags = MICROS_BOOTSTRAP_ROLE_{flag},")
    if role != "launcher":
        output.append("            .ready_timeout_counter_ticks = MICROS_ELF_SPAWN_READY_TIMEOUT,")
    if role == "tty":
        output.extend((
            "            .irq_source = MICROS_TTY_UART_IRQ_SOURCE,",
            "            .device_base = MICROS_TTY_UART_PHYSICAL_BASE,",
            "            .device_length = MICROS_TTY_UART_MAPPED_LENGTH,",
        ))
    return [*output, "        },"]


def _render_manifest(limits):
    output = [
        "const struct micros_bootstrap_manifest micros_elf_spawn_test_manifest = {",
        "    .header = {",
        "        .magic = MICROS_BOOTSTRAP_MANIFEST_MAGIC,",
        "        .version = MICROS_BOOTSTRAP_MANIFEST_VERSION,",
        "        .header_size = MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE,",
        "        .entry_size = MICROS_BOOTSTRAP_MANIFEST_ENTRY_SIZE,",
        "        .entry_capacity = MICROS_BOOTSTRAP_SERVICE_CAPACITY,",
        "        .entry_count = MICROS_ELF_SPAWN_SERVICE_COUNT,",
        f"        .total_user_page_limit = {sum(limits.values())},",
        "        .manifest_size = MICROS_BOOTSTRAP_MANIFEST_SIZE,",
        "    },",
        "    .entries = {",
    ]
    for service in SERVICES:
        output.extend(_render_manifest_entry(service, limits[service[0]]))
    return [*output, "    },", "};", ""]


def _render_objects(descriptors):
    output = [
        "const struct micros_elf_spawn_service_object",
        "    micros_elf_spawn_test_objects[MICROS_ELF_SPAWN_OBJECT_COUNT] = {",
    ]
    ids = {role: service_id for role, service_id, _, _, _, _ in SERVICES}
    for role, name, index in OBJECTS:
        symbol = _writable_object(descriptors[role], role, name)
        output.extend((
            f"    [MICROS_ELF_SPAWN_OBJECT_{index}] = {{",
            f"        .service_id = {ids[role]},",
            f"        .address = UINT64_C(0x{symbol.value:016x}),",
            f"        .size = UINT64_C(0x{symbol.size:016x}),",
            "    },",
        ))
    return [*output, "};", ""]


def render_fixture(images):
    roles = {service[0] for service in SERVICES} | set(APPLICATIONS)
    if set(images) != roles:
        raise ElfFormatError("fixture requires exactly six services and two applications")
    descriptors, limits = _service_descriptors(images)
    reports = [_application_report(images[role], role) for role in APPLICATIONS]
    output = [
        '#include "kernel/elf_spawn_test_fixture.h"',
        '#include "micros/tty.h"',
        "",
    ]
    output.extend(_render_images(descriptors))
    output.extend(_render_manifest(limits))
    output.extend(_render_objects(descriptors))
    output.append("const uint64_t micros_elf_spawn_report_addresses[2] = {")
    output.extend(f"    UINT64_C(0x{address:016x})," for address in reports)
    return "\n".join((*output, "};", ""))


def main(argv=None):
    parser = argparse.ArgumentParser(description="Generate the checked six-service ELF spawn fixture.")
    for role in (*[service[0] for service in SERVICES], *APPLICATIONS):
        parser.add_argument("--" + role + "-elf", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args(argv)
    try:
        images = {
            role: load_elf(getattr(arguments, role + "_elf"))
            for role in (*[service[0] for service in SERVICES], *APPLICATIONS)
        }
        content = render_fixture(images)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(content, encoding="utf-8")
    except (OSError, ElfFormatError, struct.error) as error:
        print(f"ELF spawn fixture generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
