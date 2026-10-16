from tools import run_qemu_smoke

from tests.host.qemu_smoke_test_fixture import (
    FRAME_ALLOCATOR_READY_RECORD,
    MMU_READY_RECORD,
    MMU_TEST_PASS,
    FRAME_OWNERSHIP_READY_RECORD,
    FRAME_OWNERSHIP_TEST_PASS,
    USER_ADDRESS_SPACE_TEST_PASS,
    USER_EXECUTION_TEST_PASS,
    FRAME_ALLOCATOR_TEST_OUTPUT,
    MMU_OUTPUT,
    MMU_TEST_OUTPUT,
    FRAME_OWNERSHIP_OUTPUT,
    FRAME_OWNERSHIP_TEST_OUTPUT,
    USER_ADDRESS_SPACE_TEST_OUTPUT,
    USER_EXECUTION_TEST_OUTPUT,
)


class MemoryReportCases:
    def test_accepts_complete_mmu_ready_report(self):
        result = run_qemu_smoke.QemuResult(
            output=MMU_OUTPUT,
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
            require_mmu_ready=True,
        )

        self.assertTrue(accepted)

    def test_rejects_invalid_mmu_ready_report(self):
        invalid_outputs = (
            MMU_OUTPUT.replace(
                "root=0x0000000080216000",
                "root=0x0000000000000000",
            ),
            MMU_OUTPUT.replace(
                "root=0x0000000080216000",
                "root=0x0000000080216001",
            ),
            MMU_OUTPUT.replace(
                "tables=0x0000000000000042",
                "tables=0x0000000000000000",
            ),
            MMU_OUTPUT.replace(
                "tables=0x0000000000000042",
                "tables=0x42",
            ),
            MMU_OUTPUT.rstrip("\n"),
        )

        for invalid in invalid_outputs:
            with self.subTest(output=invalid):
                result = run_qemu_smoke.QemuResult(
                    output=invalid,
                    return_code=0,
                    timed_out=False,
                )
                self.assertFalse(
                    run_qemu_smoke.matches_expected_result(
                        result=result,
                        observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                        expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                        markers=("MICROS_FDT_READY",),
                        patterns=(),
                        require_fdt_events=True,
                        require_fdt_reservations=True,
                        require_frame_allocator_ready=True,
                        require_mmu_ready=True,
                    )
                )

    def test_rejects_duplicate_or_early_mmu_ready_report(self):
        early = MMU_OUTPUT.replace(MMU_READY_RECORD, "").replace(
            FRAME_ALLOCATOR_READY_RECORD,
            MMU_READY_RECORD + FRAME_ALLOCATOR_READY_RECORD,
        )
        duplicate = MMU_OUTPUT + MMU_READY_RECORD

        for invalid in (early, duplicate):
            with self.subTest(output=invalid):
                result = run_qemu_smoke.QemuResult(
                    output=invalid,
                    return_code=0,
                    timed_out=False,
                )
                self.assertFalse(
                    run_qemu_smoke.matches_expected_result(
                        result=result,
                        observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                        expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                        markers=("MICROS_FDT_READY",),
                        patterns=(),
                        require_fdt_events=True,
                        require_fdt_reservations=True,
                        require_frame_allocator_ready=True,
                        require_mmu_ready=True,
                    )
                )

    def test_rejects_mmu_ready_after_target_outcome(self):
        late = FRAME_ALLOCATOR_TEST_OUTPUT + MMU_READY_RECORD
        result = run_qemu_smoke.QemuResult(
            output=late,
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
            require_frame_allocator_test_report=True,
            require_mmu_ready=True,
        )

        self.assertFalse(accepted)

    def test_accepts_complete_mmu_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=MMU_TEST_OUTPUT,
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
            require_mmu_ready=True,
            require_mmu_test_report=True,
        )

        self.assertTrue(accepted)

    def test_rejects_invalid_mmu_test_report(self):
        early = MMU_TEST_OUTPUT.replace(MMU_TEST_PASS, "").replace(
            MMU_READY_RECORD,
            MMU_TEST_PASS + MMU_READY_RECORD,
        )
        invalid_outputs = (
            MMU_OUTPUT,
            MMU_TEST_OUTPUT + MMU_TEST_PASS,
            early,
            MMU_TEST_OUTPUT.replace(
                "traps=0x0000000000000002",
                "traps=0x2",
            ),
            MMU_TEST_OUTPUT.rstrip("\n"),
        )

        for invalid in invalid_outputs:
            with self.subTest(output=invalid):
                result = run_qemu_smoke.QemuResult(
                    output=invalid,
                    return_code=0,
                    timed_out=False,
                )
                self.assertFalse(
                    run_qemu_smoke.matches_expected_result(
                        result=result,
                        observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                        expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                        markers=("MICROS_FDT_READY",),
                        patterns=(),
                        require_fdt_events=True,
                        require_fdt_reservations=True,
                        require_frame_allocator_ready=True,
                        require_mmu_ready=True,
                        require_mmu_test_report=True,
                    )
                )

    def test_accepts_complete_frame_ownership_ready_report(self):
        result = run_qemu_smoke.QemuResult(
            output=FRAME_OWNERSHIP_OUTPUT,
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
            require_mmu_ready=True,
            require_frame_ownership_ready=True,
        )

        self.assertTrue(accepted)

    def test_rejects_invalid_frame_ownership_ready_report(self):
        early = FRAME_OWNERSHIP_OUTPUT.replace(
            FRAME_OWNERSHIP_READY_RECORD,
            "",
        ).replace(
            MMU_READY_RECORD,
            FRAME_OWNERSHIP_READY_RECORD + MMU_READY_RECORD,
        )
        invalid_outputs = (
            MMU_OUTPUT,
            early,
            FRAME_OWNERSHIP_OUTPUT + FRAME_OWNERSHIP_READY_RECORD,
            FRAME_OWNERSHIP_OUTPUT.replace(
                "owned=0x0000000000000042",
                "owned=0x0000000000000000",
            ),
            FRAME_OWNERSHIP_OUTPUT.replace(
                "kernel-tables=0x0000000000000042",
                "kernel-tables=0x0000000000000041",
            ),
            FRAME_OWNERSHIP_OUTPUT.replace(
                "owned=0x0000000000000042",
                "owned=0x0000000000000043",
            ).replace(
                "kernel-tables=0x0000000000000042",
                "kernel-tables=0x0000000000000043",
            ),
            FRAME_OWNERSHIP_OUTPUT.replace(
                "owned=0x0000000000000042",
                "owned=0x42",
            ),
            FRAME_OWNERSHIP_OUTPUT.replace(
                "phase=bootstrap",
                "phase=handed-off",
            ),
            FRAME_OWNERSHIP_OUTPUT.rstrip("\n"),
        )

        for invalid in invalid_outputs:
            with self.subTest(output=invalid):
                result = run_qemu_smoke.QemuResult(
                    output=invalid,
                    return_code=0,
                    timed_out=False,
                )
                self.assertFalse(
                    run_qemu_smoke.matches_expected_result(
                        result=result,
                        observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                        expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                        markers=("MICROS_FDT_READY",),
                        patterns=(),
                        require_fdt_events=True,
                        require_fdt_reservations=True,
                        require_frame_allocator_ready=True,
                        require_mmu_ready=True,
                        require_frame_ownership_ready=True,
                    )
                )

    def test_rejects_frame_ownership_ready_after_target_outcome(self):
        late = MMU_TEST_OUTPUT + FRAME_OWNERSHIP_READY_RECORD
        result = run_qemu_smoke.QemuResult(
            output=late,
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
            require_mmu_ready=True,
            require_mmu_test_report=True,
            require_frame_ownership_ready=True,
        )

        self.assertFalse(accepted)

    def test_accepts_complete_frame_ownership_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=FRAME_OWNERSHIP_TEST_OUTPUT,
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
            require_mmu_ready=True,
            require_frame_ownership_ready=True,
            require_frame_ownership_test_report=True,
        )

        self.assertTrue(accepted)

    def test_rejects_invalid_frame_ownership_test_report(self):
        early = FRAME_OWNERSHIP_TEST_OUTPUT.replace(
            FRAME_OWNERSHIP_TEST_PASS,
            "",
        ).replace(
            FRAME_OWNERSHIP_READY_RECORD,
            FRAME_OWNERSHIP_TEST_PASS + FRAME_OWNERSHIP_READY_RECORD,
        )
        invalid_outputs = (
            FRAME_OWNERSHIP_OUTPUT,
            early,
            FRAME_OWNERSHIP_TEST_OUTPUT + FRAME_OWNERSHIP_TEST_PASS,
            FRAME_OWNERSHIP_TEST_OUTPUT.replace(
                "handoff=atomic",
                "handoff=partial",
            ),
            FRAME_OWNERSHIP_TEST_OUTPUT.rstrip("\n"),
        )

        for invalid in invalid_outputs:
            with self.subTest(output=invalid):
                result = run_qemu_smoke.QemuResult(
                    output=invalid,
                    return_code=0,
                    timed_out=False,
                )
                self.assertFalse(
                    run_qemu_smoke.matches_expected_result(
                        result=result,
                        observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                        expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                        markers=("MICROS_FDT_READY",),
                        patterns=(),
                        require_fdt_events=True,
                        require_fdt_reservations=True,
                        require_frame_allocator_ready=True,
                        require_mmu_ready=True,
                        require_frame_ownership_ready=True,
                        require_frame_ownership_test_report=True,
                    )
                )

    def test_accepts_complete_user_address_space_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=USER_ADDRESS_SPACE_TEST_OUTPUT,
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
            require_mmu_ready=True,
            require_frame_ownership_ready=True,
            require_user_address_space_test_report=True,
        )

        self.assertTrue(accepted)

    def test_rejects_invalid_user_address_space_test_report(self):
        early = USER_ADDRESS_SPACE_TEST_OUTPUT.replace(
            USER_ADDRESS_SPACE_TEST_PASS,
            "",
        ).replace(
            FRAME_OWNERSHIP_READY_RECORD,
            USER_ADDRESS_SPACE_TEST_PASS
            + FRAME_OWNERSHIP_READY_RECORD,
        )
        invalid_outputs = (
            FRAME_OWNERSHIP_OUTPUT,
            early,
            USER_ADDRESS_SPACE_TEST_OUTPUT
                + USER_ADDRESS_SPACE_TEST_PASS,
            USER_ADDRESS_SPACE_TEST_OUTPUT.replace(
                "ownership=validated",
                "ownership=unchecked",
            ),
            USER_ADDRESS_SPACE_TEST_OUTPUT.rstrip("\n"),
        )

        for invalid in invalid_outputs:
            with self.subTest(output=invalid):
                result = run_qemu_smoke.QemuResult(
                    output=invalid,
                    return_code=0,
                    timed_out=False,
                )
                self.assertFalse(
                    run_qemu_smoke.matches_expected_result(
                        result=result,
                        observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                        expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                        markers=("MICROS_FDT_READY",),
                        patterns=(),
                        require_fdt_events=True,
                        require_fdt_reservations=True,
                        require_frame_allocator_ready=True,
                        require_mmu_ready=True,
                        require_frame_ownership_ready=True,
                        require_user_address_space_test_report=True,
                    )
                )

    def test_accepts_complete_user_execution_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=USER_EXECUTION_TEST_OUTPUT,
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
            require_mmu_ready=True,
            require_frame_ownership_ready=True,
            require_user_execution_test_report=True,
        )

        self.assertTrue(accepted)

    def test_rejects_invalid_user_execution_test_report(self):
        early = USER_EXECUTION_TEST_OUTPUT.replace(
            USER_EXECUTION_TEST_PASS,
            "",
        ).replace(
            FRAME_OWNERSHIP_READY_RECORD,
            USER_EXECUTION_TEST_PASS
            + FRAME_OWNERSHIP_READY_RECORD,
        )
        late_trap = USER_EXECUTION_TEST_OUTPUT.replace(
            "MICROS_TRAP_READY\n",
            "",
        ).replace(
            USER_EXECUTION_TEST_PASS,
            USER_EXECUTION_TEST_PASS + "MICROS_TRAP_READY\n",
        )
        invalid_outputs = (
            FRAME_OWNERSHIP_OUTPUT,
            early,
            late_trap,
            USER_EXECUTION_TEST_OUTPUT + USER_EXECUTION_TEST_PASS,
            USER_EXECUTION_TEST_OUTPUT.replace(
                "context=preserved",
                "context=lost",
            ),
            USER_EXECUTION_TEST_OUTPUT.rstrip("\n"),
        )

        for invalid in invalid_outputs:
            with self.subTest(output=invalid):
                result = run_qemu_smoke.QemuResult(
                    output=invalid,
                    return_code=0,
                    timed_out=False,
                )
                self.assertFalse(
                    run_qemu_smoke.matches_expected_result(
                        result=result,
                        observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                        expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                        markers=("MICROS_FDT_READY",),
                        patterns=(),
                        require_fdt_events=True,
                        require_fdt_reservations=True,
                        require_frame_allocator_ready=True,
                        require_mmu_ready=True,
                        require_frame_ownership_ready=True,
                        require_user_execution_test_report=True,
                    )
                )
