"""Typed test-owned work observations and immutable source admission."""

from dataclasses import dataclass
from enum import Enum, IntEnum


class Owner(IntEnum):
    KERNEL_PLATFORM = 0
    KERNEL_DIAGNOSTIC = 1
    KERNEL_MEMORY = 2
    KERNEL_OBJECTS = 3
    KERNEL_SCHEDULER = 4
    KERNEL_ENDPOINT = 5
    KERNEL_IPC = 6
    KERNEL_GRANT = 7
    KERNEL_TRAP = 8
    KERNEL_EXECUTION = 9
    KERNEL_BOOT = 10
    KERNEL_VM = 11
    KERNEL_PM = 12
    KERNEL_TTY = 13
    RUNTIME = 14
    LIB_VM = 15
    LIB_SPAWN = 16
    VM_SERVICE = 17
    PM_SERVICE = 18
    TTY_SERVICE = 19
    RAMFS_SERVICE = 20
    VFS_SERVICE = 21
    APPLICATION_CLIENT = 22
    USER_COMMON = 23
    INIT = 24
    SHELL = 25
    COMMANDS = 26


class Kind(IntEnum):
    COMPARISON = 0
    RECORD_VISIT = 1
    INDEX_PROBE = 2
    LINK_TRAVERSE = 3
    BITMAP_WORD_LOAD = 4
    PTE_VISIT = 5
    QUEUE_RECORD_MOVE = 6
    COMPLETE_VALIDATOR_CALL = 7
    COPY_UNIT = 8
    COPIED_BYTE = 9
    CLEAR_UNIT = 10
    CLEARED_BYTE = 11
    RANGE_VISIT = 12
    HASH_MULTIPLY = 13


class Status(IntEnum):
    OK = 0
    ARGUMENT = 1
    STATE = 2
    OWNER = 3
    KIND = 4
    OVERFLOW = 5


class Stage(Enum):
    FIXTURE = "fixture"
    RED = "red"
    GREEN = "green"


class Placement(Enum):
    BEFORE = "before"
    AFTER = "after"
    WRAPS = "wraps"
    FUNCTION_ENTRY = "function-entry"


class WorkEvidenceError(ValueError):
    pass


@dataclass(frozen=True)
class Snapshot:
    producer_label: int
    capture_label: int
    owner_mask: int
    counts: tuple[tuple[int, ...], ...]
    version: int = 1
    reserved0: int = 0
    reserved1: int = 0


@dataclass(frozen=True)
class Capture:
    status: Status
    snapshot: Snapshot | None


@dataclass(frozen=True)
class Totals:
    per_owner: tuple[tuple[int, ...], ...]
    per_kind: tuple[int, ...]


@dataclass(frozen=True)
class Region:
    tokens: str
    occurrence: int = 0


@dataclass(frozen=True)
class Primitive:
    tokens: str
    occurrence: int = 0


@dataclass(frozen=True)
class ObservationSite:
    function: str
    site_id: str
    owner: Owner
    kind: Kind
    placement: Placement
    regions: tuple[Region, ...] = ()
    primitive: Primitive | None = None
    units: int = 1


@dataclass(frozen=True)
class SourceAdmission:
    source_path: str
    stage: Stage
    sites: tuple[ObservationSite, ...]
    include_header: bool = False
