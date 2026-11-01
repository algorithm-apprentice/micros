"""Evidence snapshots reject file drift, aliases, omissions and publication races."""

import os
import copy
from pathlib import Path
import tempfile
import unittest

from tools.stack_bound_types import StackEvidenceError


class StackManifestTest(unittest.TestCase):
    def test_paired_inputs_allow_only_metadata_flags_and_build_directory_spellings(self):
        from tools.stack_bound_manifest import require_paired_compile_inputs

        ordinary, metadata = Path("/build/ordinary"), Path("/build/metadata")
        first = {
            "tools": {"compiler": "same selected compiler"},
            "inputs": {"source:fixture.c": {"sha256": "fixed input"}},
            "artifacts": {"generated/input.h": {"sha256": "same generated input"}},
            "compile": [{
                "source": "/source/fixture.c", "output": "fixture.obj",
                "argv": ["/tool/clang", "-c", "/source/fixture.c", "-o", str(ordinary / "fixture.obj")],
            }],
        }
        second = copy.deepcopy(first)
        second["compile"][0]["argv"][-1] = str(metadata / "fixture.obj")
        second["compile"][0]["argv"].extend(("-fstack-usage", "-fstack-size-section"))
        require_paired_compile_inputs(ordinary, metadata, first, second)
        for change in ("flags", "generated", "source", "tools"):
            changed = copy.deepcopy(second)
            if change == "flags":
                changed["compile"][0]["argv"].append("-Os")
            elif change == "generated":
                changed["artifacts"]["generated/input.h"]["sha256"] = "changed generated input"
            elif change == "source":
                changed["inputs"]["source:fixture.c"]["sha256"] = "changed source"
            else:
                changed["tools"]["compiler"] = "different compiler"
            with self.subTest(change=change), self.assertRaises(StackEvidenceError):
                require_paired_compile_inputs(ordinary, metadata, first, changed)

    def test_same_size_and_timestamp_changes_invalidate_the_sealed_artifact(self):
        from tools.stack_bound_manifest import require_snapshot, snapshot_files

        with tempfile.TemporaryDirectory(prefix="micros-stack-seal-") as temporary:
            root = Path(temporary)
            path = root / "frame.obj"
            path.write_bytes(b"original")
            sealed = snapshot_files(root, ("frame.obj",))
            require_snapshot(root, sealed)
            status = path.stat()
            path.write_bytes(b"mutation")
            os.utime(path, ns=(status.st_atime_ns, status.st_mtime_ns))
            with self.assertRaises(StackEvidenceError):
                require_snapshot(root, sealed)

    def test_seal_requires_unique_normalized_unlinked_artifact_paths(self):
        from tools.stack_bound_manifest import require_snapshot, snapshot_files

        with tempfile.TemporaryDirectory(prefix="micros-stack-seal-path-") as temporary:
            root = Path(temporary)
            path = root / "frame.obj"
            path.write_bytes(b"fixture")
            for names in (("frame.obj", "frame.obj"), ("../frame.obj",), ()):
                with self.subTest(names=names), self.assertRaises(StackEvidenceError):
                    snapshot_files(root, names)
            sealed = snapshot_files(root, ("frame.obj",))
            path.rename(root / "foreign.obj")
            path.symlink_to(root / "foreign.obj")
            with self.assertRaises(StackEvidenceError):
                require_snapshot(root, sealed)


if __name__ == "__main__":
    unittest.main()
