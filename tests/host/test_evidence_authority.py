import ast
from collections import Counter
from contextlib import ExitStack
import copy
import dataclasses
from functools import lru_cache
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

from tests.host import evidence_authority
from tests.host import evidence_authority_inventory
from tests.host.ast_evidence import ast_dump
from tests.host.evidence_authority_source import literal_value, single_assignment
from tests.host.evidence_authority_types import digest, fact_set_digest
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


def memory_native_binding_reference(root=ROOT):
    contract = (root / "docs/architecture/memory-native-source-authority.md").read_text()
    policy = re.search(r"^commit: ([0-9a-f]{40})$", contract, re.MULTILINE).group(1)
    tree = re.search(r"^tree:   ([0-9a-f]{40})$", contract, re.MULTILINE).group(1)
    return _memory_native_binding_reference(root, policy, tree)


def memory_native_binding_row(task, kind, path, contract, name=""):
    assertion = "tests/host/" + (
        "bootstrap_allocation_source.py" if task == "MEM01" else "kernel_mapping_source.py"
    )
    assertions = (
        (assertion, "tools/generate_kernel_mapping_fixture.py")
        if kind == "native-input" else (assertion,)
    )
    return (
        task + "/" + kind + "/" + path + ("/" + name if name else ""),
        path, assertions, contract,
    )


def memory_native_function_contract(signature, guards, content, linkage="unconstrained"):
    return {
        "comparison": "function", "signature": signature,
        "guards": guards, "content": content, "linkage": linkage,
    }


def memory_native_lexical_digest(text):
    from tests.host.c_source_records import compact
    from tests.host.test_frame_ownership_source_layout import c_lexical_text

    return hashlib.sha256(compact(c_lexical_text(text)).encode()).hexdigest()


def memory_native_skeleton_digest(text, records):
    for record in records.values():
        fragment = record.signature + "{" + record.body + "}"
        if text.count(fragment) != 1:
            raise ValueError("fixed native reference has ambiguous function fragments")
        text = text.replace(fragment, "", 1)
    return memory_native_lexical_digest(text)


def memory_native_allocation_rows(source_at, projection):
    from tests.host.c_source_records import compact, function_records, tokens
    from tests.host.test_frame_ownership_source_layout import c_lexical_text

    reference = projection["source_reference"]
    rows = []
    for path, mutable in projection["mutable_functions"].items():
        text = source_at(reference, path)
        records = function_records(text)
        helpers = projection["new_helpers"].get(path, ())
        for name, record in records.items():
            content = ("lexical-body-sha256", memory_native_lexical_digest(record.body))
            if name in mutable:
                body = tokens(c_lexical_text(record.body))
                calls = {
                    left for left, right in zip(body, body[1:])
                    if right == "(" and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", left)
                } - {
                    "if", "while", "for", "switch", "return", "sizeof", "_Alignof",
                    "UINT64_C", "UINT32_C", "UINT16_C",
                }
                if name == "bootstrap_state_is_valid":
                    calls = {"bootstrap_bitmap_is_valid", "bootstrap_ranges_are_valid"}
                elif name == "commit_process_release":
                    calls.add("micros_frame_ownership_allocator_mark_free")
                elif name == "micros_frame_ownership_validate":
                    calls.add("allocated_prefix_is_valid")
                content = ("calls", tuple(sorted(calls)))
            rows.append(memory_native_binding_row("MEM01", "function", path,
                memory_native_function_contract(
                    ("compact", compact(record.signature)), record.guards, content,
                ), name,
            ))
        for name, signature, calls in helpers:
            rows.append(memory_native_binding_row("MEM01", "function", path,
                memory_native_function_contract(
                    ("compact", compact(signature)), (), ("calls", tuple(sorted(calls))),
                ), name,
            ))
        rows.append(memory_native_binding_row("MEM01", "definitions", path, {
            "comparison": "definition-set",
            "expected": tuple(sorted(set(records) | {helper[0] for helper in helpers})),
        }))
        rows.append(memory_native_binding_row("MEM01", "skeleton", path, {
            "comparison": "lexical-skeleton-sha256",
            "expected": memory_native_skeleton_digest(text, records),
        }))
    path = "include/micros/frame_allocator.h"
    text = source_at(reference, path)
    anchor = "    uint64_t allocated_bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];"
    if text.count(anchor) != 1:
        raise ValueError("fixed native reference lost its original cursor anchor")
    rows.append(memory_native_binding_row("MEM01", "header", path, {
        "comparison": "lexical-header-sha256",
        "expected": memory_native_lexical_digest(text.replace(
            anchor, "    uint64_t allocation_cursor;\n" + anchor, 1,
        )),
    }))
    rows.append(memory_native_binding_row("MEM01", "test-guard", "kernel/memory/bootstrap_memory.c", {
        "comparison": "exact-test-wrapper", "function": "managed_frame_is_allowed",
        "opening": "#if defined(MICROS_BUILD_FRAME_ALLOCATOR_TEST)\n",
        "closing": "\n#endif", "occurrences": 1,
    }, "managed_frame_is_allowed"))
    return rows


def memory_native_mapping_rows(source_at, projection, policy_tree, native):
    from tests.host.c_source_records import compact, function_records

    reference = projection["source_reference"]
    subjects = (
        "kernel/memory/address_space.c", "kernel/memory/address_space_validation.c",
        "kernel/memory/address_space_internal.h",
    )
    rows = [
        memory_native_binding_row("MEM02", "native-input", path, {
            "comparison": "file-bytes-sha256", "expected": hashlib.sha256(data).hexdigest(),
        }) for path, data in native.items() if path not in subjects
    ]
    calls_function, = (
        node for node in policy_tree.body if isinstance(node, ast.FunctionDef)
        and node.name == "_current_calls"
    )
    if len(calls_function.body) != 1 or not isinstance(calls_function.body[0], ast.Return):
        raise ValueError("fixed native reference has a nonliteral mapping call table")
    call_sets = ast.literal_eval(calls_function.body[0].value)
    guard, = (
        node for node in policy_tree.body if isinstance(node, ast.FunctionDef)
        and node.name == "validate_retained_source"
    )
    exceptions, = (
        node.value for node in guard.body if isinstance(node, ast.Assign)
        and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name)
        and node.targets[0].id == "changed_signatures"
    )
    exceptions = ast.literal_eval(exceptions)
    bindings = projection["current_bindings"]
    for path, owner in zip(subjects[:2], ("constructor", "validator")):
        text = source_at(reference, path)
        records = function_records(text)
        names = (set(records) - set(bindings[owner + "_removed"])) | set(bindings[owner + "_added"])
        for name in sorted(names):
            signature = (
                ("compact", compact(records[name].signature))
                if name in records and name not in exceptions else ("unconstrained",)
            )
            content = (
                ("lexical-body-sha256", memory_native_lexical_digest(records[name].body))
                if name in projection["retained_" + owner] else ("calls", tuple(sorted(call_sets[name])))
            )
            linkage = (
                "static" if name in bindings[owner + "_added"]
                and name != "micros_kernel_address_space_table_frame_is_valid" else "unconstrained"
            )
            rows.append(memory_native_binding_row("MEM02", "function", path,
                memory_native_function_contract(signature, (), content, linkage), name,
            ))
        rows.append(memory_native_binding_row("MEM02", "definitions", path, {
            "comparison": "definition-set", "expected": tuple(sorted(names)),
        }))
        rows.append(memory_native_binding_row("MEM02", "skeleton", path, {
            "comparison": "lexical-skeleton-sha256",
            "expected": memory_native_skeleton_digest(text, records),
        }))
    path = subjects[2]
    text = source_at(reference, path)
    policy = bindings["private_header"]
    text = text.replace(policy["array_anchor"], policy["array_additions"] + policy["array_anchor"], 1)
    text = text.replace(
        "struct micros_frame_allocator;",
        "struct micros_frame_allocator;\n" + policy["forward"] + "\n\n" + policy["helper"] + ";\n", 1,
    )
    rows.append(memory_native_binding_row("MEM02", "header", path, {
        "comparison": "lexical-header-sha256", "expected": memory_native_lexical_digest(text),
    }))
    rows.append(memory_native_binding_row("MEM02", "include", subjects[1], {
        "comparison": "exact-include", "text": '#include "micros/bootstrap_memory.h"\n',
        "occurrences": 1,
    }, "micros-bootstrap-memory"))
    return rows


@lru_cache(maxsize=4)
def _memory_native_binding_reference(root, policy, policy_tree):
    def fixed_bytes(reference, path):
        return subprocess.check_output(
            ("git", "show", reference + ":" + path), cwd=root, timeout=30,
        )

    def source_at(reference, path):
        return fixed_bytes(reference, path).decode()

    observed_tree = subprocess.check_output(
        ("git", "rev-parse", policy + "^{tree}"), cwd=root, text=True, timeout=30,
    ).strip()
    if observed_tree != policy_tree:
        raise ValueError("fixed native policy commit/tree disagree")
    allocation_tree = ast.parse(source_at(policy, "tests/host/bootstrap_allocation_source.py"))
    mapping_tree = ast.parse(source_at(policy, "tests/host/kernel_mapping_source.py"))
    allocation = literal_value(single_assignment(allocation_tree, "SOURCE_PROJECTION").value)
    mapping = literal_value(single_assignment(mapping_tree, "SOURCE_PROJECTION").value)
    recipe = literal_value(single_assignment(mapping_tree, "RECIPE_ADMISSION").value)
    members = set(filter(None, subprocess.check_output(
        ("git", "ls-tree", "-r", "--name-only", "-z", mapping["source_reference"]),
        cwd=root, timeout=30,
    ).decode().split("\0")))
    selected = {
        name for name in members
        if name.endswith(".h") and name.startswith(("include/", "arch/", "kernel/"))
    } | set(recipe["native_source_members"])
    archive = subprocess.check_output(
        ("git", "archive", mapping["source_reference"], *sorted(selected)), cwd=root, timeout=30,
    )
    native = {}
    with tarfile.open(fileobj=io.BytesIO(archive)) as entries:
        for entry in entries:
            if entry.isdir():
                continue
            if not entry.isfile() or entry.name not in selected or entry.name in native:
                raise ValueError("fixed native reference has an unexpected archive entry")
            native[entry.name] = entries.extractfile(entry).read()
    if len(native) != 186 or set(native) != selected:
        raise ValueError("fixed native reference lost a source/header")
    reference = (
        ("policy", policy, policy_tree),
        ("MEM01", allocation["source_reference"]),
        ("MEM02", mapping["source_reference"]),
    )
    rows = tuple(sorted((
        *memory_native_allocation_rows(source_at, allocation),
        *memory_native_mapping_rows(source_at, mapping, mapping_tree, native),
    )))
    if len(rows) != 328 or len({row[0] for row in rows}) != 328:
        raise ValueError("fixed native reference lost a current-source assertion")
    inventory_tree = ast.parse(source_at(policy, "tests/host/evidence_authority_inventory.py"))
    fingerprints = literal_value(single_assignment(inventory_tree, "DECLARED_FAMILY_FACT_DIGESTS").value)
    return reference, rows, fingerprints


def selected_ownership_native_reference(root=ROOT):
    contract = (root / "docs/architecture/selected-ownership-native-source-authority.md").read_text()
    policy = re.search(r"^policy commit:\s+([0-9a-f]{40})$", contract, re.MULTILINE).group(1)
    tree = re.search(r"^policy tree:\s+([0-9a-f]{40})$", contract, re.MULTILINE).group(1)
    return _selected_ownership_native_reference(root, policy, tree)


@lru_cache(maxsize=4)
def _selected_ownership_native_reference(root, policy, policy_tree):
    from tests.host.c_source_records import compact, function_records

    def fixed_bytes(reference, path):
        return subprocess.check_output(
            ("git", "show", reference + ":" + path), cwd=root, timeout=30,
        )

    observed_tree = subprocess.check_output(
        ("git", "rev-parse", policy + "^{tree}"), cwd=root, text=True, timeout=30,
    ).strip()
    if observed_tree != policy_tree:
        raise ValueError("fixed selected-ownership policy commit/tree disagree")
    owner = "tests/host/selected_ownership_source.py"
    tree = ast.parse(fixed_bytes(policy, owner).decode())
    roles, projection, recipe = (
        literal_value(single_assignment(tree, name).value)
        for name in ("SOURCE_ROLES", "SOURCE_PROJECTION", "RECIPE_ADMISSION")
    )
    names = {"_function_calls", "_current_calls", "_helper_contracts", "header_additions", "_source_skeleton"}
    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
    if len(functions) != len(names):
        raise ValueError("fixed selected-ownership policy lost a comparison helper")
    namespace = {"re": re}
    # Only these pure helpers from the fixed commit execute, never candidate declarations.
    exec(compile(ast.Module(body=functions, type_ignores=[]), policy + ":" + owner, "exec"), namespace)
    runtime = projection["source_reference"]
    reference = (("policy", policy, policy_tree), ("MEM03", runtime))
    rows = []

    def append(kind, path, contract, name=""):
        generator = "tools/generate_selected_ownership_fixture.py"
        assertions = (
            (owner, generator) if path.endswith(".c") else (generator,)
        ) if kind == "native-input" else (owner,)
        rows.append((
            "MEM03/" + kind + "/" + path + ("/" + name if name else ""),
            path, assertions, contract,
        ))

    helpers = namespace["_helper_contracts"]()
    for path in roles["production_paths"]:
        original = fixed_bytes(runtime, path).decode()
        if path.endswith(".h"):
            if original.count("\n#endif") != 1:
                raise ValueError("fixed selected-ownership header has an unexpected boundary")
            expected = original.replace(
                "\n#endif", "\n" + "\n".join(namespace["header_additions"](path)) + "\n\n#endif", 1,
            )
            append("header", path, {
                "comparison": "compact-header-sha256",
                "expected": hashlib.sha256(compact(expected).encode()).hexdigest(),
            })
            continue
        records = function_records(original)
        additions = projection["new_helpers"].get(path, ())
        for name, record in records.items():
            content = (
                ("calls", tuple(sorted(namespace["_current_calls"](name, record.body))))
                if name in projection["mutable_functions"][path] else
                ("body-bytes-sha256", hashlib.sha256(record.body.encode()).hexdigest())
            )
            append("function", path, {
                "comparison": "function", "signature": compact(record.signature),
                "guards": record.guards, "content": content,
            }, name)
        for name in additions:
            signature, calls = helpers[name]
            append("function", path, {
                "comparison": "function", "signature": compact(signature),
                "guards": (), "content": ("calls", tuple(sorted(calls))),
            }, name)
        append("definitions", path, {
            "comparison": "definition-set", "expected": tuple(sorted(set(records) | set(additions))),
        })
        append("skeleton", path, {
            "comparison": "compact-skeleton-sha256",
            "expected": hashlib.sha256(namespace["_source_skeleton"](original, records).encode()).hexdigest(),
        })

    members = set(filter(None, subprocess.check_output(
        ("git", "ls-tree", "-r", "--name-only", "-z", runtime), cwd=root, timeout=30,
    ).decode().split("\0")))
    selected = {
        path for path in members if path.endswith(".h") and path.startswith(("include/", "arch/", "kernel/"))
    } | set(recipe["native_source_members"])
    data = subprocess.check_output(("git", "archive", runtime, *sorted(selected)), cwd=root, timeout=30)
    native = {}
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for member in archive:
            if member.isdir():
                continue
            if not member.isfile() or member.name not in selected or member.name in native:
                raise ValueError("fixed selected-ownership archive has an unexpected entry")
            native[member.name] = archive.extractfile(member).read()
    if len(native) != 207 or set(native) != selected:
        raise ValueError("fixed selected-ownership native membership changed")
    for path, data in native.items():
        if path not in roles["production_paths"]:
            append("native-input", path, {
                "comparison": "file-bytes-sha256", "expected": hashlib.sha256(data).hexdigest(),
            })
    rows = tuple(sorted(rows))
    if len(rows) != len({row[0] for row in rows}) or len(rows) != 484:
        raise ValueError("fixed selected-ownership clause census changed")
    inventory_tree = ast.parse(fixed_bytes(policy, "tests/host/evidence_authority_inventory.py").decode())
    previous = literal_value(single_assignment(inventory_tree, "DECLARED_FAMILY_FACT_DIGESTS").value)
    return reference, rows, previous


def selected_address_space_production_rows(fixed_bytes, runtime, roles, projection, recipe, subsets, helpers):
    from tests.host.c_source_records import compact, function_records

    owner = "tests/host/selected_address_space_source.py"
    rows = []

    def append(kind, path, contract, name=""):
        rows.append((
            "MEM04/" + kind + "/" + path + ("/" + name if name else ""),
            path, (owner,), contract,
        ))

    added_contracts = helpers["_helper_contracts"]()
    for path in roles["production_paths"]:
        original = fixed_bytes(runtime, path).decode()
        if path in roles["dependent_production_paths"]:
            append("dependent-file", path, {
                "comparison": "file-bytes-sha256",
                "expected": hashlib.sha256(helpers["extend_handoff_preflight"](original).encode()).hexdigest(),
            })
            continue
        if path.endswith(".h"):
            append("header", path, {
                "comparison": "file-bytes-sha256",
                "expected": hashlib.sha256(helpers["extended_header"](path, original).encode()).hexdigest(),
            })
            continue
        before = function_records(original)
        additions = projection["new_helpers"].get(path, ())
        removed = projection["removed_functions"].get(path, ())
        mutable = projection["mutable_functions"][path]
        for name, record in before.items():
            if name in removed:
                continue
            signature = compact(record.signature)
            if name not in mutable:
                content = ("body-bytes-sha256", hashlib.sha256(record.body.encode()).hexdigest())
            else:
                signature = helpers["_current_signature"](name, record.signature)
                calls = tuple(sorted(helpers["_current_calls"](name, record.body)))
                dependent = helpers["_dependent_body"](path, name, record.body)
                content = ("calls", calls) if dependent is None else (
                    "calls-and-compact-body-sha256", calls, hashlib.sha256(dependent.encode()).hexdigest(),
                )
            append("function", path, {
                "comparison": "function", "signature": signature,
                "guards": record.guards, "content": content,
            }, name)
        for name in additions:
            signature, calls = added_contracts[name]
            append("function", path, {
                "comparison": "function", "signature": compact(signature),
                "guards": (), "content": ("calls", tuple(sorted(calls))),
            }, name)
        current = tuple(sorted(set(before).difference(removed) | set(additions)))
        append("definitions", path, {"comparison": "definition-set", "expected": current})
        append("skeleton", path, {
            "comparison": "compact-skeleton-sha256",
            "expected": hashlib.sha256(helpers["_extended_skeleton"](path, original).encode()).hexdigest(),
        })
        if path in recipe["native_source_members"]:
            selected = subsets.get(path, current)
            if path == "kernel/vm/vm_handoff_runtime.c":
                selected = (*selected, "micros_vm_handoff_runtime_inspect_scratch_binding")
            for name in sorted(set(selected) & (set(additions) | set(mutable))):
                append("observation", path, {
                    "comparison": "current-observation-shape",
                    "automatic": projection["current_automatic_primitives"].get(name, ()),
                    "explicit": projection["current_extra_primitives"].get(name, ()),
                }, name)
    return rows


def selected_address_space_native_reference(root=ROOT):
    contract = (root / "docs/architecture/selected-address-space-native-source-authority.md").read_text()
    policy = re.search(r"^policy commit:\s+([0-9a-f]{40})$", contract, re.MULTILINE).group(1)
    tree = re.search(r"^policy tree:\s+([0-9a-f]{40})$", contract, re.MULTILINE).group(1)
    return _selected_address_space_native_reference(root, policy, tree)


@lru_cache(maxsize=4)
def _selected_address_space_native_reference(root, policy, policy_tree):
    from tests.host.c_source_records import function_records

    def fixed_bytes(reference, path):
        return subprocess.check_output(("git", "show", reference + ":" + path), cwd=root, timeout=30)

    observed_tree = subprocess.check_output(
        ("git", "rev-parse", policy + "^{tree}"), cwd=root, text=True, timeout=30,
    ).strip()
    if observed_tree != policy_tree:
        raise ValueError("fixed selected-address-space policy commit/tree disagree")
    owner = "tests/host/selected_address_space_source.py"
    tree = ast.parse(fixed_bytes(policy, owner).decode())
    roles, projection, recipe = (
        literal_value(single_assignment(tree, name).value)
        for name in ("SOURCE_ROLES", "SOURCE_PROJECTION", "RECIPE_ADMISSION")
    )
    names = {
        "_function_calls", "_current_signature", "_current_calls", "_helper_contracts",
        "header_additions", "extended_header", "_dependent_body", "_source_skeleton",
        "_extended_skeleton", "extend_handoff_preflight",
    }
    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
    if len(functions) != len(names):
        raise ValueError("fixed selected-address-space policy lost a comparison helper")
    namespace = {"function_records": function_records, "SOURCE_PROJECTION": projection}
    # Execute only pure comparison helpers from fixed Git, never the candidate declaration.
    exec(compile(ast.Module(body=functions, type_ignores=[]), policy + ":" + owner, "exec"), namespace)
    subset_node = single_assignment(tree, "NATIVE_FUNCTION_SUBSETS").value
    if not isinstance(subset_node, ast.Dict):
        raise ValueError("fixed selected-address-space subsets are not a dictionary")
    subsets = {
        literal_value(key): literal_value(value)
        for key, value in zip(subset_node.keys, subset_node.values)
        if literal_value(key) in roles["production_paths"]
    }
    runtime = projection["source_reference"]
    reference = (("policy", policy, policy_tree), ("MEM04", runtime))
    rows = selected_address_space_production_rows(
        fixed_bytes, runtime, roles, projection, recipe, subsets, namespace,
    )
    members = set(filter(None, subprocess.check_output(
        ("git", "ls-tree", "-r", "--name-only", "-z", runtime), cwd=root, timeout=30,
    ).decode().split("\0")))
    selected = {
        path for path in members if path.endswith(".h") and path.startswith(("arch/", "include/", "kernel/"))
    } | set(recipe["native_source_members"])
    data = subprocess.check_output(("git", "archive", runtime, *sorted(selected)), cwd=root, timeout=30)
    native = {}
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for member in archive:
            if member.isdir():
                continue
            if not member.isfile() or member.name not in selected or member.name in native:
                raise ValueError("fixed selected-address-space archive has an unexpected entry")
            native[member.name] = archive.extractfile(member).read()
    if len(native) != 212 or set(native) != selected:
        raise ValueError("fixed selected-address-space native membership changed")
    for path, data in native.items():
        if path not in roles["production_paths"]:
            rows.append(("MEM04/native-input/" + path, path, (owner,), {
                "comparison": "file-bytes-sha256", "expected": hashlib.sha256(data).hexdigest(),
            }))
    rows = tuple(sorted(rows))
    if len(rows) != len({row[0] for row in rows}) or len(rows) != 477:
        raise ValueError("fixed selected-address-space clause census changed")
    inventory_tree = ast.parse(fixed_bytes(policy, "tests/host/evidence_authority_inventory.py").decode())
    previous = literal_value(single_assignment(inventory_tree, "DECLARED_FAMILY_FACT_DIGESTS").value)
    return reference, rows, previous


def selected_ownership_observation_reference(root=ROOT):
    contract = (root / "docs/architecture/selected-ownership-observation-authority.md").read_text()
    policy = re.search(r"^policy commit:\s+([0-9a-f]{40})$", contract, re.MULTILINE).group(1)
    tree = re.search(r"^policy tree:\s+([0-9a-f]{40})$", contract, re.MULTILINE).group(1)
    return _selected_ownership_observation_reference(root, policy, tree)


@lru_cache(maxsize=4)
def _selected_ownership_observation_reference(root, policy, policy_tree):
    from tests.host.work_count_lexical import function_spans

    def fixed_source(reference, path):
        return subprocess.check_output(
            ("git", "show", reference + ":" + path), cwd=root, text=True, timeout=30,
        )

    observed_tree = subprocess.check_output(
        ("git", "rev-parse", policy + "^{tree}"), cwd=root, text=True, timeout=30,
    ).strip()
    if observed_tree != policy_tree:
        raise ValueError("fixed ownership-observation policy commit/tree disagree")
    owner = "tests/host/selected_ownership_source.py"
    tree = ast.parse(fixed_source(policy, owner))
    namespace = {
        name: literal_value(single_assignment(tree, name).value)
        for name in (
            "SOURCE_PROJECTION", "RECIPE_ADMISSION", "OBSERVED_MEMORY_PATHS", "OBSERVED_OBJECT_PATHS",
            "OBSERVED_MEMORY_FUNCTIONS", "OBSERVED_FIELDS", "REPLACED_OWNER_READS",
            "ORIGINAL_OBSERVATION_DIGEST", "CURRENT_OBSERVATION_DIGEST",
        )
    }
    projection = namespace["SOURCE_PROJECTION"]
    runtime = projection["source_reference"]
    namespace.update({
        "baseline_source": lambda _, path: fixed_source(runtime, path),
        "digest": digest, "lru_cache": lru_cache,
    })
    names = {
        "_matching_tokens", "_region_header", "_original_regions", "_original_statement",
        "original_observations", "current_observations",
    }
    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
    if len(functions) != len(names):
        raise ValueError("fixed ownership-observation policy lost a comparison helper")
    # Reconstruct the catalogue from fixed Git helpers, never from the candidate declaration.
    exec(compile(ast.Module(body=functions, type_ignores=[]), policy + ":" + owner, "exec"), namespace)
    catalog = namespace["current_observations"](str(root))
    paths = (*namespace["OBSERVED_MEMORY_PATHS"], *namespace["OBSERVED_OBJECT_PATHS"])
    if len(paths) != len(set(paths)) or len(paths) != 17 or not set(paths) <= set(
        namespace["RECIPE_ADMISSION"]["native_source_members"],
    ):
        raise ValueError("fixed ownership-observation subjects differ from native membership")
    reference = (("policy", policy, policy_tree), ("MEM03", runtime))
    rows = []
    for path in paths:
        selected = namespace["OBSERVED_MEMORY_FUNCTIONS"].get(path)
        if path == "kernel/memory/bootstrap_memory.c":
            selected = projection["native_bootstrap_closure"]
        if selected is not None:
            selected = (*selected, *projection["new_helpers"].get(path, ()))
        spans = function_spans(fixed_source(policy, path))
        eligible = set(spans) if selected is None else set(spans) & set(selected)
        if (selected is not None and not set(selected) <= set(spans)) or not {
            site[0] for site in catalog.get(path, ())
        } <= eligible:
            raise ValueError("fixed ownership-observation function selection is incomplete")
        for name in sorted(eligible):
            rows.append(("MEM03/observation/" + path + "/" + name, path, (owner,), {
                "comparison": "current-primitive-bindings",
                "coverage": "memory-fields-and-loops" if path in namespace["OBSERVED_MEMORY_PATHS"] else "loops-only",
                "sites": tuple(site for site in catalog.get(path, ()) if site[0] == name),
            }))
    rows = tuple(sorted(rows))
    if (
        len(rows) != len({row[0] for row in rows}) or len(rows) != 120
        or sum(not row[3]["sites"] for row in rows) != 57
        or sum(len(row[3]["sites"]) for row in rows) != 179
    ):
        raise ValueError("fixed ownership-observation census changed")
    previous_tree = ast.parse(fixed_source(policy, "tests/host/evidence_authority_inventory.py"))
    previous = literal_value(single_assignment(previous_tree, "DECLARED_FAMILY_FACT_DIGESTS").value)
    return reference, rows, previous


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
        self.assertEqual(22, len(inventory.families))
        self.assertEqual(5379, len(inventory.facts))
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
        self.assertEqual(expected, {
            family: fingerprint for family, fingerprint in self.inventory.declared_family_fact_digests
            if family in expected
        })
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


class MemoryNativeBindingTests(unittest.TestCase):
    OWNER = "tests/host/memory_native_source_bindings.py"
    FAMILY = "memory-native-admission"

    @classmethod
    def setUpClass(cls):
        cls.reference, cls.rows, cls.previous = memory_native_binding_reference()
        cls.inventory = evidence_authority_inventory.current_inventory(ROOT)
        cls.facts = tuple(fact for fact in cls.inventory.facts if fact.family == cls.FAMILY)
        cls.declaration = (ROOT / cls.OWNER).read_text()

    @classmethod
    def expected_facts(cls):
        consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
        return tuple(evidence_authority.Fact(
            cls.OWNER + ":native-source-admission:" + scope, cls.FAMILY, cls.OWNER,
            "native-source-admission", scope, evidence_authority.AuthorityClass.TRANSPORT_IDENTITY,
            "B02", consumers, tuple(sorted({cls.OWNER, subject, *assertions})),
            digest((cls.reference, scope, subject, assertions, contract)),
        ) for scope, subject, assertions, contract in cls.rows)

    @classmethod
    def fixture(cls, directory):
        root = Path(directory)
        for path in {value for fact in cls.expected_facts() for value in fact.payload_paths}:
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(cls.declaration if path == cls.OWNER else "")
        return root

    @classmethod
    def materialize(cls, root):
        return evidence_authority_inventory._facts_for_owner(root, cls.FAMILY, cls.OWNER)

    @staticmethod
    def declaration_text(reference, rows):
        return "BINDING_REFERENCE = " + repr(reference) + "\nNATIVE_SOURCE_BINDINGS = " + repr(rows) + "\n"

    def test_existing_current_rejections_have_individual_inventory_keys(self):
        from tests.host import bootstrap_allocation_source as allocation
        from tests.host import kernel_mapping_source as mapping
        from tests.host import memory_native_source_bindings as declarations
        from tests.host.work_count_types import Stage
        from tools import generate_kernel_mapping_fixture as generator

        subject = "kernel/memory/frame_allocator.c"
        current = (ROOT / subject).read_text()
        allocation.validate_retained_source(ROOT, subject, current, stage=Stage.GREEN)
        changed = current.replace(
            "right > UINT64_MAX - left",
            "right >= UINT64_MAX - left", 1,
        )
        self.assertNotEqual(current, changed)
        with self.assertRaisesRegex(ValueError, "retained original body"):
            allocation.validate_retained_source(ROOT, subject, changed, stage=Stage.GREEN)
        mapping.validate_retained_source(ROOT, subject, current, Stage.GREEN)
        with self.assertRaisesRegex(ValueError, "noncandidate native source changed"):
            mapping.validate_retained_source(ROOT, subject, changed, Stage.GREEN)

        generator.source_bundle(ROOT, Stage.GREEN, observed=False)
        read = generator.read_regular
        for selected in (subject, "include/micros/frame_allocator.h"):
            def changed_read(path):
                data, identity = read(path)
                return (data + b"\n" if path == ROOT / selected else data), identity

            with self.subTest(guard=selected), patch.object(
                generator, "read_regular", side_effect=changed_read,
            ), self.assertRaisesRegex(ValueError, "corrected native input"):
                generator.source_bundle(ROOT, Stage.GREEN, observed=False)

        declared = {row[0] for row in declarations.NATIVE_SOURCE_BINDINGS}
        keys = {fact.key for fact in evidence_authority_inventory.current_inventory(ROOT).facts}
        required = (
            "MEM01/function/" + subject + "/add_u64",
            "MEM02/native-input/" + subject,
            "MEM02/native-input/include/micros/frame_allocator.h",
        )
        for scope in required:
            with self.subTest(scope=scope):
                self.assertIn(scope, declared)
                self.assertTrue(
                    self.OWNER + ":native-source-admission:" + scope in keys,
                    "missing identity for an existing rejecting native-source assertion: " + scope,
                )

    def test_complete_bindings_equal_fixed_policy_and_runtime_source(self):
        from tests.host import memory_native_source_bindings as declarations

        self.assertEqual(self.reference, declarations.BINDING_REFERENCE)
        self.assertEqual(self.rows, declarations.NATIVE_SOURCE_BINDINGS)
        self.assertEqual(self.expected_facts(), self.facts)
        self.assertEqual(328, len(self.facts))
        self.assertEqual(
            fact_set_digest(self.expected_facts()),
            dict(self.inventory.declared_family_fact_digests)[self.FAMILY],
        )
        self.assertIn(self.OWNER, self.inventory.discovered_identity_modules)
        retained = tuple(fact for fact in self.inventory.facts if fact.family in self.previous)
        self.assertEqual(3954, len(retained))
        self.assertEqual(16, len(self.previous))
        for family, expected in self.previous.items():
            with self.subTest(family=family):
                self.assertEqual(
                    expected, fact_set_digest(fact for fact in retained if fact.family == family),
                )

    def test_declaration_is_literal_only_and_cannot_execute_or_rebind(self):
        reference = "BINDING_REFERENCE = " + repr(self.reference) + "\n"
        rows = "NATIVE_SOURCE_BINDINGS = " + repr(self.rows) + "\n"
        malformed = (
            "", reference, rows, reference + reference + rows,
            self.declaration + "\nextra = 1\n",
            self.declaration + "\nraise RuntimeError('must not execute')\n",
            self.declaration + "\ndef hidden():\n    BINDING_REFERENCE = 0\n",
            reference + "NATIVE_SOURCE_BINDINGS: tuple = " + repr(self.rows),
            reference + "NATIVE_SOURCE_BINDINGS = tuple()\n",
            reference + "NATIVE_SOURCE_BINDINGS = []\n",
            reference + "NATIVE_SOURCE_BINDINGS = True\n",
            reference + "NATIVE_SOURCE_BINDINGS = None\n",
        )
        with tempfile.TemporaryDirectory(prefix="micros-memory-native-literals-") as directory:
            root = self.fixture(directory)
            for index, text in enumerate(malformed):
                with self.subTest(case=index):
                    (root / self.OWNER).write_text(text)
                    with self.assertRaises(ValueError):
                        self.materialize(root)

    def test_scope_value_owner_and_reference_drift_cannot_rebaseline_bindings(self):
        first = self.rows[0]
        changed_contract = {**first[3], "expected": ("invented_definition",)}
        variants = {
            "missing": self.rows[:-1],
            "extra": (*self.rows, first),
            "duplicate": (*self.rows[:-1], first),
            "scope": ((first[0].replace("MEM01/", "MEM99/", 1), *first[1:]), *self.rows[1:]),
            "subject": ((first[0], "kernel/memory/frame_ownership.c", *first[2:]), *self.rows[1:]),
            "assertion": ((*first[:2], ("tests/host/kernel_mapping_source.py",), first[3]), *self.rows[1:]),
            "comparison": ((*first[:3], {**first[3], "comparison": "other"}), *self.rows[1:]),
            "value": ((*first[:3], changed_contract), *self.rows[1:]),
            "field": ((*first[:3], {**first[3], "extra": 0}), *self.rows[1:]),
        }
        with tempfile.TemporaryDirectory(prefix="micros-memory-native-drift-") as directory:
            root = self.fixture(directory)
            for label, rows in variants.items():
                with self.subTest(case=label):
                    (root / self.OWNER).write_text(self.declaration_text(self.reference, rows))
                    with self.assertRaises(ValueError):
                        self.materialize(root)
            changed = (
                ("policy", "0" * 40, self.reference[0][2]), *self.reference[1:],
            )
            (root / self.OWNER).write_text(self.declaration_text(changed, self.rows))
            with self.assertRaises(ValueError):
                self.materialize(root)

    def test_payloads_are_exact_ordinary_files_not_candidate_expectations(self):
        selected = (
            self.OWNER, "kernel/memory/frame_ownership_validation.c",
            "include/micros/frame_ownership.h",
            "tests/host/bootstrap_allocation_source.py",
            "tools/generate_kernel_mapping_fixture.py",
        )
        with tempfile.TemporaryDirectory(prefix="micros-memory-native-paths-") as directory:
            root = self.fixture(directory)
            self.assertEqual(self.expected_facts(), self.materialize(root))
            subject = root / "kernel/memory/frame_ownership_validation.c"
            subject.write_text("changed candidate bytes are not a new historical expectation\n")
            self.assertEqual(self.expected_facts(), self.materialize(root))
            peer = root / "payload-peer"
            peer.write_text("not an admitted source")
            for relative in selected:
                path = root / relative
                saved = path.read_bytes()
                for kind in ("missing", "symlink", "hardlink"):
                    with self.subTest(path=relative, kind=kind):
                        path.unlink()
                        if kind == "symlink":
                            path.symlink_to(peer)
                        elif kind == "hardlink":
                            os.link(peer, path)
                        with self.assertRaises(ValueError):
                            self.materialize(root)
                        if path.exists() or path.is_symlink():
                            path.unlink()
                        path.write_bytes(saved)
            parent = root / "kernel/memory"
            moved = root / "saved-memory"
            parent.rename(moved)
            parent.symlink_to(moved, target_is_directory=True)
            with self.assertRaises(ValueError):
                self.materialize(root)
            parent.unlink()
            moved.rename(parent)

    def test_selected_clause_closure_preserves_real_overlap_rejection(self):
        scopes = {
            "MEM01/function/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_vm_transition_preflight",
            "MEM02/native-input/kernel/memory/frame_ownership_validation.c",
            "MEM02/native-input/include/micros/frame_ownership.h",
        }
        facts = tuple(fact for fact in self.facts if fact.scope in scopes)
        self.assertEqual(3, len(facts))
        keys = tuple(fact.key for fact in facts)
        units = facts[0].consumer_units
        subjects = ("include/micros/frame_ownership.h", "kernel/memory/frame_ownership_validation.c")
        paths = tuple(sorted({path for fact in facts for path in fact.payload_paths}))
        plan = evidence_authority.synthetic_plan(evidence_authority.synthetic_inventory())
        plan = dataclasses.replace(
            plan, owner_unit="B02", finding_ids=("B02-F999",),
            affected_identity_keys=keys, retained_identity_keys=(),
            planned_retirement_identity_keys=keys,
            replacement_oracles=(dataclasses.replace(
                plan.replacement_oracles[0], replaces_identity_keys=keys,
            ),),
            recertification_units=units,
            unit_commands=tuple((unit, ("validate-" + unit.lower(),)) for unit in units),
        )
        authority = dataclasses.replace(
            evidence_authority.synthetic_task_authorities(plan)[plan.owner_task],
            command_input_paths=(
                "tests/host/c_source_records.py", "tests/host/work_count_evidence.py",
                "tools/stack_bound_assembly.py",
            ),
            unit_input_paths=tuple((unit, subjects) for unit in units),
        )
        environment = evidence_authority.synthetic_environment(plan)
        immutable = dict(environment.immutable_inputs)
        del immutable["tools/command_registry.py"]
        immutable.update(
            (path, hashlib.sha256((ROOT / path).read_bytes()).hexdigest())
            for path in authority.command_input_paths
        )
        candidate = tuple((path, hashlib.sha256((ROOT / path).read_bytes()).hexdigest()) for path in paths)
        selected = tuple(item for item in candidate if item[0] in subjects)
        environment = dataclasses.replace(
            environment, immutable_inputs=tuple(sorted(immutable.items())),
            red_candidate_inputs=candidate, current_candidate_inputs=candidate,
            unit_inputs=tuple((unit, selected) for unit in units),
        )
        authorities = {plan.owner_task: authority}
        registry = evidence_authority.synthetic_transition_registry(plan)
        result = evidence_authority.validate_transition(self.inventory, authorities, registry, plan, (), environment)
        self.assertTrue(result.ok, result)
        self.assertIs(evidence_authority.TransitionState.REPLACEMENT_REGISTERED, result.state)
        self.assertFalse(result.retired_identity_keys)
        for overlap in (subjects[1], self.OWNER):
            with self.subTest(overlap=overlap):
                changed_authority = dataclasses.replace(
                    authority, command_input_paths=(*authority.command_input_paths, overlap),
                )
                changed_environment = dataclasses.replace(
                    environment,
                    immutable_inputs=(*environment.immutable_inputs, (overlap, dict(candidate)[overlap])),
                )
                result = evidence_authority.validate_transition(
                    self.inventory, {plan.owner_task: changed_authority}, registry,
                    plan, (), changed_environment,
                )
                self.assertIs(evidence_authority.ErrorCode.PROJECTION, result.error)

    def test_all_prior_committed_claims_remain_journal_free(self):
        from tests.host import (
            bootstrap_allocation_authority, diagnostic_history_authority,
            kernel_mapping_authority, runtime_memory_authority,
        )

        modules = (
            bootstrap_allocation_authority, diagnostic_history_authority,
            kernel_mapping_authority, runtime_memory_authority,
        )
        with ExitStack() as stack:
            for module in modules:
                for name in ("read_current", "read_journal", "validate_journal"):
                    stack.enter_context(patch.object(
                        module, name, side_effect=AssertionError("ordinary authority must not use a journal"),
                    ))
            _, authorities, _, registry = kernel_mapping_authority.task_definition(ROOT)
            self.assertEqual(4, len(registry.claims))
            self.assertEqual(30, sum(len(claim.claimed_identity_keys) for claim in registry.claims))
            result = evidence_authority.validate_transition(
                self.inventory, authorities, registry, None, (), None,
            )
            self.assertTrue(result.ok, result)
            retired = {key for claim in registry.claims for key in claim.claimed_identity_keys}
            self.assertEqual(
                {fact.key for fact in self.inventory.facts} - retired,
                set(result.active_identity_keys),
            )
            self.assertFalse(runtime_memory_authority.memory_identity_required(ROOT))
            for claim in registry.claims:
                if claim.owner_task == "COPY01":
                    continue
                key = claim.claimed_identity_keys[0]
                self.assertFalse(kernel_mapping_authority.identity_required(ROOT, key), key)


class SelectedOwnershipObservationBindingTests(unittest.TestCase):
    OWNER = "tests/host/selected_ownership_observation_bindings.py"
    FAMILY = "selected-ownership-observation-admission"

    @classmethod
    def setUpClass(cls):
        cls.reference, cls.rows, cls.previous = selected_ownership_observation_reference()
        cls.inventory = evidence_authority_inventory.current_inventory(ROOT)
        cls.facts = tuple(fact for fact in cls.inventory.facts if fact.family == cls.FAMILY)
        cls.declaration = (ROOT / cls.OWNER).read_text()

    @classmethod
    def expected_facts(cls):
        consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
        return tuple(evidence_authority.Fact(
            cls.OWNER + ":native-source-admission:" + scope, cls.FAMILY, cls.OWNER,
            "native-source-admission", scope, evidence_authority.AuthorityClass.TRANSPORT_IDENTITY,
            "B02", consumers, tuple(sorted({cls.OWNER, subject, *assertions})),
            digest((cls.reference, scope, subject, assertions, contract)),
        ) for scope, subject, assertions, contract in cls.rows)

    @classmethod
    def fixture(cls, directory):
        root = Path(directory)
        for path in {value for fact in cls.expected_facts() for value in fact.payload_paths}:
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(cls.declaration if path == cls.OWNER else "")
        return root

    @classmethod
    def materialize(cls, root):
        return evidence_authority_inventory._facts_for_owner(root, cls.FAMILY, cls.OWNER)

    def test_existing_observation_rejections_have_individual_inventory_keys(self):
        from tests.host import selected_ownership_source as source
        from tests.host.c_source_records import function_records
        from tests.host.work_count_types import Stage

        keys = {fact.key for fact in evidence_authority_inventory.current_inventory(ROOT).facts}
        cases = (
            (
                "kernel/memory/frame_ownership_bootstrap.c", "collect_process_release_summary",
                "frame_index < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES;",
                "(frame_index) < MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES;",
                "missing enclosing primitive region",
            ),
            (
                "kernel/memory/frame_ownership_bootstrap.c", "collect_process_release_summary",
                "release_bitmap[word_index]", "release_bitmap[(word_index)]",
                "primitive occurrence is missing or ambiguous: site-0069",
            ),
            (
                "kernel/memory/frame_ownership_validation.c", "micros_frame_ownership_validate_metadata",
                None, None, "missing, duplicated or undeclared primitive",
            ),
        )
        for path, name, before, after, reason in cases:
            with self.subTest(path=path, function=name, mutation=before):
                original = (ROOT / path).read_text()
                body = function_records(original)[name].body
                self.assertEqual(1, original.count(body))
                if before is None:
                    self.assertFalse(any(
                        site[0] == name for site in source.current_observations(str(ROOT))[path]
                    ))
                    changed_body = "\n    while (false) { }\n" + body
                else:
                    self.assertEqual(1, body.count(before))
                    changed_body = body.replace(before, after, 1)
                changed = original.replace(body, changed_body, 1)
                source.observe_source(ROOT, path, original, Stage.GREEN, "aggregate")
                source.validate_retained_source(ROOT, path, changed, Stage.GREEN)
                with self.assertRaisesRegex(ValueError, reason):
                    source.observe_source(ROOT, path, changed, Stage.GREEN, "aggregate")
                key = self.OWNER + ":native-source-admission:MEM03/observation/" + path + "/" + name
                self.assertTrue(key in keys, "missing existing observation identity: " + key)

    def test_complete_bindings_preserve_fixed_policy_and_every_predecessor(self):
        from tests.host import selected_ownership_observation_bindings as declarations

        self.assertEqual(self.reference, declarations.BINDING_REFERENCE)
        self.assertEqual(self.rows, declarations.NATIVE_SOURCE_BINDINGS)
        self.assertEqual(self.expected_facts(), self.facts)
        self.assertEqual(120, len(self.facts))
        self.assertTrue(evidence_authority.validate_inventory(self.inventory).ok)
        self.assertEqual((5379, 22), (len(self.inventory.facts), len(self.inventory.families)))
        self.assertEqual(
            fact_set_digest(self.expected_facts()),
            dict(self.inventory.declared_family_fact_digests)[self.FAMILY],
        )
        self.assertIn(self.OWNER, self.inventory.discovered_identity_modules)
        retained = tuple(fact for fact in self.inventory.facts if fact.family in self.previous)
        self.assertEqual(5259, len(retained))
        self.assertEqual(21, len(self.previous))
        for family, expected in self.previous.items():
            with self.subTest(family=family):
                self.assertEqual(expected, fact_set_digest(fact for fact in retained if fact.family == family))
        sites = tuple(site for row in self.rows for site in row[3]["sites"])
        self.assertEqual(57, sum(not row[3]["sites"] for row in self.rows))
        self.assertEqual(179, len(sites))
        self.assertEqual(179, len({site[1] for site in sites}))
        self.assertEqual({"before": 110, "loop-entry": 65, "entry": 4}, Counter(site[7] for site in sites))
        self.assertEqual(
            {"memory-fields-and-loops": 55, "loops-only": 65},
            Counter(row[3]["coverage"] for row in self.rows),
        )
        self.assertTrue(all(row[2] == ("tests/host/selected_ownership_source.py",) for row in self.rows))

    def test_empty_coverage_modes_and_current_observation_inverse_remain_distinct(self):
        from tests.host import selected_ownership_source as source
        from tests.host.c_source_records import function_records
        from tests.host.work_count_types import Stage

        for path in sorted({row[1] for row in self.rows}):
            with self.subTest(path=path):
                original = (ROOT / path).read_text()
                observed = source.observe_source(ROOT, path, original, Stage.GREEN, "aggregate")
                self.assertEqual(original, source.project_observations(ROOT, path, observed, Stage.GREEN, "aggregate"))
        for coverage in ("memory-fields-and-loops", "loops-only"):
            row = next(row for row in self.rows if row[3]["coverage"] == coverage and not row[3]["sites"])
            path, name = row[1], row[0].rsplit("/", 1)[1]
            original = (ROOT / path).read_text()
            record = function_records(original)[name]
            fragment = record.signature + "{" + record.body + "}"
            self.assertEqual(1, original.count(fragment))
            changed = original.replace(
                fragment,
                record.signature + "{\n    uint64_t release_bitmap[1] = {0};\n    (void)release_bitmap[0];\n"
                + record.body + "}", 1,
            )
            with self.subTest(coverage=coverage):
                if coverage == "memory-fields-and-loops":
                    with self.assertRaisesRegex(ValueError, "missing, duplicated or undeclared primitive"):
                        source._validate_observation_coverage(ROOT, path, changed, Stage.GREEN)
                else:
                    source._validate_observation_coverage(ROOT, path, changed, Stage.GREEN)
        path = "kernel/memory/frame_ownership_bootstrap.c"
        original = (ROOT / path).read_text()
        changed = original.replace(
            "release_bitmap[word_index]", "release_bitmap [ word_index ]", 1,
        )
        source.validate_retained_source(ROOT, path, changed, Stage.GREEN)
        observed = source.observe_source(ROOT, path, changed, Stage.GREEN, "aggregate")
        self.assertEqual(changed, source.project_observations(ROOT, path, observed, Stage.GREEN, "aggregate"))

    def test_literal_schema_and_site_drift_cannot_create_authority(self):
        def declaration(reference, rows):
            return "BINDING_REFERENCE = " + repr(reference) + "\nNATIVE_SOURCE_BINDINGS = " + repr(rows) + "\n"

        first = self.rows[0]
        variants = [
            "", "BINDING_REFERENCE = " + repr(self.reference),
            self.declaration + "\nimport os\n",
            self.declaration + "\nraise RuntimeError('must not execute')\n",
            self.declaration + "\ndef hidden():\n    BINDING_REFERENCE = 0\n",
            self.declaration + "\nBINDING_REFERENCE = " + repr(self.reference),
            "BINDING_REFERENCE = " + repr(self.reference) + "\nNATIVE_SOURCE_BINDINGS = tuple()\n",
            declaration(self.reference, list(self.rows)),
            declaration(self.reference, self.rows[:-1]),
            declaration(self.reference, (*self.rows, first)),
            declaration(self.reference, (*self.rows[:-1], first)),
            declaration(self.reference, tuple(reversed(self.rows))),
            declaration((("policy", "0" * 40, self.reference[0][2]), self.reference[1]), self.rows),
        ]
        changes = (
            (first[0].replace("MEM03/", "MEM99/", 1), *first[1:]),
            (first[0], "kernel/memory/user_address_space.c", *first[2:]),
            (*first[:2], ("tools/generate_selected_ownership_fixture.py",), first[3]),
            (*first[:3], {**first[3], "comparison": "other"}),
            (*first[:3], {**first[3], "coverage": "loops-only"}),
            (*first[:3], {**first[3], "sites": list(first[3]["sites"])}),
            (*first[:3], {**first[3], "extra": 0}),
        )
        variants.extend(declaration(self.reference, (changed, *self.rows[1:])) for changed in changes)
        index = next(
            index for index, row in enumerate(self.rows)
            if len(row[3]["sites"]) > 1 and row[3]["sites"][0][7] == "before"
        )
        row = self.rows[index]
        sites = row[3]["sites"]
        for field, value in (
            (0, "borrowed_function"), (1, "unknown-site"), (2, "KERNEL_OBJECTS"),
            (3, "unknown-category"), (4, "UNKNOWN_KIND"),
            (5, (("for ( unknown ) {", True),)), (6, sites[0][6] + " "),
            (7, "after"), (8, True), (9, ("borrowed_field", 0)),
        ):
            site = (*sites[0][:field], value, *sites[0][field + 1:])
            contract = {**row[3], "sites": (site, *sites[1:])}
            variants.append(declaration(self.reference, (
                *self.rows[:index], (*row[:3], contract), *self.rows[index + 1:],
            )))
        for changed_sites in ((sites[0], *sites), tuple(reversed(sites)), ()):
            variants.append(declaration(self.reference, (
                *self.rows[:index], (*row[:3], {**row[3], "sites": changed_sites}), *self.rows[index + 1:],
            )))
        with tempfile.TemporaryDirectory(prefix="micros-ownership-observation-schema-") as directory:
            root = self.fixture(directory)
            self.assertEqual(self.expected_facts(), self.materialize(root))
            for index, text in enumerate(variants):
                with self.subTest(case=index):
                    (root / self.OWNER).write_text(text)
                    with self.assertRaises(ValueError):
                        self.materialize(root)

    def test_payloads_are_exact_ordinary_files_not_candidate_expectations(self):
        selected = (
            self.OWNER, "kernel/memory/frame_ownership_bootstrap.c",
            "kernel/objects/kernel_object_identity.c", "tests/host/selected_ownership_source.py",
        )
        with tempfile.TemporaryDirectory(prefix="micros-ownership-observation-paths-") as directory:
            root = self.fixture(directory)
            self.assertEqual(self.expected_facts(), self.materialize(root))
            (root / selected[1]).write_text("candidate bytes cannot supply a fixed observation expectation\n")
            self.assertEqual(self.expected_facts(), self.materialize(root))
            peer = root / "payload-peer"
            peer.write_text("not an admitted source")
            for relative in selected:
                path = root / relative
                saved = path.read_bytes()
                for kind in ("missing", "symlink", "hardlink"):
                    with self.subTest(path=relative, kind=kind):
                        path.unlink()
                        if kind == "symlink":
                            path.symlink_to(peer)
                        elif kind == "hardlink":
                            os.link(peer, path)
                        with self.assertRaises(ValueError):
                            self.materialize(root)
                        if path.exists() or path.is_symlink():
                            path.unlink()
                        path.write_bytes(saved)
            parent = root / "kernel/memory"
            moved = root / "saved-memory"
            parent.rename(moved)
            parent.symlink_to(moved, target_is_directory=True)
            with self.assertRaises(ValueError):
                self.materialize(root)
            parent.unlink()
            moved.rename(parent)

    def test_selected_binding_closure_preserves_shared_policy_and_rejects_real_overlap(self):
        scopes = {
            "MEM03/observation/kernel/memory/frame_ownership_bootstrap.c/collect_process_release_summary",
            "MEM03/observation/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_validate_metadata",
            next(row[0] for row in self.rows if row[1] == "kernel/objects/kernel_object_identity.c"),
        }
        facts = tuple(fact for fact in self.facts if fact.scope in scopes)
        self.assertEqual(3, len(facts))
        keys = tuple(fact.key for fact in facts)
        units = facts[0].consumer_units
        subjects = tuple(sorted({row[1] for row in self.rows if row[0] in scopes}))
        paths = tuple(sorted({path for fact in facts for path in fact.payload_paths}))
        plan = evidence_authority.synthetic_plan(evidence_authority.synthetic_inventory())
        plan = dataclasses.replace(
            plan, owner_unit="B02", finding_ids=("B02-F999",),
            affected_identity_keys=keys, retained_identity_keys=(), planned_retirement_identity_keys=keys,
            replacement_oracles=(dataclasses.replace(plan.replacement_oracles[0], replaces_identity_keys=keys),),
            recertification_units=units,
            unit_commands=tuple((unit, ("validate-" + unit.lower(),)) for unit in units),
        )
        authority = dataclasses.replace(
            evidence_authority.synthetic_task_authorities(plan)[plan.owner_task],
            command_input_paths=(
                "tests/host/work_count_lexical.py", "tests/host/work_count_projection.py",
                "include/micros/work_observation.h",
            ),
            unit_input_paths=tuple((unit, subjects) for unit in units),
        )
        environment = evidence_authority.synthetic_environment(plan)
        immutable = dict(environment.immutable_inputs)
        del immutable["tools/command_registry.py"]
        immutable.update((path, hashlib.sha256((ROOT / path).read_bytes()).hexdigest()) for path in authority.command_input_paths)
        candidate = tuple((path, hashlib.sha256((ROOT / path).read_bytes()).hexdigest()) for path in paths)
        environment = dataclasses.replace(
            environment, immutable_inputs=tuple(sorted(immutable.items())),
            red_candidate_inputs=candidate, current_candidate_inputs=candidate,
            unit_inputs=tuple((unit, tuple(row for row in candidate if row[0] in subjects)) for unit in units),
        )
        registry = evidence_authority.synthetic_transition_registry(plan)
        result = evidence_authority.validate_transition(self.inventory, {plan.owner_task: authority}, registry, plan, (), environment)
        self.assertTrue(result.ok, result)
        self.assertIs(evidence_authority.TransitionState.REPLACEMENT_REGISTERED, result.state)
        self.assertFalse(result.retired_identity_keys)
        for overlap in (subjects[0], self.OWNER, "tests/host/selected_ownership_source.py"):
            with self.subTest(overlap=overlap):
                changed_authority = dataclasses.replace(authority, command_input_paths=(*authority.command_input_paths, overlap))
                changed_environment = dataclasses.replace(
                    environment, immutable_inputs=(*environment.immutable_inputs, (overlap, dict(candidate)[overlap])),
                )
                result = evidence_authority.validate_transition(
                    self.inventory, {plan.owner_task: changed_authority}, registry, plan, (), changed_environment,
                )
                self.assertIs(evidence_authority.ErrorCode.PROJECTION, result.error)


class SelectedAddressSpaceNativeBindingTests(unittest.TestCase):
    OWNER = "tests/host/selected_address_space_native_source_bindings.py"
    FAMILY = "selected-address-space-native-admission"

    @classmethod
    def setUpClass(cls):
        cls.reference, cls.rows, cls.previous = selected_address_space_native_reference()
        cls.inventory = evidence_authority_inventory.current_inventory(ROOT)
        cls.facts = tuple(fact for fact in cls.inventory.facts if fact.family == cls.FAMILY)
        cls.declaration = (ROOT / cls.OWNER).read_text()

    @classmethod
    def expected_facts(cls):
        consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
        return tuple(evidence_authority.Fact(
            cls.OWNER + ":native-source-admission:" + scope, cls.FAMILY, cls.OWNER,
            "native-source-admission", scope, evidence_authority.AuthorityClass.TRANSPORT_IDENTITY,
            "B02", consumers, tuple(sorted({cls.OWNER, subject, *assertions})),
            digest((cls.reference, scope, subject, assertions, contract)),
        ) for scope, subject, assertions, contract in cls.rows)

    @classmethod
    def fixture(cls, directory):
        root = Path(directory)
        for path in {value for fact in cls.expected_facts() for value in fact.payload_paths}:
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(cls.declaration if path == cls.OWNER else "")
        return root

    @classmethod
    def materialize(cls, root):
        return evidence_authority_inventory._facts_for_owner(root, cls.FAMILY, cls.OWNER)

    def test_existing_rejections_have_individual_inventory_keys(self):
        from tests.host.c_source_records import function_records
        from tests.host import selected_address_space_source as source
        from tests.host.work_count_types import Stage

        keys = {fact.key for fact in evidence_authority_inventory.current_inventory(ROOT).facts}
        prefix = self.OWNER + ":native-source-admission:MEM04/"
        for kind, path, message in (
            ("native-input", "kernel/memory/user_address_space.c", "fixed or noncandidate"),
            ("header", "include/micros/user_address_space.h", "byte-level extension"),
            ("dependent-file", "kernel/memory/user_address_space_handoff.c", "two error assignments"),
        ):
            with self.subTest(kind=kind, path=path):
                original = (ROOT / path).read_text()
                source.validate_retained_source(ROOT, path, original, Stage.GREEN)
                with self.assertRaisesRegex(ValueError, message):
                    source.validate_retained_source(ROOT, path, original + "\n", Stage.GREEN)
                key = prefix + kind + "/" + path
                self.assertTrue(key in keys, "missing existing assertion identity: " + key)
        path = "kernel/memory/user_address_space_mutation.c"
        original = (ROOT / path).read_text()
        records = function_records(original)
        for name, message in (
            ("clear_release_bitmap", "retained diagnostic or unrelated body"),
            ("micros_user_address_space_release_page", "retained command/release"),
        ):
            with self.subTest(function=name):
                record = records[name]
                fragment = record.signature + "{" + record.body + "}"
                self.assertEqual(1, original.count(fragment))
                body = ("\n" + record.body) if name == "clear_release_bitmap" else record.body.replace(
                    "return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;",
                    "return MICROS_USER_ADDRESS_SPACE_ERROR_RANGE;", 1,
                )
                self.assertNotEqual(record.body, body)
                changed = original.replace(fragment, record.signature + "{" + body + "}", 1)
                with self.assertRaisesRegex(ValueError, message):
                    source.validate_retained_source(ROOT, path, changed, Stage.GREEN)
                key = prefix + "function/" + path + "/" + name
                self.assertTrue(key in keys, "missing existing assertion identity: " + key)
        with self.subTest(definitions=path):
            changed = original.replace("preflight_selected_allocation", "foreign_allocation_preflight")
            with self.assertRaisesRegex(ValueError, "definitions"):
                source.validate_retained_source(ROOT, path, changed, Stage.GREEN)
            key = prefix + "definitions/" + path
            self.assertTrue(key in keys, "missing existing assertion identity: " + key)
        path = "kernel/memory/user_address_space_mapping.c"
        with self.subTest(observation=path):
            original = (ROOT / path).read_text()
            before = "decode_pte(table->entries[index], &entry)"
            self.assertEqual(1, original.count(before))
            changed = original.replace(before, "decode_pte(table->entries[(index)], &entry)", 1)
            source.validate_retained_source(ROOT, path, changed, Stage.GREEN)
            with self.assertRaisesRegex(ValueError, "frozen primitive/compound inventory"):
                source.observation_sites(ROOT, path, changed, Stage.GREEN)
            key = prefix + "observation/" + path + "/micros_user_address_space_select_path"
            self.assertTrue(key in keys, "missing existing assertion identity: " + key)

    def test_complete_bindings_preserve_fixed_policy_and_every_predecessor(self):
        from tests.host import selected_address_space_native_source_bindings as declarations

        self.assertEqual(self.reference, declarations.BINDING_REFERENCE)
        self.assertEqual(self.rows, declarations.NATIVE_SOURCE_BINDINGS)
        self.assertEqual(self.expected_facts(), self.facts)
        self.assertEqual(477, len(self.facts))
        self.assertTrue(evidence_authority.validate_inventory(self.inventory).ok)
        self.assertEqual((5379, 22), (len(self.inventory.facts), len(self.inventory.families)))
        self.assertEqual(
            fact_set_digest(self.expected_facts()),
            dict(self.inventory.declared_family_fact_digests)[self.FAMILY],
        )
        self.assertIn(self.OWNER, self.inventory.discovered_identity_modules)
        retained = tuple(fact for fact in self.inventory.facts if fact.family in self.previous)
        self.assertEqual(4782, len(retained))
        self.assertEqual(20, len(self.previous))
        for family, expected in self.previous.items():
            with self.subTest(family=family):
                self.assertEqual(expected, fact_set_digest(fact for fact in retained if fact.family == family))
        self.assertEqual(
            {"body-bytes-sha256": 178, "calls": 31, "calls-and-compact-body-sha256": 9},
            Counter(row[3]["content"][0] for row in self.rows if row[0].startswith("MEM04/function/")),
        )
        observations = [row[3] for row in self.rows if row[0].startswith("MEM04/observation/")]
        self.assertEqual(33, len(observations))
        self.assertEqual(28, sum(not row["automatic"] for row in observations))
        self.assertEqual(5, sum(len(row["explicit"]) for row in observations))
        self.assertTrue(all(row[2] == ("tests/host/selected_address_space_source.py",) for row in self.rows))

    def test_byte_and_compact_dependent_comparisons_keep_their_distinct_semantics(self):
        from tests.host.c_source_records import function_records
        from tests.host import selected_address_space_source as source
        from tests.host.work_count_types import Stage
        from tools.generate_selected_address_space_fixture import source_bundle

        inputs, _ = source_bundle(ROOT, Stage.GREEN, "ordinary")
        self.assertEqual(212, len(inputs))
        for path in (
            "include/micros/user_address_space.h",
            "kernel/memory/user_address_space_handoff.c",
            "kernel/memory/user_address_space.c",
        ):
            original = (ROOT / path).read_text()
            source.validate_retained_source(ROOT, path, original, Stage.GREEN)
            with self.subTest(path=path), self.assertRaises(ValueError):
                source.validate_retained_source(ROOT, path, original + "\n ", Stage.GREEN)
        path = "kernel/memory/user_address_space_mutation.c"
        original = (ROOT / path).read_text()
        record = function_records(original)["micros_user_address_space_release_page"]
        fragment = record.signature + "{" + record.body + "}"
        changed = original.replace(fragment, record.signature + "{" + record.body + "\n }", 1)
        source.validate_retained_source(ROOT, path, changed, Stage.GREEN)
        self.assertEqual(
            source.observation_sites(ROOT, path, original, Stage.GREEN),
            source.observation_sites(ROOT, path, changed, Stage.GREEN),
        )

    def test_literal_schema_and_source_binding_drift_cannot_create_authority(self):
        def declaration(reference, rows):
            return "BINDING_REFERENCE = " + repr(reference) + "\nNATIVE_SOURCE_BINDINGS = " + repr(rows) + "\n"

        first = self.rows[0]
        variants = [
            "", "BINDING_REFERENCE = " + repr(self.reference),
            self.declaration + "\nimport os\n",
            self.declaration + "\nraise RuntimeError('must not execute')\n",
            self.declaration + "\ndef hidden():\n    BINDING_REFERENCE = 0\n",
            self.declaration + "\nBINDING_REFERENCE = " + repr(self.reference),
            declaration(self.reference, list(self.rows)),
            declaration(self.reference, True),
            declaration(self.reference, self.rows[:-1]),
            declaration(self.reference, (*self.rows, first)),
            declaration(self.reference, (*self.rows[:-1], first)),
            declaration(self.reference, tuple(reversed(self.rows))),
            declaration((("policy", "0" * 40, self.reference[0][2]), self.reference[1]), self.rows),
        ]
        changes = (
            (first[0].replace("MEM04/", "MEM99/", 1), *first[1:]),
            (first[0], "kernel/memory/frame_ownership.c", *first[2:]),
            (*first[:2], ("tools/generate_selected_address_space_fixture.py",), first[3]),
            (*first[:3], {**first[3], "comparison": "other"}),
            (*first[:3], {**first[3], "expected": "0" * 64}),
            (*first[:3], {**first[3], "extra": 0}),
        )
        variants.extend(declaration(self.reference, (changed, *self.rows[1:])) for changed in changes)
        for kind, change in (
            ("function", lambda contract: {**contract, "guards": ("if FOREIGN",)}),
            ("header", lambda contract: {**contract, "comparison": "compact-header-sha256"}),
            ("observation", lambda contract: {**contract, "automatic": (("invented",),)}),
        ):
            index = next(index for index, row in enumerate(self.rows) if row[0].startswith("MEM04/" + kind + "/"))
            row = self.rows[index]
            variants.append(declaration(self.reference, (
                *self.rows[:index], (*row[:3], change(row[3])), *self.rows[index + 1:],
            )))
        with tempfile.TemporaryDirectory(prefix="micros-address-native-schema-") as directory:
            root = self.fixture(directory)
            for index, text in enumerate(variants):
                with self.subTest(case=index):
                    (root / self.OWNER).write_text(text)
                    with self.assertRaises(ValueError):
                        self.materialize(root)

    def test_payloads_are_exact_ordinary_files_not_candidate_expectations(self):
        selected = (
            self.OWNER, "kernel/memory/user_address_space.c",
            "include/micros/user_address_space.h", "kernel/memory/user_address_space_handoff.c",
            "tests/host/selected_address_space_source.py",
        )
        with tempfile.TemporaryDirectory(prefix="micros-address-native-paths-") as directory:
            root = self.fixture(directory)
            self.assertEqual(self.expected_facts(), self.materialize(root))
            (root / selected[1]).write_text("candidate bytes cannot supply a fixed expectation\n")
            self.assertEqual(self.expected_facts(), self.materialize(root))
            peer = root / "payload-peer"
            peer.write_text("not an admitted source")
            for relative in selected:
                path = root / relative
                saved = path.read_bytes()
                for kind in ("missing", "symlink", "hardlink"):
                    with self.subTest(path=relative, kind=kind):
                        path.unlink()
                        if kind == "symlink":
                            path.symlink_to(peer)
                        elif kind == "hardlink":
                            os.link(peer, path)
                        with self.assertRaises(ValueError):
                            self.materialize(root)
                        if path.exists() or path.is_symlink():
                            path.unlink()
                        path.write_bytes(saved)
            parent = root / "kernel/memory"
            moved = root / "saved-memory"
            parent.rename(moved)
            parent.symlink_to(moved, target_is_directory=True)
            with self.assertRaises(ValueError):
                self.materialize(root)
            parent.unlink()
            moved.rename(parent)

    def test_selected_clause_closure_keeps_shared_policy_disjoint_and_rejects_real_overlap(self):
        scopes = {
            "MEM04/native-input/kernel/memory/user_address_space.c",
            "MEM04/function/kernel/memory/user_address_space_mutation.c/micros_user_address_space_release_page",
            "MEM04/dependent-file/kernel/memory/user_address_space_handoff.c",
            "MEM04/observation/kernel/memory/user_address_space_mapping.c/micros_user_address_space_select_path",
        }
        facts = tuple(fact for fact in self.facts if fact.scope in scopes)
        self.assertEqual(4, len(facts))
        keys = tuple(fact.key for fact in facts)
        units = facts[0].consumer_units
        subjects = tuple(sorted({row[1] for row in self.rows if row[0] in scopes}))
        paths = tuple(sorted({path for fact in facts for path in fact.payload_paths}))
        plan = evidence_authority.synthetic_plan(evidence_authority.synthetic_inventory())
        plan = dataclasses.replace(
            plan, owner_unit="B02", finding_ids=("B02-F999",),
            affected_identity_keys=keys, retained_identity_keys=(), planned_retirement_identity_keys=keys,
            replacement_oracles=(dataclasses.replace(plan.replacement_oracles[0], replaces_identity_keys=keys),),
            recertification_units=units,
            unit_commands=tuple((unit, ("validate-" + unit.lower(),)) for unit in units),
        )
        authority = dataclasses.replace(
            evidence_authority.synthetic_task_authorities(plan)[plan.owner_task],
            command_input_paths=("tests/host/c_source_records.py", "tests/host/work_count_evidence.py", "tools/stack_bound_fatal.py"),
            unit_input_paths=tuple((unit, subjects) for unit in units),
        )
        environment = evidence_authority.synthetic_environment(plan)
        immutable = dict(environment.immutable_inputs)
        del immutable["tools/command_registry.py"]
        immutable.update((path, hashlib.sha256((ROOT / path).read_bytes()).hexdigest()) for path in authority.command_input_paths)
        candidate = tuple((path, hashlib.sha256((ROOT / path).read_bytes()).hexdigest()) for path in paths)
        environment = dataclasses.replace(
            environment, immutable_inputs=tuple(sorted(immutable.items())),
            red_candidate_inputs=candidate, current_candidate_inputs=candidate,
            unit_inputs=tuple((unit, tuple(row for row in candidate if row[0] in subjects)) for unit in units),
        )
        registry = evidence_authority.synthetic_transition_registry(plan)
        result = evidence_authority.validate_transition(self.inventory, {plan.owner_task: authority}, registry, plan, (), environment)
        self.assertTrue(result.ok, result)
        self.assertIs(evidence_authority.TransitionState.REPLACEMENT_REGISTERED, result.state)
        self.assertFalse(result.retired_identity_keys)
        for overlap in (subjects[0], self.OWNER, "tests/host/selected_address_space_source.py"):
            with self.subTest(overlap=overlap):
                changed_authority = dataclasses.replace(authority, command_input_paths=(*authority.command_input_paths, overlap))
                changed_environment = dataclasses.replace(
                    environment, immutable_inputs=(*environment.immutable_inputs, (overlap, dict(candidate)[overlap])),
                )
                result = evidence_authority.validate_transition(
                    self.inventory, {plan.owner_task: changed_authority}, registry, plan, (), changed_environment,
                )
                self.assertIs(evidence_authority.ErrorCode.PROJECTION, result.error)

    def test_all_six_committed_claims_remain_journal_free_with_the_complete_catalog(self):
        from tests.host import (
            bootstrap_allocation_authority, diagnostic_history_authority, kernel_mapping_authority,
            runtime_memory_authority, selected_address_space_authority, selected_ownership_authority,
        )

        with ExitStack() as stack:
            for module in (
                bootstrap_allocation_authority, diagnostic_history_authority,
                kernel_mapping_authority, runtime_memory_authority,
            ):
                for name in ("read_current", "read_journal", "validate_journal"):
                    stack.enter_context(patch.object(module, name, side_effect=AssertionError("ordinary authority must not use a journal")))
            for module in (selected_ownership_authority, selected_address_space_authority):
                for name in ("read_selection", "read_phase_record", "read_verified_journal", "validate_journal", "capture_author_context"):
                    stack.enter_context(patch.object(module, name, side_effect=AssertionError("committed memory authority must not use an author attempt")))
            _, authorities, _, registry = selected_address_space_authority.task_definition(ROOT)
            self.assertEqual(6, len(registry.claims))
            retired = {key for claim in registry.claims for key in claim.claimed_identity_keys}
            self.assertEqual(103, len(retired))
            result = evidence_authority.validate_transition(self.inventory, authorities, registry, None, (), None)
            self.assertTrue(result.ok, result)
            self.assertEqual({fact.key for fact in self.inventory.facts} - retired, set(result.active_identity_keys))
            self.assertFalse(retired & {fact.key for fact in self.facts})
            for claim in registry.claims:
                if claim.owner_task != "COPY01":
                    self.assertFalse(selected_address_space_authority.identity_required(ROOT, claim.claimed_identity_keys[0]))


class SelectedOwnershipNativeBindingTests(unittest.TestCase):
    OWNER = "tests/host/selected_ownership_native_source_bindings.py"
    FAMILY = "selected-ownership-native-admission"

    @classmethod
    def setUpClass(cls):
        cls.reference, cls.rows, cls.previous = selected_ownership_native_reference()
        cls.inventory = evidence_authority_inventory.current_inventory(ROOT)
        cls.facts = tuple(fact for fact in cls.inventory.facts if fact.family == cls.FAMILY)
        cls.declaration = (ROOT / cls.OWNER).read_text()

    @classmethod
    def expected_facts(cls):
        consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
        return tuple(evidence_authority.Fact(
            cls.OWNER + ":native-source-admission:" + scope, cls.FAMILY, cls.OWNER,
            "native-source-admission", scope, evidence_authority.AuthorityClass.TRANSPORT_IDENTITY,
            "B02", consumers, tuple(sorted({cls.OWNER, subject, *assertions})),
            digest((cls.reference, scope, subject, assertions, contract)),
        ) for scope, subject, assertions, contract in cls.rows)

    @classmethod
    def fixture(cls, directory):
        root = Path(directory)
        for path in {value for fact in cls.expected_facts() for value in fact.payload_paths}:
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(cls.declaration if path == cls.OWNER else "")
        return root

    @classmethod
    def materialize(cls, root):
        return evidence_authority_inventory._facts_for_owner(root, cls.FAMILY, cls.OWNER)

    @staticmethod
    def declaration_text(reference, rows):
        return "BINDING_REFERENCE = " + repr(reference) + "\nNATIVE_SOURCE_BINDINGS = " + repr(rows) + "\n"

    def test_existing_current_rejections_have_individual_inventory_keys(self):
        from tests.host import selected_ownership_source as source
        from tests.host.c_source_records import compact, function_records
        from tests.host.work_count_types import Stage
        from tools import generate_selected_ownership_fixture as generator

        subject = "kernel/memory/user_address_space_validation.c"
        current = (ROOT / subject).read_text()
        source.validate_retained_source(ROOT, subject, current, Stage.GREEN)
        name = "micros_user_address_space_validate"
        record = function_records(current)[name]
        fragment = record.signature + "{" + record.body + "}"
        self.assertEqual(1, current.count(fragment))
        changed_body = record.body + "\n "
        self.assertEqual(compact(record.body), compact(changed_body))
        changed = current.replace(fragment, record.signature + "{" + changed_body + "}", 1)
        with self.assertRaisesRegex(ValueError, "retained diagnostic or unrelated body"):
            source.validate_retained_source(ROOT, subject, changed, Stage.GREEN)
        with self.assertRaisesRegex(ValueError, "missing or unadmitted definitions"):
            source.validate_retained_source(
                ROOT, subject, current + "\nstatic void native_identity_probe(void) {}\n", Stage.GREEN,
            )

        generator.source_bundle(ROOT, Stage.GREEN, "ordinary")
        read = generator.read_regular
        headers = (
            "include/micros/user_address_space.h",
            "kernel/memory/user_address_space_internal.h",
        )
        for selected in headers:
            def changed_read(path):
                data, identity = read(path)
                return (data + b"\n" if path == ROOT / selected else data), identity

            with self.subTest(guard=selected), patch.object(
                generator, "read_regular", side_effect=changed_read,
            ), self.assertRaisesRegex(ValueError, "unadmitted native dependency"):
                generator.source_bundle(ROOT, Stage.GREEN, "ordinary")

        keys = {fact.key for fact in evidence_authority_inventory.current_inventory(ROOT).facts}
        required = (
            "MEM03/function/" + subject + "/" + name,
            "MEM03/definitions/" + subject,
            *("MEM03/native-input/" + path for path in headers),
        )
        for scope in required:
            with self.subTest(scope=scope):
                self.assertTrue(
                    self.OWNER + ":native-source-admission:" + scope in keys,
                    "missing identity for an existing rejecting MEM03 assertion: " + scope,
                )

    def test_complete_bindings_preserve_fixed_policy_and_every_predecessor(self):
        from tests.host import selected_ownership_native_source_bindings as declarations

        self.assertEqual(self.reference, declarations.BINDING_REFERENCE)
        self.assertEqual(self.rows, declarations.NATIVE_SOURCE_BINDINGS)
        self.assertEqual(self.expected_facts(), self.facts)
        self.assertEqual(484, len(self.facts))
        self.assertEqual(
            fact_set_digest(self.expected_facts()),
            dict(self.inventory.declared_family_fact_digests)[self.FAMILY],
        )
        self.assertIn(self.OWNER, self.inventory.discovered_identity_modules)
        retained = tuple(fact for fact in self.inventory.facts if fact.family in self.previous)
        self.assertEqual(4290, len(retained))
        self.assertEqual(18, len(self.previous))
        for family, expected in self.previous.items():
            with self.subTest(family=family):
                self.assertEqual(expected, fact_set_digest(fact for fact in retained if fact.family == family))
        self.assertEqual(
            {"body-bytes-sha256": 216, "calls": 35},
            Counter(row[3]["content"][0] for row in self.rows if row[0].startswith("MEM03/function/")),
        )
        self.assertEqual(
            {1: 153, 2: 43},
            Counter(len(row[2]) for row in self.rows if row[0].startswith("MEM03/native-input/")),
        )

    def test_existing_admission_normalizations_and_consumers_remain_distinct(self):
        from tests.host import selected_ownership_source as source
        from tests.host.work_count_types import Stage

        for path in source.SOURCE_ROLES["production_paths"]:
            with self.subTest(positive=path):
                source.validate_retained_source(ROOT, path, (ROOT / path).read_text(), Stage.GREEN)
        header = "include/micros/frame_ownership.h"
        current = (ROOT / header).read_text()
        source.validate_retained_source(ROOT, header, current + "\n ", Stage.GREEN)
        fixed_c = next(
            row[1] for row in self.rows
            if row[0].startswith("MEM03/native-input/") and row[1].endswith(".c")
        )
        current = (ROOT / fixed_c).read_text()
        source.validate_retained_source(ROOT, fixed_c, current, Stage.GREEN)
        with self.assertRaisesRegex(ValueError, "fixed or noncandidate source changed"):
            source.validate_retained_source(ROOT, fixed_c, current + "\n ", Stage.GREEN)
        for path in ("include/micros/user_address_space.h", "kernel/memory/user_address_space_internal.h"):
            with self.subTest(header=path), self.assertRaisesRegex(ValueError, "no fixed source binding"):
                source.validate_retained_source(ROOT, path, (ROOT / path).read_text(), Stage.GREEN)

    def test_declaration_is_literal_only_and_cannot_execute_or_rebind(self):
        reference = "BINDING_REFERENCE = " + repr(self.reference) + "\n"
        rows = "NATIVE_SOURCE_BINDINGS = " + repr(self.rows) + "\n"
        malformed = (
            "", reference, rows, reference + reference + rows,
            self.declaration + "\nextra = 1\n",
            self.declaration + "\nraise RuntimeError('must not execute')\n",
            self.declaration + "\ndef hidden():\n    BINDING_REFERENCE = 0\n",
            reference + "NATIVE_SOURCE_BINDINGS: tuple = " + repr(self.rows),
            reference + "NATIVE_SOURCE_BINDINGS = tuple()\n",
            reference + "NATIVE_SOURCE_BINDINGS = []\n",
            reference + "NATIVE_SOURCE_BINDINGS = True\n",
            reference + "NATIVE_SOURCE_BINDINGS = None\n",
        )
        with tempfile.TemporaryDirectory(prefix="micros-selected-native-literals-") as directory:
            root = self.fixture(directory)
            for index, text in enumerate(malformed):
                with self.subTest(case=index):
                    (root / self.OWNER).write_text(text)
                    with self.assertRaises(ValueError):
                        self.materialize(root)

    def test_scope_value_owner_and_reference_drift_cannot_rebaseline_bindings(self):
        first = self.rows[0]
        variants = {
            "missing": self.rows[:-1],
            "extra": (*self.rows, first),
            "duplicate": (*self.rows[:-1], first),
            "order": tuple(reversed(self.rows)),
            "scope": ((first[0].replace("MEM03/", "MEM99/", 1), *first[1:]), *self.rows[1:]),
            "subject": ((first[0], "kernel/memory/frame_ownership.c", *first[2:]), *self.rows[1:]),
            "assertion": ((*first[:2], ("tests/host/kernel_mapping_source.py",), first[3]), *self.rows[1:]),
            "comparison": ((*first[:3], {**first[3], "comparison": "other"}), *self.rows[1:]),
            "value": ((*first[:3], {**first[3], "expected": ("invented_definition",)}), *self.rows[1:]),
            "field": ((*first[:3], {**first[3], "extra": 0}), *self.rows[1:]),
        }
        for suffix, assertions in (
            (".c", ("tools/generate_selected_ownership_fixture.py",)),
            (".h", ("tests/host/selected_ownership_source.py", "tools/generate_selected_ownership_fixture.py")),
        ):
            index = next(
                index for index, row in enumerate(self.rows)
                if row[0].startswith("MEM03/native-input/") and row[1].endswith(suffix)
            )
            row = self.rows[index]
            variants["consumers" + suffix] = (
                *self.rows[:index], (*row[:2], assertions, row[3]), *self.rows[index + 1:],
            )
        index = next(
            index for index, row in enumerate(self.rows)
            if row[3].get("content", ("",))[0] == "body-bytes-sha256"
        )
        row = self.rows[index]
        variants["body-normalization"] = (
            *self.rows[:index],
            (*row[:3], {**row[3], "content": ("lexical-body-sha256", row[3]["content"][1])}),
            *self.rows[index + 1:],
        )
        with tempfile.TemporaryDirectory(prefix="micros-selected-native-drift-") as directory:
            root = self.fixture(directory)
            for label, rows in variants.items():
                with self.subTest(case=label):
                    (root / self.OWNER).write_text(self.declaration_text(self.reference, rows))
                    with self.assertRaises(ValueError):
                        self.materialize(root)
            changed = (("policy", "0" * 40, self.reference[0][2]), self.reference[1])
            (root / self.OWNER).write_text(self.declaration_text(changed, self.rows))
            with self.assertRaises(ValueError):
                self.materialize(root)

    def test_payloads_are_exact_ordinary_files_not_candidate_expectations(self):
        selected = (
            self.OWNER, "kernel/memory/user_address_space_validation.c",
            "include/micros/user_address_space.h", "tests/host/selected_ownership_source.py",
            "tools/generate_selected_ownership_fixture.py",
        )
        with tempfile.TemporaryDirectory(prefix="micros-selected-native-paths-") as directory:
            root = self.fixture(directory)
            self.assertEqual(self.expected_facts(), self.materialize(root))
            (root / selected[1]).write_text("candidate bytes cannot replace the historical expectation\n")
            self.assertEqual(self.expected_facts(), self.materialize(root))
            peer = root / "payload-peer"
            peer.write_text("not an admitted source")
            for relative in selected:
                path = root / relative
                saved = path.read_bytes()
                for kind in ("missing", "symlink", "hardlink"):
                    with self.subTest(path=relative, kind=kind):
                        path.unlink()
                        if kind == "symlink":
                            path.symlink_to(peer)
                        elif kind == "hardlink":
                            os.link(peer, path)
                        with self.assertRaises(ValueError):
                            self.materialize(root)
                        if path.exists() or path.is_symlink():
                            path.unlink()
                        path.write_bytes(saved)
            parent = root / "kernel/memory"
            moved = root / "saved-memory"
            parent.rename(moved)
            parent.symlink_to(moved, target_is_directory=True)
            with self.assertRaises(ValueError):
                self.materialize(root)
            parent.unlink()
            moved.rename(parent)

    def test_selected_clause_closure_preserves_real_overlap_rejection(self):
        scopes = {
            "MEM03/function/kernel/memory/user_address_space_validation.c/micros_user_address_space_validate",
            "MEM03/definitions/kernel/memory/user_address_space_validation.c",
            "MEM03/native-input/include/micros/user_address_space.h",
            "MEM03/native-input/kernel/memory/user_address_space_internal.h",
        }
        facts = tuple(fact for fact in self.facts if fact.scope in scopes)
        self.assertEqual(4, len(facts))
        keys = tuple(fact.key for fact in facts)
        units = facts[0].consumer_units
        subjects = (
            "include/micros/user_address_space.h", "kernel/memory/user_address_space_internal.h",
            "kernel/memory/user_address_space_validation.c",
        )
        paths = tuple(sorted({path for fact in facts for path in fact.payload_paths}))
        plan = evidence_authority.synthetic_plan(evidence_authority.synthetic_inventory())
        plan = dataclasses.replace(
            plan, owner_unit="B02", finding_ids=("B02-F999",),
            affected_identity_keys=keys, retained_identity_keys=(), planned_retirement_identity_keys=keys,
            replacement_oracles=(dataclasses.replace(
                plan.replacement_oracles[0], replaces_identity_keys=keys,
            ),),
            recertification_units=units,
            unit_commands=tuple((unit, ("validate-" + unit.lower(),)) for unit in units),
        )
        authority = dataclasses.replace(
            evidence_authority.synthetic_task_authorities(plan)[plan.owner_task],
            command_input_paths=(
                "tests/host/c_source_records.py", "tests/host/work_count_evidence.py",
                "tools/stack_bound_assembly.py",
            ),
            unit_input_paths=tuple((unit, subjects) for unit in units),
        )
        environment = evidence_authority.synthetic_environment(plan)
        immutable = dict(environment.immutable_inputs)
        del immutable["tools/command_registry.py"]
        immutable.update(
            (path, hashlib.sha256((ROOT / path).read_bytes()).hexdigest()) for path in authority.command_input_paths
        )
        candidate = tuple((path, hashlib.sha256((ROOT / path).read_bytes()).hexdigest()) for path in paths)
        selected = tuple(item for item in candidate if item[0] in subjects)
        environment = dataclasses.replace(
            environment, immutable_inputs=tuple(sorted(immutable.items())),
            red_candidate_inputs=candidate, current_candidate_inputs=candidate,
            unit_inputs=tuple((unit, selected) for unit in units),
        )
        registry = evidence_authority.synthetic_transition_registry(plan)
        result = evidence_authority.validate_transition(
            self.inventory, {plan.owner_task: authority}, registry, plan, (), environment,
        )
        self.assertTrue(result.ok, result)
        self.assertIs(evidence_authority.TransitionState.REPLACEMENT_REGISTERED, result.state)
        self.assertFalse(result.retired_identity_keys)
        for overlap in (subjects[2], self.OWNER, "tests/host/selected_ownership_source.py"):
            with self.subTest(overlap=overlap):
                changed_authority = dataclasses.replace(
                    authority, command_input_paths=(*authority.command_input_paths, overlap),
                )
                changed_environment = dataclasses.replace(
                    environment,
                    immutable_inputs=(*environment.immutable_inputs, (overlap, dict(candidate)[overlap])),
                )
                result = evidence_authority.validate_transition(
                    self.inventory, {plan.owner_task: changed_authority}, registry,
                    plan, (), changed_environment,
                )
                self.assertIs(evidence_authority.ErrorCode.PROJECTION, result.error)

    def test_all_five_committed_claims_remain_journal_free(self):
        from tests.host import (
            bootstrap_allocation_authority, diagnostic_history_authority,
            kernel_mapping_authority, runtime_memory_authority, selected_ownership_authority,
        )

        with ExitStack() as stack:
            for module in (
                bootstrap_allocation_authority, diagnostic_history_authority,
                kernel_mapping_authority, runtime_memory_authority,
            ):
                for name in ("read_current", "read_journal", "validate_journal"):
                    stack.enter_context(patch.object(
                        module, name, side_effect=AssertionError("ordinary authority must not use a journal"),
                    ))
            for name in (
                "read_selection", "read_phase_record", "read_verified_journal",
                "validate_journal", "capture_author_context",
            ):
                stack.enter_context(patch.object(
                    selected_ownership_authority, name,
                    side_effect=AssertionError("MEM03 ordinary authority must not use an author attempt"),
                ))
            _, authorities, _, registry = selected_ownership_authority.task_definition(ROOT)
            self.assertEqual(5, len(registry.claims))
            self.assertEqual(70, sum(len(claim.claimed_identity_keys) for claim in registry.claims))
            result = evidence_authority.validate_transition(self.inventory, authorities, registry, None, (), None)
            self.assertTrue(result.ok, result)
            retired = {key for claim in registry.claims for key in claim.claimed_identity_keys}
            self.assertEqual({fact.key for fact in self.inventory.facts} - retired, set(result.active_identity_keys))
            self.assertFalse(runtime_memory_authority.memory_identity_required(ROOT))
            for claim in registry.claims:
                if claim.owner_task != "COPY01":
                    self.assertFalse(
                        selected_ownership_authority.identity_required(ROOT, claim.claimed_identity_keys[0]),
                        claim.owner_task,
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
