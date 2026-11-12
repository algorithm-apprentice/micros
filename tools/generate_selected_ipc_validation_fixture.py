#!/usr/bin/env -S python3.15 -E -S -B

"""Materialize current ordinary/observed selected IPC native inputs."""

import argparse
from pathlib import Path
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host import native_observations
from tests.host.selected_ipc_validation_evidence import NATIVE_HEADERS, NATIVE_SOURCES
from tests.host.selected_ipc_validation_source import validate_native_sites
from tests.host.c_source_records import file_scope_objects, function_records
from tests.host.source_snapshot import checked_source_view, source_bytes
from tools.python_host_runtime import require_current_runtime
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import owned_artifact


ROOT = Path(__file__).resolve().parents[1]


def source_bundle(root, channel):
    root = Path(root).resolve()
    if channel not in ("ordinary", "work"):
        raise ValueError("IPC01 native materialization has an unknown channel")
    if tuple(sorted(native_observations.declarations(root)["families"]["IPC01"]["native_sources"])) != tuple(sorted(NATIVE_SOURCES)):
        raise ValueError("IPC01 observation source membership changed")
    outputs = {}
    for path in (*NATIVE_SOURCES, *NATIVE_HEADERS):
        raw = source_bytes(root, root / path)
        if read_regular(root / path)[1]["mode"] != 0o644:
            raise ValueError("IPC01 native input changed mode: " + path)
        if path in NATIVE_SOURCES:
            validate_native_sites(root, path, raw.decode())
            text = native_observations.observe_source(root, "IPC01", path, raw.decode(), channel)
            raw = (native_observations.PREFIX + text).encode() if channel == "work" else raw
        elif function_records(raw.decode()) or file_scope_objects(raw.decode()):
            raise ValueError("IPC01 native header introduced unobserved code or state: " + path)
        outputs[path] = raw
    return outputs


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.parse_args(argv)
    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("IPC01 native generation requires the project root")
    for channel in ("ordinary", "work"):
        outputs = source_bundle(ROOT, channel)
        directory = owned_artifact(ROOT, "build/host-debug/selected-ipc-validation/" + channel)
        directory.mkdir(parents=True, exist_ok=True)
        for relative, raw in outputs.items():
            destination = owned_artifact(directory, relative)
            destination.parent.mkdir(parents=True, exist_ok=True)
            if destination.exists() or destination.is_symlink():
                previous, identity = read_regular(destination)
                if previous == raw and identity["mode"] == 0o644:
                    continue
            destination.write_bytes(raw)
            destination.chmod(0o644)
    return 0


if __name__ == "__main__":
    try:
        with checked_source_view(ROOT):
            status = main()
        raise SystemExit(status)
    except (OSError, ValueError, RuntimeError) as error:
        print("MICROS_IPC01_GENERATION_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
