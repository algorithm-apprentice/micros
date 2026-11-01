"""Strict current-run CI data and file identities, never local receipts."""

import hashlib
import json
import math
from pathlib import Path
import re

from tools.ci_partition import PARTS
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import encoded
from tools.stack_bound_types import require_path, require_uint64


def require_keys(value, keys, label):
    if type(value) is not dict or set(value) != set(keys):
        raise ValueError(f"{label}: unknown or missing record fields")


def pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("duplicate CI JSON field")
        result[key] = value
    return result


def invalid_constant(_value):
    raise ValueError("nonfinite CI JSON value")


def finite_float(value):
    result = float(value)
    if not math.isfinite(result):
        raise ValueError("nonfinite CI JSON number")
    return result


def read_json(path):
    data = read_regular(path)[0]
    result = json.loads(
        data.decode("utf-8"), object_pairs_hook=pairs,
        parse_constant=invalid_constant, parse_float=finite_float,
    )
    if type(result) is not dict:
        raise ValueError("CI record must be a complete object")
    return result


def owned_file(directory, name):
    require_path(name)
    current = Path(directory)
    if current.is_symlink():
        raise ValueError("CI data directory is linked")
    for part in Path(name).parts:
        current /= part
        if current.is_symlink():
            raise ValueError("CI data path is linked")
    return current


def data_identity(path):
    data, identity = read_regular(path)
    return {"size": identity["size"], "sha256": hashlib.sha256(data).hexdigest()}


def file_inventory(directory):
    directory = Path(directory)
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError("CI part directory is not an ordinary directory")
    result = {}
    for path in sorted(directory.rglob("*")):
        name = str(path.relative_to(directory))
        owned_file(directory, name)
        if path.is_dir():
            continue
        if name != "result.json":
            result[name] = data_identity(path)
    return result


def require_files(directory, expected):
    if type(expected) is not dict or not expected:
        raise ValueError("CI part lacks its complete file inventory")
    for name, identity in expected.items():
        require_path(name)
        require_keys(identity, ("size", "sha256"), "CI file identity")
        require_uint64(identity["size"], "CI file size")
        if type(identity["sha256"]) is not str or re.fullmatch(r"[0-9a-f]{64}", identity["sha256"]) is None:
            raise ValueError("CI file lacks its exact content digest")
    if file_inventory(directory) != expected:
        raise ValueError("CI artifact file inventory or bytes changed")


def require_context(actual, expected, part):
    if part not in PARTS:
        raise ValueError("unknown CI producer part")
    require_keys(actual, expected, "CI producer context")
    if type(actual["version"]) is not int or actual["version"] != 1 or actual["role"] != "producer" or actual["part"] != part:
        raise ValueError("CI producer borrowed another role or part")
    host = actual["host"]
    require_keys(host, ("system", "machine", "version"), "CI producer host")
    if (
        (host["system"], host["machine"]) != ("Darwin", "arm64")
        or type(host["version"]) is not str or host["version"].split(".")[0] != "26"
        or any(type(actual[field]) is not dict for field in ("repository_context", "runtime", "tools", "firmware"))
    ):
        raise ValueError("CI producer has an unsupported host or context shape")
    for field in ("repository", "workflow", "event", "candidate", "committed_tree", "base"):
        if encoded(actual[field]) != encoded(expected[field]):
            raise ValueError("CI producer has a foreign source/workflow/run/attempt/base identity")
    source = actual["candidate"]
    if (
        source["head"] != actual["event"]["commit"] or source["tree"] != actual["committed_tree"]
        or source["missing_paths"] or actual["ctest_parallel"] != ("3" if part == "native" else "")
    ):
        raise ValueError("CI producer lacks its exact committed source or execution mode")
