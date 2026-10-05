import contextlib
import io
import unittest

from tools import run_qemu_smoke


PANIC_OUTPUT = (
    "MICROS_BOOT 0.1.0\n"
    "MICROS_FDT_MEMORY "
    "base=0x0000000080000000 "
    "size=0x0000000008000000\n"
    "MICROS_FDT_RESERVED_MEMORY "
    "base=0x0000000080000000 "
    "size=0x0000000000040000\n"
    "MICROS_FDT_COUNTS "
    "memory=0x0000000000000001 "
    "reservation=0x0000000000000000 "
    "reserved-memory=0x0000000000000001\n"
    "MICROS_FDT_READY\n"
    "MICROS_PANIC reason=intentional-test\n"
    "MICROS_PANIC_BUILD version=0.1.0\n"
    "MICROS_PANIC_SOURCE "
    "file=kernel/main.c line=0x0000000000000042\n"
    "MICROS_PANIC_HART mode=S id=0x0000000000000000\n"
    "MICROS_PANIC_MACHINE "
    "sstatus=0x0000000200000000 "
    "scause=0x0000000000000000 "
    "stval=0x0000000000000000 "
    "sepc=0x0000000000000000 "
    "ra=0x0000000080201234 "
    "sp=0x0000000080204000\n"
)


class SmokeClassificationTest(unittest.TestCase):
    def test_accepts_marker_followed_by_clean_exit(self):
        outcome = run_qemu_smoke.classify_smoke(
            output="OpenSBI\nMICROS_BOOT 0.1.0\n",
            return_code=0,
            timed_out=False,
            markers=("MICROS_BOOT 0.1.0",),
        )

        self.assertEqual(run_qemu_smoke.SmokeOutcome.PASS, outcome)

    def test_reports_explicit_target_failure(self):
        outcome = run_qemu_smoke.classify_smoke(
            output="MICROS_TEST_FAILURE boot invariant\n",
            return_code=None,
            timed_out=True,
            markers=("MICROS_BOOT 0.1.0",),
        )

        self.assertEqual(run_qemu_smoke.SmokeOutcome.FAILURE, outcome)

    def test_reports_kernel_panic(self):
        outcome = run_qemu_smoke.classify_smoke(
            output="MICROS_PANIC hart=0\n",
            return_code=None,
            timed_out=True,
            markers=("MICROS_BOOT 0.1.0",),
        )

        self.assertEqual(run_qemu_smoke.SmokeOutcome.PANIC, outcome)

    def test_reports_unexpected_exit_without_marker(self):
        outcome = run_qemu_smoke.classify_smoke(
            output="OpenSBI only\n",
            return_code=0,
            timed_out=False,
            markers=("MICROS_BOOT 0.1.0",),
        )

        self.assertEqual(run_qemu_smoke.SmokeOutcome.UNEXPECTED_EXIT, outcome)

    def test_rejects_marker_embedded_in_another_line(self):
        outcome = run_qemu_smoke.classify_smoke(
            output="prefix MICROS_BOOT 0.1.0 suffix\n",
            return_code=0,
            timed_out=False,
            markers=("MICROS_BOOT 0.1.0",),
        )

        self.assertEqual(run_qemu_smoke.SmokeOutcome.UNEXPECTED_EXIT, outcome)

    def test_reports_timeout_before_other_output_states(self):
        outcome = run_qemu_smoke.classify_smoke(
            output="MICROS_BOOT 0.1.0\n",
            return_code=None,
            timed_out=True,
            markers=("MICROS_BOOT 0.1.0",),
        )

        self.assertEqual(run_qemu_smoke.SmokeOutcome.TIMEOUT, outcome)

    def test_requires_every_expected_marker(self):
        outcome = run_qemu_smoke.classify_smoke(
            output="MICROS_BOOT 0.1.0\n",
            return_code=0,
            timed_out=False,
            markers=(
                "MICROS_BOOT 0.1.0",
                "MICROS_FDT_READY",
            ),
        )

        self.assertEqual(run_qemu_smoke.SmokeOutcome.UNEXPECTED_EXIT, outcome)

    def test_accepts_complete_fdt_event_stream(self):
        outcome = run_qemu_smoke.classify_smoke(
            output=(
                "MICROS_BOOT 0.1.0\n"
                "MICROS_FDT_MEMORY "
                "base=0x0000000000000000 "
                "size=0x0000000000000001\n"
                "MICROS_FDT_RESERVED_MEMORY "
                "base=0x0000000000000000 "
                "size=0x0000000000000001\n"
                "MICROS_FDT_RESERVED_MEMORY "
                "base=0x0000000000000001 "
                "size=0x0000000000000001\n"
                "MICROS_FDT_COUNTS "
                "memory=0x0000000000000001 "
                "reservation=0x0000000000000000 "
                "reserved-memory=0x0000000000000002\n"
                "MICROS_FDT_READY\n"
            ),
            return_code=0,
            timed_out=False,
            markers=("MICROS_BOOT 0.1.0", "MICROS_FDT_READY"),
            require_fdt_events=True,
            require_fdt_reservations=True,
        )

        self.assertEqual(run_qemu_smoke.SmokeOutcome.PASS, outcome)

    def test_rejects_incomplete_fdt_event_stream(self):
        outcome = run_qemu_smoke.classify_smoke(
            output=(
                "MICROS_BOOT 0.1.0\n"
                "MICROS_FDT_MEMORY "
                "base=0x0000000000000000 "
                "size=0x0000000000000001\n"
                "MICROS_FDT_RESERVED_MEMORY "
                "base=0x0000000000000000 "
                "size=0x0000000000000001\n"
                "MICROS_FDT_COUNTS "
                "memory=0x0000000000000001 "
                "reservation=0x0000000000000000 "
                "reserved-memory=0x0000000000000002\n"
                "MICROS_FDT_READY\n"
            ),
            return_code=0,
            timed_out=False,
            markers=("MICROS_BOOT 0.1.0", "MICROS_FDT_READY"),
            require_fdt_events=True,
        )

        self.assertEqual(run_qemu_smoke.SmokeOutcome.UNEXPECTED_EXIT, outcome)

    def test_rejects_empty_fdt_reservation_result(self):
        outcome = run_qemu_smoke.classify_smoke(
            output=(
                "MICROS_BOOT 0.1.0\n"
                "MICROS_FDT_MEMORY "
                "base=0x0000000000000000 "
                "size=0x0000000000000001\n"
                "MICROS_FDT_COUNTS "
                "memory=0x0000000000000001 "
                "reservation=0x0000000000000000 "
                "reserved-memory=0x0000000000000000\n"
                "MICROS_FDT_READY\n"
            ),
            return_code=0,
            timed_out=False,
            markers=("MICROS_BOOT 0.1.0", "MICROS_FDT_READY"),
            require_fdt_events=True,
            require_fdt_reservations=True,
        )

        self.assertEqual(run_qemu_smoke.SmokeOutcome.UNEXPECTED_EXIT, outcome)

    def test_rejects_malformed_fdt_range_event(self):
        outcome = run_qemu_smoke.classify_smoke(
            output=(
                "MICROS_BOOT 0.1.0\n"
                "MICROS_FDT_MEMORY base=0x0 size=0x1\n"
                "MICROS_FDT_COUNTS "
                "memory=0x0000000000000001 "
                "reservation=0x0000000000000000 "
                "reserved-memory=0x0000000000000000\n"
                "MICROS_FDT_READY\n"
            ),
            return_code=0,
            timed_out=False,
            markers=("MICROS_BOOT 0.1.0", "MICROS_FDT_READY"),
            require_fdt_events=True,
        )

        self.assertEqual(run_qemu_smoke.SmokeOutcome.UNEXPECTED_EXIT, outcome)

    def test_rejects_malformed_duplicate_fdt_count_summary(self):
        outcome = run_qemu_smoke.classify_smoke(
            output=(
                "MICROS_BOOT 0.1.0\n"
                "MICROS_FDT_MEMORY "
                "base=0x0000000000000000 "
                "size=0x0000000000000001\n"
                "MICROS_FDT_COUNTS "
                "memory=0x0000000000000001 "
                "reservation=0x0000000000000000 "
                "reserved-memory=0x0000000000000000\n"
                "MICROS_FDT_COUNTS memory=1\n"
                "MICROS_FDT_READY\n"
            ),
            return_code=0,
            timed_out=False,
            markers=("MICROS_BOOT 0.1.0", "MICROS_FDT_READY"),
            require_fdt_events=True,
        )

        self.assertEqual(run_qemu_smoke.SmokeOutcome.UNEXPECTED_EXIT, outcome)


class ExpectedOutcomeTest(unittest.TestCase):
    def test_accepts_complete_clean_panic(self):
        result = run_qemu_smoke.QemuResult(
            output=PANIC_OUTPUT,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            markers=(
                "MICROS_FDT_READY",
                "MICROS_PANIC reason=intentional-test",
                "MICROS_PANIC_BUILD version=0.1.0",
            ),
            patterns=(
                r"MICROS_PANIC_SOURCE "
                r"file=kernel/main[.]c line=0x[0-9a-f]{16}",
            ),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_panic_report=True,
        )

        self.assertTrue(accepted)

    def test_rejects_panic_that_times_out(self):
        result = run_qemu_smoke.QemuResult(
            output=PANIC_OUTPUT,
            return_code=None,
            timed_out=True,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            markers=("MICROS_PANIC reason=intentional-test",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_panic_report=True,
        )

        self.assertFalse(accepted)

    def test_rejects_duplicate_or_out_of_order_panic_records(self):
        duplicated = PANIC_OUTPUT.replace(
            "MICROS_PANIC_BUILD version=0.1.0\n",
            "MICROS_PANIC_BUILD version=0.1.0\n"
            "MICROS_PANIC reason=intentional-test\n",
        )
        result = run_qemu_smoke.QemuResult(
            output=duplicated,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            markers=("MICROS_PANIC reason=intentional-test",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_panic_report=True,
        )

        self.assertFalse(accepted)

    def test_rejects_panic_with_explicit_failure(self):
        result = run_qemu_smoke.QemuResult(
            output=(
                PANIC_OUTPUT
                + "MICROS_TEST_FAILURE sbi-system-reset-returned\n"
            ),
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            markers=("MICROS_PANIC reason=intentional-test",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_panic_report=True,
        )

        self.assertFalse(accepted)

    def test_rejects_unterminated_final_panic_record(self):
        result = run_qemu_smoke.QemuResult(
            output=PANIC_OUTPUT.rstrip("\n"),
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            markers=("MICROS_PANIC reason=intentional-test",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_panic_report=True,
        )

        self.assertFalse(accepted)


class QemuCommandTest(unittest.TestCase):
    def test_uses_explicit_single_hart_plic_machine(self):
        command = run_qemu_smoke.build_qemu_command(
            qemu="/tools/qemu-system-riscv64",
            kernel="/build/micros.elf",
        )

        self.assertEqual(
            [
                "/tools/qemu-system-riscv64",
                "-machine",
                "virt,aia=none",
                "-cpu",
                "rv64",
                "-smp",
                "1",
                "-m",
                "128M",
                "-display",
                "none",
                "-monitor",
                "none",
                "-serial",
                "stdio",
                "-nic",
                "none",
                "-bios",
                "default",
                "-kernel",
                "/build/micros.elf",
                "-no-reboot",
            ],
            command,
        )


class TapOutputTest(unittest.TestCase):
    def test_pass_includes_machine_readable_outcome(self):
        result = run_qemu_smoke.QemuResult(
            output="MICROS_BOOT 0.1.0\n",
            return_code=0,
            timed_out=False,
        )
        output = io.StringIO()

        with contextlib.redirect_stdout(output):
            exit_code = run_qemu_smoke.print_tap_result(
                run_qemu_smoke.SmokeOutcome.PASS,
                result,
                ["qemu-system-riscv64"],
            )

        self.assertEqual(0, exit_code)
        self.assertIn("# outcome: pass\n", output.getvalue())


if __name__ == "__main__":
    unittest.main()
