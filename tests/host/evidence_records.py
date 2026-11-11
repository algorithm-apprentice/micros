"""Strict records and content identities for current validation evidence."""

from dataclasses import fields, is_dataclass
from enum import Enum
import hashlib
import json
from pathlib import Path
import stat


def regular_source(root, relative):
    if (
        type(relative) is not str or not relative or "\\" in relative
        or Path(relative).is_absolute() or Path(relative).as_posix() != relative
        or any(part in (".", "..") for part in Path(relative).parts)
    ):
        raise ValueError("current evidence has an unsafe source path")
    root = Path(root)
    path = root / relative
    try:
        for parent in path.parents:
            if not stat.S_ISDIR(parent.lstat().st_mode):
                raise ValueError("current evidence has a linked source parent")
            if parent == root:
                break
        status = path.lstat()
        if not stat.S_ISREG(status.st_mode) or status.st_nlink != 1:
            raise ValueError("current evidence requires an unlinked regular source")
    except OSError as error:
        raise ValueError(f"current evidence source is unavailable: {relative}") from error
    return path


def record_fields(value, expected):
    if type(value) is not dict or set(value) != set(expected):
        raise ValueError("evidence record has unknown or missing fields")


def record_strings(value, *, empty=False):
    if type(value) is not list or (not value and not empty) or any(
        type(item) is not str or not item for item in value
    ):
        raise ValueError("evidence record lacks its exact string sequence")
    return tuple(value)


def json_pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("evidence record contains a duplicate field")
        result[key] = value
    return result


def _canonical(value):
    if isinstance(value, Enum):
        return value.value
    if is_dataclass(value):
        return {item.name: _canonical(getattr(value, item.name)) for item in fields(value)}
    if isinstance(value, dict):
        return {str(key): _canonical(item) for key, item in sorted(value.items(), key=lambda pair: str(pair[0]))}
    if isinstance(value, (tuple, list)):
        return [_canonical(item) for item in value]
    return value


def digest(value):
    return hashlib.sha256(json.dumps(
        _canonical(value), sort_keys=True, separators=(",", ":"), ensure_ascii=True,
    ).encode("ascii")).hexdigest()
