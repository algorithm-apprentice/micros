"""Independent temporary RV64 link for source-bound fatal proof regressions."""

import json
import os
from pathlib import Path
import shlex
import subprocess

from tools.stack_bound_bindings import reference_source
from tools.stack_bound_collect import collect_linked_c
from tools.stack_bound_objects import load_object


UNITS = (
    "arch/riscv64/uart.c", "arch/riscv64/sbi.c",
    "kernel/tty/uart_console_core.c", "kernel/diagnostic/panic.c",
    "arch/riscv64/panic.S",
)
SOURCES = (
    *UNITS, "arch/riscv64/interrupt.h", "arch/riscv64/platform.h",
    "arch/riscv64/panic_context.h", "arch/riscv64/trap_context.h",
    "arch/riscv64/scheduler_context.h", "kernel/tty/uart_console_core.h",
    "include/micros/panic.h",
)


def build_fatal_fixture(root, temporary):
    source_root, directory = temporary / "source", temporary / "metadata"
    source_root.mkdir()
    directory.mkdir()
    for unit in SOURCES:
        path = source_root / unit
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(reference_source(str(root), unit))
    commands, outputs, dependencies = [], [], {}
    stub = "kernel/fixture/fatal_mirrors.c"
    (source_root / stub).parent.mkdir(parents=True)
    (source_root / stub).write_text(
        "int micros_plic_panic_disable(void) { return 1; }\n"
        "int micros_tty_handoff_runtime_panic(void) { return 0; }\n"
    )
    for unit in (*UNITS, stub):
        relative = "CMakeFiles/micros.dir/" + unit + ".obj"
        output, source = directory / relative, source_root / unit
        output.parent.mkdir(parents=True, exist_ok=True)
        argv = [
            os.environ["MICROS_CLANG"], "--target=riscv64-unknown-elf",
            "-std=c17", "-march=rv64imac_zicsr_zifencei", "-mabi=lp64",
            "-mcmodel=medany", "-msmall-data-limit=0", "-mno-save-restore",
            "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie",
            "-ffunction-sections", "-fdata-sections", "-g0", '-DMICROS_VERSION="0.1.0"',
            "-I" + str(source_root), "-I" + str(source_root / "include"),
            "-I" + str(root), "-I" + str(root / "include"),
        ]
        if unit.endswith(".c"):
            argv.extend(("-fstack-usage", "-fstack-size-section"))
        argv.extend(("-c", str(source), "-o", str(output)))
        result = subprocess.run(argv, cwd=directory, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(f"{shlex.join(argv)}\n{result.stdout}{result.stderr}")
        commands.append({
            "directory": str(directory), "file": str(source),
            "output": str(output), "arguments": argv,
        })
        outputs.append(relative)
        dependencies[relative] = tuple(source_root / path for path in SOURCES if path.endswith(".h"))
    (directory / "compile_commands.json").write_text(json.dumps(commands))
    (directory / "CMakeCache.txt").write_text(
        "MICROS_QEMU:FILEPATH=/fixture/qemu-system-riscv64\n"
    )
    (directory / "kernel").mkdir()
    argv = [
        os.environ["MICROS_LD_LLD"], "-m", "elf64lriscv", "--no-relax",
        "--gc-sections",
        "--entry=micros_panic_report", "--undefined=sbi_set_timer", "-Ttext=0x80200000",
        "--Map=kernel/micros.map", "-o", "kernel/micros.elf", *outputs,
    ]
    result = subprocess.run(argv, cwd=directory, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise AssertionError(f"{shlex.join(argv)}\n{result.stdout}{result.stderr}")
    return source_root, directory, dependencies


def fixture_catalog(root, directory, dependencies):
    from unittest.mock import patch

    with patch("tools.stack_bound_collect.compiler_dependencies", return_value=dependencies):
        return collect_linked_c(root, directory, "kernel/micros.elf", "riscv64-fatal-fixture")


def changed_instruction(directory, unit, function, offset, word):
    from tools.stack_bound_objects import function_symbols

    path = directory / ("CMakeFiles/micros.dir/" + unit + ".obj")
    obj = load_object(path)
    symbol = function_symbols(obj)[function]
    from tools.stack_bound_flow import function_flow

    width, _ = function_flow(obj, symbol).instructions[offset]
    point = obj.sections[symbol.section_index].offset + symbol.value + offset
    data = bytearray(obj.data)
    data[point:point + width] = word.to_bytes(width, "little")
    return path, obj.data, bytes(data)
