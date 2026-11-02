"""Closed literal source declarations and current kernel-file bindings."""

import ast
from pathlib import Path
import stat


def literal_value(node):
    if isinstance(node, ast.Constant) and type(node.value) in (str, int):
        return node.value
    if isinstance(node, ast.Tuple):
        return tuple(literal_value(item) for item in node.elts)
    if isinstance(node, ast.Dict):
        result = {}
        for key_node, value_node in zip(node.keys, node.values):
            key = literal_value(key_node)
            if not isinstance(key, (str, int, tuple)):
                raise ValueError("source authority has a nonliteral dictionary key")
            try:
                duplicate = key in result
            except TypeError as error:
                raise ValueError("source authority has an invalid dictionary key") from error
            if duplicate:
                raise ValueError("source authority has a duplicate dictionary key")
            result[key] = literal_value(value_node)
        return result
    raise ValueError("source authority requires literal data")


def single_assignment(tree, name):
    bindings = [
        node for node in ast.walk(tree)
        if isinstance(node, ast.Name) and isinstance(node.ctx, (ast.Store, ast.Del)) and node.id == name
    ]
    statements = [
        node for node in tree.body
        if isinstance(node, ast.Assign) and len(node.targets) == 1
        and isinstance(node.targets[0], ast.Name) and node.targets[0].id == name
    ]
    if len(statements) != 1 or bindings != [statements[0].targets[0]]:
        raise ValueError(f"source authority requires one ordinary {name} assignment")
    return statements[0]


def _relative(path):
    if (
        type(path) is not str or not path or "\\" in path
        or Path(path).is_absolute() or Path(path).as_posix() != path
        or any(part in (".", "..") for part in Path(path).parts)
    ):
        raise ValueError("source authority has an unsafe payload path")
    return path


def regular_source(root, relative):
    path = root / _relative(relative)
    try:
        for parent in path.parents:
            status = parent.lstat()
            if not stat.S_ISDIR(status.st_mode):
                raise ValueError("source authority has a linked payload parent")
            if parent == root:
                break
        status = path.lstat()
        if not stat.S_ISREG(status.st_mode) or status.st_nlink != 1:
            raise ValueError("source authority requires an unlinked regular payload")
    except OSError as error:
        raise ValueError(f"source authority payload is unavailable: {relative}") from error
    return path


def kernel_directory(root):
    source = regular_source(root, "tests/host/kernel_layout_source.py")
    tree = ast.parse(source.read_text(encoding="utf-8"))
    mapping = literal_value(single_assignment(tree, "KERNEL_FILE_FACTS").value)
    if type(mapping) is not dict:
        raise ValueError("source authority requires the literal kernel directory map")
    physical_paths = set()
    for logical, record in mapping.items():
        if (
            type(logical) is not str or not logical.startswith("kernel/")
            or type(record) is not tuple or len(record) != 4 or type(record[0]) is not str
        ):
            raise ValueError("source authority has a malformed kernel directory binding")
        _relative(logical)
        physical = _relative(record[0])
        if physical in physical_paths:
            raise ValueError("source authority has ambiguous kernel directory owners")
        physical_paths.add(physical)
    return mapping


def kernel_sources(root, logical_paths, directory=None):
    mapping = kernel_directory(root) if directory is None else directory
    if len(set(logical_paths)) != len(logical_paths):
        raise ValueError("source authority has duplicate source/header ownership")
    result = {}
    selected = set()
    for logical in logical_paths:
        _relative(logical)
        physical = logical
        if logical.startswith("kernel/"):
            record = mapping.get(logical)
            if type(record) is not tuple or len(record) != 4 or type(record[0]) is not str:
                raise ValueError("source authority has an unknown kernel directory binding")
            physical = record[0]
            if physical != logical and ((root / logical).exists() or (root / logical).is_symlink()):
                raise ValueError("source authority has ambiguous old/current owners")
        regular_source(root, physical)
        if physical in selected:
            raise ValueError("source authority has ambiguous current owners")
        selected.add(physical)
        result[logical] = physical
    return result
