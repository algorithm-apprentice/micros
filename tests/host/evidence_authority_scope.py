"""Invocation-owned source views; cold authority APIs remain independent."""

from contextlib import contextmanager
from dataclasses import asdict, dataclass
from functools import partial
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
from types import MappingProxyType


_active = None


@dataclass(frozen=True)
class VerifiedAuthority:
    retired_keys: frozenset
    checks: tuple


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


def _capture_snapshot(root):
    from tools.validation_context import _index_entries, capture_candidate
    from tools.validation_inputs import fingerprint_tree

    runtime = _runtime_identity()
    before = capture_candidate(root)
    environment = _digest(dict(os.environ))
    tracked = _git(root, "ls-files", "--stage", "-z")
    entries = _index_entries(tracked, root)
    roots = tuple(sorted({Path(name).parts[0] for _, name, _, _, _ in entries}))
    if _git(root, "ls-files", "--others", "-z", "--", *roots):
        raise ValueError("checked source namespace contains untracked or ignored inputs")
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
    namespace = tuple((name, fingerprint_tree(root / name, (root,))) for name in roots)
    if (
        capture_candidate(root) != before
        or _digest(dict(os.environ)) != environment
        or _runtime_identity() != runtime
    ):
        raise ValueError("source or environment changed during checked-view capture")
    return _Snapshot(
        _digest((asdict(before), identities, namespace, environment, runtime)),
        MappingProxyType(files),
    )


def _providers():
    from tests.host import (
        bootstrap_allocation_authority,
        diagnostic_history_authority,
        kernel_mapping_authority,
        runtime_memory_authority,
        selected_ownership_authority,
        selected_address_space_authority,
        selected_release_handoff_authority,
        object_hart_validation_authority,
    )

    return (
        runtime_memory_authority, diagnostic_history_authority,
        bootstrap_allocation_authority, kernel_mapping_authority,
        selected_ownership_authority, selected_address_space_authority,
        selected_release_handoff_authority,
        object_hart_validation_authority,
    )


def _keys(provider):
    if hasattr(provider, "MEMORY_IDENTITY"):
        return (provider.MEMORY_IDENTITY,)
    return provider.SOURCE_PROJECTION["retirement_keys"]


def _cold_identity_required(root, key):
    for provider in _providers():
        if key in _keys(provider):
            if hasattr(provider, "MEMORY_IDENTITY"):
                return provider.memory_identity_required(root)
            return provider.identity_required(root, key)
    return True


def _checked_attempt(provider, root, journal):
    from tools.stack_bound_manifest import owned_artifact

    directory = provider.journal_directory(root)
    if owned_artifact(directory, journal.attempt) != journal.directory or not journal.directory.is_dir():
        raise ValueError("checked source-view attempt is absent or outside its ordinary namespace")
    return directory


def _journal_inputs(provider, root, journal):
    from tools.stack_artifact_files import read_regular

    directory = _checked_attempt(provider, root, journal)
    selector = directory / "current.json"
    _, identity = read_regular(selector)
    expected = {
        "version": 1, "attempt": journal.attempt,
        "count": len(journal.records), "sha256": journal.sha256,
    }
    if provider.read_current(directory) != expected:
        raise ValueError("checked source-view journal selection changed")
    if hasattr(journal, "selector_sha256") and identity["sha256"] != journal.selector_sha256:
        raise ValueError("checked source-view selector bytes changed")
    result = [(str(selector), identity)]
    for index, (phase, record) in enumerate(zip(provider.PHASE_KINDS, journal.records)):
        path = journal.directory / (phase + ".json")
        _, identity = read_regular(path)
        expected_digest = (
            journal.records[index + 1]["previous"]
            if index + 1 < len(journal.records) else journal.sha256
        )
        if identity["sha256"] != expected_digest:
            raise ValueError("checked source-view record bytes changed")
        result.append((str(path), identity))
        for label, declared in record["logs"].items():
            path = journal.directory / (phase + "-" + label + ".log")
            _, identity = read_regular(path)
            if any(identity[field] != declared[field] for field in ("size", "sha256")):
                raise ValueError("checked source-view execution log changed")
            result.append((str(path), identity))
    if _checked_attempt(provider, root, journal) != directory:
        raise ValueError("checked source-view journal namespace changed")
    return tuple(result)


def _check_author_inputs(provider, context, journal, inputs):
    observed = provider.external_inputs(context.root)
    expected = {
        key: value for key, value in context.environment.immutable_inputs
        if key.startswith("@")
    }
    if provider.__name__ in (
        "tests.host.selected_ownership_authority", "tests.host.selected_address_space_authority",
        "tests.host.selected_release_handoff_authority",
        "tests.host.object_hart_validation_authority",
    ):
        changed = (
            len(observed) != 3 or observed[0] != context.native_compiler
            or observed[1] != context.native_nm or observed[2] != expected
        )
    else:
        changed = len(observed) != 2 or observed[0] != context.native_compiler or observed[1] != expected
    if changed:
        raise ValueError("checked source-view external inputs changed")
    if _journal_inputs(provider, context.root, journal) != inputs:
        raise ValueError("checked source-view journal inputs changed")


def _verify_authorities(root):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host.evidence_authority_types import TransitionState
    from tests.host.evidence_authority_validation import validate_transition
    from tools.stack_artifact_files import read_regular

    states, author_context = [], None
    for provider in _providers():
        owners = (provider.AUTHORITY_PATH,)
        if hasattr(provider, "SOURCE_PATH"):
            owners += (provider.SOURCE_PATH,)
        present = tuple((root / path).exists() or (root / path).is_symlink() for path in owners)
        if not any(present):
            continue
        if not all(present):
            raise ValueError("checked source-view authority has incomplete source owners")
        if provider.committed_claim(root) is not None:
            states.append((provider, None, None))
        else:
            directory = provider.journal_directory(root)
            selected = provider.read_current(directory)
            if selected is None or selected["count"] != 3:
                continue
            if author_context is not None:
                raise ValueError("checked source view cannot combine separate active author tasks")
            selector_identity = read_regular(directory / "current.json")[1]
            if provider.__name__.endswith(("runtime_memory_authority", "diagnostic_history_authority")):
                author_context = provider.capture_author_context(root)
            else:
                author_context = provider.capture_author_context(root, "recertify")
            states.append((provider, author_context, selector_identity))
    inventory = author_context.inventory if author_context is not None else current_inventory(root)
    retired, checks = set(), []
    for provider, context, selector_identity in states:
        if context is None:
            definition = provider.task_definition(root)
            if len(definition) == 3:
                authority, _, registry = definition
                authorities = {authority.task_id: authority}
            else:
                _, authorities, _, registry = definition
            result = validate_transition(inventory, authorities, registry, None, (), None)
            if not result.ok:
                raise ValueError("checked committed authority is invalid: " + str(result.error))
        else:
            journal, result = provider.read_verified_journal(context)
            if not result.ok or result.state is not TransitionState.RETIREMENT_READY:
                raise ValueError("checked source view requires complete terminal authority")
            inputs = _journal_inputs(provider, root, journal)
            if inputs[0][1] != selector_identity:
                raise ValueError("journal selection changed while opening the checked source view")
            _check_author_inputs(provider, context, journal, inputs)
            checks.append(partial(_check_author_inputs, provider, context, journal, inputs))
        retired.update(key for key in _keys(provider) if key not in result.active_identity_keys)
    return VerifiedAuthority(frozenset(retired), tuple(checks))


class _SourceView:
    def __init__(self, root, report):
        self.root = root
        self.pid = os.getpid()
        self.report = report
        self.snapshot = None
        self.proof = None
        self.opening = False
        self.closed = False
        self.failed = False

    def _owned(self):
        if self.closed or os.getpid() != self.pid:
            raise ValueError("checked source view is closed or belongs to another process")
        if self.failed:
            raise ValueError("checked source view already failed proof opening")

    def _open(self):
        self._owned()
        if self.snapshot is not None:
            return
        if self.opening:
            raise ValueError("source proof cannot authorize its own construction")
        self.opening = True
        try:
            self.report("source-proof-start")
            snapshot = _capture_snapshot(self.root)
            proof = _verify_authorities(self.root)
            if (
                type(proof) is not VerifiedAuthority
                or type(proof.retired_keys) is not frozenset
                or type(proof.checks) is not tuple
                or any(not callable(check) for check in proof.checks)
            ):
                raise ValueError("checked source proof has an invalid immutable result")
            if _capture_snapshot(self.root).signature != snapshot.signature:
                raise ValueError("source inputs changed before checked-view publication")
            self.proof, self.snapshot = proof, snapshot
            self.report("source-proof-ready")
        except BaseException:
            self.failed = True
            self.proof = self.snapshot = None
            raise
        finally:
            self.opening = False

    def identity_required(self, key):
        self._open()
        return key not in self.proof.retired_keys

    def source_bytes(self, path):
        self._open()
        try:
            name = str(Path(path).relative_to(self.root))
        except ValueError as error:
            raise ValueError("source read escapes its checked view") from error
        if name not in self.snapshot.files:
            raise ValueError("source path is absent from the checked view: " + name)
        return self.snapshot.files[name]

    def finish(self):
        self._owned()
        if self.snapshot is not None:
            self.report("source-proof-check")
            if _capture_snapshot(self.root).signature != self.snapshot.signature:
                raise ValueError("checked source-view inputs changed during execution")
            for check in self.proof.checks:
                check()
            if _capture_snapshot(self.root).signature != self.snapshot.signature:
                raise ValueError("checked source-view inputs changed during execution")
            self.report("source-proof-closed")


@contextmanager
def checked_source_view(root, *, report=None):
    global _active

    if _active is not None:
        raise ValueError("a checked source view already has an owner")
    view = _SourceView(Path(root).resolve(), report or (lambda phase: None))
    _active = view
    try:
        yield view
        view.finish()
    finally:
        view.closed = True
        _active = None


def _view(root):
    if _active is None:
        return None
    _active._owned()
    return _active if _active.root == root else None


def identity_required(root, key):
    root = Path(root).resolve()
    for provider in _providers():
        if key in _keys(provider) and provider._provisional is not None:
            return _cold_identity_required(root, key)
    view = _view(root)
    return view.identity_required(key) if view is not None else _cold_identity_required(root, key)


def memory_identity_required(root):
    from tests.host.runtime_memory_authority import MEMORY_IDENTITY

    return identity_required(root, MEMORY_IDENTITY)


def source_bytes(root, path):
    root, path = Path(root).resolve(), Path(path)
    view = _view(root)
    return view.source_bytes(path) if view is not None else path.read_bytes()
