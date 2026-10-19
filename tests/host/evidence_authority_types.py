"""Fail-closed evidence-authority transitions for ADR-0082."""

from dataclasses import asdict, dataclass, fields, is_dataclass, replace
from enum import Enum
import hashlib
import json
import re


class AuthorityClass(str, Enum):
    TRANSPORT_IDENTITY = "TRANSPORT_IDENTITY"
    PERMANENT_INVARIANT = "PERMANENT_INVARIANT"
    BASELINE_ORACLE = "BASELINE_ORACLE"
    ARTIFACT_CONTRACT = "ARTIFACT_CONTRACT"


class AuthorityKind(str, Enum):
    MINIX_TRACE = "MINIX_TRACE"
    ACCEPTED_ADAPTATION = "ACCEPTED_ADAPTATION"
    INDEPENDENT_MODEL = "INDEPENDENT_MODEL"
    EXPLICIT_BOUND = "EXPLICIT_BOUND"


class EventKind(str, Enum):
    RED_OBSERVED = "RED_OBSERVED"
    GREEN_OBSERVED = "GREEN_OBSERVED"
    AUTHOR_RECERTIFIED = "AUTHOR_RECERTIFIED"


class RunStatus(str, Enum):
    RUN = "RUN"
    SKIPPED = "SKIPPED"
    NOT_RUN = "NOT_RUN"


class ObservedResult(str, Enum):
    EXPECTED_FAILURE = "EXPECTED_FAILURE"
    PASS = "PASS"
    FAIL = "FAIL"


class TransitionState(str, Enum):
    TRANSPORT_ONLY = "TRANSPORT_ONLY"
    REPLACEMENT_REGISTERED = "REPLACEMENT_REGISTERED"
    CORRECTION_ACTIVE = "CORRECTION_ACTIVE"
    RETIREMENT_READY = "RETIREMENT_READY"


class ErrorCode(str, Enum):
    FAMILY_MEMBERSHIP = "FAMILY_MEMBERSHIP"
    DISCOVERY_CLOSURE = "DISCOVERY_CLOSURE"
    DUPLICATE_FACT = "DUPLICATE_FACT"
    DUPLICATE_TRANSITION = "DUPLICATE_TRANSITION"
    FACT_AUTHORITY = "FACT_AUTHORITY"
    FINDING_OWNERSHIP = "FINDING_OWNERSHIP"
    PLAN_SCHEMA = "PLAN_SCHEMA"
    IDENTITY_PARTITION = "IDENTITY_PARTITION"
    ORACLE_COVERAGE = "ORACLE_COVERAGE"
    ORACLE_PROVENANCE = "ORACLE_PROVENANCE"
    RECERTIFICATION = "RECERTIFICATION"
    EVENT_ORDER = "EVENT_ORDER"
    EVENT_CHAIN = "EVENT_CHAIN"
    ORACLE_RESULT = "ORACLE_RESULT"
    PROJECTION = "PROJECTION"


@dataclass(frozen=True)
class Family:
    family_id: str
    owners: tuple[str, ...]


@dataclass(frozen=True)
class Fact:
    key: str
    family: str
    owner_path: str
    kind: str
    scope: str
    authority_class: AuthorityClass
    primary_unit: str
    consumer_units: tuple[str, ...]
    payload_paths: tuple[str, ...]
    evidence_value: str


@dataclass(frozen=True)
class Inventory:
    families: tuple[Family, ...]
    facts: tuple[Fact, ...]
    registered_owner_modules: tuple[str, ...]
    discovered_identity_modules: tuple[str, ...]
    declared_identity_modules: tuple[str, ...]
    non_identity_exceptions: tuple[str, ...]
    declared_family_owners: tuple[tuple[str, tuple[str, ...]], ...]
    declared_family_fact_digests: tuple[tuple[str, str], ...]


@dataclass(frozen=True)
class Provenance:
    kind: AuthorityKind
    minix_commit: str = ""
    path: str = ""
    symbol: str = ""
    adr: str = ""
    section: str = ""
    model_owner: str = ""
    bound_reference: str = ""


@dataclass(frozen=True)
class Oracle:
    oracle_id: str
    test_id: str
    property: str
    replaces_identity_keys: tuple[str, ...]
    authority_class: AuthorityClass
    provenance: Provenance
    input_paths: tuple[str, ...]


@dataclass(frozen=True)
class TaskAuthority:
    task_id: str
    owner_unit: str
    finding_ids: tuple[str, ...]
    design_commit: str
    design_tree: str
    design_document: str
    recertification_units: tuple[str, ...]
    red_command: tuple[str, ...]
    green_command: tuple[str, ...]
    recertification_command: tuple[str, ...]
    unit_commands: tuple[tuple[str, tuple[str, ...]], ...]
    command_input_paths: tuple[str, ...]
    unit_input_paths: tuple[tuple[str, tuple[str, ...]], ...]
    base_head: str
    base_tree: str


@dataclass(frozen=True)
class TransitionClaim:
    transition_id: str
    owner_task: str
    claimed_identity_keys: tuple[str, ...]
    oracle_ids: tuple[str, ...]
    retired_identity_keys: tuple[str, ...]


@dataclass(frozen=True)
class TransitionRegistry:
    claims: tuple[TransitionClaim, ...]


@dataclass(frozen=True)
class Plan:
    transition_id: str
    owner_task: str
    owner_unit: str
    finding_ids: tuple[str, ...]
    design_commit: str
    design_tree: str
    design_document: str
    affected_identity_keys: tuple[str, ...]
    retained_identity_keys: tuple[str, ...]
    planned_retirement_identity_keys: tuple[str, ...]
    replacement_oracles: tuple[Oracle, ...]
    recertification_units: tuple[str, ...]
    declared_external_inputs: tuple[str, ...]
    red_command: tuple[str, ...]
    green_command: tuple[str, ...]
    recertification_command: tuple[str, ...]
    unit_commands: tuple[tuple[str, tuple[str, ...]], ...]


@dataclass(frozen=True)
class OracleResult:
    oracle_id: str
    status: RunStatus
    observed: ObservedResult
    expected_property: str


@dataclass(frozen=True)
class UnitResult:
    unit_id: str
    status: RunStatus
    observed: ObservedResult
    commands: tuple[str, ...]
    projection: str
    suppressed_identity_keys: tuple[str, ...]


@dataclass(frozen=True)
class Event:
    kind: EventKind
    command: tuple[str, ...]
    result: ObservedResult
    expected_property: str
    oracle_results: tuple[OracleResult, ...]
    unit_results: tuple[UnitResult, ...]
    plan_digest: str
    prior_event_digest: str
    immutable_oracle_projection: str
    candidate_projection: str
    candidate_head: str
    candidate_tree: str
    suppressed_identity_keys: tuple[str, ...]
    event_digest: str


@dataclass(frozen=True)
class Environment:
    immutable_inputs: tuple[tuple[str, str], ...]
    red_candidate_inputs: tuple[tuple[str, str], ...]
    current_candidate_inputs: tuple[tuple[str, str], ...]
    unit_inputs: tuple[tuple[str, tuple[tuple[str, str], ...]], ...]
    current_head: str
    current_tree: str


@dataclass(frozen=True)
class ValidationResult:
    ok: bool
    state: TransitionState | None = None
    error: ErrorCode | None = None
    retired_identity_keys: tuple[str, ...] = ()
    active_identity_keys: tuple[str, ...] = ()


def _canonical(value):
    if isinstance(value, Enum):
        return value.value
    if is_dataclass(value):
        return {
            item.name: _canonical(getattr(value, item.name))
            for item in fields(value)
        }
    if isinstance(value, dict):
        return {
            str(key): _canonical(item)
            for key, item in sorted(value.items(), key=lambda pair: str(pair[0]))
        }
    if isinstance(value, (tuple, list)):
        return [_canonical(item) for item in value]
    return value


def digest(value):
    encoded = json.dumps(
        _canonical(value),
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=True,
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def projection_digest(entries):
    return digest(tuple(sorted(entries)))


def fact_set_digest(facts):
    return digest(tuple(sorted(
        (
            fact.key,
            fact.family,
            fact.owner_path,
            fact.kind,
            fact.scope,
            fact.authority_class.value,
            fact.primary_unit,
            tuple(sorted(fact.consumer_units)),
            tuple(sorted(fact.payload_paths)),
            fact.evidence_value,
        )
        for fact in facts
    )))


def unit_projection_digest(entries, suppressed_identity_keys):
    return digest({
        "inputs": tuple(sorted(entries)),
        "suppressed": tuple(sorted(suppressed_identity_keys)),
    })


def plan_digest(plan):
    return digest(plan)


def event_digest(event):
    payload = asdict(event)
    payload.pop("event_digest")
    return digest(payload)


def seal_event(event):
    return replace(event, event_digest=event_digest(event))
