"""Failure-atomic passive ELF representation publication under ADR-0085."""

import hashlib
import os
from pathlib import Path
import stat
import tempfile

from tools.stack_bound_images import require_passive_images
from tools.stack_bound_types import StackEvidenceError


def read_regular(path):
    path = Path(path)
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1:
        raise StackEvidenceError("artifact is not an unlinked regular file")
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(descriptor, "rb") as stream:
        opened = os.fstat(stream.fileno())
        if (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino):
            raise StackEvidenceError("artifact identity changed before reading")
        data = stream.read()
        after = os.fstat(stream.fileno())
    signature = lambda info: (
        info.st_dev, info.st_ino, info.st_mode, info.st_size, info.st_mtime_ns, info.st_nlink,
    )
    if signature(before) != signature(after) or signature(after) != signature(path.lstat()):
        raise StackEvidenceError("artifact changed while being read")
    return data, {
        "kind": "file", "mode": stat.S_IMODE(before.st_mode),
        "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
    }


def canonicalize_passive_file(ordinary, metadata, expected_ordinary):
    ordinary, metadata = Path(ordinary), Path(metadata)
    temporary = None
    try:
        if ordinary.resolve() == metadata.resolve():
            raise StackEvidenceError("ordinary and metadata artifacts are not distinct")
        normal, identity = read_regular(ordinary)
        if type(expected_ordinary) is not dict or identity != expected_ordinary:
            raise StackEvidenceError("ordinary artifact differs from its sealed identity")
        measured, before = read_regular(metadata)
        require_passive_images(normal, measured)
        with tempfile.NamedTemporaryFile(
            dir=metadata.parent, prefix=".stack-artifact-", delete=False,
        ) as stream:
            temporary = Path(stream.name)
            os.fchmod(stream.fileno(), before["mode"])
            stream.write(normal)
            stream.flush()
            os.fsync(stream.fileno())
        if read_regular(ordinary)[1] != identity or read_regular(metadata)[1] != before:
            raise StackEvidenceError("paired artifact changed before publication")
        os.replace(temporary, metadata)
        temporary = None
        descriptor = os.open(metadata.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
        expected = {**identity, "mode": before["mode"]}
        if read_regular(ordinary)[1] != identity or read_regular(metadata)[1] != expected:
            raise StackEvidenceError("artifact changed after canonical publication")
        return expected
    except (OSError, ValueError) as error:
        if isinstance(error, StackEvidenceError):
            raise
        raise StackEvidenceError(f"artifact canonicalization failed: {error}") from error
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
