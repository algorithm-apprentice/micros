#!/usr/bin/env -S python3.15 -E -S -B

"""Materialize current object/hart inputs and their passive observations."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.source_snapshot import checked_source_view, source_bytes
from tests.host.object_hart_validation_evidence import NATIVE_HEADERS, NATIVE_SOURCES
from tests.host import native_observations
from tests.host.common_scheduler_return_source import validate_current_native_source
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import owned_artifact


ROOT = Path(__file__).resolve().parents[1]


def source_bundle(root, stage, channel):
    root = Path(root).resolve()
    if stage != "current" or channel not in ("ordinary", "aggregate", "stack"):
        raise ValueError("OBJ01 source materialization has an unknown stage or channel")
    inputs, outputs = {}, {}
    for path in sorted((*NATIVE_SOURCES, *NATIVE_HEADERS)):
        raw = source_bytes(root, root / path)
        if read_regular(root / path)[1]["mode"] != 0o644:
            raise ValueError("OBJ01 current native input changed file mode: " + path)
        text = raw.decode()
        validate_current_native_source(path, text, root=root)
        inputs[path] = {
            "kind": "file", "mode": 0o644, "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
        }
        if path in NATIVE_SOURCES:
            text = native_observations.observe_source(root, "OBJ01", path, text, channel)
            if channel != "ordinary":
                text = native_observations.PREFIX + text
        outputs[path] = text.encode()
    return inputs, outputs


def prepare_sources(root, directory, stage, channel):
    directory = Path(directory)
    if directory.is_symlink() or directory.exists() and not directory.is_dir():
        raise ValueError("OBJ01 native output is not an ordinary owned directory")
    inputs, outputs = source_bundle(root, stage, channel)
    directory.mkdir(parents=True, exist_ok=True)
    generated = {}
    for path, data in outputs.items():
        target = owned_artifact(directory, path)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists() or target.is_symlink():
            read_regular(target)
        target.write_bytes(data)
        target.chmod(0o644)
        generated[path] = read_regular(target)[1]
    return inputs, generated


def main(argv=None):
    from tools.python_host_runtime import require_current_runtime

    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.parse_args(argv)
    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("OBJ01 generation requires the project root")
    directory = owned_artifact(ROOT, "build/host-debug/object-hart-validation")
    for channel in ("ordinary", "aggregate", "stack"):
        prepare_sources(ROOT, directory / "current" / channel, "current", channel)
    return 0


if __name__ == "__main__":
    try:
        with checked_source_view(ROOT):
            status = main()
        raise SystemExit(status)
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_OBJ01_GENERATION_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
