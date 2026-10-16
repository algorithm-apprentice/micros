from tools import run_qemu_smoke

from tests.host.qemu_smoke_test_fixture import (
    TRAP_RECOVERY_OUTPUT,
    TIMER_TEST_OUTPUT,
    FRAME_ALLOCATOR_READY_RECORD,
    FRAME_ALLOCATOR_TEST_PASS,
    FRAME_ALLOCATOR_OUTPUT,
    FRAME_ALLOCATOR_TEST_OUTPUT,
)


class PlatformReportCases:
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
                "origin=S cause=illegal-instruction registers=preserved "
                "hart-context=routed primary-stack=selected "
                "sscratch=anchor\n"
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
            "origin=S cause=illegal-instruction registers=preserved "
            "hart-context=routed primary-stack=selected "
            "sscratch=anchor\n"
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
                "interval=0x00000000000186a0 "
                "active=0x0000000000000000 "
                "deadline=0xffffffffffffffff owner=hart\n"
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
            "interval=0x00000000000186a0 "
            "active=0x0000000000000000 "
            "deadline=0xffffffffffffffff owner=hart\n"
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
