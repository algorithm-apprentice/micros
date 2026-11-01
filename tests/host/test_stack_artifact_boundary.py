"""Metadata-only artifact transport must preserve complete seed inputs."""

from pathlib import Path
import copy
import fcntl
import os
import tempfile
import unittest
from unittest.mock import patch

from tests.host.application_elf_test_fixture import build_application_elf
from tools.generate_elf_spawn_seed import build_elf_spawn_seed
from tools.stack_bound_types import StackEvidenceError
from tools.user_elf_parse import parse_elf
from tools.validation_inputs import _file_identity


ROOT = Path(__file__).resolve().parents[2]


class StackArtifactBoundaryTest(unittest.TestCase):
    def test_output_inventory_rejects_missing_copies_and_unmodeled_extra_images(self):
        from tools.stack_bound_scope import expected_images, require_image_outputs

        configure = "riscv64-application-images"
        with tempfile.TemporaryDirectory(prefix="micros-stack-output-inventory-") as temporary:
            directory = Path(temporary)
            outputs = expected_images(configure)
            for name in outputs:
                path = directory / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"independent output inventory fixture")
            require_image_outputs(directory, configure)
            copy = directory / "applications/micros-application-runtime-probe.runtime.elf"
            data = copy.read_bytes()
            copy.unlink()
            with self.assertRaises(StackEvidenceError):
                require_image_outputs(directory, configure)
            copy.write_bytes(data)
            (directory / "foreign.elf").write_bytes(data)
            with self.assertRaises(StackEvidenceError):
                require_image_outputs(directory, configure)

    def test_observed_recipe_is_explicit_and_does_not_canonicalize_observer_code(self):
        from tools.stack_bound_build import OBSERVED_RECIPE, validate_observed_recipe

        validate_observed_recipe(ROOT)
        self.assertIn("-finstrument-functions-after-inlining", OBSERVED_RECIPE)
        self.assertNotIn("canonicalize_stack_metadata_elf", OBSERVED_RECIPE)
        self.assertNotIn("MICROS_STACK_PAIR_RECORD", OBSERVED_RECIPE)

    def test_unimplemented_observation_scope_fails_before_any_build_or_qemu(self):
        from tools.stack_bound_build import build_observed_profile, static_profiles

        profile = next(row for row in static_profiles(ROOT) if row.workflow == "test-qemu-panic")
        with (
            patch("tools.stack_bound_build.subprocess.run") as launched,
            self.assertRaises(StackEvidenceError),
        ):
            build_observed_profile(ROOT, profile, os.environ["MICROS_PYTHON"])
        launched.assert_not_called()

    def test_static_image_build_completes_nondefault_copy_outputs_without_starting_qemu(self):
        from tools.stack_bound_build import Profile, build_profile
        from tools.validation_context import Candidate

        repository = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory(prefix="micros-stack-static-outputs-") as directory:
            root = Path(directory)
            (root / "cmake/toolchains").mkdir(parents=True)
            (root / "tests/stack").mkdir(parents=True)
            (root / "cmake/toolchains/riscv64-clang.cmake").write_text(
                (repository / "cmake/toolchains/riscv64-clang.cmake").read_text(),
            )
            (root / "tests/stack/StackMetadata.cmake").write_text(
                (repository / "tests/stack/StackMetadata.cmake").read_text(),
            )
            (root / "fixture.c").write_text("int probe(int x);\nint probe(int x) { return x + 1; }\n")
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.25)\nproject(stack_outputs C)\n"
                "add_executable(micros_application_runtime_probe fixture.c)\n"
                "set_target_properties(micros_application_runtime_probe PROPERTIES\n"
                '    OUTPUT_NAME "micros-application-runtime-probe" SUFFIX ".elf"\n'
                '    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/applications")\n'
                "target_compile_options(micros_application_runtime_probe PRIVATE\n"
                "    -march=rv64imac_zicsr_zifencei -mabi=lp64 -ffreestanding -fno-builtin -g0)\n"
                "target_link_options(micros_application_runtime_probe PRIVATE\n"
                '    -nostdlib -static "--ld-path=${MICROS_LD_LLD}" "-Wl,-e,probe")\n'
                'set(copy "${CMAKE_BINARY_DIR}/applications/micros-application-runtime-probe.runtime.elf")\n'
                "add_custom_command(OUTPUT \"${copy}\"\n"
                '    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:micros_application_runtime_probe>" "${copy}"\n'
                "    DEPENDS micros_application_runtime_probe)\n"
                'add_custom_target(build-application-images DEPENDS "${copy}")\n'
                'add_custom_target(test-qemu-fixture COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/qemu-launched")\n'
            )
            candidate = Candidate({"root": str(root)}, {
                "head": "a" * 40, "tree": "b" * 40,
                "source_sha256": "c" * 64, "missing_paths": [],
            }, True)
            image = Profile(
                "riscv64-application-images", "build-application-images",
                "build-application-images", (("CMAKE_BUILD_TYPE", "Debug"),),
            )
            qemu = Profile("riscv64-fixture-test", "test-qemu-fixture", "test-qemu-fixture", image.variables)
            with patch("tools.stack_bound_build.static_profiles", return_value=(image, qemu)), patch(
                "tools.validation_context.capture_candidate", return_value=candidate,
            ), patch(
                "tools.stack_artifact_build.capture_candidate", return_value=candidate,
            ):
                built = build_profile(root, image, "ordinary", os.environ["MICROS_PYTHON"])
                self.assertEqual(
                    (built / "applications/micros-application-runtime-probe.elf").read_bytes(),
                    (built / "applications/micros-application-runtime-probe.runtime.elf").read_bytes(),
                )
                original = (built / "applications/micros-application-runtime-probe.elf").read_bytes()
                source = root / "fixture.c"
                status = source.stat()
                source.write_text(source.read_text().replace("x + 1", "x + 2"))
                os.utime(source, ns=(status.st_atime_ns, status.st_mtime_ns))
                build_profile(root, image, "ordinary", os.environ["MICROS_PYTHON"])
                self.assertNotEqual(
                    original, (built / "applications/micros-application-runtime-probe.elf").read_bytes(),
                    "stale mtimes cannot certify old objects for a new source",
                )
                cache = built / "CMakeCache.txt"
                cache.write_text(cache.read_text().replace("CMAKE_C_FLAGS:STRING=\n", "CMAKE_C_FLAGS:STRING=-O3\n"))
                build_profile(root, image, "ordinary", os.environ["MICROS_PYTHON"])
                self.assertNotIn(" -O3 ", (built / "compile_commands.json").read_text())
                built_qemu = build_profile(root, qemu, "ordinary", os.environ["MICROS_PYTHON"])
                self.assertFalse((built_qemu / "qemu-launched").exists())

    def test_system_reset_firmware_identity_cannot_accept_a_same_version_replacement(self):
        from tools.stack_bound_fatal_firmware import require_pinned_firmware

        identity = {
            "kind": "file", "mode": 0o644, "size": 275928,
            "sha256": "894e2aef99590fc07ec6c60ab00282b8bc5d5d5bb2a1d0c6ada0c52df24274c0",
        }
        require_pinned_firmware(identity)
        for changed in (
            {**identity, "sha256": "0" * 64},
            {**identity, "size": identity["size"] + 1},
            {**identity, "kind": "link"},
            {**identity, "release": "1.8.1"},
        ):
            with self.subTest(changed=changed), self.assertRaisesRegex(
                StackEvidenceError, "firmware",
            ):
                require_pinned_firmware(changed)

    def test_default_firmware_resolution_checks_configuration_before_qemu_metadata(self):
        from tools.stack_bound_fatal_firmware import resolve_default_firmware
        from tools.validation_inputs import UnsupportedContext

        with tempfile.TemporaryDirectory(prefix="micros-stack-firmware-resolution-") as directory, patch(
            "tools.stack_bound_fatal_firmware._cellar_root",
        ) as installation_root:
            root = Path(directory).resolve()
            installation = root / "Cellar/qemu/11.1.3"
            installation_root.return_value = installation
            executable = installation / "bin/qemu-system-riscv64"
            data_root = installation / "share/qemu"
            executable.parent.mkdir(parents=True)
            data_root.mkdir(parents=True)
            executable.write_bytes(b"fixture executable")
            firmware = data_root / "opensbi-riscv64-generic-fw_dynamic.bin"
            firmware.write_bytes(b"fixture firmware")
            with patch(
                "tools.stack_bound_fatal_firmware.qemu_configuration",
                side_effect=UnsupportedContext("present configuration"),
            ), patch("tools.stack_bound_fatal_firmware._run") as metadata:
                with self.assertRaisesRegex(StackEvidenceError, "configuration"):
                    resolve_default_firmware(executable)
                metadata.assert_not_called()
            with patch(
                "tools.stack_bound_fatal_firmware.qemu_configuration",
                return_value={"compiled_default": "fixture", "absent": []},
            ), patch(
                "tools.stack_bound_fatal_firmware._run",
                side_effect=lambda command: (
                    b"QEMU emulator version 11.1.3\n" if command[-1] == "--version"
                    else (str(data_root) + "\n").encode()
                ),
            ), patch(
                "tools.stack_bound_fatal_firmware.require_pinned_firmware",
            ) as pinned:
                resolved = resolve_default_firmware(executable)
                self.assertEqual(str(firmware), resolved["firmware"]["path"])
                pinned.assert_called_once_with(_file_identity(firmware))
            with patch(
                "tools.stack_bound_fatal_firmware.qemu_configuration",
                return_value={"compiled_default": "fixture", "absent": []},
            ), patch(
                "tools.stack_bound_fatal_firmware._run",
                side_effect=lambda command: (
                    b"QEMU emulator version 11.1.3\n" if command[-1] == "--version"
                    else (str(root) + "\n").encode()
                ),
            ):
                with self.assertRaisesRegex(StackEvidenceError, "root"):
                    resolve_default_firmware(executable)

    def test_runtime_copy_can_borrow_only_its_exact_equal_producer_map(self):
        from tools.stack_bound_collect import image_map_text

        original = bytes(build_application_elf())
        map_text = "             VMA              LMA     Size Align Out     In      Symbol\n"
        with tempfile.TemporaryDirectory(prefix="micros-stack-runtime-map-") as directory:
            root = Path(directory)
            (root / "applications").mkdir()
            (root / "CMakeCache.txt").write_text("MICROS_BUILD_APPLICATION_IMAGES:BOOL=ON\n")
            raw = root / "applications/micros-application-runtime-probe.elf"
            runtime = root / "applications/micros-application-runtime-probe.runtime.elf"
            raw.write_bytes(original)
            runtime.write_bytes(original)
            raw.with_suffix(".map").write_text(map_text)
            relative = str(runtime.relative_to(root))
            self.assertEqual(map_text, image_map_text(root, relative))
            runtime.write_bytes(original + b"changed representation")
            with self.assertRaisesRegex(StackEvidenceError, "copy|producer"):
                image_map_text(root, relative)
            runtime.write_bytes(original)
            runtime.with_suffix(".map").write_text(map_text)
            with self.assertRaisesRegex(StackEvidenceError, "map"):
                image_map_text(root, relative)
            runtime.with_suffix(".map").unlink()
            (root / "CMakeCache.txt").write_text("MICROS_BUILD_APPLICATION_IMAGES:BOOL=OFF\n")
            with self.assertRaisesRegex(StackEvidenceError, "configuration"):
                image_map_text(root, relative)
            foreign = root / "applications/foreign.runtime.elf"
            foreign.write_bytes(original)
            foreign.with_suffix(".map").write_text(map_text)
            with self.assertRaisesRegex(StackEvidenceError, "alias"):
                image_map_text(root, str(foreign.relative_to(root)))

    def test_leaf_publications_serialize_source_queries_without_timing_retries(self):
        from tools.canonicalize_stack_metadata_elf import artifact_publication_lock

        with tempfile.TemporaryDirectory(prefix="micros-stack-lock-") as directory:
            root = Path(directory)
            (root / "build/stack-evidence").mkdir(parents=True)
            lock = root / "build/stack-evidence/canonicalization.lock"
            with artifact_publication_lock(root):
                descriptor = os.open(lock, os.O_RDWR)
                try:
                    with self.assertRaises(BlockingIOError):
                        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                finally:
                    os.close(descriptor)
            descriptor = os.open(lock, os.O_RDWR)
            try:
                fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                fcntl.flock(descriptor, fcntl.LOCK_UN)
            finally:
                os.close(descriptor)

    def test_recipe_projection_removes_only_metadata_flags_and_build_spelling(self):
        from tools.stack_artifact_build import normalize_compile_argv

        ordinary = Path("/repo/build/ordinary")
        metadata = Path("/repo/build/metadata")
        base = (
            "/llvm/clang", "--target=riscv64-unknown-elf", "-g0",
            "-mcmodel=medany", "-o", str(ordinary / "probe.obj"), "-c", "/repo/probe.c",
        )
        observed = (
            "/llvm/clang", "--target=riscv64-unknown-elf", "-g0",
            "-fstack-usage", "-fstack-size-section", "-mcmodel=medany",
            "-o", str(metadata / "probe.obj"), "-c", "/repo/probe.c",
        )
        expected = normalize_compile_argv(base, ordinary, False)
        self.assertEqual(expected, normalize_compile_argv(observed, metadata, True))
        self.assertNotEqual(expected, normalize_compile_argv((*observed, "-O2"), metadata, True))
        for arguments in (
            observed[:-1], tuple(argument for argument in observed if argument != "-fstack-size-section"),
        ):
            with self.subTest(arguments=arguments), self.assertRaises(StackEvidenceError):
                normalize_compile_argv(arguments, metadata, True)

    def test_pair_authority_rejects_source_tool_mode_leaf_and_path_drift(self):
        from tools.stack_artifact_authority import validate_pair_entry
        from tools.validation_context import Candidate

        root = Path("/fixture/repository")
        source = {"head": "a" * 40, "tree": "b" * 40, "source_sha256": "c" * 64, "missing_paths": []}
        candidate = Candidate({"root": str(root)}, source, True)
        tools = {"compiler": {"sha256": "d" * 64}, "linker": {"sha256": "e" * 64}}
        recipes = {"micros_elf_spawn_init": "f" * 64}
        record = {
            "version": 1, "mode": "metadata", "candidate": source,
            "profile": "riscv64-elf-spawn-test", "tools": tools,
            "ordinary_root": "build/stack-evidence/riscv64-elf-spawn-test/ordinary",
            "metadata_root": "build/stack-evidence/riscv64-elf-spawn-test/metadata",
            "entries": [{
                "leaf": "micros_elf_spawn_init",
                "path": "applications/micros-elf-spawn-init.elf",
                "ordinary_identity": {
                    "kind": "file", "mode": 0o755, "size": 1024, "sha256": "1" * 64,
                },
                "recipe": "f" * 64,
            }],
        }
        ordinary, metadata, identity = validate_pair_entry(
            root, record, candidate, tools, recipes, "micros_elf_spawn_init",
        )
        self.assertTrue(ordinary.is_relative_to(root / record["ordinary_root"]))
        self.assertTrue(metadata.is_relative_to(root / record["metadata_root"]))
        self.assertEqual(record["entries"][0]["ordinary_identity"], identity)
        for mutation in ("source", "tool", "mode", "path", "recipe", "duplicate", "missing"):
            with self.subTest(mutation=mutation):
                changed = copy.deepcopy(record)
                if mutation == "source":
                    changed["candidate"]["tree"] = "0" * 40
                elif mutation == "tool":
                    changed["tools"]["compiler"]["sha256"] = "0" * 64
                elif mutation == "mode":
                    changed["mode"] = "observed"
                elif mutation == "path":
                    changed["entries"][0]["path"] = "../../ordinary.elf"
                elif mutation == "recipe":
                    changed["entries"][0]["recipe"] = "0" * 64
                elif mutation == "duplicate":
                    changed["entries"].append(changed["entries"][0])
                else:
                    changed["entries"] = []
                with self.assertRaises(StackEvidenceError):
                    validate_pair_entry(root, changed, candidate, tools, recipes, "micros_elf_spawn_init")
        with self.assertRaises(StackEvidenceError):
            validate_pair_entry(root, record, candidate, tools, recipes, "foreign_leaf")

    def test_complete_seed_bytes_are_restored_only_after_passivity_validation(self):
        from tools.stack_artifact_files import canonicalize_passive_file

        normal = bytes(build_application_elf())
        metadata = normal + b"nonallocatable compiler metadata"
        expected = build_elf_spawn_seed(parse_elf(normal), parse_elf(normal))
        self.assertNotEqual(expected, build_elf_spawn_seed(parse_elf(metadata), parse_elf(metadata)))
        with tempfile.TemporaryDirectory(prefix="micros-stack-pair-") as directory:
            root = Path(directory)
            ordinary, measured = root / "ordinary.elf", root / "metadata.elf"
            ordinary.write_bytes(normal)
            measured.write_bytes(metadata)
            identity = _file_identity(ordinary)
            canonicalize_passive_file(ordinary, measured, identity)
            self.assertEqual(normal, measured.read_bytes())
            self.assertEqual(identity, _file_identity(ordinary))
            self.assertEqual(
                expected, build_elf_spawn_seed(parse_elf(measured.read_bytes()), parse_elf(measured.read_bytes())),
            )

    def test_stale_or_nonpassive_input_fails_without_modifying_either_file(self):
        from tools.stack_artifact_files import canonicalize_passive_file

        normal = bytes(build_application_elf())
        for failure in ("stale", "loaded-byte", "missing", "linked"):
            with self.subTest(failure=failure):
                with tempfile.TemporaryDirectory(prefix="micros-stack-denied-") as directory:
                    root = Path(directory)
                    ordinary, measured = root / "ordinary.elf", root / "metadata.elf"
                    ordinary.write_bytes(normal)
                    measured.write_bytes(normal + b"metadata")
                    identity = _file_identity(ordinary)
                    if failure == "stale":
                        identity = {**identity, "sha256": "0" * 64}
                    elif failure == "loaded-byte":
                        changed = bytearray(measured.read_bytes())
                        changed[0x1000] ^= 1
                        measured.write_bytes(changed)
                    elif failure == "missing":
                        ordinary.unlink()
                    else:
                        ordinary.unlink()
                        ordinary.symlink_to(measured)
                    before = measured.read_bytes()
                    with self.assertRaises(StackEvidenceError):
                        canonicalize_passive_file(ordinary, measured, identity)
                    self.assertEqual(before, measured.read_bytes())
                    if failure not in ("missing", "linked"):
                        self.assertEqual(normal, ordinary.read_bytes())

    def test_failed_atomic_publication_cleans_only_its_temporary_file(self):
        from tools.stack_artifact_files import canonicalize_passive_file

        normal = bytes(build_application_elf())
        with tempfile.TemporaryDirectory(prefix="micros-stack-publication-") as directory:
            root = Path(directory)
            ordinary, measured = root / "ordinary.elf", root / "metadata.elf"
            ordinary.write_bytes(normal)
            measured.write_bytes(normal + b"metadata")
            before = measured.read_bytes()
            with patch("tools.stack_artifact_files.os.replace", side_effect=OSError("denied")):
                with self.assertRaises(StackEvidenceError):
                    canonicalize_passive_file(ordinary, measured, _file_identity(ordinary))
            self.assertEqual(before, measured.read_bytes())
            self.assertEqual(normal, ordinary.read_bytes())
            self.assertEqual({"ordinary.elf", "metadata.elf"}, {path.name for path in root.iterdir()})


if __name__ == "__main__":
    unittest.main()
