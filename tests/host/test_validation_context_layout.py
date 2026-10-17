import ast
import hashlib
import json
from pathlib import Path
import unittest

from tests.host.ast_evidence import ast_dump
from tools import validation_build_manifest, validation_context, validation_inputs


ROOT = Path(__file__).resolve().parents[2]
OWNERS = {
    "tools/validation_inputs.py": (
        "_json_digest", "_inside", "_file_digest", "fingerprint_tree",
        "_absent_paths", "_run", "_file_identity",
    ),
    "tools/validation_build_manifest.py": (
        "_regular_output_state", "graph_nodes", "command_tools",
        "native_outputs", "native_runtime_executables", "_generator_inputs",
        "_dependency_inputs", "_selected_graph_files", "_target_outputs",
        "_build_manifest",
    ),
}


class ValidationContextLayoutTests(unittest.TestCase):
    def test_each_moved_algorithm_has_one_explicit_owner_and_frozen_body(self):
        observed = {}
        facade = ast.parse((ROOT / "tools/validation_context.py").read_text())
        remaining = {node.name for node in facade.body if isinstance(node, ast.FunctionDef)}
        for path, names in OWNERS.items():
            tree = ast.parse((ROOT / path).read_text())
            definitions = {
                node.name: node for node in tree.body if isinstance(node, ast.FunctionDef)
            }
            self.assertEqual(set(names), set(definitions), path)
            self.assertFalse(set(names) & remaining, path)
            for name in names:
                self.assertNotIn(name, observed)
                observed[name] = ast_dump(definitions[name])
        self.assertEqual(
            "00c8141862492e630b8509eaeb639619336696367880617b5b5b2a9e58ba5a4c",
            hashlib.sha256(
                json.dumps(observed, sort_keys=True, separators=(",", ":")).encode()
            ).hexdigest(),
            "shared input owners must match Accepted ADR-0074",
        )

    def test_private_dependency_direction_and_error_identity_are_acyclic(self):
        self.assertIs(validation_inputs.UnsupportedContext, validation_context.UnsupportedContext)
        self.assertIs(validation_inputs.UnsupportedContext, validation_build_manifest.UnsupportedContext)
        for path in OWNERS:
            tree = ast.parse((ROOT / path).read_text())
            for node in ast.walk(tree):
                if isinstance(node, ast.ImportFrom):
                    self.assertNotIn(
                        node.module,
                        ("tools.validation_context", "tools.validation_execution", "tools.validation_plan"),
                    )
        inputs = ast.parse((ROOT / "tools/validation_inputs.py").read_text())
        self.assertFalse(any(
            isinstance(node, ast.ImportFrom) and node.module == "tools.validation_build_manifest"
            for node in ast.walk(inputs)
        ))

    def test_profile_facade_binds_each_required_shared_input_algorithm(self):
        for name in OWNERS["tools/validation_inputs.py"]:
            with self.subTest(name=name):
                self.assertIs(
                    getattr(validation_inputs, name),
                    getattr(validation_context, name, None),
                )

    def test_task_level_function_sizes_and_single_profile_facade_stay_bounded(self):
        for path in (*OWNERS, "tools/validation_context.py"):
            tree = ast.parse((ROOT / path).read_text())
            self.assertLessEqual(
                sum(isinstance(node, ast.FunctionDef) for node in tree.body), 25, path
            )
            for node in ast.walk(tree):
                if isinstance(node, ast.FunctionDef):
                    self.assertLessEqual(node.end_lineno - node.lineno + 1, 120, (path, node.name))


if __name__ == "__main__":
    unittest.main()
