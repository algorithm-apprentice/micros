"""Bounded proof work and checked source-view lifetime regressions."""

from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from tests.host.evidence_authority_types import Environment


ROOT = Path(__file__).resolve().parents[2]


class AuthorityJournalReplayTest(unittest.TestCase):
    def test_terminal_queries_do_not_replay_an_already_verified_journal(self):
        from tests.host import (
            bootstrap_allocation_authority,
            diagnostic_history_authority,
            kernel_mapping_authority,
            runtime_memory_authority,
        )

        providers = (
            runtime_memory_authority, diagnostic_history_authority,
            bootstrap_allocation_authority, kernel_mapping_authority,
        )
        for authority in providers:
            with self.subTest(provider=authority.__name__):
                environment = Environment((), (), (), (), "a" * 40, "b" * 40)
                context = SimpleNamespace(root=ROOT, environment=environment)
                selector = {"attempt": "0" * 32, "count": 3, "sha256": "c" * 64}
                records = [
                    ({"environment": environment}, {"sha256": value * 64})
                    for value in ("a", "b", "c")
                ]
                with tempfile.TemporaryDirectory(prefix="micros-proof-count-") as directory, ExitStack() as patches:
                    path = Path(directory)
                    (path / selector["attempt"]).mkdir()
                    values = {
                        "_provisional": None,
                        "committed_claim": mock.Mock(return_value=None),
                        "read_current": mock.Mock(return_value=selector),
                        "journal_directory": mock.Mock(return_value=path),
                        "read_phase_record": mock.Mock(side_effect=records),
                        "capture_author_context": mock.Mock(return_value=context),
                        "recheck_context": mock.Mock(),
                    }
                    if authority is not runtime_memory_authority:
                        values["read_selection"] = mock.Mock(return_value=(selector, "d" * 64))
                    for name, value in values.items():
                        patches.enter_context(mock.patch.object(authority, name, value))
                    verify = patches.enter_context(mock.patch.object(
                        authority, "validate_journal",
                        return_value=SimpleNamespace(active_identity_keys=()),
                    ))
                    if authority is runtime_memory_authority:
                        required = authority.memory_identity_required(ROOT)
                    else:
                        required = authority.identity_required(ROOT, authority.SOURCE_PROJECTION["retirement_keys"][0])
                    self.assertFalse(required)
                    self.assertEqual(1, verify.call_count, "one terminal query must replay each proof only once")


class CheckedSourceViewTest(unittest.TestCase):
    def setUp(self):
        from tests.host import evidence_authority_scope

        self.scope = evidence_authority_scope
        temporary = tempfile.TemporaryDirectory(prefix="micros-source-view-")
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
            "Source fixture\n\nCo-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>",
        )
        self.patches = ExitStack()
        self.addCleanup(self.patches.close)
        self.patches.enter_context(mock.patch.object(self.scope, "_active", None))
        self.close_check = mock.Mock()
        self.proof = self.scope.VerifiedAuthority(frozenset({"fixture:retired"}), (self.close_check,))
        self.verify = self.patches.enter_context(mock.patch.object(
            self.scope, "_verify_authorities", return_value=self.proof,
        ))

    def git(self, *arguments):
        return subprocess.check_output(
            ("git", *arguments), cwd=self.root, stderr=subprocess.STDOUT,
        )

    def test_many_reads_share_one_complete_proof_and_close_it(self):
        with self.scope.checked_source_view(self.root) as view:
            for _ in range(15):
                self.assertFalse(self.scope.identity_required(self.root, "fixture:retired"))
                self.assertTrue(self.scope.identity_required(self.root, "fixture:unrelated"))
                self.assertEqual(b"int value;\n", self.scope.source_bytes(self.root, self.path))
            self.assertEqual(1, self.verify.call_count)
        self.assertGreaterEqual(self.close_check.call_count, 1)
        with self.assertRaises(ValueError):
            view.identity_required("fixture:retired")

    def test_unused_view_does_not_run_repository_or_tool_proofs(self):
        with self.scope.checked_source_view(self.root):
            pass
        self.verify.assert_not_called()
        self.close_check.assert_not_called()

    def test_source_reads_are_immutable_but_changed_inputs_reject_completion(self):
        with self.assertRaises(ValueError):
            with self.scope.checked_source_view(self.root):
                self.assertEqual(b"int value;\n", self.scope.source_bytes(self.root, self.path))
                before = self.path.stat()
                self.path.write_bytes(b"int other;\n")
                os.utime(self.path, ns=(before.st_atime_ns, before.st_mtime_ns))
                self.assertEqual(b"int value;\n", self.scope.source_bytes(self.root, self.path))

    def test_mode_and_ignored_namespace_mutation_cannot_hide_behind_same_content(self):
        for change in (
            lambda: self.path.chmod(0o755),
            lambda: (self.root / "src/ignored.py").write_text("unexpected = 1\n"),
        ):
            with self.subTest(change=change), self.assertRaises(ValueError):
                with self.scope.checked_source_view(self.root):
                    self.scope.source_bytes(self.root, self.path)
                    change()
            self.path.chmod(0o644)
            (self.root / "src/ignored.py").unlink(missing_ok=True)

    def test_environment_change_rejects_completion(self):
        with mock.patch.dict(os.environ):
            with self.assertRaises(ValueError):
                with self.scope.checked_source_view(self.root):
                    self.scope.identity_required(self.root, "fixture:retired")
                    os.environ["MICROS_SOURCE_VIEW_CHANGED"] = "1"

    def test_head_and_index_changes_reject_completion_without_worktree_byte_changes(self):
        for arguments in (
            ("update-index", "--chmod=+x", "src/example.c"),
            (
                "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                "-c", "commit.gpgsign=false", "commit", "--quiet", "--allow-empty", "-m",
                "Changed fixture HEAD\n\nCo-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>",
            ),
        ):
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                with self.scope.checked_source_view(self.root):
                    self.scope.source_bytes(self.root, self.path)
                    self.git(*arguments)
                    self.assertEqual(b"int value;\n", self.path.read_bytes())

    def test_symlink_retarget_with_identical_target_bytes_rejects_completion(self):
        other = self.root / "src/other.c"
        other.write_bytes(self.path.read_bytes())
        link = self.root / "src/linked.c"
        link.symlink_to(self.path.name)
        self.git("add", "src/other.c", "src/linked.c")
        with self.assertRaises(ValueError):
            with self.scope.checked_source_view(self.root):
                self.assertEqual(b"int value;\n", self.scope.source_bytes(self.root, link))
                link.unlink()
                link.symlink_to(other.name)
                self.assertEqual(b"int value;\n", self.scope.source_bytes(self.root, link))

    def test_source_mutation_during_proof_construction_cannot_publish_a_decision(self):
        def changed(root):
            self.path.write_bytes(b"int other;\n")
            return self.proof

        self.verify.side_effect = changed
        with self.assertRaisesRegex(ValueError, "before checked-view publication"):
            with self.scope.checked_source_view(self.root):
                self.scope.identity_required(self.root, "fixture:retired")

    def test_running_python_import_boundary_change_rejects_completion(self):
        with mock.patch.object(sys, "path", list(sys.path)):
            with self.assertRaises((ValueError, RuntimeError)):
                with self.scope.checked_source_view(self.root):
                    self.scope.identity_required(self.root, "fixture:retired")
                    sys.path.insert(0, "/unreviewed-python-import-root")

    def test_foreign_process_and_nested_owner_cannot_use_the_view(self):
        with self.scope.checked_source_view(self.root):
            self.scope.identity_required(self.root, "fixture:retired")
            with mock.patch.object(self.scope.os, "getpid", return_value=-1):
                with self.assertRaises(ValueError):
                    self.scope.identity_required(self.root, "fixture:retired")
            with self.assertRaises(ValueError):
                with self.scope.checked_source_view(self.root):
                    self.fail("nested owner was admitted")
            self.assertFalse(self.scope.identity_required(self.root, "fixture:retired"))

    def test_other_fixture_root_keeps_cold_authority_instead_of_borrowing_keys(self):
        with self.scope.checked_source_view(self.root):
            self.assertFalse(self.scope.identity_required(self.root, "fixture:retired"))
            other = self.root / "other-fixture"
            with mock.patch.object(self.scope, "_cold_identity_required", return_value=True) as cold:
                self.assertTrue(self.scope.identity_required(other, "fixture:retired"))
            cold.assert_called_once_with(other.resolve(), "fixture:retired")

    def test_failed_open_or_close_cannot_leave_usable_scope_state(self):
        self.verify.side_effect = ValueError("invalid proof")
        with self.assertRaisesRegex(ValueError, "invalid proof"):
            with self.scope.checked_source_view(self.root):
                self.scope.identity_required(self.root, "fixture:retired")
        self.verify.side_effect = None
        self.close_check.side_effect = ValueError("changed proof input")
        with self.assertRaisesRegex(ValueError, "changed proof input"):
            with self.scope.checked_source_view(self.root):
                self.scope.identity_required(self.root, "fixture:retired")
        self.close_check.side_effect = None
        with self.scope.checked_source_view(self.root):
            self.assertFalse(self.scope.identity_required(self.root, "fixture:retired"))
        self.assertEqual(3, self.verify.call_count)

    def test_failed_open_is_not_retried_or_accepted_when_a_test_catches_the_error(self):
        self.verify.side_effect = ValueError("invalid proof")
        with self.assertRaises(ValueError):
            with self.scope.checked_source_view(self.root):
                for _ in range(2):
                    with self.assertRaises(ValueError):
                        self.scope.identity_required(self.root, "fixture:retired")
                self.assertEqual(1, self.verify.call_count)


class AuthorViewInputsTest(unittest.TestCase):
    def setUp(self):
        from tests.host import evidence_authority_scope, kernel_mapping_authority
        from tests.host.evidence_authority_types import TransitionState

        self.scope = evidence_authority_scope
        temporary = tempfile.TemporaryDirectory(prefix="micros-view-author-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        (self.root / "authority.py").write_text("declaration = 1\n")
        directory = self.root / kernel_mapping_authority.JOURNAL_PATH / ("0" * 32)
        directory.mkdir(parents=True)
        records, previous = [], ""
        for phase in ("red", "green", "recertify"):
            output = (phase + " execution\n").encode()
            (directory / (phase + "-execution.log")).write_bytes(output)
            record = {
                "previous": previous,
                "logs": {"execution": {"size": len(output), "sha256": hashlib.sha256(output).hexdigest()}},
            }
            data = json.dumps(record, sort_keys=True).encode()
            (directory / (phase + ".json")).write_bytes(data)
            previous = hashlib.sha256(data).hexdigest()
            records.append(record)
        selector = {"version": 1, "attempt": directory.name, "count": 3, "sha256": previous}
        data = json.dumps(selector, sort_keys=True).encode()
        (directory.parent / "current.json").write_bytes(data)
        self.journal = SimpleNamespace(
            directory=directory, records=tuple(records), attempt=directory.name,
            sha256=previous, selector_sha256=hashlib.sha256(data).hexdigest(),
        )
        self.context = SimpleNamespace(
            root=self.root, inventory=object(), native_compiler=None,
            environment=Environment((("@fixture/tool", "stable"),), (), (), (), "a" * 40, "b" * 40),
        )
        result = SimpleNamespace(ok=True, state=TransitionState.RETIREMENT_READY, active_identity_keys=())
        self.provider = SimpleNamespace(
            __name__="fixture.kernel_mapping_authority",
            AUTHORITY_PATH="authority.py",
            SOURCE_PROJECTION={"retirement_keys": ("fixture:retired",)},
            PHASE_KINDS=("red", "green", "recertify"),
            committed_claim=mock.Mock(return_value=None),
            read_current=lambda path: json.loads((path / "current.json").read_text()),
            journal_directory=kernel_mapping_authority.journal_directory,
            capture_author_context=mock.Mock(return_value=self.context),
            read_verified_journal=mock.Mock(return_value=(self.journal, result)),
            external_inputs=mock.Mock(return_value=(None, {"@fixture/tool": "stable"})),
            recheck_context=mock.Mock(),
        )
        self.providers = mock.patch.object(self.scope, "_providers", return_value=(self.provider,)).start()
        self.inventory = mock.patch(
            "tests.host.evidence_authority_inventory.current_inventory", return_value=self.context.inventory,
        ).start()
        self.addCleanup(mock.patch.stopall)

    def test_author_view_reuses_its_already_materialized_inventory(self):
        proof = self.scope._verify_authorities(self.root)
        self.assertEqual(frozenset({"fixture:retired"}), proof.retired_keys)
        self.assertEqual(0, self.inventory.call_count, "author context already owns the verified inventory")
        self.assertEqual(1, self.provider.capture_author_context.call_count)
        self.assertEqual(1, self.provider.read_verified_journal.call_count)

    def test_absent_or_incomplete_author_attempt_retires_no_additional_identity(self):
        for selected in (None, {"count": 2}):
            with self.subTest(selected=selected):
                self.provider.read_current = mock.Mock(return_value=selected)
                proof = self.scope._verify_authorities(self.root)
                self.assertEqual(frozenset(), proof.retired_keys)
                self.assertEqual((), proof.checks)
                self.provider.capture_author_context.assert_not_called()
                self.provider.read_verified_journal.assert_not_called()

    def test_changed_log_cannot_be_accepted_at_view_completion(self):
        proof = self.scope._verify_authorities(self.root)
        path = self.journal.directory / "green-execution.log"
        data = path.read_bytes()
        path.write_bytes(b"x" * len(data))
        with self.assertRaises(ValueError):
            proof.checks[0]()

    def test_linked_attempt_or_journal_parent_rejects_identical_record_bytes(self):
        proof = self.scope._verify_authorities(self.root)
        for path in (self.journal.directory, self.journal.directory.parent):
            moved = self.root / "relocated-journal"
            with self.subTest(path=path):
                path.rename(moved)
                path.symlink_to(moved, target_is_directory=True)
                try:
                    with self.assertRaises(ValueError):
                        proof.checks[0]()
                finally:
                    path.unlink()
                    moved.rename(path)

    def test_external_input_change_rejects_completion_without_rebuilding_the_inventory(self):
        proof = self.scope._verify_authorities(self.root)
        self.provider.external_inputs.return_value = (None, {"@fixture/tool": "changed"})
        with self.assertRaisesRegex(ValueError, "external inputs"):
            proof.checks[0]()
        self.inventory.assert_not_called()
        self.provider.recheck_context.assert_not_called()

    def test_selected_ownership_view_binds_its_distinct_native_symbols_tool(self):
        for provider_name in (
            "tests.host.selected_ownership_authority",
            "tests.host.selected_address_space_authority",
            "tests.host.selected_release_handoff_authority",
            "tests.host.object_hart_validation_authority",
        ):
            self.provider.read_verified_journal.reset_mock()
            self.provider.__name__ = provider_name
            self.context.native_nm = {"path": "/fixture/nm", "identity": "original"}
            external = {"@fixture/tool": "stable"}
            self.provider.external_inputs.return_value = (None, self.context.native_nm, external)
            proof = self.scope._verify_authorities(self.root)
            self.assertEqual(frozenset({"fixture:retired"}), proof.retired_keys)
            proof.checks[0]()
            for observed in (
                (None, {"path": "/fixture/nm", "identity": "changed"}, external),
                (None, external),
            ):
                with self.subTest(observed=observed), self.assertRaises(ValueError):
                    self.provider.external_inputs.return_value = observed
                    proof.checks[0]()
            self.inventory.assert_not_called()
            self.assertEqual(1, self.provider.read_verified_journal.call_count)

    def test_selector_drift_during_proof_verification_blocks_publication(self):
        original = self.provider.read_verified_journal.return_value

        def changed(context):
            path = self.journal.directory.parent / "current.json"
            path.write_bytes(path.read_bytes() + b"\n")
            return original

        self.provider.read_verified_journal.side_effect = changed
        with self.assertRaises(ValueError):
            self.scope._verify_authorities(self.root)
