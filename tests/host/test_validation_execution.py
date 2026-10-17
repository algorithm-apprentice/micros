from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools import (
    validation_context,
    validation_evidence,
    validation_execution,
    validation_plan,
)


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
        self.readiness = mock.patch(
            "tools.validation_context.collect_readiness",
            side_effect=self.ready,
            create=True,
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

    def ready(self, root, name):
        return mock.Mock(
            outcome="READY",
            reason_code="ready",
            profile=self.manifest(root, name),
            preparation_argv=None,
            detail=None,
        )

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

    def test_only_the_exact_selected_documentation_command_is_canonical(self):
        validation_execution._validate_commands(
            [validation_plan.DOCS],
            WORKFLOWS,
        )
        for command in (
            ("python3.15", *validation_plan.DOCS[1:]),
            (validation_plan.DOCS[0], "-S", "-E", "-B", "tools/check_docs.py"),
            (*validation_plan.DOCS[:-1], "tools/other.py"),
        ):
            with self.subTest(command=command), self.assertRaises(
                validation_execution.ExecutionError
            ):
                validation_execution._validate_commands([command], WORKFLOWS)

    def test_exact_second_plan_reuses_all_three_workflows_but_not_docs_or_diffs(self):
        commands = [
            *(workflow(name) for name in WORKFLOWS),
            validation_plan.DOCS,
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
        self.assertNotIn("producer", report)

    def test_blocked_producer_runs_no_canonical_command_or_store(self):
        self.readiness.side_effect = lambda root, name: mock.Mock(
            outcome="PREPARE",
            reason_code="selected-output-absent",
            profile=None,
            preparation_argv=workflow(name),
            detail="selected output is absent",
        )
        commands = [
            *(workflow(name) for name in WORKFLOWS),
            validation_plan.DOCS,
        ]
        sentinel = self.root / "build/validation-evidence/v1/sentinel"
        sentinel.parent.mkdir(parents=True)
        sentinel.write_bytes(b"unchanged")
        with mock.patch(
            "tools.validation_evidence.EvidenceStore",
            side_effect=AssertionError("blocked producer opened the store"),
        ):
            status, report, output = self.execute(commands, producer=True)
        self.assertNotEqual(0, status)
        self.assertEqual([], self.calls)
        self.assertEqual(
            ["NOT_RUN"] * len(commands),
            [entry["status"] for entry in report["commands"]],
        )
        self.assertEqual(
            {
                "selected_workflows": list(WORKFLOWS),
                "readiness": [
                    {
                        "workflow": name,
                        "outcome": "PREPARE",
                        "reason_code": "selected-output-absent",
                        "preparation_argv": list(workflow(name)),
                    }
                    for name in WORKFLOWS
                ],
                "barrier_passed": False,
                "run_id": None,
                "complete": False,
                "failure_code": "readiness-blocked",
            },
            report["producer"],
        )
        self.assertFalse(report["final_candidate"])
        self.assertIn("PREPARE", output)
        self.assertEqual(b"unchanged", sentinel.read_bytes())

    def test_unsupported_producer_has_no_preparation_advice(self):
        self.readiness.side_effect = lambda root, name: mock.Mock(
            outcome="UNSUPPORTED",
            reason_code="profile-unsupported",
            profile=None,
            preparation_argv=None,
            detail="unclosed tool",
        )
        status, report, output = self.execute(
            [workflow("test-unit")],
            producer=True,
        )
        self.assertNotEqual(0, status)
        self.assertEqual([], self.calls)
        self.assertEqual(
            {
                "workflow": "test-unit",
                "outcome": "UNSUPPORTED",
                "reason_code": "profile-unsupported",
                "preparation_argv": None,
            },
            report["producer"]["readiness"][0],
        )
        self.assertNotIn("PREPARE cmake", output)

    def test_ready_producer_executes_all_commands_fresh_with_one_run_id(self):
        commands = [
            *(workflow(name) for name in WORKFLOWS),
            validation_plan.DOCS,
        ]
        status, report, _ = self.execute(commands, producer=True)
        self.assertEqual(0, status)
        self.assertEqual(commands, self.calls)
        self.assertEqual(
            ["FRESH"] * len(commands),
            [entry["status"] for entry in report["commands"]],
        )
        producer = report["producer"]
        self.assertEqual(list(WORKFLOWS), producer["selected_workflows"])
        self.assertTrue(producer["barrier_passed"])
        self.assertTrue(producer["complete"])
        self.assertIsNone(producer["failure_code"])
        self.assertRegex(producer["run_id"], r"^[0-9a-f]{32}$")
        self.assertEqual(
            {producer["run_id"]},
            {
                entry["producer"]["run_id"]
                for entry in report["commands"]
                if entry["command"][-1] in WORKFLOWS
            },
        )
        self.assertTrue(report["final_candidate"])

    def test_producer_checks_only_reusable_workflows_selected_by_the_plan(self):
        status, report, _ = self.execute(
            [
                workflow("test-unit"),
                validation_plan.DOCS,
            ],
            producer=True,
        )
        self.assertEqual(0, status)
        self.assertEqual(
            [mock.call(self.root, "test-unit")],
            self.readiness.call_args_list,
        )
        self.assertEqual(
            ["test-unit"],
            report["producer"]["selected_workflows"],
        )

    def test_candidate_mutation_during_readiness_starts_no_command(self):
        ready = self.ready

        def changed(root, name):
            result = ready(root, name)
            source = {**self.candidate.source, "tree": "d" * 40}
            self.candidate = validation_context.Candidate(
                self.candidate.repository,
                source,
                True,
            )
            return result

        self.readiness.side_effect = changed
        output = io.StringIO()
        with redirect_stdout(output), redirect_stderr(output):
            status = validation_execution.execute_plan(
                self.root,
                [workflow("test-unit")],
                WORKFLOWS,
                producer=True,
            )
        self.assertNotEqual(0, status)
        self.assertEqual([], self.calls)
        self.assertNotIn("VALIDATION_REPORT ", output.getvalue())

    def test_interrupted_partial_readiness_emits_no_report(self):
        calls = 0
        ready = self.ready

        def interrupted(root, name):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise KeyboardInterrupt()
            return ready(root, name)

        self.readiness.side_effect = interrupted
        output = io.StringIO()
        with redirect_stdout(output), redirect_stderr(output):
            status = validation_execution.execute_plan(
                self.root,
                [workflow(name) for name in WORKFLOWS],
                WORKFLOWS,
                producer=True,
            )
        self.assertEqual(130, status)
        self.assertEqual([], self.calls)
        self.assertNotIn("VALIDATION_REPORT ", output.getvalue())

    def test_interruption_after_barrier_is_plan_failed(self):
        self.run.side_effect = KeyboardInterrupt()
        status, report, _ = self.execute(
            [workflow("test-unit"), workflow("test-qemu-smoke")],
            producer=True,
        )
        self.assertEqual(130, status)
        self.assertEqual("FAILED", report["commands"][0]["status"])
        self.assertEqual("NOT_RUN", report["commands"][1]["status"])
        self.assertEqual("plan-failed", report["producer"]["failure_code"])
        self.assertTrue(report["producer"]["barrier_passed"])

    def test_producer_never_consults_existing_receipts(self):
        with mock.patch(
            "tools.validation_evidence.EvidenceStore.lookup",
            side_effect=AssertionError("producer consulted an old receipt"),
        ):
            status, report, _ = self.execute(
                [workflow("test-unit")],
                producer=True,
            )
        self.assertEqual(0, status)
        self.assertEqual("FRESH", report["commands"][0]["status"])
        self.assertTrue(report["producer"]["complete"])
        self.assertTrue(report["final_candidate"])

    def test_producer_profile_drift_fails_before_workflow_launch(self):
        snapshot = self.manifest(self.root, "test-unit")
        self.readiness.side_effect = lambda root, name: mock.Mock(
            outcome="READY",
            reason_code="ready",
            profile=snapshot,
            preparation_argv=None,
            detail=None,
        )
        self.state["context"] = "changed"
        commands = [workflow("test-unit"), workflow("test-qemu-smoke")]
        identity = {
            "repository": self.candidate.repository,
            "candidate": self.candidate.source,
            **snapshot,
        }
        with validation_evidence.EvidenceStore(self.root, WORKFLOWS) as store:
            attempt = store.start("test-unit")
            attempt.log.write(b"prior success\n")
            store.complete(attempt, identity, 0)
        receipt = (
            self.root
            / "build/validation-evidence/v1/test-unit.json"
        )
        self.assertTrue(receipt.exists())
        status, report, _ = self.execute(commands, producer=True)
        self.assertNotEqual(0, status)
        self.assertEqual([], self.calls)
        self.assertEqual(
            ["FAILED", "NOT_RUN"],
            [entry["status"] for entry in report["commands"]],
        )
        self.assertNotIn("exit_code", report["commands"][0])
        self.assertEqual(
            "profile-changed-before-launch",
            report["producer"]["failure_code"],
        )
        self.assertTrue(report["producer"]["barrier_passed"])
        self.assertFalse(report["producer"]["complete"])
        self.assertFalse(report["final_candidate"])
        self.assertTrue(receipt.exists())

    def test_producer_postflight_failure_is_plan_failed(self):
        runner = self.subprocess

        def changed(command, **options):
            result = runner(command, **options)
            self.state["context"] = "changed"
            return result

        self.run.side_effect = changed
        status, report, _ = self.execute(
            [workflow("test-unit"), workflow("test-qemu-smoke")],
            producer=True,
        )
        self.assertNotEqual(0, status)
        self.assertEqual(
            ["FAILED", "NOT_RUN"],
            [entry["status"] for entry in report["commands"]],
        )
        self.assertEqual(0, report["commands"][0]["exit_code"])
        self.assertIn("log", report["commands"][0])
        self.assertEqual("plan-failed", report["producer"]["failure_code"])
        self.assertFalse(report["final_candidate"])

    def test_producer_receipt_publication_failure_has_specific_code(self):
        with mock.patch(
            "tools.validation_evidence.EvidenceStore._publish",
            side_effect=OSError("injected receipt publication failure"),
        ):
            status, report, _ = self.execute(
                [workflow("test-unit"), workflow("test-qemu-smoke")],
                producer=True,
            )
        self.assertNotEqual(0, status)
        self.assertEqual(
            ["FAILED", "NOT_RUN"],
            [entry["status"] for entry in report["commands"]],
        )
        first = report["commands"][0]
        self.assertEqual(0, first["exit_code"])
        self.assertIn("log", first)
        self.assertNotIn("candidate", first)
        self.assertEqual(
            "receipt-not-published",
            report["producer"]["failure_code"],
        )
        self.assertFalse(report["final_candidate"])

    def test_producer_log_finalization_failure_is_plan_failed(self):
        with mock.patch(
            "tools.validation_evidence.EvidenceStore._finish_log",
            side_effect=OSError("injected log finalization failure"),
        ):
            status, report, _ = self.execute(
                [workflow("test-unit"), workflow("test-qemu-smoke")],
                producer=True,
            )
        self.assertNotEqual(0, status)
        self.assertEqual(
            ["FAILED", "NOT_RUN"],
            [entry["status"] for entry in report["commands"]],
        )
        self.assertEqual(0, report["commands"][0]["exit_code"])
        self.assertNotIn("log", report["commands"][0])
        self.assertEqual("plan-failed", report["producer"]["failure_code"])

    def test_producer_store_lock_failure_starts_no_canonical_command(self):
        with mock.patch(
            "tools.validation_evidence.EvidenceStore.__enter__",
            side_effect=validation_evidence.EvidenceError("store busy"),
        ):
            status, report, _ = self.execute(
                [workflow("test-unit")],
                producer=True,
            )
        self.assertNotEqual(0, status)
        self.assertEqual([], self.calls)
        self.assertEqual("NOT_RUN", report["commands"][0]["status"])
        self.assertEqual("plan-failed", report["producer"]["failure_code"])
        self.assertTrue(report["producer"]["barrier_passed"])
        self.assertIsNone(report["producer"]["run_id"])

    def test_producer_cleanup_failure_cannot_retain_complete_success(self):
        cleanup = validation_evidence.EvidenceStore.__exit__

        def interrupted(store, *arguments):
            cleanup(store, *arguments)
            raise OSError("injected cleanup failure")

        with mock.patch(
            "tools.validation_evidence.EvidenceStore.__exit__",
            new=interrupted,
        ):
            status, report, _ = self.execute(
                [workflow("test-unit")],
                producer=True,
            )
        self.assertNotEqual(0, status)
        self.assertFalse(report["producer"]["complete"])
        self.assertEqual("plan-failed", report["producer"]["failure_code"])
        self.assertFalse(report["final_candidate"])

    def test_producer_subprocess_failure_is_plan_failed(self):
        self.state["exit_code"] = 7
        status, report, _ = self.execute(
            [workflow("test-unit"), workflow("test-qemu-smoke")],
            producer=True,
        )
        self.assertNotEqual(0, status)
        self.assertEqual(
            ["FAILED", "NOT_RUN"],
            [entry["status"] for entry in report["commands"]],
        )
        self.assertEqual(7, report["commands"][0]["exit_code"])
        self.assertIn("log", report["commands"][0])
        self.assertEqual("plan-failed", report["producer"]["failure_code"])

    def test_producer_requires_at_least_one_selected_reusable_workflow(self):
        with self.assertRaisesRegex(
            validation_execution.ExecutionError,
            "reusable workflow",
        ):
            validation_execution.execute_plan(
                self.root,
                [validation_plan.DOCS],
                WORKFLOWS,
                producer=True,
            )
        self.assertEqual([], self.calls)

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
            ([validation_plan.DOCS], {"gate_only": True}),
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

    def test_execution_cannot_replace_the_expected_planning_candidate(self):
        expected = self.candidate
        self.candidate = validation_context.Candidate(
            expected.repository, {**expected.source, "tree": "d" * 40}, True,
        )
        status, report, _ = self.execute(
            [workflow("test-unit")], expected_source=expected,
        )
        self.assertNotEqual(0, status)
        self.assertEqual("NOT_RUN", report["commands"][0]["status"])
        self.assertEqual([], self.calls)

    def test_producer_candidate_race_emits_no_report(self):
        expected = self.candidate
        self.candidate = validation_context.Candidate(
            expected.repository,
            {**expected.source, "tree": "d" * 40},
            True,
        )
        output = io.StringIO()
        with redirect_stdout(output), redirect_stderr(output):
            status = validation_execution.execute_plan(
                self.root,
                [workflow("test-unit")],
                WORKFLOWS,
                producer=True,
                expected_source=expected,
            )
        self.assertNotEqual(0, status)
        self.assertEqual([], self.calls)
        self.assertNotIn("VALIDATION_REPORT ", output.getvalue())


class ValidationExecutionArgumentsTests(unittest.TestCase):
    def test_supported_modes_are_explicit(self):
        self.assertTrue(validation_plan.parse_arguments(["--execute", "--reuse"]).reuse)
        self.assertTrue(validation_plan.parse_arguments(["--execute", "--fresh"]).fresh)
        self.assertTrue(
            validation_plan.parse_arguments(["--execute", "--producer"]).producer
        )
        self.assertEqual(
            "test-unit",
            validation_plan.parse_arguments(["--record-workflow", "test-unit"]).record_workflow,
        )

    def test_conflicting_or_nonexecuting_options_are_errors(self):
        for arguments in (
            ["--reuse"], ["--fresh"], ["--producer"],
            ["--execute", "--reuse", "--fresh"],
            ["--execute", "--reuse", "--producer"],
            ["--execute", "--fresh", "--producer"],
            ["--execute", "--reuse", "--tier", "full"],
            ["--record-workflow", "test-unit", "--execute"],
            ["--record-workflow", "test-unit", "--producer"],
            ["--record-workflow", "test-unit", "--reuse"],
            ["--record-workflow", "test-unit", "--path", "kernel/ipc.c"],
            ["--record-workflow", "test-unit", "--tier", "pr"],
            ["--record-workflow", "unknown"],
            ["--base=--output=unexpected"],
        ):
            with self.subTest(arguments=arguments), redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    validation_plan.parse_arguments(arguments)


class ValidationPlanningIdentityTests(unittest.TestCase):
    def setUp(self):
        self.root = "/repo"
        self.before = validation_context.Candidate(
            {"root": self.root, "git_common": "/repo/.git"},
            {"head": "a" * 40, "tree": "b" * 40, "source_sha256": "c" * 64}, True,
        )
        self.current = self.before
        self.addCleanup(mock.patch.stopall)
        mock.patch("tools.validation_plan.planner_root", return_value=self.root).start()
        mock.patch("tools.validation_plan.validate_inventory").start()
        mock.patch("tools.validation_plan.ensure_execution_ready").start()
        self.capture = mock.patch(
            "tools.validation_context.capture_candidate",
            side_effect=lambda root: self.current,
        ).start()
        self.discover = mock.patch(
            "tools.validation_plan.changed_paths", return_value=["docs/readme.md"]
        ).start()
        self.execute = mock.patch(
            "tools.validation_execution.execute_plan", return_value=0
        ).start()

    def test_staged_code_change_after_discovery_cannot_keep_a_docs_only_plan(self):
        def changed(base, root):
            self.current = validation_context.Candidate(
                self.before.repository,
                {**self.before.source, "tree": "d" * 40}, True,
            )
            return ["docs/readme.md"]

        self.discover.side_effect = changed
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                validation_plan.main(["--tier", "pr", "--execute"])
        self.execute.assert_not_called()

    def test_execution_receives_the_candidate_captured_before_discovery(self):
        validation_plan.main(["--tier", "pr", "--execute"])
        self.assertEqual(self.before, self.execute.call_args.kwargs["expected_source"])

    def test_producer_mode_is_forwarded_only_for_a_clean_candidate(self):
        validation_plan.main(["--tier", "pr", "--execute", "--producer"])
        self.assertTrue(self.execute.call_args.kwargs["producer"])
        self.execute.reset_mock()
        self.current = validation_context.Candidate(
            self.before.repository,
            self.before.source,
            False,
        )
        with redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                validation_plan.main(
                    ["--tier", "pr", "--execute", "--producer"]
                )
        self.execute.assert_not_called()

    def test_plan_only_remains_nonexecuting_and_does_not_capture_write_tree(self):
        with redirect_stdout(io.StringIO()):
            self.assertEqual(0, validation_plan.main(["--tier", "pr"]))
        self.capture.assert_not_called()
        self.execute.assert_not_called()


if __name__ == "__main__":
    unittest.main()
