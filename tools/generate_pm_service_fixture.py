#!/usr/bin/env python3

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools import check_user_elf
from tools import generate_bootstrap_fixture


REPORT_SYMBOL = "micros_pm_service_report"


def render_fixture(launcher_image, vm_image, pm_image, probe_image):
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
    probe = generate_bootstrap_fixture._validated_descriptor(
        probe_image,
        "probe",
    )
    if vm["vm_boot_info_address"] == 0:
        raise check_user_elf.ElfFormatError(
            "VM image has no boot-information object"
        )
    if any(
        descriptor["vm_boot_info_address"] != 0
        for descriptor in (launcher, pm, probe)
    ):
        raise check_user_elf.ElfFormatError(
            "non-VM image has VM boot-information metadata"
        )
    if REPORT_SYMBOL not in pm["symbols"]:
        raise check_user_elf.ElfFormatError(
            "missing PM service report symbol"
        )

    launcher_limit = launcher["page_count"] + 2
    vm_limit = vm["page_count"] + 1
    pm_limit = pm["page_count"] + 1
    probe_limit = probe["page_count"] + 1
    total_limit = launcher_limit + vm_limit + pm_limit + probe_limit
    output = [
        '#include "kernel/pm_service_test_fixture.h"',
        "",
    ]
    for descriptor in (launcher, vm, pm, probe):
        output.extend(
            generate_bootstrap_fixture._render_segment_bytes(descriptor)
        )
    output.extend(
        (
            "const struct micros_bootstrap_image",
            "    micros_pm_service_test_images[",
            "        MICROS_PM_TEST_SERVICE_COUNT",
            "    ] = {",
        )
    )
    output.extend(generate_bootstrap_fixture._render_image(launcher, 101))
    output.extend(generate_bootstrap_fixture._render_image(vm, 102))
    output.extend(generate_bootstrap_fixture._render_image(pm, 103))
    output.extend(generate_bootstrap_fixture._render_image(probe, 104))
    output.extend(
        (
            "};",
            "",
            "const struct micros_bootstrap_manifest",
            "    micros_pm_service_test_manifest = {",
            "        .header = {",
            "            .magic = MICROS_BOOTSTRAP_MANIFEST_MAGIC,",
            "            .version = MICROS_BOOTSTRAP_MANIFEST_VERSION,",
            "            .header_size = MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE,",
            "            .entry_size = MICROS_BOOTSTRAP_MANIFEST_ENTRY_SIZE,",
            "            .entry_capacity = MICROS_BOOTSTRAP_SERVICE_CAPACITY,",
            "            .entry_count = MICROS_PM_TEST_SERVICE_COUNT,",
            f"            .total_user_page_limit = {total_limit},",
            "            .manifest_size = MICROS_BOOTSTRAP_MANIFEST_SIZE,",
            "        },",
            "        .entries = {",
            "            {",
            "                .service_id = "
            "MICROS_PM_TEST_PROBE_SERVICE_ID,",
            "                .image_id = 104,",
            "                .process_slot = 3,",
            "                .stack_page_count = 1,",
            "                .profile_id = "
            "MICROS_PM_TEST_PROBE_PROFILE_ID,",
            '                .service_name = "pm-test-probe",',
            '                .profile_name = "PM_TEST_PROBE",',
            "                .prerequisites = "
            "UINT64_C(1) << "
            "(MICROS_PM_TEST_PM_SERVICE_ID - 1),",
            "                .ready_timeout_counter_ticks = "
            "MICROS_PM_TEST_READY_TIMEOUT,",
            f"                .user_page_limit = {probe_limit},",
            "            },",
            "            {",
            "                .service_id = "
            "MICROS_PM_TEST_LAUNCHER_SERVICE_ID,",
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
            "MICROS_PM_TEST_PM_SERVICE_ID,",
            "                .image_id = 103,",
            "                .process_slot = 2,",
            "                .stack_page_count = 1,",
            "                .profile_id = MICROS_PRIVILEGE_PROFILE_PM,",
            '                .service_name = "pm",',
            '                .profile_name = "PM",',
            "                .prerequisites = "
            "UINT64_C(1) << "
            "(MICROS_PM_TEST_VM_SERVICE_ID - 1),",
            "                .ready_timeout_counter_ticks = "
            "MICROS_PM_TEST_READY_TIMEOUT,",
            f"                .user_page_limit = {pm_limit},",
            "                .role_flags = MICROS_BOOTSTRAP_ROLE_PM,",
            "            },",
            "            {",
            "                .service_id = "
            "MICROS_PM_TEST_VM_SERVICE_ID,",
            "                .image_id = 102,",
            "                .process_slot = 1,",
            "                .stack_page_count = 1,",
            "                .profile_id = MICROS_PRIVILEGE_PROFILE_VM,",
            '                .service_name = "vm",',
            '                .profile_name = "VM",',
            "                .prerequisites = UINT64_C(1),",
            "                .ready_timeout_counter_ticks = "
            "MICROS_PM_TEST_READY_TIMEOUT,",
            f"                .user_page_limit = {vm_limit},",
            "                .role_flags = MICROS_BOOTSTRAP_ROLE_VM,",
            "            },",
            "        },",
            "    };",
            "",
            "const uint64_t micros_pm_service_test_report_address =",
            "    UINT64_C("
            f"0x{pm['symbols'][REPORT_SYMBOL].value:016x}"
            ");",
            "",
        )
    )
    return "\n".join(output)


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate the static PM service QEMU fixture."
    )
    parser.add_argument("--launcher-elf", required=True, type=Path)
    parser.add_argument("--vm-elf", required=True, type=Path)
    parser.add_argument("--pm-elf", required=True, type=Path)
    parser.add_argument("--probe-elf", required=True, type=Path)
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
        probe = check_user_elf.load_validated_elf(arguments.probe_elf)
        content = render_fixture(launcher, vm, pm, probe)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(content, encoding="utf-8")
    except (OSError, check_user_elf.ElfFormatError) as error:
        raise SystemExit(f"PM service fixture generation failed: {error}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
