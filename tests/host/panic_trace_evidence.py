"""PLAT03's independently declared corrected design and trace-oracle authority."""

from dataclasses import dataclass
import io
import re
import struct
import unittest

from tests.host.panic_trace_source import RECIPE_ADMISSION
from tests.host.work_count_snapshots import validate_snapshot
from tests.host.work_count_types import Kind, Owner, Snapshot
from tools.qemu_smoke_records import _split_output_records


DESIGN_PINS = {
    "design_commit": "e47c49a5b3be2a7de8e98087675d8381e45952a6",
    "design_tree": "4c24ef1942f86861640737b6746383ded25b595d",
    "base_design_commit": "2c0b6c8ae0c0fb3ec6bcfa9473766b9c334db1be",
    "base_design_tree": "0d62f80ba2c512763a2a49f3da181b70162cbb43",
    "runtime_reference": "e12b2d6c163ac92282d380eff5be419fad7f7a30",
    "runtime_tree": "53d900f539d0e766b2998ff57aeea990ac25e30a",
    "minix_reference": "4db99f4012570a577414fe2a43697b2f239b699e",
    "psabi_reference": "9b2767e2a8851709608c4e1452998ad232d6278b",
    "adr_document": "docs/adr/0096-bounded-kernel-panic-stack-trace.md",
    "correction_document": "docs/adr/0097-panic-trace-link-rejection-precedence.md",
    "design_document": "docs/architecture/panic-stack-trace.md",
    "research_document": "docs/research/minix-panic-stack-trace.md",
}

ORACLE_RULES = (
    (
        "panic-trace-current-chain",
        "tests.host.panic_trace_evidence.PanicTraceOracles.test_current_chain",
        "actual-c-frames=ordered-return-pcs;streamed-records=bounded;ordinary-reset=no-trace",
        "ADR-0096", "Reproduce a bounded frame-chain walk", (1, 2, 6, 18),
    ),
    (
        "panic-trace-owned-epoch",
        "tests.host.panic_trace_evidence.PanicTraceOracles.test_owned_epoch",
        "owner=exact-boot-primary-thread-emergency;generation=checked;root-cfa=terminal;foreign-owner=no-read",
        "ADR-0096", "Authenticate one stack and its entry boundary", (2, 3, 4, 5, 13, 14),
    ),
    (
        "panic-trace-frame-work",
        "tests.host.panic_trace_evidence.PanicTraceOracles.test_frame_work",
        "word-reads=2-per-admitted-record;advance=actual;depth=32;malformed=explicit;auxiliary=constant",
        "ADR-0096", "Reproduce a bounded frame-chain walk", (6, 7, 8, 9, 10, 11, 12),
    ),
    (
        "panic-trace-terminal-precedence",
        "tests.host.panic_trace_evidence.PanicTraceOracles.test_terminal_precedence",
        "trace=first-entry-only;read-fault=real-trap;reentry=no-second-trace;uart=bounded;reset=shared-once",
        "ADR-0096", "Preserve the existing failure path", (15, 16, 17, 18),
    ),
)


def require_case(scenario):
    if type(scenario) is not int or not 1 <= scenario <= 18:
        raise ValueError("PLAT03 has only its eighteen fixed scenario IDs")


def decode_observation(data, scenario):
    require_case(scenario)
    if type(data) is not bytes or len(data) != 192:
        raise ValueError("PLAT03 requires the complete 192-byte observation")
    values = dict(zip(
        (row[0] for row in RECIPE_ADMISSION["observation"]["fields"]),
        struct.unpack("<4I12Q4I6Q2IQ", data),
    ))
    if (
        values["version"] != 1 or values["scenario"] != scenario
        or values["phase"] not in range(5) or values["work_status"] not in (*range(6), 0xffffffff)
        or values["reserved0"] or values["reserved1"]
        or values["owner"] not in range(5) or values["stop"] not in range(8)
        or not values["completed_traces"] <= values["trace_entries"] <= values["report_bodies"] <= 1
        or values["read_words"] > 64 or values["accepted_links"] > 31
        or values["record_count"] > 32 or values["read_words"] < 2 * values["record_count"]
        or values["reset_entries"] > 1
    ):
        raise ValueError("PLAT03 observation has foreign fields, invalid state or impossible bounded progress")
    return values


def decode_work(data, scenario):
    require_case(scenario)
    layout = struct.Struct("<IIQQII" + str(len(Owner) * len(Kind)) + "Q")
    if type(data) is not bytes or len(data) != layout.size:
        raise ValueError("PLAT03 requires the complete actual EV02 snapshot")
    version, reserved0, producer, capture, mask, reserved1, *values = layout.unpack(data)
    snapshot = Snapshot(
        producer, capture, mask,
        tuple(tuple(values[start:start + len(Kind)]) for start in range(0, len(values), len(Kind))),
        version, reserved0, reserved1,
    )
    validate_snapshot(snapshot, 1, 0x504C415430330000 | scenario, 2)
    return snapshot


@dataclass(frozen=True)
class ExpectedTrace:
    frames: tuple
    stop: str
    word_reads: int
    advances: int


@dataclass(frozen=True)
class RecordedTrace:
    owner: str
    frames: tuple
    stop: str | None


def decode_trace(output, *, complete=True):
    if type(output) is not str or type(complete) is not bool:
        raise ValueError("PLAT03 trace transcript has no exact text/completion mode")
    lines, terminated = _split_output_records(output)
    indices = [index for index, line in enumerate(lines) if line.startswith("MICROS_TRACE_")]
    if (
        not indices or indices != list(range(indices[0], indices[-1] + 1))
        or not all(terminated[index] for index in indices)
    ):
        raise ValueError("PLAT03 trace records are missing, interrupted or unterminated")
    rows = [lines[index] for index in indices]
    header = re.fullmatch(
        r"MICROS_TRACE_BEGIN version=1 owner=(boot|primary|thread|emergency|unavailable) limit=0x0000000000000020",
        rows[0],
    )
    if header is None:
        raise ValueError("PLAT03 trace header has an unknown owner, version or bound")
    ending = re.fullmatch(
        r"MICROS_TRACE_END count=(0x[0-9a-f]{16}) stop=(end|boundary|limit|owner|invalid-frame|invalid-pc|nonmonotonic)",
        rows[-1],
    )
    if (ending is not None) != complete:
        raise ValueError("PLAT03 trace did not complete its exact expected boundary")
    frames = []
    for row in rows[1:-1] if complete else rows[1:]:
        match = re.fullmatch(
            r"MICROS_TRACE_FRAME index=(0x[0-9a-f]{16}) pc=(0x[0-9a-f]{16}) fp=(0x[0-9a-f]{16})",
            row,
        )
        if match is None or int(match[1], 16) != len(frames) or len(frames) >= 32:
            raise ValueError("PLAT03 trace has an extra, malformed or misordered frame record")
        frames.append((int(match[3], 16), int(match[2], 16)))
    if complete and int(ending[1], 16) != len(frames):
        raise ValueError("PLAT03 trace end disagrees with its actual emitted record count")
    stop = ending[2] if complete else None
    if header[1] == "unavailable" and (frames or complete and stop != "owner"):
        raise ValueError("PLAT03 unavailable owner invented a successful frame chain")
    return RecordedTrace(header[1], tuple(frames), stop)


@dataclass(frozen=True)
class GuestState:
    observation: bytes
    work: bytes
    control: bytes
    sp: int
    fp: int


@dataclass(frozen=True)
class TraceCapture:
    scenario: int
    armed: GuestState
    started: GuestState | None
    final: GuestState
    history: bytes
    stack: bytes | None
    registers: tuple
    reentry: tuple | None
    stacks: tuple
    text: tuple
    stops: tuple
    return_sites: tuple


@dataclass(frozen=True)
class TraceCase:
    capture: TraceCapture
    output: str
    exit_code: int


def walk_expected_stack(data, *, bottom, root, sp, frame, text):
    if (
        type(data) is not bytes or len(data) not in (4096, 16384)
        or type(text) is not tuple or len(text) != 2
        or any(type(value) is not int or not 0 <= value <= 0xffffffffffffffff
               for value in (bottom, root, sp, frame, *text))
        or not bottom <= sp < sp + 16 <= root <= bottom + len(data) <= 0xffffffffffffffff
        or any(value % 16 for value in (bottom, root, sp))
        or not 0 < text[0] < text[1] or text[0] % 2 or text[1] % 2
        or not (bottom >= text[1] or bottom + len(data) <= text[0])
    ):
        raise ValueError("PLAT03 reference needs one complete independently bound stack/text interval")

    def admitted(pointer):
        return pointer % 16 == 0 and sp + 16 <= pointer <= root

    frames = []
    reads = advances = 0
    if frame == 0:
        return ExpectedTrace((), "end", 0, 0)
    if not admitted(frame):
        return ExpectedTrace((), "invalid-frame", 0, 0)
    while True:
        pc = struct.unpack_from("<Q", data, frame - bottom - 8)[0]
        previous = struct.unpack_from("<Q", data, frame - bottom - 16)[0]
        reads += 2
        if pc == 0 or pc % 2 or not text[0] <= pc < text[1]:
            return ExpectedTrace(tuple(frames), "invalid-pc", reads, advances)
        frames.append((frame, pc))
        if frame == root:
            stop = "boundary"
        elif previous == 0:
            stop = "end"
        elif previous <= frame:
            stop = "nonmonotonic"
        elif not admitted(previous):
            stop = "invalid-frame"
        elif len(frames) == 32:
            stop = "limit"
        else:
            frame = previous
            advances += 1
            continue
        return ExpectedTrace(tuple(frames), stop, reads, advances)


def _require(condition, message):
    if not condition:
        raise ValueError("PLAT03 " + message)


def _registers(rows):
    _require(
        type(rows) is tuple and len(rows) == 7
        and all(type(row) is tuple and len(row) == 2
                and all(type(value) is int and 0 <= value <= 0xffffffffffffffff for value in row) for row in rows),
        "capture lacks its complete integer-register witness",
    )
    values = dict(rows)
    _require(set(values) == {2, 8, 10, 11, 16, 17, 32}, "capture has a duplicate or foreign register")
    return values


def _physical_owner(scenario):
    return "primary" if scenario == 3 else "thread" if scenario in (4, 13, 14) else "emergency" if scenario == 5 else "boot"


def _owned_sp(value, region):
    return type(value) is int and value % 16 == 0 and region[0] <= value < region[1]


def _validate_execution(capture):
    _require(type(capture) is TraceCapture, "capture has no exact type")
    require_case(capture.scenario)
    _require(
        type(capture.armed) is GuestState and type(capture.final) is GuestState,
        "capture omits its real armed/reset states",
    )
    _require(
        type(capture.stacks) is tuple and len(capture.stacks) == 4
        and all(type(row) is tuple and len(row) == 3 and type(row[0]) is str
                and all(type(value) is int and 0 <= value <= 0xffffffffffffffff for value in row[1:])
                for row in capture.stacks),
        "capture has no complete independent stack bindings",
    )
    stacks = {name: (bottom, top) for name, bottom, top in capture.stacks}
    _require(
        set(stacks) == {"boot", "primary", "thread", "emergency"}
        and all(top - bottom == (4096 if name == "emergency" else 16384)
                and bottom % 16 == top % 16 == 0 for name, (bottom, top) in stacks.items()),
        "capture changed a fixed physical stack extent",
    )
    _require(
        type(capture.stops) is tuple
        and all(type(row) is tuple and len(row) == 3 and type(row[0]) is str
                and type(row[1]) is int and row[1] > 0 and type(row[2]) is int and row[2] in (2, 4)
                for row in capture.stops),
        "capture has an invalid checkpoint binding",
    )
    stops = {name: address for name, address, _ in capture.stops}
    _require(len(stops) == len(capture.stops) and {"reset", "reentry"} <= set(stops), "capture omitted actual instruction addresses")
    _require(type(capture.history) is bytes and len(capture.history) == 12012, "fixture history is incomplete")
    next_byte, retained, tail_size = struct.unpack_from("<III", capture.history, 12000)
    _require(next_byte < 10000 and retained <= 10000 and tail_size <= 1998, "fixture history has invalid original geometry")
    before = decode_observation(capture.armed.observation, capture.scenario)
    after = decode_observation(capture.final.observation, capture.scenario)
    _require(
        before["phase"] == 1 and before["work_status"] == 0xffffffff
        and all(value == 0 for name, value in before.items() if name not in ("version", "scenario", "phase", "work_status")),
        "fixture was not armed before all operation results",
    )
    _require(
        type(capture.armed.control) is bytes and capture.armed.control == bytes(4)
        and type(capture.armed.work) is bytes and capture.armed.work == bytes(3056)
        and _owned_sp(capture.armed.sp, stacks["boot"]),
        "armed capture borrowed panic ownership, work or another initial stack",
    )
    reporting = capture.scenario != 18
    _require(
        after["phase"] == 3 and after["work_status"] == 0 and after["reset_entries"] == 1
        and (after["report_bodies"], after["seizure_entries"]) == (int(reporting), int(reporting))
        and (after["reset_type"], after["reset_reason"]) == (0, int(reporting))
        and not (after["reset_sstatus"] & 2 or after["reset_sie"] & 512),
        "reset capture lacks its exact report, interrupt or reset state",
    )
    _require(
        type(capture.final.control) is bytes
        and capture.final.control == struct.pack("<I", 2 if reporting else 0),
        "actual shared reset ownership is missing or spuriously claimed",
    )
    registers = _registers(capture.registers)
    _require(
        (registers[2], registers[8], registers[32]) == (capture.final.sp, capture.final.fp, stops["reset"])
        and tuple(registers[number] for number in (10, 11, 16, 17)) == (0, int(reporting), 0, 0x53525354),
        "registers do not bind the actual final SP/FP/SRST instruction and operands",
    )
    physical = stacks[_physical_owner(capture.scenario)]
    final_region = stacks["primary"] if capture.scenario == 15 else physical
    _require(_owned_sp(capture.final.sp, final_region), "final SP leaves its actual execution epoch")
    if reporting:
        _require(_owned_sp(after["first_panic_sp"], physical), "first panic capture has no actual owned SP")
        if final_region == physical:
            _require(capture.final.sp <= after["first_panic_sp"] - 48, "final continuation discarded its first panic capture")
    else:
        _require(after["first_panic_sp"] == 0, "ordinary reset invented a panic capture")
    work = decode_work(capture.final.work, capture.scenario)
    expected_counts = [0] * len(Kind)
    expected_counts[Kind.COMPARISON] = work.counts[Owner.KERNEL_DIAGNOSTIC][Kind.COMPARISON]
    expected_counts[Kind.RECORD_VISIT] = after["read_words"]
    expected_counts[Kind.LINK_TRAVERSE] = after["accepted_links"]
    _require(
        work.counts[Owner.KERNEL_DIAGNOSTIC] == tuple(expected_counts)
        and (expected_counts[Kind.COMPARISON] > 0) == reporting,
        "EV02 snapshot does not match the actual trace-word/link and retained UART-only dimensions",
    )
    return stacks, stops, after


def validate_capture(capture):
    stacks, stops, after = _validate_execution(capture)
    physical = stacks[_physical_owner(capture.scenario)]
    final_region = stacks["primary"] if capture.scenario == 15 else physical
    if capture.scenario == 18:
        _require(capture.started is None and capture.stack is None and capture.reentry is None,
                 "absent trace invented a start, stack snapshot or reentry")
        _require(all(after[name] == 0 for name in (
            "trace_entries", "completed_traces", "read_words", "accepted_links", "trace_sp", "trace_fp",
            "allocation_bottom", "root_cfa", "owner", "stop", "record_count",
            "trace_sstatus", "trace_sie", "trace_sscratch",
        )), "absent trace fabricated observed results")
        return None

    _require(type(capture.started) is GuestState and type(capture.stack) is bytes, "current trace omitted its actual before-read witness")
    entry = decode_observation(capture.started.observation, capture.scenario)
    _require(
        entry["phase"] == 2 and entry["work_status"] == 0xffffffff
        and entry["trace_entries"] == 1 and entry["report_bodies"] == entry["seizure_entries"] == 1
        and all(entry[name] == 0 for name in ("completed_traces", "read_words", "accepted_links", "reset_entries", "record_count", "stop"))
        and capture.started.work == bytes(3056) and capture.started.control == struct.pack("<I", 1)
        and _owned_sp(capture.started.sp, physical)
        and (entry["trace_sp"], entry["trace_fp"]) == (capture.started.sp, capture.started.fp)
        and not (entry["trace_sstatus"] & 2 or entry["trace_sie"] & 512),
        "trace start does not bind its real registers, first ownership or pre-read state",
    )
    _require(
        len(capture.stack) == physical[1] - physical[0]
        and all(after[name] == entry[name] for name in (
            "trace_sp", "trace_fp", "allocation_bottom", "root_cfa", "owner",
            "trace_sstatus", "trace_sie", "trace_sscratch", "first_panic_sp",
        )),
        "trace reused another stack extent or overwrote its first-entry coordinates",
    )
    unavailable = capture.scenario in (13, 14)
    owner = 0 if unavailable else {"boot": 1, "primary": 2, "thread": 3, "emergency": 4}[_physical_owner(capture.scenario)]
    root = 0 if unavailable else physical[1] - (288 if capture.scenario in (3, 4) else 0)
    _require((entry["owner"], entry["allocation_bottom"], entry["root_cfa"]) == (owner, 0 if unavailable else physical[0], root),
             "trace borrowed a reported owner or a different root CFA")
    if capture.scenario in (3, 4, 5, 13, 14):
        _require(entry["trace_sscratch"] == 0, "trap/emergency trace has no active trap sentinel")
    fault = capture.scenario in (15, 16, 17)
    if capture.scenario in (15, 16):
        _require(capture.reentry is not None, "recursive failure omitted the real second panic entry")
        repeated = _registers(capture.reentry)
        _require(repeated[32] == stops["reentry"] and _owned_sp(repeated[2], final_region), "reentry borrowed another instruction or epoch")
        if capture.scenario == 16:
            _require(repeated[2] <= after["first_panic_sp"] - 96, "direct recursion discarded a live panic capture")
    else:
        _require(capture.reentry is None, "another scenario invented recursive entry")
    if fault:
        words = 2 if capture.scenario == 17 else 0
        _require(
            (after["trace_entries"], after["completed_traces"], after["read_words"], after["accepted_links"],
             after["record_count"], after["stop"]) == (1, 0, words, 0, 0, 0),
            "fault scenario completed or continued another trace",
        )
        return None
    expected = ExpectedTrace((), "owner", 0, 0) if unavailable else walk_expected_stack(
        capture.stack, bottom=physical[0], root=root, sp=capture.started.sp,
        frame=capture.started.fp, text=capture.text,
    )
    _require(expected.stop == RECIPE_ADMISSION["cases"][capture.scenario - 1][2], "actual fixture did not exercise its declared stop boundary")
    stop_codes = {"end": 1, "boundary": 2, "limit": 3, "owner": 4, "invalid-frame": 5, "invalid-pc": 6, "nonmonotonic": 7}
    _require(
        (after["trace_entries"], after["completed_traces"], after["read_words"], after["accepted_links"],
         after["record_count"], after["stop"])
        == (1, 1, expected.word_reads, expected.advances, len(expected.frames), stop_codes[expected.stop]),
        "trace result/work differs from the independent captured-stack walk",
    )
    return expected


def validate_case(case):
    from tools.qemu_smoke_faults import _diagnostic_history_payload, _has_complete_panic_report, _has_complete_trap_context

    _require(type(case) is TraceCase and type(case.output) is str and type(case.exit_code) is int and case.exit_code == 0,
             "case lacks its real transcript and natural firmware exit")
    expected = validate_capture(case.capture)
    scenario, output = case.capture.scenario, case.output
    _require("MICROS_TEST_FAILURE" not in output, "guest emitted an explicit failure")
    if scenario == 18:
        _require(not any(prefix in output for prefix in ("MICROS_PANIC", "MICROS_TRACE_", "MICROS_DIAGNOSTIC_HISTORY")),
                 "ordinary reset invented panic/trace/history output")
        return
    _require(_has_complete_panic_report(output), "trace lost the five original contiguous panic records")
    reason = "unexpected-exception" if scenario in (3, 4, 13, 14) else "nested-trap" if scenario == 5 else "panic-trace-test"
    _require("MICROS_PANIC reason=" + reason + "\r\n" in output, "first report has the wrong fixed fixture reason")
    lines, _ = _split_output_records(output)
    machine = next(index for index, line in enumerate(lines) if line.startswith("MICROS_PANIC_MACHINE "))
    observed = decode_observation(case.capture.final.observation, scenario)
    _require(lines[machine].endswith(f" sp=0x{observed['first_panic_sp']:016x}"), "original machine record no longer matches the first captured SP")
    after_core = machine + 1
    if scenario in (3, 4, 13, 14):
        _require(_has_complete_trap_context(output, expected_origin="S", expected_scause=2), "real S-origin trap lost its original captured record")
        after_core += 1
    else:
        _require("MICROS_TRAP_CONTEXT" not in output, "another case invented a saved trap record")
    fault = scenario in (15, 16, 17)
    trace = decode_trace(output, complete=not fault)
    owner = "unavailable" if scenario in (13, 14) else _physical_owner(scenario)
    _require(trace.owner == owner and after_core < len(lines) and lines[after_core].startswith("MICROS_TRACE_BEGIN "),
             "trace changed its owner or pre-history placement")
    if fault:
        _require(trace.frames == () and trace.stop is None, "faulting trace emitted a frame or successful end")
    else:
        _require((trace.frames, trace.stop) == (expected.frames, expected.stop), "serial trace differs from the independent actual stack")
        after_core += len(trace.frames) + 2
        if scenario in (1, 2):
            _require(type(case.capture.return_sites) is tuple and len(case.capture.return_sites) == len(set(case.capture.return_sites)) == 3,
                     "ordinary chain omitted its three actual linked return sites")
            pcs = [pc for _, pc in trace.frames]
            position = 0
            for site in case.capture.return_sites:
                _require(site in pcs[position:], "ordinary trace omitted or reordered a required real C caller")
                position = pcs.index(site, position) + 1
    if fault:
        _require("MICROS_DIAGNOSTIC_HISTORY" not in output, "failed trace continued into ordinary history")
    else:
        size = struct.unpack_from("<I", case.capture.history, 12008)[0]
        _require(_diagnostic_history_payload(output) == case.capture.history[10000:10000 + size], "trace changed the retained diagnostic tail")
        _require(after_core < len(lines) and lines[after_core].startswith("MICROS_DIAGNOSTIC_HISTORY "),
                 "trace displaced or interrupted the original history tail")


class PanicTraceOracles(unittest.TestCase):
    observed = None

    def setUp(self):
        _require(type(self.observed) is tuple and len(self.observed) == 18, "oracles require all eighteen actual executions")

    def require_current(self, scenarios):
        for scenario in scenarios:
            try:
                validate_case(self.observed[scenario - 1])
            except ValueError as error:
                self.fail(f"case {scenario}: {error}")

    def test_current_chain(self):
        self.require_current((1, 2, 6, 18))

    def test_owned_epoch(self):
        self.require_current((2, 3, 4, 5, 13, 14))

    def test_frame_work(self):
        self.require_current((6, 7, 8, 9, 10, 11, 12))

    def test_terminal_precedence(self):
        self.require_current((15, 16, 17, 18))


def run_oracles(cases):
    _require(type(cases) is tuple and len(cases) == 18, "requires eighteen complete current cases")
    _require(all(type(case) is TraceCase and type(case.capture) is TraceCapture for case in cases),
             "case set contains an untyped observation")
    _require(tuple(case.capture.scenario for case in cases) == tuple(range(1, 19)), "case set is missing, duplicate or reordered")
    for case in cases:
        validate_case(case)
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
        not result.failures,
        "named current trace oracles failed:\n" + output.getvalue(),
    )
    return tuple((row[0], "PASS") for row in ORACLE_RULES), output.getvalue()
