"""Private facade and command-line cases assigned by ADR-0063."""

from contextlib import redirect_stderr
import io
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from tests.host.user_elf_test_fixture import build_elf
from tools import check_user_elf


class UserElfCliCases:
    def _cli_run(self, data, arguments=(), module=False):
        root = Path(__file__).resolve().parents[2]
        environment = os.environ.copy()
        environment.pop("PYTHONPATH", None)
        command = [sys.executable, "-B"]
        if module:
            environment["PYTHONPATH"] = str(root)
            command.extend(("-m", "tools.check_user_elf"))
        else:
            command.append(str(root / "tools/check_user_elf.py"))
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "image.elf"
            if data is not None:
                image.write_bytes(data)
            return subprocess.run(
                [*command, "--elf", str(image), *arguments],
                cwd=temporary,
                env=environment,
                text=True,
                capture_output=True,
                timeout=10,
                check=False,
            )

    def _cli_bad_image(self):
        image, _ = build_elf()
        struct.pack_into("<H", image, 18, 62)
        struct.pack_into("<Q", image, 24, check_user_elf.USER_BASE + 4)
        struct.pack_into("<I", image, 0x1000 + 24, 0x00100073)
        return image, (
            "ELF machine must be EM_RISCV",
            "ELF entry must equal MICROS_USER_VIRTUAL_BASE",
            "_start and e_entry must both equal the user base",
            "raw syscall stub must be exact ecall; ret",
        )

    def test_cli_script_and_module_succeed_from_separate_directory(self):
        image, _ = build_elf()
        for module in (False, True):
            with self.subTest(module=module):
                result = self._cli_run(image, module=module)
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertEqual("", result.stdout)
                self.assertEqual("", result.stderr)

    def test_cli_requires_an_image_argument(self):
        errors = io.StringIO()
        with redirect_stderr(errors):
            with self.assertRaises(SystemExit) as failure:
                check_user_elf.main([])
        self.assertEqual(2, failure.exception.code)
        self.assertIn("the following arguments are required: --elf", errors.getvalue())

    def test_cli_stack_count_requires_a_limit_before_validation(self):
        result = self._cli_run(None, ("--stack-page-count", "1"))
        self.assertEqual(2, result.returncode)
        self.assertEqual("", result.stdout)
        self.assertIn("--stack-page-count requires --resident-page-limit", result.stderr)
        self.assertNotIn("ELF image does not exist", result.stderr)

    def test_cli_missing_image_is_an_argument_failure(self):
        result = self._cli_run(None)
        self.assertEqual(2, result.returncode)
        self.assertEqual("", result.stdout)
        self.assertIn("ELF image does not exist:", result.stderr)

    def test_cli_malformed_file_is_an_explicit_validation_failure(self):
        result = self._cli_run(b"\x7fELF")
        self.assertEqual(1, result.returncode)
        self.assertEqual("", result.stdout)
        self.assertEqual(
            "user ELF validation failed: ELF header is truncated\n", result.stderr,
        )

    def test_cli_resident_budget_and_negative_count_keep_stable_failures(self):
        image, _ = build_elf()
        for arguments, message in (
            (
                ("--resident-page-limit", "3", "--stack-page-count", "1"),
                "resident page count 4 exceeds limit 3",
            ),
            (
                ("--resident-page-limit", "3", "--stack-page-count", "-1"),
                "stack page count must not be negative",
            ),
            (
                ("--resident-page-limit", "0"),
                "resident page limit must be positive",
            ),
        ):
            with self.subTest(arguments=arguments):
                result = self._cli_run(image, arguments)
                self.assertEqual(1, result.returncode)
                self.assertEqual("", result.stdout)
                self.assertEqual(
                    "user ELF validation failed: " + message + "\n", result.stderr,
                )

    def test_cli_preserves_complete_diagnostic_order(self):
        image, errors = self._cli_bad_image()
        result = self._cli_run(image)
        self.assertEqual(1, result.returncode)
        self.assertEqual("", result.stdout)
        self.assertEqual(
            "".join("user ELF validation failed: " + error + "\n" for error in errors),
            result.stderr,
        )

    def test_validated_loader_joins_the_same_ordered_errors(self):
        image, errors = self._cli_bad_image()
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "image.elf"
            path.write_bytes(image)
            with self.assertRaises(check_user_elf.ElfFormatError) as failure:
                check_user_elf.load_validated_elf(path)
        self.assertEqual("; ".join(errors), str(failure.exception))
