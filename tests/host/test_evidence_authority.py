import ast
from collections import Counter
import copy
import dataclasses
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tests.host import evidence_authority
from tests.host import evidence_authority_inventory
from tests.host.ast_evidence import ast_dump
from tests.host.evidence_authority_source import literal_value, single_assignment
from tests.host.evidence_authority_types import fact_set_digest
from tests.host.work_count_recipe import MODULE_PATH, render_native_recipe


ROOT = Path(__file__).resolve().parents[2]


def relocated_binding_reference(root=ROOT):
    study = (root / "docs/research/relocated-evidence-payload-gap.md").read_text()
    reference = re.search(r"The fixed source is\s+`([0-9a-f]{40})`", study).group(1)
    tables = tuple(re.findall(
        r"^\| `(test_[^`]+\.py)` \| `([^`]+)` \| ([0-9]+) \|$",
        study, re.MULTILINE,
    ))
    if len(tables) != 17:
        raise ValueError("historical binding scope lacks its exact table admission")
    return _relocated_binding_reference(root, reference, tables)


@lru_cache(maxsize=4)
def _relocated_binding_reference(root, reference, tables):
    def fixed_source(path):
        return subprocess.check_output(
            ["git", "show", reference + ":" + path], cwd=root, text=True, timeout=30,
        )

    def literals(source):
        values = {}
        for node in ast.parse(source).body:
            if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(
                node.targets[0], ast.Name,
            ):
                try:
                    values[node.targets[0].id] = ast.literal_eval(node.value)
                except (TypeError, ValueError):
                    pass
        return values

    inventory_source = fixed_source("tests/host/evidence_authority_inventory.py")
    mapping_names = {
        "_PRIMARY_PREFIXES", "_primary_unit_for_path", "_path_units",
        "_fact_primary_unit", "_consumer_units",
    }

    def mapping_nodes(source):
        result = {}
        for node in ast.parse(source).body:
            name = node.name if isinstance(node, ast.FunctionDef) else (
                node.targets[0].id if isinstance(node, ast.Assign) and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name) else None
            )
            if name in mapping_names:
                result[name] = ast_dump(node)
        return result

    if mapping_nodes(inventory_source) != mapping_nodes(
        (root / "tests/host/evidence_authority_inventory.py").read_text(),
    ):
        raise ValueError("historical binding unit rules changed")
    files = set(filter(None, subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", "-z", reference],
        cwd=root, timeout=30,
    ).decode().split("\0")))
    directory = literals(fixed_source("tests/host/kernel_layout_source.py"))["KERNEL_FILE_FACTS"]
    objects = literals(fixed_source("tests/host/test_kernel_object_source_layout.py"))
    members = (*objects["CORE_SOURCES"], objects["PRIVATE_HEADERS"][0])
    object_keys = {
        "tests/host/test_scheduler_source_layout.py:data-entry:STABLE_ARTIFACT_HASHES/kernel/kernel_objects.c",
        "tests/host/test_endpoint_source_layout.py:data-entry:STABLE_HASHES/kernel/kernel_objects.c",
    }
    rows = {}
    sources = {}
    for filename, table, count in tables:
        owner = "tests/host/" + filename
        if owner not in sources:
            sources[owner] = fixed_source(owner)
        source = sources[owner]
        old_paths = tuple(sorted({owner} | {
            node.value for node in ast.walk(ast.parse(source))
            if isinstance(node, ast.Constant) and type(node.value) is str and node.value in files
        }))
        entries = {
            path: value for path, value in literals(source)[table].items()
            if path in directory
        }
        if len(entries) != int(count):
            raise ValueError("historical binding table membership changed")
        for logical, value in entries.items():
            key = f"{owner}:data-entry:{table}/{logical}"
            selected = members if key in object_keys else (logical,)
            new_paths = tuple(sorted({owner, *(directory[path][0] for path in selected)}))
            rows[key] = (
                value,
                (old_paths, evidence_authority_inventory._fact_primary_unit(old_paths),
                 evidence_authority_inventory._consumer_units(owner, old_paths)),
                (new_paths, evidence_authority_inventory._fact_primary_unit(new_paths),
                 evidence_authority_inventory._consumer_units(owner, new_paths)),
            )
    if len(rows) != 104:
        raise ValueError("historical binding inventory is incomplete")
    return rows, literals(inventory_source)["DECLARED_FAMILY_FACT_DIGESTS"]


def standalone_binding_reference(root=ROOT):
    study = (root / "docs/research/standalone-frame-validator-payload-gap.md").read_text()
    reference = re.search(r"The fixed source is\s+`([0-9a-f]{40})`", study).group(1)
    return _standalone_binding_reference(root, reference)


@lru_cache(maxsize=4)
def _standalone_binding_reference(root, reference):
    def fixed_source(path):
        return subprocess.check_output(
            ["git", "show", reference + ":" + path], cwd=root, text=True, timeout=30,
        )

    # Reuse the fixed-source check for the unchanged unit-rule ASTs.
    relocated_binding_reference(root)
    owner = "tests/host/kernel_layout_source.py"
    name = "BOUNDED_FRAME_VALIDATION_SHA256"
    tree = ast.parse(fixed_source(owner))
    value = literal_value(single_assignment(tree, name).value)
    directory = literal_value(single_assignment(tree, "KERNEL_FILE_FACTS").value)
    files = set(filter(None, subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", "-z", reference],
        cwd=root, timeout=30,
    ).decode().split("\0")))
    old_paths = tuple(sorted({owner} | {
        node.value for node in ast.walk(tree)
        if isinstance(node, ast.Constant) and type(node.value) is str and node.value in files
    }))
    new_paths = tuple(sorted((owner, directory["kernel/frame_ownership_validation.c"][0])))
    before = evidence_authority.Fact(
        owner + ":data-entry:" + name, "authority-core", owner, "data-entry", name,
        evidence_authority.AuthorityClass.TRANSPORT_IDENTITY,
        evidence_authority_inventory._fact_primary_unit(old_paths),
        evidence_authority_inventory._consumer_units(owner, old_paths), old_paths, value,
    )
    after = dataclasses.replace(
        before, payload_paths=new_paths, primary_unit="B02", consumer_units=("B02", "B14"),
    )
    inventory_tree = ast.parse(fixed_source("tests/host/evidence_authority_inventory.py"))
    fingerprints = literal_value(single_assignment(inventory_tree, "DECLARED_FAMILY_FACT_DIGESTS").value)
    return before, after, fingerprints


def prior_standalone_binding_facts(facts, root=ROOT):
    before, after, _ = standalone_binding_reference(root)
    result = []
    seen = False
    for fact in facts:
        if fact.key == after.key:
            if seen:
                raise ValueError("duplicate corrected standalone binding")
            seen = True
            if (fact.payload_paths, fact.primary_unit, fact.consumer_units) != (
                after.payload_paths, after.primary_unit, after.consumer_units,
            ):
                raise ValueError("corrected standalone binding metadata differs from its fixed declaration")
            fact = dataclasses.replace(
                fact, payload_paths=before.payload_paths, primary_unit=before.primary_unit,
                consumer_units=before.consumer_units,
            )
        result.append(fact)
    return tuple(result)


def prior_binding_facts(facts, root=ROOT):
    facts = prior_standalone_binding_facts(facts, root)
    rows, _ = relocated_binding_reference(root)
    admitted = {
        (owner, scope.partition("/")[0])
        for owner, scope in (key.split(":data-entry:", 1) for key in rows)
    }
    result = []
    seen = set()
    for fact in facts:
        if fact.key in rows:
            if fact.key in seen:
                raise ValueError("duplicate corrected binding")
            seen.add(fact.key)
            _, before, after = rows[fact.key]
            if (fact.payload_paths, fact.primary_unit, fact.consumer_units) != after:
                raise ValueError("corrected binding metadata differs from its fixed declaration")
            fact = dataclasses.replace(
                fact, payload_paths=before[0], primary_unit=before[1], consumer_units=before[2],
            )
        elif (
            fact.kind == "data-entry"
            and (fact.owner_path, fact.scope.partition("/")[0]) in admitted
            and fact.scope.partition("/")[2].startswith("kernel/")
        ):
            raise ValueError("unknown corrected binding")
        result.append(fact)
    if seen != set(rows):
        raise ValueError("missing or unknown corrected binding")
    return tuple(result)


class InventoryTests(unittest.TestCase):
    def test_fatal_firmware_facts_remain_artifacts_not_runtime_baseline_oracles(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        values = [
            fact for fact in inventory.facts
            if fact.family == "stack-firmware-artifact" and fact.evidence_value
        ]
        self.assertEqual(2, len(values))
        for fact in values:
            self.assertIs(evidence_authority.AuthorityClass.ARTIFACT_CONTRACT, fact.authority_class)
            self.assertIn("docs/adr/0086-finite-fatal-stack-context.md", fact.payload_paths)
            self.assertNotIn("B13", fact.consumer_units)

    def test_ev03_source_binding_registration_preserves_all_earlier_facts(self):
        research = (ROOT / "docs/research/minix-stack-bound-evidence.md").read_text()
        reference = re.search(r"commit: ([0-9a-f]{40})", research).group(1)
        result = subprocess.run(
            ["git", "show", reference + ":tests/host/evidence_authority_inventory.py"],
            cwd=ROOT, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(0, result.returncode, result.stderr)
        declaration = next(
            node for node in ast.parse(result.stdout).body
            if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == "DECLARED_FAMILY_FACT_DIGESTS"
                for target in node.targets
            )
        )
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        historical = prior_binding_facts(inventory.facts)
        for family, expected in ast.literal_eval(declaration.value).items():
            self.assertEqual(
                expected, fact_set_digest(fact for fact in historical if fact.family == family),
                family,
            )
        anchor, = (
            fact for fact in inventory.facts
            if fact.key == "tools/stack_bound_bindings.py:data-entry:AUTHORITY_COMMIT"
        )
        self.assertIs(evidence_authority.AuthorityClass.TRANSPORT_IDENTITY, anchor.authority_class)
        self.assertEqual(reference, anchor.evidence_value)
        self.assertEqual("B14", anchor.primary_unit)
        self.assertEqual(
            tuple([f"B{index:02d}" for index in range(1, 13)] + ["B14"]), anchor.consumer_units,
        )

    def test_ev02_registration_preserves_every_historical_authority(self):
        result = subprocess.run(
            [
                "git", "show",
                "fa6eb16b0e9325587680e5504139c68e32bd22ab:"
                "tests/host/evidence_authority_inventory.py",
            ],
            cwd=ROOT, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(0, result.returncode, result.stderr)
        statement = next(
            node for node in ast.parse(result.stdout).body
            if isinstance(node, ast.Assign)
            and any(isinstance(target, ast.Name)
                    and target.id == "DECLARED_FAMILY_FACT_DIGESTS"
                    for target in node.targets)
        )
        expected = ast.literal_eval(statement.value)
        artifact_key = (
            "tools/validation_context.py:data-entry:RECIPE_DIGESTS/"
            + MODULE_PATH
        )
        projected = []
        artifacts = []
        for fact in prior_binding_facts(evidence_authority_inventory.current_inventory(ROOT).facts):
            if fact.key == artifact_key:
                artifacts.append(fact)
                self.assertIs(
                    evidence_authority.AuthorityClass.ARTIFACT_CONTRACT,
                    fact.authority_class,
                )
                self.assertEqual("B14", fact.primary_unit)
                self.assertEqual(("B14",), fact.consumer_units)
                self.assertEqual(
                    (MODULE_PATH, "tools/validation_context.py"),
                    fact.payload_paths,
                )
                self.assertEqual(
                    hashlib.sha256(render_native_recipe().encode()).hexdigest(),
                    fact.evidence_value,
                )
                continue
            if fact.key == "tools/validation_context.py:data-entry:QEMU_RUNNER_DIGEST":
                self.assertIn(MODULE_PATH, fact.payload_paths)
                fact = dataclasses.replace(
                    fact,
                    payload_paths=tuple(
                        path for path in fact.payload_paths if path != MODULE_PATH
                    ),
                )
            projected.append(fact)
        self.assertEqual(1, len(artifacts))
        for family, fingerprint in expected.items():
            self.assertEqual(
                fingerprint,
                fact_set_digest(fact for fact in projected if fact.family == family),
                family,
            )

    def test_current_authority_inventory_is_closed_and_unique(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)

        result = evidence_authority.validate_inventory(inventory)

        self.assertTrue(result.ok, result)
        self.assertEqual(14, len(inventory.families))
        self.assertEqual(3938, len(inventory.facts))
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


class PackedInventoryTests(unittest.TestCase):
    OWNER = "tests/host/test_scheduler_source_layout.py"
    FAMILY = "scheduler-packed-transport"

    @classmethod
    def setUpClass(cls):
        study = (ROOT / "docs/research/packed-evidence-identity-gap.md").read_text()
        reference = re.search(
            r"The fixed current source is `micros` commit\s+`([0-9a-f]{40})`", study,
        ).group(1)

        def fixed_source(path):
            return subprocess.check_output(
                ["git", "show", reference + ":" + path],
                cwd=ROOT, text=True, timeout=30,
            )

        cls.source = fixed_source(cls.OWNER)
        cls.tables = cls.declarations(cls.source)
        cls.layout_source = fixed_source("tests/host/kernel_layout_source.py")
        cls.directory = cls.declarations(cls.layout_source)["KERNEL_FILE_FACTS"]
        cls.previous = cls.declarations(fixed_source(
            "tests/host/evidence_authority_inventory.py",
        ))["DECLARED_FAMILY_FACT_DIGESTS"]

    @staticmethod
    def declarations(source):
        values = {}
        for node in ast.parse(source).body:
            if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(
                node.targets[0], ast.Name,
            ):
                try:
                    values[node.targets[0].id] = ast.literal_eval(node.value)
                except (ValueError, TypeError):
                    pass
        return values

    def physical(self, path):
        return self.directory[path][0] if path.startswith("kernel/") else path

    def payload(self, paths):
        return tuple(sorted({self.OWNER, *map(self.physical, paths)}))

    def changed(self, name, value):
        tree = ast.parse(self.source)
        statement, = (
            node for node in tree.body
            if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == name for target in node.targets
            )
        )
        statement.value = ast.parse(repr(value), mode="eval").body
        return ast.unparse(tree)

    def packed_facts(self, source=None, root=ROOT):
        read = Path.read_text

        def selected(path, *args, **kwargs):
            return source if source is not None and path == root / self.OWNER else read(path, *args, **kwargs)

        with patch.object(Path, "read_text", selected):
            return tuple(evidence_authority_inventory._facts_for_owner(root, self.FAMILY, self.OWNER))

    def test_function_records_match_fixed_data_and_exact_current_owners(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        observed = {
            fact.scope: fact for fact in inventory.facts
            if fact.family == self.FAMILY and fact.kind == "function-body"
        }
        expected = {}
        for original, text in self.tables["FROZEN_FUNCTIONS"].items():
            for line in text.splitlines():
                if not line.strip():
                    continue
                name, lines, sha256 = line.split()
                owner, = (
                    path for path, names in self.tables["ORIGINAL_FUNCTIONS"][original].items()
                    if name in names
                )
                expected[f"FROZEN_FUNCTIONS/{original}/{name}"] = (
                    {"original_lines": int(lines), "sha256": sha256}, self.payload((owner,)),
                )
        self.assertEqual(163, len(observed))
        self.assertEqual(set(expected), set(observed))
        for scope, (value, payload) in expected.items():
            fact = observed[scope]
            self.assertEqual(self.OWNER + ":function-body:" + scope, fact.key)
            self.assertEqual(json.dumps(value, sort_keys=True, separators=(",", ":")), fact.evidence_value)
            self.assertEqual(payload, fact.payload_paths)
            self.assertIs(evidence_authority.AuthorityClass.TRANSPORT_IDENTITY, fact.authority_class)
            self.assertGreaterEqual(set(fact.consumer_units), {"B03", "B14"})
            source, = (path for path in payload if path != self.OWNER)
            self.assertEqual("B03" if source.startswith("kernel/scheduler/") else "B14", fact.primary_unit)

    def test_semantic_entries_preserve_each_minimum_multiset_and_family_closure(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        observed = {
            fact.scope: fact for fact in inventory.facts
            if fact.family == self.FAMILY and fact.kind == "data-table"
        }
        self.assertEqual(14, len(observed))
        families = {
            sources[0]: (*sources, self.tables["PRIVATE_HEADERS"][index])
            for index, sources in enumerate(self.tables["SOURCE_SETS"].values())
        }
        occurrences = 0
        for (original, name), text in self.tables["FROZEN_FACTS"].items():
            scope = f"FROZEN_FACTS/{original}/{name}"
            expected = sorted(Counter(text.split()).items())
            fact = observed[scope]
            self.assertEqual(self.OWNER + ":data-table:" + scope, fact.key)
            self.assertEqual(json.dumps(expected, separators=(",", ":")), fact.evidence_value)
            self.assertEqual(self.payload(families[original]), fact.payload_paths)
            self.assertIs(evidence_authority.AuthorityClass.TRANSPORT_IDENTITY, fact.authority_class)
            occurrences += sum(count for _, count in expected)
        self.assertEqual(1080, occurrences)

    def test_new_family_conserves_all_prior_fingerprints_and_committed_retirement(self):
        from tests.host import runtime_memory_authority as memory

        inventory = evidence_authority_inventory.current_inventory(ROOT)
        self.assertTrue(evidence_authority.validate_inventory(inventory).ok)
        retained = tuple(
            fact for fact in inventory.facts
            if fact.family in self.previous or fact.family == self.FAMILY
        )
        self.assertEqual(3912, len(retained))
        self.assertEqual(11, len({fact.family for fact in retained}))
        self.assertEqual(3735, sum(fact.family != self.FAMILY for fact in retained))
        historical = prior_binding_facts(retained)
        for family, expected in self.previous.items():
            self.assertEqual(expected, fact_set_digest(
                fact for fact in historical if fact.family == family
            ), family)
        with patch.object(memory, "read_current", side_effect=AssertionError("prior claim needs no journal")):
            self.assertFalse(memory.memory_identity_required(ROOT))
        authority, _, registry = memory.task_definition(ROOT)
        result = evidence_authority.validate_transition(
            inventory, {authority.task_id: authority}, registry, None, (), None,
        )
        self.assertTrue(result.ok, result)
        self.assertFalse(result.retired_identity_keys)
        self.assertEqual(
            {fact.key for fact in inventory.facts} - {memory.MEMORY_IDENTITY},
            set(result.active_identity_keys),
        )
        self.assertTrue({
            fact.key for fact in inventory.facts if fact.family == self.FAMILY
        } <= set(result.active_identity_keys))

    def test_tables_reject_missing_duplicate_or_nonliteral_declarations(self):
        for name in ("FROZEN_FUNCTIONS", "FROZEN_FACTS"):
            for mutation in ("missing", "repeated", "duplicate-key", "call", "alias", "annotated", "augmented"):
                tree = ast.parse(self.source)
                statement, = (
                    node for node in tree.body
                    if isinstance(node, ast.Assign) and any(
                        isinstance(target, ast.Name) and target.id == name for target in node.targets
                    )
                )
                if mutation == "missing":
                    tree.body.remove(statement)
                elif mutation == "repeated":
                    tree.body.append(copy.deepcopy(statement))
                elif mutation == "duplicate-key":
                    statement.value.keys.append(copy.deepcopy(statement.value.keys[0]))
                    statement.value.values.append(copy.deepcopy(statement.value.values[0]))
                elif mutation in ("call", "alias"):
                    statement.value = ast.parse("dict()" if mutation == "call" else "OTHER", mode="eval").body
                elif mutation == "annotated":
                    tree.body[tree.body.index(statement)] = ast.parse(name + ": dict = {}").body[0]
                else:
                    tree.body.extend(ast.parse(name + " |= {}").body)
                with self.subTest(table=name, mutation=mutation), self.assertRaises(ValueError):
                    self.packed_facts(ast.unparse(tree))

    def test_function_rows_reject_incomplete_unknown_or_malformed_membership(self):
        original, text = next(iter(self.tables["FROZEN_FUNCTIONS"].items()))
        rows = text.strip().splitlines()
        name, lines, sha256 = rows[0].split()
        changes = (
            rows[1:], [*rows, rows[0]], [*rows, "unknown 1 " + sha256],
            [f"not-a-name {lines} {sha256}", *rows[1:]],
            *([f"{name} {count} {sha256}", *rows[1:]] for count in ("0", "-1", "1.5", "4294967296")),
            [f"{name} {lines} {sha256.upper()}", *rows[1:]],
            [f"{name} {lines} {sha256[:-1]}", *rows[1:]],
            [rows[0] + " extra", *rows[1:]],
        )
        for index, changed in enumerate(changes):
            table = dict(self.tables["FROZEN_FUNCTIONS"])
            table[original] = "\n".join(changed)
            with self.subTest(row=index), self.assertRaises(ValueError):
                self.packed_facts(self.changed("FROZEN_FUNCTIONS", table))
        for mutation in ("missing-source", "extra-source"):
            table = dict(self.tables["FROZEN_FUNCTIONS"])
            if mutation == "missing-source":
                del table[original]
            else:
                table["kernel/unknown.c"] = text
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                self.packed_facts(self.changed("FROZEN_FUNCTIONS", table))

    def test_semantic_rows_reject_wrong_roots_tags_and_empty_or_nonliteral_values(self):
        pair, text = next(iter(self.tables["FROZEN_FACTS"].items()))
        for mutation in ("missing-root", "unknown-root", "empty", "wrong-tag", "short-tag", "nonliteral"):
            table = dict(self.tables["FROZEN_FACTS"])
            if mutation == "missing-root":
                del table[pair]
            elif mutation == "unknown-root":
                table[(pair[0], "unknown")] = text
            else:
                table[pair] = {
                    "empty": "", "wrong-tag": "X" + "0" * 24,
                    "short-tag": "C" + "0" * 23, "nonliteral": ("C" + "0" * 24,),
                }[mutation]
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                self.packed_facts(self.changed("FROZEN_FACTS", table))
        roots = self.tables["INITIAL_FUNCTION_MIGRATION"]
        for changed in (roots[:-1], (*roots, roots[0]), (*roots, self.tables["SHORT_PHASE_ROOTS"][0])):
            with self.subTest(roots=changed), self.assertRaises(ValueError):
                self.packed_facts(self.changed("INITIAL_FUNCTION_MIGRATION", changed))

    def test_original_ownership_requires_complete_unique_family_members(self):
        original = next(iter(self.tables["ORIGINAL_FUNCTIONS"]))
        owners = self.tables["ORIGINAL_FUNCTIONS"][original]
        first, second = tuple(owners)[:2]
        for mutation in ("duplicate", "missing", "extra-owner", "missing-owner", "wrong-family"):
            table = copy.deepcopy(self.tables["ORIGINAL_FUNCTIONS"])
            if mutation == "duplicate":
                table[original][second] += (owners[first][0],)
            elif mutation == "missing":
                table[original][first] = owners[first][1:]
            elif mutation == "extra-owner":
                table[original]["kernel/unknown.c"] = ()
            elif mutation == "missing-owner":
                del table[original][second]
            else:
                table["kernel/unknown.c"] = table.pop(original)
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                self.packed_facts(self.changed("ORIGINAL_FUNCTIONS", table))

    def test_validly_shaped_value_or_binding_changes_cannot_rebaseline_the_family(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        original, text = next(iter(self.tables["FROZEN_FUNCTIONS"].items()))
        name, lines, sha256 = text.strip().splitlines()[0].split()
        mutations = []
        for old, new in ((lines + " " + sha256, str(int(lines) + 1) + " " + sha256), (sha256, "0" * 64)):
            table = dict(self.tables["FROZEN_FUNCTIONS"])
            table[original] = text.replace(old, new, 1)
            mutations.append(self.changed("FROZEN_FUNCTIONS", table))
        table = dict(self.tables["FROZEN_FACTS"])
        pair = next(iter(table))
        table[pair] += " " + table[pair].split()[0]
        mutations.append(self.changed("FROZEN_FACTS", table))
        owners = copy.deepcopy(self.tables["ORIGINAL_FUNCTIONS"])
        first, second = tuple(owners[original])[:2]
        owners[original][first] = tuple(item for item in owners[original][first] if item != name)
        owners[original][second] += (name,)
        mutations.append(self.changed("ORIGINAL_FUNCTIONS", owners))
        for index, source in enumerate(mutations):
            changed = dataclasses.replace(
                inventory, facts=(
                    *(fact for fact in inventory.facts if fact.family != self.FAMILY),
                    *self.packed_facts(source),
                ),
            )
            with self.subTest(mutation=index):
                self.assertEqual(evidence_authority.ErrorCode.FACT_AUTHORITY, evidence_authority.validate_inventory(changed).error)

    def test_discovery_recognizes_packed_only_modules_without_approving_them(self):
        body = "function 1 " + "a" * 64
        semantic = "C" + "b" * 24
        with tempfile.TemporaryDirectory(prefix="micros-packed-discovery-") as temporary:
            path = Path(temporary) / "test_unknown.py"
            for source in (
                "FROZEN_FUNCTIONS = {}", "FROZEN_FACTS = None",
                "OTHER = " + repr({"source.c": body}),
                "OTHER = " + repr({("source.c", "function"): semantic}),
            ):
                path.write_text(source)
                self.assertTrue(evidence_authority_inventory._identity_candidate(path), source)
                with self.assertRaises(ValueError):
                    evidence_authority_inventory._facts_for_owner(Path(temporary), "kernel-structure", path.name)
            path.write_text("def fixture():\n    source = " + repr(body + "\n" + semantic))
            self.assertFalse(evidence_authority_inventory._identity_candidate(path))
        for source in (
            self.source + "\nOTHER = " + repr({"source.c": body}),
            self.source + "\nALIAS = FROZEN_FUNCTIONS",
            self.source + "\nFROZEN_FUNCTIONS.update({})",
        ):
            with self.assertRaises(ValueError):
                self.packed_facts(source)

    def test_current_payloads_reject_missing_linked_and_ambiguous_bindings(self):
        original = next(iter(self.tables["ORIGINAL_FUNCTIONS"]))
        paths = {path for sources in self.tables["SOURCE_SETS"].values() for path in sources}
        paths.update(self.tables["PRIVATE_HEADERS"])
        for mutation in ("none", "missing", "symlink", "hardlink", "parent-link", "old-owner", "ambiguous-map"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory(prefix="micros-packed-path-") as temporary:
                root = Path(temporary)
                for path in self.payload(paths):
                    destination = root / path
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_text(self.source if path == self.OWNER else "")
                layout = root / "tests/host/kernel_layout_source.py"
                layout.write_text(self.layout_source)
                target = root / self.physical(original)
                if mutation == "missing":
                    target.unlink()
                elif mutation == "symlink":
                    saved = target.with_suffix(".saved")
                    target.rename(saved)
                    target.symlink_to(saved.name)
                elif mutation == "hardlink":
                    os.link(target, target.with_suffix(".alias"))
                elif mutation == "parent-link":
                    directory = target.parent
                    saved = directory.with_name(directory.name + "-saved")
                    directory.rename(saved)
                    directory.symlink_to(saved.name)
                elif mutation == "old-owner":
                    (root / original).write_text("")
                elif mutation == "ambiguous-map":
                    mapping = copy.deepcopy(self.directory)
                    other = tuple(self.tables["SOURCE_SETS"].values())[0][1]
                    mapping[other] = (self.physical(original), *mapping[other][1:])
                    layout.write_text("KERNEL_FILE_FACTS = " + repr(mapping))
                if mutation == "none":
                    self.assertEqual(177, len(self.packed_facts(root=root)))
                else:
                    with self.assertRaises(ValueError):
                        self.packed_facts(root=root)


class RelocatedBindingTests(unittest.TestCase):
    def test_direct_tty_hash_binds_its_relocated_source(self):
        key = "tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/kernel/tty_control_core.c"
        fact, = (
            fact for fact in evidence_authority_inventory.current_inventory(ROOT).facts
            if fact.key == key
        )
        self.assertIn("kernel/tty/tty_control_core.c", fact.payload_paths)
        self.assertEqual(
            ("kernel/tty/tty_control_core.c", "tests/host/test_tty_source_layout.py"),
            fact.payload_paths,
        )
        self.assertNotIn("tools/validation_context.py", fact.payload_paths)
        self.assertEqual("B09", fact.primary_unit)
        self.assertEqual(("B01", "B09", "B14"), fact.consumer_units)

    def test_reconstruction_hashes_bind_every_current_owner_and_header(self):
        rows, _ = relocated_binding_reference()
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        for key in (
            "tests/host/test_scheduler_source_layout.py:data-entry:STABLE_ARTIFACT_HASHES/kernel/kernel_objects.c",
            "tests/host/test_endpoint_source_layout.py:data-entry:STABLE_HASHES/kernel/kernel_objects.c",
        ):
            fact, = (fact for fact in inventory.facts if fact.key == key)
            with self.subTest(key=key):
                self.assertEqual(11, len(fact.payload_paths))
                self.assertEqual(rows[key][2], (
                    fact.payload_paths, fact.primary_unit, fact.consumer_units,
                ))
                self.assertIn("kernel/objects/kernel_object_context.c", fact.payload_paths)
                self.assertIn("kernel/objects/kernel_object_private.h", fact.payload_paths)

    def test_all_corrected_records_recover_the_exact_prior_inventory(self):
        rows, fingerprints = relocated_binding_reference()
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        self.assertTrue(evidence_authority.validate_inventory(inventory).ok)
        retained = tuple(fact for fact in inventory.facts if fact.family in fingerprints)
        self.assertEqual(3912, len(retained))
        observed = {fact.key: fact for fact in inventory.facts if fact.key in rows}
        self.assertEqual(set(rows), set(observed))
        self.assertEqual({2: 102, 11: 2}, Counter(len(fact.payload_paths) for fact in observed.values()))
        for key, fact in observed.items():
            expected_value, _, expected_metadata = rows[key]
            self.assertEqual(expected_value, fact.evidence_value)
            self.assertEqual(expected_metadata, (fact.payload_paths, fact.primary_unit, fact.consumer_units))
            self.assertIs(evidence_authority.AuthorityClass.TRANSPORT_IDENTITY, fact.authority_class)
        projected = prior_binding_facts(retained)
        standalone_view = prior_standalone_binding_facts(retained)
        self.assertEqual(104, sum(before != after for before, after in zip(standalone_view, projected)))
        for family, expected in fingerprints.items():
            self.assertEqual(expected, fact_set_digest(fact for fact in projected if fact.family == family), family)

    def test_historical_inverse_rejects_binding_drift_without_erasing_value_changes(self):
        inventory = evidence_authority_inventory.current_inventory(ROOT)
        key = "tests/host/test_scheduler_source_layout.py:data-entry:STABLE_ARTIFACT_HASHES/kernel/kernel_objects.c"
        selected, = (fact for fact in inventory.facts if fact.key == key)
        changes = (
            dataclasses.replace(selected, payload_paths=selected.payload_paths[:-1]),
            dataclasses.replace(selected, payload_paths=(
                *selected.payload_paths, "tools/validation_context.py",
            )),
            dataclasses.replace(selected, primary_unit="B14"),
            dataclasses.replace(selected, consumer_units=("B14",)),
            dataclasses.replace(selected, key=selected.key + "/unknown"),
        )
        for changed in changes:
            facts = tuple(changed if fact.key == key else fact for fact in inventory.facts)
            with self.subTest(change=changed), self.assertRaises(ValueError):
                prior_binding_facts(facts)
        with self.assertRaises(ValueError):
            prior_binding_facts((*inventory.facts, selected))
        with self.assertRaises(ValueError):
            prior_binding_facts((*inventory.facts, dataclasses.replace(
                selected, key=selected.key + "/unknown", scope=selected.scope + "/unknown",
            )))
        changed = dataclasses.replace(selected, evidence_value="0" * 64)
        projected = prior_binding_facts(
            tuple(changed if fact.key == key else fact for fact in inventory.facts),
        )
        _, fingerprints = relocated_binding_reference()
        self.assertNotEqual(
            fingerprints[selected.family],
            fact_set_digest(fact for fact in projected if fact.family == selected.family),
        )

    def test_current_binding_rejects_missing_nonfacade_reconstruction_inputs(self):
        lstat = Path.lstat
        for relative in (
            "kernel/objects/kernel_object_context.c",
            "kernel/objects/kernel_object_private.h",
        ):
            def missing(path, *args, **kwargs):
                if path == ROOT / relative:
                    raise FileNotFoundError(relative)
                return lstat(path, *args, **kwargs)

            with self.subTest(path=relative), patch.object(Path, "lstat", missing), self.assertRaises(ValueError):
                evidence_authority_inventory.current_inventory(ROOT)

    def test_directory_binding_rejects_unsafe_or_ambiguous_current_sources(self):
        from tests.host.evidence_authority_source import kernel_sources

        logical = "kernel/frame_ownership_validation.c"
        physical = "kernel/memory/frame_ownership_validation.c"
        for mutation in (
            "none", "missing-entry", "malformed-entry", "missing-file",
            "symlink", "hardlink", "parent-link", "old-flat", "ambiguous-map", "duplicate-key",
        ):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory(prefix="micros-relocated-source-") as temporary:
                root = Path(temporary)
                target = root / physical
                target.parent.mkdir(parents=True)
                target.write_text("")
                mapping = {logical: (physical, "100644", "0" * 64, ())}
                if mutation == "missing-entry":
                    mapping = {}
                elif mutation == "malformed-entry":
                    mapping[logical] = physical
                elif mutation == "missing-file":
                    target.unlink()
                elif mutation == "symlink":
                    saved = target.with_suffix(".saved")
                    target.rename(saved)
                    target.symlink_to(saved.name)
                elif mutation == "hardlink":
                    os.link(target, target.with_suffix(".alias"))
                elif mutation == "parent-link":
                    directory = target.parent
                    saved = directory.with_name(directory.name + "-saved")
                    directory.rename(saved)
                    directory.symlink_to(saved.name)
                elif mutation == "old-flat":
                    (root / logical).write_text("")
                elif mutation == "ambiguous-map":
                    mapping["kernel/other.c"] = mapping[logical]
                source = root / "tests/host/kernel_layout_source.py"
                source.parent.mkdir(parents=True)
                text = "KERNEL_FILE_FACTS = " + repr(mapping)
                if mutation == "duplicate-key":
                    text = text[:-1] + ", " + repr(logical) + ": " + repr(mapping[logical]) + "}"
                source.write_text(text + "\nBOUNDED_FRAME_VALIDATION_SHA256 = " + repr("0" * 64))
                if mutation == "none":
                    self.assertEqual({logical: physical}, kernel_sources(root, (logical,)))
                    fact, = (
                        fact for fact in evidence_authority_inventory._facts_for_owner(
                            root, "authority-core", "tests/host/kernel_layout_source.py",
                        )
                        if fact.scope == "BOUNDED_FRAME_VALIDATION_SHA256"
                    )
                    self.assertEqual((physical, "tests/host/kernel_layout_source.py"), fact.payload_paths)
                else:
                    with self.assertRaises(ValueError):
                        kernel_sources(root, (logical,))
                    with self.assertRaises(ValueError):
                        evidence_authority_inventory._facts_for_owner(
                            root, "authority-core", "tests/host/kernel_layout_source.py",
                        )


class StandaloneBindingTests(unittest.TestCase):
    OWNER = "tests/host/kernel_layout_source.py"
    SOURCE = "kernel/memory/frame_ownership_validation.c"
    KEY = OWNER + ":data-entry:BOUNDED_FRAME_VALIDATION_SHA256"

    @classmethod
    def setUpClass(cls):
        cls.inventory = evidence_authority_inventory.current_inventory(ROOT)
        cls.fact, = (fact for fact in cls.inventory.facts if fact.key == cls.KEY)

    def transition(self):
        # This synthetic registration never records an actual retirement.
        plan = evidence_authority.synthetic_plan(evidence_authority.synthetic_inventory())
        units = self.fact.consumer_units
        plan = dataclasses.replace(
            plan, owner_unit="B02", finding_ids=("B02-F999",),
            affected_identity_keys=(self.KEY,), retained_identity_keys=(),
            planned_retirement_identity_keys=(self.KEY,),
            replacement_oracles=(dataclasses.replace(
                plan.replacement_oracles[0], replaces_identity_keys=(self.KEY,),
            ),),
            recertification_units=units,
            unit_commands=tuple((unit, ("validate-" + unit.lower(),)) for unit in units),
        )
        authority = dataclasses.replace(
            evidence_authority.synthetic_task_authorities(plan)[plan.owner_task],
            command_input_paths=("CMakeLists.txt", "tools/validation_plan.py"),
            unit_input_paths=tuple((unit, (self.SOURCE,)) for unit in units),
        )
        environment = evidence_authority.synthetic_environment(plan)
        immutable = dict(environment.immutable_inputs)
        del immutable["tools/command_registry.py"]
        immutable.update(
            (path, hashlib.sha256((ROOT / path).read_bytes()).hexdigest())
            for path in authority.command_input_paths
        )
        candidate = tuple(
            (path, hashlib.sha256((ROOT / path).read_bytes()).hexdigest())
            for path in (self.SOURCE, self.OWNER)
        )
        environment = dataclasses.replace(
            environment, immutable_inputs=tuple(sorted(immutable.items())),
            red_candidate_inputs=candidate, current_candidate_inputs=candidate,
            unit_inputs=tuple((unit, (candidate[0],)) for unit in units),
        )
        return plan, authority, environment

    def validate(self, plan, authority, environment, events=()):
        return evidence_authority.validate_transition(
            self.inventory, {plan.owner_task: authority},
            evidence_authority.synthetic_transition_registry(plan),
            plan, events, environment,
        )

    def test_standalone_hash_binds_only_its_actual_source(self):
        self.assertEqual((self.SOURCE, self.OWNER), self.fact.payload_paths)
        self.assertEqual("B02", self.fact.primary_unit)
        self.assertEqual(("B02", "B14"), self.fact.consumer_units)

    def test_actual_binding_keeps_independent_command_inputs_immutable(self):
        result = self.validate(*self.transition())
        self.assertTrue(result.ok, result)
        self.assertIs(evidence_authority.TransitionState.REPLACEMENT_REGISTERED, result.state)
        self.assertFalse(result.retired_identity_keys)

    def test_real_source_overlap_absence_and_changed_bytes_still_fail(self):
        plan, authority, environment = self.transition()
        source_value = dict(environment.current_candidate_inputs)[self.SOURCE]
        result = self.validate(
            plan, dataclasses.replace(
                authority, command_input_paths=(*authority.command_input_paths, self.SOURCE),
            ),
            dataclasses.replace(
                environment, immutable_inputs=(*environment.immutable_inputs, (self.SOURCE, source_value)),
            ),
        )
        self.assertIs(evidence_authority.ErrorCode.PROJECTION, result.error)
        for field in ("red_candidate_inputs", "current_candidate_inputs"):
            with self.subTest(missing=field):
                result = self.validate(plan, authority, dataclasses.replace(
                    environment, **{field: tuple(
                        item for item in getattr(environment, field) if item[0] != self.SOURCE
                    )},
                ))
                self.assertIs(evidence_authority.ErrorCode.PROJECTION, result.error)

        red = evidence_authority.synthetic_red_event(plan, environment)
        green = evidence_authority.synthetic_green_event(plan, environment, red)
        self.assertTrue(self.validate(plan, authority, environment, (red, green)).ok)
        changed = dataclasses.replace(
            environment,
            current_candidate_inputs=tuple(
                (path, "changed-source" if path == self.SOURCE else value)
                for path, value in environment.current_candidate_inputs
            ),
            unit_inputs=tuple(
                (unit, ((self.SOURCE, "changed-source"),)) for unit in plan.recertification_units
            ),
        )
        result = self.validate(plan, authority, changed, (red, green))
        self.assertIs(evidence_authority.ErrorCode.PROJECTION, result.error)

    def test_admitted_scalar_rejects_missing_duplicate_and_nonliteral_shapes(self):
        name = "BOUNDED_FRAME_VALIDATION_SHA256"
        value = repr("0" * 64)
        valid = name + " = " + value + "\n"
        declarations = (
            "", valid + valid, name + ": str = " + value,
            name + " = alias = " + value,
            valid + "def shadow():\n    " + name + " = " + value,
            valid + name + " += " + value,
            name + " = str(" + value + ")",
            name + " = (" + value + ",)",
            name + " = {" + value + ": " + value + "}",
            name + " = [" + value + "]",
            name + " = " + repr("0" * 40),
            name + " = " + repr("A" * 64),
            name + " = None",
        )
        with tempfile.TemporaryDirectory(prefix="micros-standalone-shape-") as directory:
            root = Path(directory)
            source = root / self.OWNER
            source.parent.mkdir(parents=True)
            for declaration in declarations:
                source.write_text(declaration)
                with self.subTest(declaration=declaration), self.assertRaises(ValueError):
                    evidence_authority_inventory._facts_for_owner(root, "authority-core", self.OWNER)

    def test_similar_names_and_unqualified_owners_keep_their_existing_fallback(self):
        name = "BOUNDED_FRAME_VALIDATION_SHA256"
        value = repr("0" * 64)
        with tempfile.TemporaryDirectory(prefix="micros-standalone-selector-") as directory:
            root = Path(directory)
            owner = root / self.OWNER
            owner.parent.mkdir(parents=True)
            target = root / self.SOURCE
            target.parent.mkdir(parents=True)
            target.write_text("current source need not equal its historical hash")
            (root / "CMakeLists.txt").write_text("independent command input")
            mapping = {"kernel/frame_ownership_validation.c": (self.SOURCE, "100644", "0" * 64, ())}
            declarations = (
                "KERNEL_FILE_FACTS = " + repr(mapping) + "\n"
                + name + " = " + value + "\nINPUT = 'CMakeLists.txt'\n"
            )
            for other in ("BASE_FRAME_VALIDATION_SHA256", name + "_OTHER"):
                owner.write_text(declarations + other + " = " + value)
                facts = evidence_authority_inventory._facts_for_owner(root, "authority-core", self.OWNER)
                selected, = (fact for fact in facts if fact.scope == name)
                opaque, = (fact for fact in facts if fact.scope == other)
                self.assertEqual((self.SOURCE, self.OWNER), selected.payload_paths)
                self.assertIn("CMakeLists.txt", opaque.payload_paths)
            other_owner = "tests/host/another_source.py"
            (root / other_owner).write_text(name + " = " + value + "\nINPUT = 'CMakeLists.txt'\n")
            facts = evidence_authority_inventory._facts_for_owner(root, "authority-core", other_owner)
            opaque, = (fact for fact in facts if fact.scope == name)
            self.assertEqual(("CMakeLists.txt", other_owner), opaque.payload_paths)

    def test_exact_record_and_every_other_complete_fingerprint_match_fixed_source(self):
        before, after, fingerprints = standalone_binding_reference()
        self.assertEqual(after, self.fact)
        historical = prior_standalone_binding_facts(self.inventory.facts)
        self.assertEqual(1, sum(current != prior for current, prior in zip(self.inventory.facts, historical)))
        self.assertEqual(before, next(fact for fact in historical if fact.key == self.KEY))
        for family, expected in fingerprints.items():
            self.assertEqual(
                expected, fact_set_digest(fact for fact in historical if fact.family == family), family,
            )
        contract = (ROOT / "docs/architecture/standalone-frame-validator-binding.md").read_text()
        old, new = re.search(
            r"^\| `authority-core` \| `([0-9a-f]{64})` \| `([0-9a-f]{64})` \|$",
            contract, re.MULTILINE,
        ).groups()
        self.assertEqual(old, fingerprints["authority-core"])
        expected = {**fingerprints, "authority-core": new}
        self.assertEqual(expected, dict(self.inventory.declared_family_fact_digests))
        self.assertTrue(evidence_authority.validate_inventory(self.inventory).ok)

    def test_historical_view_rejects_metadata_drift_and_preserves_every_other_field(self):
        before, _, fingerprints = standalone_binding_reference()
        metadata_changes = (
            {"payload_paths": self.fact.payload_paths[:-1]},
            {"payload_paths": (*self.fact.payload_paths, "CMakeLists.txt")},
            {"primary_unit": "B14"}, {"consumer_units": ("B14",)},
        )
        for change in metadata_changes:
            with self.subTest(change=change), self.assertRaises(ValueError):
                prior_standalone_binding_facts((dataclasses.replace(self.fact, **change),))
        with self.assertRaises(ValueError):
            prior_standalone_binding_facts((self.fact, self.fact))
        with self.assertRaises(ValueError):
            prior_standalone_binding_facts((before,))
        changes = (
            {"evidence_value": "0" * 64}, {"key": self.KEY + "/other"},
            {"family": "other"}, {"owner_path": self.OWNER + ".other"},
            {"kind": "other"}, {"scope": self.fact.scope + "/other"},
            {"authority_class": evidence_authority.AuthorityClass.PERMANENT_INVARIANT},
        )
        for change in changes:
            changed = dataclasses.replace(self.fact, **change)
            with self.subTest(change=change):
                current = tuple(changed if fact.key == self.KEY else fact for fact in self.inventory.facts)
                historical = prior_standalone_binding_facts(current)
                selected, = (fact for fact in historical if fact.key == changed.key)
                for field, value in change.items():
                    self.assertEqual(value, getattr(selected, field))
                self.assertNotEqual(
                    fingerprints[self.fact.family],
                    fact_set_digest(fact for fact in historical if fact.family == self.fact.family),
                )
        unrelated = tuple(fact for fact in self.inventory.facts if fact.key != self.KEY)
        self.assertEqual(unrelated, prior_standalone_binding_facts(unrelated))
        self.assertNotEqual(
            fingerprints[self.fact.family],
            fact_set_digest(fact for fact in unrelated if fact.family == self.fact.family),
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
