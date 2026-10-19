"""Fail-closed validation for ADR-0082 authority transitions."""

import re

from tests.host.evidence_authority_types import (
    AuthorityClass, AuthorityKind, Environment, ErrorCode, Event, EventKind,
    Fact, Family, Inventory, ObservedResult, Oracle, OracleResult, Plan,
    Provenance, RunStatus, TaskAuthority, TransitionClaim,
    TransitionRegistry, TransitionState, UnitResult, ValidationResult,
    event_digest, fact_set_digest, plan_digest, projection_digest,
    unit_projection_digest,
)

def _failed(error):
    return ValidationResult(False, error=error)


def _strings(value, *, empty=False):
    return (
        isinstance(value, tuple)
        and (empty or bool(value))
        and all(isinstance(item, str) and item for item in value)
    )


def _command_map(value):
    return (
        isinstance(value, tuple)
        and all(
            isinstance(item, tuple)
            and len(item) == 2
            and isinstance(item[0], str)
            and item[0]
            and _strings(item[1])
            for item in value
        )
    )


def _registry_schema_valid(registry):
    return (
        isinstance(registry, TransitionRegistry)
        and isinstance(registry.claims, tuple)
        and all(
            isinstance(claim, TransitionClaim)
            and _strings(claim.claimed_identity_keys)
            and _strings(claim.oracle_ids)
            and _strings(claim.retired_identity_keys, empty=True)
            for claim in registry.claims
        )
    )


def _schema_error(
    inventory, task_authorities, registry, plan, events, environment
):
    if (
        not isinstance(inventory, Inventory)
        or any(not isinstance(item, Family) for item in inventory.families)
        or any(not isinstance(item, Fact) for item in inventory.facts)
        or any(
            not isinstance(item.authority_class, AuthorityClass)
            for item in inventory.facts
        )
        or not _registry_schema_valid(registry)
    ):
        return ErrorCode.PLAN_SCHEMA
    if plan is None:
        return None if not events and environment is None else ErrorCode.PLAN_SCHEMA
    if (
        not isinstance(plan, Plan)
        or not isinstance(environment, Environment)
        or not isinstance(task_authorities, dict)
        or any(
            not isinstance(authority, TaskAuthority)
            for authority in task_authorities.values()
        )
        or any(
            not _strings(authority.finding_ids)
            or not _strings(authority.red_command)
            or not _strings(authority.green_command)
            or not _strings(authority.recertification_command)
            or not _command_map(authority.unit_commands)
            or not _strings(authority.command_input_paths)
            or not _command_map(authority.unit_input_paths)
            for authority in task_authorities.values()
        )
        or any(not isinstance(event, Event) for event in events)
        or any(
            not isinstance(oracle, Oracle)
            or not isinstance(oracle.authority_class, AuthorityClass)
            or not isinstance(oracle.provenance, Provenance)
            or not isinstance(oracle.provenance.kind, AuthorityKind)
            or not _strings(oracle.replaces_identity_keys)
            or not _strings(oracle.input_paths)
            for oracle in plan.replacement_oracles
        )
        or not _strings(plan.finding_ids)
        or not _strings(plan.affected_identity_keys)
        or not _strings(plan.retained_identity_keys, empty=True)
        or not _strings(plan.planned_retirement_identity_keys)
        or not _strings(plan.recertification_units)
        or not _strings(plan.declared_external_inputs)
        or not _strings(plan.red_command)
        or not _strings(plan.green_command)
        or not _strings(plan.recertification_command)
        or not _command_map(plan.unit_commands)
    ):
        return ErrorCode.PLAN_SCHEMA
    for event in events:
        if (
            not isinstance(event.kind, EventKind)
            or not isinstance(event.result, ObservedResult)
            or any(
                not isinstance(item, (OracleResult, UnitResult))
                or not isinstance(item.status, RunStatus)
                or not isinstance(item.observed, ObservedResult)
                for item in (*event.oracle_results, *event.unit_results)
            )
            or not _strings(event.command)
            or not _strings(event.suppressed_identity_keys, empty=True)
        ):
            return ErrorCode.PLAN_SCHEMA
    return None


def _validate_registry(plan, registry, facts):
    transition_ids = [claim.transition_id for claim in registry.claims]
    identity_keys = [
        key for claim in registry.claims
        for key in claim.claimed_identity_keys
    ]
    oracle_ids = [
        oracle for claim in registry.claims
        for oracle in claim.oracle_ids
    ]
    if (
        len(transition_ids) != len(set(transition_ids))
        or len(identity_keys) != len(set(identity_keys))
        or len(oracle_ids) != len(set(oracle_ids))
        or any(
            not set(claim.retired_identity_keys)
            <= set(claim.claimed_identity_keys)
            for claim in registry.claims
        )
        or any(key not in facts for key in identity_keys)
        or any(
            facts[key].authority_class
            is not AuthorityClass.TRANSPORT_IDENTITY
            for key in identity_keys
        )
    ):
        return ErrorCode.DUPLICATE_TRANSITION
    if plan is None:
        return None
    matches = [
        claim for claim in registry.claims
        if claim.transition_id == plan.transition_id
    ]
    if len(matches) != 1:
        return ErrorCode.PLAN_SCHEMA
    claim = matches[0]
    if (
        claim.owner_task != plan.owner_task
        or set(claim.claimed_identity_keys)
        != set(plan.planned_retirement_identity_keys)
        or set(claim.oracle_ids)
        != {oracle.oracle_id for oracle in plan.replacement_oracles}
    ):
        return ErrorCode.FINDING_OWNERSHIP
    prior_retired = {
        key for item in registry.claims
        if item.transition_id != plan.transition_id
        for key in item.retired_identity_keys
    }
    if prior_retired & set(plan.affected_identity_keys):
        return ErrorCode.IDENTITY_PARTITION
    return None


def validate_inventory(inventory):
    if not isinstance(inventory, Inventory):
        return _failed(ErrorCode.PLAN_SCHEMA)
    family_ids = [family.family_id for family in inventory.families]
    if (
        not family_ids
        or len(family_ids) != len(set(family_ids))
        or any(not family.owners for family in inventory.families)
    ):
        return _failed(ErrorCode.FAMILY_MEMBERSHIP)
    declared_owners = dict(inventory.declared_family_owners)
    if (
        set(declared_owners) != set(family_ids)
        or any(
            tuple(family.owners) != tuple(declared_owners[family.family_id])
            for family in inventory.families
        )
    ):
        return _failed(ErrorCode.FAMILY_MEMBERSHIP)
    registered = {owner for owners in declared_owners.values() for owner in owners}
    if registered != set(inventory.registered_owner_modules):
        return _failed(ErrorCode.FAMILY_MEMBERSHIP)
    if set(inventory.declared_identity_modules) - registered:
        return _failed(ErrorCode.FAMILY_MEMBERSHIP)
    if set(inventory.discovered_identity_modules) != (
        set(inventory.declared_identity_modules)
        | set(inventory.non_identity_exceptions)
    ):
        return _failed(ErrorCode.DISCOVERY_CLOSURE)
    known_families = set(family_ids)
    owners_by_family = {
        family.family_id: set(family.owners)
        for family in inventory.families
    }
    keys = set()
    for fact in inventory.facts:
        if fact.key in keys:
            return _failed(ErrorCode.DUPLICATE_FACT)
        keys.add(fact.key)
        if (
            fact.family not in known_families
            or fact.owner_path not in owners_by_family[fact.family]
            or not fact.key.startswith(f"{fact.owner_path}:")
            or not fact.primary_unit
            or not fact.consumer_units
            or not fact.payload_paths
            or not isinstance(fact.authority_class, AuthorityClass)
        ):
            return _failed(ErrorCode.FACT_AUTHORITY)
    if {fact.owner_path for fact in inventory.facts} != registered:
        return _failed(ErrorCode.FACT_AUTHORITY)
    declared_digests = dict(inventory.declared_family_fact_digests)
    if set(declared_digests) != known_families:
        return _failed(ErrorCode.FACT_AUTHORITY)
    for family in known_families:
        observed = fact_set_digest(
            fact for fact in inventory.facts if fact.family == family
        )
        if observed != declared_digests[family]:
            return _failed(ErrorCode.FACT_AUTHORITY)
    return ValidationResult(True)


def _fact_map(inventory):
    return {fact.key: fact for fact in inventory.facts}


def _b_unit(finding):
    prefix = finding.partition("-")[0]
    return prefix if len(prefix) == 3 and prefix[0] == "B" else ""


def _minimum_units(plan, facts):
    units = {plan.owner_unit, "B14"}
    units.update(filter(None, (_b_unit(item) for item in plan.finding_ids)))
    for key in plan.affected_identity_keys:
        fact = facts[key]
        units.add(fact.primary_unit)
        units.update(fact.consumer_units)
    return units


def _validate_plan(inventory, task_authorities, plan):
    facts = _fact_map(inventory)
    authority = task_authorities.get(plan.owner_task)
    if (
        authority is None
        or not plan.transition_id
        or not plan.owner_task
        or not plan.owner_unit
        or not plan.design_commit
        or not plan.design_tree
        or not plan.design_document
        or re.fullmatch(r"[0-9a-f]{40}", plan.design_commit) is None
        or re.fullmatch(r"[0-9a-f]{40}", plan.design_tree) is None
    ):
        return ErrorCode.PLAN_SCHEMA
    if (
        plan.owner_unit != authority.owner_unit
        or tuple(plan.finding_ids) != tuple(authority.finding_ids)
        or plan.design_commit != authority.design_commit
        or plan.design_tree != authority.design_tree
        or plan.design_document != authority.design_document
        or plan.recertification_units != authority.recertification_units
        or plan.red_command != authority.red_command
        or plan.green_command != authority.green_command
        or plan.recertification_command
        != authority.recertification_command
        or plan.unit_commands != authority.unit_commands
    ):
        return ErrorCode.FINDING_OWNERSHIP
    if (
        not plan.red_command
        or not plan.green_command
        or not plan.recertification_command
        or len({unit for unit, _ in plan.unit_commands})
        != len(plan.unit_commands)
        or {unit for unit, _ in plan.unit_commands}
        != set(plan.recertification_units)
        or any(not command for _, command in plan.unit_commands)
        or re.fullmatch(r"[0-9a-f]{40}", authority.base_head) is None
        or re.fullmatch(r"[0-9a-f]{40}", authority.base_tree) is None
    ):
        return ErrorCode.PLAN_SCHEMA
    affected = set(plan.affected_identity_keys)
    retained = set(plan.retained_identity_keys)
    retirement = set(plan.planned_retirement_identity_keys)
    if (
        len(affected) != len(plan.affected_identity_keys)
        or len(retained) != len(plan.retained_identity_keys)
        or len(retirement) != len(plan.planned_retirement_identity_keys)
        or not affected
        or affected != retained | retirement
        or retained & retirement
        or not retirement
        or not affected <= set(facts)
    ):
        return ErrorCode.IDENTITY_PARTITION
    if any(
        facts[key].authority_class is not AuthorityClass.TRANSPORT_IDENTITY
        for key in retirement
    ):
        return ErrorCode.IDENTITY_PARTITION
    oracle_ids = [oracle.oracle_id for oracle in plan.replacement_oracles]
    if (
        not oracle_ids
        or len(oracle_ids) != len(set(oracle_ids))
        or any(
            not oracle.test_id
            or not oracle.property
            or oracle.authority_class is not AuthorityClass.BASELINE_ORACLE
            or not _valid_provenance(
                oracle.provenance, oracle.input_paths
            )
            or not oracle.input_paths
            or len(set(oracle.replaces_identity_keys))
            != len(oracle.replaces_identity_keys)
            for oracle in plan.replacement_oracles
        )
    ):
        return ErrorCode.ORACLE_PROVENANCE
    coverage = {
        key
        for oracle in plan.replacement_oracles
        for key in oracle.replaces_identity_keys
    }
    if coverage != retirement or any(
        not set(oracle.replaces_identity_keys) <= retirement
        for oracle in plan.replacement_oracles
    ):
        return ErrorCode.ORACLE_COVERAGE
    minimum = _minimum_units(plan, facts)
    if (
        len(set(plan.recertification_units))
        != len(plan.recertification_units)
        or not minimum <= set(plan.recertification_units)
    ):
        return ErrorCode.RECERTIFICATION
    return None


def _valid_provenance(provenance, input_paths):
    if not isinstance(provenance, Provenance) or not isinstance(
        provenance.kind, AuthorityKind
    ):
        return False
    if provenance.kind is AuthorityKind.MINIX_TRACE:
        return (
            re.fullmatch(r"[0-9a-f]{40}", provenance.minix_commit) is not None
            and bool(provenance.path)
            and bool(provenance.symbol)
            and provenance.path in input_paths
        )
    if provenance.kind is AuthorityKind.ACCEPTED_ADAPTATION:
        return (
            provenance.adr.startswith("ADR-")
            and bool(provenance.section)
            and bool(provenance.path)
            and provenance.path in input_paths
        )
    if provenance.kind is AuthorityKind.INDEPENDENT_MODEL:
        return (
            bool(provenance.model_owner)
            and provenance.model_owner in input_paths
        )
    if provenance.kind is AuthorityKind.EXPLICIT_BOUND:
        return (
            bool(provenance.bound_reference)
            and bool(provenance.path)
            and provenance.path in input_paths
        )
    return False


def _expected_closures(inventory, plan, authority):
    facts = _fact_map(inventory)
    immutable = set(plan.declared_external_inputs)
    immutable.update(authority.command_input_paths)
    for oracle in plan.replacement_oracles:
        immutable.update(oracle.input_paths)
    candidate = set()
    for key in plan.affected_identity_keys:
        fact = facts[key]
        candidate.add(fact.owner_path)
        candidate.update(fact.payload_paths)
    for _, paths in authority.unit_input_paths:
        candidate.update(paths)
    return immutable, candidate


def _environment_maps(environment):
    return (
        dict(environment.immutable_inputs),
        dict(environment.red_candidate_inputs),
        dict(environment.current_candidate_inputs),
        {
            unit: dict(entries)
            for unit, entries in environment.unit_inputs
        },
    )


def _validate_environment(inventory, plan, authority, environment):
    immutable, candidate = _expected_closures(inventory, plan, authority)
    if immutable & candidate:
        return ErrorCode.PROJECTION
    immutable_map, red_map, current_map, unit_maps = _environment_maps(
        environment
    )
    if set(immutable_map) != immutable:
        return ErrorCode.PROJECTION
    if set(red_map) != candidate or set(current_map) != candidate:
        return ErrorCode.PROJECTION
    expected_unit_paths = dict(authority.unit_input_paths)
    if (
        set(unit_maps) != set(plan.recertification_units)
        or set(expected_unit_paths) != set(plan.recertification_units)
        or any(
            set(unit_maps[unit]) != set(expected_unit_paths[unit])
            for unit in plan.recertification_units
        )
    ):
        return ErrorCode.RECERTIFICATION
    if any(
        value != current_map[path]
        for inputs in unit_maps.values()
        for path, value in inputs.items()
    ):
        return ErrorCode.RECERTIFICATION
    if (
        re.fullmatch(r"[0-9a-f]{40}", environment.current_head) is None
        or re.fullmatch(r"[0-9a-f]{40}", environment.current_tree) is None
    ):
        return ErrorCode.PROJECTION
    return None


def _expected_projection(environment, which):
    immutable, red, current, units = _environment_maps(environment)
    selected = {"immutable": immutable, "red": red, "current": current}[which]
    return projection_digest(selected.items())


def _oracle_result_error(plan, event):
    expected_ids = {oracle.oracle_id for oracle in plan.replacement_oracles}
    results = event.oracle_results
    if (
        {item.oracle_id for item in results} != expected_ids
        or len(results) != len(expected_ids)
        or any(item.status is not RunStatus.RUN for item in results)
    ):
        return ErrorCode.ORACLE_RESULT
    expected = (
        ObservedResult.EXPECTED_FAILURE
        if event.kind is EventKind.RED_OBSERVED
        else ObservedResult.PASS
    )
    if event.result is not expected or not event.command:
        return ErrorCode.ORACLE_RESULT
    properties = {
        oracle.oracle_id: oracle.property
        for oracle in plan.replacement_oracles
    }
    if any(
        item.observed is not expected
        or item.expected_property != properties[item.oracle_id]
        for item in results
    ):
        return ErrorCode.ORACLE_RESULT
    return None


def _unit_result_error(plan, event, environment):
    if event.kind is not EventKind.AUTHOR_RECERTIFIED:
        return (
            None
            if not event.unit_results and not event.suppressed_identity_keys
            else ErrorCode.RECERTIFICATION
        )
    if set(event.suppressed_identity_keys) != set(
        plan.planned_retirement_identity_keys
    ) or len(event.suppressed_identity_keys) != len(
        plan.planned_retirement_identity_keys
    ):
        return ErrorCode.RECERTIFICATION
    results = event.unit_results
    if (
        {item.unit_id for item in results} != set(plan.recertification_units)
        or len(results) != len(plan.recertification_units)
    ):
        return ErrorCode.RECERTIFICATION
    unit_maps = _environment_maps(environment)[3]
    commands = dict(plan.unit_commands)
    for item in results:
        if (
            item.status is not RunStatus.RUN
            or item.observed is not ObservedResult.PASS
            or item.commands != commands[item.unit_id]
            or item.projection
            != unit_projection_digest(
                unit_maps[item.unit_id].items(),
                plan.planned_retirement_identity_keys,
            )
            or item.suppressed_identity_keys
            != plan.planned_retirement_identity_keys
        ):
            return ErrorCode.RECERTIFICATION
    return None


def validate_transition(
    inventory,
    task_authorities,
    registry,
    plan,
    events,
    environment,
):
    schema_error = _schema_error(
        inventory, task_authorities, registry, plan, events, environment
    )
    if schema_error is not None:
        return _failed(schema_error)
    inventory_result = validate_inventory(inventory)
    if not inventory_result.ok:
        return inventory_result
    if plan is None:
        error = _validate_registry(None, registry, _fact_map(inventory))
        if error is not None:
            return _failed(error)
        retired = {
            key for claim in registry.claims
            for key in claim.retired_identity_keys
        }
        return ValidationResult(
            True,
            state=TransitionState.TRANSPORT_ONLY,
            active_identity_keys=tuple(
                sorted(set(_fact_map(inventory)) - retired)
            ),
        )
    error = _validate_plan(inventory, task_authorities, plan)
    if error is not None:
        return _failed(error)
    error = _validate_registry(plan, registry, _fact_map(inventory))
    if error is not None:
        return _failed(error)
    authority = task_authorities[plan.owner_task]
    error = _validate_environment(
        inventory, plan, authority, environment
    )
    if error is not None:
        return _failed(error)
    if len(events) > 3:
        return _failed(ErrorCode.EVENT_ORDER)
    expected_kinds = (
        EventKind.RED_OBSERVED,
        EventKind.GREEN_OBSERVED,
        EventKind.AUTHOR_RECERTIFIED,
    )
    plan_value = plan_digest(plan)
    prior = ""
    immutable_projection = _expected_projection(environment, "immutable")
    red_projection = _expected_projection(environment, "red")
    current_projection = _expected_projection(environment, "current")
    for index, event in enumerate(events):
        if event.kind is not expected_kinds[index]:
            return _failed(ErrorCode.EVENT_ORDER)
        expected_command = (
            plan.red_command,
            plan.green_command,
            plan.recertification_command,
        )[index]
        if event.command != expected_command:
            return _failed(ErrorCode.EVENT_ORDER)
        expected_head = (
            authority.base_head
            if event.kind is EventKind.RED_OBSERVED
            else environment.current_head
        )
        expected_tree = (
            authority.base_tree
            if event.kind is EventKind.RED_OBSERVED
            else environment.current_tree
        )
        if (
            event.candidate_head != expected_head
            or event.candidate_tree != expected_tree
        ):
            return _failed(ErrorCode.PROJECTION)
        if event.plan_digest != plan_value:
            return _failed(ErrorCode.EVENT_CHAIN)
        if event.prior_event_digest != prior:
            return _failed(ErrorCode.EVENT_CHAIN)
        if event.event_digest != event_digest(event):
            return _failed(ErrorCode.EVENT_CHAIN)
        if event.immutable_oracle_projection != immutable_projection:
            return _failed(ErrorCode.PROJECTION)
        expected_candidate = (
            red_projection
            if event.kind is EventKind.RED_OBSERVED
            else current_projection
        )
        if event.candidate_projection != expected_candidate:
            return _failed(ErrorCode.PROJECTION)
        error = _oracle_result_error(plan, event)
        if error is not None:
            return _failed(error)
        error = _unit_result_error(plan, event, environment)
        if error is not None:
            return _failed(error)
        prior = event.event_digest
    previously_retired = {
        key for claim in registry.claims
        if claim.transition_id != plan.transition_id
        for key in claim.retired_identity_keys
    }
    active = set(_fact_map(inventory)) - previously_retired
    if len(events) == 3:
        current_claim = next(
            claim for claim in registry.claims
            if claim.transition_id == plan.transition_id
        )
        if (
            current_claim.retired_identity_keys
            != plan.planned_retirement_identity_keys
        ):
            return _failed(ErrorCode.DUPLICATE_TRANSITION)
        active -= set(plan.planned_retirement_identity_keys)
        return ValidationResult(
            True,
            state=TransitionState.RETIREMENT_READY,
            retired_identity_keys=plan.planned_retirement_identity_keys,
            active_identity_keys=tuple(sorted(active)),
        )
    state = (
        TransitionState.CORRECTION_ACTIVE
        if events
        else TransitionState.REPLACEMENT_REGISTERED
    )
    return ValidationResult(
        True,
        state=state,
        active_identity_keys=tuple(sorted(active)),
    )
