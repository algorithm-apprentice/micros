"""Structural contract for the Accepted kernel directory layout."""

from __future__ import annotations

from collections import Counter
import hashlib
from pathlib import Path
import unittest

from tests.host import kernel_layout_source


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

    def test_shared_c_inspector_remains_byte_exact(self) -> None:
        self.assertEqual(
            kernel_layout_source.C_SOURCE_RECORDS_SHA256,
            hashlib.sha256(
                (ROOT / "tests/host/c_source_records.py").read_bytes()
            ).hexdigest(),
        )


if __name__ == "__main__":
    unittest.main()
