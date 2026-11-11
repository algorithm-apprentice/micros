"""Current kernel namespaces, diagnostic mapping and validation ownership."""

import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
from tempfile import TemporaryDirectory
import unittest

from tests.host.build_source_records import cmake_commands
from tests.host.source_snapshot import source_bytes
from tools import validation_context, validation_plan


ROOT = Path(__file__).resolve().parents[2]
DIRECTORIES = (
    "boot", "diagnostic", "endpoint", "execution", "grant", "ipc", "memory",
    "objects", "platform", "pm", "scheduler", "trap", "tty", "vm",
    "tests/boot", "tests/endpoint", "tests/execution", "tests/grant", "tests/ipc",
    "tests/memory", "tests/objects", "tests/pm", "tests/ramfs", "tests/runtime",
    "tests/scheduler", "tests/spawn", "tests/trap", "tests/tty", "tests/vfs", "tests/vm",
)
NATIVE_CONSUMERS = (
    "bootstrap_control_test", "bootstrap_test", "elf_spawn_fixture_test", "elf_spawn_model_test",
    "endpoint_test", "fdt_test", "frame_allocator_test", "frame_ownership_test",
    "ipc_model_test", "kernel_objects_test", "pm_test", "scheduler_test", "sv39_test",
    "trap_route_test", "tty_control_syscall_test", "tty_fault_test", "tty_interrupt_test",
    "tty_test", "vm_bootstrap_test", "vm_core_test", "vm_handoff_syscall_test",
)


def layout_errors(root):
    kernel = Path(root) / "kernel"
    if kernel.is_symlink() or not kernel.is_dir():
        return ("kernel root must be an ordinary directory",)
    errors = []
    for path in kernel.rglob("*"):
        relative = path.relative_to(kernel)
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode):
            errors.append(f"linked kernel member: {relative}")
        elif path.suffix in (".c", ".h"):
            if (
                not stat.S_ISREG(info.st_mode) or info.st_nlink != 1
                or stat.S_IMODE(info.st_mode) != 0o644
            ):
                errors.append(f"nonordinary kernel source: {relative}")
            if str(relative.parent) not in DIRECTORIES:
                errors.append(f"kernel source has no responsibility directory: {relative}")
    return tuple(errors)


class KernelDirectoryLayoutTest(unittest.TestCase):
    def _native_compiler(self):
        xcrun = Path("/usr/bin/xcrun")
        if xcrun.is_file():
            result = subprocess.run(
                (str(xcrun), "--find", "clang"), capture_output=True, text=True, timeout=30,
            )
            if result.returncode == 0:
                return result.stdout.strip()
        for name in ("cc", "clang"):
            if compiler := shutil.which(name):
                return compiler
        self.fail("native C compiler is unavailable")

    def _preprocessed_panic_location(self, compiler, arguments):
        source = ROOT / "kernel/boot/boot_component_tests.c"
        result = subprocess.run(
            (compiler, *arguments, str(source)), cwd=ROOT, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(0, result.returncode, result.stderr)
        lines = [line.strip() for line in result.stdout.splitlines() if "intentional-test" in line]
        self.assertEqual(1, len(lines))
        prefix, separator, remainder = lines[0].partition('", (uint32_t)')
        self.assertTrue(separator, lines[0])
        line = remainder.partition(")")[0]
        self.assertTrue(line.isdigit(), lines[0])
        return prefix.rsplit('"', 1)[-1], int(line)

    def _source_line(self):
        source = source_bytes(ROOT, ROOT / "kernel/boot/boot_component_tests.c").decode()
        matches = tuple(re.finditer(r'\bMICROS_PANIC\s*\([^;]*?"intentional-test"\s*\)', source))
        self.assertEqual(1, len(matches))
        return source.count("\n", 0, matches[0].start()) + 1

    @staticmethod
    def _diagnostic_maps(replacement):
        return [
            f"-fmacro-prefix-map={ROOT / 'kernel' / directory}/={replacement}"
            for directory in DIRECTORIES
        ]

    @staticmethod
    def _preprocessor_arguments():
        return [
            "-E", "-P", "-std=c17", "-DMICROS_BUILD_PANIC_TEST=1",
            f"-I{ROOT}", f"-I{ROOT / 'include'}",
        ]

    def test_current_kernel_sources_have_ordinary_responsibility_owners(self):
        self.assertEqual((), layout_errors(ROOT))
        actual = {
            str(path.parent.relative_to(ROOT / "kernel"))
            for path in (ROOT / "kernel").rglob("*") if path.suffix in (".c", ".h")
        }
        self.assertEqual(set(DIRECTORIES), actual)

    def test_linked_flat_or_unknown_source_owners_are_rejected(self):
        for kind in ("flat", "unknown", "symlink", "dangling", "directory-link", "hardlink", "mode"):
            with self.subTest(kind=kind), TemporaryDirectory() as temporary:
                root = Path(temporary)
                directory = root / "kernel/boot"
                directory.mkdir(parents=True)
                source = directory / "entry.c"
                source.write_text("void entry(void) {}\n")
                source.chmod(0o644)
                self.assertEqual((), layout_errors(root))
                if kind == "flat":
                    (root / "kernel/entry.c").write_text(source.read_text())
                elif kind == "unknown":
                    (root / "kernel/unknown").mkdir()
                    (root / "kernel/unknown/entry.c").write_text(source.read_text())
                elif kind == "symlink":
                    (directory / "linked.c").symlink_to(source)
                elif kind == "dangling":
                    (directory / "linked.c").symlink_to("missing.c")
                elif kind == "directory-link":
                    (root / "kernel/alias").symlink_to(directory, target_is_directory=True)
                elif kind == "hardlink":
                    os.link(source, directory / "linked.c")
                else:
                    source.chmod(0o600)
                self.assertTrue(layout_errors(root), kind)

    def test_current_diagnostic_maps_cover_c_directories_and_native_consumers(self):
        cmake = source_bytes(ROOT, ROOT / "CMakeLists.txt").decode()
        blocks = cmake_commands(cmake, "foreach", "kernel_directory")
        self.assertEqual(2, len(blocks))
        for block in blocks:
            self.assertEqual(DIRECTORIES, tuple(block.partition("IN ITEMS")[2].removesuffix(")").split()))
        consumer, = cmake_commands(cmake, "foreach", "kernel_consumer")
        self.assertEqual(NATIVE_CONSUMERS, tuple(consumer.partition("IN ITEMS")[2].removesuffix(")").split()))
        self.assertEqual(2, cmake.count("$<$<COMPILE_LANGUAGE:C>:-fmacro-prefix-map="))
        self.assertNotIn("COMPILE_LANGUAGE:ASM>:-fmacro-prefix-map=", cmake)
        self.assertIn(
            "-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/kernel/${kernel_directory}/=${CMAKE_CURRENT_SOURCE_DIR}/kernel/",
            cmake,
        )
        self.assertIn("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/kernel/${kernel_directory}/=kernel/", cmake)

    def test_native_compiler_reports_current_line_and_logical_file(self):
        compiler = self._native_compiler()
        common = self._preprocessor_arguments()
        for directory in ("kernel", "wrong"):
            with self.subTest(directory=directory):
                self.assertEqual(
                    (str(ROOT / directory / "boot_component_tests.c"), self._source_line()),
                    self._preprocessed_panic_location(
                        compiler, [*common, *self._diagnostic_maps(f"{ROOT / directory}/")],
                    ),
                )

    def test_target_compiler_reports_current_line_and_logical_file(self):
        compiler = str(validation_context.target_compiler(dict(os.environ)))
        common = [
            "--target=riscv64-unknown-elf", "-ffreestanding", *self._preprocessor_arguments(),
            f"-ffile-prefix-map={ROOT}/=",
        ]
        for directory in ("kernel", "wrong"):
            self.assertEqual(
                (directory + "/boot_component_tests.c", self._source_line()),
                self._preprocessed_panic_location(compiler, [*common, *self._diagnostic_maps(directory + "/")]),
            )
        without_boot = [
            option for directory, option in zip(DIRECTORIES, self._diagnostic_maps("kernel/"))
            if directory != "boot"
        ]
        self.assertEqual(
            ("kernel/boot/boot_component_tests.c", self._source_line()),
            self._preprocessed_panic_location(compiler, [*common, *without_boot]),
        )

    def test_current_physical_aliases_preserve_complete_validation_plans(self):
        aliases = {
            path: logical for logical, paths in validation_plan.PATH_CLASSIFICATION_EQUIVALENTS
            for path in paths if logical.startswith("kernel/") and logical.count("/") == 1
            and path.startswith("kernel/") and Path(path).name == Path(logical).name
        }
        self.assertTrue(aliases)
        for physical, logical in aliases.items():
            for tier in ("fast", "pr", "full"):
                self.assertEqual(
                    validation_plan.plan([logical], tier, "main"),
                    validation_plan.plan([physical], tier, "main"), (physical, tier),
                )


if __name__ == "__main__":
    unittest.main()
