"""Producer failures cannot publish complete parts or aggregate success."""

import copy
from contextlib import redirect_stdout
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tests.host.test_ci_parts import model_parts
from tests.host.test_ci_records import context
from tools.ci_partition import PARTS, part_commands, part_profiles
from tools.ci_records import data_identity, file_inventory


ROOT = Path(__file__).resolve().parents[2]


def diagnostics(root, part):
    directory = root / "build/ci/parts" / part
    (directory / "diagnostics").mkdir(parents=True)
    (directory / "diagnostics/bootstrap.log").write_bytes(b"setup")
    (directory / "diagnostics/actionlint.log").write_bytes(b"")
    return directory


class CiProducersTest(unittest.TestCase):
    def test_unknown_profile_cannot_start_any_build(self):
        from tools.ci_stack_profile import run_profile

        with patch("tools.ci_stack_profile.static_configuration") as build:
            with self.assertRaises(ValueError):
                run_profile(ROOT, "riscv64-borrowed", {"CI_PART": "target-1"})
        build.assert_not_called()

    def test_failed_native_command_keeps_diagnostics_but_no_complete_part(self):
        from tools.run_ci_part import run_part

        before = context("native")
        with tempfile.TemporaryDirectory(prefix="micros-ci-failed-part-") as temporary:
            root = Path(temporary)
            directory = root / "build/ci/parts/native/diagnostics"
            directory.mkdir(parents=True)
            (directory / "bootstrap.log").write_bytes(b"setup")
            (directory / "actionlint.log").write_bytes(b"")
            commands = part_commands(ROOT, "native", before["base"])

            def failed(argv, _root, path):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"real failure output")
                return {"argv": list(argv), "exit_code": 1, "duration_ns": 1}

            with (
                patch("tools.run_ci_part.collect_environment", return_value=before),
                patch("tools.run_ci_part.part_commands", return_value=commands),
                patch("tools.run_ci_part.stream_command", side_effect=failed),
                self.assertRaises(ValueError),
            ):
                run_part(root, "native", {"CI_PART": "native"})
            self.assertTrue((directory.parent / "logs/000.log").is_file())
            self.assertFalse((directory.parent / "result.json").exists())

    def test_failed_or_skipped_matrix_cannot_start_collector_admission(self):
        from tools.collect_ci_results import collect_results

        for outcome in ("failure", "cancelled", "skipped", ""):
            with (
                self.subTest(outcome=outcome),
                patch("tools.collect_ci_results.collect_environment") as preflight,
                self.assertRaises(ValueError),
            ):
                collect_results(ROOT, {"CI_PRODUCER_RESULT": outcome})
            preflight.assert_not_called()

    def test_each_target_workflow_is_followed_by_its_own_profile_proof(self):
        from tools.run_ci_part import run_part

        part = "target-1"
        profiles = part_profiles(ROOT, part)[:2]
        commands = part_commands(ROOT, part, context(part)["base"])[:2]
        environment = {
            "CI_PART": part, "MICROS_PYTHON": "/selected/python3.15",
            "OPENSSL_CONF": "/dev/null", "SSL_CERT_FILE": "/dev/null", "SSL_CERT_DIR": "/dev/null",
        }
        expected = []
        for command, profile in zip(commands, profiles):
            expected.extend((
                tuple(command),
                (environment["MICROS_PYTHON"], "-E", "-S", "-B",
                 "tools/ci_stack_profile.py", "--profile", profile.configure),
            ))
        calls = []

        def execute(argv, _root, path):
            calls.append(tuple(argv))
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"routing model")
            return {"argv": list(argv), "exit_code": int(len(calls) == 4), "duration_ns": 1}

        with tempfile.TemporaryDirectory(prefix="micros-ci-profile-order-") as temporary:
            root = Path(temporary)
            directory = diagnostics(root, part)
            with (
                patch("tools.run_ci_part.collect_environment", return_value=context(part)),
                patch("tools.run_ci_part.part_commands", return_value=commands),
                patch("tools.run_ci_part.part_profiles", return_value=profiles),
                patch("tools.run_ci_part.stream_command", side_effect=execute),
                redirect_stdout(io.StringIO()), self.assertRaises(ValueError),
            ):
                run_part(root, part, environment)
            self.assertEqual(expected, calls)
            self.assertFalse((directory / "result.json").exists())

    def test_native_publication_rechecks_source_after_the_atomic_write(self):
        from tools.run_ci_part import run_part

        before = context("native")
        commands = part_commands(ROOT, "native", before["base"])

        def execute(argv, _root, path):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"command execution model")
            return {"argv": list(argv), "exit_code": 0, "duration_ns": 1}

        for changed in (False, True):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory(prefix="micros-ci-publish-") as temporary:
                root = Path(temporary)
                path = diagnostics(root, "native") / "result.json"

                def preflight(*_args):
                    result = copy.deepcopy(before)
                    if changed and path.exists():
                        result["candidate"]["source_sha256"] = "f" * 64
                    return result

                with (
                    patch("tools.run_ci_part.collect_environment", side_effect=preflight),
                    patch("tools.run_ci_part.part_commands", return_value=commands),
                    patch("tools.ci_parts.part_commands", return_value=commands),
                    patch("tools.run_ci_part.stream_command", side_effect=execute),
                    redirect_stdout(io.StringIO()),
                ):
                    if changed:
                        with self.assertRaises(ValueError):
                            run_part(root, "native", {"CI_PART": "native"})
                    else:
                        result = run_part(root, "native", {"CI_PART": "native"})
                        self.assertEqual("PART_COMPLETE", result["status"])
                        self.assertEqual([list(command) for command in commands],
                                         [row["argv"] for row in result["commands"]])
                self.assertEqual(not changed, path.exists())

    def test_successful_matrix_with_missing_artifacts_still_cannot_complete(self):
        from tools.collect_ci_results import collect_results

        before = context("collector")
        with tempfile.TemporaryDirectory(prefix="micros-ci-missing-part-") as temporary:
            root = Path(temporary)
            for part in PARTS[:-1]:
                (root / "build/ci/downloads" / f"micros-ci-42-1-part-{part}").mkdir(parents=True)
            with (
                patch("tools.collect_ci_results.collect_environment", return_value=before),
                patch("tools.collect_ci_results.validate_part") as validate,
                self.assertRaises(ValueError),
            ):
                collect_results(root, {"CI_PRODUCER_RESULT": "success"})
            validate.assert_not_called()
            self.assertFalse((root / "build/ci/complete.json").exists())

    def test_collector_publication_rechecks_all_original_transported_bytes(self):
        from tools.ci_parts import assemble_completion
        from tools.collect_ci_results import collect_results

        before = context("collector")
        before["role"] = "collector"
        for changed in (False, True):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory(prefix="micros-ci-fan-in-") as temporary:
                root = Path(temporary)
                complete = root / "build/ci/complete.json"
                parts = model_parts()
                for part in parts:
                    directory = root / "build/ci/downloads" / f"micros-ci-42-1-part-{part['part']}"
                    directory.mkdir(parents=True)
                    (directory / "fixture.log").write_bytes(b"post-validation model, not executed target evidence")
                    (directory / "result.json").write_bytes(b'{"model":true}')
                    part["files"] = file_inventory(directory)
                    part["result_identity"] = data_identity(directory / "result.json")
                original = root / "build/ci/downloads/micros-ci-42-1-part-native/fixture.log"

                def preflight(*_args, **_kwargs):
                    if changed and complete.exists():
                        original.write_bytes(b"replaced after publication")
                    return before

                output = io.StringIO()
                with (
                    patch("tools.collect_ci_results.collect_environment", side_effect=preflight),
                    patch("tools.collect_ci_results.validate_part", side_effect=parts),
                    patch("tools.collect_ci_results.assemble_completion",
                          side_effect=lambda _root, current, rows: assemble_completion(ROOT, current, rows)),
                    redirect_stdout(output),
                ):
                    if changed:
                        with self.assertRaises(ValueError):
                            collect_results(root, {"CI_PRODUCER_RESULT": "success"})
                    else:
                        result = collect_results(root, {"CI_PRODUCER_RESULT": "success"})
                        self.assertEqual("CI_VALIDATION_REPORT", result["kind"])
                self.assertEqual(not changed, complete.exists())
                self.assertEqual(not changed, output.getvalue().startswith("CI_VALIDATION_REPORT "))
                self.assertFalse((root / "build/stack-evidence/current.json").exists())
                self.assertFalse((root / "build/validation-evidence").exists())


if __name__ == "__main__":
    unittest.main()
