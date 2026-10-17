import ast
from pathlib import Path
import unittest

from tests.host.ast_evidence import ast_dump, ast_key


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
