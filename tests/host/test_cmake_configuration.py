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


def executable_fixture(directory, name, body):
    executable = pathlib.Path(directory) / name
    executable.write_text("#!/bin/sh\n" + body, encoding="utf-8")
    executable.chmod(0o755)
    return executable


class CMakeConfigurationTests(unittest.TestCase):
    @unittest.skipUnless(
        sys.platform == "darwin",
        "Homebrew precedence is a Darwin host contract",
    )
    def test_cross_toolchain_uses_host_homebrew_python_before_path(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary = pathlib.Path(temporary_directory)
            tools = temporary / "tools"
            brew_root = temporary / "brew-python"
            path_python = tools / "python3.15"
            tools.mkdir()
            (brew_root / "bin").mkdir(parents=True)
            (brew_root / "bin/python3.15").symlink_to(sys.executable)
            path_python.symlink_to(sys.executable)
            executable_fixture(
                tools,
                "brew",
                "if [ \"$1\" = \"--prefix\" ] "
                "&& [ \"$2\" = \"python@3.15\" ]; then\n"
                f"    printf '%s\\n' '{brew_root}'\n"
                "    exit 0\n"
                "fi\n"
                "exit 1\n",
            )
            environment = python_environment()
            environment.pop("MICROS_PYTHON", None)
            environment.pop("MICROS_PYTHON_ROOT", None)
            environment["PATH"] = (
                str(tools) + os.pathsep + environment["PATH"]
            )
            for name, path in (
                ("MICROS_CLANG", "/opt/homebrew/opt/llvm/bin/clang"),
                ("MICROS_LD_LLD", "/opt/homebrew/opt/lld/bin/ld.lld"),
                ("MICROS_QEMU", "/opt/homebrew/bin/qemu-system-riscv64"),
            ):
                if pathlib.Path(path).is_file():
                    environment[name] = path
            environment["CLANG_NO_DEFAULT_CONFIG"] = "1"
            build = temporary / "build"
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(ROOT),
                    "-B",
                    str(build),
                    "-G",
                    "Ninja",
                    "-DCMAKE_TOOLCHAIN_FILE="
                    + str(
                        ROOT
                        / "cmake/toolchains/riscv64-clang.cmake"
                    ),
                ],
                env=environment,
                capture_output=True,
                check=False,
                text=True,
            )
            cache = (build / "CMakeCache.txt").read_text(
                encoding="utf-8"
            )

        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn(
            "MICROS_PYTHON:FILEPATH="
            + str(brew_root / "bin/python3.15"),
            cache,
        )
        self.assertNotIn(
            "MICROS_PYTHON:FILEPATH=" + str(path_python),
            cache,
        )

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
            unsupported = executable_fixture(
                temporary_directory,
                "unsupported-python",
                "printf '%s\\n' "
                "'unsupported Python runtime: "
                "host automation requires CPython 3.15 final' >&2\n"
                "exit 2\n",
            )
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

    def test_exit_zero_non_python_fails_runtime_challenge(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            impostor = executable_fixture(
                temporary_directory,
                "python-noop",
                "exit 0\n",
            )
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(ROOT),
                    "-B",
                    str(pathlib.Path(temporary_directory) / "build"),
                    "-G",
                    "Ninja",
                    f"-DMICROS_PYTHON={impostor}",
                    "-DMICROS_BUILD_HOST_TESTS=ON",
                ],
                env=python_environment(),
                capture_output=True,
                check=False,
                text=True,
            )

        self.assertNotEqual(0, result.returncode)
        self.assertIn(
            "runtime validation challenge failed",
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
