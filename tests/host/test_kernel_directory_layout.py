"""Structural contract for the Accepted kernel directory layout."""

from __future__ import annotations

import ast
from collections import Counter
import hashlib
import json
from pathlib import Path
import unittest

from tests.host import kernel_layout_source
from tools import validation_plan


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

    def test_initial_physical_owner_migration_is_complete(self) -> None:
        self.assertEqual(
            kernel_layout_source.KERNEL_LOGICAL_PATHS,
            kernel_layout_source.KERNEL_PHYSICAL_OWNER_MIGRATION,
        )

    def test_current_owner_state_is_exact(self) -> None:
        self.assertEqual((), kernel_layout_source.validate_current_layout(ROOT))

    def test_every_current_source_restores_the_original_bytes(self) -> None:
        for logical in kernel_layout_source.KERNEL_LOGICAL_PATHS:
            restored = kernel_layout_source.read_logical_bytes(ROOT, logical)
            self.assertEqual(
                kernel_layout_source.KERNEL_ORIGINAL_SHA256[logical],
                hashlib.sha256(restored).hexdigest(),
                logical,
            )

    def test_existing_source_gate_asts_are_still_the_frozen_modules(self) -> None:
        for path, expected in kernel_layout_source.ORIGINAL_GATE_AST_SHA256.items():
            self.assertEqual(
                expected,
                kernel_layout_source.restored_gate_ast_sha256(ROOT, path),
                path,
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
                                validation_plan.plan([path], tier, "BASE"),
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


if __name__ == "__main__":
    unittest.main()
