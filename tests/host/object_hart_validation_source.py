"""Current object/hart observation and complete-audit boundaries."""

from pathlib import Path
import re

from tests.host.c_source_records import function_records


CHANNELS = ("aggregate", "stack", "ordinary")
AUDIT_SOURCES = (
    "kernel/boot/bootstrap_control_core.c", "kernel/boot/bootstrap_preparation_rollback.c",
    "kernel/endpoint/endpoint_object_validation.c", "kernel/execution/user_execution.c",
    "kernel/grant/grant_runtime.c", "kernel/ipc/ipc_runtime.c", "kernel/memory/bootstrap_memory.c",
    "kernel/memory/user_address_space_handoff.c", "kernel/objects/kernel_object_runtime.c",
    "kernel/objects/kernel_objects.c", "kernel/pm/pm_control_execution.c",
    "kernel/pm/pm_control_exit.c", "kernel/pm/pm_control_runtime_core.c",
    "kernel/pm/pm_control_validation.c", "kernel/scheduler/scheduler_core_accounting.c",
    "kernel/scheduler/scheduler_core_ipc.c", "kernel/scheduler/scheduler_core_ipc_transition.c",
    "kernel/scheduler/scheduler_core_policy.c", "kernel/scheduler/scheduler_core_return_plan.c",
)
# Retained complete-audit edges in reviewed #246; guards are part of each boundary.
CURRENT_AUDIT_BOUNDARIES = (
    ("kernel/boot/bootstrap_control_core.c", "micros_bootstrap_control_validate", "micros_kernel_objects_validate", ()),
    ("kernel/boot/bootstrap_preparation_rollback.c", "bootstrap_preparation_rollback_preparation", "micros_kernel_objects_validate", ()),
    ("kernel/endpoint/endpoint_object_validation.c", "micros_endpoint_registry_validate_objects", "micros_kernel_objects_validate", ()),
    ("kernel/execution/user_execution.c", "commit_execution_context", "micros_kernel_objects_validate", ()),
    ("kernel/execution/user_execution.c", "micros_user_execution_detach", "micros_kernel_objects_validate", ()),
    ("kernel/execution/user_execution.c", "prepare_kernel_stack", "micros_kernel_objects_validate", ()),
    ("kernel/execution/user_execution.c", "prepare_post_handoff_binding", "micros_kernel_objects_validate", ()),
    ("kernel/grant/grant_runtime.c", "micros_grant_runtime_initialize", "micros_kernel_object_runtime_validate", ()),
    ("kernel/ipc/ipc_runtime.c", "micros_ipc_runtime_initialize", "micros_kernel_object_runtime_validate", ()),
    ("kernel/memory/bootstrap_memory.c", "validate_process_owners", "micros_kernel_objects_validate", ()),
    ("kernel/memory/user_address_space_handoff.c", "micros_user_address_space_complete_wired_handoff", "micros_kernel_objects_validate", ()),
    ("kernel/memory/user_address_space_handoff.c", "micros_user_address_space_prepare_wired_handoff", "micros_kernel_objects_validate", ()),
    ("kernel/objects/kernel_object_runtime.c", "micros_kernel_object_runtime_attach_address_space", "micros_kernel_objects_validate", ()),
    ("kernel/objects/kernel_object_runtime.c", "micros_kernel_object_runtime_detach_address_space", "micros_kernel_objects_validate", ()),
    ("kernel/objects/kernel_object_runtime.c", "micros_kernel_object_runtime_initialize", "micros_kernel_objects_validate", ()),
    ("kernel/objects/kernel_object_runtime.c", "micros_kernel_object_runtime_install_trap_stacks", "micros_kernel_objects_validate", ()),
    ("kernel/objects/kernel_object_runtime.c", "micros_kernel_object_runtime_set_test_emergency_stack", "micros_kernel_objects_validate", ("ifdef MICROS_BUILD_NESTED_TRAP_TEST",)),
    ("kernel/objects/kernel_object_runtime.c", "micros_kernel_object_runtime_validate", "micros_kernel_objects_validate", ()),
    ("kernel/objects/kernel_objects.c", "micros_kernel_objects_validate", "micros_scheduler_core_validate", ()),
    ("kernel/pm/pm_control_execution.c", "prepare_environment_is_valid", "micros_kernel_objects_validate", ()),
    ("kernel/pm/pm_control_execution.c", "prepare_environment_is_valid", "micros_scheduler_core_validate", ()),
    ("kernel/pm/pm_control_exit.c", "micros_pm_control_exit_begin_preflight", "micros_scheduler_core_validate", ()),
    ("kernel/pm/pm_control_exit.c", "validate_stopped_exit", "micros_scheduler_core_validate", ()),
    ("kernel/pm/pm_control_runtime_core.c", "micros_pm_control_runtime_validate_reservation", "micros_kernel_objects_validate", ()),
    ("kernel/pm/pm_control_validation.c", "micros_pm_control_state_validate_objects", "micros_kernel_objects_validate", ()),
    ("kernel/pm/pm_control_validation.c", "micros_pm_control_validate_prepared_shape", "micros_scheduler_core_validate", ()),
)


def audit_rows(sources):
    from tests.host.c_source_records import c_lexical_text, cpp_guards

    pattern = r"\b(micros_kernel_objects_validate|micros_kernel_object_runtime_validate|micros_scheduler_core_validate)\s*\("
    rows = []
    for path, text in sources.items():
        for name, record in function_records(text).items():
            for match in re.finditer(pattern, c_lexical_text(record.body)):
                rows.append((path, name, match[1], (*record.guards, *cpp_guards(record.body, match.start()))))
    return tuple(sorted(rows))


def measure_audit_boundaries(root, stage, *, current_sources=None):
    from collections.abc import Mapping
    from tests.host.source_snapshot import source_bytes
    from tests.host.native_observations import direct_calls

    if stage != "current":
        raise ValueError("OBJ01 caller census has an unknown source stage")
    root = str(Path(root).resolve())
    current_paths = {*AUDIT_SOURCES, "kernel/scheduler/scheduler_core_validate.c"}
    if current_sources is not None and (
        stage != "current" or not isinstance(current_sources, Mapping)
        or set(current_sources) != current_paths
        or any(type(raw) is not bytes for raw in current_sources.values())
    ):
        raise ValueError("OBJ01 current audit snapshot is incomplete or untyped")
    sources = {
        path: (source_bytes(Path(root), Path(root) / path) if current_sources is None else current_sources[path]).decode()
        for path in current_paths
    }
    observed = audit_rows({path: sources[path] for path in AUDIT_SOURCES})
    if observed != CURRENT_AUDIT_BOUNDARIES:
        raise ValueError("OBJ01 changed a current complete-audit boundary")
    body = function_records(sources["kernel/scheduler/scheduler_core_validate.c"])["micros_scheduler_core_validate"].body
    if "micros_kernel_objects_validate_base" not in direct_calls(body):
        raise ValueError("OBJ01 removed the complete scheduler-to-object-base edge")
    return {"stage": stage, "retained": observed, "migrated": (), "base_edge": 1}


def observe_current_source(path, text, channel):
    from tests.host import native_observations
    from tests.host.common_scheduler_return_source import validate_current_native_source

    validate_current_native_source(path, text)
    observed = native_observations.observe_source(
        Path(__file__).resolve().parents[2], "OBJ01", path, text, channel,
    )
    return observed if channel == "ordinary" else native_observations.PREFIX + observed


def project_observations(root, path, observed, channel, stage, *, original=None):
    from tests.host.source_snapshot import source_bytes

    if stage != "current":
        raise ValueError("object observation requires current source")
    if original is None:
        original = source_bytes(Path(root), Path(root) / path).decode()
    if observed != observe_current_source(path, original, channel):
        raise ValueError("current object observation changed or moved a measured primitive")
    return original


def panic_hart_source_admitted(root, path, text):
    from tests.host.common_scheduler_return_source import validate_current_native_source

    if path != "kernel/objects/kernel_object_hart.c":
        return False
    validate_current_native_source(path, text, root=root)
    return True
