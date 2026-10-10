import pathlib
import re
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class CMakeConfigurationTests(unittest.TestCase):
    def test_tty_qemu_timeout_preserves_tcg_margin(self):
        content = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        start = content.index(
            "add_custom_target(\n        test-qemu-tty\n"
        )
        end = content.index("\n    )", start)
        block = content[start:end]
        match = re.search(r"--timeout\s+(\d+)", block)

        self.assertIsNotNone(match)
        self.assertGreaterEqual(int(match.group(1)), 90)

    def test_uart_console_target_is_exclusive(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(ROOT),
                    "-B",
                    temporary_directory,
                    "-DMICROS_BUILD_UART_CONSOLE_TEST=ON",
                    "-DMICROS_BUILD_PANIC_TEST=ON",
                ],
                capture_output=True,
                check=False,
                text=True,
            )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "Select at most one target test image",
            result.stdout + result.stderr,
        )


if __name__ == "__main__":
    unittest.main()
