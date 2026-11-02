"""Closed literal scheduler declarations and payload binding for ADR-0091."""

import ast
from collections import Counter
import json
import re

from tests.host.evidence_authority_source import (
    kernel_sources,
    literal_value as _literal,
    regular_source as _regular,
    single_assignment as _assignment,
)


OWNER = "tests/host/test_scheduler_source_layout.py"
FAMILY = "scheduler-packed-transport"
TABLES = ("FROZEN_FUNCTIONS", "FROZEN_FACTS")
GROUP_COUNTS = {
    "kernel/scheduler_core.c": 60,
    "kernel/scheduler.c": 34,
    "kernel/scheduler_test.c": 28,
    "kernel/scheduler_invalid_test.c": 11,
    "tests/host/scheduler_test.c": 13,
    "tests/host/scheduler_model_test.c": 17,
}
_IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_FUNCTION_ROW = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\s+[0-9]+\s+[0-9a-f]{64}")
_TOKEN = re.compile(r"[CPWE][0-9a-f]{24}")


def _module_nodes(tree):
    pending = list(tree.body)
    while pending:
        node = pending.pop()
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef, ast.Lambda)):
            continue
        yield node
        pending.extend(ast.iter_child_nodes(node))


def _reserved_binding(node):
    if isinstance(node, ast.Name):
        return isinstance(node.ctx, (ast.Store, ast.Del)) and node.id in TABLES
    if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
        return node.name in TABLES
    if isinstance(node, ast.alias):
        return (node.asname or node.name) in TABLES
    if isinstance(node, ast.arg):
        return node.arg in TABLES
    return False


def _packed_value(node):
    for child in ast.walk(node):
        if not isinstance(child, ast.Constant) or type(child.value) is not str:
            continue
        if any(_FUNCTION_ROW.fullmatch(line.strip()) for line in child.value.splitlines()):
            return True
        if any(_TOKEN.fullmatch(token) for token in child.value.split()):
            return True
    return False


def _packed_assignments(tree):
    return tuple(
        node for node in _module_nodes(tree)
        if isinstance(node, (ast.Assign, ast.AnnAssign, ast.AugAssign))
        and node.value is not None and _packed_value(node.value)
    )


def packed_candidate(tree):
    return any(_reserved_binding(node) for node in ast.walk(tree)) or bool(_packed_assignments(tree))


def checked_declarations(tree, relative):
    if relative != OWNER:
        if packed_candidate(tree):
            raise ValueError(f"undeclared packed authority owner: {relative}")
        return None
    statements = tuple(_assignment(tree, name) for name in TABLES)
    targets = {id(statement.targets[0]) for statement in statements}
    if any(_reserved_binding(node) and id(node) not in targets for node in ast.walk(tree)):
        raise ValueError("packed authority has an unadmitted binding")
    if any(node not in statements for node in _packed_assignments(tree)):
        raise ValueError("undeclared packed authority table")
    for statement in tree.body:
        if statement in statements or isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            continue
        if any(isinstance(node, ast.Name) and node.id in TABLES for node in ast.walk(statement)):
            raise ValueError("packed authority has a computed replacement or alias")
    values = {name: _literal(statement.value) for name, statement in zip(TABLES, statements)}
    if any(type(value) is not dict for value in values.values()):
        raise ValueError("packed authority tables must be literal dictionaries")
    return values


def _strings(value, *, empty=False):
    return type(value) is tuple and (empty or bool(value)) and all(type(item) is str for item in value)


def _source_families(tree):
    sources, headers, owners, long_roots, short_roots = (
        _literal(_assignment(tree, name).value)
        for name in (
            "SOURCE_SETS", "PRIVATE_HEADERS", "ORIGINAL_FUNCTIONS",
            "INITIAL_FUNCTION_MIGRATION", "SHORT_PHASE_ROOTS",
        )
    )
    if (
        type(sources) is not dict or len(sources) != 6
        or type(owners) is not dict or set(owners) != set(GROUP_COUNTS)
        or not _strings(headers) or len(headers) != 6
        or any(type(name) is not str or not _strings(paths) for name, paths in sources.items())
        or {paths[0] for paths in sources.values()} != set(GROUP_COUNTS)
    ):
        raise ValueError("packed authority has incomplete source families")
    functions = {}
    families = {}
    for index, paths in enumerate(sources.values()):
        original = paths[0]
        declared = owners[original]
        if type(declared) is not dict or set(declared) != set(paths) or len(set(paths)) != len(paths):
            raise ValueError("packed authority source and owner membership disagree")
        selected = {}
        for path, names in declared.items():
            if not _strings(names, empty=True):
                raise ValueError("packed authority has nonliteral original functions")
            for name in names:
                if not _IDENTIFIER.fullmatch(name) or name in selected:
                    raise ValueError("packed authority has invalid or ambiguous function ownership")
                selected[name] = path
        if len(selected) != GROUP_COUNTS[original]:
            raise ValueError("packed authority has incomplete original functions")
        functions[original] = selected
        families[original] = (*paths, headers[index])
    roots = []
    for selected, count in ((long_roots, 12), (short_roots, 2)):
        if type(selected) is not tuple or len(selected) != count:
            raise ValueError("packed authority has incomplete phase roots")
        for pair in selected:
            if not _strings(pair) or len(pair) != 2 or pair[1] not in functions.get(pair[0], {}):
                raise ValueError("packed authority has an unknown phase root")
            roots.append(pair)
    if len(set(roots)) != len(roots):
        raise ValueError("packed authority has duplicate phase roots")
    return functions, families, set(roots)


def _payload_bindings(root, families, directory):
    _regular(root, OWNER)
    logical_paths = tuple(path for paths in families.values() for path in paths)
    return kernel_sources(root, logical_paths, directory)


def _record(kind, table, original, function, payloads, value):
    scope = f"{table}/{original}/{function}"
    return (
        kind, scope, tuple(sorted({OWNER, *payloads})),
        json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True),
    )


def _function_records(table, functions, paths):
    if set(table) != set(GROUP_COUNTS):
        raise ValueError("packed function source membership changed")
    records = []
    for original, text in table.items():
        if type(text) is not str:
            raise ValueError("packed function records require literal strings")
        observed = set()
        for line in text.splitlines():
            if not line.strip():
                continue
            if not _FUNCTION_ROW.fullmatch(line.strip()):
                raise ValueError("malformed packed function record")
            name, lines, sha256 = line.split()
            count = int(lines)
            if not 0 < count <= 0xffffffff or name in observed or name not in functions[original]:
                raise ValueError("invalid or duplicate packed function record")
            observed.add(name)
            records.append(_record(
                "function-body", "FROZEN_FUNCTIONS", original, name,
                (paths[functions[original][name]],), {"original_lines": count, "sha256": sha256},
            ))
        if observed != set(functions[original]):
            raise ValueError("incomplete packed function inventory")
    return records


def _semantic_records(table, families, roots, paths):
    if set(table) != roots:
        raise ValueError("packed semantic phase-root membership changed")
    records = []
    for (original, function), text in table.items():
        if type(text) is not str:
            raise ValueError("packed semantic records require literal strings")
        tokens = text.split()
        if not tokens or any(not _TOKEN.fullmatch(token) for token in tokens):
            raise ValueError("malformed packed semantic token")
        records.append(_record(
            "data-table", "FROZEN_FACTS", original, function,
            (paths[path] for path in families[original]), sorted(Counter(tokens).items()),
        ))
    return records


def packed_records(root, tree, tables, directory=None):
    functions, families, roots = _source_families(tree)
    paths = _payload_bindings(root, families, directory)
    return tuple((
        *_function_records(tables["FROZEN_FUNCTIONS"], functions, paths),
        *_semantic_records(tables["FROZEN_FACTS"], families, roots, paths),
    ))
