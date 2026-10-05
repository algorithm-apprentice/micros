import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools import check_docs


class MarkdownChecksTest(unittest.TestCase):
    def test_reports_broken_relative_link_and_non_ascii(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            document = root / "README.md"
            document.write_text(
                "[missing](missing.md)\nnon-ascii: \u00e9\n",
                encoding="utf-8",
            )
            errors = []

            check_docs.check_markdown_file(root, document, errors)

            self.assertTrue(any("non-ASCII" in error for error in errors))
            self.assertTrue(any("broken relative link" in error for error in errors))

    def test_accepts_existing_relative_link(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "target.md"
            target.write_text("# Target\n", encoding="ascii")
            document = root / "README.md"
            document.write_text("[target](target.md)\n", encoding="ascii")
            errors = []

            check_docs.check_markdown_file(root, document, errors)

            self.assertEqual([], errors)

    def test_rejects_link_that_resolves_outside_repository(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            root = parent / "repo"
            root.mkdir()
            (parent / "outside.md").write_text("# Outside\n", encoding="ascii")
            document = root / "README.md"
            document.write_text("[outside](../outside.md)\n", encoding="ascii")
            errors = []

            check_docs.check_markdown_file(root, document, errors)

            self.assertTrue(
                any("outside repository" in error for error in errors)
            )


class InstructionChecksTest(unittest.TestCase):
    def test_requires_apply_to_frontmatter(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "kernel.instructions.md"
            path.write_text("# Missing frontmatter\n", encoding="ascii")
            errors = []

            check_docs.check_instruction_file(path, errors)

            self.assertTrue(any("applyTo frontmatter" in error for error in errors))


class GraphChecksTest(unittest.TestCase):
    def test_topological_order_rejects_cycle(self):
        with self.assertRaises(ValueError):
            check_docs.topological_order({"A", "B"}, [("A", "B"), ("B", "A")])

    def test_reachable_follows_directed_edges(self):
        graph = {"A": ["B"], "B": ["C"], "C": []}

        self.assertEqual({"A", "B", "C"}, check_docs.reachable("A", graph))

    def test_parser_accepts_compact_edge_syntax(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "graph.md"
            path.write_text(
                "```mermaid\n"
                "flowchart TD\n"
                "    A[Alpha]\n"
                "    B[Beta]\n"
                "    A-->B\n"
                "```\n",
                encoding="ascii",
            )

            nodes, edges = check_docs.parse_mermaid_graph(path, "TD")

            self.assertEqual({"A", "B"}, nodes)
            self.assertEqual([("A", "B")], edges)

    def test_graph_check_rejects_incomplete_development_baseline(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            architecture = root / "docs" / "architecture"
            research = root / "docs" / "research"
            architecture.mkdir(parents=True)
            research.mkdir(parents=True)
            (architecture / "development-dag.md").write_text(
                "```mermaid\nflowchart TD\n    A[Only]\n```\n",
                encoding="ascii",
            )
            (research / "minix-dependency-analysis.md").write_text(
                "```mermaid\nflowchart LR\n    K[Kernel]\n```\n",
                encoding="ascii",
            )
            errors = []

            check_docs.check_graphs(root, errors)

            self.assertTrue(
                any("development graph baseline" in error for error in errors)
            )

    def test_graph_check_rejects_undeclared_runtime_edge_endpoint(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            architecture = root / "docs" / "architecture"
            research = root / "docs" / "research"
            architecture.mkdir(parents=True)
            research.mkdir(parents=True)
            (architecture / "development-dag.md").write_text(
                (ROOT / "docs" / "architecture" / "development-dag.md").read_text(
                    encoding="ascii"
                ),
                encoding="ascii",
            )
            runtime_text = (
                ROOT / "docs" / "research" / "minix-dependency-analysis.md"
            ).read_text(encoding="ascii")
            runtime_text = runtime_text.replace(
                "    INIT[Init]\n",
                "    INIT[Init]\n    X --> INIT\n",
                1,
            )
            (research / "minix-dependency-analysis.md").write_text(
                runtime_text,
                encoding="ascii",
            )
            errors = []

            check_docs.check_graphs(root, errors)

            self.assertTrue(
                any("edge references unknown node: X -> INIT" in error for error in errors)
            )


if __name__ == "__main__":
    unittest.main()
