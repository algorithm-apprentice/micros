from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools import validation_context, validation_execution, validation_plan


WORKFLOWS = ("test-unit", "test-qemu-smoke", "build-tty-service-image")


def workflow(name):
    return ("cmake", "--workflow", "--preset", name)


class ValidationExecutionTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.candidate = validation_context.Candidate(
            {"root": str(self.root), "git_common": str(self.root / ".git")},
            {"head": "a" * 40, "tree": "b" * 40, "source_sha256": "c" * 64},
            True,
        )
        self.state = {"context": "same", "artifact": "same", "exit_code": 0}
        self.calls = []
        self.capture = mock.patch(
            "tools.validation_context.capture_candidate", side_effect=lambda root: self.candidate
        ).start()
        self.addCleanup(mock.patch.stopall)
        self.collect = mock.patch(
            "tools.validation_context.collect", side_effect=self.manifest
        ).start()
        self.run = mock.patch(
            "tools.validation_execution.subprocess.run", side_effect=self.subprocess
        ).start()

    def manifest(self, root, name):
        self.assertEqual(self.root, Path(root))
        return {
            "context": {"profile": "fixture", "workflow": name, "identity": self.state["context"]},
            "inputs": {"closure": "closed"},
            "outputs": {"artifact": self.state["artifact"]},
        }

    def subprocess(self, command, **options):
        self.assertEqual(self.root, Path(options["cwd"]))
        self.calls.append(tuple(command))
        if options.get("stdout") is not None:
            options["stdout"].write(b"actual fixture subprocess output\n")
        return mock.Mock(returncode=self.state["exit_code"])

    def execute(self, commands, **options):
        output = io.StringIO()
        with redirect_stdout(output), redirect_stderr(output):
            result = validation_execution.execute_plan(
                self.root, commands, WORKFLOWS, **options
            )
        reports = [
            json.loads(line[len("VALIDATION_REPORT "):])
            for line in output.getvalue().splitlines()
            if line.startswith("VALIDATION_REPORT ")
        ]
        self.assertEqual(1, len(reports), output.getvalue())
        return result, reports[0], output.getvalue()

    def test_exact_second_plan_reuses_all_three_workflows_but_not_docs_or_diffs(self):
        commands = [
            *(workflow(name) for name in WORKFLOWS),
            ("python3", "tools/check_docs.py"),
            *validation_plan.diff_commands("base"),
        ]
        first_status, first, _ = self.execute(commands)
        count = len(self.calls)
        second_status, second, output = self.execute(commands, reuse=True)
        self.assertEqual((0, 0), (first_status, second_status))
        self.assertEqual(commands, self.calls[:count])
        self.assertEqual(commands[-4:], self.calls[count:])
        self.assertEqual(
            ["REUSED"] * 3 + ["FRESH"] * 4,
            [entry["status"] for entry in second["commands"]],
        )
        for old, reused in zip(first["commands"], second["commands"]):
            if reused["status"] == "REUSED":
                self.assertEqual(old["producer"], reused["producer"])
                self.assertEqual(old["log"], reused["log"])
        self.assertEqual(self.candidate.source, second["candidate"])
        self.assertTrue(second["authoritative"])
        self.assertIn("REUSED", output)

    def test_default_fresh_mode_does_not_consult_existing_success(self):
        commands = [workflow("test-unit")]
        self.execute(commands)
        _, report, _ = self.execute(commands)
        self.assertEqual(commands * 2, self.calls)
        self.assertEqual("FRESH", report["commands"][0]["status"])

    def test_changed_environment_or_artifact_forces_fresh(self):
        commands = [workflow("test-unit")]
        self.execute(commands)
        for field in ("context", "artifact"):
            self.state[field] += "-changed"
            before = len(self.calls)
            status, report, _ = self.execute(commands, reuse=True)
            self.assertEqual(0, status)
            self.assertEqual(before + 1, len(self.calls))
            self.assertEqual("FRESH", report["commands"][0]["status"])

    def test_gate_only_producer_cannot_report_final_author_validation(self):
        status, report, output = self.execute([workflow("test-unit")], gate_only=True)
        self.assertEqual(0, status)
        self.assertFalse(report["authoritative"])
        self.assertIn("gate-only", output.lower())
        self.execute([workflow("test-unit")], reuse=True)
        self.assertEqual(1, len(self.calls))

    def test_dirty_tracked_iteration_runs_fresh_without_a_reusable_receipt(self):
        self.candidate = validation_context.Candidate(
            self.candidate.repository, self.candidate.source, False
        )
        status, report, output = self.execute([workflow("test-unit")])
        self.assertEqual(0, status)
        self.assertEqual("FRESH", report["commands"][0]["status"])
        self.assertFalse(report["final_candidate"])
        self.assertIn("iteration", output.lower())
        self.assertFalse(
            (self.root / "build/validation-evidence/v1/test-unit.json").exists()
        )

    def test_dirty_candidate_refuses_reuse_and_gate_only_before_execution(self):
        self.candidate = validation_context.Candidate(
            self.candidate.repository, self.candidate.source, False
        )
        for options in ({"reuse": True}, {"gate_only": True}):
            with self.subTest(options=options):
                status, report, _ = self.execute([workflow("test-unit")], **options)
                self.assertNotEqual(0, status)
                self.assertEqual("NOT_RUN", report["commands"][0]["status"])
        self.assertEqual([], self.calls)

    def test_untracked_or_unclosed_source_cannot_run_a_stale_plan(self):
        self.capture.side_effect = validation_context.UnsupportedContext("untracked task")
        status, report, output = self.execute([workflow("test-unit")])
        self.assertNotEqual(0, status)
        self.assertEqual("NOT_RUN", report["commands"][0]["status"])
        self.assertIn("untracked", output)
        self.assertEqual([], self.calls)

    def test_unsupported_context_runs_fresh_and_never_invents_a_receipt(self):
        self.collect.side_effect = validation_context.UnsupportedContext("unclosed tool")
        commands = [workflow("test-unit")]
        self.execute(commands)
        status, report, output = self.execute(commands, reuse=True)
        self.assertEqual(0, status)
        self.assertEqual(commands * 2, self.calls)
        self.assertEqual("FRESH", report["commands"][0]["status"])
        self.assertIn("unclosed tool", output)
        self.assertFalse(
            (self.root / "build/validation-evidence/v1/test-unit.json").exists()
        )

    def test_failed_fresh_rerun_invalidates_success_and_stops_remaining_commands(self):
        command = workflow("test-unit")
        self.execute([command])
        self.state["exit_code"] = 7
        status, report, _ = self.execute([command, workflow("test-qemu-smoke")])
        self.assertNotEqual(0, status)
        self.assertEqual(["FAILED", "NOT_RUN"], [item["status"] for item in report["commands"]])
        self.state["exit_code"] = 0
        before = len(self.calls)
        self.execute([command], reuse=True)
        self.assertEqual(before + 1, len(self.calls))

    def test_old_success_is_gone_before_fresh_subprocess_launch(self):
        command = workflow("test-unit")
        self.execute([command])
        receipt = self.root / "build/validation-evidence/v1/test-unit.json"
        runner = self.subprocess

        def checked(command, **options):
            self.assertFalse(receipt.exists())
            return runner(command, **options)

        self.run.side_effect = checked
        self.execute([command])

    def test_source_mutation_during_execution_blocks_evidence_and_remaining_work(self):
        runner = self.subprocess

        def changed(command, **options):
            result = runner(command, **options)
            source = {**self.candidate.source, "tree": "d" * 40}
            self.candidate = validation_context.Candidate(self.candidate.repository, source, True)
            return result

        self.run.side_effect = changed
        status, report, output = self.execute([
            workflow("test-unit"), workflow("test-qemu-smoke")
        ])
        self.assertNotEqual(0, status)
        self.assertEqual(["FAILED", "NOT_RUN"], [entry["status"] for entry in report["commands"]])
        self.assertIn("source", output.lower())
        self.assertFalse(
            (self.root / "build/validation-evidence/v1/test-unit.json").exists()
        )

    def test_context_mutation_during_execution_is_not_a_fresh_success_receipt(self):
        runner = self.subprocess

        def changed(command, **options):
            result = runner(command, **options)
            self.state["context"] = "mutated"
            return result

        self.run.side_effect = changed
        status, report, output = self.execute([workflow("test-unit")])
        self.assertNotEqual(0, status)
        self.assertEqual("FAILED", report["commands"][0]["status"])
        self.assertIn("context", output.lower())

    def test_interrupted_fresh_attempt_does_not_resurrect_an_older_success(self):
        command = workflow("test-unit")
        self.execute([command])
        self.run.side_effect = KeyboardInterrupt()
        status, report, _ = self.execute([command])
        self.assertNotEqual(0, status)
        self.assertEqual("FAILED", report["commands"][0]["status"])
        self.run.side_effect = self.subprocess
        before = len(self.calls)
        self.execute([command], reuse=True)
        self.assertEqual(before + 1, len(self.calls))

    def test_unknown_or_arbitrary_commands_are_rejected_not_cached_or_executed(self):
        for command in (
            workflow("unknown"), ("sh", "-c", "unexpected"),
            ("cmake", "--workflow", "--preset", "test-unit", "extra"),
        ):
            with self.subTest(command=command):
                with self.assertRaises(validation_execution.ExecutionError):
                    self.execute([command])
        self.assertEqual([], self.calls)

    def test_profile_metadata_cannot_replace_source_or_command_authority(self):
        self.collect.side_effect = None
        self.collect.return_value = {
            **self.manifest(self.root, "test-unit"),
            "candidate": {"head": "unreviewed"},
        }
        status, report, output = self.execute([workflow("test-unit")])
        self.assertNotEqual(0, status)
        self.assertEqual("NOT_RUN", report["commands"][0]["status"])
        self.assertIn("profile", output.lower())
        self.assertEqual([], self.calls)

    def test_gate_only_mode_accepts_only_one_fresh_canonical_workflow(self):
        for commands, options in (
            ([workflow("test-unit")], {"gate_only": True, "reuse": True}),
            ([workflow("test-unit"), workflow("test-qemu-smoke")], {"gate_only": True}),
            ([("python3", "tools/check_docs.py")], {"gate_only": True}),
        ):
            with self.subTest(commands=commands, options=options):
                with self.assertRaises(validation_execution.ExecutionError):
                    self.execute(commands, **options)
        self.assertEqual([], self.calls)

    def test_complete_raw_log_without_newline_cannot_merge_with_the_report(self):
        def without_newline(command, **options):
            self.calls.append(tuple(command))
            options["stdout"].write(b"unterminated raw output")
            return mock.Mock(returncode=0)

        self.run.side_effect = without_newline
        status, report, _ = self.execute([workflow("test-unit")])
        self.assertEqual(0, status)
        path = self.root / "build/validation-evidence/v1" / report["commands"][0]["log"]["path"]
        self.assertEqual(b"unterminated raw output", path.read_bytes())

    def test_actual_fresh_outputs_are_recorded_after_execution(self):
        runner = self.subprocess

        def produced(command, **options):
            result = runner(command, **options)
            self.state["artifact"] = "newly produced"
            return result

        self.run.side_effect = produced
        self.execute([workflow("test-unit")])
        status, report, _ = self.execute([workflow("test-unit")], reuse=True)
        self.assertEqual(0, status)
        self.assertEqual(1, len(self.calls))
        self.assertEqual("REUSED", report["commands"][0]["status"])


class ValidationExecutionArgumentsTests(unittest.TestCase):
    def test_supported_modes_are_explicit(self):
        self.assertTrue(validation_plan.parse_arguments(["--execute", "--reuse"]).reuse)
        self.assertTrue(validation_plan.parse_arguments(["--execute", "--fresh"]).fresh)
        self.assertEqual(
            "test-unit",
            validation_plan.parse_arguments(["--record-workflow", "test-unit"]).record_workflow,
        )

    def test_conflicting_or_nonexecuting_options_are_errors(self):
        for arguments in (
            ["--reuse"], ["--fresh"], ["--execute", "--reuse", "--fresh"],
            ["--execute", "--reuse", "--tier", "full"],
            ["--record-workflow", "test-unit", "--execute"],
            ["--record-workflow", "test-unit", "--reuse"],
            ["--record-workflow", "test-unit", "--path", "kernel/ipc.c"],
            ["--record-workflow", "test-unit", "--tier", "pr"],
            ["--record-workflow", "unknown"],
            ["--base=--output=unexpected"],
        ):
            with self.subTest(arguments=arguments), redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    validation_plan.parse_arguments(arguments)


if __name__ == "__main__":
    unittest.main()
