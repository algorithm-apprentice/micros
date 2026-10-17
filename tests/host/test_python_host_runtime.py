import ast
import os
from pathlib import Path
import re
import sys
import unittest
from unittest import mock

from tests.host.ast_evidence import ast_dump, ast_key
from tools import python_host_runtime


ROOT = Path(__file__).resolve().parents[2]


class StableAstEvidenceTests(unittest.TestCase):
    def test_dump_preserves_complete_reviewed_fields_without_315_defaults(self):
        self.assertEqual(
            "Module(body=[Import(names=[alias(name='os')])], type_ignores=[])",
            ast_dump(ast.parse("import os\n")),
        )
        function = ast.parse("def example():\n    pass\n").body[0]
        self.assertIn("args=arguments(posonlyargs=[]", ast_dump(function))
        self.assertIn("decorator_list=[]", ast_dump(function))
        self.assertNotIn("type_params", ast_dump(function))

    def test_new_ast_semantics_fail_instead_of_changing_evidence(self):
        function = ast.parse("def example():\n    pass\n").body[0]
        function.type_params = [ast.Name(id="T")]
        with self.assertRaisesRegex(AssertionError, "type parameters"):
            ast_dump(function)
        imported = ast.parse("import os\n").body[0]
        imported.is_lazy = 1
        with self.assertRaisesRegex(AssertionError, "lazy import"):
            ast_key(imported)

    def test_shared_module_is_the_only_ast_serializer_owner(self):
        direct_dumps = []
        key_owners = []
        for path in sorted((ROOT / "tests/host").glob("*.py")):
            if path.name == "ast_evidence.py":
                continue
            tree = ast.parse(path.read_text(encoding="utf-8"))
            for node in ast.walk(tree):
                if (
                    isinstance(node, ast.Call)
                    and isinstance(node.func, ast.Attribute)
                    and isinstance(node.func.value, ast.Name)
                    and node.func.value.id == "ast"
                    and node.func.attr == "dump"
                ):
                    direct_dumps.append(str(path.relative_to(ROOT)))
                if (
                    isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
                    and node.name == "ast_key"
                ):
                    key_owners.append(str(path.relative_to(ROOT)))
        self.assertEqual([], direct_dumps)
        self.assertEqual([], key_owners)


class PythonHostRuntimeTests(unittest.TestCase):
    def environment(self):
        return {
            "OPENSSL_CONF": "/dev/null",
            "SSL_CERT_FILE": "/dev/null",
            "SSL_CERT_DIR": "/dev/null",
        }

    def metadata(self):
        prefix = Path("/runtime")
        stdlib = prefix / "lib/python3.15"
        return {
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
            "executable": str(prefix / "bin/python3.15"),
            "base_executable": str(prefix / "bin/python3.15"),
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

    def test_exact_runtime_metadata_accepts_only_supported_python(self):
        metadata = self.metadata()
        python_host_runtime.validate_runtime_metadata(
            metadata,
            metadata["executable"],
            repository_root=ROOT,
            system="Linux",
        )
        for key, value in (
            ("implementation", "pypy"),
            ("version", [3, 16, 0]),
            ("releaselevel", "candidate"),
            ("cache_tag", "cpython-316"),
            ("gil_enabled", False),
            ("py_gil_disabled", 1),
            ("prefix", "/virtual"),
            ("cafile", "/external/cert.pem"),
            ("certificate_count", 1),
        ):
            changed = {**metadata, key: value}
            with self.subTest(key=key), self.assertRaises(
                python_host_runtime.UnsupportedPythonRuntime
            ):
                python_host_runtime.validate_runtime_metadata(
                    changed,
                    metadata["executable"],
                    repository_root=ROOT,
                    system="Linux",
                )

    def test_current_process_satisfies_the_exact_runtime_contract(self):
        with mock.patch.dict(os.environ, self.environment(), clear=True):
            metadata = python_host_runtime.require_current_runtime()
        self.assertEqual([3, 15], metadata["version"][:2])
        self.assertGreaterEqual(metadata["version"], [3, 15, 0])
        self.assertEqual("cpython-315", metadata["cache_tag"])
        self.assertEqual(0, metadata["certificate_count"])

    def test_documented_reusable_commands_export_runtime_before_use(self):
        building = (ROOT / "docs/development/building.md").read_text(
            encoding="utf-8"
        )
        blocks = re.findall(r"```bash\n(.*?)\n```", building, flags=re.DOTALL)
        exports = (
            "export MICROS_PYTHON=/opt/homebrew/opt/python@3.15/bin/python3.15",
            "export OPENSSL_CONF=/dev/null",
            "export SSL_CERT_FILE=/dev/null",
            "export SSL_CERT_DIR=/dev/null",
            "export CLANG_NO_DEFAULT_CONFIG=1",
            "export MICROS_CLANG=/opt/homebrew/opt/llvm/bin/clang",
            "export MICROS_LD_LLD=/opt/homebrew/opt/lld/bin/ld.lld",
        )
        for marker in (
            "--execute --producer",
            "--execute --reuse",
            "--record-workflow test-unit",
        ):
            block = next((item for item in blocks if marker in item), None)
            with self.subTest(marker=marker):
                self.assertIsNotNone(block)
                command_index = block.index('"${MICROS_PYTHON}"')
                for export in exports:
                    self.assertIn(export, block)
                    self.assertLess(block.index(export), command_index)

    def test_environment_and_child_execution_fail_closed(self):
        python_host_runtime.validate_environment(self.environment())
        for name, value in (
            ("PYTHONPATH", "/external"),
            ("PYTHONEXECUTABLE", "/tmp/fake"),
            ("__PYVENV_LAUNCHER__", "/tmp/fake"),
            ("OPENSSL_MODULES", "/external"),
            ("SSL_CERT_FILE", "/external"),
            ("RANDFILE", "/tmp/random"),
            ("CTLOG_FILE", "/tmp/ctlog"),
        ):
            environment = {**self.environment(), name: value}
            with self.subTest(name=name), self.assertRaises(
                python_host_runtime.UnsupportedPythonRuntime
            ):
                python_host_runtime.validate_environment(environment)
        self.assertEqual(
            (sys.executable, "-E", "-S", "-B", "-c", "pass"),
            python_host_runtime.python_command(
                sys.executable,
                "-c",
                "pass",
                environment=self.environment(),
            ),
        )

    def test_darwin_framework_identity_is_platform_scoped(self):
        metadata = self.metadata()
        framework = Path("/runtime/Python.framework/Versions/3.15")
        darwin = {
            **metadata,
            "executable": str(framework.parents[3] / "bin/python3.15"),
            "base_executable": str(framework / "bin/python3.15"),
            "prefix": str(framework),
            "base_prefix": str(framework),
            "stdlib": str(framework / "lib/python3.15"),
            "paths": [
                str(ROOT),
                str(framework / "lib/python315.zip"),
                str(framework / "lib/python3.15"),
                str(framework / "lib/python3.15/lib-dynload"),
            ],
        }
        python_host_runtime.validate_runtime_metadata(
            darwin,
            darwin["executable"],
            repository_root=ROOT,
            system="Darwin",
        )
        with self.assertRaises(python_host_runtime.UnsupportedPythonRuntime):
            python_host_runtime.validate_runtime_metadata(
                {**darwin, "base_executable": "/tmp/fake"},
                darwin["executable"],
                repository_root=ROOT,
                system="Darwin",
            )

    def test_cmake_and_child_sites_use_the_exact_prefix(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        markers = (
            "DEFINED CACHE{MICROS_PYTHON}",
            "ENV{MICROS_PYTHON}",
            "ENV{MICROS_PYTHON_ROOT}",
            "--prefix python@3.15",
            "NAMES python3.15",
        )
        positions = [cmake.index(marker) for marker in markers]
        self.assertEqual(sorted(positions), positions)
        commands = re.findall(
            r'COMMAND\s+"\$\{MICROS_PYTHON\}"\s+([^\s]+)',
            cmake,
        )
        self.assertGreater(len(commands), 50)
        self.assertEqual({"${MICROS_PYTHON_ARGS}"}, set(commands))
        for relative in (
            "tests/host/user_elf_cli_cases.py",
            "tests/host/test_check_application_elf.py",
            "tests/host/qemu_smoke_execution_cases.py",
            "tests/host/test_qemu_runner_source_layout.py",
            "tests/host/test_validation_context.py",
        ):
            text = (ROOT / relative).read_text(encoding="utf-8")
            self.assertNotIn('environment["PYTHONPATH"]', text, relative)
            self.assertNotIn('[sys.executable, "-B"', text, relative)

    def test_python_process_construction_has_one_reviewed_owner_inventory(self):
        expected = {
            "tools/validation_context.py",
            "tools/validation_execution.py",
            "tools/validation_plan.py",
            "tests/host/qemu_smoke_execution_cases.py",
            "tests/host/test_check_application_elf.py",
            "tests/host/test_qemu_runner_source_layout.py",
            "tests/host/user_elf_cli_cases.py",
        }
        observed = set()
        direct_commands = []
        paths = (
            sorted((ROOT / "tools").glob("*.py"))
            + sorted((ROOT / "tests/host").glob("*.py"))
        )
        for path in paths:
            relative = str(path.relative_to(ROOT))
            tree = ast.parse(path.read_text(encoding="utf-8"))
            for node in ast.walk(tree):
                if (
                    isinstance(node, ast.ImportFrom)
                    and node.module == "tools.python_host_runtime"
                ):
                    observed.add(relative)
                if (
                    relative != "tests/host/test_python_host_runtime.py"
                    and isinstance(node, (ast.List, ast.Tuple))
                    and node.elts
                    and isinstance(node.elts[0], ast.Attribute)
                    and isinstance(node.elts[0].value, ast.Name)
                    and node.elts[0].value.id == "sys"
                    and node.elts[0].attr == "executable"
                ):
                    direct_commands.append(relative)
        self.assertEqual(expected, observed)
        self.assertEqual([], direct_commands)

    def test_executable_tools_have_the_python_315_shebang(self):
        executable_tools = [
            path
            for path in sorted((ROOT / "tools").glob("*.py"))
            if path.read_text(encoding="utf-8").startswith("#!")
        ]
        self.assertGreater(len(executable_tools), 20)
        for path in executable_tools:
            with self.subTest(path=path.name):
                self.assertEqual(
                    "#!/usr/bin/env -S python3.15 -E -S -B",
                    path.read_text(encoding="utf-8").splitlines()[0],
                )
