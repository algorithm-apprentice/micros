"""PLAT02's fixed merged-design and independent baseline-oracle declarations."""

from dataclasses import dataclass
import io
import struct
import unittest

from tests.host.terminal_failure_source import RECIPE_ADMISSION
from tests.host.work_count_snapshots import validate_snapshot
from tests.host.work_count_types import Kind, Owner, Snapshot


DESIGN_PINS = {
    "design_commit": "aac991ad9d734f8ef6c375e43934bc95f9defe04",
    "design_tree": "800390f79a9c12ba5078ccdb898292fd6aaebefb",
    "runtime_reference": "8375ca2f5285bc42063af8ed92ba6f3811d517e8",
    "runtime_tree": "cb472a9262b9d6f22c42c4c9e1182bf1da069d78",
    "minix_reference": "4db99f4012570a577414fe2a43697b2f239b699e",
    "adr_document": "docs/adr/0095-bounded-uart-and-terminal-panic.md",
    "design_document": "docs/architecture/terminal-failure.md",
    "research_document": "docs/research/minix-terminal-failure-baseline.md",
}

ORACLE_RULES = (
    (
        "terminal-transmit-bound",
        "tests.host.terminal_failure_evidence.TerminalFailureOracles.test_transmit_bound",
        "thre=100000;last-ready=accepted;unready-store=forbidden;failure=no-console",
        "ADR-0095", "Bound the actual waits", (1, 2, 5, 7),
    ),
    (
        "terminal-drain-bound",
        "tests.host.terminal_failure_evidence.TerminalFailureOracles.test_drain_bound",
        "temt=100000;last-ready=accepted;failed-handoff=unpublished;failure=no-console",
        "ADR-0095", "Bound the actual waits", (3, 6, 8),
    ),
    (
        "terminal-entry-reset-ownership",
        "tests.host.terminal_failure_evidence.TerminalFailureOracles.test_entry_reset_ownership",
        "first-entry=independent-of-console;reentry=no-report;failure-reset=shared-once;fdt=covered",
        "ADR-0095", "Separate terminal ownership", (4, 9, 12),
    ),
    (
        "terminal-console-repair",
        "tests.host.terminal_failure_evidence.TerminalFailureOracles.test_console_repair",
        "magic=terminal-repaired;notice=once;stuck-output=no-retry;reset=failure",
        "ADR-0095", "Recover only terminal console metadata", (10, 11),
    ),
)


def require_case(scenario):
    if type(scenario) is not int or not 1 <= scenario <= 12:
        raise ValueError("PLAT02 has only its twelve fixed scenario IDs")


def decode_observation(data, scenario):
    require_case(scenario)
    if type(data) is not bytes or len(data) != 104:
        raise ValueError("PLAT02 observation is not the complete 104-byte object")
    fields = RECIPE_ADMISSION["observation"]["fields"]
    values = dict(zip((row[0] for row in fields), struct.unpack("<IIII9QIIII", data)))
    if (
        values["version"] != 1 or values["scenario"] != scenario
        or values["phase"] not in range(6) or values["reserved0"] or values["reserved1"]
        or values["work_status"] not in (*range(6), 0xffffffff)
        or values["thre_reads"] + values["temt_reads"] > 0xffffffffffffffff
    ):
        raise ValueError("PLAT02 observation has foreign, invalid or overflowing fields")
    return values


def decode_work(data, scenario):
    require_case(scenario)
    layout = struct.Struct("<IIQQII" + str(len(Owner) * len(Kind)) + "Q")
    if type(data) is not bytes or len(data) != layout.size:
        raise ValueError("PLAT02 requires a complete actual EV02 snapshot")
    version, reserved0, producer, capture, mask, reserved1, *values = layout.unpack(data)
    snapshot = Snapshot(
        producer, capture, mask,
        tuple(tuple(values[start:start + len(Kind)]) for start in range(0, len(values), len(Kind))),
        version, reserved0, reserved1,
    )
    validate_snapshot(snapshot, 1, 0x504C415430320000 | scenario, 2)
    return snapshot


def expected_history(original, appended):
    if type(original) is not bytes or len(original) != 12012 or type(appended) is not bytes or 0 in appended:
        raise ValueError("PLAT02 history model requires a complete history and nonzero logical bytes")
    next_, size, position = struct.unpack_from("<III", original, 12000)
    if (
        next_ >= 10000 or size > 10000 or position != min(size, 1998)
        or size < 10000 and next_ != size
        or original[10000 + position] or original[11999]
    ):
        raise ValueError("PLAT02 armed history does not satisfy the retained MINIX invariant")
    result, work = bytearray(original), 0
    for byte in appended:
        result[next_] = byte
        result[10000 + position] = byte
        next_ = (next_ + 1) % 10000
        size = min(size + 1, 10000)
        work += 2
        if position == 1998:
            result[10000:11999] = result[10001:12000]
            work += 1999
        else:
            position += 1
    struct.pack_into("<III", result, 12000, next_, size, position)
    return bytes(result), work


FDT_FAILURE = b"MICROS_TEST_FAILURE fdt-magic\n"


@dataclass(frozen=True)
class GuestState:
    observation: bytes
    history: bytes
    console: bytes
    routing: bytes
    control: bytes | None
    work: bytes | None
    ier: int | None
    sp: int


@dataclass(frozen=True)
class TerminalCapture:
    scenario: int
    checkpoint: str
    armed: GuestState
    final: GuestState
    registers: tuple
    stack: tuple
    stop_address: int


@dataclass(frozen=True)
class TerminalCase:
    capture: TerminalCapture
    output: str
    exit_code: int
    terminated_after_witness: bool


def _require(condition, message):
    if not condition:
        raise ValueError("PLAT02 " + message)


def _valid_sp(value, stack):
    return type(value) is int and value % 16 == 0 and stack[0] <= value <= stack[1]


def _case_bounds(capture, *, baseline):
    scenario = capture.scenario
    value = decode_observation(capture.final.observation, scenario)
    reporting = scenario in (4, 7, 8, 9, 10, 11)
    seizures = 2 if scenario == 4 or baseline and scenario == 9 else int(reporting)
    bodies = 2 if baseline and scenario == 9 else int(reporting)
    resets = int(not baseline or scenario in (1, 2, 3, 4, 9, 12))
    _require(
        (value["seizure_entries"], value["report_bodies"], value["reset_entries"]) == (seizures, bodies, resets),
        f"case {scenario} has the wrong first-body, seizure or reset count",
    )
    excess = int(baseline)
    exact = {
        1: (1, 0, 1), 2: (100000, 0, 1), 3: (0, 100000, 0),
        5: (100000 + excess, 0, 0), 6: (0, 100000 + excess, 0),
        7: (100000 + excess, 0, 0),
    }
    if scenario == 9 and not baseline:
        exact[9] = (0, 0, 0)
    if scenario in (10, 11) and baseline:
        exact[scenario] = (0, 0, 0)
    elif scenario == 11:
        exact[11] = (100000, 0, 0)
    if scenario in exact:
        _require(
            tuple(value[name] for name in ("thre_reads", "temt_reads", "thr_writes")) == exact[scenario],
            f"case {scenario} did not reach the exact physical-read/store boundary",
        )
    else:
        _require(
            0 < value["thr_writes"] <= value["thre_reads"] <= value["thr_writes"] * 100000,
            f"case {scenario} has no bounded complete physical output",
        )
        if scenario == 8:
            _require(value["temt_reads"] == 100000 + excess, "panic drain has the wrong exact attempt count")
        else:
            _require(1 <= value["temt_reads"] <= 100000, "successful final drain is missing or unbounded")
    if scenario == 12:
        _require(value["thr_writes"] == len(FDT_FAILURE) + 1, "FDT failure changed its exact physical output")
        if not baseline:
            _require(capture.final.control == struct.pack("<I", 2), "FDT failure reset has no published ownership")
    if bodies:
        _require(_valid_sp(value["first_entry_sp"], capture.stack), "first captured panic SP leaves its owned stack")
        _require(capture.final.sp < value["first_entry_sp"], "panic capture has no retained first-entry frame")
        if scenario == 9:
            _require(capture.final.sp <= value["first_entry_sp"] - 96, "recursive panic does not retain both captures")
    else:
        _require(value["first_entry_sp"] == 0, "non-reporting case invented a captured panic SP")


def _capture_objects(capture, *, baseline):
    scenario, initial, final = capture.scenario, capture.armed, capture.final
    magic = 0x554152544F574E52 ^ int(scenario in (10, 11))
    _require(
        type(initial.console) is bytes and len(initial.console) == 16
        and struct.unpack_from("<QIB", initial.console) == (magic, 1, 0),
        "armed console does not have its exact normal or corrupt-magic input",
    )
    expected_console = bytearray(initial.console)
    if scenario == 3:
        expected_console[12] = 1
    elif 4 <= scenario <= 11 and scenario not in (5, 6) and not (baseline and scenario in (10, 11)):
        struct.pack_into("<QIB", expected_console, 0, 0x554152544F574E52, 3, 1)
    _require(final.console == bytes(expected_console), "console transition/repair changed the wrong semantic fields or padding")
    _require(initial.routing == bytes(8), "armed diagnostic routing is not unhanded-off and empty")
    expected_routing = bytearray(initial.routing)
    if scenario == 3:
        expected_routing[:5] = struct.pack("<IB", struct.unpack_from("<I", initial.history, 12000)[0], 1)
    _require(final.routing == bytes(expected_routing), "drain failure or success published the wrong handoff cursor/state")
    appended = b"x" if scenario in (1, 2) else FDT_FAILURE if scenario == 12 else b""
    history, copied = expected_history(initial.history, appended)
    _require(final.history == history, "failed output, panic or handoff changed retained history")
    expected_control = None if baseline else struct.pack("<I", 0 if scenario <= 3 else 2)
    _require(initial.control == (None if baseline else bytes(4)), "armed panic ownership is not genuine IDLE or baseline absence")
    _require(final.control == expected_control, "reset ownership is absent, unlatched or spuriously invented")
    if scenario in (3, 6):
        _require(
            type(initial.ier) is int and type(final.ier) is int
            and initial.ier == 3 and final.ier == (0 if scenario == 3 else 3),
            "handoff failed to preserve its exact IER publication boundary",
        )
    else:
        _require(initial.ier is None and final.ier is None, "another case borrowed the handoff MMIO observation")
    return copied


def validate_capture(capture, *, baseline):
    _require(type(capture) is TerminalCapture and type(baseline) is bool, "capture has no exact source stage or type")
    require_case(capture.scenario)
    _require(type(capture.armed) is GuestState and type(capture.final) is GuestState, "capture omits a complete checkpoint")
    _require(
        type(capture.stack) is tuple and len(capture.stack) == 2
        and all(type(value) is int and 0 <= value <= 0xffffffffffffffff for value in capture.stack)
        and capture.stack[1] - capture.stack[0] == 16384,
        "capture borrowed another stack extent",
    )
    _require(
        _valid_sp(capture.armed.sp, capture.stack) and _valid_sp(capture.final.sp, capture.stack),
        "stopped SP is outside its aligned linker-owned boot stack",
    )
    scenario = capture.scenario
    expected_stop = (
        "overrun" if baseline and scenario in (5, 6, 7, 8)
        else "corruption" if baseline and scenario in (10, 11) else "reset"
    )
    _require(capture.checkpoint == expected_stop, "capture is not its exact reset, overrun or pinned corruption witness")
    before, after = (decode_observation(state.observation, scenario) for state in (capture.armed, capture.final))
    _require(before["phase"] == 1 and before["work_status"] == 0xffffffff, "capture was not armed before observation")
    _require(all(before[name] == 0 for name in (
        "thre_reads", "temt_reads", "thr_writes", "seizure_entries", "report_bodies",
        "reset_entries", "first_entry_sp", "reset_type", "reset_reason",
    )), "armed fixture already contains operation results")
    _require(capture.armed.work == bytes(3056), "armed work snapshot is not still unpublished")
    _require(
        not any(value["sstatus"] & 2 or value["sie"] & 512 for value in (before, after)),
        "terminal observation retains enabled supervisor/external interrupts",
    )
    copied = _capture_objects(capture, baseline=baseline)
    _require(after["phase"] == {"reset": 2, "overrun": 3, "corruption": 1}[expected_stop], "checkpoint phase is inconsistent")
    if expected_stop == "corruption":
        _require(
            capture.final.work is None and after["work_status"] == 0xffffffff,
            "baseline corruption spin invented a completed work capture",
        )
    else:
        _require(after["work_status"] == 0, "work collection failed before the checkpoint")
        snapshot = decode_work(capture.final.work, scenario)
        counts = [0] * len(Kind)
        counts[Kind.COMPARISON] = after["thre_reads"] + after["temt_reads"]
        counts[Kind.COPY_UNIT] = copied
        counts[Kind.COPIED_BYTE] = copied
        _require(snapshot.counts[Owner.KERNEL_DIAGNOSTIC] == tuple(counts), "EV02 work is not the exact actual waits/history movement")
    _require(
        type(capture.registers) is tuple and len(capture.registers) == 6
        and all(type(row) is tuple and len(row) == 2 and all(type(value) is int for value in row) for row in capture.registers),
        "capture lacks its exact complete integer-register witness",
    )
    registers = dict(capture.registers)
    _require(
        set(registers) == {2, 10, 11, 16, 17, 32}
        and all(0 <= value <= 0xffffffffffffffff for value in registers.values())
        and registers[2] == capture.final.sp and registers[32] == capture.stop_address,
        "registers do not name the actual stopped SP/instruction",
    )
    if expected_stop == "reset":
        reason = int(scenario > 3)
        _require(tuple(registers[number] for number in (10, 11, 16, 17)) == (0, reason, 0, 0x53525354),
                 "actual SRST instruction has a changed EID/FID/type/reason")
        _require((after["reset_type"], after["reset_reason"]) == (0, reason), "reset observation disagrees with actual registers")
    else:
        _require((after["reset_type"], after["reset_reason"]) == (0, 0), "non-reset stop invented a firmware request")
    _case_bounds(capture, baseline=baseline)


def validate_case(case, *, baseline):
    from tools.qemu_smoke_faults import _diagnostic_history_payload, _has_complete_panic_report

    _require(type(case) is TerminalCase and type(case.output) is str, "case has no actual capture and transcript")
    validate_capture(case.capture, baseline=baseline)
    scenario, output = case.capture.scenario, case.output
    terminated = baseline and case.capture.checkpoint != "reset"
    _require(type(case.terminated_after_witness) is bool and case.terminated_after_witness == terminated,
             "a host-terminated case borrowed natural firmware termination")
    _require(type(case.exit_code) is int and (terminated or case.exit_code == 0), "firmware did not actually terminate QEMU successfully")
    complete_report = scenario in (4, 8) or scenario == 10 and not baseline or scenario == 9 and baseline
    notices = output.count("MICROS_CONSOLE_METADATA_REPAIRED")
    _require(notices == int(scenario == 10 and not baseline), "repair notice is missing, duplicate or emitted on another path")
    failures = tuple(line for line in output.splitlines() if line.startswith("MICROS_TEST_FAILURE"))
    _require(failures == ((FDT_FAILURE.decode().rstrip(),) if scenario == 12 else ()),
             "target has a missing FDT failure or another explicit failure")
    if complete_report:
        _require(_has_complete_panic_report(output), "healthy report lost its five contiguous core records")
        reason = "terminal-recursive-test" if scenario == 9 else "terminal-failure-test"
        _require("MICROS_PANIC reason=" + reason + "\r\n" in output, "first report has the wrong fixture reason")
        _require("MICROS_TRAP_CONTEXT" not in output, "direct panic invented a trap record")
        history = case.capture.armed.history
        length = struct.unpack_from("<I", history, 12008)[0]
        _require(_diagnostic_history_payload(output) == history[10000:10000 + length],
                 "panic did not replay the unchanged pre-seizure tail")
        prefix = "MICROS_CONSOLE_METADATA_REPAIRED\r\n" if notices else "MICROS_PANIC reason="
        start = output.find(prefix)
        if notices:
            _require(output[start + len(prefix):].startswith("MICROS_PANIC reason="), "repair notice did not precede the first report")
        expected_writes = len(output[start:].encode("ascii"))
        observed = decode_observation(case.capture.final.observation, scenario)
        _require(observed["thr_writes"] == expected_writes, "physical write observations do not match the complete report")
    else:
        _require("MICROS_PANIC" not in output and "MICROS_DIAGNOSTIC_HISTORY" not in output,
                 "no-console or direct-FDT termination attempted a panic report")


class TerminalFailureOracles(unittest.TestCase):
    observed = None

    def setUp(self):
        _require(type(self.observed) is tuple and len(self.observed) == 12, "oracles need all twelve actual case executions")

    def require_current(self, cases):
        for scenario in cases:
            try:
                _case_bounds(self.observed[scenario - 1].capture, baseline=False)
            except ValueError as error:
                self.fail(str(error))

    def test_transmit_bound(self):
        self.require_current((1, 2, 5, 7))

    def test_drain_bound(self):
        self.require_current((3, 6, 8))

    def test_entry_reset_ownership(self):
        self.require_current((4, 9, 12))

    def test_console_repair(self):
        self.require_current((10, 11))


def run_oracles(cases, *, baseline):
    _require(type(cases) is tuple and len(cases) == 12, "requires twelve complete cases before named oracle execution")
    _require(all(type(case) is TerminalCase for case in cases), "case set contains an untyped observation")
    _require(tuple(case.capture.scenario for case in cases) == tuple(range(1, 13)), "case set is missing, duplicate or reordered")
    for case in cases:
        validate_case(case, baseline=baseline)
    suite = []
    for row in ORACLE_RULES:
        test, = unittest.defaultTestLoader.loadTestsFromName(row[1])
        test.observed = cases
        suite.append(test)
    output = io.StringIO()
    result = unittest.TextTestRunner(stream=output, verbosity=2).run(unittest.TestSuite(suite))
    _require(
        result.testsRun == 4 and not (result.errors or result.skipped or result.expectedFailures or result.unexpectedSuccesses),
        "named oracle execution is incomplete or contains an execution error",
    )
    _require(
        {test.id() for test, _ in result.failures} == ({row[1] for row in ORACLE_RULES} if baseline else set()),
        "named oracle results do not discriminate the exact baseline/current properties:\n" + output.getvalue(),
    )
    return tuple((row[0], "EXPECTED_FAILURE" if baseline else "PASS") for row in ORACLE_RULES), output.getvalue()
