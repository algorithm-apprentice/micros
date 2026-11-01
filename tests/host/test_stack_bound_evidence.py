"""Independent fixed-frame and stack-epoch regression evidence."""

import unittest
import os
import json
from pathlib import Path
import subprocess
import tempfile
import struct
from dataclasses import replace
from unittest.mock import patch

from tools.stack_bound_graph import certify_stack_bounds
from tools.stack_bound_types import (
    CallSite, EdgeKind, Frame, FunctionKey, Root, StackEvidenceError,
)

ROOT = Path(__file__).resolve().parents[2]


def function(symbol, unit="kernel/fixture.c", image="kernel.elf"):
    return FunctionKey("fixture", image, unit, symbol)


def compile_fixture(root, body, metadata=True, observed=False, size_optimized=False):
    from tools.validation_context import target_compiler

    source, output = root / "fixture.c", root / "fixture.obj"
    source.write_text(body)
    command = [
        str(target_compiler(dict(os.environ))), "--target=riscv64-unknown-elf",
        "-std=c17", "-march=rv64imac_zicsr_zifencei", "-mabi=lp64",
        "-mcmodel=medany", "-msmall-data-limit=0", "-mno-save-restore",
        "-ffreestanding", "-fno-builtin", "-ffunction-sections", "-g0",
        "-I" + str(ROOT), "-I" + str(ROOT / "include"),
        "-Wall", "-Wextra", "-Werror",
        "-c", str(source), "-o", str(output),
    ]
    if metadata:
        command.extend(("-fstack-usage", "-fstack-size-section"))
    if observed:
        command.append("-finstrument-functions-after-inlining")
    if size_optimized:
        command.append("-Os")
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=60,
        env={**os.environ, "CLANG_NO_DEFAULT_CONFIG": "1"},
    )
    if result.returncode:
        raise AssertionError(result.stderr)
    (root / "compile_commands.json").write_text(json.dumps([{
        "directory": str(root), "file": str(source), "output": str(output),
        "arguments": command,
    }]))
    return source, output


def compile_console_fixture(directory, observed=False):
    from tools.stack_bound_bindings import reference_source
    from tools.stack_bound_objects import load_object

    header = reference_source(str(ROOT), "kernel/tty/uart_console_core.h")
    source = reference_source(str(ROOT), "kernel/tty/uart_console_core.c")
    source = source.replace('#include "kernel/tty/uart_console_core.h"', "", 1)
    _, output = compile_fixture(directory, header + "\n" + source, observed=observed)
    return load_object(output)


class StackBoundEvidenceTest(unittest.TestCase):
    def test_nested_frames_add_but_sequential_callees_take_the_maximum(self):
        main, first, second, leaf = map(function, ("main", "first", "second", "leaf"))
        frames = tuple(Frame(key, size) for key, size in (
            (main, 32), (first, 16), (second, 64), (leaf, 48),
        ))
        edges = (
            CallSite(main, 0, (first,)), CallSite(main, 4, (second,)),
            CallSite(second, 0, (leaf,)),
        )
        proof, = certify_stack_bounds(
            frames, edges, (Root("supervisor", main, 16384, 288),),
        )
        self.assertEqual(432, proof.bytes_used)
        self.assertEqual((main, second, leaf), proof.witness)
        self.assertEqual(16384 - 432, proof.bytes_remaining)

    def test_local_symbol_spelling_does_not_merge_translation_units(self):
        main = function("main")
        first = function("helper", "kernel/first.c")
        second = function("helper", "kernel/second.c")
        proof, = certify_stack_bounds(
            (Frame(main, 32), Frame(first, 16), Frame(second, 64)),
            (CallSite(main, 0, (first, second)),),
            (Root("supervisor", main, 16384),),
        )
        self.assertEqual(96, proof.bytes_used)
        self.assertEqual((main, second), proof.witness)

    def test_unknown_dynamic_duplicate_and_unbound_evidence_fails(self):
        main, absent = function("main"), function("absent")
        cases = (
            ((Frame(main, 32), Frame(main, 32)), (), "duplicate"),
            ((Frame(main, 32, "dynamic"),), (), "fixed"),
            ((Frame(main, True),), (), "uint64"),
            ((Frame(main, -1),), (), "uint64"),
            ((Frame(main, 32),), (CallSite(main, 0, (absent,)),), "unknown"),
            ((Frame(main, 32),), (CallSite(main, 0, ()),), "unbound"),
            (
                (Frame(main, 32),),
                (CallSite(main, 0, (main,)), CallSite(main, 0, (main,))),
                "duplicate",
            ),
        )
        for frames, edges, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(
                StackEvidenceError, message,
            ):
                certify_stack_bounds(frames, edges, (Root("supervisor", main, 16384),))

    def test_recursion_is_rejected_even_with_small_constant_frames(self):
        first, second = function("first"), function("second")
        with self.assertRaisesRegex(StackEvidenceError, "recursive"):
            certify_stack_bounds(
                (Frame(first, 16), Frame(second, 16)),
                (CallSite(first, 0, (second,)), CallSite(second, 0, (first,))),
                (Root("supervisor", first, 16384),),
            )

    def test_epoch_pivot_requires_an_independently_budgeted_target_root(self):
        primary, emergency = function("primary"), function("emergency")
        frames = (Frame(primary, 8192), Frame(emergency, 1024))
        edges = (
            CallSite(primary, 0, (emergency,), EdgeKind.RESET),
            CallSite(emergency, 0, (primary,), EdgeKind.RESET),
        )
        roots = (
            Root("primary", primary, 16384, 288),
            Root("emergency", emergency, 4096),
        )
        proofs = certify_stack_bounds(frames, edges, roots)
        self.assertEqual((8480, 1024), tuple(proof.bytes_used for proof in proofs))
        with self.assertRaisesRegex(StackEvidenceError, "epoch"):
            certify_stack_bounds(frames, edges, roots[:1])

    def test_startup_occupancy_budget_excess_and_overflow_fail_closed(self):
        main = function("main", "lib/runtime/application.c", "application.elf")
        proof, = certify_stack_bounds(
            (Frame(main, 2048),), (), (Root("application", main, 4096, 2048),),
        )
        self.assertEqual(4096, proof.bytes_used)
        for size, occupancy, message in (
            (2064, 2048, "budget"), ((1 << 64) - 1, 1, "overflow"),
        ):
            with self.subTest(size=size), self.assertRaisesRegex(
                StackEvidenceError, message,
            ):
                certify_stack_bounds(
                    (Frame(main, size),), (),
                    (Root("application", main, 4096, occupancy),),
                )

    def test_a_tail_label_without_release_proof_keeps_the_caller_frame(self):
        main, leaf = function("main"), function("leaf")
        proof, = certify_stack_bounds(
            (Frame(main, 32), Frame(leaf, 64)),
            (CallSite(main, 0, (leaf,), EdgeKind.TAIL),),
            (Root("supervisor", main, 16384),),
        )
        self.assertEqual(96, proof.bytes_used)

    def test_actual_compiler_frames_agree_and_metadata_preserves_loaded_code(self):
        from tools.stack_bound_artifacts import allocatable_object_bytes, load_c_object

        body = (
            "static int helper(int x) {\n"
            "    volatile unsigned char bytes[80];\n"
            "    bytes[0] = (unsigned char)x; return bytes[0];\n"
            "}\n"
            "int probe(int x);\nint probe(int x) { return helper(x) + 1; }\n"
        )
        with tempfile.TemporaryDirectory(prefix="micros-stack-compiler-") as directory:
            root = Path(directory)
            ordinary = root / "ordinary"
            metadata = root / "metadata"
            ordinary.mkdir()
            metadata.mkdir()
            _, original = compile_fixture(ordinary, body, False)
            source, measured = compile_fixture(metadata, body)
            self.assertEqual(allocatable_object_bytes(original), allocatable_object_bytes(measured))
            evidence = load_c_object(
                measured, source, variant="fixture", image="kernel.elf",
                translation_unit="kernel/fixture.c",
            )
            self.assertEqual({"probe", "helper"}, {
                frame.function.symbol for frame in evidence.frames
            })
            call, = evidence.calls
            self.assertEqual(("probe", "helper"), (call.caller.symbol, call.target_symbol))
            usage = measured.with_suffix(".su")
            lines = usage.read_text().splitlines()
            fields = lines[0].split("\t")
            fields[-2] = str(int(fields[-2]) + 16)
            lines[0] = "\t".join(fields)
            usage.write_text("\n".join(lines) + "\n")
            with self.assertRaisesRegex(StackEvidenceError, "disagree"):
                load_c_object(
                    measured, source, variant="fixture", image="kernel.elf",
                    translation_unit="kernel/fixture.c",
                )

    def test_hidden_inline_stack_movement_is_not_a_compiler_frame(self):
        from tools.stack_bound_artifacts import load_c_object

        with tempfile.TemporaryDirectory(prefix="micros-stack-inline-") as directory:
            source, output = compile_fixture(
                Path(directory),
                "int probe(int x);\nint probe(int x) {\n"
                '    __asm__ volatile("addi sp, sp, -32" : : : "memory");\n'
                "    return x;\n}\n",
            )
            with self.assertRaisesRegex(StackEvidenceError, "stack.*movement|stack.*frame"):
                load_c_object(
                    output, source, variant="fixture", image="kernel.elf",
                    translation_unit="kernel/fixture.c",
                )

    def test_private_linked_symbols_join_by_object_and_address(self):
        from tools.stack_bound_link import FunctionLocation, join_function_locations
        from tools.user_elf_format import ElfSymbol

        first = FunctionLocation(
            function("helper", "kernel/first.c"),
            "CMakeFiles/micros.dir/kernel/first.c.obj", ".text.helper", 0, 32,
        )
        second = FunctionLocation(
            function("helper", "kernel/second.c"),
            "CMakeFiles/micros.dir/kernel/second.c.obj", ".text.helper", 0, 64,
        )
        text = (
            "             VMA              LMA     Size Align Out     In      Symbol\n"
            "        40000100         40000100       20     2         CMakeFiles/micros.dir/kernel/first.c.obj:(.text.helper)\n"
            "        40000120         40000120       40     2         CMakeFiles/micros.dir/kernel/second.c.obj:(.text.helper)\n"
        )
        symbols = (
            ElfSymbol("helper", 0, 2, 1, 0x40000100, 32),
            ElfSymbol("helper", 0, 2, 1, 0x40000120, 64),
        )
        self.assertEqual(
            {first.function: 0x40000100, second.function: 0x40000120},
            join_function_locations((first, second), text, symbols),
        )
        for locations, emitted in (
            ((first, second), symbols[:1]),
            ((replace(first, size=48), second), symbols),
            ((first, first), symbols),
        ):
            with self.subTest(locations=locations), self.assertRaises(StackEvidenceError):
                join_function_locations(locations, text, emitted)

    def test_linked_call_pair_cannot_borrow_another_target_register(self):
        from tests.host.user_elf_test_fixture import build_elf
        from tools.stack_bound_link import direct_call_target
        from tools.user_elf_parse import parse_elf

        data, offsets = build_elf()
        base = struct.unpack_from("<Q", data, 24)[0]
        position = offsets["text_offset"] + 64
        struct.pack_into("<II", data, position, 0x97, 0xfd0080e7)
        self.assertEqual(
            (base + 16, EdgeKind.CALL), direct_call_target(parse_elf(bytes(data)), base + 64),
        )
        word = struct.unpack_from("<I", data, position + 4)[0]
        struct.pack_into("<I", data, position + 4, (word & ~(31 << 15)) | (5 << 15))
        with self.assertRaisesRegex(StackEvidenceError, "call.*register"):
            direct_call_target(parse_elf(bytes(data)), base + 64)

    def test_static_scope_includes_nonfixture_production_images(self):
        from tools.stack_bound_build import static_profiles

        profiles = {profile.configure: profile for profile in static_profiles(ROOT)}
        for name in (
            "riscv64-tty-service-image", "riscv64-ramfs-service-image",
            "riscv64-vfs-service-image", "riscv64-application-images",
        ):
            self.assertIn(name, profiles)
        vfs = profiles["riscv64-vfs-service-image"]
        self.assertNotIn("MICROS_BUILD_VFS_SERVICE_TEST", dict(vfs.variables))
        self.assertNotIn("MICROS_BUILD_ELF_SPAWN_TEST", dict(vfs.variables))
        self.assertIn("riscv64-debug", profiles)

    def test_ordinary_link_map_adds_no_frame_flags_or_canonicalization(self):
        import json

        with tempfile.TemporaryDirectory(prefix="micros-stack-passive-map-") as directory:
            root = Path(directory)
            (root / "fixture.c").write_text("int probe(int x);\nint probe(int x) { return x + 1; }\n")
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.25)\nproject(stack_map_fixture C)\n"
                "foreach(app IN ITEMS runtime minimal)\n"
                "add_executable(micros_application_${app}_probe fixture.c)\n"
                "set_target_properties(micros_application_${app}_probe PROPERTIES\n"
                '    OUTPUT_NAME "micros-application-${app}-probe" SUFFIX ".elf"\n'
                '    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/applications")\n'
                "target_compile_options(micros_application_${app}_probe PRIVATE\n"
                "    -march=rv64imac_zicsr_zifencei -mabi=lp64 -ffreestanding -fno-builtin -g0)\n"
                "target_link_options(micros_application_${app}_probe PRIVATE\n"
                '    -nostdlib -static "--ld-path=${MICROS_LD_LLD}" "-Wl,-e,probe")\n'
                "endforeach()\n"
            )
            environment = {**os.environ, "CLANG_NO_DEFAULT_CONFIG": "1"}
            outputs = []
            for mode in ("plain", "ordinary"):
                build = root / mode
                arguments = [
                    "cmake", "-S", str(root), "-B", str(build), "-G", "Ninja",
                    "-DCMAKE_TOOLCHAIN_FILE=" + str(ROOT / "cmake/toolchains/riscv64-clang.cmake"),
                    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                ]
                if mode == "ordinary":
                    arguments.extend((
                        "-DCMAKE_PROJECT_INCLUDE=" + str(ROOT / "tests/stack/StackMetadata.cmake"),
                        "-DMICROS_STACK_BUILD_MODE=ordinary",
                    ))
                configured = subprocess.run(
                    arguments, capture_output=True, text=True, timeout=60, env=environment,
                )
                self.assertEqual(0, configured.returncode, configured.stdout + configured.stderr)
                completed = subprocess.run(
                    ("cmake", "--build", str(build)),
                    capture_output=True, text=True, timeout=60, env=environment,
                )
                self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
                outputs.append(tuple(
                    (build / f"applications/micros-application-{app}-probe.elf").read_bytes()
                    for app in ("runtime", "minimal")
                ))
            self.assertEqual(outputs[0], outputs[1])
            ordinary = root / "ordinary"
            self.assertTrue((ordinary / "applications/micros-application-runtime-probe.map").is_file())
            self.assertTrue((ordinary / "applications/micros-application-minimal-probe.map").is_file())
            command = json.loads((ordinary / "compile_commands.json").read_text())[0]["command"]
            self.assertNotIn("-fstack-", command)
            self.assertNotIn("-finstrument-", command)
            self.assertFalse(tuple(ordinary.rglob("*.su")))

    def test_call_catalog_preserves_local_scope_and_cannot_borrow_foreign_targets(self):
        from tests.host.user_elf_test_fixture import build_elf
        from tools.stack_bound_catalog import DefinedFunction, resolve_linked_calls
        from tools.stack_bound_types import SymbolCall
        from tools.user_elf_parse import parse_elf

        data, offsets = build_elf()
        base = struct.unpack_from("<Q", data, 24)[0]
        struct.pack_into("<II", data, offsets["text_offset"] + 64, 0x97, 0xfd0080e7)
        image = parse_elf(bytes(data))
        caller = function("caller", "kernel/first.c")
        local = function("helper", "kernel/first.c")
        peer = function("helper", "kernel/second.c")
        definitions = (
            DefinedFunction(caller, base + 64, 8, True),
            DefinedFunction(local, base + 16, 4, False),
            DefinedFunction(peer, base + 32, 4, True),
        )
        call = SymbolCall(caller, 0, "helper")
        edges, pending = resolve_linked_calls(image, (call,), definitions)
        self.assertEqual((), pending)
        self.assertEqual((local,), edges[0].targets)
        missing = replace(call, target_symbol="unknown")
        edges, pending = resolve_linked_calls(image, (missing,), definitions)
        self.assertEqual((), edges)
        self.assertEqual((missing,), pending)
        foreign = replace(local, image="foreign.elf")
        with self.assertRaises(StackEvidenceError):
            resolve_linked_calls(image, (call,), (
                definitions[0], DefinedFunction(foreign, base + 16, 4, False), definitions[2],
            ))
        with self.assertRaises(StackEvidenceError):
            resolve_linked_calls(image, (replace(call, offset=8),), definitions)

    def test_callback_register_retains_its_argument_and_member_provenance(self):
        from tools.stack_bound_artifacts import load_c_object
        from tools.stack_bound_callbacks import indirect_member_offset, require_callback_argument
        from tools.stack_bound_objects import function_symbols, load_object

        with tempfile.TemporaryDirectory(prefix="micros-stack-callback-origin-") as directory:
            source, output = compile_fixture(
                Path(directory),
                "typedef int (*callback)(int);\n"
                "struct io { callback first; callback second; };\n"
                "int probe(struct io *first, struct io *second, int x);\n"
                "int probe(struct io *first, struct io *second, int x) {\n"
                "    (void)first; return second->second(x);\n"
                "}\n",
            )
            evidence = load_c_object(
                output, source, variant="fixture", image="kernel.elf",
                translation_unit="kernel/fixture.c",
            )
            obj = load_object(output)
            symbol = function_symbols(obj)["probe"]
            call, = evidence.calls
            self.assertEqual(8, indirect_member_offset(obj, symbol, call.offset))
            require_callback_argument(obj, symbol, call.offset, 1, (8,))
            with self.assertRaisesRegex(StackEvidenceError, "argument"):
                require_callback_argument(obj, symbol, call.offset, 0, (8,))
            with self.assertRaisesRegex(StackEvidenceError, "member"):
                require_callback_argument(obj, symbol, call.offset, 1, (0,))

    def test_callback_control_flow_merge_cannot_choose_one_pointer_origin(self):
        from tools.stack_bound_artifacts import load_c_object
        from tools.stack_bound_callbacks import require_callback_argument
        from tools.stack_bound_objects import function_symbols, load_object

        with tempfile.TemporaryDirectory(prefix="micros-stack-callback-merge-") as directory:
            source, output = compile_fixture(
                Path(directory),
                "typedef int (*callback)(int);\n"
                "struct io { callback first; callback second; };\n"
                "int probe(struct io *first, struct io *second, int x);\n"
                "int probe(struct io *first, struct io *second, int x) {\n"
                "    if (x) { first = second; }\n"
                "    return first->second(x);\n"
                "}\n",
            )
            evidence = load_c_object(
                output, source, variant="fixture", image="kernel.elf",
                translation_unit="kernel/fixture.c",
            )
            obj = load_object(output)
            call, = evidence.calls
            with self.assertRaisesRegex(StackEvidenceError, "argument"):
                require_callback_argument(
                    obj, function_symbols(obj)["probe"], call.offset, 0, (8,),
                )

    def test_callback_provider_admission_requires_the_live_complete_constructor(self):
        from tools.stack_bound_bindings import group_provider_targets
        from tools.stack_bound_callback_contracts import GROUPS

        members = (
            "client_validate", "client_copy", "backend_grant_create",
            "backend_grant_revoke", "ramfs_call", "tty_call",
        )
        source_text = (
            "void micros_service_main(void) {\n"
            "    const struct micros_vfs_io io = {\n"
            + "".join(f"        .{member} = {member},\n" for member in members)
            + "    };\n    use(&io);\n}\n"
        )
        caller = function("probe", "servers/vfs/vfs_transfer.c")
        providers = {
            replace(caller, translation_unit="servers/vfs/vfs_service.c", symbol=member)
            for member in members
        }
        group = next(group for group in GROUPS if group.identifier == "vfs-io")
        with tempfile.TemporaryDirectory(prefix="micros-stack-providers-") as directory:
            root = Path(directory)
            source = root / "servers/vfs/vfs_service.c"
            source.parent.mkdir(parents=True)
            source.write_text(source_text)
            with patch("tools.stack_bound_bindings.reference_source", return_value=source_text):
                targets = group_provider_targets(root, group, caller, providers, frozenset())
                expected = replace(caller, translation_unit="servers/vfs/vfs_service.c", symbol="client_copy")
                self.assertEqual((expected,), targets["client_copy"])
                missing = providers - {
                    replace(caller, translation_unit="servers/vfs/vfs_service.c", symbol="tty_call"),
                }
                with self.assertRaisesRegex(StackEvidenceError, "provider"):
                    group_provider_targets(root, group, caller, missing, frozenset())
                source.write_text(
                    source_text.replace(".client_copy = client_copy", ".client_copy = foreign_provider")
                    + source_text.replace("micros_service_main", "unused"),
                )
                with self.assertRaisesRegex(StackEvidenceError, "provider"):
                    group_provider_targets(root, group, caller, providers, frozenset())

    def test_separate_visitor_provider_is_omitted_only_with_its_garbage_collected_constructor(self):
        from tools.stack_bound_bindings import group_provider_targets
        from tools.stack_bound_callback_contracts import GROUPS

        group = next(group for group in GROUPS if group.identifier == "kernel-visitor")
        caller = function("visit_leaf_table", "kernel/memory/user_address_space_mapping.c")
        validator = replace(caller, translation_unit="kernel/vm/vm_snapshot_validation.c", symbol="validate_mapping")
        validate_constructor = replace(validator, symbol="micros_vm_snapshot_validate_current")
        collector = replace(caller, translation_unit="kernel/vm/vm_snapshot.c", symbol="collect_mapping")
        collect_constructor = replace(collector, symbol="collect_snapshot_address_spaces")
        active = {validator, validate_constructor}
        targets = group_provider_targets(ROOT, group, caller, active, frozenset())
        self.assertEqual((validator,), targets["visitor"])
        for incomplete in (
            active | {collector},
            active | {collect_constructor},
        ):
            with self.subTest(incomplete=incomplete), self.assertRaisesRegex(StackEvidenceError, "provider|constructor"):
                group_provider_targets(ROOT, group, caller, incomplete, frozenset())
        targets = group_provider_targets(
            ROOT, group, caller, active | {collector, collect_constructor}, frozenset(),
        )
        self.assertEqual((collector, validator), targets["visitor"])

    def test_callback_binding_requires_the_complete_source_machine_and_origin_inventory(self):
        from tools.stack_bound_artifacts import load_c_object
        from tools.stack_bound_bindings import bind_current_callbacks
        from tools.stack_bound_catalog import DefinedFunction
        from tools.stack_bound_collect import LinkedCatalog
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_types import SourceOrigin

        members = (
            "client_validate", "client_copy", "backend_grant_create",
            "backend_grant_revoke", "ramfs_call", "tty_call",
        )
        header = (
            "typedef int (*callback)(int);\nstruct micros_vfs_io {\n"
            + "".join(f"    callback {member};\n" for member in members) + "};\n"
        )
        body = (
            header + "".join(f"int {member}(int x) {{ return x + 1; }}\n" for member in members)
            + "void micros_service_main(void) {\n"
            "    static const struct micros_vfs_io io = {\n"
            + "".join(f"        .{member} = {member},\n" for member in members)
            + "    };\n    (void)io;\n}\n"
            "int probe(const int arguments[5], struct micros_vfs_io *io, int x);\n"
            "int probe(const int arguments[5], struct micros_vfs_io *io, int x) {\n"
            "    (void)arguments; int first = io->client_copy(x);\n"
            "    return first + io->client_copy(x + 1);\n}\n"
        )
        with tempfile.TemporaryDirectory(prefix="micros-stack-callback-inventory-") as directory:
            root = Path(directory)
            source, output = compile_fixture(root, body)
            unit = "servers/vfs/vfs_service.c"
            evidence = load_c_object(
                output, source, variant="fixture", image="kernel.elf", translation_unit=unit,
            )
            (root / unit).parent.mkdir(parents=True)
            (root / unit).write_text(body)
            (root / "servers/vfs/vfs_core.h").write_text(header)
            (root / "CMakeCache.txt").write_text("")
            functions = function_symbols(load_object(output))
            catalog = LinkedCatalog(
                evidence.frames,
                tuple(DefinedFunction(
                    frame.function, 0x40000000, functions[frame.function.symbol].size, True,
                ) for frame in evidence.frames),
                (), evidence.calls,
                tuple((frame.function, "fixture.obj") for frame in evidence.frames),
                tuple(SourceOrigin(origin.function, unit, origin.line) for origin in evidence.origins),
            )
            with patch(
                "tools.stack_bound_bindings.reference_source",
                side_effect=lambda _, path: header if path.endswith("vfs_core.h") else body,
            ):
                edges, pending = bind_current_callbacks(root, root, catalog)
                self.assertEqual(2, len(edges))
                self.assertEqual((), pending)
                for changed in (
                    replace(catalog, pending=catalog.pending[:1]),
                    replace(catalog, objects=catalog.objects[:-1]),
                    replace(catalog, origins=tuple(
                        replace(origin, source="servers/vfs/vfs_core.h")
                        if origin.function.symbol == "probe" else origin
                        for origin in catalog.origins
                    )),
                ):
                    with self.subTest(changed=changed), self.assertRaisesRegex(
                        StackEvidenceError, "occurrence|inventory|source",
                    ):
                        bind_current_callbacks(root, root, changed)

    def test_nested_callback_carrier_does_not_also_match_a_bare_backend(self):
        from tests.host.c_source_records import function_records
        from tools.stack_bound_bindings import source_callback_occurrences
        from tools.stack_bound_callback_contracts import GROUPS

        record = function_records(
            "int probe(struct pm_coordinator_spawn *spawn) {\n"
            "    return spawn->backend->reserve(spawn->context, &result);\n}\n"
        )["probe"]
        group = next(group for group in GROUPS if group.identifier == "pm-backend")
        occurrence, = source_callback_occurrences(record, group)
        self.assertEqual(("spawn", "reserve", (8, 0)), (
            occurrence[1].parameter, occurrence[2], occurrence[3],
        ))

    def test_qualification_uses_actual_normal_return_dominance_not_source_order(self):
        from tools.stack_bound_artifacts import load_c_object
        from tools.stack_bound_flow import normal_return_dominated_calls
        from tools.stack_bound_objects import function_symbols, load_object

        for bypass in (False, True):
            with self.subTest(bypass=bypass):
                with tempfile.TemporaryDirectory(prefix="micros-stack-dominance-") as directory:
                    body = (
                        "void transition(void);\nvoid use(void);\nint probe(int x);\n"
                        "int probe(int x) {\n"
                        + ("    if (x) { transition(); }\n" if bypass else
                           "    if (x) { use(); }\n    transition();\n")
                        + "    use(); return x;\n}\n"
                    )
                    source, output = compile_fixture(Path(directory), body)
                    evidence = load_c_object(
                        output, source, variant="fixture", image="kernel.elf",
                        translation_unit="kernel/fixture.c",
                    )
                    obj = load_object(output)
                    symbol = function_symbols(obj)["probe"]
                    transition, = (call for call in evidence.calls if call.target_symbol == "transition")
                    consumers = tuple(call.offset for call in evidence.calls if call.target_symbol == "use")
                    expected = frozenset() if bypass else frozenset(consumers[-1:])
                    self.assertEqual(
                        expected, normal_return_dominated_calls(obj, symbol, transition.offset),
                    )
                    with self.assertRaisesRegex(StackEvidenceError, "call|continuation"):
                        normal_return_dominated_calls(obj, symbol, 0)

    def test_machine_global_argument_cannot_borrow_a_same_shaped_object(self):
        from tools.stack_bound_artifacts import load_c_object
        from tools.stack_bound_objects import function_symbols, load_object
        from tools.stack_bound_values import register_inputs

        for selected in ("console_state", "foreign_state"):
            with self.subTest(selected=selected):
                with tempfile.TemporaryDirectory(prefix="micros-stack-global-argument-") as directory:
                    source, output = compile_fixture(
                        Path(directory),
                        "__attribute__((used)) static int console_state;\n"
                        "__attribute__((used)) static int foreign_state;\n"
                        "void use(const int *state);\nvoid probe(void);\n"
                        f"void probe(void) {{ use(&{selected}); }}\n",
                    )
                    evidence = load_c_object(
                        output, source, variant="fixture", image="kernel.elf",
                        translation_unit="kernel/fixture.c",
                    )
                    obj = load_object(output)
                    call, = evidence.calls
                    inputs = register_inputs(obj, function_symbols(obj)["probe"])
                    value = inputs[call.offset][10]
                    self.assertEqual(("symbol", selected), value[:2])
                    if selected == "foreign_state":
                        self.assertNotEqual("console_state", value[1])

    def test_failed_transition_cannot_reach_a_ready_normal_return(self):
        from tools.stack_bound_artifacts import load_c_object
        from tools.stack_bound_fatal_machine import zero_success_returns
        from tools.stack_bound_objects import function_symbols, load_object

        for bypass in (False, True):
            with self.subTest(bypass=bypass):
                with tempfile.TemporaryDirectory(prefix="micros-stack-success-guard-") as directory:
                    source, output = compile_fixture(
                        Path(directory),
                        "int transition(void);\nvoid probe(int x);\nvoid probe(int x) {\n"
                        + ("    if (x) {\n" if bypass else "    (void)x;\n")
                        + "    if (transition() != 0) { for (;;) {} }\n"
                        + ("    }\n" if bypass else "")
                        + "}\n",
                    )
                    evidence = load_c_object(
                        output, source, variant="fixture", image="kernel.elf",
                        translation_unit="kernel/fixture.c",
                    )
                    obj = load_object(output)
                    call, = evidence.calls
                    if bypass:
                        with self.assertRaisesRegex(StackEvidenceError, "return|dominance"):
                            zero_success_returns(obj, function_symbols(obj)["probe"], call.offset)
                    else:
                        self.assertIsInstance(
                            zero_success_returns(obj, function_symbols(obj)["probe"], call.offset), int,
                        )

    def test_actual_system_reset_wrapper_binds_exact_scalar_ecall_registers(self):
        from tools.stack_bound_fatal_machine import require_srst_registers
        from tools.stack_bound_objects import function_symbols, instructions, load_object, section_bytes

        with tempfile.TemporaryDirectory(prefix="micros-stack-srst-operands-") as directory:
            body = (ROOT / "arch/riscv64/sbi.c").read_text().replace(
                '#include "arch/riscv64/platform.h"', "#include <stdint.h>",
            )
            _, output = compile_fixture(Path(directory), body)
            obj = load_object(output)
            symbol = function_symbols(obj)["sbi_system_reset"]
            self.assertIsInstance(require_srst_registers(obj, symbol), int)
            section = obj.sections[symbol.section_index]
            data = section_bytes(obj, section)[symbol.value:symbol.value + symbol.size]
            address, word = next(
                (address, word) for address, width, word in instructions(data, symbol.value)
                if width == 4 and word & 0x7f == 0x37
            )
            changed = bytearray(obj.data)
            struct.pack_into("<I", changed, section.offset + address, word ^ (1 << 20))
            output.write_bytes(changed)
            with self.assertRaisesRegex(StackEvidenceError, "SRST|register|operand"):
                changed_object = load_object(output)
                require_srst_registers(changed_object, function_symbols(changed_object)["sbi_system_reset"])

    def test_linked_code_and_data_must_equal_the_complete_relocated_object(self):
        from tools.stack_bound_objects import load_object, function_symbols
        from tools.stack_bound_relocations import verify_linked_object
        from tools.stack_bound_collect import collect_linked_c
        from tools.user_elf_parse import load_elf

        with tempfile.TemporaryDirectory(prefix="micros-stack-linked-bytes-") as temporary:
            directory = Path(temporary).resolve()
            _, output = compile_fixture(
                directory,
                "static volatile unsigned state = 7;\n"
                "static int helper(int x, const char *text) {\n"
                "    state = (unsigned)x; return text[0] + x;\n"
                "}\n"
                "int probe(int x);\n"
                'int probe(int x) { if (x) return helper(x, "prefixsuffix");\n'
                '    return helper((int)state, "suffix"); }\n',
            )
            result = subprocess.run(
                [
                    os.environ["MICROS_LD_LLD"], "-m", "elf64lriscv", "--no-relax",
                    "--gc-sections", "--entry=probe", "-Ttext=0x40000000",
                    "--Map=fixture.map", "-o", "fixture.elf", output.name,
                ],
                cwd=directory, capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            obj = load_object(output)
            image = load_elf(directory / "fixture.elf")
            map_text = (directory / "fixture.map").read_text()
            owners = verify_linked_object(obj, output.name, image, map_text)
            self.assertTrue(owners)
            with patch("tools.stack_bound_collect.compiler_dependencies", return_value={}):
                catalog = collect_linked_c(directory, directory, "fixture.elf", "fixture")
            self.assertEqual({"probe", "helper"}, {
                frame.function.symbol for frame in catalog.frames
            })
            probe = function_symbols(obj)["probe"]
            state, = (symbol for symbol in obj.symbols if symbol.name == "state")
            readonly, = (section for section in image.sections if section.name == ".rodata")
            for address in (
                owners[probe.section_index] + probe.value,
                owners[state.section_index] + state.value,
                readonly.address,
            ):
                program, = (
                    program for program in image.programs if program.program_type == 1
                    and program.virtual_address <= address
                    < program.virtual_address + program.file_size
                )
                position = program.offset + address - program.virtual_address
                data = bytearray(image.data)
                data[position] ^= 0x10
                with self.subTest(address=address), self.assertRaises(StackEvidenceError):
                    verify_linked_object(
                        obj, output.name, replace(image, data=bytes(data)), map_text,
                    )
                try:
                    (directory / "fixture.elf").write_bytes(data)
                    with (
                        self.subTest(collection_address=address),
                        patch("tools.stack_bound_collect.compiler_dependencies", return_value={}),
                        self.assertRaises(StackEvidenceError),
                    ):
                        collect_linked_c(directory, directory, "fixture.elf", "fixture")
                finally:
                    (directory / "fixture.elf").write_bytes(image.data)
            with self.assertRaises(StackEvidenceError):
                verify_linked_object(
                    obj, output.name, image,
                    map_text.replace(":(.text.probe)", ":(.text.foreign)"),
                )

    def test_actual_pc_relative_store_binds_its_owned_upper_register(self):
        from tools.stack_bound_objects import load_object, function_symbols
        from tools.stack_bound_relocations import verify_linked_object
        from tools.user_elf_parse import load_elf

        with tempfile.TemporaryDirectory(prefix="micros-stack-pcrel-store-") as temporary:
            directory = Path(temporary).resolve()
            source = directory / "fixture.S"
            source.write_text(
                '.section .text.probe,"ax",@progbits\n'
                ".globl probe\n.type probe,@function\nprobe:\n"
                ".Lupper: auipc t0, %pcrel_hi(state)\n"
                "sd a0, %pcrel_lo(.Lupper)(t0)\n"
                "sd a1, %pcrel_lo(.Lupper)(t0)\nret\n"
                ".size probe,.-probe\n"
                '.section .data,"aw",@progbits\n.balign 8\nstate: .quad 0\n',
            )
            command = [
                os.environ["MICROS_CLANG"], "--target=riscv64-unknown-elf",
                "-march=rv64imac_zicsr_zifencei", "-mabi=lp64", "-c",
                str(source), "-o", str(directory / "fixture.obj"),
            ]
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            result = subprocess.run(
                [
                    os.environ["MICROS_LD_LLD"], "-m", "elf64lriscv", "--no-relax",
                    "--entry=probe", "-Ttext=0x40000000", "--Map=fixture.map",
                    "-o", "fixture.elf", "fixture.obj",
                ],
                cwd=directory, capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            obj = load_object(directory / "fixture.obj")
            image = load_elf(directory / "fixture.elf")
            text = (directory / "fixture.map").read_text()
            owners = verify_linked_object(obj, "fixture.obj", image, text)
            probe = function_symbols(obj)["probe"]
            section = obj.sections[probe.section_index]
            data = bytearray(obj.data)
            store = int.from_bytes(data[section.offset + 4:section.offset + 8], "little")
            store = store & ~(31 << 15) | 6 << 15
            data[section.offset + 4:section.offset + 8] = store.to_bytes(4, "little")
            with self.assertRaises(StackEvidenceError):
                verify_linked_object(replace(obj, data=bytes(data)), "fixture.obj", image, text)
            self.assertIn(probe.section_index, owners)

    def test_console_machine_relation_requires_valid_magic_and_exact_ready_fields(self):
        from tools.stack_bound_console import console_machine_proof

        magic = 0x554152544f574e52
        with tempfile.TemporaryDirectory(prefix="micros-stack-console-relation-") as temporary:
            obj = compile_console_fixture(Path(temporary).resolve())
            proof = console_machine_proof(ROOT, obj)
            for row in proof.transitions:
                with self.subTest(before=row.before):
                    if row.before is None:
                        self.assertEqual((1, None), (row.result, row.after))
                    elif row.before[0] != magic:
                        self.assertEqual((3, row.before), (row.result, row.after))
                    else:
                        self.assertEqual((0, (magic, 3, True)), (row.result, row.after))
            ready, = (row for row in proof.predicates if row.before == (magic, 3, True))
            self.assertEqual((1, ready.before, ()), (ready.result, ready.after, ready.writes))
            tty, = (row for row in proof.predicates if row.before == (magic, 2, True))
            self.assertEqual(0, tty.result)

    def test_observed_console_relation_retains_the_same_ready_and_failure_contract(self):
        from tools.stack_bound_console import console_machine_proof

        with tempfile.TemporaryDirectory(prefix="micros-stack-observed-console-") as temporary:
            obj = compile_console_fixture(Path(temporary).resolve(), observed=True)
            proof = console_machine_proof(ROOT, obj)
            ready, = (row for row in proof.predicates if row.before == (0x554152544f574e52, 3, True))
            self.assertEqual((1, ()), (ready.result, ready.writes))
            invalid, = (row for row in proof.transitions if row.before == (0, 3, True))
            self.assertEqual((3, invalid.before, ()), (invalid.result, invalid.after, invalid.writes))

    def test_changed_console_owner_store_cannot_establish_ready_evidence(self):
        from tools.stack_bound_console import console_machine_proof
        from tools.stack_bound_objects import function_symbols
        from tools.stack_bound_flow import function_flow
        from tools.stack_bound_pointer import memory_action

        with tempfile.TemporaryDirectory(prefix="micros-stack-console-store-") as temporary:
            obj = compile_console_fixture(Path(temporary).resolve())
            symbol = function_symbols(obj)["micros_uart_console_panic"]
            flow = function_flow(obj, symbol)
            store, = (
                address for address, (width, word) in flow.instructions.items()
                if (action := memory_action(width, word)) is not None
                and action[0:2] == (True, 4) and action[4] == 8
            )
            addresses = tuple(flow.instructions)
            producer = addresses[addresses.index(store) - 1]
            width, word = flow.instructions[producer]
            self.assertEqual(2, width)
            data = bytearray(obj.data)
            point = obj.sections[symbol.section_index].offset + symbol.value + producer
            data[point:point + width] = (word ^ 4).to_bytes(width, "little")
            with self.assertRaises(StackEvidenceError):
                console_machine_proof(ROOT, replace(obj, data=bytes(data)))

    def test_console_relation_rejects_an_unmodeled_atomic_state_write(self):
        from tools.stack_bound_console import console_machine_proof
        from tools.stack_bound_bindings import reference_source
        from tools.stack_bound_objects import load_object

        header = reference_source(str(ROOT), "kernel/tty/uart_console_core.h")
        source = reference_source(str(ROOT), "kernel/tty/uart_console_core.c")
        anchor = (
            "    state->owner = MICROS_UART_CONSOLE_OWNER_PANIC;\n"
            "    state->quiesced = true;\n"
        )
        self.assertEqual(1, source.count(anchor))
        source = source.replace(anchor, anchor + (
            '    __asm__ volatile("amoswap.w zero, zero, (%0)"\n'
            '        : : "r"(state) : "memory");\n'
        ))
        source = source.replace('#include "kernel/tty/uart_console_core.h"', "", 1)
        with tempfile.TemporaryDirectory(prefix="micros-stack-console-atomic-") as temporary:
            _, output = compile_fixture(Path(temporary).resolve(), header + "\n" + source)
            with self.assertRaises(StackEvidenceError):
                console_machine_proof(ROOT, load_object(output))

    def test_uart_preservation_rejects_console_state_and_interrupt_enable_writes(self):
        from tools.stack_bound_fatal_effects import uart_machine_effects
        from tools.stack_bound_bindings import reference_source
        from tools.stack_bound_objects import function_symbols, load_object

        source = reference_source(str(ROOT), "arch/riscv64/uart.c")
        anchor = "void uart_flush(void)\n{\n"
        self.assertEqual(1, source.count(anchor))
        with tempfile.TemporaryDirectory(prefix="micros-stack-uart-effects-") as temporary:
            directory = Path(temporary).resolve()
            for index, addition in enumerate((
                "", "    uart_console_state.owner = MICROS_UART_CONSOLE_OWNER_EARLY;\n",
                '    __asm__ volatile("csrsi sstatus, 2" : : : "memory");\n',
            )):
                root = directory / str(index)
                root.mkdir()
                _, output = compile_fixture(root, source.replace(anchor, anchor + addition))
                obj = load_object(output)
                symbol = function_symbols(obj)["uart_flush"]
                if not addition:
                    uart_machine_effects(obj, symbol)
                else:
                    with self.subTest(addition=addition), self.assertRaises(StackEvidenceError):
                        uart_machine_effects(obj, symbol)

    def test_uart_seizure_requires_both_interrupt_clears_and_exact_mmio_transition(self):
        from tools.stack_bound_fatal_effects import uart_machine_effects
        from tools.stack_bound_bindings import reference_source
        from tools.stack_bound_objects import function_symbols, load_object

        source = reference_source(str(ROOT), "arch/riscv64/uart.c")
        with tempfile.TemporaryDirectory(prefix="micros-stack-uart-seizure-") as temporary:
            directory = Path(temporary).resolve()
            for index, candidate in enumerate((
                source,
                source.replace("    riscv_external_interrupt_disable();\n", "", 1),
                source.replace("uart0[UART_DLL] = UART_DIVISOR_LSB;", "uart0[UART_DLL] = 0;", 1),
            )):
                root = directory / str(index)
                root.mkdir()
                _, output = compile_fixture(root, candidate)
                obj = load_object(output)
                symbol = function_symbols(obj)["uart_panic_seize"]
                if index == 0:
                    uart_machine_effects(obj, symbol)
                else:
                    with self.subTest(index=index), self.assertRaises(StackEvidenceError):
                        uart_machine_effects(obj, symbol)

    def test_interrupt_clear_must_dominate_every_normal_return(self):
        from tools.stack_bound_fatal_effects import uart_machine_effects
        from tools.stack_bound_objects import function_symbols, load_object

        with tempfile.TemporaryDirectory(prefix="micros-stack-clear-return-") as temporary:
            directory = Path(temporary).resolve()
            for index, guard in enumerate(("", "if (flag)")):
                root = directory / str(index)
                root.mkdir()
                _, output = compile_fixture(root, (
                    "unsigned long riscv_irq_save(unsigned long flag);\n"
                    "unsigned long riscv_irq_save(unsigned long flag) {\n"
                    "    unsigned long status = 0;\n"
                    "    (void)flag;\n"
                    f"    {guard} {{\n"
                    '        __asm__ volatile("csrrci %0, sstatus, 2" : "=r"(status) : : "memory");\n'
                    "    }\n"
                    "    return status;\n"
                    "}\n"
                ))
                obj = load_object(output)
                symbol = function_symbols(obj)["riscv_irq_save"]
                if index == 0:
                    uart_machine_effects(obj, symbol)
                else:
                    with self.assertRaises(StackEvidenceError):
                        uart_machine_effects(obj, symbol)

    def test_returning_srst_wrapper_preserves_supervisor_memory_and_interrupt_state(self):
        from tools.stack_bound_fatal_effects import sbi_machine_effects
        from tools.stack_bound_bindings import reference_source
        from tools.stack_bound_objects import function_symbols, load_object

        source = reference_source(str(ROOT), "arch/riscv64/sbi.c")
        anchor = "intptr_t sbi_system_reset(uint32_t reset_type, uint32_t reset_reason)\n{\n"
        self.assertEqual(1, source.count(anchor))
        with tempfile.TemporaryDirectory(prefix="micros-stack-srst-effects-") as temporary:
            directory = Path(temporary).resolve()
            for index, effect in enumerate((
                "", "    *(volatile unsigned long *)0x10000000 = 0;\n",
                '    __asm__ volatile("csrsi sstatus, 2" : : : "memory");\n',
            )):
                root = directory / str(index)
                root.mkdir()
                _, output = compile_fixture(root, source.replace(anchor, anchor + effect))
                obj = load_object(output)
                symbol = function_symbols(obj)["sbi_system_reset"]
                if not effect:
                    sbi_machine_effects(obj, symbol)
                else:
                    with self.subTest(effect=effect), self.assertRaises(StackEvidenceError):
                        sbi_machine_effects(obj, symbol)

    def test_fatal_context_keeps_both_live_uart_frames_and_the_ordinary_failure_edge(self):
        from tools.stack_bound_context import (
            ContextBindings, Qualifier, certify_context_bounds,
        )

        main = function("entry")
        uart = function("uart_write", "arch/riscv64/uart.c")
        panic = function("micros_panic_entry", "arch/riscv64/panic.S")
        shared = function("micros_trap_panic_entry", "arch/riscv64/panic.S")
        report = function("micros_panic_report", "kernel/diagnostic/panic.c")
        seize = function("micros_panic_seize", "kernel/diagnostic/panic.c")
        character = function("uart_write_character", "arch/riscv64/uart.c")
        frames = tuple(Frame(key, size) for key, size in (
            (main, 16), (uart, 32), (panic, 0), (shared, 48),
            (report, 80), (seize, 16), (character, 64),
        ))
        edges = (
            CallSite(main, 0, (uart,)), CallSite(uart, 0, (panic,)),
            CallSite(uart, 4, (character,)), CallSite(panic, 0, (shared,), EdgeKind.TAIL),
            CallSite(shared, 0, (report,)), CallSite(report, 0, (seize,)),
            CallSite(report, 4, (uart,)),
        )
        roots = (Root("supervisor", main, 16384, 288),)
        with self.assertRaisesRegex(StackEvidenceError, "recursive"):
            certify_stack_bounds(frames, edges, roots)
        binding = ContextBindings(
            ((report, 4),), (uart, character), (uart, 0),
        )
        proof, = certify_context_bounds(frames, edges, roots, binding)
        self.assertEqual(560, proof.bytes_used)
        self.assertEqual(
            [(uart, Qualifier.UNQUALIFIED), (uart, Qualifier.UART_PANIC_READY)],
            [(node.function, node.qualifier) for node in proof.witness if node.function == uart],
        )
        with self.assertRaisesRegex(StackEvidenceError, "recursive"):
            certify_context_bounds(frames, edges, roots, replace(binding, establishing=()))
        with self.assertRaises(StackEvidenceError):
            certify_context_bounds(frames, edges, roots, replace(binding, preservers=(uart,)))

    def test_both_panic_entries_retain_one_shared_48_byte_context(self):
        from tools.stack_bound_panic import panic_frame_evidence
        from tools.stack_bound_objects import function_symbols, instructions, load_object, section_bytes
        from tools.user_elf_instructions import _instruction_destination
        from tools.validation_context import target_compiler

        with tempfile.TemporaryDirectory(prefix="micros-stack-panic-entries-") as directory:
            output = Path(directory) / "panic.obj"
            command = (
                str(target_compiler(dict(os.environ))), "--target=riscv64-unknown-elf",
                "-march=rv64imac_zicsr_zifencei", "-mabi=lp64", "-mcmodel=medany",
                "-I" + str(ROOT), "-c", str(ROOT / "arch/riscv64/panic.S"), "-o", str(output),
            )
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(0, result.returncode, result.stderr)
            obj = load_object(output)
            frames, edges = panic_frame_evidence(ROOT, obj, "fixture", "kernel.elf")
            self.assertEqual(
                {"micros_panic_entry": 0, "micros_trap_panic_entry": 48},
                {frame.function.symbol: frame.bytes_used for frame in frames},
            )
            self.assertEqual("micros_trap_panic_entry", edges[0].targets[0].symbol)
            self.assertEqual("micros_panic_report", edges[1].targets[0].symbol)
            shared = function_symbols(obj)["micros_trap_panic_entry"]
            section = obj.sections[shared.section_index]
            data = section_bytes(obj, section)[shared.value:shared.value + shared.size]
            address, width = next(
                (address, width) for address, width, word in instructions(data, shared.value)
                if _instruction_destination(word, width) == 2
            )
            changed = bytearray(obj.data)
            changed[section.offset + address:section.offset + address + width] = (
                b"\x01\x00" if width == 2 else b"\x13\x00\x00\x00"
            )
            output.write_bytes(changed)
            with self.assertRaisesRegex(StackEvidenceError, "panic|context|frame"):
                panic_frame_evidence(ROOT, load_object(output), "fixture", "kernel.elf")


if __name__ == "__main__":
    unittest.main()
