#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from pathlib import Path, PurePosixPath
import struct


MAGIC = 0x31534652
VERSION = 1
HEADER_SIZE = 64
ENTRY_SIZE = 128
ENTRY_CAPACITY = 64
DATA_OFFSET = HEADER_SIZE + ENTRY_SIZE * ENTRY_CAPACITY
DATA_SIZE_MAX = 262144
IMAGE_SIZE_MAX = DATA_OFFSET + DATA_SIZE_MAX
BLOCK_SIZE = 4096
BLOCK_CAPACITY = 64
PATH_MAX = 4096
NAME_MAX = 60
DIGEST_OFFSET = 24
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
MODE_DIRECTORY = 0x00004000
MODE_REGULAR = 0x00008000
HEADER = struct.Struct("<IHHHHHHIIQ4Q")
ENTRY = struct.Struct("<HHIII64s6Q")


class SeedError(ValueError):
    pass


def _is_relative_to(path, root):
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise SeedError(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def _load_json(path):
    try:
        return json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=_unique_object,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise SeedError(f"cannot read seed declaration: {error}") from error


def _require_fields(value, required, optional=()):
    if not isinstance(value, dict):
        raise SeedError("seed object must be a JSON object")
    allowed = set(required) | set(optional)
    unknown = set(value) - allowed
    missing = set(required) - set(value)
    if unknown:
        raise SeedError(f"unknown seed fields: {sorted(unknown)}")
    if missing:
        raise SeedError(f"missing seed fields: {sorted(missing)}")


def _canonical_path(value):
    if not isinstance(value, str):
        raise SeedError("entry path must be a string")
    if "\x00" in value or not value.startswith("/"):
        raise SeedError(f"entry path is not absolute: {value!r}")
    if value == "/":
        return value, b"", ()
    if value.endswith("/") or "//" in value:
        raise SeedError(f"entry path is not canonical: {value!r}")
    components = tuple(value[1:].split("/"))
    if any(component in ("", ".", "..") for component in components):
        raise SeedError(f"entry path is not canonical: {value!r}")
    encoded_components = tuple(
        component.encode("utf-8") for component in components
    )
    if any(
        len(component) == 0 or len(component) > NAME_MAX
        for component in encoded_components
    ):
        raise SeedError(f"entry component exceeds {NAME_MAX} bytes")
    encoded_path = value.encode("utf-8")
    if len(encoded_path) + 1 > PATH_MAX:
        raise SeedError(f"entry path exceeds {PATH_MAX} bytes")
    return value, encoded_components[-1], components


def _mode(value):
    if (
        not isinstance(value, str)
        or len(value) != 4
        or value[0] != "0"
        or any(character not in "01234567" for character in value)
    ):
        raise SeedError("mode must be four octal characters in 0000..0777")
    return int(value, 8)


def _source_bytes(source, repository_root):
    if not isinstance(source, str) or source == "":
        raise SeedError("file source must be a nonempty string")
    source_path = PurePosixPath(source)
    if (
        source_path.is_absolute()
        or any(part in ("", ".", "..") for part in source_path.parts)
    ):
        raise SeedError(f"file source is not repository-relative: {source!r}")

    current = repository_root
    for part in source_path.parts:
        current = current / part
        if current.is_symlink():
            raise SeedError(f"file source uses a symlink: {source!r}")
    try:
        resolved = current.resolve(strict=True)
    except OSError as error:
        raise SeedError(f"file source cannot be resolved: {source!r}") from error
    if not _is_relative_to(resolved, repository_root):
        raise SeedError(f"file source leaves repository: {source!r}")
    if not resolved.is_file():
        raise SeedError(f"file source is not a regular file: {source!r}")
    try:
        data = resolved.read_bytes()
    except OSError as error:
        raise SeedError(f"file source cannot be read: {source!r}") from error
    if len(data) > DATA_SIZE_MAX:
        raise SeedError("file source exceeds RAMFS file-size maximum")
    return data


def _parent_path(path):
    if path == "/":
        return None
    parent, _, _ = path.rpartition("/")
    return parent or "/"


def _validated_entries(declaration, repository_root):
    _require_fields(declaration, ("version", "entries"))
    if (
        not isinstance(declaration["version"], int)
        or isinstance(declaration["version"], bool)
        or declaration["version"] != VERSION
    ):
        raise SeedError(f"unsupported seed version: {declaration['version']!r}")
    raw_entries = declaration["entries"]
    if not isinstance(raw_entries, list):
        raise SeedError("entries must be a JSON array")
    if not 1 <= len(raw_entries) <= ENTRY_CAPACITY:
        raise SeedError("entry count exceeds RAMFS node capacity")

    entries = {}
    for raw in raw_entries:
        _require_fields(raw, ("path", "type", "mode"), ("source",))
        path, name, components = _canonical_path(raw["path"])
        if path in entries:
            raise SeedError(f"duplicate entry path: {path}")
        entry_type = raw["type"]
        if entry_type not in ("directory", "file"):
            raise SeedError(f"unsupported entry type: {entry_type!r}")
        permissions = _mode(raw["mode"])
        if entry_type == "directory":
            if "source" in raw:
                raise SeedError("directory entry must not contain source")
            data = b""
            mode = MODE_DIRECTORY | permissions
        else:
            if "source" not in raw:
                raise SeedError("file entry requires source")
            data = _source_bytes(raw["source"], repository_root)
            mode = MODE_REGULAR | permissions
        entries[path] = {
            "path": path,
            "name": name,
            "components": components,
            "type": entry_type,
            "mode": mode,
            "data": data,
        }

    root = entries.get("/")
    if root is None or root["type"] != "directory":
        raise SeedError("seed requires exactly one directory root")
    for path, entry in entries.items():
        if path == "/":
            continue
        parent_path = _parent_path(path)
        parent = entries.get(parent_path)
        if parent is None:
            raise SeedError(f"missing parent entry: {parent_path}")
        if parent["type"] != "directory":
            raise SeedError(f"parent is not a directory: {parent_path}")

    ordered = sorted(entries.values(), key=lambda entry: entry["path"].encode())
    if ordered[0]["path"] != "/":
        raise SeedError("root ordering invariant failed")
    return ordered


def image_digest(image):
    digest = FNV_OFFSET
    for index, original in enumerate(image):
        byte = 0 if DIGEST_OFFSET <= index < DIGEST_OFFSET + 8 else original
        digest ^= byte
        digest = (digest * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return digest


def build_seed(manifest, repository_root):
    root = Path(repository_root).resolve(strict=True)
    manifest_path = Path(manifest).resolve(strict=True)
    if not _is_relative_to(manifest_path, root):
        raise SeedError("seed declaration leaves repository")
    entries = _validated_entries(_load_json(manifest_path), root)
    index_by_path = {
        entry["path"]: index for index, entry in enumerate(entries)
    }

    data_size = sum(len(entry["data"]) for entry in entries)
    if data_size > DATA_SIZE_MAX:
        raise SeedError("aggregate seed data exceeds RAMFS capacity")
    rounded_blocks = sum(
        (len(entry["data"]) + BLOCK_SIZE - 1) // BLOCK_SIZE
        for entry in entries
    )
    if rounded_blocks > BLOCK_CAPACITY:
        raise SeedError("aggregate rounded seed data exceeds block capacity")

    image_size = DATA_OFFSET + data_size
    if image_size > IMAGE_SIZE_MAX:
        raise SeedError("seed image exceeds fixed maximum")
    image = bytearray(image_size)
    HEADER.pack_into(
        image,
        0,
        MAGIC,
        VERSION,
        HEADER_SIZE,
        ENTRY_SIZE,
        ENTRY_CAPACITY,
        len(entries),
        0,
        data_size,
        image_size,
        0,
        0,
        0,
        0,
        0,
    )

    payload_offset = 0
    for index, entry in enumerate(entries):
        parent_path = _parent_path(entry["path"])
        parent_index = 0 if parent_path is None else index_by_path[parent_path]
        data = entry["data"]
        ENTRY.pack_into(
            image,
            HEADER_SIZE + index * ENTRY_SIZE,
            parent_index,
            len(entry["name"]),
            entry["mode"],
            0 if entry["type"] == "directory" else payload_offset,
            len(data),
            entry["name"].ljust(64, b"\0"),
            0,
            0,
            0,
            0,
            0,
            0,
        )
        if data:
            start = DATA_OFFSET + payload_offset
            image[start:start + len(data)] = data
            payload_offset += len(data)
    if payload_offset != data_size:
        raise SeedError("seed payload accounting invariant failed")
    struct.pack_into("<Q", image, DIGEST_OFFSET, image_digest(image))
    return bytes(image)


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate the deterministic RAMFS seed image."
    )
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--repository-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    try:
        image = build_seed(
            arguments.manifest,
            arguments.repository_root,
        )
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_bytes(image)
    except (OSError, SeedError) as error:
        raise SystemExit(f"RAMFS seed generation failed: {error}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
