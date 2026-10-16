from tools import run_qemu_smoke

from tests.host.qemu_smoke_test_fixture import (
    OBJECTS_READY_RECORD,
    MMU_READY_RECORD,
    FRAME_OWNERSHIP_READY_RECORD,
    SCHEDULER_TEST_PASS,
    MMU_OUTPUT,
    FRAME_OWNERSHIP_OUTPUT,
    SCHEDULER_TEST_OUTPUT,
    ENDPOINT_TEST_PASS,
    ENDPOINT_TEST_OUTPUT,
    IPC_TEST_PASS,
    IPC_ADDRESS_SPACES_PASS,
    IPC_TEST_OUTPUT,
    OBJECT_MODEL_TEST_PASS,
    NESTED_TRAP_TEST_PASS,
    OBJECT_MODEL_TEST_OUTPUT,
    NESTED_TRAP_TEST_OUTPUT,
    SCHEDULER_INVALID_DIAGNOSTIC,
    SCHEDULER_INVALID_OUTPUT,
)


class ObjectReportCases:
    def test_accepts_complete_scheduler_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=SCHEDULER_TEST_OUTPUT,
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
            require_scheduler_test_report=True,
        )

        self.assertTrue(accepted)

    def test_rejects_invalid_scheduler_test_report(self):
        early = SCHEDULER_TEST_OUTPUT.replace(
            SCHEDULER_TEST_PASS,
            "",
        ).replace(
            FRAME_OWNERSHIP_READY_RECORD,
            SCHEDULER_TEST_PASS + FRAME_OWNERSHIP_READY_RECORD,
        )
        late_trap = SCHEDULER_TEST_OUTPUT.replace(
            "MICROS_TRAP_READY\n",
            "",
        ).replace(
            SCHEDULER_TEST_PASS,
            SCHEDULER_TEST_PASS + "MICROS_TRAP_READY\n",
        )
        invalid_outputs = (
            FRAME_OWNERSHIP_OUTPUT,
            early,
            late_trap,
            SCHEDULER_TEST_OUTPUT + SCHEDULER_TEST_PASS,
            SCHEDULER_TEST_OUTPUT.replace(
                "accounting=separate",
                "accounting=combined",
            ),
            SCHEDULER_TEST_OUTPUT.rstrip("\n"),
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
                        require_scheduler_test_report=True,
                    )
                )

    def test_accepts_complete_endpoint_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=ENDPOINT_TEST_OUTPUT,
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
            require_objects_ready=True,
            require_endpoint_test_report=True,
        )

        self.assertTrue(accepted)

    def test_rejects_invalid_endpoint_test_report(self):
        early = ENDPOINT_TEST_OUTPUT.replace(
            ENDPOINT_TEST_PASS,
            "",
        ).replace(
            FRAME_OWNERSHIP_READY_RECORD,
            ENDPOINT_TEST_PASS + FRAME_OWNERSHIP_READY_RECORD,
        )
        invalid_outputs = (
            FRAME_OWNERSHIP_OUTPUT,
            ENDPOINT_TEST_OUTPUT + ENDPOINT_TEST_PASS,
            early,
            ENDPOINT_TEST_OUTPUT.replace(
                "profiles=immutable",
                "profiles=mutable",
            ),
            ENDPOINT_TEST_OUTPUT.rstrip("\n"),
        )

        for output in invalid_outputs:
            with self.subTest(output=output):
                result = run_qemu_smoke.QemuResult(
                    output=output,
                    return_code=0,
                    timed_out=False,
                )

                self.assertFalse(
                    run_qemu_smoke.matches_expected_result(
                        result=result,
                        observed_outcome=(
                            run_qemu_smoke.SmokeOutcome.PASS
                        ),
                        expected_outcome=(
                            run_qemu_smoke.SmokeOutcome.PASS
                        ),
                        markers=("MICROS_FDT_READY",),
                        patterns=(),
                        require_fdt_events=True,
                        require_fdt_reservations=True,
                        require_frame_allocator_ready=True,
                        require_mmu_ready=True,
                        require_frame_ownership_ready=True,
                        require_objects_ready=True,
                        require_endpoint_test_report=True,
                    )
                )

    def test_accepts_complete_ipc_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=IPC_TEST_OUTPUT,
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
            require_objects_ready=True,
            require_ipc_test_report=True,
        )

        self.assertTrue(accepted)

    def test_rejects_invalid_ipc_test_report(self):
        early = IPC_TEST_OUTPUT.replace(
            IPC_TEST_PASS,
            "",
        ).replace(
            FRAME_OWNERSHIP_READY_RECORD,
            IPC_TEST_PASS + FRAME_OWNERSHIP_READY_RECORD,
        )
        invalid_outputs = (
            FRAME_OWNERSHIP_OUTPUT,
            IPC_TEST_OUTPUT + IPC_TEST_PASS,
            IPC_TEST_OUTPUT.replace(IPC_ADDRESS_SPACES_PASS, ""),
            IPC_TEST_OUTPUT + IPC_ADDRESS_SPACES_PASS,
            early,
            IPC_TEST_OUTPUT.replace(
                "calls=tokenized",
                "calls=ambiguous",
            ),
            IPC_TEST_OUTPUT.replace(
                "roots=preserved",
                "roots=changed",
            ),
            IPC_TEST_OUTPUT.rstrip("\n"),
        )

        for output in invalid_outputs:
            with self.subTest(output=output):
                result = run_qemu_smoke.QemuResult(
                    output=output,
                    return_code=0,
                    timed_out=False,
                )
                self.assertFalse(
                    run_qemu_smoke.matches_expected_result(
                        result=result,
                        observed_outcome=(
                            run_qemu_smoke.SmokeOutcome.PASS
                        ),
                        expected_outcome=(
                            run_qemu_smoke.SmokeOutcome.PASS
                        ),
                        markers=("MICROS_FDT_READY",),
                        patterns=(),
                        require_fdt_events=True,
                        require_fdt_reservations=True,
                        require_frame_allocator_ready=True,
                        require_mmu_ready=True,
                        require_frame_ownership_ready=True,
                        require_objects_ready=True,
                        require_ipc_test_report=True,
                    )
                )

    def test_requires_exact_invalid_scheduler_context_report(self):
        accepted = run_qemu_smoke.matches_expected_result(
            result=run_qemu_smoke.QemuResult(
                output=SCHEDULER_INVALID_OUTPUT,
                return_code=0,
                timed_out=False,
            ),
            observed_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            markers=(),
            patterns=(),
            require_panic_report=True,
            require_trap_context=True,
            require_scheduler_invalid_context="outgoing",
        )
        self.assertTrue(accepted)

        invalid_outputs = (
            SCHEDULER_INVALID_OUTPUT.replace(
                SCHEDULER_INVALID_DIAGNOSTIC,
                SCHEDULER_INVALID_DIAGNOSTIC * 2,
            ),
            SCHEDULER_INVALID_OUTPUT.replace(
                "state=outgoing",
                "state=next",
            ),
            SCHEDULER_INVALID_OUTPUT + SCHEDULER_TEST_PASS,
            SCHEDULER_INVALID_OUTPUT
            + "MICROS_SCHEDULER_TEST_PASS malformed\n",
            SCHEDULER_INVALID_OUTPUT
            + "MICROS_SCHEDULER_TEST_PASS unterminated",
        )
        for output in invalid_outputs:
            with self.subTest(output=output):
                self.assertFalse(
                    run_qemu_smoke.matches_expected_result(
                        result=run_qemu_smoke.QemuResult(
                            output=output,
                            return_code=0,
                            timed_out=False,
                        ),
                        observed_outcome=(
                            run_qemu_smoke.SmokeOutcome.PANIC
                        ),
                        expected_outcome=(
                            run_qemu_smoke.SmokeOutcome.PANIC
                        ),
                        markers=(),
                        patterns=(),
                        require_panic_report=True,
                        require_trap_context=True,
                        require_scheduler_invalid_context="outgoing",
                    )
                )

    def test_accepts_complete_objects_ready_report(self):
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
            require_objects_ready=True,
        )

        self.assertTrue(accepted)

    def test_rejects_invalid_objects_ready_report(self):
        late = MMU_OUTPUT.replace(OBJECTS_READY_RECORD, "").replace(
            "MICROS_TRAP_READY\n",
            "MICROS_TRAP_READY\n" + OBJECTS_READY_RECORD,
        )
        invalid_outputs = (
            MMU_OUTPUT.replace(OBJECTS_READY_RECORD, ""),
            MMU_OUTPUT + OBJECTS_READY_RECORD,
            MMU_OUTPUT.replace(
                "max-harts=0x0000000000000001",
                "max-harts=0x0000000000000002",
            ),
            MMU_OUTPUT.replace(
                "boot-hart=0x0000000000000000",
                "boot-hart=0x0000000000000001",
            ),
            MMU_OUTPUT.replace(
                "processes=0x0000000000000000",
                "processes=0x0",
            ),
            late,
            MMU_OUTPUT.replace(
                OBJECTS_READY_RECORD,
                OBJECTS_READY_RECORD.rstrip("\n"),
            ),
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
                        require_objects_ready=True,
                    )
                )

    def test_accepts_complete_object_model_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=OBJECT_MODEL_TEST_OUTPUT,
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
            require_objects_ready=True,
            require_object_model_test_report=True,
        )

        self.assertTrue(accepted)

    def test_rejects_invalid_object_model_test_report(self):
        early = OBJECT_MODEL_TEST_OUTPUT.replace(
            OBJECT_MODEL_TEST_PASS,
            "",
        ).replace(
            MMU_READY_RECORD,
            OBJECT_MODEL_TEST_PASS + MMU_READY_RECORD,
        )
        invalid_outputs = (
            MMU_OUTPUT,
            OBJECT_MODEL_TEST_OUTPUT + OBJECT_MODEL_TEST_PASS,
            early,
            OBJECT_MODEL_TEST_OUTPUT.replace(
                "thread-limit=enforced",
                "thread-limit=ignored",
            ),
            OBJECT_MODEL_TEST_OUTPUT.rstrip("\n"),
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
                        require_objects_ready=True,
                        require_object_model_test_report=True,
                    )
                )

    def test_accepts_complete_nested_trap_test_report(self):
        result = run_qemu_smoke.QemuResult(
            output=NESTED_TRAP_TEST_OUTPUT,
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
            require_objects_ready=True,
            require_nested_trap_test_report=True,
        )

        self.assertTrue(accepted)

    def test_rejects_invalid_nested_trap_test_report(self):
        early = NESTED_TRAP_TEST_OUTPUT.replace(
            NESTED_TRAP_TEST_PASS,
            "",
        ).replace(
            MMU_READY_RECORD,
            NESTED_TRAP_TEST_PASS + MMU_READY_RECORD,
        )
        invalid_outputs = (
            MMU_OUTPUT,
            NESTED_TRAP_TEST_OUTPUT + NESTED_TRAP_TEST_PASS,
            early,
            NESTED_TRAP_TEST_OUTPUT.replace(
                "emergency-stack=selected",
                "emergency-stack=default",
            ),
            NESTED_TRAP_TEST_OUTPUT.rstrip("\n"),
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
                        require_objects_ready=True,
                        require_nested_trap_test_report=True,
                    )
                )
