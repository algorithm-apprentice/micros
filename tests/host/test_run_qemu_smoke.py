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

TRAP_RECOVERY_OUTPUT = (
    "MICROS_BOOT 0.1.0\n"
    "MICROS_TRAP_READY\n"
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
    "MICROS_TRAP_TEST_PASS "
    "origin=S cause=illegal-instruction registers=preserved\n"
)

TIMER_TEST_OUTPUT = TRAP_RECOVERY_OUTPUT.replace(
    "MICROS_TRAP_TEST_PASS "
    "origin=S cause=illegal-instruction registers=preserved\n",
    "MICROS_TIMER_TEST_PASS "
    "ticks=0x0000000000000003 "
    "interval=0x00000000000186a0\n",
)

FRAME_ALLOCATOR_READY_RECORD = (
    "MICROS_FRAME_ALLOCATOR_READY "
    "managed=0x0000000000000100 "
    "free=0x0000000000000100\n"
)

FRAME_ALLOCATOR_TEST_PASS = (
    "MICROS_FRAME_ALLOCATOR_TEST_PASS "
    "allocations=0x0000000000000004 "
    "reuse=lowest invariants=preserved\n"
)

FRAME_ALLOCATOR_OUTPUT = TRAP_RECOVERY_OUTPUT.replace(
    "MICROS_TRAP_TEST_PASS "
    "origin=S cause=illegal-instruction registers=preserved\n",
    "",
).replace(
    "MICROS_FDT_READY\n",
    "MICROS_FDT_READY\n" + FRAME_ALLOCATOR_READY_RECORD,
)

FRAME_ALLOCATOR_TEST_OUTPUT = (
    FRAME_ALLOCATOR_OUTPUT + FRAME_ALLOCATOR_TEST_PASS
)

TRAP_CONTEXT_RECORD = (
    "MICROS_TRAP_CONTEXT "
    "origin=S "
    "sstatus=0x0000000200000100 "
    "scause=0x0000000000000002 "
    "stval=0x00000000c0001073 "
    "sepc=0x0000000080202000 "
    "ra=0x0000000000000101 "
    "sp=0x0000000080205000\n"
)

TRAP_PANIC_OUTPUT = (
    PANIC_OUTPUT.replace(
        "MICROS_FDT_READY\n",
        "MICROS_TRAP_READY\nMICROS_FDT_READY\n",
    )
    .replace(
        "MICROS_PANIC reason=intentional-test\n",
        "MICROS_PANIC reason=unexpected-exception\n",
    )
    .replace(
        "file=kernel/main.c",
        "file=kernel/trap.c",
    )
    + TRAP_CONTEXT_RECORD
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

    def test_rejects_trap_context_for_direct_panic(self):
        result = run_qemu_smoke.QemuResult(
            output=PANIC_OUTPUT + TRAP_CONTEXT_RECORD,
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

    def test_accepts_complete_trap_recovery(self):
        result = run_qemu_smoke.QemuResult(
            output=TRAP_RECOVERY_OUTPUT,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_trap_test_report=True,
        )

        self.assertTrue(accepted)

    def test_rejects_duplicate_trap_recovery_pass(self):
        result = run_qemu_smoke.QemuResult(
            output=(
                TRAP_RECOVERY_OUTPUT
                + "MICROS_TRAP_TEST_PASS "
                "origin=S cause=illegal-instruction registers=preserved\n"
            ),
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_trap_test_report=True,
        )

        self.assertFalse(accepted)

    def test_rejects_out_of_order_trap_recovery_pass(self):
        out_of_order = (
            TRAP_RECOVERY_OUTPUT.replace("MICROS_TRAP_READY\n", "")
            + "MICROS_TRAP_READY\n"
        )
        result = run_qemu_smoke.QemuResult(
            output=out_of_order,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_trap_test_report=True,
        )

        self.assertFalse(accepted)

    def test_rejects_trap_pass_before_fdt_ready(self):
        pass_record = (
            "MICROS_TRAP_TEST_PASS "
            "origin=S cause=illegal-instruction registers=preserved\n"
        )
        too_early = TRAP_RECOVERY_OUTPUT.replace(pass_record, "").replace(
            "MICROS_FDT_READY\n",
            pass_record + "MICROS_FDT_READY\n",
        )
        result = run_qemu_smoke.QemuResult(
            output=too_early,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_trap_test_report=True,
        )

        self.assertFalse(accepted)

    def test_accepts_complete_timer_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=TIMER_TEST_OUTPUT,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_timer_test_report=True,
        )

        self.assertTrue(accepted)

    def test_rejects_missing_timer_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=TRAP_RECOVERY_OUTPUT,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_timer_test_report=True,
        )

        self.assertFalse(accepted)

    def test_rejects_duplicate_timer_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=(
                TIMER_TEST_OUTPUT
                + "MICROS_TIMER_TEST_PASS "
                "ticks=0x0000000000000003 "
                "interval=0x00000000000186a0\n"
            ),
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_timer_test_report=True,
        )

        self.assertFalse(accepted)

    def test_rejects_timer_test_report_before_fdt_ready(self):
        pass_record = (
            "MICROS_TIMER_TEST_PASS "
            "ticks=0x0000000000000003 "
            "interval=0x00000000000186a0\n"
        )
        too_early = TIMER_TEST_OUTPUT.replace(pass_record, "").replace(
            "MICROS_FDT_READY\n",
            pass_record + "MICROS_FDT_READY\n",
        )
        result = run_qemu_smoke.QemuResult(
            output=too_early,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_timer_test_report=True,
        )

        self.assertFalse(accepted)

    def test_rejects_malformed_timer_tick_count(self):
        malformed = TIMER_TEST_OUTPUT.replace(
            "ticks=0x0000000000000003",
            "ticks=0x3",
        )
        result = run_qemu_smoke.QemuResult(
            output=malformed,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_timer_test_report=True,
        )

        self.assertFalse(accepted)

    def test_rejects_malformed_timer_interval(self):
        malformed = TIMER_TEST_OUTPUT.replace(
            "interval=0x00000000000186a0",
            "interval=0x186a0",
        )
        result = run_qemu_smoke.QemuResult(
            output=malformed,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_timer_test_report=True,
        )

        self.assertFalse(accepted)

    def test_rejects_unterminated_timer_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=TIMER_TEST_OUTPUT.rstrip("\n"),
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_timer_test_report=True,
        )

        self.assertFalse(accepted)

    def test_accepts_complete_frame_allocator_ready_report(self):
        result = run_qemu_smoke.QemuResult(
            output=FRAME_ALLOCATOR_OUTPUT,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_frame_allocator_ready=True,
        )

        self.assertTrue(accepted)

    def test_requires_expected_frame_allocator_growth(self):
        larger_output = FRAME_ALLOCATOR_OUTPUT.replace(
            "managed=0x0000000000000100",
            "managed=0x0000000000008100",
        ).replace(
            "free=0x0000000000000100",
            "free=0x0000000000008100",
        )

        self.assertTrue(
            run_qemu_smoke.has_expected_frame_allocator_growth(
                (FRAME_ALLOCATOR_OUTPUT, larger_output),
                0x8000,
            )
        )
        self.assertFalse(
            run_qemu_smoke.has_expected_frame_allocator_growth(
                (FRAME_ALLOCATOR_OUTPUT, FRAME_ALLOCATOR_OUTPUT),
                0x8000,
            )
        )

    def test_rejects_zero_or_mismatched_frame_allocator_counts(self):
        for invalid in (
            FRAME_ALLOCATOR_OUTPUT.replace(
                "managed=0x0000000000000100",
                "managed=0x0000000000000000",
            ),
            FRAME_ALLOCATOR_OUTPUT.replace(
                "free=0x0000000000000100",
                "free=0x00000000000000ff",
            ),
        ):
            with self.subTest(output=invalid):
                result = run_qemu_smoke.QemuResult(
                    output=invalid,
                    return_code=0,
                    timed_out=False,
                )
                accepted = run_qemu_smoke.matches_expected_result(
                    result=result,
                    observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                    expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                    markers=("MICROS_FDT_READY",),
                    patterns=(),
                    require_fdt_events=True,
                    require_fdt_reservations=True,
                    require_frame_allocator_ready=True,
                )
                self.assertFalse(accepted)

    def test_rejects_duplicate_or_early_frame_allocator_ready(self):
        early = FRAME_ALLOCATOR_OUTPUT.replace(
            FRAME_ALLOCATOR_READY_RECORD,
            "",
        ).replace(
            "MICROS_FDT_READY\n",
            FRAME_ALLOCATOR_READY_RECORD + "MICROS_FDT_READY\n",
        )
        duplicate = FRAME_ALLOCATOR_OUTPUT + FRAME_ALLOCATOR_READY_RECORD

        for invalid in (early, duplicate):
            with self.subTest(output=invalid):
                result = run_qemu_smoke.QemuResult(
                    output=invalid,
                    return_code=0,
                    timed_out=False,
                )
                accepted = run_qemu_smoke.matches_expected_result(
                    result=result,
                    observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                    expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                    markers=("MICROS_FDT_READY",),
                    patterns=(),
                    require_fdt_events=True,
                    require_fdt_reservations=True,
                    require_frame_allocator_ready=True,
                )
                self.assertFalse(accepted)

    def test_rejects_malformed_or_unterminated_frame_allocator_ready(self):
        malformed = FRAME_ALLOCATOR_OUTPUT.replace(
            "managed=0x0000000000000100",
            "managed=0x100",
        )
        unterminated = FRAME_ALLOCATOR_OUTPUT.rstrip("\n")

        for invalid in (malformed, unterminated):
            with self.subTest(output=invalid):
                result = run_qemu_smoke.QemuResult(
                    output=invalid,
                    return_code=0,
                    timed_out=False,
                )
                accepted = run_qemu_smoke.matches_expected_result(
                    result=result,
                    observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                    expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                    markers=("MICROS_FDT_READY",),
                    patterns=(),
                    require_fdt_events=True,
                    require_fdt_reservations=True,
                    require_frame_allocator_ready=True,
                )
                self.assertFalse(accepted)

    def test_rejects_frame_allocator_ready_after_target_outcome(self):
        late_ready = TIMER_TEST_OUTPUT + FRAME_ALLOCATOR_READY_RECORD
        result = run_qemu_smoke.QemuResult(
            output=late_ready,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_timer_test_report=True,
            require_frame_allocator_ready=True,
        )

        self.assertFalse(accepted)

    def test_ignores_unrelated_target_prefix_before_allocator_ready(self):
        unrelated = FRAME_ALLOCATOR_OUTPUT.replace(
            "MICROS_FDT_READY\n",
            "MICROS_FDT_READY\nMICROS_TIMER_TESTING unrelated-record\n",
        )
        result = run_qemu_smoke.QemuResult(
            output=unrelated,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_frame_allocator_ready=True,
        )

        self.assertTrue(accepted)

    def test_accepts_complete_frame_allocator_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=FRAME_ALLOCATOR_TEST_OUTPUT,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_frame_allocator_test_report=True,
        )

        self.assertTrue(accepted)

    def test_rejects_missing_or_duplicate_frame_allocator_test_report(self):
        duplicate = (
            FRAME_ALLOCATOR_TEST_OUTPUT + FRAME_ALLOCATOR_TEST_PASS
        )

        for invalid in (FRAME_ALLOCATOR_OUTPUT, duplicate):
            with self.subTest(output=invalid):
                result = run_qemu_smoke.QemuResult(
                    output=invalid,
                    return_code=0,
                    timed_out=False,
                )
                accepted = run_qemu_smoke.matches_expected_result(
                    result=result,
                    observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                    expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                    markers=("MICROS_FDT_READY",),
                    patterns=(),
                    require_fdt_events=True,
                    require_fdt_reservations=True,
                    require_frame_allocator_test_report=True,
                )
                self.assertFalse(accepted)

    def test_rejects_early_or_malformed_frame_allocator_test_report(self):
        early = FRAME_ALLOCATOR_TEST_OUTPUT.replace(
            FRAME_ALLOCATOR_TEST_PASS,
            "",
        ).replace(
            FRAME_ALLOCATOR_READY_RECORD,
            FRAME_ALLOCATOR_TEST_PASS + FRAME_ALLOCATOR_READY_RECORD,
        )
        malformed = FRAME_ALLOCATOR_TEST_OUTPUT.replace(
            "allocations=0x0000000000000004",
            "allocations=0x4",
        )

        for invalid in (early, malformed):
            with self.subTest(output=invalid):
                result = run_qemu_smoke.QemuResult(
                    output=invalid,
                    return_code=0,
                    timed_out=False,
                )
                accepted = run_qemu_smoke.matches_expected_result(
                    result=result,
                    observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                    expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                    markers=("MICROS_FDT_READY",),
                    patterns=(),
                    require_fdt_events=True,
                    require_fdt_reservations=True,
                    require_frame_allocator_test_report=True,
                )
                self.assertFalse(accepted)

    def test_rejects_unterminated_frame_allocator_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=FRAME_ALLOCATOR_TEST_OUTPUT.rstrip("\n"),
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
            markers=("MICROS_FDT_READY",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_frame_allocator_test_report=True,
        )

        self.assertFalse(accepted)

    def test_accepts_complete_trap_panic_context(self):
        result = run_qemu_smoke.QemuResult(
            output=TRAP_PANIC_OUTPUT,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            markers=("MICROS_PANIC reason=unexpected-exception",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_panic_report=True,
            require_trap_context=True,
            expected_trap_context_sepc=0x0000000080202000,
        )

        self.assertTrue(accepted)

    def test_rejects_duplicate_trap_panic_context(self):
        result = run_qemu_smoke.QemuResult(
            output=TRAP_PANIC_OUTPUT + TRAP_CONTEXT_RECORD,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            markers=("MICROS_PANIC reason=unexpected-exception",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_panic_report=True,
            require_trap_context=True,
            expected_trap_context_sepc=0x0000000080202000,
        )

        self.assertFalse(accepted)

    def test_rejects_unterminated_trap_panic_context(self):
        result = run_qemu_smoke.QemuResult(
            output=TRAP_PANIC_OUTPUT.rstrip("\n"),
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            markers=("MICROS_PANIC reason=unexpected-exception",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_panic_report=True,
            require_trap_context=True,
            expected_trap_context_sepc=0x0000000080202000,
        )

        self.assertFalse(accepted)

    def test_rejects_wrong_trap_context_sepc(self):
        result = run_qemu_smoke.QemuResult(
            output=TRAP_PANIC_OUTPUT,
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            markers=("MICROS_PANIC reason=unexpected-exception",),
            patterns=(),
            require_fdt_events=True,
            require_fdt_reservations=True,
            require_panic_report=True,
            require_trap_context=True,
            expected_trap_context_sepc=0xDEADBEEFDEADBEEF,
        )

        self.assertFalse(accepted)


class NmSymbolTest(unittest.TestCase):
    def test_parses_exact_defined_symbol(self):
        address = run_qemu_smoke.parse_nm_symbol_address(
            "0000000080202000 T micros_trap_panic_test_fault\n",
            "micros_trap_panic_test_fault",
        )

        self.assertEqual(0x0000000080202000, address)

    def test_rejects_duplicate_defined_symbol(self):
        with self.assertRaises(ValueError):
            run_qemu_smoke.parse_nm_symbol_address(
                "0000000080202000 T trap_fault\n"
                "0000000080203000 t trap_fault\n",
                "trap_fault",
            )


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

    def test_accepts_explicit_guest_memory_size(self):
        command = run_qemu_smoke.build_qemu_command(
            qemu="/tools/qemu-system-riscv64",
            kernel="/build/micros.elf",
            memory="256M",
        )

        self.assertEqual("256M", command[command.index("-m") + 1])


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
