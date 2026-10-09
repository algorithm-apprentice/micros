#!/usr/bin/env python3

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools import check_user_elf
from tools import generate_bootstrap_fixture


REPORT_SYMBOL = "micros_tty_service_test_report"
TTY_UART_VIRTUAL_BASE = 0x000000007FFFE000


def render_fixture(
    launcher_image,
    vm_image,
    pm_image,
    tty_image,
    vfs_image,
):
    launcher = generate_bootstrap_fixture._validated_descriptor(
        launcher_image,
        "launcher",
    )
    vm = generate_bootstrap_fixture._validated_descriptor(
        vm_image,
        "vm",
    )
    pm = generate_bootstrap_fixture._validated_descriptor(
        pm_image,
        "pm",
    )
    tty = generate_bootstrap_fixture._validated_descriptor(
        tty_image,
        "tty",
    )
    vfs = generate_bootstrap_fixture._validated_descriptor(
        vfs_image,
        "vfs",
    )
    if vm["vm_boot_info_address"] == 0:
        raise check_user_elf.ElfFormatError(
            "VM image has no boot-information object"
        )
    if any(
        descriptor["vm_boot_info_address"] != 0
        for descriptor in (launcher, pm, tty, vfs)
    ):
        raise check_user_elf.ElfFormatError(
            "non-VM image has VM boot-information metadata"
        )
    if REPORT_SYMBOL not in vfs["symbols"]:
        raise check_user_elf.ElfFormatError(
            "missing TTY service report symbol"
        )
    if tty["image_end"] > TTY_UART_VIRTUAL_BASE:
        raise check_user_elf.ElfFormatError(
            "TTY image overlaps its fixed UART mapping"
        )

    launcher_limit = launcher["page_count"] + 2
    vm_limit = vm["page_count"] + 1
    pm_limit = pm["page_count"] + 1
    tty_limit = tty["page_count"] + 1
    vfs_limit = vfs["page_count"] + 1
    total_limit = (
        launcher_limit
        + vm_limit
        + pm_limit
        + tty_limit
        + vfs_limit
    )
    output = [
        '#include "kernel/tty_service_test_fixture.h"',
        "",
    ]
    for descriptor in (launcher, vm, pm, tty, vfs):
        output.extend(
            generate_bootstrap_fixture._render_segment_bytes(descriptor)
        )
    output.extend(
        (
            "const struct micros_bootstrap_image",
            "    micros_tty_service_test_images[",
            "        MICROS_TTY_HANDOFF_TEST_SERVICE_COUNT",
            "    ] = {",
        )
    )
    output.extend(generate_bootstrap_fixture._render_image(launcher, 101))
    output.extend(generate_bootstrap_fixture._render_image(vm, 102))
    output.extend(generate_bootstrap_fixture._render_image(pm, 103))
    output.extend(generate_bootstrap_fixture._render_image(tty, 104))
    output.extend(generate_bootstrap_fixture._render_image(vfs, 105))
    output.extend(
        (
            "};",
            "",
            "const struct micros_bootstrap_manifest",
            "    micros_tty_service_test_manifest = {",
            "        .header = {",
            "            .magic = MICROS_BOOTSTRAP_MANIFEST_MAGIC,",
            "            .version = MICROS_BOOTSTRAP_MANIFEST_VERSION,",
            "            .header_size = MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE,",
            "            .entry_size = MICROS_BOOTSTRAP_MANIFEST_ENTRY_SIZE,",
            "            .entry_capacity = MICROS_BOOTSTRAP_SERVICE_CAPACITY,",
            "            .entry_count = "
            "MICROS_TTY_HANDOFF_TEST_SERVICE_COUNT,",
            f"            .total_user_page_limit = {total_limit},",
            "            .manifest_size = MICROS_BOOTSTRAP_MANIFEST_SIZE,",
            "        },",
            "        .entries = {",
            "            {",
            "                .service_id = "
            "MICROS_TTY_HANDOFF_TEST_VFS_SERVICE_ID,",
            "                .image_id = 105,",
            "                .process_slot = 4,",
            "                .stack_page_count = 1,",
            "                .profile_id = MICROS_PRIVILEGE_PROFILE_VFS,",
            '                .service_name = "tty-test-vfs",',
            '                .profile_name = "VFS",',
            "                .prerequisites = "
            "UINT64_C(1) << "
            "(MICROS_TTY_HANDOFF_TEST_TTY_SERVICE_ID - 1),",
            "                .ready_timeout_counter_ticks = "
            "MICROS_TTY_HANDOFF_TEST_READY_TIMEOUT,",
            f"                .user_page_limit = {vfs_limit},",
            "            },",
            "            {",
            "                .service_id = "
            "MICROS_TTY_HANDOFF_TEST_LAUNCHER_SERVICE_ID,",
            "                .image_id = 101,",
            "                .process_slot = 0,",
            "                .stack_page_count = 1,",
            "                .profile_id = "
            "MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,",
            '                .service_name = "bootstrap-launcher",',
            '                .profile_name = "BOOTSTRAP_LAUNCHER",',
            f"                .user_page_limit = {launcher_limit},",
            "                .role_flags = "
            "MICROS_BOOTSTRAP_ROLE_CONTROLLER,",
            "            },",
            "            {",
            "                .service_id = "
            "MICROS_TTY_HANDOFF_TEST_TTY_SERVICE_ID,",
            "                .image_id = 104,",
            "                .process_slot = 3,",
            "                .stack_page_count = 1,",
            "                .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,",
            '                .service_name = "tty",',
            '                .profile_name = "TTY",',
            "                .prerequisites = "
            "UINT64_C(1) << "
            "(MICROS_TTY_HANDOFF_TEST_PM_SERVICE_ID - 1),",
            "                .ready_timeout_counter_ticks = "
            "MICROS_TTY_HANDOFF_TEST_READY_TIMEOUT,",
            f"                .user_page_limit = {tty_limit},",
            "                .role_flags = "
            "MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER,",
            "                .irq_source = "
            "MICROS_TTY_UART_IRQ_SOURCE,",
            "                .device_base = "
            "MICROS_TTY_UART_PHYSICAL_BASE,",
            "                .device_length = "
            "MICROS_TTY_UART_MAPPED_LENGTH,",
            "            },",
            "            {",
            "                .service_id = "
            "MICROS_TTY_HANDOFF_TEST_PM_SERVICE_ID,",
            "                .image_id = 103,",
            "                .process_slot = 2,",
            "                .stack_page_count = 1,",
            "                .profile_id = MICROS_PRIVILEGE_PROFILE_PM,",
            '                .service_name = "pm",',
            '                .profile_name = "PM",',
            "                .prerequisites = "
            "UINT64_C(1) << "
            "(MICROS_TTY_HANDOFF_TEST_VM_SERVICE_ID - 1),",
            "                .ready_timeout_counter_ticks = "
            "MICROS_TTY_HANDOFF_TEST_READY_TIMEOUT,",
            f"                .user_page_limit = {pm_limit},",
            "                .role_flags = MICROS_BOOTSTRAP_ROLE_PM,",
            "            },",
            "            {",
            "                .service_id = "
            "MICROS_TTY_HANDOFF_TEST_VM_SERVICE_ID,",
            "                .image_id = 102,",
            "                .process_slot = 1,",
            "                .stack_page_count = 1,",
            "                .profile_id = MICROS_PRIVILEGE_PROFILE_VM,",
            '                .service_name = "vm",',
            '                .profile_name = "VM",',
            "                .prerequisites = UINT64_C(1),",
            "                .ready_timeout_counter_ticks = "
            "MICROS_TTY_HANDOFF_TEST_READY_TIMEOUT,",
            f"                .user_page_limit = {vm_limit},",
            "                .role_flags = MICROS_BOOTSTRAP_ROLE_VM,",
            "            },",
            "        },",
            "    };",
            "",
            "const uint64_t micros_tty_service_test_report_address =",
            "    UINT64_C("
            f"0x{vfs['symbols'][REPORT_SYMBOL].value:016x}"
            ");",
            "",
        )
    )
    return "\n".join(output)


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate the static TTY service QEMU fixture."
    )
    parser.add_argument("--launcher-elf", required=True, type=Path)
    parser.add_argument("--vm-elf", required=True, type=Path)
    parser.add_argument("--pm-elf", required=True, type=Path)
    parser.add_argument("--tty-elf", required=True, type=Path)
    parser.add_argument("--vfs-elf", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    try:
        launcher = check_user_elf.load_validated_elf(
            arguments.launcher_elf
        )
        vm = check_user_elf.load_validated_elf(arguments.vm_elf)
        pm = check_user_elf.load_validated_elf(arguments.pm_elf)
        tty = check_user_elf.load_validated_elf(arguments.tty_elf)
        vfs = check_user_elf.load_validated_elf(arguments.vfs_elf)
        content = render_fixture(
            launcher,
            vm,
            pm,
            tty,
            vfs,
        )
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(content, encoding="utf-8")
    except (OSError, check_user_elf.ElfFormatError) as error:
        raise SystemExit(
            f"TTY service fixture generation failed: {error}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
