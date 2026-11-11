#!/usr/bin/env -S python3.15 -E -S -B

"""Materialize current address-space sources with finite native bindings."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.selected_address_space_source import (
    CHANNELS, NATIVE_SOURCE_MEMBERS, OBSERVED_HEADERS, RECIPE_ADMISSION, bind_native,
)
from tests.host import native_observations
from tests.host.work_count_types import Stage
from tests.host.source_snapshot import checked_source_view, source_bytes
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import owned_artifact


ROOT = Path(__file__).resolve().parents[1]


def source_bundle(root, stage, channel):
    root = Path(root).resolve()
    if stage is not Stage.GREEN or channel not in CHANNELS:
        raise ValueError("MEM04 native stage/channel is unknown")
    inputs, outputs = {}, {}
    for path in native_observations.native_inputs(root, "MEM04"):
        raw = source_bytes(root, root / path)
        mode = read_regular(root / path)[1]["mode"]
        if mode != 0o644:
            raise ValueError("MEM04 native input has an unadmitted mode: " + path)
        inputs[path] = {
            "kind": "file", "mode": mode, "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
        }
        text = raw.decode("utf-8")
        if path in (*NATIVE_SOURCE_MEMBERS, *OBSERVED_HEADERS):
            text = native_observations.observe_source(root, "MEM04", path, text, channel)
        text = bind_native(path, text, stage)
        if path in (*NATIVE_SOURCE_MEMBERS, *OBSERVED_HEADERS) and channel != "ordinary":
            text = native_observations.PREFIX + text
        outputs[path] = (text.encode(), mode)
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
