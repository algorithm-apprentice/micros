import copy
import json
import os
import subprocess
import unittest
from pathlib import Path
from unittest.mock import patch

from tests.host import vm_command_evidence as target
from tests.host import selected_vm_evidence as native
from tests.host.selected_vm_source import validate_native_sites
from tests.host.work_count_types import Kind, Owner


def target_transcript(audits=0, root_certificates=None, root_enumerations=0, elf_plans=3, request_decodes=9):
    def line(name, fields):
        return "# " + name + " " + " ".join(f"{key}=0x{value:016x}" for key, value in fields) + "\n"

    text = "# " + target.PASS_LINE + "\n"
    for producer, commands in zip(target.PRODUCERS, ((3, 12, 12, 3, 1, 2, 1), (3, 1, 2, 1, 0, 0, 0)), strict=True):
        text += line("MICROS_VM01_CAPTURE", (
            ("version", 1), ("reserved0", 0), ("producer", producer), ("capture", 1),
            ("owner_mask", target.MASKS[producer]), ("reserved1", 0),
            ("attempts", sum(commands)), ("complete", 1),
        ))
        for index, count in enumerate(commands):
            text += line("MICROS_VM01_COMMAND", (("producer", producer), ("index", index), ("count", count)))
        for owner in Owner:
            counts = {Kind.COMPLETE_VALIDATOR_CALL: audits} if producer == target.KERNEL_PRODUCER and owner == Owner.KERNEL_VM else {}
            if producer == target.KERNEL_PRODUCER:
                if owner == Owner.KERNEL_PM:
                    counts[Kind.INDEX_PROBE] = sum(commands)
                elif owner == Owner.KERNEL_VM:
                    counts[Kind.INDEX_PROBE] = 4 * sum(commands)
                elif owner == Owner.KERNEL_MEMORY:
                    counts[Kind.RECORD_VISIT] = root_enumerations
                    counts[Kind.COMPLETE_VALIDATOR_CALL] = (
                        commands[3] + commands[4] + commands[6]
                        if root_certificates is None else root_certificates
                    )
            elif owner == Owner.VM_SERVICE:
                counts[Kind.INDEX_PROBE] = sum(commands)
                counts[Kind.RANGE_VISIT] = elf_plans
            elif owner == Owner.LIB_SPAWN:
                counts[Kind.INDEX_PROBE] = request_decodes
            text += line("MICROS_VM01_COUNTS", (
                ("producer", producer), ("owner", owner),
                *((name, counts.get(kind, 0)) for kind, name in enumerate(target.DIMENSIONS)),
            ))
    return text + line("MICROS_VM01_CAPTURE_END", (("complete", 1), ("producers", 2)))


class SelectedVmTargetTest(unittest.TestCase):
    def test_retained_load_plan_rejects_repeated_or_missing_source_reconstruction(self):
        for plans in (0, 2, 4, 66):
            with self.subTest(plans=plans):
                self.assertEqual(
                    (("VM_SERVICE", "elf-plans", plans, 3),),
                    target.target_failures(target.parse_target_output(target_transcript(elf_plans=plans), 0)),
                )
        for decodes in (0, 8, 10, 72):
            with self.subTest(decodes=decodes):
                self.assertEqual(
                    (("LIB_SPAWN", "request-decodes", decodes, 9),),
                    target.target_failures(target.parse_target_output(target_transcript(request_decodes=decodes), 0)),
                )

    def test_scoped_mode_rejects_unrelated_terminal_copy_and_history_calls(self):
        enabled = ("-DMICROS_TEST_WORK_COUNTS=1", "-DMICROS_TEST_VM_COMMAND_COUNTS=1")
        for source in target.UNSCOPED_SOURCES:
            target.require_observation_mode(source, (*enabled, "-UMICROS_TEST_WORK_COUNTS"), ())
            for arguments, symbols in (
                (enabled, ()), ((*enabled, "-UMICROS_TEST_WORK_COUNTS"), ("micros_test_work_record",)),
            ):
                with self.subTest(source=source), self.assertRaises(ValueError):
                    target.require_observation_mode(source, arguments, symbols)
        with self.assertRaises(ValueError):
            target.require_observation_mode("kernel/vm/vm_handoff_core.c", (), ())

    def test_vm_callback_carrier_keeps_admission_before_copy_and_original_arguments(self):
        from tools.stack_bound_bindings import anchored_function
        from tools.stack_bound_types import FunctionKey, StackEvidenceError

        root = Path(__file__).resolve().parents[2]
        unit = "servers/vm/vm_service_load.c"
        path = root / unit
        source = path.read_text()
        key = FunctionKey("fixture", "vm.elf", unit, "vm_service_load")
        self.assertIn("vm_core_record_headers_are_valid", anchored_function(root, key).body)
        guard = (
            "    if (!vm_core_record_headers_are_valid(state)) {\n"
            "        return MICROS_VM_SERVICE_ERROR_INVARIANT;\n"
            "    }\n"
        )
        call = "    error = service_copy_inputs(service, backend, backend_context, request);"
        self.assertIn(guard + call, source)
        read = Path.read_text
        for changed in (
            source.replace(guard, "", 1),
            source.replace(guard, "", 1).replace(call, call + "\n" + guard, 1),
            source.replace(guard, guard + guard, 1),
            source.replace("vm_core_record_headers_are_valid(state)", "vm_core_record_headers_are_valid(foreign)", 1),
            source.replace(call, call.replace("backend_context", "foreign"), 1),
        ):
            def content(requested, *args, **kwargs):
                return changed if requested == path else read(requested, *args, **kwargs)

            with patch.object(Path, "read_text", content), self.assertRaises(StackEvidenceError):
                anchored_function(root, key)

    def test_vm_callback_admission_has_actual_normal_return_dominance(self):
        from tempfile import TemporaryDirectory

        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_bindings import require_vm_callback_admission
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import StackEvidenceError

        root = Path(__file__).resolve().parents[2]
        source = (root / "servers/vm/vm_service_load.c").read_text()
        with TemporaryDirectory(prefix="micros-vm-callback-admission-") as temporary:
            directory = Path(temporary)
            for observed in (False, True):
                _, path = compile_fixture(directory, source, observed=observed)
                obj = load_object(path)
                require_vm_callback_admission(obj, function_symbols(obj)["vm_service_load"])
            changed = source.replace("!vm_core_record_headers_are_valid(state)", "0", 1)
            _, path = compile_fixture(directory, changed)
            obj = load_object(path)
            with self.assertRaises(StackEvidenceError):
                require_vm_callback_admission(obj, function_symbols(obj)["vm_service_load"])

    def test_existing_guest_semantics_precede_the_independent_audit_limit(self):
        self.assertEqual((), target.target_failures(target.parse_target_output(target_transcript(), 0)))
        self.assertEqual(
            (("KERNEL_VM", "complete-audits", 1, 0),),
            target.target_failures(target.parse_target_output(target_transcript(1), 0)),
        )

    def test_selected_root_certificates_reject_all_process_walks_and_missing_proof(self):
        self.assertEqual((), target.target_failures(target.parse_target_output(target_transcript(), 0)))
        for roots in (0, 4, 6, 199):
            with self.subTest(roots=roots):
                self.assertEqual(
                    (("KERNEL_MEMORY", "selected-root-certificates", roots, 5),),
                    target.target_failures(target.parse_target_output(
                        target_transcript(root_certificates=roots), 0
                    )),
                )
        self.assertEqual(
            (("KERNEL_MEMORY", "root-enumeration", 1, 0),),
            target.target_failures(target.parse_target_output(target_transcript(root_enumerations=1), 0)),
        )

    def test_missing_borrowed_reordered_and_incomplete_evidence_fails(self):
        good = target_transcript()
        lines = good.splitlines(keepends=True)
        for damaged in (
            good[:-1], "".join(lines[:-1]), "".join(lines[:4] + lines[5:]),
            "".join(lines[:4] + [lines[3]] + lines[4:]),
            good.replace(target.PASS_LINE, "wrong-scenario", 1),
            good.replace(f"producer=0x{target.SERVICE_PRODUCER:016x}", f"producer=0x{target.KERNEL_PRODUCER:016x}"),
            good.replace("index=0x0000000000000000 count=0x0000000000000003",
                         "index=0x0000000000000000 count=0x0000000000000002", 1),
            good.replace("owner_mask=0x000000000001ffff", "owner_mask=0x0000000000000001", 1),
            good.replace("reserved0=0x0000000000000000", "reserved0=0x0000000000000001", 1),
            good + lines[-1],
        ):
            with self.subTest(damaged=damaged[-120:]), self.assertRaises(ValueError):
                target.parse_target_output(damaged, 0)
        with self.assertRaises(ValueError):
            target.parse_target_output(good, 1)

    def test_terminal_composition_preserves_original_evidence_and_rejects_split_tail(self):
        raw = "".join(line.removeprefix("# ") for line in target_transcript().splitlines(keepends=True))
        self.assertEqual(target.PASS_LINE + "\n", target.checked_terminal_prefix(raw, 0))
        for damaged in (
            raw.replace("MICROS_VM01_CAPTURE_END", "MICROS_STACK_OBSERVATION late\nMICROS_VM01_CAPTURE_END"),
            raw + target.PASS_LINE + "\n",
            "".join(line.removeprefix("# ") for line in target_transcript(1).splitlines(keepends=True)),
        ):
            with self.assertRaises(ValueError):
                target.checked_terminal_prefix(damaged, 0)

    def test_current_target_sites_and_capture_boundaries_are_nonvacuous(self):
        root = Path(__file__).resolve().parents[2]
        target.validate_target_observations(root)

    def test_selected_admission_and_retained_walk_evidence_cannot_be_vacuous(self):
        measured = target.parse_target_output(target_transcript(), 0)
        captures, _ = measured
        for owner, kind in (
            (Owner.KERNEL_PM, Kind.INDEX_PROBE), (Owner.KERNEL_VM, Kind.INDEX_PROBE),
            (Owner.VM_SERVICE, Kind.INDEX_PROBE), (Owner.KERNEL_MEMORY, Kind.COMPLETE_VALIDATOR_CALL),
        ):
            from tests.host.work_count_types import Totals

            totals = measured[1]
            changed = [list(row) for row in totals.per_owner]
            changed[owner][kind] = 0
            with self.subTest(owner=owner):
                self.assertTrue(target.target_failures((captures, Totals(tuple(tuple(row) for row in changed), totals.per_kind))))

    def test_target_objects_reject_missing_duplicate_or_foreign_compiler_ownership(self):
        import shlex
        from tempfile import TemporaryDirectory

        with TemporaryDirectory(prefix="micros-vm-object-owners-") as temporary:
            root = Path(temporary).resolve()
            directory = root / "build"
            directory.mkdir()
            compiler = "/closed/clang"
            names = ("kernel/vm/vm_handoff_core.c", "servers/vm/vm_validation.c")
            outputs = {name: directory / ("object-" + str(index) + ".o") for index, name in enumerate(names)}
            owners = {str(path.relative_to(directory)) for path in outputs.values()}
            commands = [{
                "directory": str(directory), "file": str(root / name), "output": str(output),
                "command": shlex.join((compiler, "-c", str(root / name), "-o", str(output))),
            } for name, output in outputs.items()]
            self.assertEqual(set(names), target._target_objects(root, directory, commands, owners, names, compiler).keys())
            changes = [commands[:-1], [*commands, commands[0]]]
            for field, value in (
                ("directory", str(root)), ("command", commands[0]["command"] + " @foreign.rsp"),
                ("command", commands[0]["command"].replace(compiler, "/foreign/clang", 1)),
                ("command", commands[0]["command"].replace("object-0.o", "foreign.o")),
            ):
                changed = copy.deepcopy(commands)
                changed[0][field] = value
                changes.append(changed)
            for changed in changes:
                with self.assertRaises(ValueError):
                    target._target_objects(root, directory, changed, owners, names, compiler)
            with self.assertRaises(ValueError):
                target._target_objects(root, directory, commands, owners - {"object-0.o"}, names, compiler)


def native_transcript(channel):
    lines = []
    for row, spec in zip(native.expected_cases(), native.CASES, strict=True):
        row.update(before_state=1, after_state=1 if row["preserved"] else 2)
        lines.append("MICROS_VM01_CASE " + json.dumps(row))
        if channel == "work":
            counts = [[0] * len(Kind) for _ in Owner]
            owner = (Owner.KERNEL_VM if row["name"].startswith("kernel-")
                     else Owner.LIB_VM if row["name"].startswith("boot-") else Owner.VM_SERVICE)
            counts[owner][Kind.COMPLETE_VALIDATOR_CALL] = spec[4]
            if row["name"] in native.GEOMETRY_EXTENTS:
                counts[owner][Kind.RANGE_VISIT] = native.GEOMETRY_EXTENTS[row["name"]].bit_length()
            lines.append("MICROS_VM01_WORK " + json.dumps({
                "version": 1, "reserved0": 0, "producer": native.PRODUCER, "case": row["case"],
                "owner_mask": native.OWNER_MASK, "reserved1": 0, "counts": counts,
            }))
    return "\n".join((*lines, "MICROS_VM01_NATIVE_PASS cases=" + str(len(native.CASES)))) + "\n"


class SelectedVmNativeTest(unittest.TestCase):
    def test_native_agreement_bounds_and_positive_diagnostics(self):
        ordinary, absent = native.parse_native_output(native_transcript("ordinary"), "ordinary")
        observed, work = native.parse_native_output(native_transcript("work"), "work")
        self.assertEqual(ordinary, observed)
        self.assertEqual((), absent)
        self.assertEqual((), native.work_failures(observed, work))
        for name, kind, value in (
            ("service-next-page", Kind.COMPLETE_VALIDATOR_CALL, 1),
            ("service-max-frames", Kind.RECORD_VISIT, 262144),
            ("service-max-ranges-pages", Kind.COMPARISON, 131072),
            ("kernel-dense-records", Kind.INDEX_PROBE, 262144),
            ("service-diagnostic", Kind.COMPLETE_VALIDATOR_CALL, 0),
            ("boot-mapping-max", Kind.RECORD_VISIT, 4096 * 4095 // 2),
            ("boot-mapping-duplicate", Kind.INDEX_PROBE, 4096 * 4095 // 2),
            ("service-range-to-index", Kind.RANGE_VISIT, 96),
            ("service-index-to-range", Kind.RANGE_VISIT, 0),
            ("kernel-global-records", Kind.INDEX_PROBE, 1260 * 1259 // 2),
            ("service-global-records", Kind.RECORD_VISIT, 1260 * 1259 // 2),
            ("service-membership-max", Kind.RECORD_VISIT, 601777),
            ("kernel-owner-unvisited", Kind.RECORD_VISIT, 33),
            ("kernel-scanned-count", Kind.RECORD_VISIT, 33),
            ("service-large-next", Kind.INDEX_PROBE, 1),
            ("service-long-fill", Kind.RECORD_VISIT, 1),
            ("kernel-long-map", Kind.COMPARISON, 1),
            ("service-stack-min", Kind.RECORD_VISIT, 5297),
            ("service-stack-max", Kind.INDEX_PROBE, 34993),
        ):
            changed = copy.deepcopy(work)
            index = next(index for index, row in enumerate(observed) if row["name"] == name)
            owner = (Owner.KERNEL_VM if name.startswith("kernel-")
                     else Owner.LIB_VM if name.startswith("boot-") else Owner.VM_SERVICE)
            changed[index]["counts"][owner][kind] = value
            with self.subTest(name=name):
                self.assertTrue(native.work_failures(observed, changed))

    def test_native_reports_reject_partial_or_borrowed_channels(self):
        good = native_transcript("work")
        lines = good.splitlines(keepends=True)
        for damaged in (
            good[:-1], "".join(lines[:-1]), "".join(lines[:1] + lines[2:]),
            good.replace('"preserved": 1', '"preserved": true', 1),
            good.replace('"extent": 4096', '"extent": 0', 1),
            good.replace('"producer": ' + str(native.PRODUCER), '"producer": ' + str(native.PRODUCER + 1), 1),
            native_transcript("ordinary"),
        ):
            with self.assertRaises(ValueError):
                native.parse_native_output(damaged, "work")

    def test_actual_native_primitive_closure_rejects_missing_sites(self):
        root = Path(__file__).resolve().parents[2]
        for path in native.NATIVE_SOURCES:
            validate_native_sites(root, path, (root / path).read_text())
        path = "servers/vm/vm_validation.c"
        original = json.loads((root / "tests/host/native_observations.json").read_text())
        for kind in ("RECORD_VISIT", "INDEX_PROBE"):
            changed = copy.deepcopy(original)
            for sites in changed["families"]["VM01"]["sites"][path].values():
                match = next((index for index, (_, site) in enumerate(sites) if site[3] == kind), None)
                if match is not None:
                    del sites[match]
                    break
            with patch("tests.host.native_observations.source_bytes", return_value=json.dumps(changed).encode()):
                with self.assertRaises(ValueError):
                    validate_native_sites(root, path, (root / path).read_text())

    def test_launcher_rejects_consumed_environment_before_python(self):
        root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            (str(root / "build/host-debug/selected_vm_test"), str(root / "build/host-debug/selected_vm_ordinary")),
            cwd=root, env={**os.environ, "__PYVENV_LAUNCHER__": "/invalid/vm01-python"},
            stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(2, result.returncode)
        self.assertEqual("", result.stdout)
        self.assertEqual("__PYVENV_LAUNCHER__ is unsupported\n", result.stderr)
        read = target.source_bytes
        mutations = (
            ("servers/vm/vm_validation.c", '"vm01-service-state"', '"missing-audit"'),
            ("kernel/vm/vm_handoff_core.c", "MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL, 1);\n    size_t loading_slot",
             "MICROS_WORK_KIND_INDEX_PROBE, 1);\n    size_t loading_slot"),
            ("kernel/trap/syscall.c", "return MICROS_VM01_DISPATCH(", "return micros_vm_handoff_handle_captured_user_ecall("),
            ("tests/qemu/vm_command_work.c", "capture->active = true;", "capture->active = false;"),
            ("kernel/tests/spawn/elf_spawn_test_report.c", "MICROS_VM01_FINISH()", "true"),
        )
        for path, before, after in mutations:
            self.assertIn(before, (root / path).read_text())

            def changed(project, requested):
                raw = read(project, requested)
                return raw.decode().replace(before, after, 1).encode() if requested == root / path else raw

            with self.subTest(path=path), patch.object(target, "source_bytes", side_effect=changed):
                with self.assertRaises(ValueError):
                    target.validate_target_observations(root)
