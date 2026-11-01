"""Same-run CI data transport rejects changed, linked and ambiguous records."""

import copy
from pathlib import Path
import tempfile
import unittest


def context(part="target-1"):
    return {
        "version": 1, "role": "producer", "part": part,
        "host": {"system": "Darwin", "machine": "arm64", "version": "26.6.2"},
        "image_version": "fixture", "repository": "owner/repo",
        "repository_context": {"root": "/producer", "git_common": "/producer/.git"},
        "workflow": {"ref": "owner/repo/.github/workflows/ci.yml@refs/pull/1/merge", "sha": "a" * 40},
        "event": {"name": "pull_request", "commit": "a" * 40, "run_id": "42", "attempt": "1"},
        "candidate": {"head": "a" * 40, "tree": "b" * 40, "source_sha256": "c" * 64, "missing_paths": []},
        "committed_tree": "b" * 40, "base": "d" * 40,
        "runtime": {"checked": True}, "tools": {
            name: {"path": "/tools/" + name, "resolved": "/tools/" + name,
                   "identity": {"kind": "file", "mode": 493, "size": 1, "sha256": "e" * 64},
                   "version": "fixture"}
            for name in ("python", "compiler", "linker", "llvm-nm", "llvm-readelf")
        },
        "firmware": {"pinned": True}, "ctest_parallel": "3" if part == "native" else "",
    }


class CiRecordsTest(unittest.TestCase):
    def test_json_duplicates_nonfinite_values_and_unterminated_data_fail(self):
        from tools.ci_records import read_json

        with tempfile.TemporaryDirectory(prefix="micros-ci-json-") as temporary:
            path = Path(temporary) / "record.json"
            for data in (
                b'{"status":1,"status":2}', b'{"value":NaN}', b'{"value":1e999}',
                b'{"value":-1e999}', b'{"value":',
            ):
                path.write_bytes(data)
                with self.subTest(data=data), self.assertRaises(ValueError):
                    read_json(path)
            path.write_bytes(b'{"value":1}')
            self.assertEqual({"value": 1}, read_json(path))

    def test_file_lengths_cannot_borrow_boolean_or_float_equality(self):
        from tools.ci_records import file_inventory, require_files

        with tempfile.TemporaryDirectory(prefix="micros-ci-file-length-") as temporary:
            root = Path(temporary)
            (root / "empty.log").write_bytes(b"")
            inventory = file_inventory(root)
            for size in (False, 0.0):
                changed = copy.deepcopy(inventory)
                changed["empty.log"]["size"] = size
                with self.subTest(size=size), self.assertRaises(ValueError):
                    require_files(root, changed)

    def test_content_inventory_rejects_mutation_extra_files_and_linked_paths(self):
        from tools.ci_records import file_inventory, require_files

        with tempfile.TemporaryDirectory(prefix="micros-ci-files-") as temporary:
            root = Path(temporary)
            (root / "logs").mkdir()
            path = root / "logs/gate.log"
            path.write_bytes(b"passed")
            inventory = file_inventory(root)
            require_files(root, inventory)
            path.write_bytes(b"failed")
            with self.assertRaises(ValueError):
                require_files(root, inventory)
            path.write_bytes(b"passed")
            extra = root / "extra.json"
            extra.write_text("{}")
            with self.assertRaises(ValueError):
                require_files(root, inventory)
            extra.unlink()
            path.rename(root / "original.log")
            path.symlink_to(root / "original.log")
            with self.assertRaises(ValueError):
                file_inventory(root)

    def test_only_code_identity_is_shared_not_another_runners_filesystem_identity(self):
        from tools.ci_records import require_context

        expected, actual = context("collector"), context()
        expected["role"] = "collector"
        expected["repository_context"]["root"] = "/collector"
        require_context(actual, expected, "target-1")
        self.assertEqual("/producer", actual["repository_context"]["root"])
        for field in ("repository", "workflow", "event", "candidate", "base"):
            changed = copy.deepcopy(actual)
            if isinstance(changed[field], dict):
                changed[field][next(iter(changed[field]))] = "foreign"
            else:
                changed[field] = "foreign"
            with self.subTest(field=field), self.assertRaises(ValueError):
                require_context(changed, expected, "target-1")
        with self.assertRaises(ValueError):
            require_context(actual, expected, "target-2")


if __name__ == "__main__":
    unittest.main()
