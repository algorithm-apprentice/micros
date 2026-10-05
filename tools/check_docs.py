#!/usr/bin/env python3

import argparse
import re
from collections import Counter, defaultdict, deque
from pathlib import Path


ADR_STATUS_PATTERN = re.compile(
    r"^\|\s*\[(\d{4})\]\(([^)]+)\)\s*\|\s*[^|]+\|\s*"
    r"(Proposed|Accepted|Rejected|Superseded)\s*\|$",
    re.MULTILINE,
)
INSTRUCTION_FRONTMATTER_PATTERN = re.compile(
    r'^---\napplyTo: "[^"]+"\n(?:excludeAgent: "(?:code-review|cloud-agent)"\n)?---\n'
)
MARKDOWN_LINK_PATTERN = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
MERMAID_EDGE_PATTERN = re.compile(r"\s*([A-Z]+)\s*-->\s*([A-Z]+)\s*")
MERMAID_NODE_PATTERN = re.compile(r"\s*([A-Z]+)\s*\[[^\n]+\]\s*")
VALID_ADR_STATUSES = {"Proposed", "Accepted", "Rejected", "Superseded"}
EXPECTED_DEVELOPMENT_NODES = set("ABCDEFGHIJKLMNOPQRSTUVW")
EXPECTED_DEVELOPMENT_EDGES = {
    ("A", "B"),
    ("B", "C"),
    ("C", "D"),
    ("C", "E"),
    ("D", "F"),
    ("E", "F"),
    ("F", "G"),
    ("G", "H"),
    ("H", "I"),
    ("I", "J"),
    ("J", "K"),
    ("K", "L"),
    ("K", "P"),
    ("K", "T"),
    ("L", "M"),
    ("L", "P"),
    ("L", "T"),
    ("M", "N"),
    ("M", "Q"),
    ("N", "O"),
    ("O", "P"),
    ("O", "T"),
    ("O", "U"),
    ("P", "Q"),
    ("Q", "R"),
    ("Q", "V"),
    ("R", "S"),
    ("S", "T"),
    ("U", "V"),
    ("V", "W"),
}
EXPECTED_RUNTIME_SCC = {
    "K",
    "VM",
    "RS",
    "PM",
    "SCH",
    "VFS",
    "DS",
    "PFS",
    "MFS",
    "MEM",
    "TTY",
}
EXPECTED_RUNTIME_NODES = EXPECTED_RUNTIME_SCC | {"INIT"}


def display_path(root, path):
    try:
        return path.relative_to(root)
    except ValueError:
        return path


def check_markdown_file(root, path, errors):
    data = path.read_bytes()
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as error:
        errors.append(
            f"{display_path(root, path)}: non-ASCII byte at offset {error.start}"
        )
        text = data.decode("utf-8", errors="replace")

    lines = text.splitlines()
    for line_number, line in enumerate(lines, 1):
        if line.rstrip() != line:
            errors.append(
                f"{display_path(root, path)}:{line_number}: trailing whitespace"
            )
        if "\t" in line:
            errors.append(f"{display_path(root, path)}:{line_number}: tab character")

    if sum(line.startswith("```") for line in lines) % 2:
        errors.append(f"{display_path(root, path)}: unbalanced fenced code block")

    for raw_target in MARKDOWN_LINK_PATTERN.findall(text):
        target = raw_target.strip()
        if (
            "://" in target
            or target.startswith("#")
            or target.startswith("mailto:")
        ):
            continue
        target_path = target.split("#", 1)[0]
        if not target_path:
            continue
        if Path(target_path).is_absolute():
            errors.append(
                f"{display_path(root, path)}: link outside repository {raw_target}"
            )
            continue
        root_resolved = root.resolve()
        resolved = (path.parent / target_path).resolve()
        try:
            resolved.relative_to(root_resolved)
        except ValueError:
            errors.append(
                f"{display_path(root, path)}: link outside repository {raw_target}"
            )
            continue
        if not resolved.exists():
            errors.append(
                f"{display_path(root, path)}: broken relative link {raw_target}"
            )


def check_instruction_file(path, errors):
    text = path.read_text(encoding="ascii")
    if not INSTRUCTION_FRONTMATTER_PATTERN.match(text):
        errors.append(f"{path}: invalid applyTo frontmatter")


def parse_mermaid_graph(path, flow):
    text = path.read_text(encoding="ascii")
    match = re.search(
        rf"```mermaid\nflowchart {re.escape(flow)}\n(.*?)```",
        text,
        re.DOTALL,
    )
    if match is None:
        raise ValueError(f"{path}: missing Mermaid flowchart {flow}")

    block = match.group(1)
    nodes = set()
    edges = []
    for line_number, line in enumerate(block.splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("%%"):
            continue
        node_match = MERMAID_NODE_PATTERN.fullmatch(line)
        if node_match is not None:
            node = node_match.group(1)
            if node in nodes:
                raise ValueError(f"{path}: duplicate graph node {node}")
            nodes.add(node)
            continue
        edge_match = MERMAID_EDGE_PATTERN.fullmatch(line)
        if edge_match is not None:
            edges.append(edge_match.groups())
            continue
        raise ValueError(
            f"{path}: unparsed Mermaid graph line {line_number}: {stripped}"
        )

    if len(edges) != len(set(edges)):
        raise ValueError(f"{path}: duplicate graph edge")
    return nodes, edges


def validate_edge_endpoints(nodes, edges):
    for source, target in edges:
        if source not in nodes or target not in nodes:
            raise ValueError(f"edge references unknown node: {source} -> {target}")


def topological_order(nodes, edges):
    validate_edge_endpoints(nodes, edges)
    outgoing = defaultdict(list)
    indegree = {node: 0 for node in nodes}
    for source, target in edges:
        outgoing[source].append(target)
        indegree[target] += 1

    ready = deque(sorted(node for node, degree in indegree.items() if degree == 0))
    order = []
    while ready:
        node = ready.popleft()
        order.append(node)
        for target in sorted(outgoing[node]):
            indegree[target] -= 1
            if indegree[target] == 0:
                ready.append(target)

    if len(order) != len(nodes):
        raise ValueError("graph contains a cycle")
    return order


def reachable(start, graph):
    seen = {start}
    stack = [start]
    while stack:
        node = stack.pop()
        for target in graph.get(node, []):
            if target not in seen:
                seen.add(target)
                stack.append(target)
    return seen


def collect_markdown_files(root):
    files = {
        root / "README.md",
        root / "AGENTS.md",
        root / "CONTRIBUTING.md",
    }
    files.update((root / "docs").rglob("*.md"))
    files.update((root / ".github").rglob("*.md"))
    return sorted(path for path in files if path.exists())


def check_adrs(root, errors):
    adr_directory = root / "docs" / "adr"
    index_path = adr_directory / "README.md"
    index_text = index_path.read_text(encoding="ascii")
    index_entries = {
        adr_id: (target, status)
        for adr_id, target, status in ADR_STATUS_PATTERN.findall(index_text)
    }
    adr_files = sorted(adr_directory.glob("[0-9][0-9][0-9][0-9]-*.md"))
    statuses = Counter()

    for path in adr_files:
        adr_id = path.name[:4]
        text = path.read_text(encoding="ascii")
        match = re.search(
            r"^- Status: (Proposed|Accepted|Rejected|Superseded)$",
            text,
            re.MULTILINE,
        )
        if match is None:
            errors.append(f"{display_path(root, path)}: missing valid ADR status")
            continue

        status = match.group(1)
        statuses[status] += 1
        entry = index_entries.get(adr_id)
        if entry is None:
            errors.append(f"{display_path(root, path)}: missing ADR index entry")
            continue
        target, indexed_status = entry
        if (adr_directory / target).resolve() != path.resolve():
            errors.append(
                f"{display_path(root, path)}: ADR index points to {target}"
            )
        if indexed_status != status:
            errors.append(
                f"{display_path(root, path)}: index status {indexed_status} "
                f"does not match {status}"
            )

    file_ids = {path.name[:4] for path in adr_files}
    for adr_id in sorted(set(index_entries) - file_ids):
        errors.append(f"docs/adr/README.md: index entry {adr_id} has no ADR file")

    invalid_statuses = set(statuses) - VALID_ADR_STATUSES
    if invalid_statuses:
        errors.append(f"invalid ADR statuses: {sorted(invalid_statuses)}")
    return adr_files, statuses


def check_graphs(root, errors):
    development_path = root / "docs" / "architecture" / "development-dag.md"
    runtime_path = root / "docs" / "research" / "minix-dependency-analysis.md"

    development_nodes = set()
    development_edges = []
    runtime_nodes = set()
    runtime_edges = []

    try:
        development_nodes, development_edges = parse_mermaid_graph(
            development_path,
            "TD",
        )
        if development_nodes != EXPECTED_DEVELOPMENT_NODES:
            errors.append(
                "development graph baseline node mismatch: expected "
                f"{sorted(EXPECTED_DEVELOPMENT_NODES)}, "
                f"got {sorted(development_nodes)}"
            )
        if set(development_edges) != EXPECTED_DEVELOPMENT_EDGES:
            errors.append(
                "development graph baseline edge mismatch: expected "
                f"{len(EXPECTED_DEVELOPMENT_EDGES)} edges, "
                f"got {len(set(development_edges))}"
            )
        topological_order(development_nodes, development_edges)
    except ValueError as error:
        errors.append(str(error))

    try:
        runtime_nodes, runtime_edges = parse_mermaid_graph(runtime_path, "LR")
        validate_edge_endpoints(runtime_nodes, runtime_edges)
        if runtime_nodes != EXPECTED_RUNTIME_NODES:
            errors.append(
                "runtime graph baseline node mismatch: expected "
                f"{sorted(EXPECTED_RUNTIME_NODES)}, got {sorted(runtime_nodes)}"
            )
        outgoing = defaultdict(list)
        incoming = defaultdict(list)
        for source, target in runtime_edges:
            outgoing[source].append(target)
            incoming[target].append(source)
        actual_scc = reachable("K", outgoing) & reachable("K", incoming)
        if actual_scc != EXPECTED_RUNTIME_SCC:
            errors.append(
                "runtime SCC mismatch: expected "
                f"{sorted(EXPECTED_RUNTIME_SCC)}, got {sorted(actual_scc)}"
            )
    except ValueError as error:
        errors.append(str(error))

    return (
        development_nodes,
        development_edges,
        runtime_nodes,
        runtime_edges,
    )


def validate_repository(root):
    errors = []
    markdown_files = collect_markdown_files(root)
    for path in markdown_files:
        check_markdown_file(root, path, errors)

    instruction_files = sorted(
        (root / ".github" / "instructions").glob("*.instructions.md")
    )
    for path in instruction_files:
        check_instruction_file(path, errors)

    adr_files, adr_statuses = check_adrs(root, errors)
    graph_summary = check_graphs(root, errors)
    return {
        "errors": errors,
        "markdown_files": markdown_files,
        "instruction_files": instruction_files,
        "adr_files": adr_files,
        "adr_statuses": adr_statuses,
        "graph_summary": graph_summary,
    }


def main():
    parser = argparse.ArgumentParser(description="Validate micros documentation")
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="repository root",
    )
    arguments = parser.parse_args()
    root = arguments.root.resolve()

    result = validate_repository(root)
    if result["errors"]:
        for error in result["errors"]:
            print(error)
        return 1

    development_nodes, development_edges, runtime_nodes, runtime_edges = result[
        "graph_summary"
    ]
    status_summary = ", ".join(
        f"{status}={count}"
        for status, count in sorted(result["adr_statuses"].items())
    )
    print(
        f"checked {len(result['markdown_files'])} Markdown/instruction files"
    )
    print(f"validated {len(result['instruction_files'])} path instructions")
    print(f"validated {len(result['adr_files'])} ADRs ({status_summary})")
    print(
        "development DAG: "
        f"{len(development_nodes)} nodes, {len(development_edges)} edges, acyclic"
    )
    print(
        "runtime graph: "
        f"{len(runtime_nodes)} nodes, {len(runtime_edges)} edges; "
        "11-node SCC verified"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
