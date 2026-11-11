import contextlib
import io
from pathlib import Path
import unittest
from unittest import mock

from tools import qemu_smoke_arguments, qemu_smoke_cli
from tools.qemu_smoke_records import QemuResult
from tests.host.build_source_records import cmake_command


ROOT = Path(__file__).resolve().parents[2]
PASS_OUTPUT = "MICROS_TRANSCRIPT_OBSERVATION value=17\nMICROS_TRANSCRIPT_TEST_PASS\n"
PASS_TAP = (
    "TAP version 13\nok 1 - QEMU smoke test observed expected pass\n"
    "# outcome: pass\n# expected outcome: pass\n"
)


class QemuTranscriptTest(unittest.TestCase):
    def invoke(self, result, *extra):
        output = io.StringIO()
        with (
            mock.patch.object(qemu_smoke_cli, "run_qemu", return_value=result) as execute,
            contextlib.redirect_stdout(output),
        ):
            status = qemu_smoke_cli.main([
                "--kernel", str(Path(__file__).resolve()),
                "--marker", "MICROS_TRANSCRIPT_TEST_PASS", *extra,
            ])
        return status, output.getvalue(), execute.call_args_list

    def test_requested_success_reports_the_single_owned_result(self):
        arguments = qemu_smoke_arguments.parse_arguments([
            "--kernel", str(Path(__file__).resolve()),
            "--marker", "MICROS_TRANSCRIPT_TEST_PASS",
        ])
        arguments.report_transcript = True
        result = QemuResult(
            output=(
                "MICROS_TRANSCRIPT_OBSERVATION value=17\n"
                "MICROS_TRANSCRIPT_TEST_PASS\n"
            ),
            return_code=0,
            timed_out=False,
        )
        output = io.StringIO()
        with (
            mock.patch.object(qemu_smoke_cli, "parse_arguments", return_value=arguments),
            mock.patch.object(qemu_smoke_cli, "run_qemu", return_value=result) as execute,
            contextlib.redirect_stdout(output),
        ):
            status = qemu_smoke_cli.main([])
        self.assertEqual(0, status)
        execute.assert_called_once()
        self.assertIn("ok 1 - QEMU smoke test observed expected pass\n", output.getvalue())
        self.assertIn("# MICROS_TRANSCRIPT_OBSERVATION value=17\n", output.getvalue())

    def test_real_option_keeps_default_output_and_exact_execution_arguments(self):
        result = QemuResult(output=PASS_OUTPUT, return_code=0, timed_out=False)
        default = self.invoke(result)
        reported = self.invoke(result, "--report-transcript")
        self.assertEqual((0, PASS_TAP), default[:2])
        self.assertEqual(
            (0, PASS_TAP + "# MICROS_TRANSCRIPT_OBSERVATION value=17\n# MICROS_TRANSCRIPT_TEST_PASS\n"),
            reported[:2],
        )
        self.assertEqual(1, len(default[2]))
        self.assertEqual(default[2], reported[2])

    def test_accepted_expected_panic_reports_without_reclassification(self):
        result = QemuResult(
            output="MICROS_PANIC fixture=expected\n" + PASS_OUTPUT,
            return_code=0, timed_out=False,
        )
        status, output, calls = self.invoke(result, "--expect", "panic", "--report-transcript")
        self.assertEqual(0, status)
        self.assertEqual(1, len(calls))
        self.assertIn("# outcome: panic\n# expected outcome: panic\n# MICROS_PANIC fixture=expected\n", output)
        self.assertEqual(1, output.count("# MICROS_TRANSCRIPT_OBSERVATION value=17\n"))

    def test_rejected_runs_keep_original_output_without_duplicates(self):
        cases = (
            (QemuResult(PASS_OUTPUT + "MICROS_TEST_FAILURE\n", 0, False), (), 1),
            (QemuResult("partial diagnostic\n", None, True), (), 4),
            (QemuResult("MICROS_PANIC\n" + PASS_OUTPUT, 0, False), (), 2),
            (QemuResult("missing required marker\n", 0, False), (), 3),
            (QemuResult(PASS_OUTPUT, 0, False), ("--serial-input-trigger", "READY", "--serial-input-hex", "61"), 3),
        )
        for result, extra, expected in cases:
            with self.subTest(output=result.output, extra=extra):
                default = self.invoke(result, *extra)
                reported = self.invoke(result, *extra, "--report-transcript")
                self.assertEqual(expected, reported[0])
                self.assertEqual(default, reported)
                self.assertEqual(1, len(reported[2]))

    def test_decoded_whitespace_and_incomplete_final_line_are_not_normalized(self):
        from tools.qemu_smoke_outcome import print_transcript

        for text, expected in (
            ("", ""),
            ("\n", "# \n"),
            (" first \r\n\r\nlast\t", "#  first \r\n# \r\n# last\t"),
        ):
            output = io.StringIO()
            with self.subTest(text=text), contextlib.redirect_stdout(output):
                print_transcript(text)
            self.assertEqual(expected, output.getvalue())
        result = QemuResult(output=PASS_OUTPUT + "partial", return_code=0, timed_out=False)
        status, output, calls = self.invoke(result, "--report-transcript")
        self.assertEqual(0, status)
        self.assertEqual(1, len(calls))
        self.assertTrue(output.endswith("# partial"))

    def test_multiple_memory_reporting_rejects_before_execution_after_old_errors(self):
        for extra, message in (
            ((), "--report-transcript requires exactly one memory size"),
            (("--timeout", "0"), "--timeout must be greater than zero"),
        ):
            error = io.StringIO()
            with (
                self.subTest(extra=extra),
                mock.patch.object(qemu_smoke_cli, "run_qemu") as execute,
                contextlib.redirect_stderr(error),
                self.assertRaises(SystemExit) as stopped,
            ):
                qemu_smoke_cli.main([
                    "--kernel", str(Path(__file__).resolve()), "--marker", "PASS",
                    "--memory", "128M", "--memory", "256M", "--report-transcript", *extra,
                ])
            self.assertEqual(2, stopped.exception.code)
            self.assertIn(message, error.getvalue())
            execute.assert_not_called()
        status, output, calls = self.invoke(
            QemuResult(PASS_OUTPUT, 0, False), "--memory", "128M", "--memory", "256M",
        )
        self.assertEqual(0, status)
        self.assertEqual(2, len(calls))
        self.assertIn("expected pass for every memory size\n", output)
        self.assertNotIn("# MICROS_TRANSCRIPT_OBSERVATION", output)


class QemuTranscriptRecipeTest(unittest.TestCase):
    def test_current_semantic_workflows_publish_their_owned_transcript(self):
        source = (ROOT / "CMakeLists.txt").read_text()
        for target in (
            "test-qemu-user-address-space", "test-qemu-address-space-handoff", "test-qemu-scheduler",
        ):
            with self.subTest(target=target):
                command = cmake_command(source, "add_custom_target", target)
                self.assertEqual(1, command.count("--report-transcript"))
