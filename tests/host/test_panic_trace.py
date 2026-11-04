"""Independent PLAT03 trace and corrected link-precedence regressions."""

import ast
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import unittest
from unittest.mock import patch

from tests.host.evidence_authority_inventory import current_inventory
from tests.host.evidence_authority_types import AuthorityClass, fact_set_digest


ROOT = Path(__file__).resolve().parents[2]


class PanicTraceAuthorityTest(unittest.TestCase):
    def test_exact_nine_authorities_preserve_the_two_prior_claims(self):
        source = "tests/host/panic_trace_source.py"
        evidence = "tests/host/panic_trace_evidence.py"
        expected = {
            evidence + ":design-anchor:PLAT03": AuthorityClass.TRANSPORT_IDENTITY,
            source + ":source-membership:PLAT03": AuthorityClass.PERMANENT_INVARIANT,
            source + ":artifact-contract:PLAT03-recipes": AuthorityClass.ARTIFACT_CONTRACT,
            source + ":effect-contract:PLAT03-fatal": AuthorityClass.PERMANENT_INVARIANT,
            source + ":source-projection:PLAT03": AuthorityClass.TRANSPORT_IDENTITY,
        }
        for name in (
            "panic-trace-current-chain", "panic-trace-owned-epoch",
            "panic-trace-frame-work", "panic-trace-terminal-precedence",
        ):
            expected[evidence + ":baseline-oracle:" + name] = AuthorityClass.BASELINE_ORACLE
        actual = {
            fact.key: fact.authority_class
            for fact in current_inventory(ROOT).facts
            if fact.family == "panic-trace-baseline"
        }
        self.assertEqual(expected, actual)

        from tests.host.diagnostic_history_authority import task_definition

        _, _, _, registry = task_definition(ROOT)
        self.assertEqual(
            {"COPY01-runtime-memory", "PLAT01-diagnostic-history"},
            {claim.transition_id for claim in registry.claims},
        )

    def test_all_prior_complete_records_and_family_fingerprints_are_preserved(self):
        from tests.host.panic_trace_evidence import DESIGN_PINS

        source = subprocess.check_output(
            ("git", "show", DESIGN_PINS["design_commit"] + ":tests/host/evidence_authority_inventory.py"),
            cwd=ROOT, text=True, timeout=30,
        )
        declaration, = (
            node for node in ast.parse(source).body
            if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == "DECLARED_FAMILY_FACT_DIGESTS"
                for target in node.targets
            )
        )
        expected = ast.literal_eval(declaration.value)
        inventory = current_inventory(ROOT)
        self.assertEqual(13, len(expected))
        self.assertEqual(3929, sum(fact.family in expected for fact in inventory.facts))
        for family, fingerprint in expected.items():
            self.assertEqual(
                fingerprint, fact_set_digest(fact for fact in inventory.facts if fact.family == family),
                family,
            )

    def test_oracle_properties_may_share_cases_but_not_test_or_identity_ownership(self):
        from tests.host.panic_trace_evidence import ORACLE_RULES
        from tests.host.panic_trace_source import checked_oracle_rules

        self.assertEqual(ORACLE_RULES, checked_oracle_rules(ORACLE_RULES))
        for index, replacement in (
            (0, (ORACLE_RULES[1][0], *ORACLE_RULES[0][1:])),
            (0, (ORACLE_RULES[0][0], ORACLE_RULES[1][1], *ORACLE_RULES[0][2:])),
            (0, (*ORACLE_RULES[0][:5], (True, 2, 6, 18))),
            (0, (*ORACLE_RULES[0][:5], (1, 1, 2, 6, 18))),
            (2, (*ORACLE_RULES[2][:5], (6, 7, 8, 9, 10, 11))),
        ):
            rules = list(ORACLE_RULES)
            rules[index] = replacement
            with self.subTest(rule=replacement), self.assertRaises(ValueError):
                checked_oracle_rules(tuple(rules))

    def test_declarations_reject_duplicate_binding_keys_and_candidate_fixture_overlap(self):
        from tests.host.panic_trace_source import EVIDENCE_PATH, SOURCE_PATH, declaration_facts

        with tempfile.TemporaryDirectory(prefix="micros-plat03-declarations-") as directory:
            root = Path(directory)
            for name in (SOURCE_PATH, EVIDENCE_PATH):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes((ROOT / name).read_bytes())
            path = root / SOURCE_PATH
            source = path.read_text()
            for changed in (
                source + "\nSOURCE_ROLES = {}\n",
                source.replace('    "candidate_paths": (\n', '    "candidate_paths": (),\n    "candidate_paths": (\n', 1),
                source.replace('"kernel/diagnostic/panic.c"', '"tests/host/panic_trace_test.c"', 1),
                source.replace('"pc_bytes_before_cfa": 8', '"pc_bytes_before_cfa": False', 1),
                source.replace('"pc_bytes_before_cfa": 8', '"pc_bytes_before_cfa": -8', 1),
            ):
                path.write_text(changed)
                with self.assertRaises(ValueError):
                    declaration_facts(root, SOURCE_PATH)
            with self.assertRaises(ValueError):
                declaration_facts(ROOT, "tests/host/test_panic_trace.py")


class PanicTraceReferenceTest(unittest.TestCase):
    BOTTOM = 0x81000000
    ROOT_CFA = BOTTOM + 16384
    SP = BOTTOM + 64
    FRAME = BOTTOM + 128
    TEXT = (0x80200000, 0x80300000)

    def stack(self, records):
        storage = bytearray(16384)
        for frame, pc, previous in records:
            struct.pack_into("<QQ", storage, frame - self.BOTTOM - 16, previous, pc)
        return bytes(storage)

    def walk(self, records, *, frame=None, root=None):
        from tests.host.panic_trace_evidence import walk_expected_stack

        return walk_expected_stack(
            self.stack(records), bottom=self.BOTTOM,
            root=self.ROOT_CFA if root is None else root,
            sp=self.SP, frame=self.FRAME if frame is None else frame, text=self.TEXT,
        )

    def test_successor_overlap_obeys_the_first_failed_guard_without_another_read(self):
        from tests.host.panic_trace_source import RECIPE_ADMISSION

        pc = self.TEXT[0] + 16
        for previous, expected in (
            (0, "end"), (0x40000000, "nonmonotonic"),
            (self.FRAME, "nonmonotonic"), (self.FRAME + 1, "invalid-frame"),
            (self.ROOT_CFA + 16, "invalid-frame"),
        ):
            with self.subTest(previous=previous):
                result = self.walk(((self.FRAME, pc, previous),))
                self.assertEqual(((self.FRAME, pc),), result.frames)
                self.assertEqual((expected, 2, 0), (result.stop, result.word_reads, result.advances))
                if previous == 0x40000000:
                    self.assertEqual(result.stop, RECIPE_ADMISSION["cases"][7][2])

    def test_root_boundary_ignores_saved_predecessor_but_not_an_invalid_pc(self):
        root = self.ROOT_CFA - 288
        pc = self.TEXT[0] + 16
        for previous in (0x40000000, root, root + 32, 1):
            with self.subTest(previous=previous):
                result = self.walk(((root, pc, previous),), frame=root, root=root)
                self.assertEqual(("boundary", 2, 0), (result.stop, result.word_reads, result.advances))
                self.assertEqual(((root, pc),), result.frames)
        result = self.walk(((root, 0, root + 32),), frame=root, root=root)
        self.assertEqual(((), "invalid-pc", 2, 0), (result.frames, result.stop, result.word_reads, result.advances))

    def test_depth_cap_does_not_override_null_bad_successor_or_root_and_never_reads_pair_33(self):
        frames = [self.FRAME + 32 * index for index in range(33)]
        records = [
            (frame, self.TEXT[0] + 2 * (index + 1), frames[index + 1] if index < 32 else 0)
            for index, frame in enumerate(frames)
        ]
        for previous, expected in (
            (frames[32], "limit"), (0, "end"), (0x40000000, "nonmonotonic"),
            (frames[31] + 1, "invalid-frame"), (self.ROOT_CFA + 16, "invalid-frame"),
        ):
            changed = list(records)
            changed[31] = (*records[31][:2], previous)
            changed[32] = (frames[32], 0, 0)
            with self.subTest(previous=previous):
                result = self.walk(changed)
                self.assertEqual((expected, 64, 31, 32), (
                    result.stop, result.word_reads, result.advances, len(result.frames),
                ))
        records[30] = (*records[30][:2], self.ROOT_CFA)
        records[31] = (self.ROOT_CFA, records[31][1], 0x40000000)
        result = self.walk(records)
        self.assertEqual(("boundary", 64, 31, 32), (
            result.stop, result.word_reads, result.advances, len(result.frames),
        ))

    def test_invalid_initial_frame_never_reads_stack_and_null_ends_after_valid_bounds(self):
        for frame in (self.BOTTOM, self.SP, self.FRAME + 1, self.ROOT_CFA + 16):
            with self.subTest(frame=frame):
                result = self.walk((), frame=frame)
                self.assertEqual(((), "invalid-frame", 0, 0), (
                    result.frames, result.stop, result.word_reads, result.advances,
                ))
        result = self.walk((), frame=0)
        self.assertEqual(((), "end", 0, 0), (result.frames, result.stop, result.word_reads, result.advances))


class PanicTraceObservationTest(unittest.TestCase):
    def test_observation_rejects_truncation_foreign_values_and_impossible_progress(self):
        from tests.host.panic_trace_evidence import decode_observation

        data = bytearray(192)
        struct.pack_into("<4I", data, 0, 1, 1, 3, 0)
        struct.pack_into("<7Q", data, 16, 1, 1, 1, 1, 8, 3, 1)
        struct.pack_into("<5Q", data, 72, 0x81003f00, 0x81003d00, 0x81003e00, 0x81000000, 0x81004000)
        struct.pack_into("<3I", data, 112, 1, 2, 4)
        struct.pack_into("<2I", data, 176, 0, 1)
        observed = decode_observation(bytes(data), 1)
        self.assertEqual((8, 3, 4, 2), tuple(observed[name] for name in (
            "read_words", "accepted_links", "record_count", "stop",
        )))
        for offset, format_, value in (
            (0, "<I", 2), (4, "<I", 2), (8, "<I", 5), (12, "<I", 6),
            (16, "<Q", 0), (32, "<Q", 2), (40, "<Q", 2), (48, "<Q", 65),
            (56, "<Q", 32), (64, "<Q", 2), (112, "<I", 5), (116, "<I", 8),
            (120, "<I", 33), (124, "<I", 1), (184, "<Q", 1),
        ):
            changed = bytearray(data)
            struct.pack_into(format_, changed, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                decode_observation(bytes(changed), 1)
        for changed in (bytes(data[:-1]), bytes(data) + b"\0", data):
            with self.assertRaises(ValueError):
                decode_observation(changed, 1)
        for scenario in (True, 0, 19):
            with self.assertRaises(ValueError):
                decode_observation(bytes(data), scenario)

    def test_work_snapshot_binds_exact_producer_capture_owner_and_all_dimensions(self):
        from tests.host.panic_trace_evidence import decode_work
        from tests.host.work_count_types import Kind, Owner

        counts = [0] * (27 * 14)
        counts[14] = 100000
        counts[15] = 64
        counts[17] = 31
        data = struct.pack("<IIQQII378Q", 1, 0, 0x504C41543033000c, 1, 2, 0, *counts)
        observed = decode_work(data, 12)
        self.assertEqual(64, observed.counts[Owner.KERNEL_DIAGNOSTIC][Kind.RECORD_VISIT])
        self.assertEqual(31, observed.counts[Owner.KERNEL_DIAGNOSTIC][Kind.LINK_TRAVERSE])
        for offset, format_, value in (
            (0, "<I", 2), (4, "<I", 1), (8, "<Q", 0x504C415430330001),
            (16, "<Q", 2), (24, "<I", 3), (28, "<I", 1), (32, "<Q", 1),
        ):
            changed = bytearray(data)
            struct.pack_into(format_, changed, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                decode_work(bytes(changed), 12)
        for changed in (data[:-8], data + bytes(8), bytearray(data)):
            with self.assertRaises(ValueError):
                decode_work(changed, 12)

    def test_trace_records_are_complete_contiguous_and_distinct_from_legacy_panic_records(self):
        from tests.host.panic_trace_evidence import decode_trace

        header = "MICROS_TRACE_BEGIN version=1 owner=boot limit=0x0000000000000020\r\n"
        row = "MICROS_TRACE_FRAME index=0x0000000000000000 pc=0x0000000080200010 fp=0x0000000081000080\r\n"
        ending = "MICROS_TRACE_END count=0x0000000000000001 stop=end\r\n"
        output = "earlier-record\r\n" + header + row + ending + "later-record\r\n"
        parsed = decode_trace(output)
        self.assertEqual(("boot", ((0x81000080, 0x80200010),), "end"), (parsed.owner, parsed.frames, parsed.stop))
        for changed in (
            output.replace("version=1", "version=2"),
            output.replace("owner=boot", "owner=foreign"),
            output.replace("limit=0x0000000000000020", "limit=0x0000000000000040"),
            output.replace("index=0x0000000000000000", "index=0x0000000000000001"),
            output.replace("count=0x0000000000000001", "count=0x0000000000000002"),
            output.replace(row, "intervening-record\r\n" + row),
            output.replace(header, header + header),
            output.replace(ending, ending + ending),
            header + row + ending.rstrip(),
            header + "MICROS_TRACE_UNDECLARED\r\n" + ending,
        ):
            with self.subTest(output=changed[:90]), self.assertRaises(ValueError):
                decode_trace(changed)
        partial = decode_trace(header, complete=False)
        self.assertEqual(("boot", (), None), (partial.owner, partial.frames, partial.stop))
        with self.assertRaises(ValueError):
            decode_trace(header)
        with self.assertRaises(ValueError):
            decode_trace(header + "MICROS_TRACE_END count=", complete=False)


class PanicTraceRecipeTest(unittest.TestCase):
    def test_baseline_and_current_recipe_extensions_recover_the_complete_fixed_input(self):
        from tests.host.panic_trace_source import extend_cmake, fixed_source, project_trace_cmake

        source = fixed_source("CMakeLists.txt")
        for production in (False, True):
            with self.subTest(production=production):
                current = extend_cmake(source, production=production)
                self.assertEqual(source, project_trace_cmake(current, production=production))
                self.assertIn("tests/qemu/panic_trace_kernel.S", current)
                self.assertEqual(production, "kernel/diagnostic/panic_trace_core.c" in current)
                self.assertEqual(production, "-fno-optimize-sibling-calls" in current)
                for changed in (
                    current.replace('tools/check_panic_traces.py"', 'tools/check_panic_traces.py" --arbitrary'),
                    current.replace(" -Werror", " -Wno-error", 1),
                    current + '\nset(MICROS_EXTRA_UNREVIEWED ON)\n',
                ):
                    with self.assertRaises(ValueError):
                        project_trace_cmake(changed, production=production)
                with self.assertRaises(ValueError):
                    project_trace_cmake(current, production=not production)
        with self.assertRaises(ValueError):
            extend_cmake(source, production=1)

    def test_preset_additions_preserve_every_original_workflow_and_reject_duplicate_ownership(self):
        from tests.host.panic_trace_source import extend_presets, fixed_source, project_trace_presets

        source = fixed_source("CMakePresets.json")
        current = extend_presets(source)
        self.assertEqual(source, project_trace_presets(current))
        before, after = json.loads(source), json.loads(current)
        for field in ("configurePresets", "buildPresets", "workflowPresets"):
            self.assertEqual(before[field], after[field][:-1])
        self.assertEqual("test-qemu-panic-trace", after["workflowPresets"][-1]["name"])
        for changed in (
            current.replace('"MICROS_BUILD_PANIC_TRACE_TEST": "ON"', '"MICROS_BUILD_PANIC_TRACE_TEST": "OFF"'),
            current.replace('"name": "test-qemu-smoke"', '"name": "test-qemu-omitted"', 1),
            current.replace('"targets": [\n        "test-qemu-panic-trace"', '"targets": [\n        "test-qemu-panic-trace",\n        "test-qemu-panic-trace"'),
        ):
            with self.assertRaises(ValueError):
                project_trace_presets(changed)


class PanicTraceSourceTest(unittest.TestCase):
    def test_every_observation_overlay_recovers_the_complete_original_source(self):
        from tests.host.panic_trace_source import (
            SOURCE_PROJECTION, fixed_source, observe_source, project_observations,
        )

        for relative, _ in SOURCE_PROJECTION["test_overlays"]:
            with self.subTest(relative=relative):
                source = fixed_source(relative)
                observed = observe_source(relative, source)
                self.assertEqual(source, project_observations(relative, observed))
                with self.assertRaises(ValueError):
                    observe_source(relative, observed)
                with self.assertRaises(ValueError):
                    project_observations(relative, observed + "\nint unreviewed_state;\n")
                with self.assertRaises(ValueError):
                    project_observations(relative, observed.replace("MICROS_BUILD_PANIC_TRACE_TEST", "MICROS_UNREVIEWED", 1))

    def test_boot_hook_keeps_all_original_panic_source_locations_and_the_terminal_call(self):
        from tests.host.panic_trace_source import fixed_source, observe_source

        relative = "kernel/boot/boot_component_tests.c"
        source = fixed_source(relative)
        observed = observe_source(relative, source)
        original_locations = [(index, line) for index, line in enumerate(source.splitlines()) if "MICROS_PANIC(" in line]
        current_locations = [(index, line) for index, line in enumerate(observed.splitlines()) if "MICROS_PANIC(" in line]
        self.assertEqual(original_locations, current_locations)
        self.assertEqual(1, observed.count("micros_terminal_test_run(boot->hart_id);"))
        self.assertEqual(1, observed.count("micros_panic_trace_test_run(boot->hart_id);"))


class PanicTraceRemoteTest(unittest.TestCase):
    class Connection:
        def __init__(self, replies=b""):
            self.replies = bytearray(replies)
            self.sent = []

        def settimeout(self, timeout):
            self.timeout = timeout

        def sendall(self, data):
            self.sent.append(data)

        def recv(self, size):
            value = bytes(self.replies[:min(size, 1)])
            del self.replies[:len(value)]
            return value

    @staticmethod
    def binding(*, baseline=False):
        from tools.check_panic_traces import ImageBinding

        return ImageBinding(
            "0" * 64,
            (
                ("micros_panic_trace_test_scenario", 0x82000000, 4),
                ("micros_panic_trace_test_observation", 0x82000008, 192),
                ("micros_panic_trace_test_work_snapshot", 0x82000100, 3056),
                ("panic_control", 0x82000d00, 4),
            ),
            (
                ("kernel_main", 0x80200000, 4), ("armed", 0x80200010, 2),
                ("started", 0x80200020, 2), ("error", 0x80200030, 4),
                ("reset", 0x80200040, 4), ("reentry", 0x80200050, 2),
            ),
            (
                ("boot", 0x81000000, 0x81004000), ("primary", 0x81004000, 0x81008000),
                ("thread", 0x81204000, 0x81208000), ("emergency", 0x81008000, 0x81009000),
            ),
            (0x80200000, 0x80300000), not baseline,
        )

    def test_selector_is_once_at_kernel_main_even_through_the_raw_exchange(self):
        from tools.check_panic_traces import Remote, encode_packet

        connection = self.Connection(b"+" + encode_packet(b"OK"))
        remote = Remote(connection, self.binding(), 1, time.monotonic() + 8)
        with self.assertRaises(ValueError):
            remote.select_scenario()
        self.assertEqual([], connection.sent)
        remote.stopped_at = "kernel_main"
        remote.exchange(b"M82000000,4:01000000")
        for request in (
            b"M82000000,4:01000000", b"M82000000,4:02000000", b"P8=0000000000000000",
            b"m10000005,1", b"m82001000,8", b"m81000000,8", b"p100", b"qRcmd,00",
        ):
            with self.subTest(request=request), self.assertRaises(ValueError):
                remote.exchange(request)
        self.assertEqual(2, len(connection.sent))

    def test_only_the_selected_case_can_arm_trace_and_reentry_breakpoints(self):
        from tools.check_panic_traces import Remote, encode_packet

        for scenario, baseline, stop in (
            (18, False, "started"), (14, False, "reentry"), (16, True, "reentry"),
        ):
            connection = self.Connection()
            remote = Remote(connection, self.binding(baseline=baseline), scenario, time.monotonic() + 8)
            with self.subTest(case=scenario, baseline=baseline), self.assertRaises(ValueError):
                remote.breakpoint(stop, True)
            self.assertEqual([], connection.sent)
        connection = self.Connection(b"+" + encode_packet(b"OK"))
        remote = Remote(connection, self.binding(), 16, time.monotonic() + 8)
        remote.breakpoint("reentry", True)
        self.assertEqual(encode_packet(b"Z1,80200050,2"), connection.sent[0])

    def test_stack_reads_require_the_before_read_stop_and_actual_expected_stack_sp(self):
        from tools.check_panic_traces import Remote

        remote = Remote(self.Connection(), self.binding(), 4, time.monotonic() + 8)
        with self.assertRaises(ValueError):
            remote.read_stack()
        remote.stopped_at = "started"
        with self.assertRaises(ValueError):
            remote.exchange(b"m81204000,8")
        with patch.object(remote, "register", return_value=0x81000100), patch.object(remote, "_memory") as memory:
            with self.assertRaises(ValueError):
                remote.read_stack()
            memory.assert_not_called()
        with patch.object(remote, "register", return_value=0x81204100) as register, patch.object(
            remote, "_memory", side_effect=lambda address, size: bytes(size),
        ) as memory:
            self.assertEqual(bytes(16384), remote.read_stack())
            register.assert_called_once_with(2)
            self.assertEqual(
                [(0x81204000 + offset, 1024) for offset in range(0, 16384, 1024)],
                [call.args for call in memory.call_args_list],
            )
        with self.assertRaises(ValueError):
            remote.exchange(b"m81208000,8")
        with self.assertRaises(ValueError):
            remote.exchange(b"m81204000,401")

    def test_resume_invalidates_memory_authority_and_errors_never_become_success(self):
        from tools.check_panic_traces import Remote

        remote = Remote(self.Connection(b"+"), self.binding(), 1, time.monotonic() + 8)
        remote.stopped_at = "reset"
        remote.stack_authority = (0x81000000, 0x81004000)
        remote.exchange(b"c", reply=False)
        self.assertIsNone(remote.stopped_at)
        self.assertIsNone(remote.stack_authority)
        with self.assertRaises(ValueError):
            remote.read_object("micros_panic_trace_test_observation")
        for response in (b"", b"-", b"$"):
            with self.subTest(response=response), self.assertRaises(ValueError):
                Remote(self.Connection(response), self.binding(), 1, time.monotonic() + 8).exchange(b"?")
        with self.assertRaises(TimeoutError):
            Remote(self.Connection(), self.binding(), 1, time.monotonic() - 1).exchange(b"?")

    def test_only_final_continue_can_omit_a_reply_and_the_mode_is_boolean(self):
        from tools.check_panic_traces import Remote

        for request, reply in ((b"?", False), (b"c", False), (b"c", 0), (b"c", "")):
            connection = self.Connection(b"+")
            remote = Remote(connection, self.binding(), 1, time.monotonic() + 8)
            with self.subTest(request=request, reply=reply), self.assertRaises(ValueError):
                remote.exchange(request, reply=reply)
            self.assertEqual([], connection.sent)


class PanicTraceArchiveTest(unittest.TestCase):
    def test_baseline_archive_has_only_exact_originals_and_immutable_guest_overlays(self):
        from tests.host.panic_trace_source import SOURCE_PROJECTION, fixed_source
        from tools.check_panic_traces import prepare_baseline, validate_baseline

        with tempfile.TemporaryDirectory(prefix="micros-plat03-external-") as directory:
            with self.assertRaises(ValueError):
                prepare_baseline(Path(directory))
            self.assertFalse((Path(directory) / "source").exists())
        (ROOT / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="micros-plat03-archive-", dir=ROOT / "build") as directory:
            source = prepare_baseline(Path(directory))
            validate_baseline(source)
            for name in (*SOURCE_PROJECTION["new_kernel_members"], *SOURCE_PROJECTION["new_arch_members"]):
                self.assertFalse((source / name).exists(), name)
            protected = source / "kernel/boot/boot_foundation.c"
            self.assertEqual(fixed_source("kernel/boot/boot_foundation.c"), protected.read_text())
            panic = source / "kernel/diagnostic/panic.c"
            saved = panic.read_bytes()
            panic.write_bytes(saved + b"\nint unreviewed_state;\n")
            with self.assertRaises(ValueError):
                validate_baseline(source)
            panic.write_bytes(saved)
            stub = source / "kernel/diagnostic/panic_trace_core.c"
            stub.write_text("void synthetic_trace(void) {}\n")
            with self.assertRaises(ValueError):
                validate_baseline(source)
            stub.unlink()
            fixture = source / "tests/qemu/panic_trace_kernel.c"
            fixture.write_text("void synthetic_fixture(void) {}\n")
            with self.assertRaises(ValueError):
                validate_baseline(source)


class PanicTraceImageTest(unittest.TestCase):
    def build_image(self, directory, *, trace):
        from tests.host.test_stack_bound_evidence import compile_fixture

        source = """
#include <stdint.h>
uint32_t micros_panic_trace_test_scenario;
_Alignas(8) unsigned char micros_panic_trace_test_observation[192];
_Alignas(8) unsigned char micros_panic_trace_test_work_snapshot[3056];
_Alignas(4) unsigned char micros_panic_trace_test_history[12012];
uint32_t panic_control;
void _start(void) {}
void kernel_main(void) {}
void micros_panic_trace_test_armed(void) {}
void micros_panic_trace_test_started(void) {}
void micros_panic_trace_test_error(void) { __asm__ volatile("wfi"); }
void micros_panic_report(void) {}
void sbi_system_reset(void) { __asm__ volatile("ecall"); }
__attribute__((noinline, disable_tail_calls)) void trace_chain_one(void) {}
__attribute__((noinline, disable_tail_calls)) void trace_chain_two(void) { trace_chain_one(); }
__attribute__((noinline, disable_tail_calls)) void trace_chain_three(void) { trace_chain_two(); }
__attribute__((noinline, disable_tail_calls)) void micros_panic_trace_test_run(void) { trace_chain_three(); }
__asm__(
    ".section .stack,\\"aw\\",@nobits\\n.balign 16\\n"
    ".globl __boot_stack_bottom\\n__boot_stack_bottom:\\n.skip 16384\\n"
    ".globl __boot_stack_top\\n__boot_stack_top:\\n"
    ".section .thread_kernel_stacks,\\"aw\\",@nobits\\n.balign 4096\\n"
    ".globl micros_thread_kernel_stacks\\nmicros_thread_kernel_stacks:\\n.skip 2097152\\n"
    ".globl micros_thread_kernel_stacks_end\\nmicros_thread_kernel_stacks_end:\\n"
    ".section .trap_stack,\\"aw\\",@nobits\\n.balign 16\\n"
    ".globl __trap_stack_bottom\\n__trap_stack_bottom:\\n.skip 16384\\n"
    ".globl __trap_stack_top\\n__trap_stack_top:\\n"
    ".section .trap_emergency_stack,\\"aw\\",@nobits\\n.balign 16\\n"
    ".globl __trap_emergency_stack_bottom\\n__trap_emergency_stack_bottom:\\n.skip 4096\\n"
    ".globl __trap_emergency_stack_top\\n__trap_emergency_stack_top:\\n"
);
"""
        if trace:
            helpers = """
.section .text,"ax",@progbits
.option push
.option norvc
.option norelax
.balign 4
.globl micros_panic_trace_frame_pointer
.type micros_panic_trace_frame_pointer,@function
micros_panic_trace_frame_pointer:
    mv a0, s0
    ret
.size micros_panic_trace_frame_pointer, . - micros_panic_trace_frame_pointer
.globl micros_panic_trace_load_word
.type micros_panic_trace_load_word,@function
micros_panic_trace_load_word:
    la t0, micros_panic_trace_test_scenario
    lw t0, 0(t0)
    li t1, 15
    beq t0, t1, .Lfixture_fault
    ld a0, 0(a0)
    ret
.Lfixture_fault:
    li a0, 0
.globl micros_panic_trace_test_load_fault
micros_panic_trace_test_load_fault:
    ld a0, 0(a0)
    ret
.size micros_panic_trace_load_word, . - micros_panic_trace_load_word
.option pop
"""
            source += "void micros_panic_trace_report(void) {}\n__asm__(" + json.dumps(helpers) + ");\n"
        _, obj = compile_fixture(directory, source)
        output = directory / "fixture.elf"
        result = subprocess.run(
            (os.environ["MICROS_LD_LLD"], "-m", "elf64lriscv", "--no-relax",
             "-T", str(ROOT / "arch/riscv64/kernel.ld"), str(obj), "-o", str(output)),
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(0, result.returncode, result.stderr)
        return output

    def test_actual_elf_binding_requires_all_typed_objects_real_stops_and_exact_trace_absence(self):
        from dataclasses import replace
        from tools import check_panic_traces as controller
        from tools.user_elf_parse import load_elf

        with tempfile.TemporaryDirectory(prefix="micros-plat03-elf-") as directory:
            root = Path(directory)
            for trace in (False, True):
                selected = root / str(int(trace))
                selected.mkdir()
                path = self.build_image(selected, trace=trace)
                binding = controller.bind_image(path, baseline=not trace)
                self.assertEqual(trace, binding.trace_present)
                self.assertEqual({"boot", "primary", "emergency", "thread"}, {row[0] for row in binding.stacks})
                self.assertEqual(192, next(size for name, _, size in binding.objects if name.endswith("_observation")))
                with self.assertRaises(ValueError):
                    controller.bind_image(path, baseline=trace)
                image = load_elf(path)
                observation, = (symbol for symbol in image.symbols if symbol.name == "micros_panic_trace_test_observation")
                for changed in (
                    replace(observation, size=104),
                    replace(observation, value=observation.value + 1),
                    replace(observation, symbol_type=2),
                ):
                    altered = replace(image, symbols=tuple(changed if symbol == observation else symbol for symbol in image.symbols))
                    with patch.object(controller, "load_elf", return_value=altered), self.assertRaises(ValueError):
                        controller.bind_image(path, baseline=not trace)

    def test_required_callers_bind_actual_linked_return_sites_not_just_function_names(self):
        from tools import check_panic_traces as controller
        from tools.user_elf_parse import load_elf

        with tempfile.TemporaryDirectory(prefix="micros-plat03-return-sites-") as directory:
            path = self.build_image(Path(directory), trace=False)
            sites = controller.fixture_return_sites(path)
            image = load_elf(path)
            self.assertEqual(3, len(set(sites)))
            for site, caller in zip(sites, ("trace_chain_two", "trace_chain_three", "micros_panic_trace_test_run")):
                symbol, = (value for value in image.symbols if value.name == caller)
                self.assertLess(symbol.value, site)
                self.assertLessEqual(site, symbol.value + symbol.size)
            missing = tuple(value for value in image.symbols if value.name != "trace_chain_one")
            from dataclasses import replace

            with patch.object(controller, "load_elf", return_value=replace(image, symbols=missing)), self.assertRaises(ValueError):
                controller.fixture_return_sites(path)

    def test_trace_helpers_reject_wrong_registers_addresses_selector_and_fault_instruction(self):
        from tools import check_panic_traces as controller
        from tools.user_elf_parse import load_elf

        with tempfile.TemporaryDirectory(prefix="micros-plat03-load-") as directory:
            path = self.build_image(Path(directory), trace=True)
            image = load_elf(path)
            controller.bind_image(path, baseline=False)
            for name, instruction, replacement in (
                ("micros_panic_trace_frame_pointer", 0, 0x00048513),
                ("micros_panic_trace_frame_pointer", 0, 0xff010113),
                ("micros_panic_trace_load_word", 2, 0x0042a283),
                ("micros_panic_trace_load_word", 5, 0x0005b503),
                ("micros_panic_trace_load_word", 7, 0x00800513),
            ):
                symbol, = (value for value in image.symbols if value.name == name and value.symbol_type == 2)
                section = image.sections[symbol.section_index]
                data = bytearray(image.data)
                struct.pack_into("<I", data, section.offset + symbol.value - section.address + 4 * instruction, replacement)
                path.write_bytes(data)
                with self.subTest(helper=name, instruction=instruction), self.assertRaises(ValueError):
                    controller.bind_image(path, baseline=False)


class PanicTraceCaptureTest(unittest.TestCase):
    @staticmethod
    def case(*, baseline=False):
        from tests.host.panic_trace_evidence import GuestState, TraceCapture, TraceCase

        bottom, top, sp, fp = 0x81000000, 0x81004000, 0x81000200, 0x81000400
        pc = 0x80200010
        armed = bytearray(192)
        struct.pack_into("<4I", armed, 0, 1, 7, 1, 0xffffffff)
        started = bytearray(armed)
        struct.pack_into("<I", started, 8, 2)
        struct.pack_into("<3Q", started, 16, 1, 1, 1)
        struct.pack_into("<5Q", started, 72, 0x81001000, sp, fp, bottom, top)
        struct.pack_into("<I", started, 112, 1)
        final = bytearray(armed if baseline else started)
        struct.pack_into("<2I", final, 8, 3, 0)
        struct.pack_into("<2Q", final, 16, 1, 1)
        struct.pack_into("<Q", final, 64, 1)
        struct.pack_into("<Q", final, 72, 0x81001000)
        struct.pack_into("<2I", final, 176, 0, 1)
        if not baseline:
            struct.pack_into("<2Q", final, 40, 1, 2)
            struct.pack_into("<2I", final, 116, 5, 1)
        counts = [0] * (27 * 14)
        counts[14] = 1000
        counts[15] = 0 if baseline else 2
        work = struct.pack("<IIQQII378Q", 1, 0, 0x504C415430330007, 1, 2, 0, *counts)
        stack = bytearray(16384)
        struct.pack_into("<QQ", stack, fp - bottom - 16, fp + 1, pc)
        first = GuestState(bytes(armed), bytes(3056), bytes(4), 0x81002000, 0x81002100)
        entry = None if baseline else GuestState(bytes(started), bytes(3056), struct.pack("<I", 1), sp, fp)
        last = GuestState(bytes(final), work, struct.pack("<I", 2), 0x81000800, 0x81000900)
        capture = TraceCapture(
            7, first, entry, last, bytes(12012), None if baseline else bytes(stack),
            ((2, last.sp), (8, last.fp), (10, 0), (11, 1), (16, 0), (17, 0x53525354), (32, 0x80200080)),
            None,
            (("boot", bottom, top), ("primary", 0x82000000, 0x82004000),
             ("emergency", 0x82004000, 0x82005000), ("thread", 0x83000000, 0x83004000)),
            (0x80200000, 0x80300000), (("reset", 0x80200080, 4), ("reentry", 0x80200090, 2)), (),
        )
        output = (
            "MICROS_PANIC reason=panic-trace-test\r\n"
            "MICROS_PANIC_BUILD version=0.1.0\r\n"
            "MICROS_PANIC_SOURCE file=tests/qemu/panic_trace_kernel.c line=0x0000000000000001\r\n"
            "MICROS_PANIC_HART mode=S id=0x0000000000000000\r\n"
            "MICROS_PANIC_MACHINE sstatus=0x0000000000000000 scause=0x0000000000000000 "
            "stval=0x0000000000000000 sepc=0x0000000000000000 ra=0x0000000080200020 sp=0x0000000081001000\r\n"
        )
        if not baseline:
            output += (
                "MICROS_TRACE_BEGIN version=1 owner=boot limit=0x0000000000000020\r\n"
                "MICROS_TRACE_FRAME index=0x0000000000000000 pc=0x0000000080200010 fp=0x0000000081000400\r\n"
                "MICROS_TRACE_END count=0x0000000000000001 stop=invalid-frame\r\n"
            )
        output += "MICROS_DIAGNOSTIC_HISTORY state=retained bytes=0x0000000000000000\r\n"
        return TraceCase(capture, output, 0)

    def test_current_trace_requires_real_snapshot_register_work_and_transcript_agreement(self):
        from dataclasses import replace
        from tests.host.panic_trace_evidence import validate_case

        case = self.case()
        validate_case(case, baseline=False)
        changed_count = bytearray(case.capture.final.observation)
        struct.pack_into("<Q", changed_count, 48, 3)
        for invalid in (
            replace(case, exit_code=1),
            replace(case, output=case.output.replace("stop=invalid-frame", "stop=nonmonotonic")),
            replace(case, output=case.output.replace("pc=0x0000000080200010", "pc=0x0000000080200012")),
            replace(case, capture=replace(case.capture, started=None)),
            replace(case, capture=replace(case.capture, final=replace(case.capture.final, control=bytes(4)))),
            replace(case, capture=replace(case.capture, final=replace(case.capture.final, observation=bytes(changed_count)))),
            replace(case, capture=replace(case.capture, registers=tuple(
                (number, 0 if number == 17 else value) for number, value in case.capture.registers
            ))),
        ):
            with self.subTest(case=invalid.exit_code), self.assertRaises(ValueError):
                validate_case(invalid, baseline=False)

    def test_genuine_baseline_absence_cannot_be_a_current_trace_or_an_invented_start(self):
        from dataclasses import replace
        from tests.host.panic_trace_evidence import validate_case

        baseline = self.case(baseline=True)
        validate_case(baseline, baseline=True)
        with self.assertRaises(ValueError):
            validate_case(baseline, baseline=False)
        with self.assertRaises(ValueError):
            validate_case(replace(baseline, capture=replace(baseline.capture, started=self.case().capture.started)), baseline=True)

    def test_existing_history_decoder_accepts_only_one_complete_trace_without_weakening_legacy_records(self):
        from tools.qemu_smoke_faults import _diagnostic_history_payload, _has_complete_panic_report

        baseline, current = self.case(baseline=True).output, self.case().output
        self.assertEqual(b"", _diagnostic_history_payload(baseline))
        self.assertTrue(_has_complete_panic_report(current))
        self.assertEqual(b"", _diagnostic_history_payload(current))
        for changed in (
            current.replace("limit=0x0000000000000020", "limit=0x0000000000000040"),
            current.replace("index=0x0000000000000000", "index=0x0000000000000001"),
            current.replace("count=0x0000000000000001", "count=0x0000000000000002"),
            current.replace("owner=boot", "owner=unavailable"),
            current.replace("MICROS_TRACE_FRAME ", "intervening\r\nMICROS_TRACE_FRAME "),
            current.replace("MICROS_TRACE_END ", "MICROS_TRACE_UNDECLARED "),
            current + "MICROS_TRACE_END count=0x0000000000000001 stop=end\r\n",
            current.replace("MICROS_PANIC_BUILD version=0.1.0\r\n", ""),
        ):
            with self.subTest(output=changed[-100:]):
                self.assertIsNone(_diagnostic_history_payload(changed))


class PanicTraceOracleTest(unittest.TestCase):
    @staticmethod
    def baseline_cases():
        from dataclasses import replace

        original = PanicTraceCaptureTest.case(baseline=True)
        regions = {name: (bottom, top) for name, bottom, top in original.capture.stacks}
        owners = (
            "boot", "boot", "primary", "thread", "emergency", "boot",
            "boot", "boot", "boot", "boot", "boot", "boot",
            "thread", "thread", "boot", "boot", "boot", "boot",
        )
        cases = []
        for scenario, owner in enumerate(owners, 1):
            bottom, _ = regions[owner]
            reporting = int(scenario != 18)
            armed = bytearray(original.capture.armed.observation)
            final = bytearray(original.capture.final.observation)
            struct.pack_into("<I", armed, 4, scenario)
            struct.pack_into("<I", final, 4, scenario)
            struct.pack_into("<2Q", final, 16, reporting, reporting)
            struct.pack_into("<Q", final, 72, bottom + 0x900 if reporting else 0)
            struct.pack_into("<I", final, 180, reporting)
            work = bytearray(original.capture.final.work)
            struct.pack_into("<Q", work, 8, 0x504C415430330000 | scenario)
            if not reporting:
                work[32:] = bytes(27 * 14 * 8)
            last = replace(
                original.capture.final, observation=bytes(final), work=bytes(work),
                control=struct.pack("<I", 2 if reporting else 0), sp=bottom + 0x400, fp=bottom + 0x500,
            )
            reason = (
                "unexpected-exception" if scenario in (3, 4, 13, 14)
                else "nested-trap" if scenario == 5 else "panic-trace-test"
            )
            output = original.output.replace("panic-trace-test", reason).replace(
                "sp=0x0000000081001000", f"sp=0x{bottom + 0x900:016x}",
            )
            if scenario in (3, 4, 13, 14):
                trap = (
                    "MICROS_TRAP_CONTEXT origin=S sstatus=0x0000000000000100 scause=0x0000000000000002 "
                    "stval=0x0000000000000000 sepc=0x0000000080200010 ra=0x0000000080200020 "
                    "sp=0x0000000081002000\r\n"
                )
                output = output.replace("MICROS_DIAGNOSTIC_HISTORY ", trap + "MICROS_DIAGNOSTIC_HISTORY ")
            cases.append(replace(
                original, output=output if reporting else "",
                capture=replace(
                    original.capture, scenario=scenario,
                    armed=replace(original.capture.armed, observation=bytes(armed)), final=last,
                    registers=tuple(
                        (number, {2: last.sp, 8: last.fp, 11: reporting}.get(number, value))
                        for number, value in original.capture.registers
                    ),
                    return_sites=(0x80200010, 0x80200020, 0x80200030) if scenario in (1, 2) else (),
                ),
            ))
        return tuple(cases)

    def test_all_four_oracles_discriminate_complete_real_shaped_baseline_absence(self):
        from tests.host.panic_trace_evidence import ORACLE_RULES, run_oracles

        cases = self.baseline_cases()
        results, output = run_oracles(cases, baseline=True)
        self.assertEqual(tuple((row[0], "EXPECTED_FAILURE") for row in ORACLE_RULES), results)
        self.assertIn("Ran 4 tests", output)
        for invalid in (cases[:-1], (*cases[:-1], cases[0]), tuple(reversed(cases))):
            with self.assertRaises(ValueError):
                run_oracles(invalid, baseline=True)
        with self.assertRaises(ValueError):
            run_oracles(cases, baseline=False)

    def test_missing_property_failure_or_skipped_oracle_cannot_qualify_as_baseline_red(self):
        from tests.host.panic_trace_evidence import PanicTraceOracles, run_oracles

        cases = self.baseline_cases()
        for replacement in (lambda self: None, unittest.skip("missing measurement")(lambda self: None)):
            with patch.object(PanicTraceOracles, "test_owned_epoch", replacement), self.assertRaises(ValueError):
                run_oracles(cases, baseline=True)

        def broken_observation(self):
            raise RuntimeError("broken observation")

        with patch.object(PanicTraceOracles, "test_owned_epoch", broken_observation):
            with self.assertRaises(ValueError):
                run_oracles(cases, baseline=True)


class PanicTraceControllerTest(unittest.TestCase):
    def test_cli_has_only_current_and_fixed_baseline_and_never_promotes_execution_errors(self):
        from contextlib import redirect_stderr
        import io
        from tools import check_panic_traces as controller
        from tools.check_panic_traces import main

        with patch.object(controller, "execute", return_value=0) as execute:
            self.assertEqual(0, main([]))
            execute.assert_called_once_with(False)
        with patch.object(controller, "execute", return_value=1) as execute:
            self.assertEqual(1, main(["--baseline"]))
            execute.assert_called_once_with(True)
        with redirect_stderr(io.StringIO()), patch.object(controller, "execute") as execute:
            for arguments in (["--base"], ["--case", "1"], ["--timeout", "60"], ["--kernel", "other.elf"]):
                with self.assertRaises(SystemExit) as result:
                    main(arguments)
                self.assertEqual(2, result.exception.code)
            execute.assert_not_called()
        errors = io.StringIO()
        with redirect_stderr(errors), patch.object(controller, "execute", side_effect=ValueError("broken target")):
            self.assertEqual(2, main(["--baseline"]))
        self.assertIn("MICROS_PLAT03_EXECUTION_ERROR", errors.getvalue())

    def test_record_preserves_complete_optional_stack_and_all_raw_observation_objects(self):
        from tools.check_panic_traces import case_record

        for case in (PanicTraceCaptureTest.case(), PanicTraceCaptureTest.case(baseline=True)):
            record = json.loads(json.dumps(case_record(case)))
            self.assertEqual(case.output, record["output"])
            self.assertEqual(case.capture.history, bytes.fromhex(record["capture"]["history"]))
            for name in ("armed", "started", "final"):
                expected, actual = getattr(case.capture, name), record["capture"][name]
                if expected is None:
                    self.assertIsNone(actual)
                    continue
                for field in ("observation", "work", "control"):
                    self.assertEqual(getattr(expected, field), bytes.fromhex(actual[field]))
            actual_stack = record["capture"]["stack"]
            self.assertEqual(case.capture.stack, None if actual_stack is None else bytes.fromhex(actual_stack))

    def test_executor_runs_every_case_once_and_propagates_a_missing_execution(self):
        from contextlib import redirect_stdout
        import io
        from tools import check_panic_traces as controller
        from tools.check_panic_traces import run_cases

        cases = PanicTraceOracleTest.baseline_cases()
        binding = object()
        with (
            redirect_stdout(io.StringIO()),
            patch.object(controller, "bind_image", return_value=binding),
            patch.object(controller.shutil, "which", return_value="/fixed/qemu"),
            patch.object(controller, "run_case", side_effect=tuple((case, {}) for case in cases)) as run,
        ):
            actual_binding, actual_cases, records = run_cases(Path("fixed.elf"), baseline=True)
            self.assertIs(binding, actual_binding)
            self.assertEqual(cases, actual_cases)
            self.assertEqual(18, len(records))
            self.assertEqual(tuple(range(1, 19)), tuple(call.args[2] for call in run.call_args_list))
        with (
            redirect_stdout(io.StringIO()),
            patch.object(controller, "bind_image", return_value=binding),
            patch.object(controller.shutil, "which", return_value="/fixed/qemu"),
            patch.object(controller, "run_case", side_effect=[(cases[0], {}), TimeoutError("no second case")]) as run,
        ):
            with self.assertRaises(TimeoutError):
                run_cases(Path("fixed.elf"), baseline=True)
            self.assertEqual(2, run.call_count)


class PanicTraceIntegrationTest(unittest.TestCase):
    def test_current_recipes_and_test_only_overlays_restore_the_complete_fixed_inputs(self):
        from tests.host.panic_trace_source import (
            SOURCE_OVERLAYS, TEST_HEADER, extend_cmake, extend_presets, fixed_source, observe_source,
            project_trace_runtime,
        )

        self.assertTrue(
            (ROOT / "CMakeLists.txt").read_text() == extend_cmake(fixed_source("CMakeLists.txt")),
            "current CMake does not contain the exact independently specified trace integration",
        )
        self.assertTrue(
            (ROOT / "CMakePresets.json").read_text() == extend_presets(fixed_source("CMakePresets.json")),
            "current presets do not preserve the complete predecessor plus the one trace workflow",
        )
        for name in SOURCE_OVERLAYS:
            with self.subTest(path=name):
                actual = (ROOT / name).read_text()
                if name == "kernel/diagnostic/panic.c":
                    self.assertIn(TEST_HEADER, actual)
                    self.assertEqual(fixed_source(name), project_trace_runtime(name, actual))
                else:
                    self.assertEqual(observe_source(name, fixed_source(name)), actual)

    def test_trace_workflow_extends_every_coverage_inventory_without_replacing_an_old_gate(self):
        from tools import validation_plan
        from tools.stack_bound_scope import SCENARIOS, expected_images

        self.assertIn("test-qemu-panic-trace", validation_plan.QEMU_WORKFLOWS)
        presets = json.loads((ROOT / "CMakePresets.json").read_text())
        workflows = {row["name"]: row for row in presets["workflowPresets"]}
        configurations = {
            workflows[name]["steps"][0]["name"]
            for name in (*validation_plan.QEMU_WORKFLOWS, *validation_plan.IMAGE_WORKFLOWS)
        }
        self.assertEqual((44, 113, 10), (
            len(configurations), sum(len(expected_images(name)) for name in configurations), len(SCENARIOS),
        ))
        self.assertEqual(("kernel/micros.elf",), expected_images("riscv64-panic-trace-test"))
        for path in (
            "kernel/diagnostic/panic_trace_core.c", "kernel/diagnostic/panic_trace.c",
            "arch/riscv64/panic_trace.S", "tests/host/panic_trace_test.c",
            "tests/qemu/panic_trace_kernel.c", "tools/check_panic_traces.py",
        ):
            with self.subTest(path=path):
                self.assertIn("test-qemu-panic-trace", validation_plan.affected_workflows([path]))


class PanicTraceTransportTest(unittest.TestCase):
    def test_existing_runtime_sources_inverse_exactly_before_older_adapters(self):
        from tests.host.panic_trace_source import SOURCE_OVERLAYS, fixed_source, observe_source, project_trace_runtime

        for name in SOURCE_OVERLAYS:
            original = fixed_source(name)
            observed = observe_source(name, original)
            self.assertEqual(original, project_trace_runtime(name, observed))
            with self.assertRaises(ValueError):
                project_trace_runtime(name, observed + "\nint undeclared_state;\n")
        name = "kernel/diagnostic/panic.c"
        source = observe_source(name, fixed_source(name))
        source = source.replace(
            '#include "kernel/diagnostic/diagnostic_history.h"\n\n',
            '#include "kernel/diagnostic/diagnostic_history.h"\n#include "kernel/diagnostic/panic_trace.h"\n',
        ).replace(
            "    micros_diagnostic_panic_report();\n",
            "    micros_panic_trace_report();\n    micros_diagnostic_panic_report();\n",
        )
        self.assertEqual(fixed_source(name), project_trace_runtime(name, source))
        for changed in (
            source.replace("    micros_panic_trace_report();\n", ""),
            source.replace("    micros_panic_trace_report();\n", "    micros_panic_trace_report();\n" * 2),
            source.replace(
                "    micros_panic_trace_report();\n    micros_diagnostic_panic_report();\n",
                "    micros_diagnostic_panic_report();\n    micros_panic_trace_report();\n",
            ),
        ):
            with self.assertRaises(ValueError):
                project_trace_runtime(name, changed)

    def test_planner_projection_preserves_every_original_rule_and_rejects_reordered_new_work(self):
        from tests.host.panic_trace_source import (
            fixed_source, project_trace_plan, project_trace_planner, project_trace_workflows,
        )
        from tools import validation_plan

        source = (ROOT / "tools/validation_plan.py").read_text()
        self.assertEqual(fixed_source("tools/validation_plan.py"), project_trace_planner(source))
        plan = validation_plan.plan([], "full")
        self.assertEqual((50, 49), (len(plan), len(project_trace_plan(plan))))
        self.assertEqual(plan[-3:], project_trace_plan(plan)[-3:])
        workflow = "test-qemu-panic-trace"
        self.assertNotIn(workflow, project_trace_workflows(validation_plan.QEMU_WORKFLOWS))
        for changed in (
            source.replace('"test-qemu-panic-trace"', '"test-qemu-undeclared"', 1),
            source.replace("def plan(", "def undeclared_plan("),
        ):
            with self.assertRaises(ValueError):
                project_trace_planner(changed)
        with self.assertRaises(ValueError):
            project_trace_plan([*plan, ("cmake", "--workflow", "--preset", workflow)])
        with self.assertRaises(ValueError):
            project_trace_workflows((workflow, *validation_plan.QEMU_WORKFLOWS))


class PanicTraceMachineTest(unittest.TestCase):
    def test_real_fault_dispatch_preserves_the_latch_and_exact_context_output(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import TRACE_FAULT_FUNCTIONS, trace_fault_prefix_effects, trace_object_layout
        from tools.stack_bound_objects import load_object
        from tools.stack_bound_types import StackEvidenceError

        layout = trace_object_layout(ROOT)
        with tempfile.TemporaryDirectory(prefix="micros-plat03-fault-prefix-") as directory:
            objects, sections = {}, {}
            for index, unit in enumerate(dict.fromkeys(unit for unit, _ in TRACE_FAULT_FUNCTIONS)):
                selected = Path(directory) / str(index)
                selected.mkdir()
                _, path = compile_fixture(selected, "#define MICROS_BUILD_PANIC_TRACE_TEST 1\n" + (ROOT / unit).read_text())
                obj = load_object(path)
                objects[unit] = obj
                sections[unit] = {
                    section.index: 0x6400000000000000 + index * 0x1000000000 + section.index * 0x1000000
                    for section in obj.sections if section.flags & 3 == 3
                }
            trace_fault_prefix_effects(objects, sections, layout, 0x83004000)
            for index, (unit, before, after) in enumerate((
                ("kernel/objects/kernel_object_hart.c", "*hart = candidate;\n            return MICROS_KERNEL_OBJECT_OK;",
                 "*hart = candidate + 1;\n            return MICROS_KERNEL_OBJECT_OK;"),
                ("kernel/trap/trap_context.c", "    return hart;\n",
                 "    *(volatile uint32_t *)0x10000000 = 0;\n    return hart;\n"),
                ("kernel/trap/trap.c", "if ((frame->sstatus & MICROS_RISCV_SSTATUS_SPP) == 0)",
                 "if ((frame->sstatus & MICROS_RISCV_SSTATUS_SPP) != 0)"),
            )):
                source = (ROOT / unit).read_text()
                self.assertIn(before, source)
                selected = Path(directory) / ("mutation-" + str(index))
                selected.mkdir()
                _, path = compile_fixture(selected, "#define MICROS_BUILD_PANIC_TRACE_TEST 1\n" + source.replace(before, after, 1))
                changed = load_object(path)
                mapped = {section.index: 0x6600000000000000 + section.index * 0x1000000
                          for section in changed.sections if section.flags & 3 == 3}
                with self.subTest(mutation=index), self.assertRaises(StackEvidenceError):
                    trace_fault_prefix_effects({**objects, unit: changed}, {**sections, unit: mapped}, layout, 0x83004000)

    def test_trace_fixture_source_occurrences_bind_every_qualified_hook(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal import definition_line, source_calls
        from tools.stack_bound_fatal_effects import OBSERVER_CALLS, TRACE_FIXTURE, TRACE_FIXTURE_CALLS, WORK_COLLECTOR
        from tools.stack_bound_flow import function_flow
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import FunctionKey, SourceOrigin

        with tempfile.TemporaryDirectory(prefix="micros-plat03-fixture-sources-") as directory:
            for index, (unit, names) in enumerate((
                (TRACE_FIXTURE, tuple(TRACE_FIXTURE_CALLS)),
                (WORK_COLLECTOR, tuple(name for owner, name in OBSERVER_CALLS if owner == WORK_COLLECTOR)),
                ("kernel/objects/kernel_object_runtime.c",
                 ("micros_kernel_object_runtime_test_registry", "micros_kernel_object_runtime_boot_hart")),
            )):
                source = (ROOT / unit).read_text()
                selected = Path(directory) / str(index)
                selected.mkdir()
                _, path = compile_fixture(selected, (
                    "#define MICROS_BUILD_PANIC_TRACE_TEST 1\n#define MICROS_TEST_WORK_COUNTS 1\n"
                    "#define MICROS_TEST_PANIC_TRACE_COUNTS 1\n" + source
                ))
                obj = load_object(path)
                for name in names:
                    key = FunctionKey("fixture", "kernel/micros.elf", unit, name)
                    origin = SourceOrigin(key, unit, definition_line(source, name))
                    calls = function_flow(obj, function_symbols(obj)[name]).calls
                    with self.subTest(unit=unit, symbol=name):
                        source_calls(ROOT, key, origin, calls, trace_test=True)

    def test_guest_constructor_keeps_getter_results_and_slot_one_guard(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import trace_fixture_alias_effects, trace_object_layout
        from tools.stack_bound_objects import load_object
        from tools.stack_bound_types import StackEvidenceError

        original = (
            "#define MICROS_BUILD_PANIC_TRACE_TEST 1\n#define MICROS_TEST_WORK_COUNTS 1\n"
            "#define MICROS_TEST_PANIC_TRACE_COUNTS 1\n"
            + (ROOT / "tests/qemu/panic_trace_kernel.c").read_text()
        )
        layout = trace_object_layout(ROOT)
        registry = 0x6800000000000000
        aliases = {
            "registry": registry, "hart": registry + layout["hart_table"],
            "thread": registry + layout["thread_table"] + layout["thread_bytes"],
            "bank": 0x6900000000000000, "fault": 0x80200010,
        }
        with tempfile.TemporaryDirectory(prefix="micros-plat03-fixture-aliases-") as directory:
            for index, source in enumerate((
                original,
                original.replace("objects = micros_kernel_object_runtime_test_registry();",
                                 "objects = micros_kernel_object_runtime_test_registry() + 1;", 1),
                original.replace("hart = micros_kernel_object_runtime_boot_hart();",
                                 "hart = &objects->harts[0];", 1),
                original.replace("selected_thread.slot == 1", "selected_thread.slot <= 1", 1),
            )):
                selected = Path(directory) / str(index)
                selected.mkdir()
                _, path = compile_fixture(selected, source)
                obj = load_object(path)
                sections = {section.index: 0x6400000000000000 + section.index * 0x1000000
                            for section in obj.sections if section.flags & 3 == 3}
                if index:
                    with self.subTest(mutation=index), self.assertRaises(StackEvidenceError):
                        trace_fixture_alias_effects(obj, sections, layout, aliases)
                else:
                    trace_fixture_alias_effects(obj, sections, layout, aliases)

    def test_trace_collector_binds_all_three_work_kinds_and_the_real_snapshot(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import trace_collector_effects
        from tools.stack_bound_objects import load_object
        from tools.stack_bound_types import StackEvidenceError

        original = "#define MICROS_TEST_WORK_COUNTS 1\n" + (ROOT / "tests/work/work_counts.c").read_text()
        update = "previous = atomic_fetch_add_explicit(\n        &collector.counts[owner][kind],\n        units,"
        with tempfile.TemporaryDirectory(prefix="micros-plat03-collector-effects-") as directory:
            for index, source in enumerate((
                original,
                original.replace(update, update.replace("[kind]", "[0]"), 1),
                original.replace(update, update.replace("units,", "units + 1,"), 1),
                original.replace("publish_snapshot(output);", "publish_snapshot((struct micros_test_work_snapshot *)&collector);", 1),
            )):
                selected = Path(directory) / str(index)
                selected.mkdir()
                _, path = compile_fixture(selected, source)
                obj = load_object(path)
                sections = {section.index: 0x6400000000000000 + section.index * 0x1000000
                            for section in obj.sections if section.flags & 3 == 3}
                collector, = (symbol for symbol in obj.symbols if symbol.name == "collector")
                address = sections[collector.section_index] + collector.value
                if index:
                    with self.subTest(mutation=index), self.assertRaises(StackEvidenceError):
                        trace_collector_effects(obj, sections, address, 0x6500000000000000)
                else:
                    trace_collector_effects(obj, sections, address, 0x6500000000000000)

    def test_guest_observers_cannot_borrow_snapshot_or_write_another_thread_slot(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import trace_fixture_effects, trace_object_layout
        from tools.stack_bound_objects import load_object
        from tools.stack_bound_types import StackEvidenceError

        original = (
            "#define MICROS_BUILD_PANIC_TRACE_TEST 1\n#define MICROS_TEST_WORK_COUNTS 1\n"
            "#define MICROS_TEST_PANIC_TRACE_COUNTS 1\n"
            + (ROOT / "tests/qemu/panic_trace_kernel.c").read_text()
        )
        layout = trace_object_layout(ROOT)
        registry = 0x6800000000000000
        aliases = {
            "registry": registry, "hart": registry + layout["hart_table"],
            "thread": registry + layout["thread_table"] + layout["thread_bytes"],
            "bank": 0x6900000000000000, "fault": 0x80200010,
        }
        with tempfile.TemporaryDirectory(prefix="micros-plat03-guest-effects-") as directory:
            for index, source in enumerate((
                original,
                original.replace("micros_test_work_finish(&micros_panic_trace_test_work_snapshot)",
                                 "micros_test_work_finish((struct micros_test_work_snapshot *)&micros_panic_trace_test_observation)", 1),
                original.replace("objects->threads[selected_thread.slot].kernel_stack_bottom",
                                 "objects->threads[0].kernel_stack_bottom", 1),
                original.replace("++micros_panic_trace_test_observation.read_words;",
                                 "++micros_panic_trace_test_observation.completed_traces;", 1),
                original.replace("require(cause == 13 && fault_address == 0);",
                                 "require(cause == 14 && fault_address == 0);", 1),
                original.replace("require(cause == 13 && fault_address == 0);", "require(true);", 1),
                original.replace("fault_pc == (uintptr_t)micros_panic_trace_test_load_fault",
                                 "fault_pc == (uintptr_t)micros_panic_trace_test_load_fault + 4", 1),
                original.replace("micros_panic_trace_test_observation.reset_sstatus = status;",
                                 "micros_panic_trace_test_observation.reset_sstatus = 0;", 1),
                original.replace(
                    "require(fault_pc == (uintptr_t)micros_panic_trace_test_load_fault);",
                    "bool full_width = fault_pc == (uintptr_t)micros_panic_trace_test_load_fault;\n"
                    "        (void)full_width;\n"
                    "        require((uint32_t)fault_pc == (uint32_t)(uintptr_t)micros_panic_trace_test_load_fault);", 1,
                ),
            )):
                selected = Path(directory) / str(index)
                selected.mkdir()
                _, path = compile_fixture(selected, source)
                obj = load_object(path)
                sections = {section.index: 0x6400000000000000 + section.index * 0x1000000
                            for section in obj.sections if section.flags & 3 == 3}
                if index:
                    with self.subTest(mutation=index), self.assertRaises(StackEvidenceError):
                        trace_fixture_effects(obj, sections, layout, aliases)
                else:
                    trace_fixture_effects(obj, sections, layout, aliases)

    def test_guest_setup_aliases_require_the_same_actual_registry_and_selected_hart(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import trace_accessor_effects, trace_object_layout
        from tools.stack_bound_objects import load_object
        from tools.stack_bound_types import StackEvidenceError

        original = "#define MICROS_BUILD_PANIC_TRACE_TEST 1\n" + (ROOT / "kernel/objects/kernel_object_runtime.c").read_text()
        layout = trace_object_layout(ROOT)
        with tempfile.TemporaryDirectory(prefix="micros-plat03-fixture-accessors-") as directory:
            for index, source in enumerate((
                original, original.replace("return &kernel_objects.harts[boot_hart_handle.slot];",
                                           "return &kernel_objects.harts[0];", 1),
            )):
                selected = Path(directory) / str(index)
                selected.mkdir()
                _, path = compile_fixture(selected, source)
                obj = load_object(path)
                sections = {section.index: 0x6400000000000000 + section.index * 0x1000000
                            for section in obj.sections if section.flags & 3 == 3}
                if index:
                    with self.assertRaises(StackEvidenceError):
                        trace_accessor_effects(obj, sections, layout, fixture=True)
                else:
                    trace_accessor_effects(obj, sections, layout, fixture=True)

    def test_trace_report_source_occurrences_match_actual_machine_evaluation_order(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal import definition_line, source_calls
        from tools.stack_bound_flow import function_flow
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import FunctionKey, SourceOrigin

        unit, name = "kernel/diagnostic/panic_trace.c", "micros_panic_trace_report"
        source = (ROOT / unit).read_text()
        key = FunctionKey("fixture", "kernel/micros.elf", unit, name)
        origin = SourceOrigin(key, unit, definition_line(source, name))
        with tempfile.TemporaryDirectory(prefix="micros-plat03-call-order-") as directory:
            _, path = compile_fixture(Path(directory), source)
            obj = load_object(path)
            calls = function_flow(obj, function_symbols(obj)[name]).calls
            actual = source_calls(ROOT, key, origin, calls)
            self.assertEqual(tuple(call.target_symbol for call in calls), tuple(call.target for call in actual))

    def test_new_private_source_modes_bind_the_actual_owner_and_exact_work_sites(self):
        from tools.stack_bound_fatal import checked_source_record, definition_line
        from tools.stack_bound_types import FunctionKey, SourceOrigin

        for unit, name, count in (
            ("kernel/diagnostic/panic_trace_core.c", "trace_advance", 1),
            ("kernel/diagnostic/panic_trace.c", "micros_panic_trace_report", 2),
        ):
            key = FunctionKey("fixture", "kernel/micros.elf", unit, name)
            origin = SourceOrigin(key, unit, definition_line((ROOT / unit).read_text(), name))
            for enabled in (False, True):
                record = checked_source_record(ROOT, key, origin, trace_test=enabled)
                self.assertNotIn("#", record.body)
                self.assertNotIn("MICROS_TEST_WORK_HIT", record.body)
                self.assertEqual(count if enabled else 0, record.body.count("micros_test_work_record("))

    def test_runtime_accessors_preserve_the_fixed_registry_and_complete_boot_handle(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import trace_accessor_effects, trace_object_layout
        from tools.stack_bound_objects import load_object
        from tools.stack_bound_types import StackEvidenceError

        original = (ROOT / "kernel/objects/kernel_object_runtime.c").read_text()
        layout = trace_object_layout(ROOT)
        with tempfile.TemporaryDirectory(prefix="micros-plat03-accessor-effects-") as directory:
            for index, source in enumerate((
                original,
                original.replace("return &kernel_objects;", "return (const struct micros_kernel_objects *)0x10000000;", 1),
                original.replace("return &kernel_objects;", "runtime_ready = false;\n    return &kernel_objects;", 1),
                original.replace("return boot_hart_handle;", "return invalid;", 1),
            )):
                selected = Path(directory) / str(index)
                selected.mkdir()
                _, path = compile_fixture(selected, source)
                obj = load_object(path)
                sections = {section.index: 0x6400000000000000 + section.index * 0x1000000
                            for section in obj.sections if section.flags & 3 == 3}
                if index:
                    with self.subTest(mutation=index), self.assertRaises(StackEvidenceError):
                        trace_accessor_effects(obj, sections, layout)
                else:
                    trace_accessor_effects(obj, sections, layout)

    def test_selected_resolvers_write_only_the_exact_local_output_for_each_valid_slot(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import trace_object_layout, trace_resolver_effects
        from tools.stack_bound_objects import load_object
        from tools.stack_bound_types import StackEvidenceError

        layout = trace_object_layout(ROOT)
        with tempfile.TemporaryDirectory(prefix="micros-plat03-selected-effects-") as directory:
            for name in ("hart", "thread", "process", "identity"):
                unit = "kernel/objects/kernel_object_" + name + ".c"
                selected = Path(directory) / name
                selected.mkdir()
                _, path = compile_fixture(selected, (ROOT / unit).read_text())
                trace_resolver_effects(load_object(path), unit, layout)
            unit = "kernel/objects/kernel_object_hart.c"
            original = (ROOT / unit).read_text()
            for index, source in enumerate((
                original.replace("*hart = candidate;", "*hart = candidate + 1;", 1),
                original.replace("*hart = candidate;", "((uint64_t *)hart)[1] = 0;\n    *hart = candidate;", 1),
            )):
                selected = Path(directory) / str(index)
                selected.mkdir()
                _, path = compile_fixture(selected, source)
                with self.subTest(mutation=index), self.assertRaises(StackEvidenceError):
                    trace_resolver_effects(load_object(path), unit, layout)

    def test_trace_work_and_completion_observations_follow_actual_load_and_serial_return(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import trace_target_effects
        from tools.stack_bound_objects import load_object
        from tools.stack_bound_types import StackEvidenceError

        original = (
            "#define MICROS_BUILD_PANIC_TRACE_TEST 1\n#define MICROS_TEST_WORK_COUNTS 1\n"
            "#define MICROS_TEST_PANIC_TRACE_COUNTS 1\n"
            + (ROOT / "kernel/diagnostic/panic_trace.c").read_text()
        )
        load = "        pc = micros_panic_trace_load_word(pc_address);\n"
        count = (
            "#ifdef MICROS_TEST_PANIC_TRACE_COUNTS\n"
            "        MICROS_TEST_WORK_HIT(\n"
            '            "panic-trace-word-pc", MICROS_WORK_OWNER_KERNEL_DIAGNOSTIC, MICROS_WORK_KIND_RECORD_VISIT, 1\n'
            "        );\n#endif\n"
        )
        complete = (
            "#ifdef MICROS_BUILD_PANIC_TRACE_TEST\n"
            "    micros_panic_trace_test_complete(cursor.count, (uint32_t)cursor.stop);\n#endif\n"
        )
        newline = '    uart_write("\\n");\n'
        with tempfile.TemporaryDirectory(prefix="micros-plat03-observation-effects-") as directory:
            for index, source in enumerate((
                original,
                original.replace(load + count, count + load, 1),
                original.replace(newline + complete, complete + newline, 1),
                original.replace("micros_panic_trace_test_complete(cursor.count, (uint32_t)cursor.stop)",
                                 "micros_panic_trace_test_complete(0, (uint32_t)cursor.stop)", 1),
                original.replace("micros_panic_trace_test_complete(cursor.count, (uint32_t)cursor.stop)",
                                 "micros_panic_trace_test_complete(cursor.count, 1)", 1),
            )):
                selected = Path(directory) / str(index)
                selected.mkdir()
                _, path = compile_fixture(selected, source)
                if index:
                    with self.subTest(mutation=index), self.assertRaises(StackEvidenceError):
                        trace_target_effects(load_object(path), trace_test=True)
                else:
                    trace_target_effects(load_object(path), trace_test=True)

    def test_target_machine_requires_local_views_real_getters_and_successful_plan_word_custody(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import trace_target_effects
        from tools.stack_bound_objects import load_object
        from tools.stack_bound_types import StackEvidenceError

        original = (ROOT / "kernel/diagnostic/panic_trace.c").read_text()
        mutations = (
            original.replace("    view->anchor = 0;", "    *(volatile uint32_t *)0x10000000 = 1;\n    view->anchor = 0;", 1),
            original.replace("objects = micros_kernel_object_runtime_registry();",
                             "objects = (const struct micros_kernel_objects *)0x10000000;", 1),
            original.replace("pc = micros_panic_trace_load_word(pc_address);",
                             "pc = micros_panic_trace_load_word(link_address);", 1),
            original.replace("if (!micros_panic_trace_cursor_plan(&cursor, &pc_address, &link_address))",
                             "if (micros_panic_trace_cursor_plan(&cursor, &pc_address, &link_address))", 1),
            original.replace(
                "micros_hart_resolve(objects, handle, &hart)",
                "micros_hart_resolve((const struct micros_kernel_objects *)0x40000000, handle, &hart)", 1,
            ),
            original.replace(
                "micros_thread_resolve(objects, hart->current_thread, &thread)",
                "micros_thread_resolve(objects, (struct micros_thread_handle){1, 2}, &thread)", 1,
            ),
            original.replace(
                "micros_process_resolve(objects, thread->owner, &process)",
                "micros_process_resolve(objects, (struct micros_process_handle){0, 1}, &process)", 1,
            ),
            original.replace("objects == NULL || objects->max_harts", "false || objects->max_harts", 1),
            original.replace(
                "micros_process_resolve(objects, thread->owner, &process) == MICROS_KERNEL_OBJECT_OK;",
                "micros_process_resolve(objects, thread->owner, &process) == MICROS_KERNEL_OBJECT_OK || true;", 1,
            ),
            original.replace(
                "frame = micros_panic_trace_frame_pointer();",
                'frame = micros_panic_trace_frame_pointer();\n    __asm__ volatile("sd zero, -16(s0)");', 1,
            ),
        )
        with tempfile.TemporaryDirectory(prefix="micros-plat03-target-effects-") as directory:
            for index, source in enumerate((original, *mutations)):
                selected = Path(directory) / str(index)
                selected.mkdir()
                _, path = compile_fixture(selected, source)
                if index:
                    with self.subTest(mutation=index), self.assertRaises(StackEvidenceError):
                        trace_target_effects(load_object(path))
                else:
                    trace_target_effects(load_object(path))

    def test_trace_uart_observer_consumes_the_actual_lsr_and_preserves_terminal_effects(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import uart_machine_effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        original = (
            "#define MICROS_BUILD_PANIC_TRACE_TEST 1\n#define MICROS_TEST_WORK_COUNTS 1\n"
            + (ROOT / "arch/riscv64/uart.c").read_text()
        )
        with tempfile.TemporaryDirectory(prefix="micros-plat03-uart-effects-") as directory:
            for index, source in enumerate((
                original, original.replace("MICROS_PANIC_TRACE_TEST_LSR(uart0[UART_LSR])", "MICROS_PANIC_TRACE_TEST_LSR(0)", 1),
            )):
                selected = Path(directory) / str(index)
                selected.mkdir()
                _, path = compile_fixture(selected, source)
                obj = load_object(path)
                symbols = function_symbols(obj)
                if index:
                    with self.assertRaises(StackEvidenceError):
                        uart_machine_effects(obj, symbols["uart_write_character"], terminal=True, trace_test=True)
                else:
                    for name in ("uart_write_character", "uart_flush"):
                        uart_machine_effects(obj, symbols[name], terminal=True, trace_test=True)
                    console, = (value for value in obj.symbols if value.name == "uart_console_state")
                    uart_machine_effects(obj, symbols["uart_panic_seize"], terminal=True, trace_test=True, console=console)
                    with self.assertRaises(StackEvidenceError):
                        uart_machine_effects(obj, symbols["uart_flush"], terminal=True, terminal_test=True, trace_test=True)

    def test_trace_reset_observation_retains_actual_tuple_and_shared_claim_precedence(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import terminal_srst_effects
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        original = (
            "#define MICROS_BUILD_PANIC_TRACE_TEST 1\n#define MICROS_TEST_WORK_COUNTS 1\n"
            + (ROOT / "arch/riscv64/sbi.c").read_text()
        )
        with tempfile.TemporaryDirectory(prefix="micros-plat03-reset-effects-") as directory:
            for index, source in enumerate((
                original,
                original.replace("micros_panic_trace_test_reset(reset_type, reset_reason);",
                                 "micros_panic_trace_test_reset(reset_reason, reset_type);", 1),
            )):
                selected = Path(directory) / str(index)
                selected.mkdir()
                _, path = compile_fixture(selected, source)
                obj = load_object(path)
                symbol = function_symbols(obj)["sbi_system_reset"]
                if index:
                    with self.assertRaises(StackEvidenceError):
                        terminal_srst_effects(obj, symbol, trace_observer=True)
                else:
                    terminal_srst_effects(obj, symbol, trace_observer=True)

    def test_existing_fatal_sources_keep_exact_selected_trace_hooks_and_poll_work(self):
        from tools.stack_bound_fatal import checked_source_record, definition_line
        from tools.stack_bound_types import FunctionKey, SourceOrigin, StackEvidenceError

        for unit, name in (
            ("arch/riscv64/uart.c", "uart_write_character"),
            ("arch/riscv64/uart.c", "uart_flush"),
            ("kernel/diagnostic/panic.c", "panic_report_claimed"),
            ("arch/riscv64/sbi.c", "sbi_system_reset"),
        ):
            key = FunctionKey("fixture", "kernel/micros.elf", unit, name)
            origin = SourceOrigin(key, unit, definition_line((ROOT / unit).read_text(), name))
            normal = checked_source_record(ROOT, key, origin, trace_test=False).body
            observed = checked_source_record(ROOT, key, origin, trace_test=True).body
            self.assertNotIn("micros_panic_trace_test_", normal)
            self.assertNotIn("MICROS_BUILD_PANIC_TRACE_TEST", observed)
            if name in ("uart_write_character", "uart_flush"):
                self.assertNotIn("micros_test_work_record", normal)
                self.assertEqual(1, observed.count("micros_test_work_record("))
            if name == "uart_write_character":
                self.assertEqual(1, observed.count("micros_panic_trace_test_lsr(uart0[UART_LSR])"))
                self.assertEqual(1, observed.count("micros_panic_trace_test_thr();"))
            if name == "panic_report_claimed":
                self.assertLess(normal.index("micros_panic_trace_report();"), normal.index("micros_diagnostic_panic_report();"))
                self.assertEqual(1, observed.count("micros_panic_trace_test_after_core();"))
            with self.assertRaises(StackEvidenceError):
                checked_source_record(ROOT, key, origin, terminal_test=True, trace_test=True)

    def test_portable_machine_effects_reject_address_dereferences_escaped_outputs_and_hidden_calls(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import trace_core_effects
        from tools.stack_bound_objects import load_object
        from tools.stack_bound_types import StackEvidenceError

        original = (ROOT / "kernel/diagnostic/panic_trace_core.c").read_text()
        mutations = (
            original.replace(
                "    return bottom != 0",
                "    *(volatile uint32_t *)0x10000000 = 1;\n    return bottom != 0", 1,
            ),
            original.replace(
                "    return (frame & 15)",
                "    (void)*(const volatile uint64_t *)frame;\n    return (frame & 15)", 1,
            ),
            original.replace("    bounds->root = 0;", "    bounds->root = 0;\n    ((uint64_t *)bounds)[3] = 0;", 1),
            "extern void unexpected(void);\n" + original.replace(
                "    return bottom != 0", "    unexpected();\n    return bottom != 0", 1,
            ),
            original.replace("*pc_address = cursor->frame - 8;", "*pc_address = cursor->frame - 24;", 1),
            original.replace("*link_address = cursor->frame - 16;", "*link_address = cursor->root - 16;", 1),
            original.replace(
                "if (!trace_frame(cursor->lower, cursor->root, cursor->frame))",
                "if (trace_frame(cursor->lower, cursor->root, cursor->frame))", 1,
            ),
        )
        with tempfile.TemporaryDirectory(prefix="micros-plat03-core-effects-") as directory:
            for index, source in enumerate((original, *mutations)):
                selected = Path(directory) / str(index)
                selected.mkdir()
                _, path = compile_fixture(selected, source)
                if index:
                    with self.subTest(mutation=index), self.assertRaises(StackEvidenceError):
                        trace_core_effects(load_object(path))
                else:
                    trace_core_effects(load_object(path))

    def test_only_the_actual_advance_may_record_its_one_trace_specific_link_unit(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_fatal_effects import trace_core_effects
        from tools.stack_bound_objects import load_object
        from tools.stack_bound_types import StackEvidenceError

        original = (
            "#define MICROS_TEST_WORK_COUNTS 1\n#define MICROS_TEST_PANIC_TRACE_COUNTS 1\n"
            + (ROOT / "kernel/diagnostic/panic_trace_core.c").read_text()
        )
        with tempfile.TemporaryDirectory(prefix="micros-plat03-core-work-") as directory:
            for index, source in enumerate((
                original,
                original.replace("MICROS_WORK_KIND_LINK_TRAVERSE, 1", "MICROS_WORK_KIND_RECORD_VISIT, 1", 1),
                original.replace("MICROS_WORK_KIND_LINK_TRAVERSE, 1", "MICROS_WORK_KIND_LINK_TRAVERSE, 2", 1),
            )):
                selected = Path(directory) / str(index)
                selected.mkdir()
                _, path = compile_fixture(selected, source)
                obj = load_object(path)
                if index:
                    with self.subTest(mutation=index), self.assertRaises(StackEvidenceError):
                        trace_core_effects(obj, counted=True)
                else:
                    trace_core_effects(obj, counted=True)
                    with self.assertRaises(StackEvidenceError):
                        trace_core_effects(obj, counted=False)


class PanicTraceAssemblyTest(unittest.TestCase):
    def compile_assembly(self, directory, source, flags=()):
        from tools.stack_bound_objects import load_object

        path, output = directory / "fixture.S", directory / "fixture.obj"
        path.write_text(source)
        result = subprocess.run(
            (os.environ["MICROS_CLANG"], "--target=riscv64-unknown-elf",
             "-march=rv64imac_zicsr_zifencei", "-mabi=lp64", "-mcmodel=medany",
             "-I" + str(ROOT), *(f"-D{flag}=1" for flag in flags),
             "-c", str(path), "-o", str(output)),
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(0, result.returncode, result.stderr)
        return load_object(output)

    def test_trace_fault_pivots_retain_the_original_epoch_and_complete_primary_capture(self):
        from tools.stack_bound_assembly import trace_trap_effects
        from tools.stack_bound_types import StackEvidenceError

        original = (ROOT / "arch/riscv64/trap.S").read_text()
        bindings = {
            "anchor": 0x81000000, "source_sp": 0x82003f00, "primary_top": 0x83004000,
            "emergency_top": 0x84001000, "selector": 0x85000000, "entry_pc": 0x80200010,
        }
        with tempfile.TemporaryDirectory(prefix="micros-plat03-trap-epochs-") as directory:
            for index, source in enumerate((
                original,
                original.replace("sd t0, MICROS_TRAP_CONTEXT_STVAL_OFFSET(sp)", "nop", 1),
                original.replace("ld sp, MICROS_HART_TRAP_EMERGENCY_TOP_OFFSET(tp)",
                                 "ld sp, MICROS_HART_TRAP_PRIMARY_TOP_OFFSET(tp)", 1),
                original.replace("csrw sscratch, zero", "csrw sscratch, tp", 1),
                original.replace("li t1, 5", "li t1, 15", 1),
            )):
                selected = Path(directory) / str(index)
                selected.mkdir()
                obj = self.compile_assembly(selected, source, ("MICROS_BUILD_PANIC_TRACE_TEST",))
                if index:
                    with self.subTest(mutation=index), self.assertRaises(StackEvidenceError):
                        trace_trap_effects(obj, bindings)
                else:
                    trace_trap_effects(obj, bindings)

    def test_helpers_have_two_actual_zero_frames_and_only_the_bound_case15_fault(self):
        from tools.stack_bound_assembly import assembly_object_evidence
        from tools.stack_bound_types import StackEvidenceError

        unit = "arch/riscv64/panic_trace.S"
        original = (ROOT / unit).read_text()
        with tempfile.TemporaryDirectory(prefix="micros-plat03-assembly-") as directory:
            for index, flags in enumerate((frozenset(), frozenset({"MICROS_BUILD_PANIC_TRACE_TEST"}))):
                selected = Path(directory) / str(index)
                selected.mkdir()
                obj = self.compile_assembly(selected, original, flags)
                proof = assembly_object_evidence(ROOT, obj, unit, "fixture", "kernel/micros.elf", flags)
                self.assertEqual((0, 0), tuple(frame.bytes_used for frame in proof.frames))
                self.assertEqual((), proof.calls)
                self.assertEqual(index, len(proof.faults))
            for index, source in enumerate((
                original.replace("ld a0, 0(a0)", "ld a0, 0(a1)", 1),
                original.replace("    mv a0, s0", "    addi sp, sp, -16\n    mv a0, s0", 1),
            ), 2):
                selected = Path(directory) / str(index)
                selected.mkdir()
                obj = self.compile_assembly(selected, source)
                with self.subTest(mutation=index), self.assertRaises(StackEvidenceError):
                    assembly_object_evidence(ROOT, obj, unit, "fixture", "kernel/micros.elf")

    def test_guest_constructor_keeps_its_actual_frame_and_never_escapes_into_foreign_storage(self):
        from tools.stack_bound_assembly import assembly_object_evidence
        from tools.stack_bound_types import EdgeKind, StackEvidenceError

        unit = "tests/qemu/panic_trace_kernel.S"
        original = (ROOT / unit).read_text()
        flags = frozenset({"MICROS_BUILD_PANIC_TRACE_TEST"})
        with tempfile.TemporaryDirectory(prefix="micros-plat03-guest-assembly-") as directory:
            for index, source in enumerate((
                original,
                original.replace("addi sp, sp, -1024", "addi sp, sp, -1040", 1),
                original.replace("addi s0, a0, 32", "addi s0, a0, 16", 1),
                original.replace("sd t2, -16(t0)", "li t4, 0x10000000\n    sd t2, 0(t4)", 1),
                original.replace("li t1, 33", "li t1, 34", 1),
            )):
                selected = Path(directory) / str(index)
                selected.mkdir()
                obj = self.compile_assembly(selected, source, flags)
                if index:
                    with self.subTest(mutation=index), self.assertRaises(StackEvidenceError):
                        assembly_object_evidence(ROOT, obj, unit, "fixture", "kernel/micros.elf", flags)
                else:
                    proof = assembly_object_evidence(ROOT, obj, unit, "fixture", "kernel/micros.elf", flags)
                    sizes = {frame.function.symbol: frame.bytes_used for frame in proof.frames}
                    self.assertEqual(1024, sizes["micros_panic_trace_test_synthetic"])
                    self.assertEqual({0}, {size for name, size in sizes.items() if not name.endswith("_synthetic")})
                    self.assertTrue(all(call.kind is EdgeKind.CALL for call in proof.calls))
                    self.assertEqual(1, len(proof.faults))
