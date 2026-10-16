from tools import run_qemu_smoke

from tests.host.qemu_smoke_test_fixture import (
    PANIC_OUTPUT,
    TRAP_CONTEXT_RECORD,
    TRAP_PANIC_OUTPUT,
)


class PanicReportCases:
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

    def test_rejects_forbidden_marker(self):
        result = run_qemu_smoke.QemuResult(
            output=PANIC_OUTPUT + "MICROS_BOOTSTRAP_TEST_PASS\n",
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            markers=("MICROS_PANIC reason=intentional-test",),
            patterns=(),
            forbidden_markers=("MICROS_BOOTSTRAP_TEST_PASS",),
            require_panic_report=True,
        )

        self.assertFalse(accepted)

    def test_rejects_forbidden_pattern(self):
        result = run_qemu_smoke.QemuResult(
            output=PANIC_OUTPUT + "MICROS_BOOTSTRAP_FAILURE reason=x\n",
            return_code=0,
            timed_out=False,
        )

        accepted = run_qemu_smoke.matches_expected_result(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            markers=("MICROS_PANIC reason=intentional-test",),
            patterns=(),
            forbidden_patterns=(r"MICROS_BOOTSTRAP_FAILURE .+",),
            require_panic_report=True,
        )

        self.assertFalse(accepted)

    def test_requires_patterns_in_order(self):
        result = run_qemu_smoke.QemuResult(
            output="third\nfirst\nsecond\n",
            return_code=0,
            timed_out=False,
        )

        self.assertFalse(
            run_qemu_smoke.matches_expected_result(
                result=result,
                observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                markers=("first",),
                patterns=(),
                ordered_patterns=("first", "second", "third"),
            )
        )
        self.assertTrue(
            run_qemu_smoke.matches_expected_result(
                result=result,
                observed_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                expected_outcome=run_qemu_smoke.SmokeOutcome.PASS,
                markers=("first",),
                patterns=(),
                ordered_patterns=("third", "first", "second"),
            )
        )

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

    def test_requires_exact_vm_self_fault_and_context(self):
        fault_context = (
            "MICROS_TRAP_CONTEXT "
            "origin=U "
            "sstatus=0x0000000200000020 "
            "scause=0x000000000000000f "
            "stval=0x0000000070000000 "
            "sepc=0x0000000040000150 "
            "ra=0x00000000400001b8 "
            "sp=0x000000007fffffa0\n"
        )
        fault_record = (
            "MICROS_VM_SELF_FAULT "
            "service=0x0000000000000002 "
            "process-slot=0x0000000000000001 "
            "process-generation=0x0000000000000001 "
            "endpoint=0x0000000000001001 "
            "scause=0x000000000000000f "
            "stval=0x0000000070000000 "
            "sepc=0x0000000040000150 "
            "ownership=handed-off\n"
        )
        output = fault_record + TRAP_PANIC_OUTPUT.replace(
            TRAP_CONTEXT_RECORD,
            fault_context,
        )
        result = run_qemu_smoke.QemuResult(
            output=output,
            return_code=0,
            timed_out=False,
        )
        arguments = dict(
            result=result,
            observed_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            expected_outcome=run_qemu_smoke.SmokeOutcome.PANIC,
            markers=("MICROS_PANIC reason=unexpected-exception",),
            patterns=(),
            require_panic_report=True,
            require_trap_context=True,
            expected_trap_context_sepc=0x40000150,
            expected_trap_context_origin="U",
            expected_trap_context_scause=0xF,
            expected_trap_context_stval=0x70000000,
            require_vm_self_fault=True,
            expected_vm_self_fault_sepc=0x40000150,
            expected_vm_self_fault_scause=0xF,
            expected_vm_self_fault_stval=0x70000000,
            expected_vm_self_fault_ownership="handed-off",
        )

        self.assertTrue(
            run_qemu_smoke.matches_expected_result(**arguments)
        )
        arguments["expected_vm_self_fault_sepc"] = 0x40000154
        self.assertFalse(
            run_qemu_smoke.matches_expected_result(**arguments)
        )
