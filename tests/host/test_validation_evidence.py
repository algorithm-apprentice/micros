import copy
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools import validation_evidence


class ValidationEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.directory = self.root / "build/validation-evidence/v1"
        self.identity = {
            "repository": {"root": str(self.root), "git_common": str(self.root / ".git")},
            "candidate": {"head": "a" * 40, "tree": "b" * 40, "source_sha256": "c" * 64},
            "context": {"profile": "fixture", "environment_sha256": "d" * 64},
            "inputs": {"fixture": "e" * 64},
            "outputs": {"artifact": {"mode": 0o755, "sha256": "f" * 64}},
        }

    def store(self):
        return validation_evidence.EvidenceStore(
            self.root, ("test-unit", "test-qemu-smoke")
        )

    def produce(self, store, name="test-unit", identity=None):
        attempt = store.start(name)
        attempt.log.write(b"actual complete fixture output\n")
        return store.complete(attempt, self.identity if identity is None else identity, 0)

    def receipt_path(self, name="test-unit"):
        return self.directory / (name + ".json")

    def mutate_receipt(self, change):
        path = self.receipt_path()
        value = json.loads(path.read_text())
        change(value)
        path.write_text(json.dumps(value))

    def test_actual_success_publishes_exact_identity_and_complete_log(self):
        with self.store() as store:
            produced = self.produce(store)
            reused, reason = store.lookup("test-unit", self.identity)
            self.assertEqual(produced, reused)
            self.assertIn("verified", reason)
            self.assertEqual(
                ["cmake", "--workflow", "--preset", "test-unit"], reused["argv"]
            )
            self.assertEqual({"exit_code": 0, "postflight": True}, reused["result"])
            log = self.directory / reused["log"]["path"]
            self.assertEqual(b"actual complete fixture output\n", log.read_bytes())

    def test_absence_is_an_explained_miss(self):
        with self.store() as store:
            receipt, reason = store.lookup("test-unit", self.identity)
            self.assertIsNone(receipt)
            self.assertIn("absent", reason)

    def test_changed_candidate_context_inputs_or_outputs_cannot_reuse(self):
        with self.store() as store:
            self.produce(store)
            for field in ("repository", "candidate", "context", "inputs", "outputs"):
                with self.subTest(field=field):
                    identity = copy.deepcopy(self.identity)
                    identity[field]["changed"] = True
                    receipt, reason = store.lookup("test-unit", identity)
                    self.assertIsNone(receipt)
                    self.assertIn(field, reason)

    def test_workflow_identity_never_covers_another_gate(self):
        with self.store() as store:
            self.produce(store)
            receipt, reason = store.lookup("test-qemu-smoke", self.identity)
            self.assertIsNone(receipt)
            self.assertIn("absent", reason)

    def test_fresh_attempt_invalidates_previous_success_before_execution(self):
        with self.store() as store:
            self.produce(store)
            attempt = store.start("test-unit")
            self.assertFalse(self.receipt_path().exists())
            attempt.log.write(b"failed current attempt\n")
            self.assertIsNone(store.complete(attempt, self.identity, 7))
            self.assertIsNone(store.lookup("test-unit", self.identity)[0])

    def test_interruption_cannot_resurrect_old_success(self):
        with self.assertRaises(KeyboardInterrupt):
            with self.store() as store:
                self.produce(store)
                attempt = store.start("test-unit")
                attempt.log.write(b"incomplete")
                raise KeyboardInterrupt()
        with self.store() as store:
            self.assertIsNone(store.lookup("test-unit", self.identity)[0])

    def test_failed_postflight_does_not_publish_success(self):
        with self.store() as store:
            attempt = store.start("test-unit")
            attempt.log.write(b"source changed during execution\n")
            with self.assertRaisesRegex(validation_evidence.EvidenceError, "postflight"):
                store.complete(attempt, self.identity, 0, postflight=False)
            self.assertFalse(self.receipt_path().exists())

    def test_ineligible_success_preserves_a_log_but_no_receipt(self):
        with self.store() as store:
            attempt = store.start("test-unit")
            attempt.log.write(b"fresh but unsupported configuration\n")
            self.assertIsNone(store.complete(attempt, None, 0))
            self.assertFalse(self.receipt_path().exists())
            self.assertEqual(1, len(list((self.directory / "logs").glob("*.log"))))

    def test_competing_publisher_is_rejected_without_waiting(self):
        with self.store():
            with self.assertRaisesRegex(validation_evidence.EvidenceError, "busy"):
                with self.store():
                    self.fail("a second publisher acquired the exclusive lock")

    def test_unknown_command_and_path_spellings_are_rejected(self):
        with self.store() as store:
            for name in ("../outside", "test-unit; echo injected", "unknown"):
                with self.subTest(name=name):
                    with self.assertRaises(validation_evidence.EvidenceError):
                        store.start(name)

    def test_malformed_schema_unknown_fields_and_fake_success_are_misses(self):
        changes = (
            lambda value: value.update(schema=99),
            lambda value: value.update(extra=True),
            lambda value: value.update(workflow="test-qemu-smoke"),
            lambda value: value.update(argv=["sh", "-c", "untrusted"]),
            lambda value: value["producer"].update(tool_version="unknown"),
            lambda value: value["result"].update(exit_code=1),
            lambda value: value["result"].update(exit_code=False),
            lambda value: value["result"].update(postflight=False),
        )
        with self.store() as store:
            for change in changes:
                with self.subTest(change=change):
                    self.produce(store)
                    self.mutate_receipt(change)
                    receipt, reason = store.lookup("test-unit", self.identity)
                    self.assertIsNone(receipt)
                    self.assertTrue(reason)
            self.receipt_path().write_text("{truncated")
            self.assertIsNone(store.lookup("test-unit", self.identity)[0])

    def test_missing_edited_or_truncated_complete_log_is_not_success(self):
        with self.store() as store:
            for replacement in (None, b"", b"changed"):
                with self.subTest(replacement=replacement):
                    receipt = self.produce(store)
                    log = self.directory / receipt["log"]["path"]
                    if replacement is None:
                        log.unlink()
                    else:
                        log.write_bytes(replacement)
                    self.assertIsNone(store.lookup("test-unit", self.identity)[0])

    def test_log_traversal_external_path_and_wrong_attempt_are_rejected(self):
        with self.store() as store:
            for path in ("../external.log", "/tmp/external.log", "logs/" + "0" * 32 + ".log"):
                with self.subTest(path=path):
                    self.produce(store)
                    self.mutate_receipt(lambda value: value["log"].update(path=path))
                    self.assertIsNone(store.lookup("test-unit", self.identity)[0])

    def test_receipt_and_log_symlinks_never_follow_external_files(self):
        outside = self.root / "external"
        outside.write_text("must not be read or changed")
        with self.store() as store:
            receipt = self.produce(store)
            log = self.directory / receipt["log"]["path"]
            log.unlink()
            log.symlink_to(outside)
            self.assertIsNone(store.lookup("test-unit", self.identity)[0])
            self.receipt_path().unlink()
            self.receipt_path().symlink_to(outside)
            self.assertIsNone(store.lookup("test-unit", self.identity)[0])
        self.assertEqual("must not be read or changed", outside.read_text())

    def test_store_ancestor_symlinks_are_rejected(self):
        outside = self.root / "outside"
        outside.mkdir()
        (self.root / "build").symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(validation_evidence.EvidenceError, "symlink"):
            with self.store():
                self.fail("symlinked evidence store was admitted")

    def test_duplicate_json_fields_are_rejected(self):
        with self.store() as store:
            self.produce(store)
            path = self.receipt_path()
            content = path.read_text()
            path.write_text('{"schema":1,' + content[1:])
            receipt, reason = store.lookup("test-unit", self.identity)
            self.assertIsNone(receipt)
            self.assertIn("duplicate", reason)

    def test_context_json_types_cannot_be_coerced_into_equal_identities(self):
        identity = copy.deepcopy(self.identity)
        identity["context"]["version"] = 1
        with self.store() as store:
            self.produce(store, identity=identity)
            self.mutate_receipt(lambda value: value["context"].update(version=True))
            self.assertIsNone(store.lookup("test-unit", identity)[0])

    def test_one_publisher_cannot_overlap_two_attempts_for_the_same_workflow(self):
        with self.store() as store:
            store.start("test-unit")
            with self.assertRaisesRegex(validation_evidence.EvidenceError, "active"):
                store.start("test-unit")

    def test_receipt_publication_failure_leaves_no_stale_or_partial_success(self):
        replace = os.replace
        with self.store() as store:
            self.produce(store)
            attempt = store.start("test-unit")
            attempt.log.write(b"current successful subprocess\n")

            def fail_receipt(source, destination, **options):
                if destination.endswith(".json"):
                    logs = list((self.directory / "logs").glob("*.log"))
                    self.assertTrue(any(path.read_bytes() == b"current successful subprocess\n" for path in logs))
                    raise OSError("injected receipt publication failure")
                return replace(source, destination, **options)

            with mock.patch("tools.validation_evidence.os.replace", side_effect=fail_receipt):
                with self.assertRaisesRegex(OSError, "publication failure"):
                    store.complete(attempt, self.identity, 0)
            self.assertFalse(self.receipt_path().exists())
            self.assertEqual([], list(self.directory.glob("*.tmp")))
            self.assertIsNone(store.lookup("test-unit", self.identity)[0])

    def test_directory_substitution_during_attempt_cannot_publish(self):
        with self.store() as store:
            attempt = store.start("test-unit")
            attempt.log.write(b"output\n")
            moved = self.directory.with_name("moved")
            self.directory.rename(moved)
            self.directory.mkdir()
            (self.directory / "logs").mkdir()
            with self.assertRaisesRegex(validation_evidence.EvidenceError, "identity changed"):
                store.complete(attempt, self.identity, 0)


if __name__ == "__main__":
    unittest.main()
