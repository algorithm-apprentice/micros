#!/usr/bin/env -S python3.15 -E -S -B

"""Exact metadata-only leaf artifact boundary from Accepted ADR-0085."""

import argparse
from contextlib import contextmanager
import fcntl
import json
import os
from pathlib import Path
import sys
import stat

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.python_host_runtime import require_current_runtime
from tools.stack_artifact_authority import validate_pair_entry
from tools.stack_artifact_build import leaf_recipes, pair_record_path, tool_identities
from tools.stack_artifact_files import canonicalize_passive_file, read_regular
from tools.stack_bound_build import static_profiles
from tools.stack_bound_types import StackEvidenceError
from tools.validation_context import capture_candidate


@contextmanager
def artifact_publication_lock(root):
    path = Path(root).resolve() / "build/stack-evidence/canonicalization.lock"
    if any(parent.is_symlink() for parent in (path, *path.parents)):
        raise StackEvidenceError("linked canonicalization lock path")
    descriptor = os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        information = os.fstat(descriptor)
        if (
            not stat.S_ISREG(information.st_mode) or information.st_nlink != 1
            or stat.S_IMODE(information.st_mode) != 0o600
            or information.st_uid != os.geteuid()
        ):
            raise StackEvidenceError("unreviewed canonicalization lock identity")
        fcntl.flock(descriptor, fcntl.LOCK_EX)
        yield
    finally:
        os.close(descriptor)


def require_owned_path(path, directory):
    if not path.resolve(strict=True).is_relative_to(directory.resolve(strict=True)):
        raise StackEvidenceError("leaf artifact escaped its declared build directory")
    for current in (path, *path.parents):
        if current.is_symlink():
            raise StackEvidenceError("linked artifact paths are not admitted")


def canonicalize_pair(root, pair, leaf):
    root = Path(root).resolve()
    candidate = capture_candidate(root)
    if not candidate.clean:
        raise StackEvidenceError("artifact source is not the exact staged candidate")
    data, identity = read_regular(pair)
    if identity["mode"] != 0o600:
        raise StackEvidenceError("pair authority file has an unreviewed mode")
    record = json.loads(data)
    profiles = {profile.configure: profile for profile in static_profiles(root)}
    if (
        type(record) is not dict or type(record.get("profile")) is not str
        or record["profile"] not in profiles
    ):
        raise StackEvidenceError("pair does not name a current compiled configuration")
    profile = profiles[record["profile"]]
    if Path(pair) != pair_record_path(root, profile):
        raise StackEvidenceError("pair authority has an unexpected path")
    directory = root / "build/stack-evidence" / profile.configure / "metadata"
    recipes = leaf_recipes(root, directory, True)
    ordinary, metadata, expected = validate_pair_entry(
        root, record, candidate, tool_identities(dict(os.environ)), recipes, leaf,
    )
    require_owned_path(ordinary, root / record["ordinary_root"])
    require_owned_path(metadata, root / record["metadata_root"])
    if capture_candidate(root) != candidate:
        raise StackEvidenceError("artifact source changed before publication")
    result = canonicalize_passive_file(ordinary, metadata, expected)
    if capture_candidate(root) != candidate or read_regular(pair)[1] != identity:
        raise StackEvidenceError("source or pair authority changed after publication")
    print(f"STACK_METADATA_CANONICALIZED leaf={leaf} sha256={result['sha256']}")
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description="Canonicalize one sealed metadata-only ELF leaf.")
    parser.add_argument("--pair", required=True, type=Path)
    parser.add_argument("--leaf", required=True)
    arguments = parser.parse_args(argv)
    root = Path(__file__).resolve().parent.parent
    try:
        require_current_runtime(sys.executable)
        with artifact_publication_lock(root):
            canonicalize_pair(root, arguments.pair, arguments.leaf)
    except (OSError, ValueError, RuntimeError) as error:
        print(f"stack metadata artifact failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
