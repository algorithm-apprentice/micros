"""Typed, test-owned stack evidence under Accepted ADR-0084."""

from dataclasses import dataclass
from enum import Enum
from pathlib import PurePosixPath
import re


MAX_U64 = (1 << 64) - 1


class StackEvidenceError(ValueError):
    pass


class EdgeKind(Enum):
    CALL = "call"
    TAIL = "tail"
    RESET = "reset"


@dataclass(frozen=True, order=True)
class FunctionKey:
    variant: str
    image: str
    translation_unit: str
    symbol: str


@dataclass(frozen=True)
class Frame:
    function: FunctionKey
    bytes_used: int
    classification: str = "static"


@dataclass(frozen=True)
class CallSite:
    caller: FunctionKey
    offset: int
    targets: tuple[FunctionKey, ...]
    kind: EdgeKind = EdgeKind.CALL


@dataclass(frozen=True)
class Root:
    identifier: str
    function: FunctionKey
    capacity: int
    initial_bytes: int = 0


@dataclass(frozen=True)
class Certificate:
    root: Root
    bytes_used: int
    bytes_remaining: int
    witness: tuple[FunctionKey, ...]


@dataclass(frozen=True)
class SymbolCall:
    caller: FunctionKey
    offset: int
    target_symbol: str | None
    kind: EdgeKind = EdgeKind.CALL


@dataclass(frozen=True)
class SourceOrigin:
    function: FunctionKey
    source: str
    line: int


@dataclass(frozen=True)
class ObjectEvidence:
    frames: tuple[Frame, ...]
    calls: tuple[SymbolCall, ...]
    object_sha256: str
    usage_sha256: str | None
    origins: tuple[SourceOrigin, ...] = ()


def require_uint64(value, name):
    if type(value) is not int or not 0 <= value <= MAX_U64:
        raise StackEvidenceError(f"{name}: expected uint64")


def checked_add(left, right):
    if right > MAX_U64 - left:
        raise StackEvidenceError("stack-bound arithmetic overflow")
    return left + right


def require_path(value):
    if (
        type(value) is not str or not value or "\\" in value
        or PurePosixPath(value).is_absolute()
        or not PurePosixPath(value).parts
        or str(PurePosixPath(value)) != value
        or any(part in (".", "..") for part in PurePosixPath(value).parts)
    ):
        raise StackEvidenceError("stack evidence path is not repository-relative")


def require_function(value):
    if type(value) is not FunctionKey:
        raise StackEvidenceError("invalid qualified function identity")
    require_path(value.image)
    require_path(value.translation_unit)
    if (
        type(value.variant) is not str
        or not re.fullmatch(r"[A-Za-z0-9_-]+", value.variant)
        or type(value.symbol) is not str
        or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.$]*", value.symbol)
    ):
        raise StackEvidenceError("invalid qualified function identity")
