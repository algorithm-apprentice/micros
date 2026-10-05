import contextlib
import io
import unittest

from tools import run_qemu_smoke


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
