"""Finite grant target-test table admission against real RV64 artifacts."""

from dataclasses import replace
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import unittest

from tools.stack_bound_artifacts import load_c_object
from tools.stack_bound_bindings import reference_source
from tools.stack_bound_catalog import DefinedFunction, resolve_linked_calls
from tools.stack_bound_collect import LinkedCatalog
from tools.stack_bound_flow import function_flow
from tools.stack_bound_link import FunctionLocation, join_function_locations
from tools.stack_bound_objects import (
    RELOCATION, function_symbols, load_object, relocations,
)
from tools.stack_bound_pointer import memory_action
from tools.stack_bound_types import (
    CallSite, EdgeKind, FunctionKey, SourceOrigin, StackEvidenceError,
)
from tools.user_elf_parse import load_elf


ROOT = Path(__file__).resolve().parents[2]
UNIT = "kernel/tests/grant/grant_syscall_test_commands.c"
COPY_UNIT = "kernel/tests/grant/grant_syscall_test_copy_commands.c"
HEADER = "kernel/tests/grant/grant_syscall_test_internal.h"
CALLER = FunctionKey(
    "riscv64-grant-syscall-test", "kernel/micros.elf", UNIT,
    "grant_syscall_test_arm_command",
)


def run_fixture_command(command, directory):
    result = subprocess.run(
        command, cwd=directory, capture_output=True, text=True, timeout=60,
        env={**os.environ, "CLANG_NO_DEFAULT_CONFIG": "1"},
    )
    if result.returncode:
        raise AssertionError(f"{shlex.join(command)}\n{result.stdout}{result.stderr}")


def build_fixture(source_root, directory, observed=False):
    directory.mkdir()
    entries, objects, evidence = [], {}, []
    for unit in (UNIT, COPY_UNIT):
        relative = "CMakeFiles/micros.dir/" + unit + ".obj"
        output = directory / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        source = source_root / unit
        command = [
            os.environ["MICROS_CLANG"], "--target=riscv64-unknown-elf",
            "-DMICROS_BUILD_GRANT_SYSCALL_TEST=1",
            "-I" + str(source_root), "-I" + str(ROOT), "-I" + str(ROOT / "include"),
            "-std=c17", "-march=rv64imac_zicsr_zifencei", "-mabi=lp64",
            "-mcmodel=medany", "-msmall-data-limit=0", "-mno-save-restore",
            "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie",
            "-ffunction-sections", "-fdata-sections", "-g0",
            "-fstack-usage", "-fstack-size-section", "-Wall", "-Wextra", "-Werror",
        ]
        if observed:
            command.append("-finstrument-functions-after-inlining")
        command.extend(("-c", str(source), "-o", str(output)))
        run_fixture_command(command, directory)
        entries.append({
            "directory": str(directory), "file": str(source), "output": str(output),
            "command": shlex.join(command),
        })
        objects[unit] = load_object(output)
        evidence.append(load_c_object(
            output, source, variant=CALLER.variant, image=CALLER.image,
            translation_unit=unit, compilation_directory=directory,
        ))
    (directory / "compile_commands.json").write_text(json.dumps(entries))
    (directory / "CMakeCache.txt").write_text("MICROS_BUILD_GRANT_SYSCALL_TEST:BOOL=ON\n")
    inputs = ["CMakeFiles/micros.dir/" + unit + ".obj" for unit in (UNIT, COPY_UNIT)]
    if observed:
        hooks = directory / "hooks.S"
        hooks.write_text(
            ".text\n.globl __cyg_profile_func_enter\n"
            ".type __cyg_profile_func_enter,@function\n"
            "__cyg_profile_func_enter: ret\n.size __cyg_profile_func_enter,.-__cyg_profile_func_enter\n"
            ".globl __cyg_profile_func_exit\n.type __cyg_profile_func_exit,@function\n"
            "__cyg_profile_func_exit: ret\n.size __cyg_profile_func_exit,.-__cyg_profile_func_exit\n"
        )
        run_fixture_command([
            os.environ["MICROS_CLANG"], "--target=riscv64-unknown-elf",
            "-march=rv64imac_zicsr_zifencei", "-mabi=lp64", "-c", "hooks.S", "-o", "hooks.obj",
        ], directory)
        inputs.append("hooks.obj")
    (directory / "kernel").mkdir()
    run_fixture_command([
        os.environ["MICROS_LD_LLD"], "-m", "elf64lriscv", "--no-relax",
        "--unresolved-symbols=ignore-all", "--gc-sections",
        "--entry=" + CALLER.symbol, "-Ttext=0x40000000",
        "--Map=kernel/micros.map", "-o", CALLER.image, *inputs,
    ], directory)
    image = load_elf(directory / CALLER.image)
    map_text = (directory / "kernel/micros.map").read_text()
    locations = []
    for record in evidence:
        for frame in record.frames:
            symbol = function_symbols(objects[frame.function.translation_unit])[frame.function.symbol]
            locations.append(FunctionLocation(
                frame.function, "CMakeFiles/micros.dir/" + frame.function.translation_unit + ".obj",
                objects[frame.function.translation_unit].sections[symbol.section_index].name,
                symbol.value, symbol.size,
            ))
    addresses = join_function_locations(tuple(locations), map_text, image.symbols)
    frames = tuple(frame for record in evidence for frame in record.frames if frame.function in addresses)
    definitions = tuple(DefinedFunction(
        key, address,
        function_symbols(objects[key.translation_unit])[key.symbol].size,
        function_symbols(objects[key.translation_unit])[key.symbol].binding != 0,
    ) for key, address in addresses.items())
    calls, pending = resolve_linked_calls(
        image,
        tuple(call for record in evidence for call in record.calls if call.caller in addresses),
        definitions,
    )
    catalog = LinkedCatalog(
        frames, definitions, calls, pending,
        tuple((frame.function, "CMakeFiles/micros.dir/" + frame.function.translation_unit + ".obj")
              for frame in frames),
        tuple(SourceOrigin(origin.function, str(Path(origin.source).relative_to(source_root)), origin.line)
              for record in evidence for origin in record.origins if origin.function in addresses),
    )
    return objects[UNIT], catalog


def instruction_change(obj, symbol, offset, word):
    flow = function_flow(obj, symbol)
    width, _ = flow.instructions[offset]
    data = bytearray(obj.data)
    position = obj.sections[symbol.section_index].offset + symbol.value + offset
    data[position:position + width] = word.to_bytes(width, "little")
    return replace(obj, data=bytes(data))


def relocation_change(obj, section_index, address, *, kind=None, target=None, addend=None):
    data = bytearray(obj.data)
    found = False
    for section in obj.sections:
        if section.section_type != 4 or section.info != section_index:
            continue
        for position in range(section.offset, section.offset + section.size, RELOCATION.size):
            actual, info, extra = RELOCATION.unpack_from(data, position)
            if actual != address or info & 0xffffffff == 51:
                continue
            if found:
                raise AssertionError("fixture relocation is ambiguous")
            found = True
            info = ((info >> 32 if target is None else target) << 32) | (
                info & 0xffffffff if kind is None else kind
            )
            RELOCATION.pack_into(data, position, actual, info, extra if addend is None else addend)
    if not found:
        raise AssertionError("fixture relocation is missing")
    return replace(obj, data=bytes(data))


class StackTestTablesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        scratch = Path("build") / ("ev03-test-table-fixture-" + str(os.getpid()))
        scratch.mkdir()
        cls.addClassCleanup(shutil.rmtree, scratch)
        cls.source_root = (scratch / "source").absolute()
        for unit in (UNIT, COPY_UNIT, HEADER):
            path = cls.source_root / unit
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(reference_source(str(ROOT), unit))
        cls.directory = (scratch / "metadata").absolute()
        cls.obj, cls.catalog = build_fixture(cls.source_root, cls.directory)
        cls.observed_directory = (scratch / "observed").absolute()
        cls.observed_obj, cls.observed_catalog = build_fixture(
            cls.source_root, cls.observed_directory, True,
        )
        cls.symbol = function_symbols(cls.obj)[CALLER.symbol]
        cls.flow = function_flow(cls.obj, cls.symbol)
        cls.table, = (symbol for symbol in cls.obj.symbols if symbol.name == "command_bindings")

    def bind(self, catalog=None, directory=None):
        from tools.stack_bound_test_tables import bind_current_test_tables
        return bind_current_test_tables(
            self.source_root, self.directory if directory is None else directory,
            self.catalog if catalog is None else catalog,
        )

    def artifact(self, obj=None, symbol=None):
        from tools.stack_bound_test_tables import validate_grant_command_table
        return validate_grant_command_table(
            ROOT, self.obj if obj is None else obj, self.symbol if symbol is None else symbol,
        )

    def test_current_grant_table_binds_every_local_and_imported_frame(self):
        unbound, = (call for call in self.catalog.pending if call.caller == CALLER and call.target_symbol is None)
        self.assertEqual((0x70, EdgeKind.CALL), (unbound.offset, unbound.kind))
        bound, pending = self.bind()
        site, = bound
        self.assertEqual((CALLER, unbound.offset, EdgeKind.CALL), (site.caller, site.offset, site.kind))
        targets = {
            frame.function for frame in self.catalog.frames
            if frame.function.symbol.startswith("grant_syscall_test_arm_")
            and frame.function.symbol != CALLER.symbol
        }
        self.assertEqual(targets, set(site.targets))
        self.assertEqual(38, len(site.targets))
        self.assertEqual({UNIT, COPY_UNIT}, {key.translation_unit for key in site.targets})
        self.assertEqual(tuple(call for call in self.catalog.pending if call != unbound), pending)
        self.assertTrue(any(call.target_symbol == "grant_syscall_test_arm_raw" for call in pending))

    def test_indirect_dispatcher_preserves_unrelated_unresolved_boundaries(self):
        from tools.stack_bound_bindings import bind_current_indirect_calls

        from tools.stack_bound_types import SymbolCall
        foreign = SymbolCall(CALLER, 0x1234, "unbound_assembly_entry", EdgeKind.TAIL)
        catalog = replace(self.catalog, pending=self.catalog.pending + (foreign,))
        sites, pending = bind_current_indirect_calls(
            self.source_root, self.directory, catalog,
        )
        expected, remaining = self.bind()
        self.assertEqual(expected, sites)
        self.assertEqual(remaining + (foreign,), pending)

    def test_observed_object_binds_only_the_table_not_compiler_hooks(self):
        site, = self.bind(self.observed_catalog, self.observed_directory)[0]
        self.assertEqual(38, len(site.targets))
        remaining = self.bind(self.observed_catalog, self.observed_directory)[1]
        self.assertTrue(any(call.target_symbol == "__cyg_profile_func_enter" for call in remaining))
        self.assertFalse(any(call.caller == CALLER and call.target_symbol is None for call in remaining))

    def test_exact_readonly_slot_geometry_is_required(self):
        section = self.obj.sections[self.table.section_index]
        cases = (
            replace(section, flags=3),
            replace(section, size=section.size - 16),
            replace(section, size=section.size + 16),
            replace(section, alignment=4),
            replace(section, name=".rodata.foreign_table"),
        )
        for candidate in cases:
            sections = tuple(candidate if item.index == section.index else item for item in self.obj.sections)
            with self.subTest(candidate=candidate), self.assertRaises(StackEvidenceError):
                self.artifact(replace(self.obj, sections=sections))
        for size in (self.table.size - 16, self.table.size + 16):
            symbols = tuple(replace(item, size=size) if item == self.table else item for item in self.obj.symbols)
            with self.subTest(size=size), self.assertRaises(StackEvidenceError):
                self.artifact(replace(self.obj, symbols=symbols))

    def test_missing_extra_shifted_and_foreign_target_relocations_fail(self):
        entries = relocations(self.obj, self.table.section_index)
        first, second = entries[:2]
        wrong_target = self.obj.symbols.index(second[2])
        changes = (
            dict(kind=0), dict(kind=1), dict(target=wrong_target), dict(addend=2),
        )
        for change in changes:
            with self.subTest(change=change), self.assertRaises(StackEvidenceError):
                self.artifact(relocation_change(
                    self.obj, self.table.section_index, first[0], **change,
                ))
        data = bytearray(self.obj.data)
        section, = (item for item in self.obj.sections if item.section_type == 4
                    and item.info == self.table.section_index)
        address, info, addend = RELOCATION.unpack_from(data, section.offset)
        RELOCATION.pack_into(data, section.offset, address - 8, info, addend)
        with self.assertRaises(StackEvidenceError):
            self.artifact(replace(self.obj, data=bytes(data)))

    def test_command_bytes_padding_and_nonzero_pointer_bytes_fail(self):
        section = self.obj.sections[self.table.section_index]
        for displacement in (0, 4, 8, self.table.size - 1):
            data = bytearray(self.obj.data)
            data[section.offset + self.table.value + displacement] ^= 1
            with self.subTest(displacement=displacement), self.assertRaises(StackEvidenceError):
                self.artifact(replace(self.obj, data=bytes(data)))

    def test_wrong_stride_member_load_and_actual_jalr_register_fail(self):
        arm_call, = (call for call in self.flow.calls if call.target_symbol is None)
        scale = [
            address for address, (width, word) in self.flow.instructions.items()
            if width == 4 and word & 0xfc00707f == 0x1013 and word >> 20 & 63 == 4
        ]
        lookup, = (
            address for address, (width, word) in self.flow.instructions.items()
            if memory_action(width, word) == (False, 8, 12, 10, 8)
        )
        cases = [
            (address, (self.flow.instructions[address][1] & ~(63 << 20)) | (3 << 20))
            for address in scale
        ] + [(lookup, 0x6108), (arm_call.offset, 0x9682), (arm_call.offset, 0x8602)]
        for address, word in cases:
            with self.subTest(address=address, word=word), self.assertRaises(StackEvidenceError):
                self.artifact(instruction_change(self.obj, self.symbol, address, word))

    def test_loaded_member_cannot_be_clobbered_or_increment_replaced_by_a_constant(self):
        loaded_into_argument = instruction_change(self.obj, self.symbol, 0x6a, 0x6508)
        clobbered = instruction_change(loaded_into_argument, self.symbol, 0x70, 0x9502)
        constant_increment = instruction_change(self.obj, self.symbol, 0x7c, 0x4505)
        for obj in (clobbered, constant_increment):
            with self.subTest(clobbered=obj is clobbered), self.assertRaises(StackEvidenceError):
                self.artifact(obj)

    def test_guard_extent_initial_index_and_increment_are_checked(self):
        for offset, word in ((0x36, 0x02e00513), (0x28, 0x4505), (0x7c, 0x0509)):
            with self.subTest(offset=offset), self.assertRaises(StackEvidenceError):
                self.artifact(instruction_change(self.obj, self.symbol, offset, word))
        guard_word = self.flow.instructions[0x3a][1]
        with self.assertRaises(StackEvidenceError):
            self.artifact(instruction_change(
                self.obj, self.symbol, 0x3a, (guard_word & ~(7 << 12)) | (4 << 12),
            ))

    def test_incoming_edges_cannot_bypass_guard_comparison_or_pointer_load(self):
        for branch, destination in ((0x32, 0x5a), (0x3e, 0x5a), (0x58, 0x6c)):
            reference, = (
                item for item in relocations(self.obj, self.symbol.section_index)
                if item[0] == self.symbol.value + branch and item[1] != 51
            )
            with self.subTest(branch=branch), self.assertRaises(StackEvidenceError):
                self.artifact(relocation_change(
                    self.obj, self.symbol.section_index, self.symbol.value + branch,
                    addend=self.symbol.value + destination - reference[2].value,
                ))

    def test_foreign_table_operand_or_mid_function_entry_fails(self):
        references = [
            item for item in relocations(self.obj, self.symbol.section_index)
            if item[1] == 23 and item[2] == self.table
        ]
        foreign = self.obj.symbols.index(self.symbol)
        for reference in references:
            with self.subTest(offset=reference[0]), self.assertRaises(StackEvidenceError):
                self.artifact(relocation_change(
                    self.obj, self.symbol.section_index, reference[0], target=foreign,
                ))
        with self.assertRaises(StackEvidenceError):
            self.artifact(symbol=replace(self.symbol, value=2, size=self.symbol.size - 2))

    def test_table_mutation_and_extra_or_missing_machine_occurrences_fail(self):
        for offset, word in ((0x6c, 0xe110), (0x28, 0x9702), (0x70, 0x0001)):
            with self.subTest(offset=offset), self.assertRaises(StackEvidenceError):
                self.artifact(instruction_change(self.obj, self.symbol, offset, word))

    def test_source_provider_type_table_and_whole_caller_anchors_are_fixed(self):
        for unit, old, new in (
            (UNIT, "static const struct grant_syscall_command_binding command_bindings",
             "static struct grant_syscall_command_binding command_bindings"),
            (UNIT, "return command_bindings[index].arm(fixture, &input);",
             "return command_bindings[0].arm(fixture, &input);"),
            (COPY_UNIT, "return true;", "return false;"),
            (HEADER, "    GRANT_COMMAND_NONE = 0,", "    GRANT_COMMAND_NONE = 1,"),
        ):
            path = self.source_root / unit
            original = path.read_text()
            self.assertIn(old, original)
            try:
                path.write_text(original.replace(old, new, 1))
                with self.subTest(unit=unit, mutation=new), self.assertRaises(StackEvidenceError):
                    self.bind()
            finally:
                path.write_text(original)

    def test_target_frame_object_origin_and_occurrence_inventory_cannot_be_omitted(self):
        target = next(frame.function for frame in self.catalog.frames if frame.function.translation_unit == COPY_UNIT)
        cases = (
            replace(self.catalog, frames=tuple(frame for frame in self.catalog.frames if frame.function != target)),
            replace(self.catalog, definitions=tuple(item for item in self.catalog.definitions if item.function != target)),
            replace(self.catalog, objects=tuple(item for item in self.catalog.objects if item[0] != target)),
            replace(self.catalog, origins=tuple(item for item in self.catalog.origins if item.function != target)),
            replace(self.catalog, pending=tuple(call for call in self.catalog.pending if call.caller != CALLER)),
            replace(self.catalog, pending=self.catalog.pending + tuple(
                call for call in self.catalog.pending if call.caller == CALLER
            )),
            replace(self.catalog, calls=self.catalog.calls + (CallSite(CALLER, 0x70, (target,)),)),
            replace(self.catalog, frames=tuple(frame for frame in self.catalog.frames if frame.function != target),
                    definitions=tuple(item for item in self.catalog.definitions if item.function != target),
                    objects=tuple(item for item in self.catalog.objects if item[0] != target),
                    origins=tuple(item for item in self.catalog.origins if item.function != target)),
        )
        for index, catalog in enumerate(cases):
            with self.subTest(index=index), self.assertRaises(StackEvidenceError):
                self.bind(catalog)
        caller_frame = next(frame for frame in self.catalog.frames if frame.function == CALLER)
        for frame in (replace(caller_frame, bytes_used=caller_frame.bytes_used + 16),
                      replace(caller_frame, classification="dynamic")):
            with self.subTest(frame=frame), self.assertRaises(StackEvidenceError):
                self.bind(replace(self.catalog, frames=tuple(
                    frame if item == caller_frame else item for item in self.catalog.frames
                )))

    def test_variant_and_source_origins_cannot_borrow_test_table_authority(self):
        for origin in (
            SourceOrigin(CALLER, COPY_UNIT, 456),
            SourceOrigin(CALLER, UNIT, 1),
        ):
            with self.subTest(origin=origin), self.assertRaises(StackEvidenceError):
                self.bind(replace(self.catalog, origins=tuple(
                    origin if item.function == CALLER else item for item in self.catalog.origins
                )))
        def foreign(key):
            return replace(key, variant="foreign-variant")
        catalog = replace(
            self.catalog,
            frames=tuple(replace(item, function=foreign(item.function)) for item in self.catalog.frames),
            definitions=tuple(replace(item, function=foreign(item.function)) for item in self.catalog.definitions),
            objects=tuple((foreign(key), path) for key, path in self.catalog.objects),
            origins=tuple(replace(item, function=foreign(item.function)) for item in self.catalog.origins),
            pending=tuple(replace(item, caller=foreign(item.caller)) for item in self.catalog.pending),
        )
        with self.assertRaises(StackEvidenceError):
            self.bind(catalog)

    def test_compiled_test_flag_is_required_and_never_production_authority(self):
        cache = self.directory / "CMakeCache.txt"
        original = cache.read_text()
        try:
            cache.write_text("MICROS_BUILD_GRANT_SYSCALL_TEST:BOOL=OFF\n")
            with self.assertRaises(StackEvidenceError):
                self.bind()
            self.assertEqual(((), ()), self.bind(LinkedCatalog((), (), (), ())))
        finally:
            cache.write_text(original)
        commands = self.directory / "compile_commands.json"
        original = commands.read_text()
        try:
            commands.write_text(original.replace("MICROS_BUILD_GRANT_SYSCALL_TEST=1",
                                                  "MICROS_BUILD_GRANT_SYSCALL_TEST=0"))
            with self.assertRaises(StackEvidenceError):
                self.bind()
        finally:
            commands.write_text(original)

    def test_compile_output_is_bound_relative_to_the_exact_build_directory(self):
        commands = self.directory / "compile_commands.json"
        original = commands.read_text()
        entries = json.loads(original)
        try:
            for entry in entries:
                argv = shlex.split(entry["command"])
                index = argv.index("-o") + 1
                argv[index] = str(Path(argv[index]).relative_to(self.directory))
                entry["command"] = shlex.join(argv)
            commands.write_text(json.dumps(entries))
            self.assertEqual(1, len(self.bind()[0]))
            for entry in entries:
                entry["command"] = entry["command"].replace("-o CMakeFiles/", "-o ../metadata/CMakeFiles/")
            commands.write_text(json.dumps(entries))
            with self.assertRaises(StackEvidenceError):
                self.bind()
        finally:
            commands.write_text(original)

    def test_dual_compile_representations_cannot_hide_flag_drift(self):
        path = self.directory / "compile_commands.json"
        original = path.read_text()
        entries = json.loads(original)
        entries[0]["arguments"] = shlex.split(entries[0]["command"])
        entries[0]["command"] = entries[0]["command"].replace(
            "MICROS_BUILD_GRANT_SYSCALL_TEST=1", "MICROS_BUILD_GRANT_SYSCALL_TEST=0",
        )
        try:
            path.write_text(json.dumps(entries))
            with self.assertRaises(StackEvidenceError):
                self.bind()
        finally:
            path.write_text(original)

    def test_linked_table_pointer_guard_and_call_bytes_must_match_objects(self):
        image_path = self.directory / CALLER.image
        original = image_path.read_bytes()
        image = load_elf(image_path)
        caller, = (item for item in self.catalog.definitions if item.function == CALLER)
        table, = (item for item in image.symbols if item.name == self.table.name)
        locations = (table.value + 8, caller.address + 0x3a, caller.address + 0x70)
        try:
            for address in locations:
                program, = (item for item in image.programs if item.program_type == 1
                            and item.virtual_address <= address < item.virtual_address + item.file_size)
                position = program.offset + address - program.virtual_address
                data = bytearray(original)
                data[position] ^= 0x10
                image_path.write_bytes(data)
                with self.subTest(address=address), self.assertRaises(StackEvidenceError):
                    self.bind()
        finally:
            image_path.write_bytes(original)

    def test_combined_readonly_output_labels_confer_no_write_or_execute_permission(self):
        path = self.directory / CALLER.image
        original = path.read_bytes()
        image = load_elf(path)
        table, = (item for item in image.symbols if item.name == "command_bindings")
        position = image.header.section_offset + table.section_index * image.header.section_entry_size + 8
        try:
            data = bytearray(original)
            struct.pack_into("<Q", data, position, 0x32)
            path.write_bytes(data)
            self.assertEqual(1, len(self.bind()[0]))
            for flags in (0x33, 0x36, 0x132):
                struct.pack_into("<Q", data, position, flags)
                path.write_bytes(data)
                with self.subTest(flags=flags), self.assertRaises(StackEvidenceError):
                    self.bind()
        finally:
            path.write_bytes(original)

    def test_link_map_and_local_target_identity_cannot_be_borrowed(self):
        path = self.directory / "kernel/micros.map"
        original = path.read_text()
        try:
            path.write_text(original.replace(":(.rodata.command_bindings)", ":(.rodata.foreign_table)"))
            with self.assertRaises(StackEvidenceError):
                self.bind()
        finally:
            path.write_text(original)
        target = next(item for item in self.catalog.definitions if item.function.translation_unit == UNIT
                      and item.function.symbol != CALLER.symbol)
        for definition in (
            replace(target, address=target.address + 2),
            replace(target, global_linkage=True),
        ):
            catalog = replace(self.catalog, definitions=tuple(
                definition if item == target else item for item in self.catalog.definitions
            ))
            with self.subTest(definition=definition), self.assertRaises(StackEvidenceError):
                self.bind(catalog)


if __name__ == "__main__":
    unittest.main()
