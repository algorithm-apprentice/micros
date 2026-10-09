import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class CMakeConfigurationTests(unittest.TestCase):
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
