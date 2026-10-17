"""Content identities and bounded metadata reads for validation evidence."""

import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess


SYSTEM_ROOTS = (Path("/usr/bin"), Path("/usr/lib"), Path("/System/Library"))


class UnsupportedContext(ValueError):
    pass


def _json_digest(value):
    return hashlib.sha256(
        json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()
    ).hexdigest()


def _inside(path, roots):
    return any(path == root or root in path.parents for root in roots)


def _file_digest(path):
    before = path.stat()
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    after = path.stat()
    if (
        before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_mode,
    ) != (
        after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_mode,
    ):
        raise UnsupportedContext("input changed while being fingerprinted")
    return digest.hexdigest()


def fingerprint_tree(path, admitted_roots, *, inert_link_roots=()):
    path = Path(path)
    roots = tuple(Path(root).resolve() for root in admitted_roots)
    inert_roots = tuple(Path(root).resolve() for root in inert_link_roots)
    digest = hashlib.sha256()

    def visit(selected, relative):
        information = selected.lstat()
        mode = information.st_mode
        entry = [relative, stat.S_IMODE(mode)]
        if stat.S_ISLNK(mode):
            try:
                resolved = selected.resolve(strict=True)
            except (OSError, RuntimeError) as error:
                raise UnsupportedContext("unresolved manifest link") from error
            if (
                not _inside(resolved, (*roots, *SYSTEM_ROOTS))
                and not _inside(selected, inert_roots)
            ):
                raise UnsupportedContext("manifest link escapes admitted roots")
            entry.extend(("link", os.readlink(selected), str(resolved)))
        elif stat.S_ISREG(mode):
            entry.extend(("file", information.st_size, _file_digest(selected)))
        elif stat.S_ISDIR(mode):
            entry.append("directory")
        else:
            raise UnsupportedContext("unsupported input kind")
        digest.update(json.dumps(entry, ensure_ascii=True, separators=(",", ":")).encode())
        digest.update(b"\n")
        if stat.S_ISDIR(mode):
            children = sorted(selected.iterdir(), key=lambda child: child.name)
            for child in children:
                visit(child, relative + "/" + child.name)
            after = selected.stat()
            if (information.st_ino, information.st_mtime_ns) != (after.st_ino, after.st_mtime_ns):
                raise UnsupportedContext("directory changed while being fingerprinted")

    visit(path, ".")
    return digest.hexdigest()


def _absent_paths(paths):
    identities = {}
    for path in paths:
        try:
            path.lstat()
        except FileNotFoundError:
            identities[str(path)] = "absent"
        else:
            raise UnsupportedContext("standard fallback search root exists outside the admitted closure")
    return identities


def _run(command, *, cwd=None):
    try:
        completed = subprocess.run(
            tuple(map(str, command)), cwd=cwd, check=False,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise UnsupportedContext(f"metadata query unavailable: {Path(command[0]).name}") from error
    if completed.returncode != 0:
        raise UnsupportedContext(f"metadata query failed: {Path(command[0]).name}")
    return completed.stdout


def _file_identity(path):
    information = path.lstat()
    if not stat.S_ISREG(information.st_mode):
        raise UnsupportedContext("artifact/input is not a regular file")
    return {
        "kind": "file", "mode": stat.S_IMODE(information.st_mode),
        "size": information.st_size, "sha256": _file_digest(path),
    }
