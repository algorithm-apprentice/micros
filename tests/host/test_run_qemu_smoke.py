import contextlib
import io
import sys
import unittest

from tools import run_qemu_smoke


OBJECTS_READY_RECORD = (
    "MICROS_OBJECTS_READY "
    "processes=0x0000000000000000 "
    "threads=0x0000000000000000 "
    "harts=0x0000000000000001 "
    "max-threads=0x0000000000000001 "
    "max-harts=0x0000000000000001 "
    "boot-hart=0x0000000000000000\n"
)

PANIC_OUTPUT = (
    "MICROS_BOOT 0.1.0\n"
    + OBJECTS_READY_RECORD
    + "MICROS_TRAP_READY\n"
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
    + OBJECTS_READY_RECORD
    + "MICROS_TRAP_READY\n"
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
    "origin=S cause=illegal-instruction registers=preserved "
    "hart-context=routed primary-stack=selected sscratch=anchor\n"
)

TIMER_TEST_OUTPUT = TRAP_RECOVERY_OUTPUT.replace(
    "MICROS_TRAP_TEST_PASS "
    "origin=S cause=illegal-instruction registers=preserved "
    "hart-context=routed primary-stack=selected sscratch=anchor\n",
    "MICROS_TIMER_TEST_PASS "
    "ticks=0x0000000000000003 "
    "interval=0x00000000000186a0 "
    "active=0x0000000000000000 "
    "deadline=0xffffffffffffffff owner=hart\n",
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

MMU_READY_RECORD = (
    "MICROS_MMU_READY "
    "mode=sv39 "
    "root=0x0000000080216000 "
    "tables=0x0000000000000042\n"
)

MMU_TEST_PASS = (
    "MICROS_MMU_TEST_PASS "
    "store-fault=text "
    "execute-fault=writable "
    "traps=0x0000000000000002\n"
)

FRAME_OWNERSHIP_READY_RECORD = (
    "MICROS_FRAME_OWNERSHIP_READY "
    "owned=0x0000000000000042 "
    "kernel-tables=0x0000000000000042 "
    "phase=bootstrap\n"
)

FRAME_OWNERSHIP_TEST_PASS = (
    "MICROS_FRAME_OWNERSHIP_TEST_PASS "
    "stale=rejected "
    "release=blocked "
    "handoff=atomic "
    "invariants=preserved\n"
)

USER_ADDRESS_SPACE_TEST_PASS = (
    "MICROS_USER_ADDRESS_SPACE_TEST_PASS "
    "roots=isolated "
    "reuse=zeroed "
    "active=guarded "
    "ownership=validated "
    "sum=cleared\n"
)

USER_EXECUTION_TEST_PASS = (
    "MICROS_USER_EXECUTION_TEST_PASS "
    "mode=entered "
    "faults=isolated "
    "context=preserved "
    "stack=owned "
    "return=resumed\n"
)

SCHEDULER_TEST_PASS = (
    "MICROS_SCHEDULER_TEST_PASS "
    "queues=minix-priority "
    "current=reachable "
    "accounting=separate "
    "switches=alternating "
    "idle=resumed "
    "registers=preserved\n"
)

FRAME_ALLOCATOR_OUTPUT = TRAP_RECOVERY_OUTPUT.replace(
    "MICROS_TRAP_TEST_PASS "
    "origin=S cause=illegal-instruction registers=preserved "
    "hart-context=routed primary-stack=selected sscratch=anchor\n",
    "",
).replace(
    "MICROS_FDT_READY\n",
    "MICROS_FDT_READY\n" + FRAME_ALLOCATOR_READY_RECORD,
)

FRAME_ALLOCATOR_TEST_OUTPUT = (
    FRAME_ALLOCATOR_OUTPUT + FRAME_ALLOCATOR_TEST_PASS
)

MMU_OUTPUT = FRAME_ALLOCATOR_OUTPUT + MMU_READY_RECORD

MMU_TEST_OUTPUT = MMU_OUTPUT + MMU_TEST_PASS

FRAME_OWNERSHIP_OUTPUT = MMU_OUTPUT + FRAME_OWNERSHIP_READY_RECORD

FRAME_OWNERSHIP_TEST_OUTPUT = (
    FRAME_OWNERSHIP_OUTPUT + FRAME_OWNERSHIP_TEST_PASS
)

USER_ADDRESS_SPACE_TEST_OUTPUT = (
    FRAME_OWNERSHIP_OUTPUT + USER_ADDRESS_SPACE_TEST_PASS
)

USER_EXECUTION_TEST_OUTPUT = (
    FRAME_OWNERSHIP_OUTPUT + USER_EXECUTION_TEST_PASS
)

SCHEDULER_TEST_OUTPUT = (
    FRAME_OWNERSHIP_OUTPUT + SCHEDULER_TEST_PASS
)

ENDPOINT_TEST_PASS = (
    "MICROS_ENDPOINT_TEST_PASS "
    "generation=validated "
    "profiles=immutable "
    "visibility=staged "
    "authorization=separate "
    "grants=generation-safe\n"
)

ENDPOINT_TEST_OUTPUT = (
    FRAME_OWNERSHIP_OUTPUT + ENDPOINT_TEST_PASS
)

IPC_TEST_PASS = (
    "MICROS_IPC_TEST_PASS "
    "endpoints=generation-safe "
    "queues=blocking "
    "calls=tokenized "
    "notifications=coalesced "
    "kernel-events=injected "
    "deadlock=rejected\n"
)

IPC_ADDRESS_SPACES_PASS = (
    "MICROS_IPC_ADDRESS_SPACES "
    "count=three "
    "roots=preserved "
    "contexts=preserved "
    "stacks=preserved "
    "scheduler=preserved "
    "messages=preserved\n"
)

IPC_TEST_OUTPUT = (
    FRAME_OWNERSHIP_OUTPUT
    + IPC_ADDRESS_SPACES_PASS
    + IPC_TEST_PASS
)

OBJECT_MODEL_TEST_PASS = (
    "MICROS_OBJECT_MODEL_TEST_PASS "
    "process-generation=advanced "
    "stale=rejected "
    "thread-limit=enforced "
    "hart-local=preserved\n"
)

NESTED_TRAP_TEST_PASS = (
    "MICROS_NESTED_TRAP_TEST_PASS "
    "hart=routed emergency-stack=selected\n"
)

OBJECT_MODEL_TEST_OUTPUT = MMU_OUTPUT + OBJECT_MODEL_TEST_PASS

NESTED_TRAP_TEST_OUTPUT = MMU_OUTPUT + NESTED_TRAP_TEST_PASS

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
        "MICROS_PANIC reason=intentional-test\n",
        "MICROS_PANIC reason=unexpected-exception\n",
    )
    .replace(
        "file=kernel/main.c",
        "file=kernel/trap.c",
    )
    + TRAP_CONTEXT_RECORD
)

SCHEDULER_INVALID_DIAGNOSTIC = (
    "MICROS_SCHEDULER_INVALID_CONTEXT "
    "state=outgoing ownership=preserved accounting=kernel\n"
)
SCHEDULER_INVALID_CONTEXT_RECORD = (
    "MICROS_TRAP_CONTEXT "
    "origin=U "
    "sstatus=0x0000000200000020 "
    "scause=0x8000000000000005 "
    "stval=0x0000000000000000 "
    "sepc=0x0000000040000010 "
    "ra=0x0000000000001001 "
    "sp=0x0000000040005000\n"
)
SCHEDULER_INVALID_OUTPUT = (
    PANIC_OUTPUT.replace(
        "MICROS_PANIC reason=intentional-test\n",
        SCHEDULER_INVALID_DIAGNOSTIC
        + "MICROS_PANIC reason=invalid-bootstrap-user-context\n",
    )
    + SCHEDULER_INVALID_CONTEXT_RECORD
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


class QemuSerialInputTest(unittest.TestCase):
    def test_writes_serial_input_once_after_complete_trigger_line(self):
        command = [
            sys.executable,
            "-c",
            (
                "import sys;"
                "sys.stdout.write('READY\\n');"
                "sys.stdout.flush();"
                "data=sys.stdin.buffer.read(3);"
                "sys.stdout.write(data.hex()+'\\n');"
                "sys.stdout.flush()"
            ),
        ]

        result = run_qemu_smoke.run_qemu(
            command,
            2.0,
            serial_input_trigger="READY",
            serial_input=b"x\x7f\r",
        )

        self.assertFalse(result.timed_out)
        self.assertEqual(0, result.return_code)
        self.assertTrue(result.serial_input_sent)
        self.assertEqual("READY\n787f0d\n", result.output)

    def test_default_execution_keeps_stdin_closed(self):
        command = [
            sys.executable,
            "-c",
            (
                "import sys;"
                "data=sys.stdin.buffer.read(1);"
                "sys.stdout.write('EOF\\n' if not data else 'DATA\\n')"
            ),
        ]

        result = run_qemu_smoke.run_qemu(command, 2.0)

        self.assertFalse(result.timed_out)
        self.assertEqual(0, result.return_code)
        self.assertFalse(result.serial_input_sent)
        self.assertEqual("EOF\n", result.output)

    def test_closed_serial_input_pipe_returns_a_normal_result(self):
        command = [
            sys.executable,
            "-c",
            (
                "import os,sys;"
                "os.close(0);"
                "sys.stdout.write('READY\\n');"
                "sys.stdout.flush()"
            ),
        ]

        result = run_qemu_smoke.run_qemu(
            command,
            2.0,
            serial_input_trigger="READY",
            serial_input=b"x",
        )

        self.assertFalse(result.timed_out)
        self.assertEqual(0, result.return_code)
        self.assertFalse(result.serial_input_sent)
        self.assertEqual("READY\n", result.output)

    def test_trigger_requires_an_exact_terminated_line(self):
        self.assertFalse(
            run_qemu_smoke._serial_trigger_observed(
                b"prefix READY\nREADY",
                "READY",
            )
        )
        self.assertTrue(
            run_qemu_smoke._serial_trigger_observed(
                b"prefix READY\nREADY\r\n",
                "READY",
            )
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
