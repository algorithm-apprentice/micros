import ast
from pathlib import Path
import unittest

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
    def test_shared_input_algorithms_have_one_current_owner(self):
        observed = set()
        facade = ast.parse((ROOT / "tools/validation_context.py").read_text())
        remaining = {node.name for node in facade.body if isinstance(node, ast.FunctionDef)}
        for path, names in OWNERS.items():
            tree = ast.parse((ROOT / path).read_text())
            definitions = {
                node.name: node for node in tree.body if isinstance(node, ast.FunctionDef)
            }
            self.assertLessEqual(set(names), set(definitions), path)
            self.assertFalse(set(names) & remaining, path)
            for name in names:
                self.assertNotIn(name, observed)
                observed.add(name)

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

if __name__ == "__main__":
    unittest.main()
