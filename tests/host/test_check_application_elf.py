from dataclasses import replace
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from tests.host.application_elf_test_fixture import build_application_elf
from tools import check_application_elf
from tools.user_elf_format import (
    ElfSymbol,
    PAGE_SIZE,
    PROGRAM_HEADER,
    SYMBOL_BIND_GLOBAL,
    USER_BASE,
)
from tools.user_elf_parse import parse_elf


def application_errors(data):
    return check_application_elf.validate_elf(parse_elf(bytes(data)))


class ApplicationElfCheckerTest(unittest.TestCase):
    def cli_result(self, data, runtime=None, module=False):
        root = Path(__file__).resolve().parents[2]
        environment = os.environ.copy()
        environment.pop("PYTHONPATH", None)
        command = [sys.executable, "-B"]
        if module:
            environment["PYTHONPATH"] = str(root)
            command.extend(("-m", "tools.check_application_elf"))
        else:
            command.append(str(root / "tools/check_application_elf.py"))
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "application.elf"
            if data is not None:
                path.write_bytes(data)
            command.extend(("--elf", str(path)))
            if runtime is not None:
                artifact = Path(temporary) / "runtime.elf"
                artifact.write_bytes(runtime)
                command.extend(("--runtime-elf", str(artifact)))
            return subprocess.run(
                command, cwd=temporary, env=environment, text=True,
                capture_output=True, timeout=10, check=False,
            )

    def test_accepts_exact_application_and_seeded_bytes(self):
        data = build_application_elf()
        image = parse_elf(bytes(data))
        self.assertEqual([], check_application_elf.validate_elf(image))
        self.assertEqual([], check_application_elf.validate_seeded_elf(image, bytes(data)))

    def test_header_identity_is_stricter_than_service_policy(self):
        for offset, value, expected in (
            (7, 1, "OS ABI"), (8, 1, "ABI version"), (9, 1, "padding"),
        ):
            with self.subTest(offset=offset):
                data = build_application_elf()
                data[offset] = value
                self.assertIn(expected, "\n".join(application_errors(data)))
        image = parse_elf(bytes(build_application_elf()))
        for field, value, expected in (
            ("program_offset", 128, "program-header offset"),
            ("program_count", 4, "program-header count"),
            ("program_entry_size", 64, "program-header entry size"),
        ):
            with self.subTest(field=field):
                changed = replace(image, header=replace(image.header, **{field: value}))
                self.assertIn(expected, "\n".join(check_application_elf.validate_elf(changed)))

    def test_requires_page_closed_contiguous_ordered_loads(self):
        image = parse_elf(bytes(build_application_elf()))
        for index, field, value, expected in (
            (0, "memory_size", PAGE_SIZE - 1, "page-closed"),
            (1, "virtual_address", USER_BASE + 2 * PAGE_SIZE, "contiguous"),
            (2, "offset", image.programs[1].offset, "file ranges"),
            (0, "program_type", 3, "PT_LOAD"),
        ):
            with self.subTest(index=index, field=field):
                programs = list(image.programs)
                programs[index] = replace(programs[index], **{field: value})
                changed = replace(image, programs=tuple(programs))
                self.assertIn(expected, "\n".join(check_application_elf.validate_elf(changed)))
        changed = replace(image, programs=tuple(reversed(image.programs)))
        self.assertIn("order", "\n".join(check_application_elf.validate_elf(changed)))

    def test_enforces_file_and_resident_budgets(self):
        data = build_application_elf()
        data.extend(bytes(131073 - len(data)))
        self.assertIn("131072", "\n".join(application_errors(data)))
        image = parse_elf(bytes(build_application_elf()))
        programs = list(image.programs)
        programs[2] = replace(programs[2], memory_size=14 * PAGE_SIZE)
        changed = replace(image, programs=tuple(programs))
        self.assertIn("15 image pages", "\n".join(check_application_elf.validate_elf(changed)))

    def test_reuses_denied_section_and_load_closure_checks(self):
        for name in (".got", ".init_array", ".tdata", ".eh_frame", ".llvm_prf_data"):
            with self.subTest(name=name):
                errors = application_errors(build_application_elf(data_section_name=name))
                self.assertIn("forbidden section " + name, "\n".join(errors))
        image = parse_elf(bytes(build_application_elf()))
        for index, field, value, expected in (
            (0, "flags", 7, "permission"),
            (1, "physical_address", USER_BASE, "p_paddr"),
            (2, "file_size", PAGE_SIZE + 1, "p_filesz"),
            (2, "memory_size", 0, "empty"),
        ):
            with self.subTest(index=index, field=field):
                programs = list(image.programs)
                programs[index] = replace(programs[index], **{field: value})
                self.assertIn(expected, "\n".join(check_application_elf.validate_elf(
                    replace(image, programs=tuple(programs)),
                )))

    def test_rejects_missing_undefined_and_service_configuration_symbols(self):
        image = parse_elf(bytes(build_application_elf()))
        changed = replace(
            image, symbols=tuple(symbol for symbol in image.symbols
                                 if symbol.name != "micros_application_main"),
        )
        self.assertIn("missing required application symbol", "\n".join(
            check_application_elf.validate_elf(changed),
        ))
        for name, section, expected in (
            ("host_dependency", 0, "undefined symbol"),
            ("micros_bootstrap_service_config", 4, "bootstrap service"),
        ):
            with self.subTest(name=name):
                symbol = ElfSymbol(name, SYMBOL_BIND_GLOBAL, 1, section,
                                   USER_BASE + 0x2080, 128)
                changed = replace(image, symbols=image.symbols + (symbol,))
                self.assertIn(expected, "\n".join(check_application_elf.validate_elf(changed)))

    def test_checks_startup_global_extent_and_external_stack_boundary(self):
        image = parse_elf(bytes(build_application_elf()))
        for name, field, value, expected in (
            ("micros_application_pid", "size", 4, "startup global"),
            ("micros_application_pm_endpoint", "value", USER_BASE, "startup global"),
            ("__micros_application_stack_top", "value", 0x7FFFF000, "stack top"),
        ):
            with self.subTest(name=name):
                symbols = tuple(
                    replace(symbol, **{field: value}) if symbol.name == name else symbol
                    for symbol in image.symbols
                )
                self.assertIn(expected, "\n".join(
                    check_application_elf.validate_elf(replace(image, symbols=symbols)),
                ))

    def test_checks_exact_application_entry_raw_stub_and_fatal_loops(self):
        for offset, word, expected in (
            (0, 0x00000013, "application startup"),
            (8, 0x00000513, "application startup"),
            (16, 0x014080E7, "call target"),
            (28, 0x00100073, "raw syscall"),
            (44, 0x00000013, "breakpoint loop"),
            (40, 0x00000193, "gp or tp"),
        ):
            with self.subTest(offset=offset):
                data = build_application_elf()
                struct.pack_into("<I", data, 0x1000 + offset, word)
                self.assertIn(expected, "\n".join(application_errors(data)))

    def test_seeded_metadata_may_differ_but_no_executable_byte_may_change(self):
        data = build_application_elf()
        image = parse_elf(bytes(data))
        last_load_end = max(program.offset + program.file_size for program in image.programs)
        runtime = bytes(data[:last_load_end])
        self.assertEqual([], check_application_elf.validate_seeded_elf(image, runtime))
        changed = bytearray(runtime)
        changed[image.programs[1].offset + 7] ^= 1
        self.assertIn("PT_LOAD bytes", "\n".join(
            check_application_elf.validate_seeded_elf(image, bytes(changed)),
        ))
        changed = bytearray(runtime)
        changed[24] ^= 4
        self.assertIn("ELF header", "\n".join(
            check_application_elf.validate_seeded_elf(image, bytes(changed)),
        ))
        changed = bytearray(runtime)
        changed[image.header.program_offset + PROGRAM_HEADER.size + 4] ^= 1
        self.assertIn("program headers", "\n".join(
            check_application_elf.validate_seeded_elf(image, bytes(changed)),
        ))

    def test_seeded_truncated_load_data_is_an_explicit_failure(self):
        data = build_application_elf()
        image = parse_elf(bytes(data))
        last_load_end = max(program.offset + program.file_size for program in image.programs)
        self.assertTrue(check_application_elf.validate_seeded_elf(image, bytes(data[:40])))
        self.assertTrue(check_application_elf.validate_seeded_elf(
            image, bytes(data[:last_load_end - 1]),
        ))

    def test_cli_direct_and_module_validate_real_pair_from_other_directory(self):
        data = build_application_elf()
        image = parse_elf(bytes(data))
        end = max(program.offset + program.file_size for program in image.programs)
        for module in (False, True):
            with self.subTest(module=module):
                result = self.cli_result(data, bytes(data[:end]), module)
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertEqual("", result.stdout)
                self.assertEqual("", result.stderr)

    def test_cli_missing_and_malformed_inputs_fail_explicitly(self):
        missing = self.cli_result(None)
        self.assertEqual(2, missing.returncode)
        self.assertIn("ELF image does not exist", missing.stderr)
        malformed = self.cli_result(b"\x7fELF")
        self.assertEqual(1, malformed.returncode)
        self.assertEqual("", malformed.stdout)
        self.assertEqual(
            "application ELF validation failed: ELF header is truncated\n",
            malformed.stderr,
        )

    def test_cli_rejects_changed_runtime_load_bytes(self):
        data = build_application_elf()
        changed = bytearray(data)
        changed[0x2000] ^= 1
        result = self.cli_result(data, changed)
        self.assertEqual(1, result.returncode)
        self.assertEqual("", result.stdout)
        self.assertIn("PT_LOAD bytes", result.stderr)

    def test_validated_loader_preserves_shared_image_representation(self):
        data = build_application_elf()
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "application.elf"
            path.write_bytes(data)
            image = check_application_elf.load_validated_elf(path)
        self.assertEqual(bytes(data), image.data)
        self.assertEqual(USER_BASE, image.header.entry)


if __name__ == "__main__":
    unittest.main()
