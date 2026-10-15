"""Local exact-execution receipts and publication ownership (ADR-0064)."""

from dataclasses import dataclass
import fcntl
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import stat
from typing import BinaryIO, Optional
import uuid


TOOL_VERSION = "1"
IDENTITY_FIELDS = {"repository", "candidate", "context", "inputs", "outputs"}
RECEIPT_FIELDS = IDENTITY_FIELDS | {
    "schema", "workflow", "argv", "producer", "result", "log",
}
RECEIPT_LIMIT = 16 * 1024 * 1024


class EvidenceError(ValueError):
    pass


@dataclass
class Attempt:
    workflow: str
    identifier: str
    temporary: str
    log: BinaryIO
    recorded_log: Optional[dict] = None


def _encoded(value):
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), allow_nan=False,
    ).encode("utf-8")


def _unique_fields(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise EvidenceError("duplicate receipt field")
        value[key] = item
    return value


def _digest_file(descriptor):
    digest = hashlib.sha256()
    while chunk := os.read(descriptor, 65536):
        digest.update(chunk)
    return digest.hexdigest()


def _regular_open(directory, name):
    descriptor = os.open(name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=directory)
    if not stat.S_ISREG(os.fstat(descriptor).st_mode):
        os.close(descriptor)
        raise EvidenceError("evidence must be a regular file")
    return descriptor


def _directory(parent, name):
    try:
        os.mkdir(name, mode=0o700, dir_fd=parent)
    except FileExistsError:
        pass
    mode = os.stat(name, dir_fd=parent, follow_symlinks=False).st_mode
    if stat.S_ISLNK(mode):
        raise EvidenceError("symlink evidence directory is forbidden")
    if not stat.S_ISDIR(mode):
        raise EvidenceError("evidence directory is not a directory")
    return os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)


def _validate_record(record, workflow, identity):
    if type(record) is not dict or set(record) != RECEIPT_FIELDS:
        raise EvidenceError("unknown or missing receipt fields")
    if type(record["schema"]) is not int or record["schema"] != 1:
        raise EvidenceError("unsupported receipt schema")
    if record["workflow"] != workflow or record["argv"] != [
        "cmake", "--workflow", "--preset", workflow,
    ]:
        raise EvidenceError("receipt command does not match")
    for field in IDENTITY_FIELDS:
        if _encoded(record[field]) != _encoded(identity[field]):
            raise EvidenceError(f"{field} identity changed")
    producer = record["producer"]
    if type(producer) is not dict or set(producer) != {
        "run_id", "attempt_id", "tool_version",
    }:
        raise EvidenceError("invalid producer fields")
    if producer["tool_version"] != TOOL_VERSION or any(
        type(producer[name]) is not str
        or re.fullmatch(r"[a-f0-9]{32}", producer[name]) is None
        for name in ("run_id", "attempt_id")
    ):
        raise EvidenceError("unsupported or invalid producer")
    result = record["result"]
    if (
        type(result) is not dict
        or set(result) != {"exit_code", "postflight"}
        or type(result["exit_code"]) is not int
        or result["exit_code"] != 0
        or result["postflight"] is not True
    ):
        raise EvidenceError("receipt is not an actual successful postflight")
    log = record["log"]
    if type(log) is not dict or set(log) != {"path", "size", "mode", "sha256"}:
        raise EvidenceError("invalid log fields")
    if (
        log["path"] != f"logs/{producer['attempt_id']}.log"
        or type(log["size"]) is not int or log["size"] < 0
        or type(log["mode"]) is not int or log["mode"] != 0o600
        or type(log["sha256"]) is not str
        or re.fullmatch(r"[a-f0-9]{64}", log["sha256"]) is None
    ):
        raise EvidenceError("unsafe or incomplete log identity")


class EvidenceStore:
    def __init__(self, root, workflows):
        self.root = Path(root).resolve()
        self.path = self.root / "build/validation-evidence/v1"
        self.workflows = frozenset(workflows)
        if any(re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", name) is None for name in self.workflows):
            raise EvidenceError("invalid canonical workflow inventory")
        self.run_id = uuid.uuid4().hex
        self._descriptors = []
        self._active = {}
        self._locked = False

    def __enter__(self):
        try:
            parent = os.open(self.root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
            self._descriptors.append(parent)
            for name in ("build", "validation-evidence", "v1"):
                parent = _directory(parent, name)
                self._descriptors.append(parent)
            self.directory = parent
            self.logs = _directory(parent, "logs")
            self._descriptors.append(self.logs)
            self.lock = os.open(
                ".lock", os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW,
                0o600, dir_fd=self.directory,
            )
            self._descriptors.append(self.lock)
            try:
                fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise EvidenceError("validation evidence store is busy") from error
            self._locked = True
            self._assert_location()
            return self
        except (OSError, EvidenceError):
            self.__exit__(None, None, None)
            raise

    def __exit__(self, exception_type, exception, traceback):
        for attempt in self._active.values():
            if not attempt.log.closed:
                attempt.log.close()
            try:
                os.unlink(attempt.temporary, dir_fd=self.logs)
            except FileNotFoundError:
                pass
        self._active.clear()
        if self._locked:
            fcntl.flock(self.lock, fcntl.LOCK_UN)
            self._locked = False
        for descriptor in reversed(self._descriptors):
            os.close(descriptor)
        self._descriptors.clear()

    def _assert_location(self):
        current = self.root
        for name in ("build", "validation-evidence", "v1", "logs"):
            current /= name
            information = current.lstat()
            if not stat.S_ISDIR(information.st_mode):
                raise EvidenceError("evidence directory symlink/substitution rejected")
        for descriptor, path in (
            (self.directory, self.path), (self.logs, self.path / "logs"),
            (self.lock, self.path / ".lock"),
        ):
            if not os.path.samestat(os.fstat(descriptor), path.lstat()):
                raise EvidenceError("evidence store identity changed")
        if not self._locked:
            raise EvidenceError("evidence store lock is not held")

    def _workflow(self, workflow):
        if workflow not in self.workflows:
            raise EvidenceError("unknown canonical workflow")
        self._assert_location()

    def lookup(self, workflow, identity):
        self._workflow(workflow)
        if type(identity) is not dict or set(identity) != IDENTITY_FIELDS:
            raise EvidenceError("incomplete current execution identity")
        try:
            descriptor = _regular_open(self.directory, workflow + ".json")
        except FileNotFoundError:
            return None, "receipt absent"
        except (OSError, EvidenceError) as error:
            return None, f"receipt rejected: {error}"
        try:
            with os.fdopen(descriptor, "rb") as source:
                data = source.read(RECEIPT_LIMIT + 1)
            if len(data) > RECEIPT_LIMIT:
                raise EvidenceError("receipt exceeds the supported bound")
            record = json.loads(data, object_pairs_hook=_unique_fields)
            _validate_record(record, workflow, identity)
            self._verify_log(record["log"])
        except (OSError, UnicodeDecodeError, ValueError) as error:
            return None, f"receipt rejected: {error}"
        return record, "exact producer/log verified"

    def _verify_log(self, expected):
        descriptor = _regular_open(self.logs, expected["path"][5:])
        try:
            information = os.fstat(descriptor)
            if (
                information.st_size != expected["size"]
                or stat.S_IMODE(information.st_mode) != expected["mode"]
                or _digest_file(descriptor) != expected["sha256"]
            ):
                raise EvidenceError("complete log identity changed")
        finally:
            os.close(descriptor)

    def replay_log(self, expected, destination):
        self._assert_location()
        self._verify_log(expected)
        descriptor = _regular_open(self.logs, expected["path"][5:])
        with os.fdopen(descriptor, "rb") as source:
            with io.TextIOWrapper(source, encoding="utf-8", errors="replace") as text:
                shutil.copyfileobj(text, destination)

    def start(self, workflow):
        self._workflow(workflow)
        if any(attempt.workflow == workflow for attempt in self._active.values()):
            raise EvidenceError("canonical workflow attempt is already active")
        try:
            os.unlink(workflow + ".json", dir_fd=self.directory)
        except FileNotFoundError:
            pass
        os.fsync(self.directory)
        identifier = uuid.uuid4().hex
        temporary = identifier + ".tmp"
        descriptor = os.open(
            temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
            0o600, dir_fd=self.logs,
        )
        attempt = Attempt(workflow, identifier, temporary, os.fdopen(descriptor, "wb"))
        self._active[identifier] = attempt
        return attempt

    def _finish_log(self, attempt):
        attempt.log.flush()
        os.fsync(attempt.log.fileno())
        attempt.log.close()
        name = attempt.identifier + ".log"
        os.replace(attempt.temporary, name, src_dir_fd=self.logs, dst_dir_fd=self.logs)
        os.fsync(self.logs)
        descriptor = _regular_open(self.logs, name)
        try:
            information = os.fstat(descriptor)
            return {
                "path": "logs/" + name,
                "size": information.st_size,
                "mode": stat.S_IMODE(information.st_mode),
                "sha256": _digest_file(descriptor),
            }
        finally:
            os.close(descriptor)

    def _publish(self, workflow, record):
        temporary = workflow + "." + uuid.uuid4().hex + ".tmp"
        descriptor = os.open(
            temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
            0o600, dir_fd=self.directory,
        )
        try:
            with os.fdopen(descriptor, "wb") as destination:
                destination.write(_encoded(record) + b"\n")
                destination.flush()
                os.fsync(destination.fileno())
            self._assert_location()
            os.replace(
                temporary, workflow + ".json",
                src_dir_fd=self.directory, dst_dir_fd=self.directory,
            )
            os.fsync(self.directory)
        finally:
            try:
                os.unlink(temporary, dir_fd=self.directory)
            except FileNotFoundError:
                pass

    def complete(self, attempt, identity, exit_code, *, postflight=True):
        self._workflow(attempt.workflow)
        if self._active.get(attempt.identifier) is not attempt:
            raise EvidenceError("attempt is not owned by this publisher")
        log = self._finish_log(attempt)
        attempt.recorded_log = log
        del self._active[attempt.identifier]
        if not postflight:
            raise EvidenceError("execution postflight failed")
        if exit_code != 0 or identity is None:
            return None
        if type(identity) is not dict or set(identity) != IDENTITY_FIELDS:
            raise EvidenceError("incomplete successful execution identity")
        record = {
            **identity,
            "schema": 1,
            "workflow": attempt.workflow,
            "argv": ["cmake", "--workflow", "--preset", attempt.workflow],
            "producer": {
                "run_id": self.run_id,
                "attempt_id": attempt.identifier,
                "tool_version": TOOL_VERSION,
            },
            "result": {"exit_code": exit_code, "postflight": True},
            "log": log,
        }
        _validate_record(record, attempt.workflow, identity)
        self._publish(attempt.workflow, record)
        return record
