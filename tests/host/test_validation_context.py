import copy
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools import validation_context


ROOT = Path(__file__).resolve().parents[2]


class ValidationContextTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()

    def digest(self):
        return validation_context.fingerprint_tree(self.root, (self.root,))

    def test_content_changes_are_detected_even_with_equal_size_and_mtime(self):
        path = self.root / "input"
        path.write_bytes(b"first")
        before = self.digest()
        information = path.stat()
        path.write_bytes(b"other")
        os.utime(path, ns=(information.st_atime_ns, information.st_mtime_ns))
        self.assertNotEqual(before, self.digest())

    def test_membership_mode_and_link_target_are_part_of_identity(self):
        path = self.root / "input"
        path.write_bytes(b"bytes")
        first = self.digest()
        (self.root / "new").write_bytes(b"")
        self.assertNotEqual(first, self.digest())
        (self.root / "new").unlink()
        self.assertEqual(first, self.digest())
        path.chmod(0o700)
        self.assertNotEqual(first, self.digest())
        (self.root / "alias").symlink_to("input")
        second = self.digest()
        (self.root / "alias").unlink()
        (self.root / "alias").symlink_to("./input")
        self.assertNotEqual(second, self.digest())

    def test_external_or_unresolved_link_cannot_close_a_manifest(self):
        (self.root / "escape").symlink_to(self.root.parent)
        with self.assertRaisesRegex(validation_context.UnsupportedContext, "link"):
            self.digest()
        (self.root / "escape").unlink()
        (self.root / "missing").symlink_to("absent")
        with self.assertRaisesRegex(validation_context.UnsupportedContext, "link"):
            self.digest()

    def test_link_to_another_explicit_root_is_closed_without_following_cycles(self):
        first = self.root / "first"
        second = self.root / "second"
        first.mkdir()
        second.mkdir()
        (first / "link").symlink_to(second, target_is_directory=True)
        (second / "back").symlink_to(first, target_is_directory=True)
        self.assertEqual(
            validation_context.fingerprint_tree(first, (first, second)),
            validation_context.fingerprint_tree(first, (first, second)),
        )

    def test_environment_fingerprint_includes_unlisted_inherited_values(self):
        first = {"PYTHONNOUSERSITE": "1", "ordinary": "private-value-one"}
        second = {**first, "ordinary": "private-value-two"}
        digest = validation_context.environment_digest(first)
        self.assertNotEqual(digest, validation_context.environment_digest(second))
        self.assertNotIn("private-value", digest)
        self.assertEqual(64, len(digest))

    def test_injection_and_unclosed_import_or_compiler_environment_is_unsupported(self):
        for variable in (
            "DYLD_INSERT_LIBRARIES", "LD_PRELOAD", "PYTHONPATH", "PYTHONHOME",
            "CPATH", "C_INCLUDE_PATH", "CFLAGS", "LDFLAGS",
            "CMAKE_PROJECT_INCLUDE", "QEMU_FIRMWAREPATH",
        ):
            with self.subTest(variable=variable):
                with self.assertRaises(validation_context.UnsupportedContext):
                    validation_context.validate_environment(
                        {
                            "PYTHONNOUSERSITE": "1", "CLANG_NO_DEFAULT_CONFIG": "1",
                            variable: "injected",
                        }, target=False
                    )
        with self.assertRaisesRegex(validation_context.UnsupportedContext, "PYTHONNOUSERSITE"):
            validation_context.validate_environment({}, target=False)
        with self.assertRaisesRegex(validation_context.UnsupportedContext, "MICROS_CLANG"):
            validation_context.validate_environment(
                {"PYTHONNOUSERSITE": "1", "CLANG_NO_DEFAULT_CONFIG": "1"}, target=True
            )

    def test_only_active_sdk_derived_shim_paths_are_admitted(self):
        sdk = self.root / "sdk"
        toolchain = self.root / "toolchain"
        for path in (sdk / "usr/include", sdk / "usr/lib", toolchain):
            path.mkdir(parents=True)
        environment = {
            "PYTHONNOUSERSITE": "1", "CLANG_NO_DEFAULT_CONFIG": "1", "SDKROOT": str(sdk),
            "CPATH": str(sdk / "usr/include"), "LIBRARY_PATH": str(sdk / "usr/lib"),
        }
        validation_context.validate_environment(environment, target=False)
        validation_context.validate_sdk_environment(environment, sdk, toolchain)
        with self.assertRaises(validation_context.UnsupportedContext):
            validation_context.validate_sdk_environment(
                {**environment, "CPATH": str(self.root)}, sdk, toolchain
            )

    def test_only_the_reviewed_inactive_vendor_startup_declaration_is_allowed(self):
        framework = self.root / "Python3.framework"
        startup = framework / "Versions/3.9/lib/python3.9/site-packages/distutils-precedence.pth"
        startup.parent.mkdir(parents=True)
        startup.write_bytes(b"vendor fixture, not executable startup text")
        with mock.patch(
            "tools.validation_context._file_digest",
            return_value="7ea7ffef3fe2a117ee12c68ed6553617f0d7fd2f0590257c25c484959a3b7373",
        ):
            validation_context.validate_python_startup(framework, {})
            with self.assertRaises(validation_context.UnsupportedContext):
                validation_context.validate_python_startup(
                    framework, {"SETUPTOOLS_USE_DISTUTILS": "local"}
                )
            (startup.parent / "unexpected.pth").write_bytes(b"unreviewed")
            with self.assertRaises(validation_context.UnsupportedContext):
                validation_context.validate_python_startup(framework, {})

    def preset_fixture(self):
        value = json.loads((ROOT / "CMakePresets.json").read_text())
        (self.root / "CMakePresets.json").write_text(json.dumps(value))
        return value

    def test_only_the_three_reviewed_workflows_have_reusable_declarations(self):
        self.preset_fixture()
        for name, directory, target in (
            ("test-unit", "host-debug", "test-unit"),
            ("test-qemu-smoke", "riscv64-debug", "test-qemu-smoke"),
            ("build-tty-service-image", "riscv64-tty-service-image", "micros_tty_service"),
        ):
            with self.subTest(name=name):
                profile = validation_context.resolve_preset(self.root, name)
                self.assertEqual(self.root / "build" / directory, profile.directory)
                self.assertEqual(target, profile.target)
        with self.assertRaises(validation_context.UnsupportedContext):
            validation_context.resolve_preset(self.root, "test-qemu-panic")

    def test_extra_workflow_steps_user_presets_or_build_redirects_are_unsupported(self):
        original = self.preset_fixture()
        for alteration in ("step", "directory", "target", "generator", "include"):
            value = copy.deepcopy(original)
            if alteration == "step":
                value["workflowPresets"][0]["steps"].append({"type": "build", "name": "other"})
            elif alteration == "directory":
                value["configurePresets"][0]["binaryDir"] = "/tmp/external-build"
            elif alteration == "target":
                next(
                    preset for preset in value["buildPresets"] if preset["name"] == "test-unit"
                )["targets"] = ["other"]
            elif alteration == "generator":
                value["configurePresets"][0]["generator"] = "Unix Makefiles"
            else:
                value["include"] = ["external.json"]
            (self.root / "CMakePresets.json").write_text(json.dumps(value))
            with self.subTest(alteration=alteration):
                with self.assertRaises(validation_context.UnsupportedContext):
                    validation_context.resolve_preset(self.root, "test-unit")
        (self.root / "CMakePresets.json").write_text(json.dumps(original))
        (self.root / "CMakeUserPresets.json").write_text("{}")
        with self.assertRaises(validation_context.UnsupportedContext):
            validation_context.resolve_preset(self.root, "test-unit")

    def test_missing_or_unsupported_platform_is_explicitly_ineligible(self):
        with self.assertRaisesRegex(validation_context.UnsupportedContext, "Darwin"):
            validation_context.platform_identity(system="Linux", machine="x86_64")

    def test_ninja_graph_identity_does_not_depend_on_process_pointer_ids(self):
        first = (
            'digraph ninja {\nrankdir="LR"\n'
            '"0x1" [label="file.c"]\n'
            '"0x2" [label="C_COMPILER__test_Debug", shape=ellipse]\n'
            '"0x3" [label="file.o"]\n'
            '"0x1" -> "0x2" [arrowhead=none]\n'
            '"0x2" -> "0x3"\n}\n'
        )
        second = first.replace("0x1", "0xaa").replace("0x2", "0xbb").replace("0x3", "0xcc")
        self.assertEqual(
            validation_context.graph_nodes(first), validation_context.graph_nodes(second)
        )
        self.assertEqual(("file.c", "file.o"), validation_context.graph_nodes(first)["files"])
        self.assertEqual(
            ("file.c", "file.o"),
            validation_context.graph_nodes(
                first.replace("arrowhead=none", "arrowhead=none style=dotted")
            )["files"],
        )
        sibling = first.replace('}\n', '"0x2" -> "0xff"\n}\n')
        self.assertEqual(
            ("C_COMPILER__test_Debug",),
            validation_context.graph_nodes(sibling)["unselected_outputs"],
        )
        with self.assertRaises(validation_context.UnsupportedContext):
            validation_context.graph_nodes(first.replace('}\n', '"0xff" -> "0x2"\n}\n'))
        with self.assertRaises(validation_context.UnsupportedContext):
            validation_context.graph_nodes(first.replace("file.c", "unclosed\\q"))

    def test_command_manifest_rejects_new_shell_or_dynamic_resolution(self):
        for command in (
            "/bin/sh -c unexpected",
            "/tool/compiler source.c; unexpected",
            "/tool/compiler $(unexpected)",
            "/tool/compiler source.c | unexpected",
        ):
            with self.subTest(command=command):
                with self.assertRaises(validation_context.UnsupportedContext):
                    validation_context.command_tools(command, self.root, {"/tool/compiler"})
        self.assertEqual(
            {"/tool/compiler"},
            validation_context.command_tools(
                f"cd {self.root} && /tool/compiler source.c && :",
                self.root, {"/tool/compiler"},
            ),
        )

    def native_fixture(self):
        (self.root / "CMakeLists.txt").write_text(
            "add_test(NAME fast COMMAND fast_test)\n"
            "add_test(NAME model COMMAND model_test)\n"
            "set_tests_properties(model PROPERTIES LABELS slow)\n"
        )
        build = self.root / "build"
        build.mkdir()
        for name in ("fast_test", "model_test"):
            (build / name).write_bytes(b"fixture executable")
        return build, {
            "tests": [
                {"name": "fast", "command": [str(build / "fast_test")], "properties": []},
                {
                    "name": "model", "command": [str(build / "model_test")],
                    "properties": [{"name": "LABELS", "value": ["slow"]}],
                },
            ]
        }

    def test_native_outputs_include_every_case_and_slow_model_label(self):
        build, inventory = self.native_fixture()
        files = validation_context.native_outputs(self.root, build, inventory)
        self.assertEqual({build / "fast_test", build / "model_test"}, set(files))
        seed = build / "seed.bin"
        seed.write_bytes(b"fixture data")
        inventory["tests"][0]["command"].append(str(seed))
        self.assertEqual(
            (build / "fast_test", build / "model_test"),
            validation_context.native_runtime_executables(inventory),
        )
        for changed in ("missing", "label", "command", "argument"):
            altered = copy.deepcopy(inventory)
            if changed == "missing":
                altered["tests"].pop()
            elif changed == "label":
                altered["tests"][-1]["properties"] = []
            elif changed == "command":
                altered["tests"][-1]["command"][0] = str(build / "fast_test")
            else:
                altered["tests"][-1]["command"].append("/tmp/external")
            with self.subTest(changed=changed):
                with self.assertRaises(validation_context.UnsupportedContext):
                    validation_context.native_outputs(self.root, build, altered)

    def test_macho_install_id_is_not_an_unresolved_runtime_dependency(self):
        library = self.root / "Python3"
        library.write_bytes(b"\xcf\xfa\xed\xfemetadata fixture")
        tools = validation_context.DarwinTools.__new__(validation_context.DarwinTools)
        tools._visited_libraries = set()

        def metadata(command, **options):
            self.assertEqual(("-arch", "arm64"), command[2:4])
            if command[1] == "-l":
                return (
                    b"Load command 1\n      cmd LC_ID_DYLIB\n  cmdsize 80\n"
                    b"     name @rpath/Python3 (offset 24)\n"
                )
            return (
                b"Python3:\n\t@rpath/Python3 (compatibility version 3.9.0, current version 3.9.0)\n"
                b"\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1.0.0)\n"
            )

        with mock.patch("tools.validation_context._run", side_effect=metadata):
            tools.libraries(library)

    def test_exact_loader_and_executable_directory_rpath_tokens_are_resolved(self):
        tools = validation_context.DarwinTools.__new__(validation_context.DarwinTools)
        loader = self.root / "lib/loader"
        executable = self.root / "bin/program"
        self.assertEqual(
            executable.parent, tools._expand("@executable_path", loader, executable)
        )
        self.assertEqual(loader.parent, tools._expand("@loader_path", loader, executable))

    def test_multicall_linker_driver_spelling_is_not_replaced_by_its_realpath(self):
        compiler = self.root / "clang"
        compiler.write_bytes(b"compiler fixture")
        linker = self.root / "lld"
        linker.write_bytes(b"multicall fixture")
        driver = self.root / "ld.lld"
        driver.symlink_to(linker)
        tools = validation_context.DarwinTools.__new__(validation_context.DarwinTools)
        tools.clang = compiler
        tools.profile = mock.Mock(target_build=True)
        tools.environment = {"MICROS_CLANG": str(compiler), "MICROS_LD_LLD": str(driver)}
        tools.cache = {
            "CMAKE_C_COMPILER": str(compiler), "MICROS_LD_LLD": str(driver),
        }
        tools._tool = mock.Mock()
        with mock.patch("tools.validation_context._run", return_value=b"/usr/bin/make\n"):
            tools._selected_tools()
        self.assertIn(mock.call("linker", driver), tools._tool.call_args_list)

    def test_automatic_compiler_configuration_must_be_explicitly_disabled(self):
        for value in (None, "", "0", "true"):
            environment = {"PYTHONNOUSERSITE": "1"}
            if value is not None:
                environment["CLANG_NO_DEFAULT_CONFIG"] = value
            with self.subTest(value=value):
                with self.assertRaisesRegex(validation_context.UnsupportedContext, "CLANG_NO_DEFAULT_CONFIG"):
                    validation_context.validate_environment(environment, target=False)
        environment = {"PYTHONNOUSERSITE": "1", "CLANG_NO_DEFAULT_CONFIG": "1"}
        validation_context.validate_environment(environment, target=False)
        with self.assertRaises(validation_context.UnsupportedContext):
            validation_context.validate_environment(
                {**environment, "CCC_OVERRIDE_OPTIONS": "^--config=external.cfg"}, target=False
            )

    def test_only_reviewed_native_and_target_compiler_families_are_supported(self):
        self.assertEqual(
            {"family": "AppleClang", "major": 21},
            validation_context.compiler_version("Apple clang version 21.0.0\n", target=False),
        )
        self.assertEqual(
            {"family": "LLVMClang", "major": 23},
            validation_context.compiler_version("Homebrew clang version 23.1.3\n", target=True),
        )
        for value, target in (
            ("clang version 22.0.0\n", True),
            ("Apple clang version 20.0.0\n", False),
            ("gcc version 23.0\n", True),
            ("Apple clang version 23.0\n", True),
        ):
            with self.subTest(value=value, target=target):
                with self.assertRaises(validation_context.UnsupportedContext):
                    validation_context.compiler_version(value, target=target)

    def compiler_fixture(self):
        installation = self.root / "Cellar/llvm/23.1.3"
        (installation / "etc").mkdir(parents=True)
        target = self.root / "etc/clang"
        target.mkdir(parents=True)
        return installation, target

    def test_only_the_selected_same_prefix_compiler_configuration_alias_is_admitted(self):
        installation, target = self.compiler_fixture()
        alias = installation / "etc/clang"
        alias.symlink_to(target, target_is_directory=True)
        (target / "native.cfg").write_bytes(b"opaque fixture data, never executed")
        metadata, roots = validation_context.compiler_configuration(installation)
        self.assertEqual((target,), roots)
        self.assertEqual("automatic-disabled", metadata["policy"])
        self.assertEqual(str(target), metadata["alias"]["resolved"])
        self.assertTrue(
            validation_context.fingerprint_tree(installation, (installation, target))
        )
        alias.unlink()
        unknown = self.root / "unreviewed"
        unknown.mkdir()
        alias.symlink_to(unknown, target_is_directory=True)
        with self.assertRaises(validation_context.UnsupportedContext):
            validation_context.compiler_configuration(installation)

    def test_compiler_alias_absence_and_dangling_target_have_distinct_outcomes(self):
        installation, target = self.compiler_fixture()
        metadata, roots = validation_context.compiler_configuration(installation)
        self.assertEqual((), roots)
        self.assertEqual("absent", metadata["alias"]["kind"])
        (installation / "etc/clang").symlink_to(target / "missing", target_is_directory=True)
        with self.assertRaises(validation_context.UnsupportedContext):
            validation_context.compiler_configuration(installation)

    def test_explicit_configuration_and_response_inputs_are_not_reusable(self):
        for argument in (
            "--config=external.cfg", "--config external.cfg",
            "--config-system-dir=/external", "--config-user-dir=/external",
            "@response.rsp", "-Wl,@response.rsp",
        ):
            with self.subTest(argument=argument):
                with self.assertRaises(validation_context.UnsupportedContext):
                    validation_context.command_tools(
                        "/tool/compiler " + argument, self.root, {"/tool/compiler"}
                    )

    def qemu_fixture(self):
        installation = self.root / "Cellar/qemu/11.1.2"
        executable = installation / "bin/qemu-system-riscv64"
        executable.parent.mkdir(parents=True)
        config = installation / "etc/qemu/qemu.conf"
        executable.write_bytes(b"image fixture\0" + str(config).encode() + b"\0")
        return installation, executable

    def test_qemu_declares_both_configs_and_bundle_absent_without_execution(self):
        installation, executable = self.qemu_fixture()
        identity = validation_context.qemu_configuration(executable, installation)
        self.assertEqual(
            {
                str(installation / "etc/qemu/qemu.conf"),
                str(self.root / "etc/qemu/qemu.conf"),
                str(executable.parent / "qemu-bundle"),
            },
            set(identity["absent"]),
        )
        self.assertTrue(all(value == "absent" for value in identity["absent"].values()))
        executable.write_bytes(b"unknown layout\0")
        with self.assertRaises(validation_context.UnsupportedContext):
            validation_context.qemu_configuration(executable, installation)

    def test_qemu_present_config_bundle_and_symlink_are_refused_before_metadata(self):
        installation, executable = self.qemu_fixture()
        tools = validation_context.DarwinTools.__new__(validation_context.DarwinTools)
        tools.cache = {"MICROS_QEMU": str(executable)}
        tools._tool = mock.Mock(side_effect=AssertionError("metadata executed before absence checks"))
        bundle = executable.parent / "qemu-bundle"
        for location in (
            installation / "etc/qemu/qemu.conf",
            self.root / "etc/qemu/qemu.conf",
            bundle,
        ):
            location.parent.mkdir(parents=True, exist_ok=True)
            location.write_bytes(b"must not be interpreted")
            with self.subTest(location=location):
                with mock.patch("tools.validation_context._cellar_root", return_value=installation):
                    with self.assertRaises(validation_context.UnsupportedContext):
                        tools.qemu()
            location.unlink()
        bundle.symlink_to(self.root, target_is_directory=True)
        with mock.patch("tools.validation_context._cellar_root", return_value=installation):
            with self.assertRaises(validation_context.UnsupportedContext):
                tools.qemu()
        tools._tool.assert_not_called()

    def candidate_fixture(self, *, dirty=False):
        (self.root / ".git").mkdir()
        source = self.root / "source.c"
        source.write_bytes(b"reviewed source bytes\n")
        data = source.read_bytes()
        blob = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        responses = {
            ("rev-parse", "--show-toplevel"): str(self.root).encode() + b"\n",
            ("ls-files", "--others", "--exclude-standard", "-z"): b"",
            ("rev-parse", "--git-common-dir"): b".git\n",
            ("rev-parse", "--verify", "HEAD"): b"a" * 40 + b"\n",
            ("write-tree",): b"b" * 40 + b"\n",
            ("ls-files", "--stage", "-z"): f"100644 {blob} 0\tsource.c\0".encode(),
            ("diff", "--name-only", "-z"): b"source.c\0" if dirty else b"",
        }

        def query(command, *, cwd=None):
            self.assertEqual(self.root, cwd)
            self.assertEqual("git", command[0])
            return responses[tuple(command[1:])]

        return source, responses, query

    def test_candidate_binds_head_staged_tree_repository_and_actual_source(self):
        _, _, query = self.candidate_fixture()
        with mock.patch("tools.validation_context._run", side_effect=query):
            candidate = validation_context.capture_candidate(self.root)
        self.assertTrue(candidate.clean)
        self.assertEqual("a" * 40, candidate.source["head"])
        self.assertEqual("b" * 40, candidate.source["tree"])
        self.assertEqual(str(self.root), candidate.repository["root"])
        self.assertEqual(str(self.root / ".git"), candidate.repository["git_common"])
        self.assertEqual(64, len(candidate.source["source_sha256"]))

    def test_git_clean_flags_cannot_hide_source_bytes_outside_the_staged_tree(self):
        source, _, query = self.candidate_fixture()
        source.write_bytes(b"unreviewed source now\n")
        with mock.patch("tools.validation_context._run", side_effect=query):
            candidate = validation_context.capture_candidate(self.root)
        self.assertFalse(candidate.clean)

    def test_dirty_iteration_remains_representable_without_reusable_evidence(self):
        _, _, query = self.candidate_fixture(dirty=True)
        with mock.patch("tools.validation_context._run", side_effect=query):
            self.assertFalse(validation_context.capture_candidate(self.root).clean)

    def test_untracked_source_blocks_candidate_evidence(self):
        _, responses, query = self.candidate_fixture()
        responses[("ls-files", "--others", "--exclude-standard", "-z")] = b"new.c\0"
        with mock.patch("tools.validation_context._run", side_effect=query):
            with self.assertRaisesRegex(validation_context.UnsupportedContext, "untracked"):
                validation_context.capture_candidate(self.root)

    def test_tracked_link_cannot_claim_untracked_ignored_target_bytes(self):
        source, responses, query = self.candidate_fixture()
        source.unlink()
        target = self.root / "ignored.c"
        target.write_bytes(b"not in the reviewed index")
        source.symlink_to(target)
        data = os.fsencode(os.readlink(source))
        blob = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        responses[("ls-files", "--stage", "-z")] = f"120000 {blob} 0\tsource.c\0".encode()
        with mock.patch("tools.validation_context._run", side_effect=query):
            with self.assertRaises(validation_context.UnsupportedContext):
                validation_context.capture_candidate(self.root)


if __name__ == "__main__":
    unittest.main()
