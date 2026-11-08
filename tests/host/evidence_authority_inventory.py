"""Closed current evidence-authority inventory for ADR-0082."""

import ast
from collections import Counter
from pathlib import Path
import re

from tests.host import (
    bootstrap_allocation_source, diagnostic_history_source, evidence_authority_packed, kernel_mapping_source,
    panic_trace_source, selected_address_space_source, selected_ownership_source, terminal_failure_source,
)
from tests.host.evidence_authority_source import (
    kernel_directory, kernel_sources, literal_value, regular_source, single_assignment,
)
from tests.host.evidence_authority_types import (
    AuthorityClass,
    Fact,
    Family,
    Inventory,
    digest,
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
    "runtime-memory-baseline": (
        "tests/host/runtime_memory_authority.py",
    ),
    "scheduler-packed-transport": (
        "tests/host/test_scheduler_source_layout.py",
    ),
    "diagnostic-history-baseline": (
        "tests/host/diagnostic_history_authority.py",
        "tests/host/diagnostic_history_source.py",
    ),
    "terminal-failure-baseline": (
        "tests/host/terminal_failure_evidence.py",
        "tests/host/terminal_failure_source.py",
    ),
    "panic-trace-baseline": (
        "tests/host/panic_trace_evidence.py",
        "tests/host/panic_trace_source.py",
    ),
    "bootstrap-allocation-baseline": (
        "tests/host/bootstrap_allocation_authority.py",
        "tests/host/bootstrap_allocation_source.py",
    ),
    "kernel-mapping-baseline": (
        "tests/host/kernel_mapping_authority.py",
        "tests/host/kernel_mapping_source.py",
    ),
    "memory-native-admission": (
        "tests/host/memory_native_source_bindings.py",
    ),
    "selected-ownership-baseline": (
        "tests/host/selected_ownership_authority.py",
        "tests/host/selected_ownership_source.py",
    ),
    "selected-ownership-native-admission": (
        "tests/host/selected_ownership_native_source_bindings.py",
    ),
    "selected-address-space-baseline": (
        "tests/host/selected_address_space_authority.py",
        "tests/host/selected_address_space_source.py",
    ),
    "selected-address-space-native-admission": (
        "tests/host/selected_address_space_native_source_bindings.py",
    ),
}

# These regressions exercise registered declarations, not additional identities.
NON_IDENTITY_EXCEPTIONS = (
    "tests/host/test_selected_ownership.py",
    "tests/host/test_selected_address_space.py",
    "tests/host/test_validation_execution.py",
)

LOGICAL_FILE_TABLES = {
    "tests/host/test_endpoint_source_layout.py": ("STABLE_HASHES",),
    "tests/host/test_grant_data_plane_source_layout.py": ("STABLE_HASHES",),
    "tests/host/test_kernel_bootstrap_source_layout.py": ("STABLE_HASHES",),
    "tests/host/test_kernel_ipc_source_layout.py": (
        "FILE_SCOPE_STORAGE_HASHES", "STABLE_HASHES", "STATIC_LOCAL_STORAGE_HASHES",
    ),
    "tests/host/test_kernel_object_source_layout.py": ("STABLE_HASHES",),
    "tests/host/test_kernel_pm_control_source_layout.py": ("STABLE_HEADER_HASHES",),
    "tests/host/test_kernel_vm_handoff_source_layout.py": ("STABLE_HEADER_HASHES",),
    "tests/host/test_pm_service_source_layout.py": ("STABLE_HEADER_HASHES",),
    "tests/host/test_ramfs_source_layout.py": ("STABLE_HASHES",),
    "tests/host/test_scheduler_source_layout.py": ("STABLE_ARTIFACT_HASHES",),
    "tests/host/test_trap_syscall_source_layout.py": (
        "EXISTING_INLINE_HEADERS", "ORIGINAL_SOURCE_HASHES", "STABLE_HASHES",
    ),
    "tests/host/test_tty_source_layout.py": ("STABLE_HASHES",),
    "tests/host/test_vm_service_source_layout.py": ("STABLE_HEADER_HASHES",),
}
OBJECT_RECONSTRUCTION_ROWS = (
    ("tests/host/test_scheduler_source_layout.py", "STABLE_ARTIFACT_HASHES"),
    ("tests/host/test_endpoint_source_layout.py", "STABLE_HASHES"),
)
OBJECT_RECONSTRUCTION_SOURCES = (
    "kernel/kernel_objects.c",
    "kernel/kernel_object_state.c",
    "kernel/kernel_object_identity.c",
    "kernel/kernel_object_stack.c",
    "kernel/kernel_object_process.c",
    "kernel/kernel_object_thread.c",
    "kernel/kernel_object_context.c",
    "kernel/kernel_object_hart.c",
    "kernel/kernel_object_validation.c",
)

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
        "152ba765d3a332961b59e88a7725f4a823c335bb62b97ba09c662bb84e1098fd",
    "fixture-artifacts":
        "3cff438cd00a5ee1b78bffdedb2b9e66c123c9972ab048eda1854aff2918d149",
    "kernel-structure":
        "667e133360ea23123cabcda1f600e18b7b0ab1419d415f6b25bf885a0d69523d",
    "recipe-impact":
        "a14c50d641800083794bfa4f67e3b275e02528b181afc82879a6dae9f0f898dd",
    "service-structure":
        "97730d70d876f7504caa34c9ca64e7884b2cc5f4d591790f2480371602e3d99e",
    "tool-structure":
        "29bee701c5920506b72b9ad5f60da85521170673a222a3770c6d96953c6ed73b",
    "stack-source-bindings":
        "8148089c54841e214d43b1f8f811eadd40904a4b39f88817d98dfb74477c33b6",
    "stack-firmware-artifact":
        "9d2683554290ebe151342a003891705b6351aeddc69fddf2b32b256d226e58bb",
    "stack-vm-lifetime":
        "17d0e193b7747ce3df1efe34c1fa6d777201f1d95ad6e07cec4a5904d57032a4",
    "runtime-memory-baseline":
        "df7b43a3b7dfb185a109e5d2b95fc610681a16127c8542ade497e99254e6f729",
    "scheduler-packed-transport":
        "6949be2673b5e0d6cf694ad2cfa7f7c810c468209f0c6d3c1d4b6de11c0de309",
    "diagnostic-history-baseline":
        "d30a3bf2737a3a38b20940e4acde18e26de252198d9a3ee1bbf24b4e8e973d49",
    "terminal-failure-baseline":
        "65851676be2a3e117557f4e73f5b12b078a06d54918e02f3e43179d902a6f3bd",
    "panic-trace-baseline":
        "d442149bc63732d9409a9f045cba855bae31d6cab675455717aa297f2d064740",
    "bootstrap-allocation-baseline":
        "36e0a2b18375288c5b43b8ccb01dbf8faff137db16120b693d7f8d8456108811",
    "kernel-mapping-baseline":
        "f2388c75814e42ff852e397a35e37bddf80f171f65074ade752dae10e6ddff92",
    "memory-native-admission":
        "9fdb9c93e92ff742f563d63e54e574e5e630260c07dddeec9997ed2d8144ec16",
    "selected-ownership-baseline":
        "473d2640bd3d9a756183d51b2d6d6ad31aa890f9f8efeebdf1d76d42d514ed2c",
    "selected-ownership-native-admission":
        "4678706adb8ef75162ed57b16821af9262b06ed642a721f482778dbba381d3ff",
    "selected-address-space-baseline":
        "205d2a15b6dd9c1b8cc3688f53194b5844b2f13e1975cf156e8f6b02cfcfbfe4",
    "selected-address-space-native-admission":
        "eae7b3d815f8f239094921fe2cd97b2508ada96b0e53eff394f84a9359477f88",
}

_MEMORY_NATIVE_COUNTS = {
    "MEM01/function": 85,
    "MEM01/definitions": 5,
    "MEM01/skeleton": 5,
    "MEM01/header": 1,
    "MEM01/test-guard": 1,
    "MEM02/native-input": 183,
    "MEM02/function": 42,
    "MEM02/definitions": 2,
    "MEM02/skeleton": 2,
    "MEM02/header": 1,
    "MEM02/include": 1,
}

_SELECTED_OWNERSHIP_NATIVE_COUNTS = {
    "function": 251,
    "definitions": 17,
    "skeleton": 17,
    "header": 3,
    "native-input": 196,
}

_SELECTED_ADDRESS_SPACE_NATIVE_COUNTS = {
    "function": 218,
    "definitions": 11,
    "skeleton": 11,
    "header": 5,
    "dependent-file": 1,
    "native-input": 198,
    "observation": 33,
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
    if evidence_authority_packed.packed_candidate(tree):
        return True
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


def _object_reconstruction_sources(root, directory):
    path = regular_source(root, "tests/host/test_kernel_object_source_layout.py")
    tree = ast.parse(path.read_text(encoding="utf-8"))
    sources = literal_value(single_assignment(tree, "CORE_SOURCES").value)
    headers = literal_value(single_assignment(tree, "PRIVATE_HEADERS").value)
    if (
        sources != OBJECT_RECONSTRUCTION_SOURCES or type(headers) is not tuple
        or not headers or headers[0] != "kernel/kernel_object_private.h"
    ):
        raise ValueError("object reconstruction source/header membership changed")
    return kernel_sources(root, (*sources, headers[0]), directory).values()


def _semantic_payload_paths(root, name, leaf, literals, relative, directory):
    paths = set()
    if (
        relative == "tests/host/kernel_layout_source.py"
        and name == "BOUNDED_FRAME_VALIDATION_SHA256" and not leaf
    ):
        paths.update(kernel_sources(
            root, ("kernel/frame_ownership_validation.c",), directory,
        ).values())
    elif (
        name in LOGICAL_FILE_TABLES.get(relative, ()) and len(leaf) == 1
        and leaf[0].startswith("kernel/") and len(Path(leaf[0]).parts) == 2
    ):
        if leaf[0] == "kernel/kernel_objects.c" and (relative, name) in OBJECT_RECONSTRUCTION_ROWS:
            paths.update(_object_reconstruction_sources(root, directory))
        else:
            paths.update(kernel_sources(root, leaf, directory).values())
    elif name == "BASELINE_BODIES":
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
    root, family, relative, tree, literals, declared_paths, directory
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
                *_semantic_payload_paths(root, name, leaf, literals, relative, directory),
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


def _runtime_memory_facts(tree, family, relative):
    declarations = _literal_assignments(tree)
    references = (
        "docs/architecture/runtime-memory-primitives.md",
        "docs/adr/0090-minix-word-based-runtime-memory.md",
        "docs/research/minix-runtime-memory-primitives.md",
        relative,
    )
    anchor = tuple(declarations.get(name) for name in (
        "DESIGN_COMMIT", "DESIGN_TREE", "DESIGN_DOCUMENT",
    ))
    oracles = declarations.get("ORACLE_DECLARATIONS")
    if any(type(value) is not str for value in anchor) or (
        type(oracles) is not tuple or len(oracles) != 2
        or any(type(row) is not tuple or len(row) != 3 or any(type(value) is not str for value in row)
               for row in oracles)
    ):
        raise ValueError("runtime memory authority declaration is malformed")
    consumers = tuple(f"B{index:02d}" for index in range(5, 13)) + ("B14",)
    facts = [Fact(
        relative + ":design-anchor:COPY01", family, relative, "design-anchor", "COPY01",
        AuthorityClass.TRANSPORT_IDENTITY, "B05", consumers, tuple(sorted(references)), ":".join(anchor),
    )]
    for identifier, test, property_name in oracles:
        paths = (
            *references, "tests/host/runtime_memory_fixture.c", "tests/host/runtime_memory_evidence.py",
            "tests/host/test_runtime_memory.py", "tools/check_runtime_memory.py",
        )
        facts.append(Fact(
            relative + ":baseline-oracle:" + identifier, family, relative,
            "baseline-oracle", identifier, AuthorityClass.BASELINE_ORACLE, "B05", consumers,
            tuple(sorted(paths)), test + ":" + property_name,
        ))
    return facts


def _memory_native_binding_row(row):
    if (
        type(row) is not tuple or len(row) != 4
        or any(type(value) is not str or not value for value in row[:2])
        or type(row[2]) is not tuple
        or any(type(path) is not str for path in row[2])
        or type(row[3]) is not dict
    ):
        raise ValueError("memory-native authority has a malformed binding row")
    scope, subject, assertions, contract = row
    parts = scope.split("/", 2)
    if len(parts) != 3 or "/".join(parts[:2]) not in _MEMORY_NATIVE_COUNTS:
        raise ValueError("memory-native authority has an unknown clause")
    task, kind, suffix = parts
    expected_assertions = (
        ("tests/host/bootstrap_allocation_source.py",) if task == "MEM01" else
        ("tests/host/kernel_mapping_source.py", "tools/generate_kernel_mapping_fixture.py")
        if kind == "native-input" else ("tests/host/kernel_mapping_source.py",)
    )
    if assertions != expected_assertions:
        raise ValueError("memory-native authority has an unknown assertion owner")
    if kind == "function":
        prefix = subject + "/"
        if not suffix.startswith(prefix) or re.fullmatch(
            r"[A-Za-z_][A-Za-z0-9_]*", suffix[len(prefix):],
        ) is None:
            raise ValueError("memory-native authority has an invalid function scope")
    elif kind == "test-guard":
        if suffix != subject + "/managed_frame_is_allowed":
            raise ValueError("memory-native authority has an invalid test-guard scope")
    elif kind == "include":
        if suffix != subject + "/micros-bootstrap-memory":
            raise ValueError("memory-native authority has an invalid include scope")
    elif suffix != subject:
        raise ValueError("memory-native authority borrowed another subject")
    comparisons = {
        "function": ("function", {"comparison", "signature", "guards", "content", "linkage"}),
        "definitions": ("definition-set", {"comparison", "expected"}),
        "skeleton": ("lexical-skeleton-sha256", {"comparison", "expected"}),
        "header": ("lexical-header-sha256", {"comparison", "expected"}),
        "native-input": ("file-bytes-sha256", {"comparison", "expected"}),
        "test-guard": (
            "exact-test-wrapper", {"comparison", "function", "opening", "closing", "occurrences"},
        ),
        "include": ("exact-include", {"comparison", "text", "occurrences"}),
    }
    comparison, fields = comparisons[kind]
    if set(contract) != fields or contract["comparison"] != comparison:
        raise ValueError("memory-native authority changed a comparison schema")
    return task + "/" + kind


def _memory_native_binding_facts(root, family, relative):
    if relative != "tests/host/memory_native_source_bindings.py":
        raise ValueError("memory-native authority has an undeclared declaration owner")
    tree = ast.parse(regular_source(root, relative).read_text(encoding="utf-8"))
    statements = tree.body
    if statements and isinstance(statements[0], ast.Expr) and isinstance(
        statements[0].value, ast.Constant,
    ) and type(statements[0].value.value) is str:
        statements = statements[1:]
    names = ("BINDING_REFERENCE", "NATIVE_SOURCE_BINDINGS")
    if len(statements) != 2 or any(
        not isinstance(node, ast.Assign) or _assignment_name(node) not in names
        for node in statements
    ):
        raise ValueError("memory-native authority permits only its literal declarations")
    reference, rows = (
        literal_value(single_assignment(tree, name).value) for name in names
    )
    if type(reference) is not tuple or len(reference) != 3:
        raise ValueError("memory-native authority has an invalid reference")
    for index, (tag, size) in enumerate((("policy", 3), ("MEM01", 2), ("MEM02", 2))):
        entry = reference[index]
        if (
            type(entry) is not tuple or len(entry) != size or entry[0] != tag
            or any(
                type(value) is not str or re.fullmatch(r"[0-9a-f]{40}", value) is None
                for value in entry[1:]
            )
        ):
            raise ValueError("memory-native authority has an invalid fixed reference")
    if type(rows) is not tuple or len(rows) != sum(_MEMORY_NATIVE_COUNTS.values()):
        raise ValueError("memory-native authority omitted or added a binding")
    facts, observed, counts, checked_paths = [], set(), Counter(), {relative}
    consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
    for row in rows:
        clause = _memory_native_binding_row(row)
        scope, subject, assertions, contract = row
        if scope in observed:
            raise ValueError("memory-native authority has duplicate binding ownership")
        observed.add(scope)
        counts[clause] += 1
        paths = tuple(sorted({relative, subject, *assertions}))
        for path in paths:
            if path not in checked_paths:
                regular_source(root, path)
                checked_paths.add(path)
        facts.append(Fact(
            relative + ":native-source-admission:" + scope, family, relative,
            "native-source-admission", scope, AuthorityClass.TRANSPORT_IDENTITY,
            "B02", consumers, paths,
            digest((reference, scope, subject, assertions, contract)),
        ))
    if counts != _MEMORY_NATIVE_COUNTS or fact_set_digest(facts) != (
        DECLARED_FAMILY_FACT_DIGESTS[family]
    ):
        raise ValueError("memory-native authority differs from its independent declaration")
    return tuple(facts)


def _selected_ownership_native_row(row):
    if (
        type(row) is not tuple or len(row) != 4
        or any(type(value) is not str or not value for value in row[:2])
        or type(row[2]) is not tuple
        or any(type(path) is not str for path in row[2])
        or type(row[3]) is not dict
    ):
        raise ValueError("selected-ownership native authority has a malformed row")
    scope, subject, assertions, contract = row
    parts = scope.split("/", 2)
    if len(parts) != 3 or parts[0] != "MEM03" or parts[1] not in _SELECTED_OWNERSHIP_NATIVE_COUNTS:
        raise ValueError("selected-ownership native authority has an unknown clause")
    _, kind, suffix = parts
    source = "tests/host/selected_ownership_source.py"
    generator = "tools/generate_selected_ownership_fixture.py"
    if kind == "native-input":
        if not subject.endswith((".c", ".h")):
            raise ValueError("selected-ownership native authority has an unknown dependency kind")
        expected_assertions = (source, generator) if subject.endswith(".c") else (generator,)
    else:
        expected_assertions = (source,)
    if assertions != expected_assertions:
        raise ValueError("selected-ownership native authority changed assertion ownership")
    if kind == "function":
        prefix = subject + "/"
        if not suffix.startswith(prefix) or re.fullmatch(
            r"[A-Za-z_][A-Za-z0-9_]*", suffix[len(prefix):],
        ) is None:
            raise ValueError("selected-ownership native authority has an invalid function scope")
    elif suffix != subject:
        raise ValueError("selected-ownership native authority borrowed another subject")
    comparisons = {
        "function": ("function", {"comparison", "signature", "guards", "content"}),
        "definitions": ("definition-set", {"comparison", "expected"}),
        "skeleton": ("compact-skeleton-sha256", {"comparison", "expected"}),
        "header": ("compact-header-sha256", {"comparison", "expected"}),
        "native-input": ("file-bytes-sha256", {"comparison", "expected"}),
    }
    comparison, fields = comparisons[kind]
    if set(contract) != fields or contract["comparison"] != comparison:
        raise ValueError("selected-ownership native authority changed a comparison schema")
    return kind


def _selected_ownership_native_facts(root, family, relative):
    if relative != "tests/host/selected_ownership_native_source_bindings.py":
        raise ValueError("selected-ownership native authority has an undeclared owner")
    tree = ast.parse(regular_source(root, relative).read_text(encoding="utf-8"))
    statements = tree.body
    if statements and isinstance(statements[0], ast.Expr) and isinstance(
        statements[0].value, ast.Constant,
    ) and type(statements[0].value.value) is str:
        statements = statements[1:]
    names = ("BINDING_REFERENCE", "NATIVE_SOURCE_BINDINGS")
    if len(statements) != 2 or any(
        not isinstance(node, ast.Assign) or _assignment_name(node) not in names
        for node in statements
    ):
        raise ValueError("selected-ownership native authority permits only literal declarations")
    reference, rows = (literal_value(single_assignment(tree, name).value) for name in names)
    if type(reference) is not tuple or len(reference) != 2:
        raise ValueError("selected-ownership native authority has an invalid reference")
    for entry, (tag, size) in zip(reference, (("policy", 3), ("MEM03", 2))):
        if (
            type(entry) is not tuple or len(entry) != size or entry[0] != tag
            or any(
                type(value) is not str or re.fullmatch(r"[0-9a-f]{40}", value) is None
                for value in entry[1:]
            )
        ):
            raise ValueError("selected-ownership native authority has an invalid fixed reference")
    if type(rows) is not tuple or len(rows) != sum(_SELECTED_OWNERSHIP_NATIVE_COUNTS.values()):
        raise ValueError("selected-ownership native authority omitted or added a binding")
    facts, scopes, counts, checked_paths = [], [], Counter(), {relative}
    consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
    for row in rows:
        counts[_selected_ownership_native_row(row)] += 1
        scope, subject, assertions, contract = row
        scopes.append(scope)
        paths = tuple(sorted({relative, subject, *assertions}))
        for path in paths:
            if path not in checked_paths:
                regular_source(root, path)
                checked_paths.add(path)
        facts.append(Fact(
            relative + ":native-source-admission:" + scope, family, relative,
            "native-source-admission", scope, AuthorityClass.TRANSPORT_IDENTITY,
            "B02", consumers, paths, digest((reference, scope, subject, assertions, contract)),
        ))
    if scopes != sorted(set(scopes)) or counts != _SELECTED_OWNERSHIP_NATIVE_COUNTS or (
        fact_set_digest(facts) != DECLARED_FAMILY_FACT_DIGESTS[family]
    ):
        raise ValueError("selected-ownership native authority differs from its independent declaration")
    return tuple(facts)


def _selected_address_space_native_row(row):
    if (
        type(row) is not tuple or len(row) != 4
        or any(type(value) is not str or not value for value in row[:2])
        or row[2] != ("tests/host/selected_address_space_source.py",)
        or type(row[3]) is not dict
    ):
        raise ValueError("selected-address-space native authority has a malformed row or assertion owner")
    scope, subject, _, contract = row
    parts = scope.split("/", 2)
    if len(parts) != 3 or parts[0] != "MEM04" or parts[1] not in _SELECTED_ADDRESS_SPACE_NATIVE_COUNTS:
        raise ValueError("selected-address-space native authority has an unknown clause")
    _, kind, suffix = parts
    if kind in ("function", "observation"):
        prefix = subject + "/"
        if not subject.endswith(".c") or not suffix.startswith(prefix) or re.fullmatch(
            r"[A-Za-z_][A-Za-z0-9_]*", suffix[len(prefix):],
        ) is None:
            raise ValueError("selected-address-space native authority has an invalid function scope")
    elif suffix != subject:
        raise ValueError("selected-address-space native authority borrowed another subject")
    if (
        kind == "native-input" and not subject.endswith((".c", ".h"))
        or kind == "header" and not subject.endswith(".h")
        or kind in ("definitions", "skeleton") and not subject.endswith(".c")
        or kind == "dependent-file" and subject != "kernel/memory/user_address_space_handoff.c"
    ):
        raise ValueError("selected-address-space native authority has an unknown subject kind")
    comparisons = {
        "function": ("function", {"comparison", "signature", "guards", "content"}),
        "definitions": ("definition-set", {"comparison", "expected"}),
        "skeleton": ("compact-skeleton-sha256", {"comparison", "expected"}),
        "header": ("file-bytes-sha256", {"comparison", "expected"}),
        "dependent-file": ("file-bytes-sha256", {"comparison", "expected"}),
        "native-input": ("file-bytes-sha256", {"comparison", "expected"}),
        "observation": ("current-observation-shape", {"comparison", "automatic", "explicit"}),
    }
    comparison, fields = comparisons[kind]
    if set(contract) != fields or contract["comparison"] != comparison:
        raise ValueError("selected-address-space native authority changed a comparison schema")
    return kind


def _selected_address_space_native_facts(root, family, relative):
    if relative != "tests/host/selected_address_space_native_source_bindings.py":
        raise ValueError("selected-address-space native authority has an undeclared owner")
    tree = ast.parse(regular_source(root, relative).read_text(encoding="utf-8"))
    statements = tree.body
    if statements and isinstance(statements[0], ast.Expr) and isinstance(
        statements[0].value, ast.Constant,
    ) and type(statements[0].value.value) is str:
        statements = statements[1:]
    names = ("BINDING_REFERENCE", "NATIVE_SOURCE_BINDINGS")
    if len(statements) != 2 or any(
        not isinstance(node, ast.Assign) or _assignment_name(node) not in names
        for node in statements
    ):
        raise ValueError("selected-address-space native authority permits only literal declarations")
    reference, rows = (literal_value(single_assignment(tree, name).value) for name in names)
    if type(reference) is not tuple or len(reference) != 2:
        raise ValueError("selected-address-space native authority has an invalid reference")
    for entry, (tag, size) in zip(reference, (("policy", 3), ("MEM04", 2))):
        if (
            type(entry) is not tuple or len(entry) != size or entry[0] != tag
            or any(
                type(value) is not str or re.fullmatch(r"[0-9a-f]{40}", value) is None
                for value in entry[1:]
            )
        ):
            raise ValueError("selected-address-space native authority has an invalid fixed reference")
    if type(rows) is not tuple or len(rows) != sum(_SELECTED_ADDRESS_SPACE_NATIVE_COUNTS.values()):
        raise ValueError("selected-address-space native authority omitted or added a binding")
    facts, scopes, counts, checked_paths = [], [], Counter(), {relative}
    consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
    for row in rows:
        counts[_selected_address_space_native_row(row)] += 1
        scope, subject, assertions, contract = row
        scopes.append(scope)
        paths = tuple(sorted({relative, subject, *assertions}))
        for path in paths:
            if path not in checked_paths:
                regular_source(root, path)
                checked_paths.add(path)
        facts.append(Fact(
            relative + ":native-source-admission:" + scope, family, relative,
            "native-source-admission", scope, AuthorityClass.TRANSPORT_IDENTITY,
            "B02", consumers, paths, digest((reference, scope, subject, assertions, contract)),
        ))
    if scopes != sorted(set(scopes)) or counts != _SELECTED_ADDRESS_SPACE_NATIVE_COUNTS or (
        fact_set_digest(facts) != DECLARED_FAMILY_FACT_DIGESTS[family]
    ):
        raise ValueError("selected-address-space native authority differs from its independent declaration")
    return tuple(facts)


def _facts_for_owner(root, family, relative, directory=None):
    if family == "memory-native-admission":
        return _memory_native_binding_facts(root, family, relative)
    if family == "selected-ownership-native-admission":
        return _selected_ownership_native_facts(root, family, relative)
    if family == "selected-address-space-native-admission":
        return _selected_address_space_native_facts(root, family, relative)
    tree = ast.parse((root / relative).read_text(encoding="utf-8"))
    if relative == "tests/host/kernel_layout_source.py":
        value = literal_value(single_assignment(tree, "BOUNDED_FRAME_VALIDATION_SHA256").value)
        if type(value) is not str or re.fullmatch(r"[0-9a-f]{64}", value) is None:
            raise ValueError("standalone frame-validator authority requires a scalar SHA256")
    packed = evidence_authority_packed.checked_declarations(tree, relative)
    if family == evidence_authority_packed.FAMILY:
        if relative != evidence_authority_packed.OWNER:
            raise ValueError("packed authority family has an undeclared owner")
        return [
            Fact(
                f"{relative}:{kind}:{scope}", family, relative, kind, scope,
                AuthorityClass.TRANSPORT_IDENTITY, _fact_primary_unit(paths),
                _consumer_units(relative, paths), paths, value,
            )
            for kind, scope, paths, value in evidence_authority_packed.packed_records(root, tree, packed, directory)
        ]
    if family == "runtime-memory-baseline":
        return _runtime_memory_facts(tree, family, relative)
    if family == diagnostic_history_source.FAMILY:
        return diagnostic_history_source.declaration_facts(root, relative)
    if family == terminal_failure_source.FAMILY:
        return terminal_failure_source.declaration_facts(root, relative)
    if family == panic_trace_source.FAMILY:
        return panic_trace_source.declaration_facts(root, relative)
    if family == bootstrap_allocation_source.FAMILY:
        return bootstrap_allocation_source.declaration_facts(root, relative)
    if family == kernel_mapping_source.FAMILY:
        return kernel_mapping_source.declaration_facts(root, relative)
    if family == selected_ownership_source.FAMILY:
        return selected_ownership_source.declaration_facts(root, relative)
    if family == selected_address_space_source.FAMILY:
        return selected_address_space_source.declaration_facts(root, relative)
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
            directory,
        ),
        *_function_facts(
            root, family, relative, tree, declared_paths
        ),
    ]


def current_inventory(root):
    root = Path(root).resolve()
    directory = kernel_directory(root)
    families = tuple(
        Family(family, owners)
        for family, owners in FAMILY_OWNERS.items()
    )
    facts = tuple(
        fact
        for family, owners in FAMILY_OWNERS.items()
        for owner in owners
        for fact in _facts_for_owner(root, family, owner, directory)
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
