"""Closed MEM03 source, recipe, case and primitive-work declarations."""

from pathlib import Path
import re

from tests.host.work_count_types import Stage


RECIPE_ADMISSION = {'base_cmake_sha256': '602a66808442168f23fcc28d2a769386af549ab6d1d8e869d55050dcc18084a2',
 'baseline_headers': 'fixed-runtime-reference',
 'current_headers': 'actual-current-source',
 'driver': 'tools/check_selected_ownership.py',
 'fixture_header': 'tests/host/selected_ownership_test.h',
 'fixture_sources': ('tests/host/selected_ownership_test.c', 'tests/work/work_counts.c'),
 'generation_dependencies': 'explicit-fixed-and-current-source-header-inputs;no-depfile',
 'generator': 'tools/generate_selected_ownership_fixture.py',
 'generator_output': 'build/host-debug/selected-ownership',
 'native_case': 'selected-ownership',
 'native_label': 'slow',
 'native_sanitizers': ('address', 'undefined'),
 'native_source_groups': ('MICROS_FRAME_OWNERSHIP_SOURCES',
                          'MICROS_KERNEL_OBJECT_CORE_SOURCES',
                          'MICROS_SCHEDULER_CORE_SOURCES',
                          'MICROS_ENDPOINT_CORE_SOURCES',
                          'MICROS_IPC_CORE_SOURCES'),
 'native_source_members': ('kernel/memory/frame_allocator.c',
                           'kernel/memory/bootstrap_memory.c',
                           'kernel/pm/pm_control_core.c',
                           'kernel/pm/pm_control_validation.c',
                           'kernel/pm/pm_control_runtime_core.c',
                           'kernel/vm/vm_handoff_core.c',
                           'kernel/memory/frame_ownership.c',
                           'kernel/memory/frame_ownership_validation.c',
                           'kernel/memory/frame_ownership_bootstrap.c',
                           'kernel/memory/frame_ownership_handoff.c',
                           'kernel/memory/frame_ownership_vm_claim.c',
                           'kernel/memory/frame_ownership_vm_freeze.c',
                           'kernel/memory/frame_ownership_vm_release.c',
                           'kernel/objects/kernel_objects.c',
                           'kernel/objects/kernel_object_state.c',
                           'kernel/objects/kernel_object_identity.c',
                           'kernel/objects/kernel_object_stack.c',
                           'kernel/objects/kernel_object_process.c',
                           'kernel/objects/kernel_object_thread.c',
                           'kernel/objects/kernel_object_context.c',
                           'kernel/objects/kernel_object_hart.c',
                           'kernel/objects/kernel_object_validation.c',
                           'kernel/scheduler/scheduler_core.c',
                           'kernel/scheduler/scheduler_core_queue.c',
                           'kernel/scheduler/scheduler_core_validate.c',
                           'kernel/scheduler/scheduler_core_policy.c',
                           'kernel/scheduler/scheduler_core_ipc.c',
                           'kernel/scheduler/scheduler_core_ipc_transition.c',
                           'kernel/scheduler/scheduler_core_return_plan.c',
                           'kernel/scheduler/scheduler_core_return.c',
                           'kernel/scheduler/scheduler_core_accounting.c',
                           'kernel/endpoint/endpoint.c',
                           'kernel/endpoint/endpoint_codec.c',
                           'kernel/endpoint/endpoint_identity.c',
                           'kernel/endpoint/endpoint_registry.c',
                           'kernel/endpoint/endpoint_privilege.c',
                           'kernel/endpoint/endpoint_completion_validation.c',
                           'kernel/endpoint/endpoint_queue_validation.c',
                           'kernel/endpoint/endpoint_object_validation.c',
                           'kernel/endpoint/endpoint_publication.c',
                           'kernel/endpoint/endpoint_close.c',
                           'kernel/ipc/ipc_common.c',
                           'kernel/ipc/ipc_queue.c',
                           'kernel/ipc/ipc_completion.c',
                           'kernel/ipc/ipc_tokens.c',
                           'kernel/ipc/ipc_notifications.c',
                           'kernel/ipc/ipc_deadlock.c',
                           'kernel/ipc/ipc_rendezvous.c',
                           'kernel/ipc/ipc_close.c',
                           'kernel/ipc/ipc_reply.c',
                           'kernel/ipc/ipc.c'),
 'native_standard': 'c17',
 'native_targets': ('selected_ownership_aggregate_test',
                    'selected_ownership_active_frame_test',
                    'selected_ownership_inactive_frame_test',
                    'selected_ownership_active_geometry_test',
                    'selected_ownership_inactive_geometry_test',
                    'selected_ownership_ordinary_test'),
 'native_tiers': ('test-unit',),
 'observed_definitions': ('MICROS_TEST_WORK_COUNTS=1', 'MICROS_TEST_SELECTED_OWNERSHIP_COUNTS=1'),
 'ordinary_observations': 'absent',
 'reference_cmake_sha256': '8527b1e866c2104797c7b9318723b3708aa010efff1a8dbd52dd9bcda7adb78a'}

STATE_WORK_CONTRACT = {'bitmap_bounds': {'active_frame': '16*q+32', 'active_geometry': '8*g+16', 'bitmap': '4*P+8*q+16'},
 'bitmap_cases': ((65, (0, 64)), (262144, (0, 63, 64))),
 'capacity': {'bitmap_words': 4096, 'frames': 262144, 'owner_kinds': 8, 'ranges': 96},
 'capture_labels': 'case IDs 1..99, increasing within each executable',
 'channels': ('aggregate', 'active-frame', 'inactive-frame', 'active-geometry', 'inactive-geometry'),
 'complete_calls': 'zero-memory-complete-diagnostics-on-every-nondiagnostic-case',
 'current_producer_offset': 256,
 'dense_frames': (1, 63, 64, 65, 257, 4096, 262143, 262144),
 'diagnostic_counts': {'active_geometry': '3*g',
                       'bitmap': '4096+(p%64!=0)',
                       'inactive_frame': '2*(P-p)',
                       'inactive_geometry': '2*(G-g)',
                       'portable_active_frame': '2*p',
                       'runtime_active_frame': '3*p'},
 'dimensions': (27, 14),
 'fragmented': {'input_order': 'reversed',
                'managed_frames': 96,
                'managed_ranges': 96,
                'pages': 11,
                'ranges': 16,
                'reserved_page_offsets': (1, 3, 5, 7, 9),
                'stride_pages': 12},
 'handoff_cases': {'managed_frames': (65, 262144),
                   'operations': ('stage-wired', 'complete-handoff'),
                   'setup_prefix': 65,
                   'transferable_retained_index': 1},
 'inactivity': 'zero-frame-and-geometry-visits-on-every-nondiagnostic-case',
 'interfaces': ('micros_frame_ownership_validate_metadata',
                'micros_frame_ownership_validate_active',
                'micros_frame_ownership_selected_record',
                'micros_frame_ownership_runtime_validate_metadata'),
 'kinds': ('COMPARISON',
           'RECORD_VISIT',
           'INDEX_PROBE',
           'BITMAP_WORD_LOAD',
           'COMPLETE_VALIDATOR_CALL',
           'RANGE_VISIT'),
 'observed_snapshots': 495,
 'ordinary_cases': 99,
 'owner_mask': 6268,
 'physical_base': 3221225472,
 'pm_bounds': {'active_frame': 0,
               'active_geometry': '8*g+16',
               'aggregate': '64*g+256',
               'bitmap': 0,
               'object_complete_calls': 1,
               'object_scheduler_work': 'equal-fixed-original-execution'},
 'pm_identity': {'process_slot': 2, 'profile': 'MICROS_PRIVILEGE_PROFILE_PM', 'service': 3},
 'point_bounds': {'active_frame': '16',
                  'active_geometry': '8*g+16',
                  'aggregate': '64*g+256',
                  'bitmap': '8'},
 'point_operations': ('bootstrap-allocate',
                      'lookup-last-free',
                      'bootstrap-release',
                      'prepare-one-wired',
                      'claim-last-free',
                      'runtime-release-empty-process',
                      'pm-idle-reservation',
                      'audit-portable',
                      'audit-runtime'),
 'preservation': ('complete-diagnostics-and-original-active-inactive-prefix-bodies',
                  'exact-allocator-cursor-defensive-rollback',
                  'valid-empty-transferable-and-process-release-sets',
                  'exact-live-generations-and-success-only-plans',
                  'original-MEM01-MEM02-work-and-semantic-oracles',
                  'no-new-runtime-state-capacity-or-target-workflow'),
 'producer_base': 5567941383722893312,
 'runtime_bounds': {'active_frame': '4*p+32',
                    'active_geometry': '8*g+16',
                    'bitmap': '8',
                    'object_complete_calls': 0,
                    'record': '16*p+64*g+512',
                    'scheduler_complete_calls': 0},
 'set_bounds': {'active_frame': '16*n+32',
                'active_geometry': '8*(g+1)*(n+1)',
                'aggregate': '64*(g+1)*(n+1)+16*n*n+256',
                'bitmap': '8*n+16'},
 'set_cases': {'data_counts': (1, 16),
               'data_indices': '0..q-1',
               'managed_frames': (65, 262144),
               'operations': ('freeze', 'release-transferable', 'release-process'),
               'table_count': 4,
               'table_indices': 'q..q+3'},
 'valid_cases': 99,
 'version': 1,
 'whole_active_bounds': {'active_frame': '32*p+64',
                         'handoff_bitmap': '16*w+64',
                         'record': '64*p+64*g+1024',
                         'stage_bitmap': '4*P+16*w+64'}}


COMPLETE_AUDITS = ("micros_frame_ownership_validate", "micros_frame_ownership_runtime_validate")


def _audit_calls(text, path, names):
    from tests.host.c_source_records import c_lexical_text, function_records

    calls = []
    for name, record in function_records(text).items():
        body = c_lexical_text(record.body)
        for callee in names:
            references = len(re.findall(r"\b" + callee + r"\b", body))
            count = len(re.findall(r"\b" + callee + r"\s*\(", body))
            if count != references:
                raise ValueError("MEM03 audit ledger contains an unadmitted indirect validator reference")
            if count:
                calls.append((path, name, callee, record.guards, count))
    return tuple(calls)


CURRENT_COMPLETE_BOUNDARIES = (
    ("kernel/boot/boot_foundation.c", "boot_initialize_memory", "micros_frame_ownership_runtime_validate", (), 1),
    ("kernel/boot/bootstrap_preparation.c", "validate_prepared_services", "micros_frame_ownership_runtime_validate", (), 1),
    ("kernel/boot/bootstrap_preparation_rollback.c", "bootstrap_preparation_rollback_preparation", "micros_frame_ownership_runtime_validate", (), 1),
    ("kernel/memory/address_space_validation.c", "validation_authority_is_valid", "micros_frame_ownership_validate", (), 1),
    ("kernel/memory/bootstrap_memory.c", "micros_bootstrap_memory_run_self_test", "micros_frame_ownership_runtime_validate", ("ifdef MICROS_BUILD_FRAME_ALLOCATOR_TEST",), 2),
    ("kernel/memory/bootstrap_memory.c", "micros_frame_ownership_runtime_initialize", "micros_frame_ownership_validate", (), 1),
    ("kernel/memory/bootstrap_memory.c", "micros_frame_ownership_runtime_validate", "micros_frame_ownership_validate", (), 1),
)
CURRENT_SELECTED_BOUNDARIES = (
    ("kernel/boot/bootstrap_runtime_validation.c", "micros_bootstrap_runtime_validate", "micros_frame_ownership_runtime_validate_metadata", (), 1),
    ("kernel/memory/bootstrap_memory.c", "micros_frame_ownership_runtime_complete_handoff", "micros_frame_ownership_runtime_validate_metadata", (), 1),
    ("kernel/memory/bootstrap_memory.c", "micros_frame_ownership_runtime_release_process", "micros_frame_ownership_runtime_validate_metadata", (), 1),
    ("kernel/memory/frame_ownership_bootstrap.c", "micros_frame_ownership_release_process_set", "micros_frame_ownership_validate_metadata", (), 1),
    ("kernel/memory/frame_ownership_handoff.c", "micros_frame_ownership_complete_handoff", "micros_frame_ownership_validate_active", (), 1),
    ("kernel/memory/frame_ownership_handoff.c", "micros_frame_ownership_prepare_wired_process_user_set", "micros_frame_ownership_validate_active", (), 1),
    ("kernel/memory/frame_ownership_validation.c", "micros_frame_ownership_vm_transition_preflight", "micros_frame_ownership_validate_metadata", (), 1),
    ("kernel/memory/user_address_space.c", "micros_user_address_space_create", "micros_frame_ownership_runtime_validate_metadata", (), 1),
    ("kernel/memory/user_address_space_handoff.c", "micros_user_address_space_complete_wired_handoff", "micros_frame_ownership_runtime_validate_metadata", (), 1),
    ("kernel/memory/user_address_space_handoff.c", "micros_user_address_space_prepare_wired_handoff", "micros_frame_ownership_runtime_validate_metadata", (), 1),
    ("kernel/memory/user_address_space_validation.c", "bind_validation_process", "micros_frame_ownership_runtime_validate_metadata", (), 1),
    ("kernel/memory/user_address_space_validation.c", "micros_user_address_space_load_selected_bindings", "micros_frame_ownership_runtime_validate_metadata", (), 1),
    ("kernel/memory/user_address_space_vm_freeze.c", "validate_vm_freeze_address_spaces", "micros_frame_ownership_runtime_validate_metadata", (), 1),
    ("kernel/memory/user_address_space_vm_scratch.c", "micros_user_address_space_prepare_vm_scratch_mapping", "micros_frame_ownership_runtime_validate_metadata", (), 1),
    ("kernel/pm/pm_control_runtime_core.c", "micros_pm_control_runtime_validate_reservation", "micros_frame_ownership_validate_metadata", (), 1),
    ("kernel/pm/pm_control_runtime_core.c", "micros_pm_control_runtime_validate_vm_reservation", "micros_frame_ownership_validate_metadata", (), 1),
    ("kernel/vm/vm_bootstrap.c", "micros_vm_boot_build", "micros_frame_ownership_validate_active", (), 1),
    ("kernel/vm/vm_bootstrap.c", "micros_vm_boot_validate_authority", "micros_frame_ownership_validate_active", (), 1),
    ("kernel/vm/vm_handoff_runtime.c", "micros_vm_handoff_runtime_validate", "micros_frame_ownership_runtime_validate_metadata", (), 1),
)


def measure_audit_boundaries(root, stage):
    from tests.host.source_snapshot import source_bytes

    root = Path(root).resolve()
    if stage is not Stage.GREEN:
        raise ValueError("MEM03 audit measurement requires current source")
    wanted = {row[:3] for row in CURRENT_SELECTED_BOUNDARIES}
    paths = tuple(sorted(
        path.relative_to(root).as_posix() for path in (root / "kernel").rglob("*.c")
        if not path.relative_to(root).as_posix().startswith("kernel/tests/")
    ))
    complete, replacements = [], []
    names = (*COMPLETE_AUDITS, "micros_frame_ownership_validate_metadata",
             "micros_frame_ownership_validate_active", "micros_frame_ownership_runtime_validate_metadata")
    for path in paths:
        text = source_bytes(root, root / path).decode()
        for row in _audit_calls(text, path, names):
            if row[2] in COMPLETE_AUDITS:
                complete.append(row)
            elif row[:3] in wanted:
                replacements.append(row)
    complete, replacements = tuple(sorted(complete)), tuple(sorted(replacements))
    if complete != CURRENT_COMPLETE_BOUNDARIES or replacements != CURRENT_SELECTED_BOUNDARIES:
        raise ValueError("MEM03 changed a required complete or selected validation boundary")
    return {"complete": complete, "selected": replacements}
