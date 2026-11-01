"""Complete authenticated producer reports, never regex-only partial success."""

from dataclasses import replace
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tools.stack_bound_types import StackEvidenceError


def record(image, owner, slot, generation, samples=3, use=512, bottom=0x10000, top=0x14000, failure=0):
    return (
        f"MICROS_STACK_OBSERVATION image={image} owner={owner} slot={slot} "
        f"generation=0x{generation:016x} samples=0x{samples:016x} use=0x{use:016x} "
        f"bottom=0x{bottom:016x} top=0x{top:016x} failure=0x{failure:016x}\n"
    )


class StackReportsTest(unittest.TestCase):
    def test_a_failed_static_graph_prevents_guest_execution(self):
        from tools.stack_bound_run import run_observed_scope

        with (
            patch("tools.stack_bound_run.capture_candidate", return_value=SimpleNamespace(clean=True)),
            patch("tools.stack_bound_run.certify_kernel_observation",
                  side_effect=StackEvidenceError("unclosed static graph")),
            patch("tools.stack_bound_run.run_qemu") as launch,
            self.assertRaisesRegex(StackEvidenceError, "unclosed static graph"),
        ):
            run_observed_scope(Path("/fixture"), Path("/fixture/observed"))
        launch.assert_not_called()

    def test_records_cannot_split_or_precede_the_complete_legacy_transcript(self):
        from tools.stack_bound_reports import legacy_transcript

        output = self.output()
        self.assertEqual("MICROS_OLD_SCENARIO_PASS\n", legacy_transcript(output))
        with self.assertRaises(StackEvidenceError):
            legacy_transcript(output + "MICROS_LATE_REQUIRED_RECORD\n")
        with self.assertRaises(StackEvidenceError):
            legacy_transcript("MICROS_OLD_SCENARIO_PASS\n")

    def producers(self):
        from tools.stack_bound_reports import Producer

        return (
            Producer("kernel", "thread", 4, 2, 0x10000, 0x14000, 1024),
            Producer("applications/child.elf", "user", 4, 1, 0x7ffff000, 0x80000000, 3072),
        )

    def output(self):
        return (
            "MICROS_OLD_SCENARIO_PASS\n"
            + record("kernel", "thread", 4, 2)
            + record("applications/child.elf", "user", 4, 1, bottom=0x7ffff000, top=0x80000000)
        )

    def test_distinct_process_and_thread_generations_bind_their_own_stack(self):
        from tools.stack_bound_reports import validate_observation_records

        records = validate_observation_records(self.output(), self.producers())
        self.assertEqual(2, len(records))
        self.assertEqual((2, 1), tuple(item.producer.generation for item in records))
        self.assertEqual((512, 512), tuple(item.use for item in records))

    def test_missing_duplicate_wrong_generation_and_ignored_malformed_rows_fail(self):
        from tools.stack_bound_reports import validate_observation_records

        output = self.output()
        for changed in (
            output.split("MICROS_STACK_OBSERVATION image=applications/")[0],
            output + record("kernel", "thread", 4, 2),
            output.replace("generation=0x0000000000000002", "generation=0x0000000000000001"),
            output + "MICROS_STACK_OBSERVATION malformed-but-previously-ignored\n",
            output.rstrip("\n"),
            output.replace("slot=4", "slot=04", 1),
        ):
            with self.subTest(output=changed), self.assertRaises(StackEvidenceError):
                validate_observation_records(changed, self.producers())

    def test_zero_samples_sticky_failure_wrong_interval_and_static_excess_fail(self):
        from tools.stack_bound_reports import validate_observation_records

        for options in (
            {"samples": 0}, {"failure": 1}, {"bottom": 0x10010},
            {"use": 1025}, {"use": 16385},
        ):
            output = record("kernel", "thread", 4, 2, **options) + self.output().splitlines(keepends=True)[-1]
            with self.subTest(options=options), self.assertRaises(StackEvidenceError):
                validate_observation_records(output, self.producers())

    def test_an_optional_idle_owner_may_be_absent_but_not_unbound(self):
        from tools.stack_bound_reports import Producer, validate_observation_records

        idle = Producer("kernel", "primary", 0, 0, 0x20000, 0x24000, 2048, required=False)
        producers = self.producers() + (idle,)
        self.assertEqual(2, len(validate_observation_records(self.output(), producers)))
        output = self.output() + record("kernel", "primary", 0, 0, bottom=0x20000, top=0x24000)
        self.assertEqual(3, len(validate_observation_records(output, producers)))
        with self.assertRaises(StackEvidenceError):
            validate_observation_records(output, self.producers())
        with self.assertRaises(StackEvidenceError):
            validate_observation_records(self.output(), producers + (replace(idle, required=True),))


if __name__ == "__main__":
    unittest.main()
