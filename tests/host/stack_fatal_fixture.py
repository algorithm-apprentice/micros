"""Independent temporary RV64 link for source-bound fatal proof regressions."""

import json
import os
from pathlib import Path
import shlex
import subprocess

from tools.stack_bound_bindings import reference_source
from tools.stack_bound_collect import collect_linked_c
from tools.stack_bound_objects import load_object
from tests.host.diagnostic_history_source import (
    FATAL_SOURCE_CONTRACTS, HISTORY_LAYOUT, diagnostic_header_source,
    THR_OBSERVATION, THR_OBSERVATION_HEADER,
)


ORIGINAL_UNITS = (
    "arch/riscv64/uart.c", "arch/riscv64/sbi.c",
    "kernel/tty/uart_console_core.c", "kernel/diagnostic/panic.c",
    "arch/riscv64/panic.S",
)
ORIGINAL_SOURCES = (
    *ORIGINAL_UNITS, "arch/riscv64/interrupt.h", "arch/riscv64/platform.h",
    "arch/riscv64/panic_context.h", "arch/riscv64/trap_context.h",
    "arch/riscv64/scheduler_context.h", "kernel/tty/uart_console_core.h",
    "include/micros/panic.h",
)
HISTORY_UNITS = (
    "kernel/diagnostic/diagnostic_history_core.c",
    "kernel/diagnostic/diagnostic_history.c",
    "kernel/diagnostic/panic_history.c",
)
HISTORY_HEADERS = (
    "include/micros/diagnostic.h", "kernel/diagnostic/diagnostic_history_core.h",
    "kernel/diagnostic/diagnostic_history.h",
)
UNITS = (*ORIGINAL_UNITS, *HISTORY_UNITS)
SOURCES = (*ORIGINAL_SOURCES, *HISTORY_UNITS, *HISTORY_HEADERS)


def install_diagnostic_fixture(source_root):
    from tests.host.c_source_records import function_records

    uart = source_root / "arch/riscv64/uart.c"
    text = uart.read_text()
    text = text.replace("void uart_write(", "static void uart_write_direct(", 1)
    text = text.replace("void uart_write_hex64(", "static void uart_write_hex64_direct(", 1)
    store = "    uart0[UART_THR] = (uint8_t)character;\n"
    text = text.replace(store, store + THR_OBSERVATION, 1)
    text = '#include "kernel/diagnostic/diagnostic_history.h"\n' + THR_OBSERVATION_HEADER + text
    quiesce = function_records(text)["uart_console_begin_quiesce_prevalidated"]
    body = quiesce.body + "    micros_diagnostic_handoff();\n"
    text = text.replace(
        quiesce.signature + "{" + quiesce.body + "}",
        quiesce.signature + "{" + body + "}", 1,
    )
    text += (
        "\nstatic void uart_write_record(const char *text) {\n"
        '    (void)text; MICROS_PANIC(0, "fixture-record");\n}\n'
        "\nstatic void uart_write_hex64_record(uint64_t value) {\n"
        '    (void)value; MICROS_PANIC(0, "fixture-record");\n}\n'
    )
    for name in ("uart_panic_mode", "uart_write", "uart_write_hex64"):
        text += FATAL_SOURCE_CONTRACTS[("arch/riscv64/uart.c", name)]
    uart.write_text(text)
    reporter = source_root / "kernel/diagnostic/panic.c"
    text = reporter.read_text()
    report = function_records(text)["micros_panic_report"]
    body = report.body.replace(
        "    uart_flush();\n", "    micros_diagnostic_panic_report();\n    uart_flush();\n", 1,
    )
    text = text.replace(
        report.signature + "{" + report.body + "}", report.signature + "{" + body + "}", 1,
    )
    reporter.write_text('#include "kernel/diagnostic/diagnostic_history.h"\n' + text)
    declarations = {
        HISTORY_HEADERS[0]: diagnostic_header_source(),
        HISTORY_HEADERS[1]: (
            "#ifndef FIXTURE_HISTORY_CORE_H\n#define FIXTURE_HISTORY_CORE_H\n"
            '#include <stdbool.h>\n#include "micros/diagnostic.h"\n' + HISTORY_LAYOUT
            + "bool micros_diagnostic_history_valid(const struct micros_diagnostic_history *history);\n#endif\n"
        ),
        HISTORY_HEADERS[2]: (
            "#include <stdint.h>\nvoid micros_diagnostic_handoff(void);\n"
            "int32_t micros_diagnostic_panic_size(void);\n"
            "uint8_t micros_diagnostic_panic_byte_prevalidated(uint32_t index);\n"
            "void micros_diagnostic_panic_report(void);\n"
        ),
    }
    for unit in HISTORY_UNITS:
        prefix = (
            '#include "kernel/diagnostic/diagnostic_history_core.h"\n'
            '#include "kernel/diagnostic/diagnostic_history.h"\n'
            '#include "arch/riscv64/platform.h"\n#include <stddef.h>\n'
        )
        if unit == HISTORY_UNITS[1]:
            prefix += "static struct micros_diagnostic_history diagnostic_history;\n"
        declarations[unit] = prefix + "".join(
            source for (owner, _), source in FATAL_SOURCE_CONTRACTS.items() if owner == unit
        )
    for unit, text in declarations.items():
        path = source_root / unit
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)


def build_fatal_fixture(root, temporary, *, tty_test=False):
    source_root, directory = temporary / "source", temporary / "metadata"
    source_root.mkdir()
    directory.mkdir()
    for unit in ORIGINAL_SOURCES:
        path = source_root / unit
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(reference_source(str(root), unit))
    install_diagnostic_fixture(source_root)
    units, sources = UNITS, SOURCES
    if tty_test:
        unit = "tests/qemu/diagnostic_history_kernel.c"
        header = "tests/qemu/diagnostic_history_kernel.h"
        (source_root / unit).parent.mkdir(parents=True, exist_ok=True)
        (source_root / header).write_text("void micros_diagnostic_test_observe_thr(void);\n")
        (source_root / unit).write_text(
            "#include <stdint.h>\nstatic uint64_t diagnostic_thr_writes;\n"
            + FATAL_SOURCE_CONTRACTS[(unit, "micros_diagnostic_test_observe_thr")]
        )
        units, sources = (*units, unit), (*sources, unit, header)
    commands, outputs, dependencies = [], [], {}
    stub = "kernel/fixture/fatal_mirrors.c"
    (source_root / stub).parent.mkdir(parents=True)
    (source_root / stub).write_text(
        "int micros_plic_panic_disable(void) { return 1; }\n"
        "int micros_tty_handoff_runtime_panic(void) { return 0; }\n"
    )
    for unit in (*units, stub):
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
        if tty_test:
            argv.append("-DMICROS_BUILD_TTY_SERVICE_TEST=1")
        argv.extend(("-c", str(source), "-o", str(output)))
        result = subprocess.run(argv, cwd=directory, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(f"{shlex.join(argv)}\n{result.stdout}{result.stderr}")
        commands.append({
            "directory": str(directory), "file": str(source),
            "output": str(output), "arguments": argv,
        })
        outputs.append(relative)
        dependencies[relative] = tuple(source_root / path for path in sources if path.endswith(".h"))
    (directory / "compile_commands.json").write_text(json.dumps(commands))
    (directory / "CMakeCache.txt").write_text(
        "MICROS_QEMU:FILEPATH=/fixture/qemu-system-riscv64\n"
        + ("MICROS_BUILD_TTY_SERVICE_TEST:BOOL=ON\n" if tty_test else "")
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
