#!/usr/bin/env -S python3.15 -E -S -B

"""Materialize MEM04's fixed native binding view without changing production."""

import argparse
from functools import lru_cache
import hashlib
import io
from pathlib import Path
import subprocess
import sys
import tarfile

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.selected_address_space_source import (
    CHANNELS, NATIVE_SOURCE_MEMBERS, OBSERVED_HEADERS, RECIPE_ADMISSION, RUNTIME_REFERENCE,
    bind_native, observe_source, project_observations, source_bytes,
)
from tests.host.work_count_types import Stage
from tests.host.evidence_authority_source import regular_source
from tests.host.evidence_authority_scope import checked_source_view
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import owned_artifact


ROOT = Path(__file__).resolve().parents[1]


@lru_cache(maxsize=4)
def fixed_inputs(root):
    root = Path(root)
    entries = subprocess.check_output(
        ("git", "ls-tree", "-r", "-z", RUNTIME_REFERENCE),
        cwd=root, timeout=30,
    ).decode().split("\0")
    members = {}
    for entry in filter(None, entries):
        metadata, separator, path = entry.partition("\t")
        fields = metadata.split()
        if (
            not separator or len(fields) != 3 or fields[1] != "blob" or path in members
            or Path(path).is_absolute() or ".." in Path(path).parts or "\\" in path
        ):
            raise ValueError("MEM04 fixed native Git membership is malformed")
        members[path] = tuple(fields)
    selected = {
        path for path in members if path.endswith(".h") and path.startswith(("arch/", "include/", "kernel/"))
    } | set(NATIVE_SOURCE_MEMBERS)
    if any(path not in members or members[path][0] not in ("100644", "100755") for path in selected):
        raise ValueError("MEM04 fixed native member is not an ordinary Git blob")
    raw = subprocess.check_output(
        ("git", "-c", "tar.umask=022", "archive", RUNTIME_REFERENCE, *sorted(selected)),
        cwd=root, timeout=30,
    )
    result = {}
    with tarfile.open(fileobj=io.BytesIO(raw)) as archive:
        for member in archive:
            if member.isdir():
                continue
            if (
                not member.isfile() or member.name not in selected or member.name in result
                or member.mode & 0o777 != int(members[member.name][0], 8) & 0o777
            ):
                raise ValueError("MEM04 native archive has an unadmitted member")
            data = archive.extractfile(member).read()
            oid = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
            if oid != members[member.name][2]:
                raise ValueError("MEM04 native archive differs from fixed Git bytes")
            result[member.name] = (data, member.mode & 0o777)
    if set(result) != selected:
        raise ValueError("MEM04 native archive omitted a fixed input")
    return result


def source_bundle(root, stage, channel):
    root = Path(root).resolve()
    if stage not in (Stage.RED, Stage.GREEN) or channel not in CHANNELS:
        raise ValueError("MEM04 native stage/channel is unknown")
    inputs, outputs = {}, {}
    for path, (original, mode) in sorted(fixed_inputs(str(root)).items()):
        raw = original
        if stage is Stage.GREEN:
            raw, identity = read_regular(regular_source(root, path))
            if identity["mode"] != mode:
                raise ValueError("MEM04 native input mode changed: " + path)
            if raw != source_bytes(root, path, stage):
                raise ValueError("MEM04 native input changed during source admission")
        inputs[path] = {
            "kind": "file", "mode": mode, "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
        }
        text = raw.decode("utf-8")
        if path in (*NATIVE_SOURCE_MEMBERS, *OBSERVED_HEADERS):
            text = observe_source(root, path, text, channel, stage)
            if project_observations(root, path, text, channel, stage).encode() != raw:
                raise ValueError("MEM04 native observation did not preserve actual source")
        outputs[path] = (bind_native(path, text, stage).encode(), mode)
    return inputs, outputs


def prepare_sources(root, directory, stage, channel="ordinary"):
    root, directory = Path(root).resolve(), Path(directory)
    if directory.is_symlink() or directory.exists() and not directory.is_dir():
        raise ValueError("MEM04 native output is not an owned directory")
    directory.mkdir(parents=True, exist_ok=True)
    inputs, outputs = source_bundle(root, stage, channel)
    generated = {}
    for path, (data, mode) in outputs.items():
        target = owned_artifact(directory, path)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists() or target.is_symlink():
            read_regular(target)
        target.write_bytes(data)
        target.chmod(mode)
        generated[path] = read_regular(target)[1]
    return inputs, generated


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.parse_args(argv)
    from tools.python_host_runtime import require_current_runtime

    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("MEM04 native generation requires the project root")
    directory = owned_artifact(ROOT, RECIPE_ADMISSION["generator_output"])
    for channel in CHANNELS:
        prepare_sources(ROOT, directory / channel, Stage.GREEN, channel)
    return 0


if __name__ == "__main__":
    try:
        with checked_source_view(ROOT):
            status = main()
        raise SystemExit(status)
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_MEM04_GENERATION_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
