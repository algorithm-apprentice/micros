"""Current source snapshots do not depend on historical retirement proofs."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tests.host import source_snapshot


class CurrentSourceSnapshotTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="micros-current-source-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.path = self.root / "src/example.c"
        self.path.parent.mkdir()
        self.path.write_bytes(b"int value;\n")
        (self.root / ".gitignore").write_text("src/ignored.py\n")
        self.git("init", "--quiet")
        self.git("add", ".")
        self.git(
            "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
            "-c", "commit.gpgsign=false", "commit", "--quiet", "-m",
            "Current source fixture\n\nCo-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>",
        )
        self.active = mock.patch.object(source_snapshot, "_active", None)
        self.active.start()
        self.addCleanup(self.active.stop)

    def git(self, *arguments):
        return subprocess.check_output(
            ("git", *arguments), cwd=self.root, stderr=subprocess.STDOUT,
        )

    def test_current_reads_need_no_historical_catalog_or_author_journal(self):
        with source_snapshot.checked_source_view(self.root) as view:
            for _ in range(15):
                self.assertEqual(
                    b"int value;\n",
                    source_snapshot.source_bytes(self.root, self.path),
                )
        self.assertFalse((self.root / "build").exists())
        with self.assertRaises(ValueError):
            view.source_bytes(self.path)

    def test_source_reads_do_not_write_or_lock_the_shared_git_index(self):
        lock = self.root / ".git/index.lock"
        lock.write_text("independent index owner\n")
        before = (self.root / ".git/index").read_bytes()
        with source_snapshot.checked_source_view(self.root):
            self.assertEqual(b"int value;\n", source_snapshot.source_bytes(self.root, self.path))
        self.assertEqual(before, (self.root / ".git/index").read_bytes())
        self.assertEqual("independent index owner\n", lock.read_text())

    def test_captured_bytes_are_stable_but_changed_source_prevents_success(self):
        changed = False
        with self.assertRaises(ValueError):
            with source_snapshot.checked_source_view(self.root):
                self.assertEqual(
                    b"int value;\n",
                    source_snapshot.source_bytes(self.root, self.path),
                )
                before = self.path.stat()
                self.path.write_bytes(b"int other;\n")
                os.utime(self.path, ns=(before.st_atime_ns, before.st_mtime_ns))
                changed = True
                self.assertEqual(
                    b"int value;\n",
                    source_snapshot.source_bytes(self.root, self.path),
                )
        self.assertTrue(changed, "failure must occur after the actual source mutation")
        with source_snapshot.checked_source_view(self.root):
            self.assertEqual(
                b"int other;\n",
                source_snapshot.source_bytes(self.root, self.path),
            )

    def test_nested_or_foreign_process_cannot_borrow_the_snapshot(self):
        with source_snapshot.checked_source_view(self.root):
            self.assertEqual(
                b"int value;\n",
                source_snapshot.source_bytes(self.root, self.path),
            )
            with self.assertRaises(ValueError):
                with source_snapshot.checked_source_view(self.root):
                    self.fail("nested snapshot owner was admitted")
            with mock.patch.object(source_snapshot.os, "getpid", return_value=-1):
                with self.assertRaises(ValueError):
                    source_snapshot.source_bytes(self.root, self.path)

    def test_mode_or_ignored_namespace_changes_prevent_completion(self):
        for change in (
            lambda: self.path.chmod(0o755),
            lambda: (self.root / "src/ignored.py").write_text("unexpected = 1\n"),
        ):
            changed = False
            with self.subTest(change=change), self.assertRaises(ValueError):
                with source_snapshot.checked_source_view(self.root):
                    source_snapshot.source_bytes(self.root, self.path)
                    change()
                    changed = True
            self.assertTrue(changed)
            self.path.chmod(0o644)
            (self.root / "src/ignored.py").unlink(missing_ok=True)

    def test_environment_and_runtime_import_changes_prevent_completion(self):
        for change in (
            lambda: os.environ.__setitem__("MICROS_SNAPSHOT_CHANGED", "1"),
            lambda: sys.path.insert(0, "/unreviewed-python-import-root"),
        ):
            changed = False
            with mock.patch.dict(os.environ), mock.patch.object(sys, "path", list(sys.path)):
                with self.subTest(change=change), self.assertRaises((ValueError, RuntimeError)):
                    with source_snapshot.checked_source_view(self.root):
                        source_snapshot.source_bytes(self.root, self.path)
                        change()
                        changed = True
            self.assertTrue(changed)

    def test_head_or_index_changes_cannot_hide_behind_unchanged_bytes(self):
        for arguments in (
            ("update-index", "--chmod=+x", "src/example.c"),
            (
                "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                "-c", "commit.gpgsign=false", "commit", "--quiet", "--allow-empty", "-m",
                "Changed fixture HEAD\n\nCo-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>",
            ),
        ):
            changed = False
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                with source_snapshot.checked_source_view(self.root):
                    source_snapshot.source_bytes(self.root, self.path)
                    self.git(*arguments)
                    changed = True
            self.assertTrue(changed)

    def test_symlink_retarget_is_not_equivalent_source_identity(self):
        other = self.root / "src/other.c"
        other.write_bytes(self.path.read_bytes())
        link = self.root / "src/linked.c"
        link.symlink_to(self.path.name)
        self.git("add", "src/other.c", "src/linked.c")
        changed = False
        with self.assertRaises(ValueError):
            with source_snapshot.checked_source_view(self.root):
                self.assertEqual(b"int value;\n", source_snapshot.source_bytes(self.root, link))
                link.unlink()
                link.symlink_to(other.name)
                changed = True
        self.assertTrue(changed)

    def test_temporary_fixture_reads_require_explicit_isolation(self):
        with tempfile.TemporaryDirectory(prefix="micros-independent-source-") as temporary:
            root = Path(temporary).resolve()
            path = root / "example.c"
            path.write_bytes(b"fixture\n")
            with source_snapshot.checked_source_view(self.root):
                source_snapshot.source_bytes(self.root, self.path)
                with self.assertRaises(ValueError):
                    source_snapshot.source_bytes(root, path)
                with source_snapshot.isolated_source_view():
                    self.assertEqual(b"fixture\n", source_snapshot.source_bytes(root, path))
                self.assertEqual(b"int value;\n", source_snapshot.source_bytes(self.root, self.path))

    def test_failed_capture_is_not_retried_or_accepted_after_catching_it(self):
        with mock.patch.object(
            source_snapshot, "_capture_snapshot", side_effect=ValueError("changed input"),
        ) as capture:
            with self.assertRaises(ValueError):
                with source_snapshot.checked_source_view(self.root):
                    for _ in range(2):
                        with self.assertRaises(ValueError):
                            source_snapshot.source_bytes(self.root, self.path)
            self.assertEqual(1, capture.call_count)
        with source_snapshot.checked_source_view(self.root):
            self.assertEqual(b"int value;\n", source_snapshot.source_bytes(self.root, self.path))

    def test_recursive_capture_is_rejected_before_another_capture(self):
        calls = 0

        def capture(root):
            nonlocal calls
            calls += 1
            self.assertEqual(1, calls, "recursive source snapshot capture")
            source_snapshot.source_bytes(root, self.path)

        with mock.patch.object(source_snapshot, "_capture_snapshot", side_effect=capture):
            with self.assertRaisesRegex(ValueError, "opening"):
                with source_snapshot.checked_source_view(self.root):
                    source_snapshot.source_bytes(self.root, self.path)
        self.assertEqual(1, calls)

    def test_unused_snapshot_does_not_capture_repository_inputs(self):
        with mock.patch.object(source_snapshot, "_capture_snapshot") as capture:
            with source_snapshot.checked_source_view(self.root):
                pass
        capture.assert_not_called()
