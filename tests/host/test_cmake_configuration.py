import os
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def qemu_target_timeout(target):
    content = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    start = content.index(f"add_custom_target(\n        {target}\n")
    end = content.index("\n    )", start)
    block = content[start:end]
    match = re.search(r"--timeout\s+(\d+)", block)

    if match is None:
        raise AssertionError(f"{target} has no host timeout")
    return int(match.group(1))


def python_environment():
    environment = os.environ.copy()
    environment.update({
        "MICROS_PYTHON": sys.executable,
        "OPENSSL_CONF": "/dev/null",
        "SSL_CERT_FILE": "/dev/null",
        "SSL_CERT_DIR": "/dev/null",
    })
    environment.pop("PYTHONEXECUTABLE", None)
    environment.pop("__PYVENV_LAUNCHER__", None)
    return environment


class CMakeConfigurationTests(unittest.TestCase):
    def test_explicit_python_315_configures_with_exact_cache_spelling(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(ROOT),
                    "-B",
                    temporary_directory,
                    "-G",
                    "Ninja",
                    f"-DMICROS_PYTHON={sys.executable}",
                    "-DMICROS_BUILD_HOST_TESTS=ON",
                ],
                env=python_environment(),
                capture_output=True,
                check=False,
                text=True,
            )
            cache = (
                pathlib.Path(temporary_directory) / "CMakeCache.txt"
            ).read_text(encoding="utf-8")

        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn(
            f"MICROS_PYTHON:FILEPATH={sys.executable}",
            cache,
        )

    def test_explicit_unsupported_python_does_not_fall_through(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            unsupported = pathlib.Path(temporary_directory) / "unsupported-python"
            unsupported.write_text(
                "#!/bin/sh\n"
                "printf '%s\\n' "
                "'unsupported Python runtime: "
                "host automation requires CPython 3.15 final' >&2\n"
                "exit 2\n",
                encoding="utf-8",
            )
            unsupported.chmod(0o755)
            build = pathlib.Path(temporary_directory) / "build"
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(ROOT),
                    "-B",
                    str(build),
                    "-G",
                    "Ninja",
                    f"-DMICROS_PYTHON={unsupported}",
                ],
                env=python_environment(),
                capture_output=True,
                check=False,
                text=True,
            )

        self.assertNotEqual(0, result.returncode)
        self.assertIn(
            "host automation requires CPython 3.15 final",
            result.stdout + result.stderr,
        )

    def test_preexec_virtual_environment_launcher_is_rejected(self):
        environment = python_environment()
        environment["__PYVENV_LAUNCHER__"] = "/tmp/fake"
        with tempfile.TemporaryDirectory() as temporary_directory:
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(ROOT),
                    "-B",
                    temporary_directory,
                    "-G",
                    "Ninja",
                    f"-DMICROS_PYTHON={sys.executable}",
                ],
                env=environment,
                capture_output=True,
                check=False,
                text=True,
            )

        self.assertNotEqual(0, result.returncode)
        self.assertIn(
            "__PYVENV_LAUNCHER__ is unsupported",
            result.stdout + result.stderr,
        )

    def test_ramfs_service_target_is_exclusive(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(ROOT),
                    "-B",
                    temporary_directory,
                    "-DMICROS_BUILD_RAMFS_SERVICE_TEST=ON",
                    "-DMICROS_BUILD_TTY_SERVICE_TEST=ON",
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

    def test_ramfs_qemu_timeout_preserves_tcg_margin(self):
        self.assertGreaterEqual(
            qemu_target_timeout("test-qemu-ramfs"),
            180,
        )

    def test_tty_qemu_timeout_preserves_tcg_margin(self):
        self.assertGreaterEqual(
            qemu_target_timeout("test-qemu-tty"),
            90,
        )

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
