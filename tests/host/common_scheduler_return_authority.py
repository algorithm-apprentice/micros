"""OBJ02's Accepted scope, immutable attempts and exact execution authority."""

import ast
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

from tests.host.evidence_authority_source import literal_value, single_assignment
from tests.host.evidence_authority_types import (
    AuthorityClass, AuthorityKind, Environment, EventKind, Oracle, Plan, Provenance,
    TaskAuthority, TransitionClaim, TransitionRegistry, TransitionState, digest, fact_set_digest,
)
from tests.host.common_scheduler_return_source import (
    AUTHORITY_PATH, FAMILY, NATIVE_FAMILY, RECIPE_ADMISSION, SOURCE_PATH, SOURCE_PROJECTION,
    SOURCE_ROLES, candidate_paths,
)
from tests.host.object_hart_validation_authority import (
    AuthorContext, Journal, begin_attempt, read_current, read_phase_record, read_selection,
)
from tools.stack_artifact_files import read_regular


TRANSITION = "OBJ02-bounded-common-scheduler-return"
COMPLETED_CLAIM = ("OBJ02-bounded-common-scheduler-return", "OBJ02", ("B03-F002", "B03-F005"))
DESIGN_PINS = {
    "design_commit": "cc6e0eee984ea1731eca9fc02bc4132ead1d2e2c",
    "design_tree": "5d2a9d6022b08b20cba75b6b1c33da5f6df4db09",
    "original_design_commit": "015ec2d7920551caacbdcb753ecadedd7fd3deba",
    "original_design_tree": "c0d383285e719fcfdc8b238516d6c0216c3f019f",
    "runtime_reference": "34125516b60e9e0a9e5362ff2ee60555691adb99",
    "runtime_tree": "1de53de9dcf5102dbef3e76d59b7f2fef28cee9e",
    "minix_reference": "4db99f4012570a577414fe2a43697b2f239b699e",
    "minix_tree": "80db89de58d5ecaf31efe48b4d1d98c7e285e81f",
    "design_document": "docs/architecture/common-scheduler-return.md",
    "adr_document": "docs/adr/0122-obj02-evidence-consumer-scope.md",
    "runtime_adr_document": "docs/adr/0121-bounded-common-scheduler-return.md",
    "research_document": "docs/research/minix-common-scheduler-return.md",
}
ORACLE_RULES = (('obj02-bounded-return-delta',
  'tests.host.common_scheduler_return_evidence.CommonSchedulerReturnOracles.test_bounded_return_delta',
  'zero-workspace-and-shifts;bounded-plan-and-selected-work;linked-queue-state;checked-commit',
  'Compact portable plan',
  ('tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_core_return.c/2',
   'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/include/micros/scheduler_core.h',
   'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/scheduler/scheduler_core_return.c',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-input/include/micros/scheduler_core.h',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-input/kernel/scheduler/scheduler_core_return.c',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return.c/micros_hart_commit_user_return',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return.c/micros_scheduler_apply_return_plan',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return.c/plans_equal',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/micros_hart_plan_user_return',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/preflight_return_threads',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/project_candidate_queues',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/repair_preempted_candidate',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/scratch_head',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/scratch_load',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/scratch_pick',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/scratch_remove',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/scratch_tail',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/current-observation/kernel/scheduler/scheduler_core_return_plan.c/select_and_renew_candidate',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/definitions/kernel/scheduler/scheduler_core_return_plan.c',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/micros_hart_plan_user_return',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/preflight_return_threads',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/project_candidate_queues',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/repair_preempted_candidate',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/scratch_head',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/scratch_load',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/scratch_pick',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/scratch_remove',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/scratch_tail',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/function/kernel/scheduler/scheduler_core_return_plan.c/select_and_renew_candidate',
   'tests/host/object_hart_native_source_bindings.py:native-source-admission:OBJ01/skeleton/kernel/scheduler/scheduler_core_return_plan.c',
   'tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/include/micros/scheduler_core.h',
   'tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/scheduler/scheduler_core_return.c',
   'tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/include/micros/scheduler_core.h',
   'tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/scheduler/scheduler_core_return.c',
   'tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/include/micros/scheduler_core.h',
   'tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_core_return.c',
   'tests/host/test_kernel_object_source_layout.py:data-entry:STABLE_HASHES/include/micros/scheduler_core.h',
   'tests/host/test_scheduler_source_layout.py:data-entry:STABLE_ARTIFACT_HASHES/include/micros/scheduler_core.h',
   'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_hart_commit_user_return',
   'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/micros_scheduler_apply_return_plan',
   'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/plans_equal',
   'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/scratch_head',
   'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/scratch_pick',
   'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/scratch_remove',
   'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler_core.c/scratch_tail')),
 ('obj02-protected-context-reuse',
  'tests.host.common_scheduler_return_evidence.CommonSchedulerReturnOracles.test_protected_context_reuse',
  'actual-same-different-captured-validation-counts;exact-context-custody;retained-target-semantics',
  'Retained outgoing-context proof',
  ('tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler.c/2',
   'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_runtime_internal.h/2',
   'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_test_finish.c/2',
   'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/scheduler/scheduler_runtime_internal.h',
   'tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/function/kernel/scheduler/scheduler_context.c/scheduler_runtime_load_selected_context',
   'tests/host/selected_address_space_native_source_bindings.py:native-source-admission:MEM04/native-input/kernel/scheduler/scheduler_runtime_internal.h',
   'tests/host/selected_ownership_native_source_bindings.py:native-source-admission:MEM03/native-input/kernel/scheduler/scheduler_runtime_internal.h',
   'tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_context.c',
   'tests/host/selected_release_handoff_native_source_bindings.py:native-source-admission:MEM05/native-input/kernel/scheduler/scheduler_runtime_internal.h',
   'tests/host/test_scheduler_source_layout.py:data-table:FROZEN_FACTS/kernel/scheduler.c/micros_scheduler_idle_select',
   'tests/host/test_scheduler_source_layout.py:data-table:FROZEN_FACTS/kernel/scheduler.c/micros_scheduler_start',
   'tests/host/test_scheduler_source_layout.py:data-table:FROZEN_FACTS/kernel/scheduler.c/select_user_return',
   'tests/host/test_scheduler_source_layout.py:function-body:FROZEN_FUNCTIONS/kernel/scheduler.c/load_selected_context')))
RECERTIFICATION_UNITS = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)
PHASE_KINDS = {
    "red": EventKind.RED_OBSERVED,
    "green": EventKind.GREEN_OBSERVED,
    "recertify": EventKind.AUTHOR_RECERTIFIED,
}
EXTERNAL_INPUTS = (
    "@obj02/native-compiler", "@obj02/native-nm", "@obj02/closed-tools", "@obj02/native-cache",
    "@obj02/qemu-firmware", "@obj02/accepted-design", "@obj02/frozen-candidates",
)
JOURNAL_PATH = "build/evidence-transitions/OBJ02"
_provisional = None


def git(root, *arguments):
    completed = subprocess.run(
        ("git", *arguments), cwd=root, stdin=subprocess.DEVNULL, capture_output=True, timeout=30,
    )
    if completed.returncode:
        raise ValueError("OBJ02 fixed Git authority is unavailable: " + completed.stderr.decode())
    return completed.stdout


def design_blob(root, path):
    if path not in tuple(DESIGN_PINS[name] for name in ("design_document", "adr_document", "runtime_adr_document", "research_document")):
        raise ValueError("OBJ02 design input leaves its fixed namespace")
    for commit, tree in (
        (DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"]),
        (DESIGN_PINS["original_design_commit"], DESIGN_PINS["original_design_tree"]),
        (DESIGN_PINS["runtime_reference"], DESIGN_PINS["runtime_tree"]),
    ):
        if git(root, "rev-parse", commit + "^{tree}").decode().strip() != tree:
            raise ValueError("OBJ02 fixed commit/tree identity changed")
    raw = git(root, "show", DESIGN_PINS["design_commit"] + ":" + path)
    if path == DESIGN_PINS["runtime_adr_document"] and raw != git(
        root, "show", DESIGN_PINS["original_design_commit"] + ":" + path,
    ):
        raise ValueError("OBJ02 correction rewrote its original Accepted runtime decision")
    return raw


def base_membership(root):
    result = {}
    for entry in filter(None, git(root, "ls-tree", "-r", "-z", DESIGN_PINS["design_commit"]).split(b"\0")):
        metadata, separator, raw_path = entry.partition(b"\t")
        fields, path = metadata.split(), raw_path.decode()
        if (
            not separator or len(fields) != 3 or fields[0] not in (b"100644", b"100755")
            or fields[1] != b"blob" or path in result or Path(path).is_absolute()
            or ".." in Path(path).parts or "\\" in path
        ):
            raise ValueError("OBJ02 implementation base has a nonordinary member")
        result[path] = fields[0].decode(), fields[2].decode()
    if len(result) != 1551 or digest(result) != SOURCE_ROLES["base_membership_sha256"]:
        raise ValueError("OBJ02 independently frozen base membership changed")
    return {path: (int(mode, 8) & 0o777, oid) for path, (mode, oid) in result.items()}


def predecessor_records(root, inventory):
    from tests.host.evidence_authority_validation import validate_inventory

    checked = validate_inventory(inventory)
    if not checked.ok:
        raise ValueError("OBJ02 current authority inventory is invalid: " + str(checked.error))
    tree = ast.parse(git(
        root, "show", DESIGN_PINS["runtime_reference"] + ":tests/host/evidence_authority_inventory.py",
    ).decode())
    expected = literal_value(single_assignment(tree, "DECLARED_FAMILY_FACT_DIGESTS").value)
    if type(expected) is not dict or len(expected) != 27 or {FAMILY, NATIVE_FAMILY} & set(expected):
        raise ValueError("OBJ02 fixed predecessor families changed")
    records = tuple(fact for fact in inventory.facts if fact.family in expected)
    if (
        len(records) != 6327 or fact_set_digest(records) != SOURCE_PROJECTION["predecessor_facts_sha256"]
        or any(fact_set_digest(fact for fact in records if fact.family == name) != fingerprint
               for name, fingerprint in expected.items())
    ):
        raise ValueError("OBJ02 changed a predecessor fact or family fingerprint")
    return records


def declared_scope(root):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host import object_hart_validation_authority as previous

    if any(b"\n- Status: Accepted\n" not in design_blob(root, DESIGN_PINS[name])
           for name in ("adr_document", "runtime_adr_document")):
        raise ValueError("OBJ02 requires its separately merged Accepted design")
    contract = design_blob(root, DESIGN_PINS["design_document"]).decode()
    if SOURCE_PROJECTION["affected_facts_sha256"] not in contract:
        raise ValueError("OBJ02 selection lacks its Accepted complete-record digest")
    keys = SOURCE_PROJECTION["retirement_keys"]
    prior = predecessor_records(root, current_inventory(root))
    facts = tuple(sorted((fact for fact in prior if fact.key in keys), key=lambda fact: fact.key))
    if (
        len(keys) != 58 or keys != tuple(sorted(set(keys))) or len(facts) != 58
        or fact_set_digest(facts) != SOURCE_PROJECTION["affected_facts_sha256"]
    ):
        raise ValueError("OBJ02 selected identities differ from the complete fixed records")
    if previous.committed_claim(root) is None:
        raise ValueError("OBJ02 requires its complete committed OBJ01 predecessor")
    _, authorities, _, registry = previous.task_definition(root)
    if len(registry.claims) != 8 or sum(len(claim.retired_identity_keys) for claim in registry.claims) != 280:
        raise ValueError("OBJ02 lost a predecessor claim")
    if set(keys) & {key for claim in registry.claims for key in claim.retired_identity_keys}:
        raise ValueError("OBJ02 selected an already retired identity")
    closure = {path for fact in facts for path in (fact.owner_path, *fact.payload_paths)}
    if closure != set(candidate_paths()):
        raise ValueError("OBJ02 direct source roles differ from the selected identity closure")
    groups = tuple(tuple(keys.index(key) for key in row[4]) for row in ORACLE_RULES)
    if (
        tuple(map(len, groups)) != (45, 13)
        or len(set(groups[0]) | set(groups[1])) != 58 or set(groups[0]) & set(groups[1])
    ):
        raise ValueError("OBJ02 oracle partition is incomplete or overlapping")
    blocks = re.findall(r"^```text\n(.*?)\n```$", contract, re.M | re.S)
    for paths in (SOURCE_ROLES["production_paths"], SOURCE_ROLES["immutable_additions"], SOURCE_ROLES["immutable_adapters"]):
        if not any(set(block.splitlines()) == set(paths) for block in blocks):
            raise ValueError("OBJ02 roles differ from an Accepted closed path inventory")
    others = {*SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["unchanged_candidates"]}
    if not any(set(block.splitlines()) == others for block in blocks):
        raise ValueError("OBJ02 candidate adapters differ from the Accepted direct domain")
    return contract, authorities, registry


def declared_commands(contract, executable):
    from tools.python_host_runtime import python_command

    phases = re.findall(
        r'^"\$\{MICROS_PYTHON\}" -E -S -B tools/check_common_scheduler_return.py --(red|green|recertify)$',
        contract, re.M,
    )
    if tuple(phases) != tuple(PHASE_KINDS):
        raise ValueError("OBJ02 design lacks its three exact commands")
    commands = {phase: python_command(executable, RECIPE_ADMISSION["driver"], "--" + phase) for phase in phases}
    rows = re.findall(r"^\| (B[0-9]{2}) \| (.*?) \|$", contract, re.M)
    if tuple(unit for unit, _ in rows) != RECERTIFICATION_UNITS:
        raise ValueError("OBJ02 design omitted or added a recertification unit")
    units = []
    for unit, spelling in rows:
        command = commands["recertify"] if unit == "B14" else tuple(shlex.split(spelling.strip("`")))
        if unit != "B14" and (len(command) != 4 or command[:3] != ("cmake", "--workflow", "--preset")):
            raise ValueError("OBJ02 affected-unit command left its closed workflow shape")
        units.append((unit, command))
    return commands, tuple(units)


def task_definition(root):
    from tools.python_host_runtime import require_current_runtime, selected_executable

    require_current_runtime()
    contract, authorities, registry = declared_scope(root)
    phases, commands = declared_commands(contract, selected_executable(dict(os.environ)))
    candidates = candidate_paths()
    members = tuple(sorted(set(base_membership(root)) | set(SOURCE_ROLES["immutable_additions"])))
    immutable = tuple(path for path in members if path not in candidates)
    findings = ("B03-F002", "B03-F005")
    authority = TaskAuthority(
        "OBJ02", "B03", findings, DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"],
        DESIGN_PINS["design_document"], RECERTIFICATION_UNITS,
        phases["red"], phases["green"], phases["recertify"], commands,
        immutable, tuple((unit, candidates) for unit in RECERTIFICATION_UNITS),
        DESIGN_PINS["runtime_reference"], DESIGN_PINS["runtime_tree"],
    )
    inputs = (
        DESIGN_PINS["design_document"], DESIGN_PINS["adr_document"],
        DESIGN_PINS["runtime_adr_document"], DESIGN_PINS["research_document"],
        *SOURCE_ROLES["immutable_additions"], *SOURCE_ROLES["immutable_adapters"],
    )
    if any("\n## " + row[3] + "\n" not in contract for row in ORACLE_RULES):
        raise ValueError("OBJ02 oracle provenance lacks its Accepted section")
    oracles = tuple(Oracle(
        identifier, test, property_name, keys, AuthorityClass.BASELINE_ORACLE,
        Provenance(AuthorityKind.ACCEPTED_ADAPTATION, path=DESIGN_PINS["design_document"],
                   adr="ADR-0121", section=section), inputs,
    ) for identifier, test, property_name, section, keys in ORACLE_RULES)
    keys = SOURCE_PROJECTION["retirement_keys"]
    plan = Plan(
        TRANSITION, "OBJ02", "B03", findings,
        DESIGN_PINS["design_commit"], DESIGN_PINS["design_tree"], DESIGN_PINS["design_document"],
        keys, (), keys, oracles, RECERTIFICATION_UNITS, EXTERNAL_INPUTS,
        phases["red"], phases["green"], phases["recertify"], commands,
    )
    claim = TransitionClaim(TRANSITION, "OBJ02", keys, tuple(row.oracle_id for row in oracles), keys)
    return authority, {**authorities, "OBJ02": authority}, plan, TransitionRegistry((*registry.claims, claim))


def _literal_declarations(authority_raw, source_raw):
    from tests.host.common_scheduler_return_source import DECLARATION_NAMES

    result = {}
    for raw, names in (
        (authority_raw, ("DESIGN_PINS", "ORACLE_RULES", "COMPLETED_CLAIM")),
        (source_raw, DECLARATION_NAMES),
    ):
        tree = ast.parse(raw.decode())
        result.update((name, literal_value(single_assignment(tree, name).value)) for name in names)
    return result


def committed_claim(root):
    root = Path(root)
    current = _literal_declarations(read_regular(root / AUTHORITY_PATH)[0], read_regular(root / SOURCE_PATH)[0])
    if (
        current["DESIGN_PINS"] != DESIGN_PINS or current["ORACLE_RULES"] != ORACLE_RULES
        or current["COMPLETED_CLAIM"] != (TRANSITION, "OBJ02", ("B03-F002", "B03-F005"))
    ):
        raise ValueError("OBJ02 completion declaration changed its design or scope")
    paths = tuple(name.decode() for name in git(
        root, "ls-tree", "-r", "--name-only", "-z", "HEAD", "--", AUTHORITY_PATH, SOURCE_PATH,
    ).split(b"\0") if name)
    if not paths:
        return None
    if set(paths) != {AUTHORITY_PATH, SOURCE_PATH}:
        raise ValueError("OBJ02 committed claim has an incomplete owner set")
    previous = _literal_declarations(
        git(root, "show", "HEAD:" + AUTHORITY_PATH), git(root, "show", "HEAD:" + SOURCE_PATH),
    )
    if previous != current:
        raise ValueError("OBJ02 staged declarations cannot borrow committed authority")
    keys = SOURCE_PROJECTION["retirement_keys"]
    return TransitionClaim(TRANSITION, "OBJ02", keys, tuple(row[0] for row in ORACLE_RULES), keys)


def current_membership(root):
    base = base_membership(root)
    expected = set(base) | set(SOURCE_ROLES["immutable_additions"])
    observed = tuple(path.decode() for path in git(root, "ls-files", "-z").split(b"\0") if path)
    if len(observed) != len(set(observed)) or set(observed) != expected:
        raise ValueError("OBJ02 staged membership is incomplete or contains unowned inputs")
    roots = tuple(sorted({Path(path).parts[0] for path in expected}))
    if git(root, "ls-files", "--others", "-z", "--", *roots):
        raise ValueError("OBJ02 source namespaces contain untracked or ignored inputs")
    return tuple(sorted(expected)), base


def external_inputs(root):
    import shutil
    from tests.host.diagnostic_history_evidence import closed_tool_inputs
    from tests.host.runtime_memory_evidence import native_compiler
    from tests.host.selected_address_space_evidence import native_symbols_tool
    from tools.stack_bound_fatal_firmware import resolve_default_firmware

    compiler, nm = native_compiler(root), native_symbols_tool()
    qemu = shutil.which("qemu-system-riscv64")
    if qemu is None:
        raise ValueError("OBJ02 requires the configured target tool")
    return compiler, nm, {
        "@obj02/native-compiler": digest(compiler), "@obj02/native-nm": digest(nm),
        "@obj02/closed-tools": digest(closed_tool_inputs(root)),
        "@obj02/native-cache": digest(read_regular(Path(root) / "build/host-debug/CMakeCache.txt")[1]),
        "@obj02/qemu-firmware": digest(resolve_default_firmware(qemu)),
        "@obj02/accepted-design": digest({
            "pins": DESIGN_PINS,
            **{name: hashlib.sha256(design_blob(root, DESIGN_PINS[name])).hexdigest()
               for name in ("design_document", "adr_document", "runtime_adr_document", "research_document")},
        }),
        "@obj02/frozen-candidates": digest({
            path: read_regular(Path(root) / path)[1]
            for path in (*SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["unchanged_candidates"])
        }),
    }


def capture_author_context(root, phase):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host.evidence_authority_validation import validate_transition
    from tests.host.common_scheduler_return_source import (
        TARGET_PATHS, extend_target_observations, fixed_source, validate_retained_source, validate_scaffold,
    )
    from tools.stack_bound_manifest import owned_artifact
    from tools.validation_context import capture_candidate

    if phase not in PHASE_KINDS:
        raise ValueError("OBJ02 source capture has an unknown author phase")
    root = Path(root).resolve()
    candidate = capture_candidate(root)
    if not candidate.clean:
        raise ValueError("OBJ02 requires the complete clean staged candidate")
    members, base = current_membership(root)
    files = {path: read_regular(owned_artifact(root, path))[1] for path in members}
    validate_scaffold(root)
    for path in SOURCE_ROLES["production_paths"]:
        raw = read_regular(root / path)[0]
        validate_retained_source(root, path, raw.decode(), "baseline" if phase == "red" else "current")
        if path in TARGET_PATHS and phase == "red" and raw.decode() != extend_target_observations(
            path, fixed_source(str(root), path),
        ):
            raise ValueError("OBJ02 Red target is not the fixed source with exact passive hooks")
    allowed = {*SOURCE_ROLES["production_paths"], *SOURCE_ROLES["fixed_adapters"], *SOURCE_ROLES["immutable_adapters"]}
    for path, (mode, oid) in base.items():
        if path in allowed or path.endswith(".md"):
            continue
        raw, identity = read_regular(root / path)
        observed = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
        if (identity["mode"], observed) != (mode, oid):
            raise ValueError("OBJ02 changed a source outside its closed roles: " + path)
    authority, authorities, plan, registry = task_definition(root)
    compiler, nm, external = external_inputs(root)
    candidates = candidate_paths()
    immutable = tuple(sorted(
        [(path, digest(files[path])) for path in members if path not in candidates] + list(external.items())
    ))
    red = []
    for path in candidates:
        if path in SOURCE_ROLES["production_paths"]:
            text = fixed_source(str(root), path)
            if path in TARGET_PATHS:
                text = extend_target_observations(path, text)
            raw = text.encode()
            identity = {"kind": "file", "mode": base[path][0], "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
        else:
            identity = files[path]
        red.append((path, digest(identity)))
    current = tuple(sorted((path, digest(files[path])) for path in candidates))
    environment = Environment(
        immutable, tuple(sorted(red)), current, tuple((unit, current) for unit in RECERTIFICATION_UNITS),
        candidate.source["head"], candidate.source["tree"],
    )
    inventory = current_inventory(root)
    result = validate_transition(inventory, authorities, registry, plan, (), environment)
    if not result.ok or result.state is not TransitionState.REPLACEMENT_REGISTERED:
        raise ValueError("OBJ02 source/declaration closure failed: " + str(result.error))
    if capture_candidate(root) != candidate:
        raise ValueError("OBJ02 source changed during complete input capture")
    return AuthorContext(root, phase, candidate, authority, authorities, plan, registry,
                         inventory, environment, files, compiler, nm)


def recheck_context(context):
    if capture_author_context(context.root, context.phase) != context:
        raise ValueError("OBJ02 immutable inputs changed during execution")


def journal_directory(root):
    directory = Path(root)
    for part in Path(JOURNAL_PATH).parts:
        directory /= part
        if directory.is_symlink() or directory.exists() and not directory.is_dir():
            raise ValueError("OBJ02 journal namespace is linked or non-directory")
    return directory


@contextmanager
def author_lock(root):
    import fcntl
    import stat

    directory = journal_directory(root)
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    if journal_directory(root) != directory:
        raise ValueError("OBJ02 journal namespace changed")
    descriptor = os.open(directory / ".writer.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        information = os.fstat(descriptor)
        if not stat.S_ISREG(information.st_mode) or information.st_nlink != 1:
            raise ValueError("OBJ02 writer lock is not an ordinary unlinked file")
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield directory
    finally:
        os.close(descriptor)


def validate_journal(context, journal):
    from tests.host.diagnostic_history_authority import json_pairs, record_fields, record_strings
    from tests.host.evidence_authority_validation import validate_transition
    from tests.host.common_scheduler_return_evidence import validate_execution_log

    result = validate_transition(
        context.inventory, context.authorities, context.registry, context.plan,
        tuple(row["event"] for row in journal.records), context.environment,
    )
    if not result.ok:
        raise ValueError("OBJ02 transition rejected: " + str(result.error))
    reference = None
    for phase, record in zip(PHASE_KINDS, journal.records):
        if record["event"].expected_property != ";".join(oracle.property for oracle in context.plan.replacement_oracles):
            raise ValueError("OBJ02 event changed its complete property set")
        replay = json.loads(read_regular(journal.directory / (phase + "-execution.log"))[0], object_pairs_hook=json_pairs)
        outcomes = validate_execution_log(context, replay, phase=phase, attempt=journal.attempt, reference=reference)
        if outcomes != record["event"].oracle_results:
            raise ValueError("OBJ02 event borrowed results from a different execution")
        if phase == "red":
            reference = replay
        if phase != "recertify":
            continue
        for unit, command in context.plan.unit_commands:
            value = json.loads(read_regular(journal.directory / ("recertify-" + unit + ".log"))[0], object_pairs_hook=json_pairs)
            record_fields(value, ("version", "unit", "command", "exit_code", "output"))
            if (
                type(value["version"]) is not int or value["version"] != 1 or value["unit"] != unit
                or record_strings(value["command"]) != command
                or type(value["exit_code"]) is not int or value["exit_code"] != 0
                or type(value["output"]) is not str or not value["output"]
            ):
                raise ValueError("OBJ02 affected-unit log lacks its actual successful canonical execution")
    return result


def read_verified_journal(context):
    from tools.stack_bound_manifest import owned_artifact

    directory = journal_directory(context.root)
    current, selector_sha256 = read_selection(directory)
    if current is None:
        raise ValueError("OBJ02 requires a recorded --red attempt")
    selected = owned_artifact(directory, current["attempt"])
    if not selected.is_dir():
        raise ValueError("OBJ02 selected attempt is absent")
    records, previous = [], ""
    for phase in tuple(PHASE_KINDS)[:current["count"]]:
        record, identity = read_phase_record(selected, phase, current["attempt"], previous)
        environment = record["environment"]
        if (
            environment.immutable_inputs != context.environment.immutable_inputs
            or environment.red_candidate_inputs != context.environment.red_candidate_inputs
            or phase != "red" and environment != context.environment
        ):
            raise ValueError("OBJ02 oracle/source/tool/adapter inputs changed; start a fresh --red attempt")
        records.append(record)
        previous = identity["sha256"]
    if previous != current["sha256"] or read_selection(directory) != (current, selector_sha256):
        raise ValueError("OBJ02 selector changed or does not bind its complete raw chain")
    journal = Journal(selected, current["attempt"], tuple(records), previous, selector_sha256)
    return journal, validate_journal(context, journal)


def read_journal(context):
    return read_verified_journal(context)[0]


def baseline_execution(journal):
    from tests.host.diagnostic_history_authority import json_pairs

    if not journal.records:
        raise ValueError("OBJ02 lacks its actual fixed-source execution")
    return json.loads(read_regular(journal.directory / "red-execution.log")[0], object_pairs_hook=json_pairs)


@contextmanager
def provisional_retirement(context, journal, replay):
    from tests.host.common_scheduler_return_evidence import validate_execution_log

    global _provisional
    if _provisional is not None or len(journal.records) != 2 or context.phase != "recertify":
        raise ValueError("OBJ02 provisional retirement requires one complete Green transaction")
    validate_journal(context, journal)
    outcomes = validate_execution_log(
        context, replay, phase="recertify", attempt=journal.attempt, reference=baseline_execution(journal),
    )
    if outcomes != journal.records[-1]["event"].oracle_results:
        raise ValueError("OBJ02 recertification lacks complete actual Green replay")
    recheck_context(context)
    _provisional = os.getpid(), context
    try:
        yield
        recheck_context(context)
    finally:
        _provisional = None


def retired_identity_keys(root):
    from tests.host.evidence_authority_inventory import current_inventory
    from tests.host.evidence_authority_validation import validate_transition

    root = Path(root).resolve()
    keys = SOURCE_PROJECTION["retirement_keys"]
    if any(not (root / path).exists() and not (root / path).is_symlink() for path in (AUTHORITY_PATH, SOURCE_PATH)):
        return frozenset()
    if _provisional is not None and _provisional[0] == os.getpid() and _provisional[1].root == root:
        from tools.validation_context import capture_candidate

        if capture_candidate(root) != _provisional[1].candidate:
            raise ValueError("OBJ02 candidate changed within its checked author transaction")
        return frozenset(keys)
    if committed_claim(root) is not None:
        _, authorities, _, registry = task_definition(root)
        result = validate_transition(current_inventory(root), authorities, registry, None, (), None)
        if not result.ok:
            raise ValueError("OBJ02 committed retirement failed ordinary authority checks")
        return frozenset(key for key in keys if key not in result.active_identity_keys)
    selected = read_current(journal_directory(root))
    if selected is not None and selected["count"] == 3:
        context = capture_author_context(root, "recertify")
        _, result = read_verified_journal(context)
        recheck_context(context)
        return frozenset(key for key in keys if key not in result.active_identity_keys)
    return frozenset()


def identity_required(root, key):
    if key not in SOURCE_PROJECTION["retirement_keys"]:
        from tests.host.object_hart_validation_authority import identity_required as previous

        return previous(root, key)
    return key not in retired_identity_keys(root)
