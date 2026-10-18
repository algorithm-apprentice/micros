#!/usr/bin/env -S python3.15 -E -S -B

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools import check_user_elf
from tools import generate_bootstrap_fixture


VM_SYMBOLS = (
    "micros_vm_scratch_preemption_vm_hook_trap",
    "micros_vm_scratch_preemption_vm_hook_spin",
    "micros_vm_scratch_preemption_vm_hook_resume",
    "micros_vm_scratch_preemption_stale_load_fault",
    "micros_vm_scratch_preemption_stale_load_unexpected",
)
PEER_SYMBOLS = (
    "micros_vm_scratch_preemption_peer_hold_trap",
    "micros_vm_scratch_preemption_peer_hold_resume",
    "micros_vm_scratch_preemption_peer_run_trap",
    "micros_vm_scratch_preemption_peer_run_resume",
)


def _require_symbols(descriptor, names, image_name):
    missing = [name for name in names if name not in descriptor["symbols"]]
    if missing:
        raise check_user_elf.ElfFormatError(
            f"missing {image_name} scratch-preemption symbols: "
            + ", ".join(missing)
        )


def _render_address(name, symbol):
    return (
        f"const uint64_t {name} =\n"
        f"    UINT64_C(0x{symbol.value:016x});\n"
    )


def _validated_descriptors(launcher_image, vm_image, pm_image, driver_image):
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
    driver = generate_bootstrap_fixture._validated_descriptor(
        driver_image,
        "driver",
    )
    if vm["vm_boot_info_address"] == 0:
        raise check_user_elf.ElfFormatError(
            "VM image has no boot-information object"
        )
    if any(
        descriptor["vm_boot_info_address"] != 0
        for descriptor in (launcher, pm, driver)
    ):
        raise check_user_elf.ElfFormatError(
            "non-VM image has VM boot-information metadata"
        )
    _require_symbols(vm, VM_SYMBOLS, "VM")
    _require_symbols(pm, PEER_SYMBOLS, "PM peer")
    return launcher, vm, pm, driver


def _resident_limits(launcher, vm, pm, driver):
    launcher_limit = launcher["page_count"] + 2
    vm_limit = vm["page_count"] + 1
    pm_limit = pm["page_count"] + 1
    driver_limit = driver["page_count"] + 1
    return launcher_limit, vm_limit, pm_limit, driver_limit


def _render_images(descriptors):
    launcher, vm, pm, driver = descriptors
    output = []
    for descriptor in descriptors:
        output.extend(
            generate_bootstrap_fixture._render_segment_bytes(descriptor)
        )
    output.extend(
        (
            "const struct micros_bootstrap_image",
            "    micros_vm_scratch_preemption_test_images[",
            "        MICROS_VM_SCRATCH_PREEMPTION_SERVICE_COUNT",
            "    ] = {",
        )
    )
    output.extend(generate_bootstrap_fixture._render_image(launcher, 101))
    output.extend(generate_bootstrap_fixture._render_image(vm, 102))
    output.extend(generate_bootstrap_fixture._render_image(pm, 103))
    output.extend(generate_bootstrap_fixture._render_image(driver, 104))
    output.extend(("};", ""))
    return output


def _render_manifest(limits):
    launcher_limit, vm_limit, pm_limit, driver_limit = limits
    total_limit = launcher_limit + vm_limit + pm_limit + driver_limit
    return [
        "const struct micros_bootstrap_manifest",
        "    micros_vm_scratch_preemption_test_manifest = {",
        "        .header = {",
        "            .magic = MICROS_BOOTSTRAP_MANIFEST_MAGIC,",
        "            .version = MICROS_BOOTSTRAP_MANIFEST_VERSION,",
        "            .header_size = MICROS_BOOTSTRAP_MANIFEST_HEADER_SIZE,",
        "            .entry_size = MICROS_BOOTSTRAP_MANIFEST_ENTRY_SIZE,",
        "            .entry_capacity = MICROS_BOOTSTRAP_SERVICE_CAPACITY,",
        "            .entry_count = "
        "MICROS_VM_SCRATCH_PREEMPTION_SERVICE_COUNT,",
        f"            .total_user_page_limit = {total_limit},",
        "            .manifest_size = MICROS_BOOTSTRAP_MANIFEST_SIZE,",
        "        },",
        "        .entries = {",
        "            {",
        "                .service_id = "
        "MICROS_VM_SCRATCH_PREEMPTION_DRIVER_SERVICE_ID,",
        "                .image_id = 104,",
        "                .process_slot = 3,",
        "                .stack_page_count = 1,",
        "                .profile_id = "
        "MICROS_VM_SCRATCH_PREEMPTION_DRIVER_PROFILE_ID,",
        '                .service_name = "vm-scratch-driver",',
        '                .profile_name = "VM_SCRATCH_DRIVER",',
        "                .prerequisites = "
        "UINT64_C(1) << "
        "(MICROS_VM_SCRATCH_PREEMPTION_PM_SERVICE_ID - 1),",
        "                .ready_timeout_counter_ticks = "
        "MICROS_VM_SCRATCH_PREEMPTION_READY_TIMEOUT,",
        f"                .user_page_limit = {driver_limit},",
        "            },",
        "            {",
        "                .service_id = "
        "MICROS_VM_SCRATCH_PREEMPTION_LAUNCHER_SERVICE_ID,",
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
        "MICROS_VM_SCRATCH_PREEMPTION_PM_SERVICE_ID,",
        "                .image_id = 103,",
        "                .process_slot = 2,",
        "                .stack_page_count = 1,",
        "                .profile_id = MICROS_PRIVILEGE_PROFILE_PM,",
        '                .service_name = "pm",',
        '                .profile_name = "PM",',
        "                .prerequisites = "
        "UINT64_C(1) << "
        "(MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID - 1),",
        "                .ready_timeout_counter_ticks = "
        "MICROS_VM_SCRATCH_PREEMPTION_READY_TIMEOUT,",
        f"                .user_page_limit = {pm_limit},",
        "                .role_flags = MICROS_BOOTSTRAP_ROLE_PM,",
        "            },",
        "            {",
        "                .service_id = "
        "MICROS_VM_SCRATCH_PREEMPTION_VM_SERVICE_ID,",
        "                .image_id = 102,",
        "                .process_slot = 1,",
        "                .stack_page_count = 1,",
        "                .profile_id = MICROS_PRIVILEGE_PROFILE_VM,",
        '                .service_name = "vm",',
        '                .profile_name = "VM",',
        "                .prerequisites = UINT64_C(1),",
        "                .ready_timeout_counter_ticks = "
        "MICROS_VM_SCRATCH_PREEMPTION_READY_TIMEOUT,",
        f"                .user_page_limit = {vm_limit},",
        "                .role_flags = MICROS_BOOTSTRAP_ROLE_VM,",
        "            },",
        "        },",
        "    };",
        "",
    ]


def _render_addresses(vm, pm):
    addresses = (
        (
            "micros_vm_scratch_preemption_peer_hold_trap_address",
            pm["symbols"][PEER_SYMBOLS[0]],
        ),
        (
            "micros_vm_scratch_preemption_peer_hold_resume_address",
            pm["symbols"][PEER_SYMBOLS[1]],
        ),
        (
            "micros_vm_scratch_preemption_peer_run_trap_address",
            pm["symbols"][PEER_SYMBOLS[2]],
        ),
        (
            "micros_vm_scratch_preemption_peer_run_resume_address",
            pm["symbols"][PEER_SYMBOLS[3]],
        ),
        (
            "micros_vm_scratch_preemption_vm_hook_trap_address",
            vm["symbols"][VM_SYMBOLS[0]],
        ),
        (
            "micros_vm_scratch_preemption_vm_hook_spin_address",
            vm["symbols"][VM_SYMBOLS[1]],
        ),
        (
            "micros_vm_scratch_preemption_vm_hook_resume_address",
            vm["symbols"][VM_SYMBOLS[2]],
        ),
        (
            "micros_vm_scratch_preemption_stale_load_fault_address",
            vm["symbols"][VM_SYMBOLS[3]],
        ),
        (
            "micros_vm_scratch_preemption_stale_load_unexpected_address",
            vm["symbols"][VM_SYMBOLS[4]],
        ),
    )
    output = []
    for name, symbol in addresses:
        output.extend(_render_address(name, symbol).rstrip().splitlines())
        output.append("")
    return output


def render_fixture(launcher_image, vm_image, pm_image, driver_image):
    descriptors = _validated_descriptors(
        launcher_image, vm_image, pm_image, driver_image
    )
    limits = _resident_limits(*descriptors)
    output = [
        '#include "kernel/tests/vm/vm_scratch_preemption_test_fixture.h"',
        "",
    ]
    output.extend(_render_images(descriptors))
    output.extend(_render_manifest(limits))
    output.extend(_render_addresses(descriptors[1], descriptors[2]))
    return "\n".join(output)


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate the VM scratch-preemption QEMU fixture."
    )
    parser.add_argument("--launcher-elf", required=True, type=Path)
    parser.add_argument("--vm-elf", required=True, type=Path)
    parser.add_argument("--pm-elf", required=True, type=Path)
    parser.add_argument("--driver-elf", required=True, type=Path)
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
        driver = check_user_elf.load_validated_elf(
            arguments.driver_elf
        )
        content = render_fixture(launcher, vm, pm, driver)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(content, encoding="utf-8")
    except (OSError, check_user_elf.ElfFormatError) as error:
        raise SystemExit(
            f"VM scratch-preemption fixture generation failed: {error}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
