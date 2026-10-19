import dataclasses
from pathlib import Path
import unittest

from tests.host import evidence_authority
from tests.host import evidence_authority_inventory


ROOT = Path(__file__).resolve().parents[2]


class InventoryTests(unittest.TestCase):
    def test_current_authority_inventory_is_closed_and_unique(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)

        result = evidence_authority.validate_inventory(inventory)

        self.assertTrue(result.ok, result)
        self.assertEqual(6, len(inventory.families))
        self.assertGreater(len(inventory.facts), 30)
        self.assertTrue(any(
            fact.authority_class
            is evidence_authority.AuthorityClass.ARTIFACT_CONTRACT
            for fact in inventory.facts
        ))
        self.assertEqual(
            set(inventory.discovered_identity_modules),
            set(inventory.declared_identity_modules)
            | set(inventory.non_identity_exceptions),
        )
        self.assertTrue(any(
            "KERNEL_FILE_FACTS/kernel/address_space.c/" in fact.scope
            and "kernel/memory/address_space.c" in fact.payload_paths
            and "B02" in fact.consumer_units
            for fact in inventory.facts
        ))

    def test_missing_family_owner_fails_before_fact_validation(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        family = inventory.families[0]
        changed = dataclasses.replace(
            inventory,
            families=(
                dataclasses.replace(family, owners=family.owners[:-1]),
                *inventory.families[1:],
            ),
        )

        result = evidence_authority.validate_inventory(changed)

        self.assertEqual(
            evidence_authority.ErrorCode.FAMILY_MEMBERSHIP,
            result.error,
        )

    def test_simultaneous_owner_and_fact_deletion_still_fails(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        removed = "tests/host/test_vfs_source_layout.py"
        families = tuple(
            dataclasses.replace(
                family,
                owners=tuple(
                    owner for owner in family.owners if owner != removed
                ),
            )
            for family in inventory.families
        )
        changed = dataclasses.replace(
            inventory,
            families=families,
            facts=tuple(
                fact for fact in inventory.facts
                if fact.owner_path != removed
            ),
            registered_owner_modules=tuple(
                owner for owner in inventory.registered_owner_modules
                if owner != removed
            ),
        )

        result = evidence_authority.validate_inventory(changed)

        self.assertEqual(
            evidence_authority.ErrorCode.FAMILY_MEMBERSHIP,
            result.error,
        )

    def test_duplicate_fact_across_families_is_rejected(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        duplicate = dataclasses.replace(
            inventory.facts[0],
            family=inventory.families[-1].family_id,
        )

        result = evidence_authority.validate_inventory(
            dataclasses.replace(
                inventory,
                facts=(*inventory.facts, duplicate),
            )
        )

        self.assertEqual(
            evidence_authority.ErrorCode.DUPLICATE_FACT,
            result.error,
        )

    def test_unknown_candidate_module_is_not_an_implicit_exception(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        changed = dataclasses.replace(
            inventory,
            discovered_identity_modules=(
                *inventory.discovered_identity_modules,
                "tests/host/test_unknown_authority.py",
            ),
        )

        result = evidence_authority.validate_inventory(changed)

        self.assertEqual(
            evidence_authority.ErrorCode.DISCOVERY_CLOSURE,
            result.error,
        )

    def test_fact_cannot_move_to_an_unrelated_family(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        fact = next(
            item for item in inventory.facts
            if item.family == "kernel-structure"
        )
        changed = dataclasses.replace(
            inventory,
            facts=tuple(
                dataclasses.replace(item, family="service-structure")
                if item.key == fact.key else item
                for item in inventory.facts
            ),
        )

        result = evidence_authority.validate_inventory(changed)

        self.assertEqual(
            evidence_authority.ErrorCode.FACT_AUTHORITY,
            result.error,
        )

    def test_extra_fact_requires_an_independent_declaration(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        extra = dataclasses.replace(
            inventory.facts[0],
            key="tests/host/ast_evidence.py:owner-boundary:extra",
            scope="extra",
        )

        result = evidence_authority.validate_inventory(
            dataclasses.replace(
                inventory,
                facts=(*inventory.facts, extra),
            )
        )

        self.assertEqual(
            evidence_authority.ErrorCode.FACT_AUTHORITY,
            result.error,
        )

    def test_digest_payload_cannot_be_rebaselined_silently(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        fact = next(item for item in inventory.facts if item.evidence_value)
        changed = dataclasses.replace(
            inventory,
            facts=tuple(
                dataclasses.replace(item, evidence_value="0" * 64)
                if item.key == fact.key else item
                for item in inventory.facts
            ),
        )

        result = evidence_authority.validate_inventory(changed)

        self.assertEqual(
            evidence_authority.ErrorCode.FACT_AUTHORITY,
            result.error,
        )

    def test_cross_unit_fact_metadata_is_materialized(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        fact = next(
            item for item in inventory.facts
            if item.owner_path
            == "tests/host/test_kernel_vm_handoff_source_layout.py"
        )

        self.assertEqual("B14", fact.primary_unit)
        self.assertGreaterEqual(
            set(fact.consumer_units),
            {"B02", "B07", "B14"},
        )
        ramfs = next(
            item for item in inventory.facts
            if "BASELINE_BODIES/0/micros_ramfs_handle_call"
            in item.scope
        )
        vm_classification = next(
            item for item in inventory.facts
            if "KERNEL_CLASSIFICATION_FACTS/kernel/vm_handoff_core.c"
            in item.scope
        )
        self.assertIn("servers/ramfs/ramfs_core.c", ramfs.payload_paths)
        self.assertEqual("B10", ramfs.primary_unit)
        self.assertGreaterEqual(set(ramfs.consumer_units), {"B10", "B14"})
        self.assertIn(
            "kernel/vm/vm_handoff_core.c",
            vm_classification.payload_paths,
        )
        self.assertEqual("B07", vm_classification.primary_unit)
        self.assertGreaterEqual(
            set(vm_classification.consumer_units),
            {"B02", "B07", "B14"},
        )


class TransitionTests(unittest.TestCase):
    def setUp(self):
        self.inventory = evidence_authority.synthetic_inventory()
        self.task_findings = {"FIX01": ("B99-F001", "B98-F002")}
        self.plan = evidence_authority.synthetic_plan(self.inventory)
        self.task_authorities = (
            evidence_authority.synthetic_task_authorities(self.plan)
        )
        self.registry = evidence_authority.synthetic_transition_registry(
            self.plan
        )
        self.environment = evidence_authority.synthetic_environment(self.plan)

    def validate(
        self,
        plan=None,
        events=(),
        task_authorities=None,
        registry=None,
    ):
        return evidence_authority.validate_transition(
            self.inventory,
            task_authorities or self.task_authorities,
            registry or self.registry,
            plan or self.plan,
            events,
            self.environment,
        )

    def test_registered_plan_derives_replacement_registered(self):
        result = self.validate()

        self.assertTrue(result.ok, result)
        self.assertEqual(
            evidence_authority.TransitionState.REPLACEMENT_REGISTERED,
            result.state,
        )

    def test_inventory_without_plan_is_transport_only(self):
        result = evidence_authority.validate_transition(
            self.inventory,
            self.task_authorities,
            self.registry,
            None,
            (),
            None,
        )

        self.assertTrue(result.ok, result)
        self.assertEqual(
            evidence_authority.TransitionState.TRANSPORT_ONLY,
            result.state,
        )

    def test_no_plan_preserves_recorded_retirements(self):
        registry = evidence_authority.synthetic_transition_registry(
            self.plan, retired=True
        )

        result = evidence_authority.validate_transition(
            self.inventory,
            self.task_authorities,
            registry,
            None,
            (),
            None,
        )

        self.assertTrue(result.ok, result)
        self.assertEqual(
            {fact.key for fact in self.inventory.facts}
            - set(self.plan.planned_retirement_identity_keys),
            set(result.active_identity_keys),
        )

    def test_no_plan_rejects_duplicate_transition_claims(self):
        registry = evidence_authority.TransitionRegistry(
            (*self.registry.claims, *self.registry.claims)
        )

        result = evidence_authority.validate_transition(
            self.inventory,
            self.task_authorities,
            registry,
            None,
            (),
            None,
        )

        self.assertEqual(
            evidence_authority.ErrorCode.DUPLICATE_TRANSITION,
            result.error,
        )

    def test_plan_requires_complete_task_findings(self):
        changed = dataclasses.replace(
            self.plan,
            finding_ids=("B99-F001",),
        )

        result = self.validate(plan=changed)

        self.assertEqual(
            evidence_authority.ErrorCode.FINDING_OWNERSHIP,
            result.error,
        )

    def test_plan_must_match_external_design_authority(self):
        changed = dataclasses.replace(
            self.plan,
            design_commit="3" * 40,
        )

        result = self.validate(plan=changed)

        self.assertEqual(
            evidence_authority.ErrorCode.FINDING_OWNERSHIP,
            result.error,
        )

    def test_plan_cannot_self_declare_commands(self):
        changed = dataclasses.replace(
            self.plan,
            red_command=("true",),
            green_command=("true",),
            recertification_command=("true",),
        )

        result = self.validate(plan=changed)

        self.assertEqual(
            evidence_authority.ErrorCode.FINDING_OWNERSHIP,
            result.error,
        )

    def test_registry_rejects_cross_transition_duplicate_ownership(self):
        current = self.registry.claims[0]
        duplicate = dataclasses.replace(
            current,
            transition_id="FIX02-authority",
            owner_task="FIX02",
        )
        registry = evidence_authority.TransitionRegistry(
            (current, duplicate)
        )

        result = evidence_authority.validate_transition(
            self.inventory,
            self.task_authorities,
            registry,
            self.plan,
            (),
            self.environment,
        )

        self.assertEqual(
            evidence_authority.ErrorCode.DUPLICATE_TRANSITION,
            result.error,
        )

    def test_plan_rejects_duplicate_identity_partition(self):
        changed = dataclasses.replace(
            self.plan,
            affected_identity_keys=(
                *self.plan.affected_identity_keys,
                self.plan.affected_identity_keys[0],
            ),
        )

        result = self.validate(plan=changed)

        self.assertEqual(
            evidence_authority.ErrorCode.IDENTITY_PARTITION,
            result.error,
        )

    def test_planned_retirement_is_bound_to_oracle_coverage(self):
        oracle = dataclasses.replace(
            self.plan.replacement_oracles[0],
            replaces_identity_keys=self.plan.retained_identity_keys,
        )
        changed = dataclasses.replace(
            self.plan,
            replacement_oracles=(oracle,),
        )

        result = self.validate(plan=changed)

        self.assertEqual(
            evidence_authority.ErrorCode.ORACLE_COVERAGE,
            result.error,
        )

    def test_replacement_oracle_must_have_baseline_authority(self):
        oracle = dataclasses.replace(
            self.plan.replacement_oracles[0],
            authority_class=(
                evidence_authority.AuthorityClass.TRANSPORT_IDENTITY
            ),
        )
        changed = dataclasses.replace(
            self.plan,
            replacement_oracles=(oracle,),
        )

        result = self.validate(plan=changed)

        self.assertEqual(
            evidence_authority.ErrorCode.ORACLE_PROVENANCE,
            result.error,
        )

    def test_candidate_derived_oracle_authority_is_rejected(self):
        oracle = dataclasses.replace(
            self.plan.replacement_oracles[0],
            provenance=dataclasses.replace(
                self.plan.replacement_oracles[0].provenance,
                kind="CANDIDATE_OUTPUT",
            ),
        )

        result = self.validate(
            plan=dataclasses.replace(
                self.plan,
                replacement_oracles=(oracle,),
            )
        )

        self.assertEqual(
            evidence_authority.ErrorCode.PLAN_SCHEMA,
            result.error,
        )

    def test_schema_failure_precedes_invalid_inventory(self):
        inventory = dataclasses.replace(
            self.inventory,
            facts=(),
        )
        oracle = dataclasses.replace(
            self.plan.replacement_oracles[0],
            provenance=dataclasses.replace(
                self.plan.replacement_oracles[0].provenance,
                kind="INVALID",
            ),
        )

        result = evidence_authority.validate_transition(
            inventory,
            self.task_authorities,
            self.registry,
            dataclasses.replace(
                self.plan,
                replacement_oracles=(oracle,),
            ),
            (),
            self.environment,
        )

        self.assertEqual(
            evidence_authority.ErrorCode.PLAN_SCHEMA,
            result.error,
        )

    def test_oracle_and_candidate_closures_must_be_disjoint(self):
        oracle = dataclasses.replace(
            self.plan.replacement_oracles[0],
            input_paths=("minix/kernel/example.c", "src/old.c"),
        )
        plan = dataclasses.replace(
            self.plan,
            replacement_oracles=(oracle,),
        )
        environment = dataclasses.replace(
            self.environment,
            immutable_inputs=(
                ("docs/design.md", "design"),
                ("minix/kernel/example.c", "minix-source"),
                ("src/old.c", "oracle"),
                ("tools/command_registry.py", "commands"),
            ),
        )

        result = evidence_authority.validate_transition(
            self.inventory,
            self.task_authorities,
            self.registry,
            plan,
            (),
            environment,
        )

        self.assertEqual(
            evidence_authority.ErrorCode.PROJECTION,
            result.error,
        )

    def test_event_candidate_identity_is_not_caller_selectable(self):
        red = evidence_authority.synthetic_red_event(
            self.plan,
            self.environment,
        )
        changed = evidence_authority.seal_event(
            dataclasses.replace(red, candidate_head="8" * 40)
        )

        result = self.validate(events=(changed,))

        self.assertEqual(
            evidence_authority.ErrorCode.PROJECTION,
            result.error,
        )

    def test_plan_cannot_omit_derived_recertification_unit(self):
        changed = dataclasses.replace(
            self.plan,
            recertification_units=("B14", "B99"),
            unit_commands=(
                ("B14", ("validate-b14",)),
                ("B99", ("validate-b99",)),
            ),
        )

        authority = dataclasses.replace(
            self.task_authorities["FIX01"],
            recertification_units=changed.recertification_units,
            unit_commands=changed.unit_commands,
        )
        result = self.validate(
            plan=changed,
            task_authorities={"FIX01": authority},
        )

        self.assertEqual(
            evidence_authority.ErrorCode.RECERTIFICATION,
            result.error,
        )

    def test_red_requires_every_oracle_to_run_and_fail_as_expected(self):
        red = evidence_authority.synthetic_red_event(
            self.plan,
            self.environment,
        )
        changed_result = dataclasses.replace(
            red.oracle_results[0],
            status=evidence_authority.RunStatus.NOT_RUN,
        )
        changed = evidence_authority.seal_event(
            dataclasses.replace(
                red,
                oracle_results=(changed_result,),
            )
        )

        result = self.validate(events=(changed,))

        self.assertEqual(
            evidence_authority.ErrorCode.ORACLE_RESULT,
            result.error,
        )

    def test_red_event_derives_correction_active(self):
        red = evidence_authority.synthetic_red_event(
            self.plan,
            self.environment,
        )

        result = self.validate(events=(red,))

        self.assertTrue(result.ok, result)
        self.assertEqual(
            evidence_authority.TransitionState.CORRECTION_ACTIVE,
            result.state,
        )

    def test_green_cannot_skip_red(self):
        green = evidence_authority.synthetic_green_event(
            self.plan,
            self.environment,
            evidence_authority.synthetic_red_event(
                self.plan,
                self.environment,
            ),
        )

        result = self.validate(events=(green,))

        self.assertEqual(
            evidence_authority.ErrorCode.EVENT_ORDER,
            result.error,
        )

    def test_green_rejects_oracle_input_drift(self):
        red = evidence_authority.synthetic_red_event(
            self.plan,
            self.environment,
        )
        green = evidence_authority.synthetic_green_event(
            self.plan,
            self.environment,
            red,
        )
        changed = evidence_authority.seal_event(
            dataclasses.replace(
                green,
                immutable_oracle_projection="0" * 64,
            )
        )

        result = self.validate(events=(red, changed))

        self.assertEqual(
            evidence_authority.ErrorCode.PROJECTION,
            result.error,
        )

    def test_recertification_requires_every_derived_unit(self):
        red = evidence_authority.synthetic_red_event(
            self.plan,
            self.environment,
        )
        green = evidence_authority.synthetic_green_event(
            self.plan,
            self.environment,
            red,
        )
        recert = evidence_authority.synthetic_recert_event(
            self.plan,
            self.environment,
            green,
        )
        changed = evidence_authority.seal_event(
            dataclasses.replace(
                recert,
                unit_results=recert.unit_results[:-1],
            )
        )

        result = self.validate(events=(red, green, changed))

        self.assertEqual(
            evidence_authority.ErrorCode.RECERTIFICATION,
            result.error,
        )

    def test_recertification_inputs_match_current_candidate_values(self):
        authority = self.task_authorities["FIX01"]
        authority = dataclasses.replace(
            authority,
            unit_input_paths=tuple(
                (unit, (*paths, "src/old.c")) if unit == "B99"
                else (unit, paths)
                for unit, paths in authority.unit_input_paths
            ),
        )
        environment = dataclasses.replace(
            self.environment,
            unit_inputs=tuple(
                (unit, (*inputs, ("src/old.c", "old-before")))
                if unit == "B99" else (unit, inputs)
                for unit, inputs in self.environment.unit_inputs
            ),
        )
        red = evidence_authority.synthetic_red_event(self.plan, environment)
        green = evidence_authority.synthetic_green_event(
            self.plan, environment, red
        )
        recert = evidence_authority.synthetic_recert_event(
            self.plan, environment, green
        )

        result = evidence_authority.validate_transition(
            self.inventory,
            {"FIX01": authority},
            evidence_authority.synthetic_transition_registry(
                self.plan, retired=True
            ),
            self.plan,
            (red, green, recert),
            environment,
        )

        self.assertEqual(
            evidence_authority.ErrorCode.RECERTIFICATION,
            result.error,
        )

    def test_recertification_cannot_suppress_a_retained_identity(self):
        red = evidence_authority.synthetic_red_event(
            self.plan,
            self.environment,
        )
        green = evidence_authority.synthetic_green_event(
            self.plan,
            self.environment,
            red,
        )
        recert = evidence_authority.synthetic_recert_event(
            self.plan,
            self.environment,
            green,
        )
        suppressed = self.plan.affected_identity_keys
        changed_units = tuple(
            dataclasses.replace(
                item,
                suppressed_identity_keys=suppressed,
            )
            for item in recert.unit_results
        )
        changed = evidence_authority.seal_event(
            dataclasses.replace(
                recert,
                suppressed_identity_keys=suppressed,
                unit_results=changed_units,
            )
        )

        result = self.validate(events=(red, green, changed))

        self.assertEqual(
            evidence_authority.ErrorCode.RECERTIFICATION,
            result.error,
        )

    def test_failed_recertification_keeps_complete_inventory_active(self):
        red = evidence_authority.synthetic_red_event(
            self.plan,
            self.environment,
        )
        green = evidence_authority.synthetic_green_event(
            self.plan,
            self.environment,
            red,
        )
        recert = evidence_authority.synthetic_recert_event(
            self.plan,
            self.environment,
            green,
        )
        failed_unit = dataclasses.replace(
            recert.unit_results[0],
            observed=evidence_authority.ObservedResult.FAIL,
        )
        failed = evidence_authority.seal_event(
            dataclasses.replace(
                recert,
                unit_results=(failed_unit, *recert.unit_results[1:]),
            )
        )

        rejected = self.validate(events=(red, green, failed))
        active = self.validate(events=(red, green))

        self.assertEqual(
            evidence_authority.ErrorCode.RECERTIFICATION,
            rejected.error,
        )
        self.assertEqual(
            {fact.key for fact in self.inventory.facts},
            set(active.active_identity_keys),
        )

    def test_missing_permanent_invariant_blocks_transition(self):
        permanent = next(
            fact for fact in self.inventory.facts
            if fact.authority_class
            is evidence_authority.AuthorityClass.PERMANENT_INVARIANT
        )
        changed_inventory = dataclasses.replace(
            self.inventory,
            facts=tuple(
                fact for fact in self.inventory.facts
                if fact.key != permanent.key
            ),
        )

        result = evidence_authority.validate_transition(
            changed_inventory,
            self.task_authorities,
            self.registry,
            self.plan,
            (),
            self.environment,
        )

        self.assertEqual(
            evidence_authority.ErrorCode.FACT_AUTHORITY,
            result.error,
        )

    def test_green_and_recertification_require_same_candidate(self):
        red = evidence_authority.synthetic_red_event(
            self.plan,
            self.environment,
        )
        green = evidence_authority.synthetic_green_event(
            self.plan,
            self.environment,
            red,
        )
        recert = evidence_authority.synthetic_recert_event(
            self.plan,
            self.environment,
            green,
        )
        changed = evidence_authority.seal_event(
            dataclasses.replace(
                recert,
                candidate_projection="f" * 64,
            )
        )

        result = self.validate(events=(red, green, changed))

        self.assertEqual(
            evidence_authority.ErrorCode.PROJECTION,
            result.error,
        )

    def test_complete_transition_retires_only_planned_identity(self):
        red = evidence_authority.synthetic_red_event(
            self.plan,
            self.environment,
        )
        green = evidence_authority.synthetic_green_event(
            self.plan,
            self.environment,
            red,
        )
        recert = evidence_authority.synthetic_recert_event(
            self.plan,
            self.environment,
            green,
        )

        result = self.validate(
            events=(red, green, recert),
            registry=evidence_authority.synthetic_transition_registry(
                self.plan, retired=True
            ),
        )

        self.assertTrue(result.ok, result)
        self.assertEqual(
            evidence_authority.TransitionState.RETIREMENT_READY,
            result.state,
        )
        self.assertEqual(
            set(self.plan.planned_retirement_identity_keys),
            set(result.retired_identity_keys),
        )
        self.assertEqual(
            {fact.key for fact in self.inventory.facts}
            - set(self.plan.planned_retirement_identity_keys),
            set(result.active_identity_keys),
        )

    def test_terminal_transition_must_update_global_registry(self):
        red = evidence_authority.synthetic_red_event(
            self.plan,
            self.environment,
        )
        green = evidence_authority.synthetic_green_event(
            self.plan,
            self.environment,
            red,
        )
        recert = evidence_authority.synthetic_recert_event(
            self.plan,
            self.environment,
            green,
        )

        result = self.validate(events=(red, green, recert))

        self.assertEqual(
            evidence_authority.ErrorCode.DUPLICATE_TRANSITION,
            result.error,
        )

    def test_terminal_state_preserves_prior_retirements(self):
        previous = next(
            fact.key for fact in self.inventory.facts
            if fact.scope == "previous"
        )
        prior = evidence_authority.TransitionClaim(
            "OLD01-authority",
            "OLD01",
            (previous,),
            ("old-oracle",),
            (previous,),
        )
        registry = evidence_authority.TransitionRegistry(
            (
                prior,
                *evidence_authority.synthetic_transition_registry(
                    self.plan, retired=True
                ).claims,
            )
        )
        red = evidence_authority.synthetic_red_event(
            self.plan,
            self.environment,
        )
        green = evidence_authority.synthetic_green_event(
            self.plan,
            self.environment,
            red,
        )
        recert = evidence_authority.synthetic_recert_event(
            self.plan,
            self.environment,
            green,
        )

        result = evidence_authority.validate_transition(
            self.inventory,
            self.task_authorities,
            registry,
            self.plan,
            (red, green, recert),
            self.environment,
        )

        self.assertTrue(result.ok, result)
        self.assertNotIn(previous, result.active_identity_keys)

    def test_event_chain_rejects_mutated_prior_event(self):
        red = evidence_authority.synthetic_red_event(
            self.plan,
            self.environment,
        )
        green = evidence_authority.synthetic_green_event(
            self.plan,
            self.environment,
            red,
        )
        changed_red = evidence_authority.seal_event(
            dataclasses.replace(red, expected_property="changed")
        )

        result = self.validate(events=(changed_red, green))

        self.assertEqual(
            evidence_authority.ErrorCode.EVENT_CHAIN,
            result.error,
        )


if __name__ == "__main__":
    unittest.main()
