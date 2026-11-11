"""Invocation-owned current source bytes and closing consistency checks."""

from contextlib import contextmanager
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
from types import MappingProxyType


_active = None


@dataclass(frozen=True)
class _Snapshot:
    signature: str
    files: object


def _digest(value):
    return hashlib.sha256(json.dumps(
        value, sort_keys=True, separators=(",", ":"), allow_nan=False,
    ).encode()).hexdigest()


def _git(root, *arguments):
    return subprocess.check_output(
        ("git", *arguments), cwd=root, stdin=subprocess.DEVNULL,
        stderr=subprocess.PIPE, timeout=30,
    )


def _runtime_identity():
    from tools.python_host_runtime import require_current_runtime

    runtime = dict(require_current_runtime())
    runtime["paths"] = runtime["paths"][-3:]
    return runtime


def _repository_identity(root):
    actual = Path(_git(root, "rev-parse", "--show-toplevel").decode().strip()).resolve()
    if actual != root:
        raise ValueError("source snapshot does not identify its repository root")
    common = (root / _git(root, "rev-parse", "--git-common-dir").decode().strip()).resolve()
    locations = tuple((str(path), path.stat().st_dev, path.stat().st_ino) for path in (root, common))
    head = _git(root, "rev-parse", "--verify", "HEAD").decode().strip()
    if re.fullmatch(r"[a-f0-9]{40}|[a-f0-9]{64}", head) is None:
        raise ValueError("source snapshot has an unknown HEAD identity")
    if _git(root, "ls-files", "--others", "--exclude-standard", "-z"):
        raise ValueError("untracked task files block source snapshots")
    index = _git(root, "ls-files", "--stage", "-z")
    return (locations, head, hashlib.sha256(index).hexdigest()), index


def _capture_snapshot(root):
    from tools.validation_context import _index_entries
    from tools.validation_inputs import fingerprint_tree

    runtime = _runtime_identity()
    before, index = _repository_identity(root)
    environment = _digest(dict(os.environ))
    entries = _index_entries(index, root)
    roots = tuple(sorted({Path(name).parts[0] for _, name, _, _, _ in entries}))
    if _git(root, "ls-files", "--others", "-z", "--", *roots):
        raise ValueError("source snapshot contains untracked or ignored inputs")
    namespace = tuple((name, fingerprint_tree(root / name, (root,))) for name in roots)
    files, identities = {}, []
    for _, name, path, mode, resolved in entries:
        if mode is None:
            identities.append((name, "absent"))
            continue
        data = resolved.read_bytes()
        files[name] = data
        identities.append((
            name, stat.S_IMODE(mode),
            os.readlink(path) if stat.S_ISLNK(mode) else None,
            str(resolved.relative_to(root)), len(data), hashlib.sha256(data).hexdigest(),
        ))
    if (
        _repository_identity(root) != (before, index)
        or tuple((name, fingerprint_tree(root / name, (root,))) for name in roots) != namespace
        or _digest(dict(os.environ)) != environment
        or _runtime_identity() != runtime
    ):
        raise ValueError("source or environment changed during snapshot capture")
    return _Snapshot(
        _digest((before, identities, namespace, environment, runtime)),
        MappingProxyType(files),
    )


class _SourceView:
    def __init__(self, root, report):
        self.root = root
        self.pid = os.getpid()
        self.report = report
        self.snapshot = None
        self.opening = False
        self.closed = False
        self.failed = False

    def _owned(self):
        if self.closed or os.getpid() != self.pid:
            raise ValueError("source snapshot is closed or belongs to another process")
        if self.failed:
            raise ValueError("source snapshot failed to open")

    def _open(self):
        self._owned()
        if self.snapshot is not None:
            return
        if self.opening:
            raise ValueError("source snapshot is still opening")
        self.opening = True
        try:
            self.report("source-snapshot-start")
            self.snapshot = _capture_snapshot(self.root)
            self.report("source-snapshot-ready")
        except BaseException:
            self.failed = True
            self.snapshot = None
            raise
        finally:
            self.opening = False

    def source_bytes(self, path):
        self._open()
        try:
            name = str(Path(path).relative_to(self.root))
        except ValueError as error:
            raise ValueError("source read escapes its snapshot") from error
        if name not in self.snapshot.files:
            raise ValueError("source path is absent from the snapshot: " + name)
        return self.snapshot.files[name]

    def finish(self):
        self._owned()
        if self.snapshot is not None:
            self.report("source-snapshot-check")
            if _capture_snapshot(self.root).signature != self.snapshot.signature:
                raise ValueError("source snapshot inputs changed during execution")
            self.report("source-snapshot-closed")


@contextmanager
def checked_source_view(root, *, report=None):
    global _active

    if _active is not None:
        raise ValueError("a source snapshot already has an owner")
    view = _SourceView(Path(root).resolve(), report or (lambda phase: None))
    _active = view
    try:
        yield view
        view.finish()
    finally:
        view.closed = True
        _active = None


def source_bytes(root, path):
    root, path = Path(root).resolve(), Path(path)
    if _active is None:
        return path.read_bytes()
    _active._owned()
    if root != _active.root:
        raise ValueError("source read belongs to another snapshot root")
    return _active.source_bytes(path)


@contextmanager
def isolated_source_view():
    global _active

    previous, _active = _active, None
    try:
        yield
    finally:
        _active = previous
