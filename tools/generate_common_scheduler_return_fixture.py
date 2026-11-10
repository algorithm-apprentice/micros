#!/usr/bin/env -S python3.15 -E -S -B

"""Materialize OBJ02's exact fixed/current native scheduler inputs."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.common_scheduler_return_source import (
    NATIVE_SOURCES, OBSERVATION_PREFIX, PLAN_PATH, fixed_native_inputs,
    validate_retained_source, workspace_accessor,
)
from tests.host.evidence_authority_scope import checked_source_view, source_bytes
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import owned_artifact


ROOT = Path(__file__).resolve().parents[1]


def source_bundle(root, stage, channel, *, current_sources=None):
    from collections.abc import Mapping

    root = Path(root).resolve()
    if stage not in ("baseline", "current") or channel not in ("ordinary", "work"):
        raise ValueError("OBJ02 materialization has an unknown stage or channel")
    fixed = fixed_native_inputs(root)
    if current_sources is not None and (
        stage != "current" or not isinstance(current_sources, Mapping)
        or set(current_sources) != set(fixed)
        or any(type(value) is not bytes for value in current_sources.values())
    ):
        raise ValueError("OBJ02 current native snapshot is incomplete or untyped")
    inputs, outputs = {}, {}
    for path, raw in sorted(fixed.items()):
        if stage == "current":
            raw = source_bytes(root, root / path) if current_sources is None else current_sources[path]
            if read_regular(root / path)[1]["mode"] != 0o644:
                raise ValueError("OBJ02 current native input changed mode: " + path)
        text = raw.decode()
        validate_retained_source(root, path, text, stage)
        inputs[path] = {
            "kind": "file", "mode": 0o644, "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
        }
        if channel == "work" and path in NATIVE_SOURCES:
            from tests.host.common_scheduler_return_source import observe_source, project_observations

            text = observe_source(root, path, text, stage)
            if project_observations(root, path, text, stage, original=raw.decode()).encode() != raw:
                raise ValueError("OBJ02 observer changed its admitted source")
        if path == PLAN_PATH:
            if channel == "ordinary":
                text = OBSERVATION_PREFIX + text
            text += workspace_accessor(stage)
        outputs[path] = text.encode()
    return inputs, outputs


def prepare_sources(root, directory, stage, channel):
    directory = Path(directory)
    if directory.is_symlink() or directory.exists() and not directory.is_dir():
        raise ValueError("OBJ02 native output is not an ordinary directory")
    inputs, outputs = source_bundle(root, stage, channel)
    directory.mkdir(parents=True, exist_ok=True)
    generated = {}
    for path, raw in outputs.items():
        destination = owned_artifact(directory, path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists() or destination.is_symlink():
            read_regular(destination)
        destination.write_bytes(raw)
        destination.chmod(0o644)
        generated[path] = read_regular(destination)[1]
    return inputs, generated


def main(argv=None):
    from tests.host.common_scheduler_return_source import RECIPE_ADMISSION, native_products
    from tools.python_host_runtime import require_current_runtime

    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.parse_args(argv)
    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("OBJ02 generation requires the project root")
    directory = owned_artifact(ROOT, RECIPE_ADMISSION["native_root"])
    for stage, channel in native_products():
        prepare_sources(ROOT, directory / stage / channel, stage, channel)
    return 0


if __name__ == "__main__":
    try:
        with checked_source_view(ROOT):
            status = main()
        raise SystemExit(status)
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_OBJ02_GENERATION_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
