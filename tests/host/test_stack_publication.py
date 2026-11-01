"""Only complete current scopes may atomically replace published evidence."""

from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tools.stack_bound_types import StackEvidenceError


class StackPublicationTest(unittest.TestCase):
    def test_application_exit_is_an_explicit_root_with_startup_occupancy(self):
        from tools.stack_bound_catalog import DefinedFunction
        from tools.stack_bound_certify import root_functions
        from tools.stack_bound_types import FunctionKey

        image = "applications/probe.elf"
        definitions = tuple(DefinedFunction(
            FunctionKey("fixture", image, unit, symbol), 0x40000000 + index * 16, 16, True,
        ) for index, (unit, symbol) in enumerate((
            ("lib/runtime/application_start.S", "_start"),
            ("lib/runtime/application.c", "micros_application_start"),
            ("tests/qemu/probe.c", "micros_application_main"),
            ("lib/runtime/application.c", "micros_application_exit"),
        )))
        roots = root_functions(SimpleNamespace(definitions=definitions), 0x40000000, image)
        exit_roots = tuple(root for root in roots if root.function.symbol == "micros_application_exit")
        self.assertEqual(1, len(exit_roots))
        self.assertEqual(2048, exit_roots[0].initial_bytes)
        self.assertEqual(4096, exit_roots[0].capacity)
        inlined = root_functions(SimpleNamespace(definitions=definitions[:-1]), 0x40000000, image)
        self.assertFalse(any(root.function.symbol == "micros_application_exit" for root in inlined))

    def test_changed_qemu_or_firmware_invalidates_the_bound_image_proofs(self):
        import copy
        from tools.stack_bound_certify import verify_firmware_proofs

        snapshot = {
            "qemu": {"path": "/fixture/qemu", "sha256": "original executable"},
            "firmware": {"sha256": "original firmware"}, "configuration": {"default": "absent"},
        }
        proof = SimpleNamespace(fatal=SimpleNamespace(firmware=snapshot))
        with patch("tools.stack_bound_certify.resolve_default_firmware", return_value=snapshot) as resolve:
            verify_firmware_proofs((proof, proof))
        resolve.assert_called_once_with("/fixture/qemu")
        for key in snapshot:
            changed = copy.deepcopy(snapshot)
            changed[key] = {"changed": True}
            with (
                self.subTest(key=key),
                patch("tools.stack_bound_certify.resolve_default_firmware", return_value=changed),
                self.assertRaises(StackEvidenceError),
            ):
                verify_firmware_proofs((proof,))

    def test_missing_static_or_target_scopes_never_replace_existing_evidence(self):
        from tools.stack_bound_publish import publish_complete_result

        with tempfile.TemporaryDirectory(prefix="micros-stack-publication-") as temporary:
            root = Path(temporary)
            output = root / "build/stack-evidence/current.json"
            output.parent.mkdir(parents=True)
            output.write_bytes(b"prior complete evidence")
            with self.assertRaises(StackEvidenceError):
                publish_complete_result(root, None, (), ())
            self.assertEqual(b"prior complete evidence", output.read_bytes())

    def test_a_new_valid_build_seal_cannot_replace_the_observed_run_identity(self):
        from tools.stack_artifact_files import read_regular
        from tools.stack_bound_manifest import SEAL
        from tools.stack_bound_publish import publish_complete_result

        with tempfile.TemporaryDirectory(prefix="micros-stack-observed-seal-") as temporary:
            root = Path(temporary)
            configure = "riscv64-scheduler-test"
            seal = root / "build/stack-evidence" / configure / "observed" / SEAL
            seal.parent.mkdir(parents=True)
            seal.write_bytes(b'{"build":"first"}')
            observed = SimpleNamespace(
                configure=configure, gap="B03-E006", seal=read_regular(seal)[1],
                image_proofs=(), observations=(), command=(), timeout=10, transcript="old run",
            )
            seal.write_bytes(b'{"build":"replacement"}')
            candidate = SimpleNamespace(clean=True, source={"tree": "fixture"})
            with (
                patch("tools.stack_bound_publish.validate_complete_scopes"),
                patch("tools.stack_bound_publish.capture_candidate", return_value=candidate),
                patch("tools.stack_bound_publish.verify_build_seal"),
                self.assertRaisesRegex(StackEvidenceError, "observed.*changed"),
            ):
                publish_complete_result(root, candidate, (), (observed,))
            self.assertFalse((root / "build/stack-evidence/current.json").exists())

    def test_source_drift_during_publication_restores_the_previous_record(self):
        from tools.stack_bound_publish import publish_complete_result

        with tempfile.TemporaryDirectory(prefix="micros-stack-publication-race-") as temporary:
            root = Path(temporary)
            output = root / "build/stack-evidence/current.json"
            output.parent.mkdir(parents=True)
            previous = b'{"previous":true}'
            output.write_bytes(previous)
            output.chmod(0o600)
            candidate = SimpleNamespace(clean=True, source={"tree": "fixture"})
            changed = SimpleNamespace(clean=False, source={"tree": "changed"})

            def current(_root):
                return candidate if output.read_bytes() == previous else changed

            with (
                patch("tools.stack_bound_publish.validate_complete_scopes"),
                patch("tools.stack_bound_publish.capture_candidate", side_effect=current),
                self.assertRaisesRegex(StackEvidenceError, "source changed"),
            ):
                publish_complete_result(root, candidate, (), ())
            self.assertEqual(previous, output.read_bytes())


if __name__ == "__main__":
    unittest.main()
