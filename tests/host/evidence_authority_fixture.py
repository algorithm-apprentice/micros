"""Synthetic positive and negative authority-transition fixtures."""

from dataclasses import fields

from tests.host.evidence_authority_types import (
    AuthorityClass, AuthorityKind, Environment, Event, EventKind, Fact, Family,
    Inventory, ObservedResult, Oracle, OracleResult, Plan, RunStatus,
    Provenance, TaskAuthority, TransitionClaim, TransitionRegistry, UnitResult,
    event_digest, fact_set_digest, plan_digest, seal_event,
    unit_projection_digest,
)
from tests.host.evidence_authority_validation import (
    _environment_maps, _expected_projection,
)

def _make_event(
    kind,
    plan,
    environment,
    prior="",
):
    observed = (
        ObservedResult.EXPECTED_FAILURE
        if kind is EventKind.RED_OBSERVED
        else ObservedResult.PASS
    )
    oracle_results = tuple(
        OracleResult(
            oracle.oracle_id,
            RunStatus.RUN,
            observed,
            oracle.property,
        )
        for oracle in plan.replacement_oracles
    )
    unit_results = ()
    if kind is EventKind.AUTHOR_RECERTIFIED:
        unit_maps = _environment_maps(environment)[3]
        unit_results = tuple(
            UnitResult(
                unit,
                RunStatus.RUN,
                ObservedResult.PASS,
                dict(plan.unit_commands)[unit],
                unit_projection_digest(
                    unit_maps[unit].items(),
                    plan.planned_retirement_identity_keys,
                ),
                plan.planned_retirement_identity_keys,
            )
            for unit in plan.recertification_units
        )
    event = Event(
        kind=kind,
        command={
            EventKind.RED_OBSERVED: plan.red_command,
            EventKind.GREEN_OBSERVED: plan.green_command,
            EventKind.AUTHOR_RECERTIFIED: plan.recertification_command,
        }[kind],
        result=observed,
        expected_property=kind.value,
        oracle_results=oracle_results,
        unit_results=unit_results,
        plan_digest=plan_digest(plan),
        prior_event_digest=prior,
        immutable_oracle_projection=_expected_projection(
            environment,
            "immutable",
        ),
        candidate_projection=_expected_projection(
            environment,
            "red" if kind is EventKind.RED_OBSERVED else "current",
        ),
        candidate_head=(
            "3" * 40
            if kind is EventKind.RED_OBSERVED
            else environment.current_head
        ),
        candidate_tree=(
            "4" * 40
            if kind is EventKind.RED_OBSERVED
            else environment.current_tree
        ),
        suppressed_identity_keys=(
            plan.planned_retirement_identity_keys
            if kind is EventKind.AUTHOR_RECERTIFIED
            else ()
        ),
        event_digest="",
    )
    return seal_event(event)


def dataclass_replace(instance, **changes):
    values = {
        item.name: getattr(instance, item.name)
        for item in fields(instance)
    }
    values.update(changes)
    return type(instance)(**values)


def synthetic_inventory():
    facts = (
        Fact(
            "tests/host/synthetic.py:function-body:old",
            "synthetic",
            "tests/host/synthetic.py",
            "function-body",
            "old",
            AuthorityClass.TRANSPORT_IDENTITY,
            "B99",
            ("B98", "B99"),
            ("src/old.c", "tests/host/synthetic.py"),
            "old-digest",
        ),
        Fact(
            "tests/host/synthetic.py:function-body:unchanged",
            "synthetic",
            "tests/host/synthetic.py",
            "function-body",
            "unchanged",
            AuthorityClass.TRANSPORT_IDENTITY,
            "B99",
            ("B99",),
            ("src/unchanged.c", "tests/host/synthetic.py"),
            "unchanged-digest",
        ),
        Fact(
            "tests/host/synthetic.py:owner-boundary:module",
            "synthetic",
            "tests/host/synthetic.py",
            "owner-boundary",
            "module",
            AuthorityClass.PERMANENT_INVARIANT,
            "B99",
            ("B97", "B99"),
            ("tests/host/synthetic.py",),
            "",
        ),
        Fact(
            "tests/host/synthetic.py:function-body:previous",
            "synthetic",
            "tests/host/synthetic.py",
            "function-body",
            "previous",
            AuthorityClass.TRANSPORT_IDENTITY,
            "B99",
            ("B99",),
            ("src/previous.c", "tests/host/synthetic.py"),
            "previous-digest",
        ),
    )
    return Inventory(
        families=(Family("synthetic", ("tests/host/synthetic.py",)),),
        facts=facts,
        registered_owner_modules=("tests/host/synthetic.py",),
        discovered_identity_modules=("tests/host/synthetic.py",),
        declared_identity_modules=("tests/host/synthetic.py",),
        non_identity_exceptions=(),
        declared_family_owners=(
            ("synthetic", ("tests/host/synthetic.py",)),
        ),
        declared_family_fact_digests=(
            ("synthetic", fact_set_digest(facts)),
        ),
    )


def synthetic_plan(inventory):
    keys = {fact.scope: fact.key for fact in inventory.facts}
    return Plan(
        transition_id="FIX01-authority",
        owner_task="FIX01",
        owner_unit="B99",
        finding_ids=("B99-F001", "B98-F002"),
        design_commit="1" * 40,
        design_tree="2" * 40,
        design_document="docs/design.md",
        affected_identity_keys=(keys["old"], keys["unchanged"]),
        retained_identity_keys=(keys["unchanged"],),
        planned_retirement_identity_keys=(keys["old"],),
        replacement_oracles=(
            Oracle(
                "oracle-old",
                "tests.test_old",
                "old mechanism rejected",
                (keys["old"],),
                AuthorityClass.BASELINE_ORACLE,
                Provenance(
                    AuthorityKind.MINIX_TRACE,
                    minix_commit="5" * 40,
                    path="minix/kernel/example.c",
                    symbol="example",
                ),
                (
                    "minix/kernel/example.c",
                    "tests/oracle.py",
                    "tests/model.py",
                ),
            ),
        ),
        recertification_units=("B14", "B98", "B99"),
        declared_external_inputs=("docs/design.md",),
        red_command=("python3.15", "-m", "unittest", "red"),
        green_command=("python3.15", "-m", "unittest", "green"),
        recertification_command=(
            "python3.15",
            "-m",
            "unittest",
            "recertify",
        ),
        unit_commands=(
            ("B14", ("validate-b14",)),
            ("B98", ("validate-b98",)),
            ("B99", ("validate-b99",)),
        ),
    )


def synthetic_task_authorities(plan):
    return {
        plan.owner_task: TaskAuthority(
            plan.owner_task,
            plan.owner_unit,
            plan.finding_ids,
            plan.design_commit,
            plan.design_tree,
            plan.design_document,
            plan.recertification_units,
            plan.red_command,
            plan.green_command,
            plan.recertification_command,
            plan.unit_commands,
            ("tools/command_registry.py",),
            (
                ("B14", ("docs/b14.md",)),
                ("B98", ("docs/b98.md",)),
                ("B99", ("docs/b99.md",)),
            ),
            "3" * 40,
            "4" * 40,
        )
    }


def synthetic_transition_registry(plan, *, retired=False):
    return TransitionRegistry((
        TransitionClaim(
            plan.transition_id,
            plan.owner_task,
            plan.planned_retirement_identity_keys,
            tuple(
                oracle.oracle_id for oracle in plan.replacement_oracles
            ),
            (
                plan.planned_retirement_identity_keys
                if retired else ()
            ),
        ),
    ))


def synthetic_environment(plan):
    return Environment(
        immutable_inputs=(
            ("docs/design.md", "design"),
            ("minix/kernel/example.c", "minix-source"),
            ("tests/model.py", "model"),
            ("tests/oracle.py", "oracle"),
            ("tools/command_registry.py", "commands"),
        ),
        red_candidate_inputs=(
            ("docs/b14.md", "B14-ledger-before"),
            ("docs/b98.md", "B98-ledger-before"),
            ("docs/b99.md", "B99-ledger-before"),
            ("src/old.c", "old-before"),
            ("src/unchanged.c", "unchanged"),
            ("tests/host/synthetic.py", "before"),
        ),
        current_candidate_inputs=(
            ("docs/b14.md", "B14-ledger-after"),
            ("docs/b98.md", "B98-ledger-after"),
            ("docs/b99.md", "B99-ledger-after"),
            ("src/old.c", "old-after"),
            ("src/unchanged.c", "unchanged"),
            ("tests/host/synthetic.py", "after"),
        ),
        unit_inputs=(
            ("B14", (("docs/b14.md", "B14-ledger-after"),)),
            ("B98", (("docs/b98.md", "B98-ledger-after"),)),
            ("B99", (("docs/b99.md", "B99-ledger-after"),)),
        ),
        current_head="6" * 40,
        current_tree="7" * 40,
    )


def synthetic_red_event(plan, environment):
    return _make_event(EventKind.RED_OBSERVED, plan, environment)


def synthetic_green_event(plan, environment, red):
    return _make_event(
        EventKind.GREEN_OBSERVED,
        plan,
        environment,
        red.event_digest,
    )


def synthetic_recert_event(plan, environment, green):
    return _make_event(
        EventKind.AUTHOR_RECERTIFIED,
        plan,
        environment,
        green.event_digest,
    )
