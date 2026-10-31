"""Fail-closed composition of independently declared work producers."""

from tests.host.work_count_types import (
    Capture, Kind, Owner, Snapshot, Status, Totals, WorkEvidenceError,
)


MAX_U64 = (1 << 64) - 1
OWNER_MASK = (1 << len(Owner)) - 1


def require_uint(value, name, *, positive=False):
    if type(value) is not int or not 0 <= value <= MAX_U64:
        raise WorkEvidenceError(f"{name}: expected uint64")
    if positive and value == 0:
        raise WorkEvidenceError(f"{name}: expected nonzero value")


def require_mask(value):
    require_uint(value, "owner mask", positive=True)
    if value & ~OWNER_MASK:
        raise WorkEvidenceError("owner mask contains unknown owners")


def validate_snapshot(snapshot, capture_label, producer, expected_mask):
    if type(snapshot) is not Snapshot:
        raise WorkEvidenceError("missing complete snapshot")
    for value, name in (
        (snapshot.version, "version"), (snapshot.reserved0, "reserved0"),
        (snapshot.reserved1, "reserved1"),
    ):
        require_uint(value, name)
    if snapshot.version != 1 or snapshot.reserved0 or snapshot.reserved1:
        raise WorkEvidenceError("unsupported snapshot version/reserved fields")
    require_uint(snapshot.producer_label, "producer", positive=True)
    require_uint(snapshot.capture_label, "capture", positive=True)
    require_mask(snapshot.owner_mask)
    if (
        snapshot.producer_label != producer
        or snapshot.capture_label != capture_label
        or snapshot.owner_mask != expected_mask
    ):
        raise WorkEvidenceError("snapshot provenance does not match declaration")
    if type(snapshot.counts) is not tuple or len(snapshot.counts) != len(Owner):
        raise WorkEvidenceError("incomplete owner vector")
    for owner, row in enumerate(snapshot.counts):
        if type(row) is not tuple or len(row) != len(Kind):
            raise WorkEvidenceError("incomplete work-kind vector")
        for value in row:
            require_uint(value, "count")
        if not expected_mask & (1 << owner) and any(row):
            raise WorkEvidenceError("unselected owner has nonzero work")


def declared_snapshots(captures, capture_label, expected_producers):
    require_uint(capture_label, "expected capture", positive=True)
    if type(expected_producers) is not dict or not 1 <= len(expected_producers) <= 65:
        raise WorkEvidenceError("invalid expected producer set")
    for producer, mask in expected_producers.items():
        require_uint(producer, "expected producer", positive=True)
        require_mask(mask)
    if type(captures) not in (tuple, list) or len(captures) != len(expected_producers):
        raise WorkEvidenceError("incomplete producer set")
    snapshots = []
    seen = set()
    for capture in captures:
        if type(capture) is not Capture or type(capture.status) is not Status:
            raise WorkEvidenceError("invalid capture status")
        if capture.status is not Status.OK:
            raise WorkEvidenceError("failed capture cannot be composed")
        if type(capture.snapshot) is not Snapshot:
            raise WorkEvidenceError("missing successful snapshot")
        producer = capture.snapshot.producer_label
        require_uint(producer, "producer", positive=True)
        if producer in seen:
            raise WorkEvidenceError("duplicate producer")
        if producer not in expected_producers:
            raise WorkEvidenceError("unexpected producer")
        validate_snapshot(
            capture.snapshot, capture_label, producer, expected_producers[producer],
        )
        seen.add(producer)
        snapshots.append(capture.snapshot)
    if seen != set(expected_producers):
        raise WorkEvidenceError("missing producer")
    return snapshots


def checked_add(left, right):
    if right > MAX_U64 - left:
        raise WorkEvidenceError("work composition overflow")
    return left + right


def compose_snapshots(captures, *, capture_label, expected_producers):
    snapshots = declared_snapshots(captures, capture_label, expected_producers)
    per_owner = [[0] * len(Kind) for _ in Owner]
    per_kind = [0] * len(Kind)
    for snapshot in snapshots:
        for owner, row in enumerate(snapshot.counts):
            for kind, value in enumerate(row):
                per_owner[owner][kind] = checked_add(per_owner[owner][kind], value)
                per_kind[kind] = checked_add(per_kind[kind], value)
    return Totals(tuple(tuple(row) for row in per_owner), tuple(per_kind))
