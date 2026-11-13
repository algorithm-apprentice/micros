#!/usr/bin/env -S python3.15 -E -S -B

"""Materialize current ordinary/observed selected-grant native inputs."""

import argparse
from pathlib import Path
import re
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host import native_observations
from tests.host.c_source_records import file_scope_objects, function_records
from tests.host.selected_grant_validation_evidence import (
    NATIVE_BACKENDS, NATIVE_HEADERS, NATIVE_SOURCES, NATIVE_SUBSETS, OBSERVED_HEADERS,
)
from tests.host.selected_grant_validation_source import validate_native_sites
from tests.host.source_snapshot import checked_source_view, source_bytes
from tools.python_host_runtime import require_current_runtime
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import owned_artifact


ROOT = Path(__file__).resolve().parents[1]


def source_bundle(root, channel):
    root = Path(root).resolve()
    if channel not in ("ordinary", "work"):
        raise ValueError("GRANT01 materialization has an unknown channel")
    family = native_observations.declarations(root)["families"]["GRANT01"]
    if tuple(sorted(family["native_sources"])) != tuple(sorted(NATIVE_SOURCES)):
        raise ValueError("GRANT01 source membership changed")
    if dict(family["subsets"]) != NATIVE_SUBSETS:
        raise ValueError("GRANT01 compiled subset changed")
    outputs = {}
    for path in (*NATIVE_SOURCES, *NATIVE_HEADERS):
        original = NATIVE_BACKENDS.get(path, path)
        raw = source_bytes(root, root / original)
        if read_regular(root / original)[1]["mode"] != 0o644:
            raise ValueError("GRANT01 native input changed mode: " + path)
        if path in NATIVE_SOURCES or path in OBSERVED_HEADERS:
            validate_native_sites(root, path, raw.decode())
            text = native_observations.observe_source(root, "GRANT01", path, raw.decode(), channel)
            if path in NATIVE_SUBSETS:
                records = function_records(text)
                text = '#include "kernel/boot/bootstrap_control_internal.h"\n' + "".join(
                    records[name].signature + "{" + records[name].body + "}\n"
                    for name in NATIVE_SUBSETS[path]
                )
            raw = (native_observations.PREFIX + text).encode() if channel == "work" else raw
            if path in NATIVE_SUBSETS and channel == "ordinary":
                raw = text.encode()
        elif function_records(raw.decode()) or file_scope_objects(raw.decode()):
            raise ValueError("GRANT01 header introduced unobserved code or state: " + path)
        outputs[path] = raw
    for path, raw in outputs.items():
        for delimiter, name in re.findall(r'(?m)^\s*#\s*include\s*(["<])([^">]+)[">]', raw.decode()):
            if delimiter == "<" and name in ("stdbool.h", "stddef.h", "stdint.h"):
                continue
            candidates = (Path(path).parent / name, Path(name), Path("include") / name)
            if not any(str(candidate) in outputs for candidate in candidates):
                raise ValueError("GRANT01 compiled header leaves its qualified input set: " + name)
    return outputs


def main(argv=None):
    argparse.ArgumentParser(description=__doc__, allow_abbrev=False).parse_args(argv)
    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("GRANT01 generation requires the project root")
    for channel in ("ordinary", "work"):
        directory = owned_artifact(ROOT, "build/host-debug/selected-grant-validation/" + channel)
        directory.mkdir(parents=True, exist_ok=True)
        for relative, raw in source_bundle(ROOT, channel).items():
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
        print("MICROS_GRANT01_GENERATION_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error
