"""Closed current evidence-authority inventory for ADR-0082."""

import ast
from pathlib import Path
import re

from tests.host.evidence_authority_types import (
    AuthorityClass,
    Fact,
    Family,
    Inventory,
    fact_set_digest,
)


FAMILY_OWNERS = {
    "authority-core": (
        "tests/host/ast_evidence.py",
        "tests/host/c_source_records.py",
        "tests/host/evidence_authority.py",
        "tests/host/evidence_authority_fixture.py",
        "tests/host/evidence_authority_inventory.py",
        "tests/host/evidence_authority_types.py",
        "tests/host/evidence_authority_validation.py",
        "tests/host/kernel_layout_source.py",
        "tests/host/test_evidence_authority.py",
    ),
    "kernel-structure": (
        "tests/host/test_frame_ownership_source_layout.py",
        "tests/host/test_frame_validation_hot_path.py",
        "tests/host/test_kernel_address_space_source_layout.py",
        "tests/host/test_kernel_bootstrap_source_layout.py",
        "tests/host/test_kernel_directory_layout.py",
        "tests/host/test_kernel_ipc_source_layout.py",
        "tests/host/test_kernel_object_source_layout.py",
        "tests/host/test_kernel_pm_control_source_layout.py",
        "tests/host/test_kernel_vm_handoff_source_layout.py",
        "tests/host/test_scheduler_source_layout.py",
        "tests/host/test_trap_syscall_source_layout.py",
        "tests/host/test_vm_validation_hot_path.py",
    ),
    "service-structure": (
        "tests/host/test_elf_spawn_model_layout.py",
        "tests/host/test_endpoint_source_layout.py",
        "tests/host/test_grant_data_plane_source_layout.py",
        "tests/host/test_pm_service_source_layout.py",
        "tests/host/test_ramfs_source_layout.py",
        "tests/host/test_tty_source_layout.py",
        "tests/host/test_vfs_source_layout.py",
        "tests/host/test_vfs_test_source_layout.py",
        "tests/host/test_vm_service_source_layout.py",
    ),
    "tool-structure": (
        "tools/validation_context.py",
        "tests/host/test_qemu_runner_source_layout.py",
        "tests/host/test_user_elf_source_layout.py",
        "tests/host/test_validation_context_layout.py",
    ),
    "recipe-impact": (
        "tools/validation_context.py",
        "tools/validation_build_impact.py",
        "tests/host/test_validation_build_impact.py",
    ),
    "fixture-artifacts": (
        "tests/host/test_generate_bootstrap_fixture.py",
        "tests/host/test_generate_elf_spawn_fixture.py",
        "tests/host/test_generate_pm_service_fixture.py",
        "tests/host/test_generate_ramfs_service_fixture.py",
        "tests/host/test_generate_tty_service_fixture.py",
        "tests/host/test_generate_user_runtime_fixture.py",
        "tests/host/test_generate_vfs_service_fixture.py",
        "tests/host/test_generate_vm_handoff_fixture.py",
        "tests/host/test_generate_vm_scratch_preemption_fixture.py",
    ),
    "stack-source-bindings": (
        "tools/stack_bound_bindings.py",
        "tools/stack_bound_callback_contracts.py",
    ),
    "stack-firmware-artifact": (
        "tests/host/test_stack_artifact_boundary.py",
        "tools/stack_bound_fatal_firmware.py",
    ),
    "stack-vm-lifetime": (
        "tests/host/test_vm_load_stack_lifetime.py",
    ),
}

NON_IDENTITY_EXCEPTIONS = ()

STACK_BINDING_PAYLOAD_PATHS = (
    "docs/architecture/stack-bound-evidence.md",
    "include/micros/user_address_space.h",
    "kernel/memory/user_address_space_mapping.c",
    "kernel/pm/pm_control_syscall_core.h",
    "kernel/pm/pm_control_syscall_runtime.c",
    "kernel/tests/memory/address_space_handoff_test_snapshot.c",
    "kernel/tests/memory/address_space_handoff_test_tty.c",
    "kernel/vm/vm_snapshot.c",
    "kernel/vm/vm_snapshot_validation.c",
    "servers/pm/pm_coordinator.h",
    "servers/pm/pm_coordinator_internal.h",
    "servers/pm/pm_service_backend.c",
    "servers/ramfs/ramfs_core.h",
    "servers/ramfs/ramfs_service.c",
    "servers/tty/tty_service.c",
    "servers/tty/tty_service_core.h",
    "servers/tty/tty_uart.h",
    "servers/vfs/vfs_core.h",
    "servers/vfs/vfs_service.c",
    "servers/vm/vm_service_backend.c",
    "servers/vm/vm_service_core.h",
    "tools/stack_bound_bindings.py",
    "tools/stack_bound_callback_contracts.py",
)

STACK_FIRMWARE_PAYLOAD_PATHS = (
    "docs/adr/0086-finite-fatal-stack-context.md",
    "docs/architecture/stack-bound-evidence.md",
    "docs/research/fatal-stack-context-evidence.md",
    "tests/host/test_stack_artifact_boundary.py",
    "tools/stack_bound_fatal_firmware.py",
)

STACK_VM_LIFETIME_PAYLOAD_PATHS = (
    "docs/adr/0087-phase-bounded-vm-load-temporaries.md",
    "docs/architecture/stack-bound-evidence.md",
    "servers/vm/vm_load.c",
    "tests/host/test_vm_load_stack_lifetime.py",
    "tools/stack_bound_vm_lifetime.py",
)

DECLARED_FAMILY_FACT_DIGESTS = {
    "authority-core":
        "c9f825f5b1488d03ee1c54db9ae3324b312807cd62de6dfa23166331a32e7e5d",
    "fixture-artifacts":
        "3cff438cd00a5ee1b78bffdedb2b9e66c123c9972ab048eda1854aff2918d149",
    "kernel-structure":
        "6d4f3a952a315b57ae4d234b67b0c882e60b19b1e9af49c29a739e6c16ed43b9",
    "recipe-impact":
        "a14c50d641800083794bfa4f67e3b275e02528b181afc82879a6dae9f0f898dd",
    "service-structure":
        "11a2f268950946b406dd52d176a14198b3dfaffe36cb41e226ba8f648b078533",
    "tool-structure":
        "29bee701c5920506b72b9ad5f60da85521170673a222a3770c6d96953c6ed73b",
    "stack-source-bindings":
        "8148089c54841e214d43b1f8f811eadd40904a4b39f88817d98dfb74477c33b6",
    "stack-firmware-artifact":
        "9d2683554290ebe151342a003891705b6351aeddc69fddf2b32b256d226e58bb",
    "stack-vm-lifetime":
        "17d0e193b7747ce3df1efe34c1fa6d777201f1d95ad6e07cec4a5904d57032a4",
}

_IDENTITY_NAME = re.compile(
    r"(?:REVIEWED|ORIGINAL|FROZEN|BASELINE|DIGEST|SHA256|HASH)"
)
_HEX = re.compile(r"(?:[0-9a-f]{40}|[0-9a-f]{64})")
_HELPERS = {
    "ast_digest",
    "digest_bytes",
    "function_token_digest",
    "original_body_digest",
    "restore_generated_fixture",
    "restore_cmake_source",
    "restore_validation_context_source",
    "restored_runner_source",
}


def _module_path(root, name):
    path = root.joinpath(*name.split(".")).with_suffix(".py")
    return path if path.is_file() else None


def _import_closure(root):
    pending = list((root / "tests/host").glob("test_*.py"))
    observed = set()
    while pending:
        path = pending.pop().resolve()
        relative = str(path.relative_to(root))
        if relative in observed:
            continue
        observed.add(relative)
        tree = ast.parse(path.read_text(encoding="utf-8"))
        for node in ast.walk(tree):
            names = []
            if isinstance(node, ast.Import):
                names = [alias.name for alias in node.names]
            elif (
                isinstance(node, ast.ImportFrom)
                and node.level == 0
                and node.module
            ):
                names = [node.module]
                if node.module in ("tools", "tests.host"):
                    names.extend(
                        node.module + "." + alias.name
                        for alias in node.names
                    )
            for name in names:
                dependency = _module_path(root, name)
                if dependency is not None:
                    relative_dependency = str(
                        dependency.resolve().relative_to(root)
                    )
                    if relative_dependency.startswith(("tests/host/", "tools/")):
                        pending.append(dependency)
    return observed


def _assignment_name(node):
    if isinstance(node, ast.Assign) and len(node.targets) == 1:
        target = node.targets[0]
    elif isinstance(node, ast.AnnAssign):
        target = node.target
    else:
        return None
    return target.id if isinstance(target, ast.Name) else None


def _contains_fixed_hash(node):
    return any(
        isinstance(child, ast.Constant)
        and isinstance(child.value, str)
        and _HEX.fullmatch(child.value)
        for child in ast.walk(node)
    )


def _call_name(node):
    if isinstance(node.func, ast.Name):
        return node.func.id
    if isinstance(node.func, ast.Attribute):
        return node.func.attr
    return ""


def _owned_functions(tree):
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            yield node.name, node
        elif isinstance(node, ast.ClassDef):
            for child in node.body:
                if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef)):
                    yield f"{node.name}.{child.name}", child


def _identity_candidate(path):
    tree = ast.parse(path.read_text(encoding="utf-8"))
    for node in ast.walk(tree):
        name = _assignment_name(node)
        if (
            name
            and name != "DECLARED_FAMILY_FACT_DIGESTS"
            and _contains_fixed_hash(node)
        ):
            return True
        if isinstance(node, ast.Call):
            call = _call_name(node)
            if call in _HELPERS or call.startswith(("restore_", "original_")):
                return True
    return False


def _assignment_class(family):
    if family in ("fixture-artifacts", "stack-firmware-artifact"):
        return AuthorityClass.ARTIFACT_CONTRACT
    return AuthorityClass.TRANSPORT_IDENTITY


_PRIMARY_PREFIXES = (
    (("arch/", "kernel/platform/", "kernel/diagnostic/"), "B01"),
    (("kernel/memory/",), "B02"),
    (("kernel/objects/", "kernel/scheduler/"), "B03"),
    (("kernel/endpoint/", "kernel/ipc/"), "B04"),
    (("kernel/grant/", "kernel/trap/", "kernel/execution/",
      "lib/runtime/"), "B05"),
    (("kernel/boot/",), "B06"),
    (("kernel/vm/", "servers/vm/", "lib/vm/"), "B07"),
    (("kernel/pm/", "servers/pm/", "lib/spawn/"), "B08"),
    (("kernel/tty/", "servers/tty/"), "B09"),
    (("servers/ramfs/",), "B10"),
    (("servers/vfs/",), "B11"),
)


def _primary_unit_for_path(path):
    for spellings, unit in _PRIMARY_PREFIXES:
        if path.startswith(spellings):
            return unit
    return None


def _path_units(path):
    units = set(filter(None, (_primary_unit_for_path(path),)))
    if path.startswith("kernel/vm/"):
        units.add("B02")
    if path.startswith("kernel/boot/"):
        units.add("B01")
    if path.startswith("kernel/tty/"):
        units.add("B01")
    if path.startswith("servers/vfs/vfs_spawn"):
        units.add("B12")
    if path.startswith("kernel/"):
        name = Path(path).name
        legacy = (
            (("frame", "address_space", "sv39"), "B02"),
            (("scheduler", "kernel_object"), "B03"),
            (("endpoint", "ipc"), "B04"),
            (("grant", "trap", "user_execution", "syscall"), "B05"),
            (("bootstrap",), "B06"),
            (("vm_",), "B07"),
            (("pm_",), "B08"),
            (("tty", "uart"), "B09"),
        )
        for markers, unit in legacy:
            if any(marker in name for marker in markers):
                units.add(unit)
    return units


def _fact_primary_unit(payload_paths):
    units = {
        unit for unit in map(_primary_unit_for_path, payload_paths)
        if unit is not None
    }
    return next(iter(units)) if len(units) == 1 else "B14"


def _consumer_units(relative, payload_paths=()):
    units = {"B14"}
    mappings = (
        ("stack_", tuple(f"B{index:02d}" for index in range(1, 13))),
        ("frame", ("B02",)),
        ("address_space", ("B02",)),
        ("kernel_vm_handoff", ("B02", "B07")),
        ("kernel_bootstrap", ("B06",)),
        ("kernel_ipc", ("B03", "B04")),
        ("endpoint", ("B04",)),
        ("kernel_object", ("B03",)),
        ("scheduler", ("B03",)),
        ("kernel_pm_control", ("B08",)),
        ("pm_service", ("B08", "B12")),
        ("trap_syscall", ("B05",)),
        ("grant", ("B05",)),
        ("ramfs", ("B10",)),
        ("tty", ("B09",)),
        ("vfs", ("B11",)),
        ("vm_service", ("B07", "B12")),
        ("vm_validation", ("B07",)),
        ("elf_spawn", ("B12",)),
        ("user_runtime", ("B05", "B12")),
        ("bootstrap_fixture", ("B06",)),
        ("vm_handoff_fixture", ("B07",)),
        ("vm_scratch", ("B07",)),
    )
    for marker, selected in mappings:
        if marker in relative:
            units.update(selected)
    for path in payload_paths:
        units.update(_path_units(path))
    return tuple(sorted(units))


def _owner_scope(family, relative):
    if relative == "tools/validation_context.py":
        return {
            "tool-structure": "qemu-runner-source-owner",
            "recipe-impact": "recipe-digest-owner",
        }[family]
    return "module"


def _path_strings(root, node):
    paths = set()
    for child in ast.walk(node):
        if not isinstance(child, ast.Constant) or not isinstance(child.value, str):
            continue
        value = child.value
        if _HEX.fullmatch(value):
            continue
        candidate = root / value
        if candidate.is_file():
            paths.add(value)
    return paths


def _label(node, index):
    if isinstance(node, ast.Constant) and isinstance(
        node.value, (str, int)
    ):
        return str(node.value)
    return str(index)


def _hash_leaves(root, node, prefix=(), payload_paths=()):
    if (
        isinstance(node, ast.Constant)
        and isinstance(node.value, str)
        and _HEX.fullmatch(node.value)
    ):
        yield prefix, payload_paths, node.value
        return
    if isinstance(node, ast.Dict):
        for index, (key, value) in enumerate(zip(node.keys, node.values)):
            branch_paths = set(payload_paths)
            branch_paths.update(_path_strings(root, key))
            branch_paths.update(_path_strings(root, value))
            yield from _hash_leaves(
                root,
                value,
                (*prefix, _label(key, index)),
                tuple(sorted(branch_paths)),
            )
        return
    if isinstance(node, (ast.Tuple, ast.List, ast.Set)):
        for index, item in enumerate(node.elts):
            branch_paths = set(payload_paths)
            branch_paths.update(_path_strings(root, item))
            yield from _hash_leaves(
                root,
                item,
                (*prefix, str(index)),
                tuple(sorted(branch_paths)),
            )
        return
    for field, value in ast.iter_fields(node):
        if isinstance(value, ast.AST):
            yield from _hash_leaves(root, value, (*prefix, field), payload_paths)
        elif isinstance(value, list):
            for index, item in enumerate(value):
                if isinstance(item, ast.AST):
                    yield from _hash_leaves(
                        root,
                        item,
                        (*prefix, field, str(index)),
                        payload_paths,
                    )


def _global_strings(tree):
    values = {}
    for node in tree.body:
        name = _assignment_name(node)
        value = node.value if isinstance(node, (ast.Assign, ast.AnnAssign)) else None
        if (
            name
            and isinstance(value, ast.Constant)
            and isinstance(value.value, str)
        ):
            values[name] = value.value
    return values


def _literal_assignments(tree):
    values = {}
    for node in tree.body:
        name = _assignment_name(node)
        if not name:
            continue
        try:
            values[name] = ast.literal_eval(node.value)
        except (TypeError, ValueError):
            continue
    return values


def _baseline_body_path(literals, leaf):
    if len(leaf) < 2:
        return None
    try:
        family = int(leaf[0])
        name = leaf[1]
        sources = tuple(literals["SOURCE_SETS"].values())[family]
        owners = literals["ORIGINAL_FUNCTIONS"][family]
    except (KeyError, IndexError, TypeError, ValueError):
        return None
    matches = [
        owner for owner, names in owners.items()
        if name in names
    ]
    if len(matches) != 1 or matches[0] >= len(sources):
        return None
    return sources[matches[0]]


def _kernel_classification_path(root, literals, leaf):
    if not leaf:
        return None
    old_path = leaf[0]
    try:
        record = literals["KERNEL_FILE_FACTS"][old_path]
    except (KeyError, TypeError):
        return None
    candidate = record[0] if record else None
    if (
        isinstance(candidate, str)
        and not re.fullmatch(r"[0-9]{6}", candidate)
        and (root / candidate).is_file()
    ):
        return candidate
    return old_path if (root / old_path).is_file() else None


def _semantic_payload_paths(root, name, leaf, literals):
    paths = set()
    if name == "BASELINE_BODIES":
        path = _baseline_body_path(literals, leaf)
        if path and (root / path).is_file():
            paths.add(path)
    elif name == "KERNEL_CLASSIFICATION_FACTS":
        path = _kernel_classification_path(root, literals, leaf)
        if path:
            paths.add(path)
    return paths


def _owner_boundary_fact(family, relative):
    scope = _owner_scope(family, relative)
    return Fact(
        f"{relative}:owner-boundary:{scope}",
        family,
        relative,
        "owner-boundary",
        scope,
        AuthorityClass.PERMANENT_INVARIANT,
        "B14",
        _consumer_units(relative, (relative,)),
        (relative,),
        "",
    )


def _assignment_facts(
    root, family, relative, tree, literals, declared_paths
):
    facts = []
    for node in tree.body:
        name = _assignment_name(node)
        if not (
            name
            and name != "DECLARED_FAMILY_FACT_DIGESTS"
            and _contains_fixed_hash(node)
        ):
            continue
        if relative == "tools/validation_context.py":
            selected = (
                "recipe-impact"
                if name == "RECIPE_DIGESTS"
                else "tool-structure"
            )
            if family != selected:
                continue
        for leaf, leaf_paths, evidence_value in _hash_leaves(
            root, node.value
        ):
            scope = "/".join((name, *leaf))
            payload_set = {
                relative,
                *leaf_paths,
                *_semantic_payload_paths(root, name, leaf, literals),
            }
            if payload_set == {relative}:
                payload_set.update(declared_paths)
            payload_paths = tuple(sorted(payload_set))
            facts.append(
                Fact(
                    f"{relative}:data-entry:{scope}",
                    family,
                    relative,
                    "data-entry",
                    scope,
                    (
                        AuthorityClass.ARTIFACT_CONTRACT
                        if relative == "tools/validation_context.py"
                        and scope == "RECIPE_DIGESTS/tests/work/WorkCounts.cmake"
                        else _assignment_class(family)
                    ),
                    _fact_primary_unit(payload_paths),
                    _consumer_units(relative, payload_paths),
                    payload_paths,
                    evidence_value,
                )
            )
    return facts


def _function_facts(
    root, family, relative, tree, declared_paths
):
    facts = []
    global_strings = _global_strings(tree)
    for function_name, node in _owned_functions(tree):
        calls = {
            _call_name(child)
            for child in ast.walk(node)
            if isinstance(child, ast.Call)
        }
        if any(
            call in _HELPERS or call.startswith(("restore_", "original_"))
            for call in calls
        ):
            payloads = {relative, *_path_strings(root, node)}
            for name, value in global_strings.items():
                if name in {
                    child.id
                    for child in ast.walk(node)
                    if isinstance(child, ast.Name)
                } and (root / value).is_file():
                    payloads.add(value)
            facts.append(
                Fact(
                    f"{relative}:python-ast:{function_name}",
                    family,
                    relative,
                    "python-ast",
                    function_name,
                    AuthorityClass.TRANSPORT_IDENTITY,
                    _fact_primary_unit(tuple(sorted(payloads))),
                    _consumer_units(relative, tuple(sorted(payloads))),
                    tuple(sorted(payloads)),
                    "",
                )
            )
        fixed_hashes = [
            child.value
            for child in ast.walk(node)
            if isinstance(child, ast.Constant)
            and isinstance(child.value, str)
            and _HEX.fullmatch(child.value)
        ]
        for index, _ in enumerate(fixed_hashes):
            payloads = {relative, *_path_strings(root, node)}
            if payloads == {relative}:
                payloads.update(declared_paths)
            facts.append(
                Fact(
                    (
                        f"{relative}:artifact-bytes:"
                        f"{function_name}/hash-{index}"
                    ),
                    family,
                    relative,
                    "artifact-bytes",
                    f"{function_name}/hash-{index}",
                    (
                        AuthorityClass.ARTIFACT_CONTRACT
                        if family in ("fixture-artifacts", "stack-firmware-artifact")
                        else AuthorityClass.TRANSPORT_IDENTITY
                    ),
                    _fact_primary_unit(tuple(sorted(payloads))),
                    _consumer_units(relative, tuple(sorted(payloads))),
                    tuple(sorted(payloads)),
                    fixed_hashes[index],
                )
            )
    return facts


def _facts_for_owner(root, family, relative):
    tree = ast.parse((root / relative).read_text(encoding="utf-8"))
    declared_paths = _path_strings(root, tree)
    if family == "stack-source-bindings":
        declared_paths = set(STACK_BINDING_PAYLOAD_PATHS)
    elif family == "stack-firmware-artifact":
        declared_paths = set(STACK_FIRMWARE_PAYLOAD_PATHS)
    elif family == "stack-vm-lifetime":
        declared_paths = set(STACK_VM_LIFETIME_PAYLOAD_PATHS)
    return [
        _owner_boundary_fact(family, relative),
        *_assignment_facts(
            root,
            family,
            relative,
            tree,
            _literal_assignments(tree),
            declared_paths,
        ),
        *_function_facts(
            root, family, relative, tree, declared_paths
        ),
    ]


def current_inventory(root):
    root = Path(root).resolve()
    families = tuple(
        Family(family, owners)
        for family, owners in FAMILY_OWNERS.items()
    )
    facts = tuple(
        fact
        for family, owners in FAMILY_OWNERS.items()
        for owner in owners
        for fact in _facts_for_owner(root, family, owner)
    )
    closure = _import_closure(root)
    discovered = tuple(sorted(
        relative
        for relative in closure
        if _identity_candidate(root / relative)
    ))
    registered = tuple(sorted({
        owner for owners in FAMILY_OWNERS.values() for owner in owners
    }))
    declared = tuple(sorted(
        relative for relative in discovered if relative in registered
    ))
    return Inventory(
        families=families,
        facts=facts,
        registered_owner_modules=registered,
        discovered_identity_modules=discovered,
        declared_identity_modules=declared,
        non_identity_exceptions=NON_IDENTITY_EXCEPTIONS,
        declared_family_owners=tuple(FAMILY_OWNERS.items()),
        declared_family_fact_digests=tuple(
            sorted(DECLARED_FAMILY_FACT_DIGESTS.items())
        ),
    )
