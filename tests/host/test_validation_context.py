import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tools import (
    python_host_runtime,
    validation_build_manifest,
    validation_context,
)


ROOT = Path(__file__).resolve().parents[2]


class ValidationContextTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()

    def environment(self, **values):
        return {
            "OPENSSL_CONF": "/dev/null",
            "SSL_CERT_FILE": "/dev/null",
            "SSL_CERT_DIR": "/dev/null",
            "CLANG_NO_DEFAULT_CONFIG": "1",
            **values,
        }

    def digest(self):
        return validation_context.fingerprint_tree(self.root, (self.root,))

    def python_runtime_fixture(self):
        installation = self.root / "Cellar/python@3.15/3.15.0"
        framework = installation / "Frameworks/Python.framework"
        prefix = framework / "Versions/3.15"
        executable = installation / "bin/python3.15"
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b"python fixture")
        base_executable = prefix / "bin/python3.15"
        base_executable.parent.mkdir(parents=True)
        base_executable.write_bytes(b"framework python fixture")
        stdlib = prefix / "lib/python3.15"
        (stdlib / "lib-dynload").mkdir(parents=True)
        formula = self.root / "opt/python@3.15"
        formula.parent.mkdir(parents=True)
        formula.symlink_to(installation, target_is_directory=True)
        selected = formula / "bin/python3.15"
        metadata = {
            "implementation": "cpython",
            "version": [3, 15, 0],
            "releaselevel": "final",
            "cache_tag": "cpython-315",
            "gil_enabled": True,
            "py_gil_disabled": 0,
            "flags": {
                "ignore_environment": 1,
                "no_site": 1,
                "dont_write_bytecode": 1,
                "isolated": 0,
                "safe_path": 0,
            },
            "executable": str(selected),
            "base_executable": str(base_executable),
            "prefix": str(prefix),
            "base_prefix": str(prefix),
            "stdlib": str(stdlib),
            "paths": [
                str(ROOT),
                str(prefix / "lib/python315.zip"),
                str(stdlib),
                str(stdlib / "lib-dynload"),
            ],
            "cafile": None,
            "capath": None,
            "certificate_count": 0,
        }
        return selected, executable, installation, metadata

    def qemu_family_fixture(self):
        paths = (
            "tools/run_qemu_smoke.py", "tools/qemu_smoke_records.py", "tools/qemu_smoke_platform.py",
            "tools/qemu_smoke_objects.py", "tools/qemu_smoke_faults.py", "tools/qemu_smoke_command.py",
            "tools/qemu_smoke_execution.py", "tools/qemu_smoke_outcome.py",
            "tools/qemu_smoke_arguments.py", "tools/qemu_smoke_cli.py",
        )
        expected = {}
        for index, name in enumerate(paths):
            path = self.root / name
            path.parent.mkdir(exist_ok=True)
            raw = ("fixed reviewed source member " + str(index) + "\n").encode()
            path.write_bytes(raw)
            expected[name] = hashlib.sha256(raw).hexdigest()
        return expected

    def test_qemu_source_family_refuses_package_shadowing_before_metadata(self):
        expected = self.qemu_family_fixture()
        shadow = self.root / "tools/qemu_smoke_command"
        shadow.mkdir()
        (shadow / "__init__.pyc").write_bytes(b"ignored replacement code, never executed")
        tools = mock.Mock()
        tools.qemu.side_effect = AssertionError("QEMU metadata ran before source-family refusal")
        with mock.patch.multiple(
            validation_context,
            resolve_preset=mock.Mock(return_value=mock.Mock(target_build=True)),
            validate_environment=mock.Mock(), platform_identity=mock.Mock(return_value={}),
            _cache=mock.Mock(return_value={}), DarwinTools=mock.Mock(return_value=tools),
            RECIPE_DIGESTS={},
            QEMU_RUNNER_DIGEST=expected["tools/run_qemu_smoke.py"],
        ), mock.patch.object(validation_context, "QEMU_RUNNER_SOURCE_DIGESTS", expected, create=True):
            with self.assertRaises(validation_context.UnsupportedContext):
                validation_context.collect(self.root, "test-qemu-smoke")
        tools.qemu.assert_not_called()

    def qemu_loader_fixture(self):
        names = (
            "qemu_smoke_records", "qemu_smoke_platform", "qemu_smoke_objects", "qemu_smoke_faults",
            "qemu_smoke_command", "qemu_smoke_execution", "qemu_smoke_outcome",
            "qemu_smoke_arguments", "qemu_smoke_cli",
        )
        return {
            "implementation": "cpython", "source": [".py"], "bytecode": [".pyc"],
            "extensions": [
                ".cpython-315-darwin.so",
                ".abi3-darwin.so",
                ".abi3.so",
                ".abi3t-darwin.so",
                ".abi3t.so",
                ".so",
            ],
            "namespace": {"origin": None, "loader": None, "locations": [str(self.root / "tools")]},
            "helpers": {
                name: {"origin": str(self.root / "tools" / (name + ".py")), "loader": "SourceFileLoader", "locations": []}
                for name in names
            },
        }

    def test_qemu_source_family_requires_all_fixed_members_and_refuses_loader_files(self):
        expected = self.qemu_family_fixture()
        with mock.patch.object(validation_context, "QEMU_RUNNER_SOURCE_DIGESTS", expected, create=True):
            validation_context._validate_qemu_runner_family(self.root)
            changed = self.root / "tools/qemu_smoke_command.py"
            original = changed.read_bytes()
            changed.write_bytes(original + b"changed")
            with self.assertRaises(validation_context.UnsupportedContext):
                validation_context._validate_qemu_runner_family(self.root)
            changed.write_bytes(original)
            for relative in ("tools/__init__.py", "tools/qemu_smoke_command.pyc", "tools.py"):
                path = self.root / relative
                path.write_bytes(b"unreviewed loader input")
                with self.subTest(relative=relative), self.assertRaises(validation_context.UnsupportedContext):
                    validation_context._validate_qemu_runner_family(self.root)
                path.unlink()
            cache = self.root / "tools/__pycache__"
            cache.mkdir()
            (cache / "qemu_smoke_command.cpython-315.pyc").write_bytes(b"cached replacement, never executed")
            with self.assertRaises(validation_context.UnsupportedContext):
                validation_context._validate_qemu_runner_family(self.root)
            (cache / "qemu_smoke_command.cpython-315.pyc").unlink()
            changed.unlink()
            changed.symlink_to(self.root / "tools/qemu_smoke_records.py")
            with self.assertRaises(validation_context.UnsupportedContext):
                validation_context._validate_qemu_runner_family(self.root)

    def test_qemu_namespace_accepts_only_standard_workflow_source_origins_without_importing(self):
        self.qemu_family_fixture()
        tools = mock.Mock(python=Path("/verified/workflow-python"))
        information = self.qemu_loader_fixture()
        with mock.patch("tools.validation_context._run", return_value=json.dumps(information).encode()) as run:
            validation_context._validate_qemu_runner_namespace(self.root, tools)
        self.assertEqual(tools.python, run.call_args.args[0][0])
        self.assertEqual(("-E", "-S", "-B", "-c"), run.call_args.args[0][1:5])
        information["namespace"]["locations"].append(str(self.root / "foreign-tools"))
        with mock.patch("tools.validation_context._run", return_value=json.dumps(information).encode()):
            with self.assertRaises(validation_context.UnsupportedContext):
                validation_context._validate_qemu_runner_namespace(self.root, tools)

    def test_qemu_namespace_models_script_directory_before_any_qemu_metadata(self):
        expected = self.qemu_family_fixture()
        tools = mock.Mock(python=Path(sys.executable))
        tools.qemu.side_effect = AssertionError("QEMU metadata ran before script-directory refusal")

        def probe(command, *, cwd):
            self.assertEqual((tools.python, "-E", "-S", "-B", "-c"), command[:5])
            completed = subprocess.run(
                command, cwd=cwd,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
            )
            return completed.stdout

        with mock.patch.object(validation_context, "_run", side_effect=probe):
            validation_context._validate_qemu_runner_namespace(self.root, tools)
            for relative, directory in (
                ("tools.pyc", False), ("tools.so", False),
                ("tools/__init__.pyc", False), ("tools", True),
            ):
                path = self.root / "tools" / relative
                if directory:
                    path.mkdir()
                else:
                    path.parent.mkdir(exist_ok=True)
                    path.write_bytes(b"replacement metadata fixture, never imported or executed")
                try:
                    with self.subTest(relative=relative), mock.patch.multiple(
                        validation_context,
                        resolve_preset=mock.Mock(return_value=mock.Mock(target_build=True)),
                        validate_environment=mock.Mock(), platform_identity=mock.Mock(return_value={}),
                        _cache=mock.Mock(return_value={}), DarwinTools=mock.Mock(return_value=tools),
                        RECIPE_DIGESTS={}, QEMU_RUNNER_DIGEST=expected["tools/run_qemu_smoke.py"],
                        QEMU_RUNNER_SOURCE_DIGESTS=expected,
                    ):
                        with self.assertRaises(validation_context.UnsupportedContext):
                            validation_context.collect(self.root, "test-qemu-smoke")
                        tools.qemu.assert_not_called()
                finally:
                    if directory:
                        path.rmdir()
                    else:
                        path.unlink()
                        if path.parent != self.root / "tools":
                            path.parent.rmdir()

    def test_qemu_namespace_refuses_package_native_sourceless_and_initializer_replacements(self):
        self.qemu_family_fixture()
        tools = mock.Mock(python=Path("/verified/workflow-python"))
        original = self.qemu_loader_fixture()
        for loader in ("SourcelessFileLoader", "ExtensionFileLoader", "UnknownLoader"):
            value = copy.deepcopy(original)
            value["helpers"]["qemu_smoke_command"]["loader"] = loader
            with mock.patch("tools.validation_context._run", return_value=json.dumps(value).encode()):
                with self.subTest(loader=loader), self.assertRaises(validation_context.UnsupportedContext):
                    validation_context._validate_qemu_runner_namespace(self.root, tools)
        with mock.patch("tools.validation_context._run", return_value=json.dumps(original).encode()):
            for relative in (
                "tools/qemu_smoke_command.cpython-315-darwin.so",
                "tools/qemu_smoke_command.abi3-darwin.so",
                "tools/qemu_smoke_command.abi3.so",
                "tools/qemu_smoke_command.abi3t-darwin.so",
                "tools/qemu_smoke_command.abi3t.so",
                "tools/qemu_smoke_command.so",
                "tools/__init__.so",
                "tools.so",
            ):
                path = self.root / relative
                path.write_bytes(b"native replacement metadata fixture, never loaded")
                with self.subTest(relative=relative), self.assertRaises(validation_context.UnsupportedContext):
                    validation_context._validate_qemu_runner_namespace(self.root, tools)
                path.unlink()

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

    def test_inert_site_link_records_spelling_without_admitting_target_bytes(self):
        installation = self.root / "python"
        site = installation / "lib/python3.15/site-packages"
        target = self.root / "external-cert.pem"
        site.mkdir(parents=True)
        target.write_bytes(b"external trust bytes")
        link = site / "cacert.pem"
        link.symlink_to(target)
        first = validation_context.fingerprint_tree(
            installation,
            (installation,),
            inert_link_roots=(site,),
        )
        target.write_bytes(b"changed inactive trust bytes")
        self.assertEqual(
            first,
            validation_context.fingerprint_tree(
                installation,
                (installation,),
                inert_link_roots=(site,),
            ),
        )
        link.unlink()
        link.symlink_to("../../../../external-cert.pem")
        self.assertNotEqual(
            first,
            validation_context.fingerprint_tree(
                installation,
                (installation,),
                inert_link_roots=(site,),
            ),
        )

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
        first = {**self.environment(), "ordinary": "private-value-one"}
        second = {**first, "ordinary": "private-value-two"}
        digest = validation_context.environment_digest(first)
        self.assertNotEqual(digest, validation_context.environment_digest(second))
        self.assertNotIn("private-value", digest)
        self.assertEqual(64, len(digest))

    def test_injection_and_unclosed_import_or_compiler_environment_is_unsupported(self):
        for variable in (
            "DYLD_INSERT_LIBRARIES", "LD_PRELOAD", "PYTHONPATH", "PYTHONHOME",
            "PYTHONEXECUTABLE", "__PYVENV_LAUNCHER__", "OPENSSL_MODULES",
            "_PYTHON_SYSCONFIGDATA_NAME", "_PYTHON_SYSCONFIGDATA_PATH",
            "_PYTHON_PROJECT_BASE", "_PYTHON_HOST_PLATFORM",
            "RANDFILE", "CTLOG_FILE", "MICROS_PYTHON_ROOT",
            "CPATH", "C_INCLUDE_PATH", "CFLAGS", "LDFLAGS",
            "CMAKE_PROJECT_INCLUDE", "QEMU_FIRMWAREPATH",
        ):
            with self.subTest(variable=variable):
                with self.assertRaises(validation_context.UnsupportedContext):
                    validation_context.validate_environment(
                        self.environment(**{variable: "injected"}),
                        target=False,
                    )
        for name in ("OPENSSL_CONF", "SSL_CERT_FILE", "SSL_CERT_DIR"):
            environment = self.environment()
            environment.pop(name)
            with self.subTest(name=name), self.assertRaisesRegex(
                validation_context.UnsupportedContext,
                name,
            ):
                validation_context.validate_environment(environment, target=False)
        with self.assertRaisesRegex(validation_context.UnsupportedContext, "MICROS_CLANG"):
            validation_context.validate_environment(
                self.environment(),
                target=True,
            )

    def test_only_active_sdk_derived_shim_paths_are_admitted(self):
        sdk = self.root / "sdk"
        toolchain = self.root / "toolchain"
        for path in (sdk / "usr/include", sdk / "usr/lib", toolchain):
            path.mkdir(parents=True)
        environment = self.environment(
            SDKROOT=str(sdk),
            CPATH=str(sdk / "usr/include"),
            LIBRARY_PATH=str(sdk / "usr/lib"),
        )
        validation_context.validate_environment(environment, target=False)
        validation_context.validate_sdk_environment(environment, sdk, toolchain)
        with self.assertRaises(validation_context.UnsupportedContext):
            validation_context.validate_sdk_environment(
                {**environment, "CPATH": str(self.root)}, sdk, toolchain
            )

    def test_inert_site_customization_remains_fingerprinted_not_allowlisted(self):
        framework = self.root / "Python.framework"
        startup = framework / "Versions/3.15/lib/python3.15/sitecustomize.py"
        startup.parent.mkdir(parents=True)
        startup.write_bytes(b"inert under -S")
        first = validation_context.fingerprint_tree(framework, (framework,))
        startup.write_bytes(b"changed but still inert under -S")
        self.assertNotEqual(
            first,
            validation_context.fingerprint_tree(framework, (framework,)),
        )

    def test_reusable_python_matches_cache_environment_and_current_process(self):
        selected, executable, installation, metadata = (
            self.python_runtime_fixture()
        )
        tools = validation_context.DarwinTools.__new__(
            validation_context.DarwinTools
        )
        tools.root = ROOT
        tools.cache = {"MICROS_PYTHON": str(selected)}
        tools.environment = self.environment(MICROS_PYTHON=str(selected))
        tools.roots = set()
        tools._tool = mock.Mock()
        with mock.patch(
            "tools.validation_context._run",
            return_value=json.dumps(metadata).encode(),
        ) as run, mock.patch.object(
            validation_context.sys,
            "executable",
            str(selected),
        ), mock.patch(
            "tools.validation_context._cellar_root",
            return_value=installation,
        ), mock.patch(
            "tools.validation_context.fingerprint_tree",
            return_value="closed-python-spelling",
        ):
            tools._python()
        self.assertEqual(
            (
                str(selected),
                "-E",
                "-S",
                "-B",
                str(ROOT / "tools/python_host_runtime.py"),
                "--selected",
                str(selected),
                "--metadata",
            ),
            run.call_args.args[0],
        )
        self.assertEqual(executable, tools.python)
        self.assertTrue(tools.python_spellings)
        tools._tool.assert_has_calls([
            mock.call("python", str(selected)),
            mock.call("python-runtime", tools.python),
        ])

        other = self.root / "other-python"
        other.write_bytes(b"other python fixture")
        for cache, environment in (
            ({"MICROS_PYTHON": str(selected)}, self.environment()),
            (
                {"MICROS_PYTHON": str(other)},
                self.environment(MICROS_PYTHON=str(other)),
            ),
        ):
            tools.cache = cache
            tools.environment = environment
            with self.subTest(cache=cache), self.assertRaises(
                validation_context.UnsupportedContext
            ):
                tools._python()

    def test_reusable_python_rejects_external_active_framework(self):
        selected, _, installation, metadata = self.python_runtime_fixture()
        framework = self.root / "external/Python.framework"
        prefix = framework / "Versions/3.15"
        base_executable = prefix / "bin/python3.15"
        base_executable.parent.mkdir(parents=True)
        base_executable.write_bytes(b"external framework Python")
        stdlib = prefix / "lib/python3.15"
        (stdlib / "lib-dynload").mkdir(parents=True)
        external = {
            **metadata,
            "base_executable": str(base_executable),
            "prefix": str(prefix),
            "base_prefix": str(prefix),
            "stdlib": str(stdlib),
            "paths": [
                str(ROOT),
                str(prefix / "lib/python315.zip"),
                str(stdlib),
                str(stdlib / "lib-dynload"),
            ],
        }
        tools = validation_context.DarwinTools.__new__(
            validation_context.DarwinTools
        )
        tools.root = ROOT
        tools.cache = {"MICROS_PYTHON": str(selected)}
        tools.environment = self.environment(
            MICROS_PYTHON=str(selected)
        )
        tools.roots = set()
        tools._tool = mock.Mock()
        with mock.patch(
            "tools.validation_context._run",
            return_value=json.dumps(external).encode(),
        ), mock.patch.object(
            validation_context.sys,
            "executable",
            str(selected),
        ), mock.patch(
            "tools.validation_context._cellar_root",
            return_value=installation,
        ), mock.patch(
            "tools.validation_context.fingerprint_tree",
            return_value="external-framework-was-admitted",
        ), self.assertRaisesRegex(
            validation_context.UnsupportedContext,
            "framework.*installation",
        ):
            tools._python()

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
            validation_build_manifest.graph_nodes(first), validation_build_manifest.graph_nodes(second)
        )
        self.assertEqual(("file.c", "file.o"), validation_build_manifest.graph_nodes(first)["files"])
        self.assertEqual(
            ("file.c", "file.o"),
            validation_build_manifest.graph_nodes(
                first.replace("arrowhead=none", "arrowhead=none style=dotted")
            )["files"],
        )
        sibling = first.replace('}\n', '"0x2" -> "0xff"\n}\n')
        self.assertEqual(
            ("C_COMPILER__test_Debug",),
            validation_build_manifest.graph_nodes(sibling)["unselected_outputs"],
        )
        with self.assertRaises(validation_context.UnsupportedContext):
            validation_build_manifest.graph_nodes(first.replace('}\n', '"0xff" -> "0x2"\n}\n'))
        with self.assertRaises(validation_context.UnsupportedContext):
            validation_build_manifest.graph_nodes(first.replace("file.c", "unclosed\\q"))

    def test_command_manifest_rejects_new_shell_or_dynamic_resolution(self):
        for command in (
            "/bin/sh -c unexpected",
            "/tool/compiler source.c; unexpected",
            "/tool/compiler $(unexpected)",
            "/tool/compiler source.c | unexpected",
        ):
            with self.subTest(command=command):
                with self.assertRaises(validation_context.UnsupportedContext):
                    validation_build_manifest.command_tools(command, self.root, {"/tool/compiler"})
        self.assertEqual(
            {"/tool/compiler"},
            validation_build_manifest.command_tools(
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
        files = validation_build_manifest.native_outputs(self.root, build, inventory)
        self.assertEqual({build / "fast_test", build / "model_test"}, set(files))
        seed = build / "seed.bin"
        seed.write_bytes(b"fixture data")
        inventory["tests"][0]["command"].append(str(seed))
        self.assertEqual(
            (build / "fast_test", build / "model_test"),
            validation_build_manifest.native_runtime_executables(inventory),
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
                    validation_build_manifest.native_outputs(self.root, build, altered)

    def test_missing_declared_native_output_is_typed_preparation(self):
        build, inventory = self.native_fixture()
        missing = build / "fast_test"
        missing.unlink()
        preparation = []
        outputs = validation_build_manifest.native_outputs(
            self.root,
            build,
            inventory,
            preparation=preparation,
        )
        self.assertIn(missing, outputs)
        self.assertEqual(
            ["declared-output-absent"],
            [item.reason_code for item in preparation],
        )
        with self.assertRaises(validation_context.UnsupportedContext):
            validation_build_manifest.native_outputs(
                self.root,
                build,
                inventory,
            )
        missing.symlink_to(build / "model_test")
        with self.assertRaises(validation_context.UnsupportedContext):
            validation_build_manifest.native_outputs(
                self.root,
                build,
                inventory,
                preparation=[],
            )

    def test_stale_selected_dependency_is_typed_preparation(self):
        build = self.root / "build"
        build.mkdir()
        source = self.root / "source.h"
        source.write_bytes(b"header")
        tools = mock.Mock(
            executables={"ninja": {"path": "/tool/ninja"}},
            profile=mock.Mock(directory=build),
        )
        metadata = (
            "fast_test: #deps 1, deps mtime 0 (STALE)\n"
            f"    {source}\n"
        ).encode()
        with mock.patch(
            "tools.validation_build_manifest._run",
            return_value=metadata,
        ):
            preparation = []
            self.assertEqual(
                {source},
                validation_build_manifest._dependency_inputs(
                    tools,
                    {"fast_test"},
                    preparation=preparation,
                ),
            )
            self.assertEqual(
                ["selected-dependency-stale"],
                [item.reason_code for item in preparation],
            )
            with self.assertRaises(validation_context.UnsupportedContext):
                validation_build_manifest._dependency_inputs(
                    tools,
                    {"fast_test"},
                )

    def test_missing_selected_build_output_is_typed_preparation(self):
        build = self.root / "build"
        (build / "CMakeFiles/compiler").mkdir(parents=True)
        (build / "CMakeCache.txt").write_text("cache")
        (build / "build.ninja").write_text("")
        (build / "CMakeFiles/compiler/CMakeCCompiler.cmake").write_text(
            "compiler"
        )
        source = self.root / "source.c"
        source.write_text("int source;\n")
        profile = mock.Mock(
            directory=build,
            target="test-qemu-smoke",
            target_build=True,
        )
        tools = mock.Mock(
            executables={
                "ninja": {"path": "/tool/ninja", "resolved": "/tool/ninja"},
                "compiler": {
                    "path": "/tool/compiler",
                    "resolved": "/tool/compiler",
                },
            },
            profile=profile,
        )
        graph = (
            "digraph ninja {\n"
            'rankdir="LR"\n'
            f'"0x1" [label="{source}"]\n'
            '"0x2" [label="C_COMPILER__test_Debug", shape=ellipse]\n'
            '"0x3" [label="kernel/micros.elf"]\n'
            '"0x1" -> "0x2" [arrowhead=none]\n'
            '"0x2" -> "0x3"\n'
            "}\n"
        ).encode()

        def metadata(command, **options):
            del options
            if command[-2:] == ("graph", profile.target):
                return graph
            if command[-2:] == ("commands", profile.target):
                return b"/tool/compiler source.c\n"
            if command[-2:] == ("targets", "all"):
                return b"kernel/micros.elf: C_EXECUTABLE_LINKER__test_Debug\n"
            if command[-2:] == ("-t", "deps"):
                return b""
            self.fail(command)

        with mock.patch(
            "tools.validation_build_manifest._run",
            side_effect=metadata,
        ):
            preparation = []
            _, outputs = validation_build_manifest._build_manifest(
                self.root,
                profile,
                tools,
                preparation=preparation,
            )
            self.assertIn(build / "kernel/micros.elf", outputs)
            self.assertEqual(
                [
                    "selected-output-absent",
                    "declared-output-absent",
                ],
                [item.reason_code for item in preparation],
            )
            with self.assertRaises(validation_context.UnsupportedContext):
                validation_build_manifest._build_manifest(
                    self.root,
                    profile,
                    tools,
                )

    def test_custom_output_utility_and_ambiguous_nodes_are_distinct(self):
        build = self.root / "build"
        (build / "CMakeFiles/compiler").mkdir(parents=True)
        (build / "kernel").mkdir()
        (build / "kernel/micros.elf").write_bytes(b"elf")
        (build / "kernel/micros.map").write_bytes(b"map")
        (build / "CMakeCache.txt").write_text("cache")
        (build / "build.ninja").write_text("")
        (build / "CMakeFiles/compiler/CMakeCCompiler.cmake").write_text(
            "compiler"
        )
        source = self.root / "source.c"
        source.write_text("int source;\n")
        profile = mock.Mock(
            directory=build,
            target="test-qemu-smoke",
            target_build=True,
        )
        tools = mock.Mock(
            executables={
                "ninja": {"path": "/tool/ninja", "resolved": "/tool/ninja"},
                "compiler": {
                    "path": "/tool/compiler",
                    "resolved": "/tool/compiler",
                },
            },
            profile=profile,
        )

        def graph(output):
            return (
                "digraph ninja {\n"
                'rankdir="LR"\n'
                f'"0x1" [label="{source}"]\n'
                '"0x2" [label="CUSTOM_COMMAND", shape=ellipse]\n'
                f'"0x3" [label="{output}"]\n'
                '"0x4" [label="CMakeFiles/test-qemu-smoke"]\n'
                '"0x1" -> "0x2" [arrowhead=none]\n'
                '"0x2" -> "0x3"\n'
                '"0x2" -> "0x4"\n'
                "}\n"
            ).encode()

        def run_manifest(output, rules):
            def metadata(command, **options):
                del options
                if command[-2:] == ("graph", profile.target):
                    return graph(output)
                if command[-2:] == ("commands", profile.target):
                    return b"/tool/compiler source.c\n"
                if command[-2:] == ("targets", "all"):
                    return rules
                if command[-2:] == ("-t", "deps"):
                    return b""
                self.fail(command)

            with mock.patch(
                "tools.validation_build_manifest._run",
                side_effect=metadata,
            ):
                preparation = []
                result = validation_build_manifest._build_manifest(
                    self.root,
                    profile,
                    tools,
                    preparation=preparation,
                )
            return result, [item.reason_code for item in preparation]

        output = "generated/custom.bin"
        rules = (
            b"generated/custom.bin: CUSTOM_COMMAND\n"
            b"CMakeFiles/test-qemu-smoke: CUSTOM_COMMAND\n"
        )
        (_, outputs), preparation = run_manifest(output, rules)
        self.assertIn(build / output, outputs)
        self.assertEqual(["selected-output-absent"], preparation)

        with self.assertRaises(validation_context.UnsupportedContext):
            run_manifest("generated/ambiguous.bin", rules)

        generated = build / output
        generated.parent.mkdir()
        generated.mkdir()
        with self.assertRaises(validation_context.UnsupportedContext):
            run_manifest(output, rules)

    def test_readiness_reason_precedence_and_unsupported_override(self):
        profile = {"context": {}, "inputs": {}, "outputs": {}}

        def prepared(root, workflow, *, preparation=None):
            preparation.extend((
                validation_build_manifest.SelectedDependencyStale(
                    "stale"
                ),
                validation_build_manifest.DeclaredOutputAbsent(
                    "declared"
                ),
                validation_build_manifest.SelectedOutputAbsent(
                    "selected"
                ),
            ))
            return profile

        with mock.patch(
            "tools.validation_context.collect",
            side_effect=prepared,
        ):
            readiness = validation_context.collect_readiness(
                self.root,
                "test-unit",
            )
        self.assertEqual("PREPARE", readiness.outcome)
        self.assertEqual("selected-output-absent", readiness.reason_code)
        self.assertEqual(
            ("cmake", "--workflow", "--preset", "test-unit"),
            readiness.preparation_argv,
        )
        self.assertIsNone(readiness.profile)

        def unsupported(root, workflow, *, preparation=None):
            preparation.append(
                validation_build_manifest.SelectedOutputAbsent(
                    "selected"
                )
            )
            raise validation_context.UnsupportedContext("unclosed tool")

        with mock.patch(
            "tools.validation_context.collect",
            side_effect=unsupported,
        ):
            readiness = validation_context.collect_readiness(
                self.root,
                "test-unit",
            )
        self.assertEqual("UNSUPPORTED", readiness.outcome)
        self.assertEqual("profile-unsupported", readiness.reason_code)
        self.assertIsNone(readiness.preparation_argv)

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
            environment = self.environment()
            if value is not None:
                environment["CLANG_NO_DEFAULT_CONFIG"] = value
            else:
                environment.pop("CLANG_NO_DEFAULT_CONFIG")
            with self.subTest(value=value):
                with self.assertRaisesRegex(validation_context.UnsupportedContext, "CLANG_NO_DEFAULT_CONFIG"):
                    validation_context.validate_environment(environment, target=False)
        environment = self.environment()
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

    def test_target_compiler_selection_matches_toolchain_precedence(self):
        explicit = self.root / "explicit-clang"
        explicit.write_bytes(b"explicit compiler")
        explicit.chmod(0o755)
        llvm_root = self.root / "llvm"
        llvm_compiler = llvm_root / "bin/clang"
        llvm_compiler.parent.mkdir(parents=True)
        llvm_compiler.write_bytes(b"root compiler")
        llvm_compiler.chmod(0o755)
        fallback = self.root / "path-clang"
        fallback.write_bytes(b"path compiler")
        fallback.chmod(0o755)

        self.assertEqual(
            explicit,
            validation_context.target_compiler(
                {
                    "MICROS_CLANG": str(explicit),
                    "MICROS_LLVM_ROOT": str(llvm_root),
                },
                system="Darwin",
            ),
        )
        with mock.patch.object(
            validation_context.shutil,
            "which",
            return_value=str(fallback),
        ):
            with self.assertRaisesRegex(
                validation_context.UnsupportedContext,
                "MICROS_CLANG",
            ):
                validation_context.target_compiler(
                    {"MICROS_CLANG": str(self.root / "missing")},
                    system="Darwin",
                )
        self.assertEqual(
            llvm_compiler,
            validation_context.target_compiler(
                {"MICROS_LLVM_ROOT": str(llvm_root)},
                system="Darwin",
            ),
        )
        with mock.patch.object(
            validation_context.shutil,
            "which",
            side_effect=lambda name: str(fallback) if name == "clang" else None,
        ):
            self.assertEqual(
                fallback,
                validation_context.target_compiler({}, system="Linux"),
            )

    def test_test_unit_target_preprocessor_is_fingerprinted(self):
        compiler = self.root / "clang"
        compiler.write_bytes(b"compiler fixture")
        compiler.chmod(0o755)
        installation = self.root / "Cellar/llvm/23.1.3"
        installation.mkdir(parents=True)
        configuration_root = self.root / "etc/clang"
        configuration_root.mkdir(parents=True)
        configuration = {"policy": "automatic-disabled"}
        tools = validation_context.DarwinTools.__new__(
            validation_context.DarwinTools
        )
        tools.environment = {"MICROS_CLANG": str(compiler)}
        tools.roots = set()
        tools.executables = {}
        tools.versions = {}

        tools.environment = {}
        with mock.patch.object(
            validation_context,
            "target_compiler",
            side_effect=AssertionError(
                "fallback discovery ran in a reusable profile"
            ),
        ):
            with self.assertRaisesRegex(
                validation_context.UnsupportedContext,
                "explicit",
            ):
                tools.target_preprocessor()

        tools.environment = {"MICROS_CLANG": str(compiler)}
        with mock.patch.multiple(
            validation_context,
            target_compiler=mock.Mock(return_value=compiler),
            _cellar_root=mock.Mock(return_value=installation),
            compiler_configuration=mock.Mock(
                return_value=(configuration, (configuration_root,))
            ),
            _run=mock.Mock(return_value=b"Homebrew clang version 23.1.3\n"),
        ):
            self.assertEqual(
                configuration,
                tools.target_preprocessor(),
            )

        self.assertEqual(
            {
                "path": str(compiler),
                "resolved": str(compiler.resolve()),
            },
            tools.executables["target-preprocessor"],
        )
        self.assertEqual(
            "LLVMClang",
            tools.versions["target-preprocessor"]["family"],
        )
        self.assertEqual(23, tools.versions["target-preprocessor"]["major"])
        self.assertIn(installation, tools.roots)
        self.assertIn(configuration_root, tools.roots)

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
                    validation_build_manifest.command_tools(
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

    def test_sanitizer_override_inputs_are_ineligible_not_untracked_dependencies(self):
        baseline = self.environment()
        for name, value in (
            ("ASAN_OPTIONS", "include=/external/options"),
            ("UBSAN_OPTIONS", "suppressions=/external/suppressions"),
            ("LSAN_OPTIONS", "include=/external/options"),
            ("ASAN_SYMBOLIZER_PATH", "/external/symbolizer"),
            ("LLVM_SYMBOLIZER_OPTS", "--obj=/external/image"),
        ):
            with self.subTest(name=name):
                with self.assertRaises(validation_context.UnsupportedContext):
                    validation_context.validate_environment({**baseline, name: value}, target=False)

    def test_unstaged_tracked_deletion_has_explicit_dirty_source_identity(self):
        source, responses, query = self.candidate_fixture()
        with mock.patch("tools.validation_context._run", side_effect=query):
            before = validation_context.capture_candidate(self.root)
            source.unlink()
            responses[("diff", "--name-only", "-z")] = b"source.c\0"
            after = validation_context.capture_candidate(self.root)
        self.assertFalse(after.clean)
        self.assertEqual(["source.c"], after.source["missing_paths"])
        self.assertEqual(before.source["tree"], after.source["tree"])
        self.assertNotEqual(before.source["source_sha256"], after.source["source_sha256"])


if __name__ == "__main__":
    unittest.main()
