"""Structural contract for the Accepted kernel directory layout."""

from __future__ import annotations

import ast
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

from tests.host import kernel_layout_source
from tools import validation_context, validation_plan


ROOT = Path(__file__).resolve().parents[2]

EXPECTED_DIRECTORY_COUNTS = {
    "boot": 31,
    "diagnostic": 1,
    "endpoint": 12,
    "execution": 4,
    "grant": 12,
    "ipc": 21,
    "memory": 29,
    "objects": 12,
    "platform": 6,
    "pm": 23,
    "scheduler": 18,
    "trap": 15,
    "tty": 20,
    "vm": 27,
    "tests/boot": 3,
    "tests/endpoint": 4,
    "tests/execution": 6,
    "tests/grant": 12,
    "tests/ipc": 23,
    "tests/memory": 23,
    "tests/objects": 3,
    "tests/pm": 6,
    "tests/ramfs": 6,
    "tests/runtime": 10,
    "tests/scheduler": 14,
    "tests/spawn": 12,
    "tests/trap": 1,
    "tests/tty": 5,
    "tests/vfs": 3,
    "tests/vm": 11,
}


class KernelDirectoryLayoutTest(unittest.TestCase):
    def _native_compiler(self) -> str:
        xcrun = Path("/usr/bin/xcrun")
        if xcrun.is_file():
            result = subprocess.run(
                (str(xcrun), "--find", "clang"),
                capture_output=True,
                check=False,
                text=True,
                timeout=30,
            )
            if result.returncode == 0:
                return result.stdout.strip()
        for name in ("cc", "clang"):
            compiler = shutil.which(name)
            if compiler is not None:
                return compiler
        self.fail("native C compiler is unavailable")

    def _target_compiler(self) -> str:
        try:
            return str(validation_context.target_compiler(dict(os.environ)))
        except validation_context.UnsupportedContext as error:
            self.fail(str(error))

    def _preprocessed_panic_location(
        self,
        compiler: str,
        arguments: list[str],
    ) -> tuple[str, int]:
        source = ROOT / "kernel/boot/boot_component_tests.c"
        result = subprocess.run(
            (compiler, *arguments, str(source)),
            cwd=ROOT,
            capture_output=True,
            check=False,
            text=True,
            timeout=30,
        )
        self.assertEqual(0, result.returncode, result.stderr)
        lines = [
            line.strip()
            for line in result.stdout.splitlines()
            if "intentional-test" in line
        ]
        self.assertEqual(1, len(lines), result.stdout)
        prefix, separator, remainder = lines[0].partition(
            '", (uint32_t)'
        )
        self.assertTrue(separator, lines[0])
        file_name = prefix.rsplit('"', 1)[-1]
        line_number = remainder.partition(")")[0]
        self.assertTrue(line_number.isdigit(), lines[0])
        return file_name, int(line_number)

    @staticmethod
    def _diagnostic_maps(replacement: str) -> list[str]:
        return [
            (
                f"-fmacro-prefix-map={ROOT / 'kernel' / directory}/="
                f"{replacement}"
            )
            for directory in kernel_layout_source.KERNEL_DESTINATIONS
        ]

    @staticmethod
    def _preprocessor_arguments() -> list[str]:
        return [
            "-E",
            "-P",
            "-std=c17",
            "-DMICROS_BUILD_PANIC_TEST=1",
            f"-I{ROOT}",
            f"-I{ROOT / 'include'}",
        ]

    def test_closed_manifest_is_the_exact_partition(self) -> None:
        logical_paths = kernel_layout_source.KERNEL_LOGICAL_PATHS
        physical_paths = tuple(
            kernel_layout_source.KERNEL_LOGICAL_TO_PHYSICAL[path]
            for path in logical_paths
        )

        self.assertEqual(373, len(logical_paths))
        self.assertEqual(373, len(set(logical_paths)))
        self.assertEqual(373, len(set(physical_paths)))
        self.assertEqual(
            {".c": 274, ".h": 99},
            Counter(Path(path).suffix for path in logical_paths),
        )
        self.assertEqual(
            EXPECTED_DIRECTORY_COUNTS,
            Counter(
                str(Path(path).parent.relative_to("kernel"))
                for path in physical_paths
            ),
        )
        for logical, physical in zip(logical_paths, physical_paths):
            self.assertEqual(Path(logical).name, Path(physical).name)

    def test_physical_owner_migration_is_an_ordered_subset(self) -> None:
        migration = kernel_layout_source.KERNEL_PHYSICAL_OWNER_MIGRATION
        self.assertEqual((), migration)
        self.assertEqual(
            tuple(
                path
                for path in kernel_layout_source.KERNEL_LOGICAL_PATHS
                if path in set(migration)
            ),
            migration,
        )
        self.assertEqual(len(migration), len(set(migration)))

    def test_current_owner_state_is_exact(self) -> None:
        self.assertEqual((), kernel_layout_source.validate_current_layout(ROOT))
        self.assertEqual(
            (),
            tuple(
                path
                for path in (ROOT / "kernel").iterdir()
                if path.is_file() and path.suffix in (".c", ".h")
            ),
        )

    def test_every_current_source_restores_the_original_bytes(self) -> None:
        for logical in kernel_layout_source.KERNEL_LOGICAL_PATHS:
            restored = kernel_layout_source.read_logical_bytes(ROOT, logical)
            self.assertEqual(
                kernel_layout_source.KERNEL_ORIGINAL_SHA256[logical],
                hashlib.sha256(restored).hexdigest(),
                logical,
            )

    def test_external_consumers_and_generators_restore_exactly(self) -> None:
        self.assertEqual(22, len(kernel_layout_source.EXTERNAL_SOURCE_FACTS))
        self.assertEqual(
            58,
            sum(
                len(fact[2])
                for fact in kernel_layout_source.EXTERNAL_SOURCE_FACTS.values()
            ),
        )
        for relative, (_, expected, _) in (
            kernel_layout_source.EXTERNAL_SOURCE_FACTS.items()
        ):
            restored = kernel_layout_source.read_bytes_path(ROOT / relative)
            self.assertEqual(expected, hashlib.sha256(restored).hexdigest(), relative)

        self.assertEqual(9, len(kernel_layout_source.GENERATOR_FILE_FACTS))
        for relative, (expected, _, _) in (
            kernel_layout_source.GENERATOR_FILE_FACTS.items()
        ):
            restored = kernel_layout_source.read_bytes_path(ROOT / relative)
            self.assertEqual(expected, hashlib.sha256(restored).hexdigest(), relative)

    def test_existing_source_gate_asts_are_still_the_frozen_modules(self) -> None:
        for path, expected in kernel_layout_source.ORIGINAL_GATE_AST_SHA256.items():
            self.assertEqual(
                expected,
                kernel_layout_source.restored_gate_ast_sha256(ROOT, path),
                path,
            )

    def test_vm_helper_inventory_inverse_rejects_nonliteral_elements(
        self,
    ) -> None:
        relative = "tests/host/test_vm_service_source_layout.py"
        source = (ROOT / relative).read_text(encoding="utf-8")
        marker = (
            '        "vm_core_validate_state",\n'
            "    ),\n"
            '    "servers/vm/vm_image.c": (\n'
        )
        self.assertEqual(1, source.count(marker))
        changed = source.replace(
            marker,
            '        "vm_core_validate_state",\n'
            "        *(),\n"
            "    ),\n"
            '    "servers/vm/vm_image.c": (\n',
            1,
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / relative
            path.parent.mkdir(parents=True)
            path.write_text(changed, encoding="utf-8")
            with self.assertRaisesRegex(
                AssertionError,
                "invalid VM validation helper inventory",
            ):
                kernel_layout_source.restored_gate_ast_sha256(
                    root,
                    relative,
                )

    def test_existing_source_gates_use_the_bounded_path_adapter(self) -> None:
        direct_methods = {"exists", "glob", "is_file", "read_bytes", "read_text"}
        for path in kernel_layout_source.ORIGINAL_GATE_AST_SHA256:
            tree = ast.parse((ROOT / path).read_text(encoding="utf-8"))
            direct = [
                (node.lineno, node.func.attr)
                for node in ast.walk(tree)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Attribute)
                and node.func.attr in direct_methods
            ]
            self.assertEqual([], direct, path)

    def test_shared_c_inspector_remains_byte_exact(self) -> None:
        self.assertEqual(
            kernel_layout_source.C_SOURCE_RECORDS_SHA256,
            hashlib.sha256(
                (ROOT / "tests/host/c_source_records.py").read_bytes()
            ).hexdigest(),
        )

    def test_validation_metadata_is_the_exact_data_only_expansion(self) -> None:
        self.assertEqual(
            kernel_layout_source.FINAL_PATH_CLASSIFICATION_EQUIVALENTS,
            validation_plan.PATH_CLASSIFICATION_EQUIVALENTS,
        )
        self.assertEqual(
            kernel_layout_source.FINAL_HOST_UNIT_ONLY_INPUTS,
            validation_plan.HOST_UNIT_ONLY_INPUTS,
        )
        self.assertEqual(421, len(validation_plan.PATH_CLASSIFICATION_EQUIVALENTS))
        self.assertEqual(
            kernel_layout_source.ORIGINAL_PATH_CLASSIFICATION_EQUIVALENTS,
            kernel_layout_source.original_path_classification_equivalents(
                validation_plan.PATH_CLASSIFICATION_EQUIVALENTS
            ),
        )
        self.assertEqual(
            kernel_layout_source.ORIGINAL_HOST_UNIT_ONLY_INPUTS,
            kernel_layout_source.original_host_unit_only_inputs(
                validation_plan.HOST_UNIT_ONLY_INPUTS
            ),
        )

    def test_every_physical_path_preserves_all_validation_dimensions(self) -> None:
        for logical in kernel_layout_source.KERNEL_LOGICAL_PATHS:
            physical = kernel_layout_source.KERNEL_LOGICAL_TO_PHYSICAL[logical]
            expected = kernel_layout_source.KERNEL_CLASSIFICATION_FACTS[logical]
            for path in (logical, physical):
                observed = (
                    validation_plan.requires_full_tier([path]),
                    tuple(validation_plan.affected_workflows([path])),
                    tuple(validation_plan.affected_slow_models([path])),
                    tuple(validation_plan.affected_image_workflows([path])),
                    {
                        tier: hashlib.sha256(
                            json.dumps(
                                kernel_layout_source.original_validation_plan(
                                    validation_plan.plan([path], tier, "BASE")
                                ),
                                separators=(",", ":"),
                            ).encode()
                        ).hexdigest()
                        for tier in ("fast", "pr", "full")
                    },
                )
                self.assertEqual(expected, observed, path)

    def test_cmake_transport_is_exact_and_fully_reversible(self) -> None:
        actual = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertEqual(30, len(kernel_layout_source.KERNEL_DESTINATIONS))
        self.assertEqual(21, len(kernel_layout_source.NATIVE_DIAGNOSTIC_CONSUMERS))
        self.assertEqual(
            1,
            actual.count(kernel_layout_source.CMAKE_NATIVE_DIAGNOSTIC_BLOCK),
        )
        self.assertEqual(
            1,
            actual.count(kernel_layout_source.CMAKE_TARGET_DIAGNOSTIC_BLOCK),
        )
        for target, (old, new) in kernel_layout_source.CMAKE_INCLUDE_TRANSPORTS.items():
            self.assertEqual(0, actual.count(old), target)
            self.assertEqual(1, actual.count(new), target)
        restored = kernel_layout_source.read_path(
            ROOT / "CMakeLists.txt",
            encoding="utf-8",
        )
        self.assertEqual(
            kernel_layout_source.BASE_CMAKE_SHA256,
            hashlib.sha256(restored.encode()).hexdigest(),
        )
        self.assertEqual(
            hashlib.sha256(actual.encode()).hexdigest(),
            validation_context.RECIPE_DIGESTS["CMakeLists.txt"],
        )

    def test_native_compiler_preserves_logical_diagnostic_location(self) -> None:
        common = self._preprocessor_arguments()
        native_replacement = f"{ROOT / 'kernel'}/"
        native_maps = self._diagnostic_maps(native_replacement)
        native_compiler = self._native_compiler()
        self.assertEqual(
            (str(ROOT / "kernel/boot_component_tests.c"), 107),
            self._preprocessed_panic_location(
                native_compiler,
                [*common, *native_maps],
            ),
        )
        native_wrong = self._diagnostic_maps(f"{ROOT / 'wrong'}/")
        self.assertEqual(
            (str(ROOT / "wrong/boot_component_tests.c"), 107),
            self._preprocessed_panic_location(
                native_compiler,
                [*common, *native_wrong],
            ),
        )

    def test_target_compiler_preserves_logical_diagnostic_location(self) -> None:
        target_compiler = self._target_compiler()
        target_maps = self._diagnostic_maps("kernel/")
        target_common = [
            "--target=riscv64-unknown-elf",
            "-ffreestanding",
            *self._preprocessor_arguments(),
            f"-ffile-prefix-map={ROOT}/=",
        ]
        self.assertEqual(
            ("kernel/boot_component_tests.c", 107),
            self._preprocessed_panic_location(
                target_compiler,
                [*target_common, *target_maps],
            ),
        )
        target_wrong = self._diagnostic_maps("wrong/")
        self.assertEqual(
            ("wrong/boot_component_tests.c", 107),
            self._preprocessed_panic_location(
                target_compiler,
                [*target_common, *target_wrong],
            ),
        )
        target_without_boot = [
            option
            for directory, option in zip(
                kernel_layout_source.KERNEL_DESTINATIONS,
                target_maps,
            )
            if directory != "boot"
        ]
        self.assertEqual(
            ("kernel/boot/boot_component_tests.c", 107),
            self._preprocessed_panic_location(
                target_compiler,
                [*target_common, *target_without_boot],
            ),
        )

    def test_diagnostic_maps_apply_only_to_c_sources(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertEqual(
            2,
            cmake.count("$<$<COMPILE_LANGUAGE:C>:-fmacro-prefix-map="),
        )
        self.assertNotIn("COMPILE_LANGUAGE:ASM>:-fmacro-prefix-map=", cmake)

    def test_layout_validation_rejects_missing_duplicate_and_linked_members(self) -> None:
        logical = "kernel/panic.c"
        physical = kernel_layout_source.KERNEL_LOGICAL_TO_PHYSICAL[logical]
        another = kernel_layout_source.KERNEL_LOGICAL_TO_PHYSICAL[
            "kernel/timer.c"
        ]
        mutations = {
            "missing": lambda root: (root / physical).unlink(),
            "duplicate": lambda root: (
                (root / logical).parent.mkdir(parents=True, exist_ok=True),
                shutil.copy2(root / physical, root / logical),
            ),
            "symlink": lambda root: (
                (root / physical).unlink(),
                (root / physical).symlink_to(root / another),
            ),
            "hardlink": lambda root: (
                (root / physical).unlink(),
                os.link(root / another, root / physical),
            ),
            "mode": lambda root: (root / physical).chmod(0o600),
            "unexpected": lambda root: (
                root / "kernel" / "unexpected.c"
            ).write_text("int unexpected;\n"),
            "unexpected-dangling-symlink": lambda root: (
                root / "kernel" / "dangling.c"
            ).symlink_to("missing.c"),
            "unexpected-directory-symlink": lambda root: (
                root / "kernel" / "legacy"
            ).symlink_to("boot", target_is_directory=True),
            "kernel-root-symlink": lambda root: (
                shutil.rmtree(root / "kernel"),
                (root / "kernel").symlink_to(
                    ROOT / "kernel",
                    target_is_directory=True,
                ),
            ),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                shutil.copytree(ROOT / "kernel", root / "kernel")
                mutate(root)
                self.assertNotEqual(
                    (),
                    kernel_layout_source.validate_current_layout(root),
                )

    def test_source_transport_rejects_include_and_body_drift(self) -> None:
        cases = (
            (
                "kernel/address_space.c",
                '"kernel/memory/address_space_internal.h"',
                '"kernel/memory/not_address_space_internal.h"',
            ),
            (
                "kernel/panic.c",
                '"MICROS_PANIC_SOURCE file="',
                '"MICROS_PANIC_SOURCE changed="',
            ),
        )
        for logical, before, after in cases:
            with self.subTest(logical=logical), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                shutil.copytree(ROOT / "kernel", root / "kernel")
                physical = kernel_layout_source.KERNEL_LOGICAL_TO_PHYSICAL[logical]
                path = root / physical
                text = path.read_text(encoding="utf-8")
                self.assertEqual(1, text.count(before))
                path.write_text(text.replace(before, after), encoding="utf-8")
                with self.assertRaises(AssertionError):
                    kernel_layout_source.read_logical_bytes(root, logical)

    def test_cmake_transport_rejects_recipe_drift(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        with self.assertRaises(AssertionError):
            kernel_layout_source.restore_cmake_source(
                cmake.replace(
                    "kernel/tests/vm/vm_handoff_test.c",
                    "kernel/tests/vm/not_vm_handoff_test.c",
                    1,
                )
            )
        for name, block in (
            ("native", kernel_layout_source.CMAKE_NATIVE_DIAGNOSTIC_BLOCK),
            ("target", kernel_layout_source.CMAKE_TARGET_DIAGNOSTIC_BLOCK),
        ):
            with self.subTest(block=name, mutation="missing"):
                with self.assertRaises(AssertionError):
                    kernel_layout_source.restore_cmake_source(
                        cmake.replace(block + "\n\n", "", 1)
                    )
            with self.subTest(block=name, mutation="misplaced"):
                without_block = cmake.replace(block + "\n\n", "", 1)
                with self.assertRaises(AssertionError):
                    kernel_layout_source.restore_cmake_source(
                        block + "\n\n" + without_block
                    )
        with self.assertRaises(AssertionError):
            kernel_layout_source.restore_cmake_source(
                cmake.replace(
                    "COMPILE_LANGUAGE:C>:-fmacro-prefix-map=",
                    "COMPILE_LANGUAGE:ASM>:-fmacro-prefix-map=",
                    1,
                )
            )

    def test_validation_metadata_rejects_unreviewed_rows(self) -> None:
        with self.assertRaises(AssertionError):
            kernel_layout_source.original_path_classification_equivalents(
                (*validation_plan.PATH_CLASSIFICATION_EQUIVALENTS, ("extra", ())),
            )

    def test_validation_context_transport_rejects_closure_drift(self) -> None:
        source = (ROOT / "tools/validation_context.py").read_text(
            encoding="utf-8"
        )
        with mock.patch.object(
            ast,
            "unparse",
            side_effect=AssertionError("Python 3.8 has no ast.unparse"),
            create=True,
        ):
            restored = (
                kernel_layout_source.restore_validation_context_source(
                    source
                )
            )
        self.assertEqual(
            kernel_layout_source.BASE_VALIDATION_CONTEXT_SHA256,
            hashlib.sha256(restored.encode()).hexdigest(),
        )
        with self.assertRaises(AssertionError):
            kernel_layout_source.restore_validation_context_source(
                source.replace(
                    "target_preprocessor",
                    "changed_target_preprocessor",
                    1,
                )
            )

    def test_generated_output_rejects_unreviewed_includes(self) -> None:
        for generator, (_, _, transports) in (
            kernel_layout_source.GENERATOR_FILE_FACTS.items()
        ):
            self.assertEqual(1, len(transports), generator)
            _, target, old_source_line, new_source_line = transports[0]
            old_include = ast.literal_eval(old_source_line.strip().rstrip(","))
            new_include = ast.literal_eval(new_source_line.strip().rstrip(","))
            expected = (
                old_include
                if target in kernel_layout_source.KERNEL_PHYSICAL_OWNER_MIGRATION
                else new_include
            )
            self.assertEqual(
                f"{old_include}\n",
                kernel_layout_source.restore_generated_fixture(
                    generator,
                    f"{expected}\n",
                ),
                generator,
            )
            with self.subTest(generator=generator, mutation="missing"):
                with self.assertRaises(AssertionError):
                    kernel_layout_source.restore_generated_fixture(
                        generator,
                        "no reviewed include is present",
                    )
            with self.subTest(generator=generator, mutation="duplicate"):
                with self.assertRaises(AssertionError):
                    kernel_layout_source.restore_generated_fixture(
                        generator,
                        f"{expected}\n{expected}\n",
                    )
            with self.subTest(generator=generator, mutation="old-and-new"):
                with self.assertRaises(AssertionError):
                    kernel_layout_source.restore_generated_fixture(
                        generator,
                        f"{old_include}\n{new_include}\n",
                    )
            with self.subTest(generator=generator, mutation="wrong"):
                with self.assertRaises(AssertionError):
                    kernel_layout_source.restore_generated_fixture(
                        generator,
                        '#include "kernel/not_reviewed_fixture.h"\n',
                    )


if __name__ == "__main__":
    unittest.main()
