"""Closed MEM03 source, recipe, case and primitive-work declarations."""

import ast
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import re
import subprocess

from tests.host.evidence_authority_source import literal_value, regular_source, single_assignment
from tests.host.evidence_authority_types import AuthorityClass, Fact, digest
from tests.host.work_count_types import Stage


FAMILY = "selected-ownership-baseline"
SOURCE_PATH = "tests/host/selected_ownership_source.py"
AUTHORITY_PATH = "tests/host/selected_ownership_authority.py"


SOURCE_ROLES = {'base_membership_sha256': '9e0f30290ff491efa24db6019be7a5bd4d619cc267423c8fa8a482abb950e8a6',
 'candidate_paths': ('include/micros/frame_ownership.h',
                     'include/micros/frame_ownership_runtime.h',
                     'kernel/boot/bootstrap_runtime_validation.c',
                     'kernel/boot/bootstrap_syscall_release.c',
                     'kernel/memory/bootstrap_memory.c',
                     'kernel/memory/frame_ownership.c',
                     'kernel/memory/frame_ownership_bootstrap.c',
                     'kernel/memory/frame_ownership_handoff.c',
                     'kernel/memory/frame_ownership_internal.h',
                     'kernel/memory/frame_ownership_validation.c',
                     'kernel/memory/frame_ownership_vm_claim.c',
                     'kernel/memory/frame_ownership_vm_release.c',
                     'kernel/memory/user_address_space.c',
                     'kernel/memory/user_address_space_handoff.c',
                     'kernel/memory/user_address_space_validation.c',
                     'kernel/memory/user_address_space_vm_freeze.c',
                     'kernel/memory/user_address_space_vm_scratch.c',
                     'kernel/pm/pm_control_runtime_core.c',
                     'kernel/vm/vm_bootstrap.c',
                     'kernel/vm/vm_handoff_runtime.c',
                     'tests/host/bootstrap_allocation_source.py',
                     'tests/host/kernel_layout_source.py',
                     'tests/host/kernel_mapping_source.py',
                     'tests/host/memory_native_source_bindings.py',
                     'tests/host/test_frame_validation_hot_path.py',
                     'tests/host/test_kernel_directory_layout.py',
                     'tools/generate_kernel_mapping_fixture.py'),
 'fixed_adapters': ('tests/host/bootstrap_allocation_source.py',
                    'tests/host/kernel_layout_source.py',
                    'tests/host/kernel_mapping_source.py',
                    'tests/host/test_frame_validation_hot_path.py',
                    'tests/host/test_kernel_directory_layout.py',
                    'tools/generate_kernel_mapping_fixture.py'),
 'immutable_additions': ('tests/host/selected_ownership_test.c',
                         'tests/host/selected_ownership_test.h',
                         'tests/host/selected_ownership_source.py',
                         'tests/host/selected_ownership_evidence.py',
                         'tests/host/selected_ownership_authority.py',
                         'tests/host/test_selected_ownership.py',
                         'tools/generate_selected_ownership_fixture.py',
                         'tools/check_selected_ownership.py'),
 'production_paths': ('include/micros/frame_ownership.h',
                      'include/micros/frame_ownership_runtime.h',
                      'kernel/memory/frame_ownership_internal.h',
                      'kernel/memory/frame_ownership.c',
                      'kernel/memory/frame_ownership_validation.c',
                      'kernel/memory/frame_ownership_bootstrap.c',
                      'kernel/memory/frame_ownership_handoff.c',
                      'kernel/memory/frame_ownership_vm_claim.c',
                      'kernel/memory/frame_ownership_vm_release.c',
                      'kernel/memory/bootstrap_memory.c',
                      'kernel/memory/user_address_space.c',
                      'kernel/memory/user_address_space_validation.c',
                      'kernel/memory/user_address_space_handoff.c',
                      'kernel/memory/user_address_space_vm_scratch.c',
                      'kernel/memory/user_address_space_vm_freeze.c',
                      'kernel/boot/bootstrap_runtime_validation.c',
                      'kernel/boot/bootstrap_syscall_release.c',
                      'kernel/vm/vm_bootstrap.c',
                      'kernel/vm/vm_handoff_runtime.c',
                      'kernel/pm/pm_control_runtime_core.c'),
 'unchanged_candidate': 'tests/host/memory_native_source_bindings.py'}

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

SOURCE_PROJECTION = {'adapter_reference': 'dedf9a8d9d72f153519ea07150e50594a6618c6b',
 'admissions': ('fixed-original-native-source-and-header-bundle',
                'actual-current-functions-and-bounded-closed-helper-additions',
                'original-diagnostic-geometry-active-inactive-prefix-bodies-unchanged',
                'exact-passive-per-site-source-stage-and-channel-bindings',
                'six-frozen-legacy-adapter-inverses-before-actual-red',
                'literal-C4-declaration-byte-identical',
                'one-native-CTest-six-products-full-only-slow-tier',
                'exact40-key-retirement-and-all-prior-claim-delegation',
                'precise-complete-helper-tokens-after-exact-prefix-retirement',
                'checked-source-view-per-key-reader-adaptation',
                'fixed-validation-prerequisite-adapter-composition'),
 'complete_helper_tokens': {
     'validate_active_frames': 'd023646f0a6a0fe5bd3f5bfca640ae3ef72e5a3837a4ac00936bbdc1701de320',
     'inactive_state_is_zero': 'b5696c8cd0c3f62b187bcb771b68c7901a598b347a71eb202a10a8fc2072ee5a',
 },
 'complete_prefix_keys': (
     'tests/host/test_frame_validation_hot_path.py:data-entry:ACTIVE_TOKENS_SHA256',
     'tests/host/test_frame_validation_hot_path.py:data-entry:INACTIVE_TOKENS_SHA256',
 ),
 'mutable_functions': {'kernel/boot/bootstrap_runtime_validation.c': ('micros_bootstrap_runtime_validate',),
                       'kernel/boot/bootstrap_syscall_release.c': ('validate_console_begin_runtime',),
                       'kernel/memory/bootstrap_memory.c': ('micros_frame_ownership_runtime_complete_handoff',
                                                            'micros_frame_ownership_runtime_release_process'),
                       'kernel/memory/frame_ownership.c': (),
                       'kernel/memory/frame_ownership_bootstrap.c': ('micros_frame_ownership_allocate',
                                                                     'micros_frame_ownership_release',
                                                                     'collect_process_release_summary',
                                                                     'micros_frame_ownership_release_process_set'),
                       'kernel/memory/frame_ownership_handoff.c': ('micros_frame_ownership_prepare_handoff',
                                                                   'micros_frame_ownership_prepare_wired_process_user_set',
                                                                   'micros_frame_ownership_complete_handoff'),
                       'kernel/memory/frame_ownership_validation.c': ('micros_frame_ownership_lookup',
                                                                      'micros_frame_ownership_count_process',
                                                                      'micros_frame_ownership_vm_transition_preflight',
                                                                      'resolve_vm_data_frames',
                                                                      'resolve_vm_table_frames'),
                       'kernel/memory/frame_ownership_vm_claim.c': ('micros_frame_ownership_vm_claim_preflight',),
                       'kernel/memory/frame_ownership_vm_release.c': ('micros_frame_ownership_vm_release_transferable_preflight',),
                       'kernel/memory/user_address_space.c': ('micros_user_address_space_create',),
                       'kernel/memory/user_address_space_handoff.c': ('micros_user_address_space_prepare_wired_handoff',
                                                                      'micros_user_address_space_complete_wired_handoff'),
                       'kernel/memory/user_address_space_validation.c': ('load_validation_context',),
                       'kernel/memory/user_address_space_vm_freeze.c': ('validate_vm_freeze_address_spaces',),
                       'kernel/memory/user_address_space_vm_scratch.c': ('micros_user_address_space_prepare_vm_scratch_mapping',),
                       'kernel/pm/pm_control_runtime_core.c': ('micros_pm_control_runtime_validate_reservation',),
                       'kernel/vm/vm_bootstrap.c': ('micros_vm_boot_build',
                                                    'micros_vm_boot_validate_authority'),
                       'kernel/vm/vm_handoff_runtime.c': ('micros_vm_handoff_runtime_validate',)},
 'native_bindings': ('actual-bootstrap-allocator-ledger-and-ready-state',
                     'riscv_irq_is_enabled-to-explicit-native-boolean',
                     'canonical-getter-for-real-portable-object-registry'),
 'native_bootstrap_closure': ('micros_bootstrap_frame_allocator',
                              'ownership_mutation_is_allowed',
                              'owner_is_process_bound',
                              'validate_process_owners',
                              'micros_frame_ownership_runtime_initialize',
                              'micros_frame_ownership_runtime_ledger',
                              'micros_frame_ownership_runtime_validate',
                              'micros_frame_ownership_runtime_release_process',
                              'micros_frame_ownership_runtime_complete_handoff'),
 'new_helpers': {'kernel/memory/bootstrap_memory.c': ('micros_frame_ownership_runtime_validate_metadata',
                                                      'validate_active_process_owners'),
                 'kernel/memory/frame_ownership.c': ('micros_frame_ownership_active_geometry_matches',),
                 'kernel/memory/frame_ownership_validation.c': ('validate_metadata_header',
                                                                'micros_frame_ownership_validate_metadata',
                                                                'micros_frame_ownership_selected_record',
                                                                'micros_frame_ownership_validate_active')},
 'original_function_policy': 'derive complete signatures/bodies/CPP ownership only from SHA-bound fixed '
                             'Git sources',
 'retained_source_sha256': {'include/micros/frame_ownership.h': '50a41916f40b68ee7e113dc4f6916511743167f3b94f298d5fa0a25308cf5002',
                            'include/micros/frame_ownership_runtime.h': '75f5e9d9f1ec337482bbf06c99841d3f6bd494f625eb59a40cc02d99eb0a3db7',
                            'kernel/boot/bootstrap_runtime_validation.c': '48cf29d64eb6d66a3708cfbc328ce8b22c623b086570b3b455bcde2e8e6d382c',
                            'kernel/boot/bootstrap_syscall_release.c': '2a8492205f8f29088af76a6bbd662500199ccf748100f300102793b1846e0657',
                            'kernel/memory/bootstrap_memory.c': '6f79e3229c224a929f0f385af24324c823bbc2d8a3a471aa8de1f99e8eede33b',
                            'kernel/memory/frame_ownership.c': '3392bd46752b5a0e1499ba2b88437e5f018a666d07d6a00b13dcd3fc8966ab7b',
                            'kernel/memory/frame_ownership_bootstrap.c': '8b84ad7ab8fa0b61bbe3d50841938ba17626419c757bff9049c4f55b4d6df26d',
                            'kernel/memory/frame_ownership_handoff.c': '155d62def0dcca4dabaff43107fdbe6766c9648d2e01b64db8e6c55eb4f3fbc9',
                            'kernel/memory/frame_ownership_internal.h': '8108098cb4b56b933b9c9aaf01bab2ccecdc3c89843fa3a96feba984eac112b8',
                            'kernel/memory/frame_ownership_validation.c': '8682da131521ec54a6a83fb97ac88a74711987226d1823739e4373a88abb1aae',
                            'kernel/memory/frame_ownership_vm_claim.c': '1485508df515e14ef783766915faf520e45f9299b8346bd53c08a7a32b8b96ec',
                            'kernel/memory/frame_ownership_vm_release.c': '752a39d2f7eb8b729a6947b5006fffad51cf9e8222516f3c636fe090197b5054',
                            'kernel/memory/user_address_space.c': '64a1f7ec08b7135a9a037dfd92eafc09e3a0e43d13e19b9b2e56a313184d1f8b',
                            'kernel/memory/user_address_space_handoff.c': '02f221bc6764f77f36ff15b0e4ecdd60d73f438699c85525580b092595686b76',
                            'kernel/memory/user_address_space_validation.c': '09665c80bc134868468be5c36011765fbf248c8564003c43116a174cd4a7c3fe',
                            'kernel/memory/user_address_space_vm_freeze.c': '3c06f5791e5c4d0186c2b22099568ee422cbc1c19bd98516af512dc3850c1fa0',
                            'kernel/memory/user_address_space_vm_scratch.c': '304eeeca8fe1468a04b24289ea4dbe1319db1c1ece9447f2cc4390b1641aa9e3',
                            'kernel/pm/pm_control_runtime_core.c': '97e1226d4621acb0496fa047cdeb98098f0fe283d80b5dc786f7ea4f9bf4c2a4',
                            'kernel/vm/vm_bootstrap.c': 'bd2c7b19a426d0484d7f3ec20b9d275dfcd3ca87e8e308a06c6fa5185541a971',
                            'kernel/vm/vm_handoff_runtime.c': '01683ea29ef2291433f26db35cda0b924f425f4534106215fb63b7c5add3239e',
                            'tests/host/bootstrap_allocation_source.py': '53f3b2dcfa4b1d7ccb55c9ac4ecee8c9ed6eaf325c7f09c07a75c8ea96d1a3a4',
                            'tests/host/kernel_layout_source.py': 'b1a2c575c45e160af9506f01c80bb856633b70554f2dc5305d680abc96392efb',
                            'tests/host/kernel_mapping_source.py': 'db51d36eaacf8e5fdf4bf8930d63a50bb2c8a4e517c5d08e84f7980f4dcb4b49',
                            'tests/host/memory_native_source_bindings.py': '4a90c7ec808b4fa3edd4606b83b250b0e9ddba4cbde09de03df65dea6ec35202',
                            'tests/host/test_frame_validation_hot_path.py': '44715080c773eaa7655b61e17acb2c8415f02570402908fa0a672d7eb260b0ee',
                            'tests/host/test_kernel_directory_layout.py': 'bc813045eac3de89f4f45ebb94e746752a1349e145995433c9f99ea3fa5be72d',
                            'tools/generate_kernel_mapping_fixture.py': 'abef5c1dc0ce28c398a8f45d1d557f677faf3a4be432d7b2796839c221b77185'},
 'retirement_keys': ('tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/bootstrap_runtime_validation.c/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/bootstrap_syscall_release.c/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership_handoff.c/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership_internal.h/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership_vm_claim.c/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership_vm_release.c/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/pm_control_runtime_core.c/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space.c/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_handoff.c/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_validation.c/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_vm_freeze.c/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_vm_scratch.c/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/vm_bootstrap.c/2',
                     'tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/vm_handoff_runtime.c/2',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/definitions/kernel/memory/bootstrap_memory.c',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/definitions/kernel/memory/frame_ownership.c',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/definitions/kernel/memory/frame_ownership_validation.c',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/bootstrap_memory.c/micros_frame_ownership_runtime_complete_handoff',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/bootstrap_memory.c/micros_frame_ownership_runtime_release_process',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/frame_ownership_bootstrap.c/collect_process_release_summary',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/frame_ownership_bootstrap.c/micros_frame_ownership_allocate',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/frame_ownership_bootstrap.c/micros_frame_ownership_release',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/frame_ownership_bootstrap.c/micros_frame_ownership_release_process_set',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_count_process',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_lookup',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_vm_transition_preflight',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/frame_ownership_validation.c/resolve_vm_data_frames',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM01/function/kernel/memory/frame_ownership_validation.c/resolve_vm_table_frames',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/include/micros/frame_ownership.h',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/include/micros/frame_ownership_runtime.h',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/memory/bootstrap_memory.c',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/memory/frame_ownership.c',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/memory/frame_ownership_bootstrap.c',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/memory/frame_ownership_handoff.c',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/memory/frame_ownership_internal.h',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/memory/frame_ownership_validation.c',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/memory/frame_ownership_vm_claim.c',
                     'tests/host/memory_native_source_bindings.py:native-source-admission:MEM02/native-input/kernel/memory/frame_ownership_vm_release.c',
                     'tests/host/test_frame_validation_hot_path.py:data-entry:ACTIVE_TOKENS_SHA256',
                     'tests/host/test_frame_validation_hot_path.py:data-entry:INACTIVE_TOKENS_SHA256'),
 'source_reference': '8927f9956eb270962517b19715a12bc93a5fa466',
 'validation_base_source_sha256': {
     'tests/host/bootstrap_allocation_source.py': '53f3b2dcfa4b1d7ccb55c9ac4ecee8c9ed6eaf325c7f09c07a75c8ea96d1a3a4',
     'tests/host/kernel_layout_source.py': '490cc2eacfed49c61588484e361a10df152ac22fab341debc2cd42c7f393ec03',
     'tests/host/kernel_mapping_source.py': 'a67ccc2e56a19b8f44eb944558f13f67ce461ceb254eee41d16d196051cb116a',
     'tests/host/test_frame_validation_hot_path.py': '44715080c773eaa7655b61e17acb2c8415f02570402908fa0a672d7eb260b0ee',
     'tests/host/test_kernel_directory_layout.py': '6f5817df896b2de4ff411d66a987ad063c4123fdc4f2bdda7fcfb68da7eadca4',
     'tools/generate_kernel_mapping_fixture.py': '6c96ab56f81e4eb4c54fecdb67192a7d2c28679d4b320c3d020217019e15b0bb',
 }}


def _paths(value, count):
    return (
        type(value) is tuple and len(value) == count
        and all(
            type(path) is str and path and not Path(path).is_absolute()
            and "\\" not in path and ".." not in Path(path).parts
            and Path(path).as_posix() == path
            for path in value
        )
        and len(set(value)) == len(value)
    )


def read_declarations(root):
    values = {}
    for path, names in (
        (AUTHORITY_PATH, ("DESIGN_PINS", "ORACLE_RULES")),
        (SOURCE_PATH, ("SOURCE_ROLES", "RECIPE_ADMISSION", "STATE_WORK_CONTRACT", "SOURCE_PROJECTION")),
    ):
        tree = ast.parse(regular_source(Path(root), path).read_text(encoding="utf-8"))
        values.update((name, literal_value(single_assignment(tree, name).value)) for name in names)
    validate_declarations(values)
    return values


def validate_declarations(values):
    if type(values) is not dict or set(values) != {
        "DESIGN_PINS", "ORACLE_RULES", "SOURCE_ROLES", "RECIPE_ADMISSION",
        "STATE_WORK_CONTRACT", "SOURCE_PROJECTION",
    } or any(type(values[name]) is not dict for name in values if name != "ORACLE_RULES"):
        raise ValueError("MEM03 has incomplete literal authority declarations")
    roles, projection = values["SOURCE_ROLES"], values["SOURCE_PROJECTION"]
    if (
        set(roles) != {
            "base_membership_sha256", "candidate_paths", "production_paths",
            "immutable_additions", "unchanged_candidate", "fixed_adapters",
        }
        or type(roles["base_membership_sha256"]) is not str
        or re.fullmatch(r"[0-9a-f]{64}", roles["base_membership_sha256"]) is None
        or not _paths(roles["candidate_paths"], 27)
        or not _paths(roles["production_paths"], 20)
        or not _paths(roles["immutable_additions"], 8)
        or not _paths(roles["fixed_adapters"], 6)
        or roles["unchanged_candidate"] != "tests/host/memory_native_source_bindings.py"
        or set(roles["candidate_paths"]) != {
            *roles["production_paths"], *roles["fixed_adapters"], roles["unchanged_candidate"],
        }
        or set(roles["candidate_paths"]) & set(roles["immutable_additions"])
        or not _paths(projection.get("retirement_keys"), 40)
        or projection.get("source_reference") != values["DESIGN_PINS"].get("runtime_reference")
        or projection.get("adapter_reference") != values["DESIGN_PINS"].get("implementation_base_commit")
        or type(projection.get("validation_base_source_sha256")) is not dict
        or set(projection["validation_base_source_sha256"]) != set(roles["fixed_adapters"])
        or any(type(value) is not str or re.fullmatch(r"[0-9a-f]{64}", value) is None
               for value in projection["validation_base_source_sha256"].values())
    ):
        raise ValueError("MEM03 source membership or reference is malformed")
    rules = values["ORACLE_RULES"]
    if type(rules) is not tuple or len(rules) != 3:
        raise ValueError("MEM03 requires three independently owned work oracles")
    ids, tests, positions = set(), set(), set()
    for row in rules:
        if (
            type(row) is not tuple or len(row) != 6
            or any(type(item) is not str or not item for item in row[:5])
            or row[0] in ids or row[1] in tests or row[3] != "ADR-0103"
            or type(row[5]) is not tuple or not row[5] or len(set(row[5])) != len(row[5])
            or any(type(index) is not int or index < 0 or index >= 40 for index in row[5])
            or positions & set(row[5])
        ):
            raise ValueError("MEM03 oracle scope is malformed or duplicated")
        ids.add(row[0])
        tests.add(row[1])
        positions.update(row[5])
    if positions != set(range(40)) or tuple(len(row[5]) for row in rules) != (25, 5, 10):
        raise ValueError("MEM03 replacement oracles omit their exact retirement partition")
    helpers = projection.get("complete_helper_tokens")
    if (
        type(helpers) is not dict or set(helpers) != {"validate_active_frames", "inactive_state_is_zero"}
        or any(type(value) is not str or re.fullmatch(r"[0-9a-f]{64}", value) is None for value in helpers.values())
        or projection.get("complete_prefix_keys") != projection["retirement_keys"][-2:]
    ):
        raise ValueError("MEM03 complete-helper token authority is malformed")


def declaration_facts(root, owner):
    if owner not in (AUTHORITY_PATH, SOURCE_PATH):
        raise ValueError("MEM03 has no declaration in this source owner")
    values = read_declarations(root)
    pins, roles = values["DESIGN_PINS"], values["SOURCE_ROLES"]
    recipe, projection = values["RECIPE_ADMISSION"], values["SOURCE_PROJECTION"]
    references = tuple(pins[key] for key in (
        "design_document", "adr_document", "research_document",
        "correction_adr_document", "correction_research_document",
        "validation_adr_document", "validation_document",
    ))
    consumers = tuple(f"B{index:02d}" for index in range(1, 13)) + ("B14",)

    def fact(kind, scope, classification, payloads, value):
        return Fact(
            owner + ":" + kind + ":" + scope, FAMILY, owner, kind, scope, classification,
            "B02", consumers, tuple(sorted({owner, *references, *payloads})), digest(value),
        )

    if owner == AUTHORITY_PATH:
        return (
            fact("design-anchor", "MEM03", AuthorityClass.TRANSPORT_IDENTITY, (), pins),
            *(fact("baseline-oracle", row[0], AuthorityClass.BASELINE_ORACLE,
                   roles["immutable_additions"], row) for row in values["ORACLE_RULES"]),
        )
    return (
        fact("source-membership", "MEM03", AuthorityClass.PERMANENT_INVARIANT,
             (*roles["candidate_paths"], *roles["immutable_additions"]), roles),
        fact("artifact-contract", "MEM03-recipes", AuthorityClass.ARTIFACT_CONTRACT,
             ("CMakeLists.txt", "CMakePresets.json", "include/micros/work_observation.h",
              "tests/work/work_counts.h", *recipe["fixture_sources"], *recipe["native_source_members"],
              recipe["generator"], recipe["driver"]), recipe),
        fact("state-work-contract", "MEM03", AuthorityClass.PERMANENT_INVARIANT,
             (*roles["production_paths"], *roles["immutable_additions"][:2],
              "tests/host/selected_ownership_evidence.py", "include/micros/work_observation.h",
              "tests/work/work_counts.h"), values["STATE_WORK_CONTRACT"]),
        fact("source-projection", "MEM03", AuthorityClass.TRANSPORT_IDENTITY,
             (*roles["candidate_paths"], "tests/host/c_source_records.py",
              "tests/host/work_count_evidence.py", recipe["generator"]), projection),
    )


@lru_cache(maxsize=256)
def baseline_source(root, path):
    allowed = {
        *SOURCE_ROLES["candidate_paths"], *RECIPE_ADMISSION["native_source_members"],
        *original_audit_paths(root), "tests/host/test_kernel_bootstrap_source_layout.py",
    }
    if path not in allowed:
        raise ValueError("MEM03 has no fixed source binding for this path")
    result = subprocess.run(
        ("git", "show", SOURCE_PROJECTION["source_reference"] + ":" + path),
        cwd=root, stdin=subprocess.DEVNULL, capture_output=True, check=False, timeout=30,
    )
    expected = SOURCE_PROJECTION["retained_source_sha256"].get(path)
    if result.returncode or expected is not None and hashlib.sha256(result.stdout).hexdigest() != expected:
        raise ValueError("MEM03 source differs from its fixed independent reference")
    return result.stdout.decode("utf-8")


def _project_validation_prerequisite(path, text):
    if path in ("tests/host/kernel_layout_source.py", "tests/host/test_kernel_directory_layout.py"):
        from tests.host.work_count_recipe import project_feedback_reader

        return project_feedback_reader(path, text)
    if path == "tests/host/kernel_mapping_source.py":
        changes = (
            (
                "    from tests.host.work_count_recipe import project_feedback_reader\n\n"
                "    text = project_feedback_reader(path, text)\n",
                "",
            ),
            (
                "    from tests.host.work_count_recipe import project_host_test_commands\n\n"
                "    text = project_host_test_commands(text)\n",
                "",
            ),
        )
    elif path == "tools/generate_kernel_mapping_fixture.py":
        changes = (
            ("from tests.host.evidence_authority_scope import checked_source_view\n", ""),
            (
                "        with checked_source_view(ROOT):\n"
                "            status = main()\n"
                "        raise SystemExit(status)\n",
                "        raise SystemExit(main())\n",
            ),
            (
                "    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:\n",
                "    except (OSError, ValueError, AssertionError, subprocess.SubprocessError) as error:\n",
            ),
        )
    else:
        return text
    for current, original in changes:
        if text.count(current) != 1:
            raise ValueError("MEM03 validation prerequisite has an unreviewed adapter form")
        text = text.replace(current, original, 1)
    return text


@lru_cache(maxsize=32)
def baseline_adapter_source(root, path):
    if path not in SOURCE_ROLES["fixed_adapters"]:
        raise ValueError("MEM03 has no validation-base adapter in this owner")
    data = subprocess.check_output(
        ("git", "show", SOURCE_PROJECTION["adapter_reference"] + ":" + path),
        cwd=root, stdin=subprocess.DEVNULL, timeout=30,
    )
    if hashlib.sha256(data).hexdigest() != SOURCE_PROJECTION["validation_base_source_sha256"][path]:
        raise ValueError("MEM03 validation-base adapter identity changed")
    text = data.decode("utf-8")
    if _project_validation_prerequisite(path, text) != baseline_source(root, path):
        raise ValueError("MEM03 validation prerequisite changed unrelated fixed-source policy")
    return text


def _function_calls(body):
    from tests.host.c_source_records import tokens
    from tests.host.test_frame_ownership_source_layout import c_lexical_text

    text = c_lexical_text(body)
    lexical = tokens(text)
    casts = {"size_t", "uintptr_t", "uint64_t", "uint32_t", "uint16_t", "uint8_t", "unsigned", "int", "bool", "void"}
    for index, (left, right) in enumerate(zip(lexical, lexical[1:])):
        if left == ")" and right == "(" and not (
            index >= 2 and lexical[index - 2] == "(" and lexical[index - 1] in casts
        ):
            raise ValueError("MEM03 does not admit an indirect call in its measured closure")
    return set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", text)) - {
        "if", "while", "for", "switch", "return", "sizeof", "_Alignof", "UINT64_C", "UINT32_C", "UINT16_C",
    }


def _helper_contracts():
    ledger = "const struct micros_frame_ownership *ownership"
    objects = "const struct micros_kernel_objects *objects"
    error = "enum micros_frame_ownership_error "
    return {
        "micros_frame_ownership_active_geometry_matches": (
            "bool micros_frame_ownership_active_geometry_matches(" + ledger + ")",
            {"micros_frame_ownership_require_initialized"},
        ),
        "validate_metadata_header": (
            "static " + error + "validate_metadata_header(" + ledger + ")",
            {"micros_frame_ownership_require_initialized"},
        ),
        "micros_frame_ownership_validate_metadata": (
            error + "micros_frame_ownership_validate_metadata(" + ledger + ")",
            {"validate_metadata_header", "micros_frame_ownership_active_geometry_matches"},
        ),
        "micros_frame_ownership_selected_record": (
            error + "micros_frame_ownership_selected_record("
            + ledger + ", uint64_t frame_index, struct micros_frame_owner *owner)",
            {"validate_metadata_header", "micros_frame_owner_is_valid",
             "micros_frame_ownership_allocator_bit_is_set", "micros_frame_ownership_handoff_target_is_valid"},
        ),
        "micros_frame_ownership_validate_active": (
            error + "micros_frame_ownership_validate_active(" + ledger + ")",
            {"micros_frame_ownership_validate_metadata", "allocated_prefix_is_valid", "validate_active_frames"},
        ),
        "micros_frame_ownership_runtime_validate_metadata": (
            error + "micros_frame_ownership_runtime_validate_metadata(" + objects + ")",
            {"riscv_irq_is_enabled", "micros_frame_ownership_validate_metadata",
             "micros_kernel_object_runtime_registry"},
        ),
        "validate_active_process_owners": (
            "static " + error + "validate_active_process_owners(" + objects + ")",
            {"owner_is_process_bound", "micros_process_resolve"},
        ),
    }


def header_additions(path):
    ledger = "const struct micros_frame_ownership *ownership"
    error = "enum micros_frame_ownership_error "
    if path == "include/micros/frame_ownership.h":
        return (
            error + "micros_frame_ownership_validate_metadata(" + ledger + ");",
            error + "micros_frame_ownership_validate_active(" + ledger + ");",
        )
    if path == "include/micros/frame_ownership_runtime.h":
        return (
            error + "micros_frame_ownership_runtime_validate_metadata("
            "const struct micros_kernel_objects *objects);",
        )
    if path == "kernel/memory/frame_ownership_internal.h":
        return (
            "bool micros_frame_ownership_active_geometry_matches(" + ledger + ");",
            error + "micros_frame_ownership_selected_record("
            + ledger + ", uint64_t frame_index, struct micros_frame_owner *owner);",
        )
    raise ValueError("MEM03 has no header extension in this owner")


def _source_skeleton(text, records):
    from tests.host.c_source_records import compact

    for record in records.values():
        fragment = record.signature + "{" + record.body + "}"
        if text.count(fragment) != 1:
            raise ValueError("MEM03 source has ambiguous function boundaries")
        text = text.replace(fragment, "", 1)
    return compact(text)


def _current_calls(name, before):
    calls = _function_calls(before)
    metadata = "micros_frame_ownership_validate_metadata"
    selected = "micros_frame_ownership_selected_record"
    active = "micros_frame_ownership_validate_active"
    runtime = "micros_frame_ownership_runtime_validate_metadata"
    if name == "micros_frame_ownership_lookup":
        return {metadata, selected, "micros_frame_ownership_physical_address_to_index"}
    if "micros_frame_ownership_geometry_matches" in calls:
        calls.remove("micros_frame_ownership_geometry_matches")
        calls.add(metadata)
    if "micros_frame_ownership_runtime_validate" in calls:
        calls.remove("micros_frame_ownership_runtime_validate")
        calls.add(runtime)
    if "micros_frame_ownership_validate" in calls:
        calls.remove("micros_frame_ownership_validate")
        calls.add(active if name in {
            "micros_frame_ownership_prepare_wired_process_user_set", "micros_frame_ownership_complete_handoff",
            "micros_vm_boot_build", "micros_vm_boot_validate_authority",
        } else metadata)
    if name in {
        "micros_frame_ownership_count_process", "micros_frame_ownership_release",
        "collect_process_release_summary", "micros_frame_ownership_prepare_handoff",
        "micros_frame_ownership_vm_claim_preflight", "micros_frame_ownership_vm_release_transferable_preflight",
        "resolve_vm_data_frames", "resolve_vm_table_frames",
    }:
        calls.add(selected)
    if name == "micros_frame_ownership_runtime_complete_handoff":
        calls.add("validate_active_process_owners")
    return calls


def validate_retained_source(root, path, text, stage):
    from tests.host.c_source_records import compact, function_records
    from tests.host.selected_address_space_source import native_assertion_admission as next_admission

    retired = next_admission(root, "MEM03", path, text, stage)

    if stage not in (Stage.RED, Stage.GREEN) or type(text) is not str:
        raise ValueError("MEM03 source admission has an unknown stage or representation")
    original = baseline_source(str(root), path)
    if stage is Stage.RED or path not in SOURCE_ROLES["production_paths"]:
        if text != original:
            if "MEM03/native-input/" + path in retired:
                return
            raise ValueError("MEM03 fixed or noncandidate source changed: " + path)
        return
    if path.endswith(".h"):
        if "MEM03/header/" + path in retired:
            return
        if original.count("\n#endif") != 1:
            raise ValueError("MEM03 header has an unexpected original boundary")
        expected = original.replace("\n#endif", "\n" + "\n".join(header_additions(path)) + "\n\n#endif", 1)
        if compact(text) != compact(expected):
            raise ValueError("MEM03 header changed beyond its exact declarations")
        return
    before, after = function_records(original), function_records(text)
    additions = SOURCE_PROJECTION["new_helpers"].get(path, ())
    if "MEM03/definitions/" + path not in retired and set(after) != set(before) | set(additions):
        raise ValueError("MEM03 current source has missing or unadmitted definitions")
    mutable = SOURCE_PROJECTION["mutable_functions"][path]
    for name, old in before.items():
        if "MEM03/function/" + path + "/" + name in retired:
            continue
        current = after[name]
        if compact(current.signature) != compact(old.signature) or current.guards != old.guards:
            raise ValueError("MEM03 changed an original signature or CPP owner: " + name)
        if name not in mutable and current.body != old.body:
            raise ValueError("MEM03 changed a retained diagnostic or unrelated body: " + name)
        if name in mutable and _function_calls(current.body) != _current_calls(name, old.body):
            raise ValueError("MEM03 operation escaped its exact admitted callees: " + name)
    contracts = _helper_contracts()
    for name in additions:
        if "MEM03/function/" + path + "/" + name in retired:
            continue
        current = after[name]
        signature, calls = contracts[name]
        if compact(current.signature) != compact(signature) or current.guards or _function_calls(current.body) != calls:
            raise ValueError("MEM03 helper escaped its exact signature/call admission: " + name)
    if "MEM03/skeleton/" + path not in retired and _source_skeleton(text, after) != _source_skeleton(original, before):
        raise ValueError("MEM03 changed unrelated include, type or storage ownership")


def assert_complete_helper_tokens(preprocessed, name):
    from tests.host.c_source_records import function_records
    from tests.host.test_frame_ownership_source_layout import c_lexical_text, tokens

    expected = SOURCE_PROJECTION["complete_helper_tokens"]
    if type(preprocessed) is not str or name not in expected:
        raise AssertionError("unadmitted complete-validator helper")
    try:
        record = function_records(preprocessed)[name]
    except (KeyError, ValueError) as error:
        raise AssertionError("missing or ambiguous complete-validator helper") from error
    complete = record.signature + "{" + record.body + "}"
    data = json.dumps(tokens(c_lexical_text(complete)), separators=(",", ":")).encode()
    observed = hashlib.sha256(data).hexdigest()
    if observed != expected[name]:
        raise AssertionError("validator work shape differs from reviewed identity")
    return observed


OBSERVATION_PREFIX = (
    '#if !defined(MICROS_TEST_SELECTED_OWNERSHIP_COUNTS) || MICROS_TEST_SELECTED_OWNERSHIP_COUNTS != 1\n'
    '#error "MEM03 observations require the private native evidence target"\n'
    '#endif\n'
    '#include "micros/work_observation.h"\n'
)

ORIGINAL_OBSERVATION_DIGEST = "0aa7b21308f096676ebfa2bb0e654e66f36773c70a98731916db5b702e4da508"
CURRENT_OBSERVATION_DIGEST = "dd940413c0de49a913827ceebee0cc0f6088669ffac343c184efca0dc0bbe71b"
REPLACED_OWNER_READS = (
    "site-0025", "site-0050", "site-0054", "site-0063",
    "site-0070", "site-0083", "site-0103", "site-0123",
)
OBSERVED_MEMORY_PATHS = (
    "kernel/memory/frame_allocator.c", "kernel/memory/frame_ownership.c",
    "kernel/memory/frame_ownership_validation.c", "kernel/memory/frame_ownership_bootstrap.c",
    "kernel/memory/frame_ownership_handoff.c", "kernel/memory/frame_ownership_vm_claim.c",
    "kernel/memory/frame_ownership_vm_freeze.c", "kernel/memory/frame_ownership_vm_release.c",
    "kernel/memory/bootstrap_memory.c",
)
OBSERVED_OBJECT_PATHS = (
    "kernel/objects/kernel_objects.c", "kernel/objects/kernel_object_validation.c",
    "kernel/objects/kernel_object_state.c", "kernel/objects/kernel_object_identity.c",
    "kernel/objects/kernel_object_stack.c", "kernel/objects/kernel_object_process.c",
    "kernel/scheduler/scheduler_core.c", "kernel/scheduler/scheduler_core_validate.c",
)
OBSERVED_MEMORY_FUNCTIONS = {
    "kernel/memory/frame_allocator.c": ("micros_frame_allocator_allocate", "micros_frame_allocator_release"),
    "kernel/memory/frame_ownership.c": (
        "allocator_geometry_is_valid", "micros_frame_ownership_geometry_matches",
        "micros_frame_ownership_physical_address_to_index", "micros_frame_ownership_allocator_bit_is_set",
        "micros_frame_ownership_allocator_mark_allocated", "micros_frame_ownership_allocator_mark_free",
    ),
}
OBSERVED_FIELDS = {
    "owners": "RECORD_VISIT", "handoff_targets": "RECORD_VISIT", "owner_counts": "RECORD_VISIT",
    "managed_ranges": "RANGE_VISIT", "allocated_bitmap": "BITMAP_WORD_LOAD",
    "release_bitmap": "BITMAP_WORD_LOAD", "selected_bitmap": "BITMAP_WORD_LOAD",
    "frame_indices": "INDEX_PROBE", "data_frame_indices": "INDEX_PROBE",
    "table_frame_indices": "INDEX_PROBE", "physical_addresses": "INDEX_PROBE",
    "data_physical_addresses": "INDEX_PROBE", "table_physical_addresses": "INDEX_PROBE",
}


def _matching_tokens(values, opening):
    pairs = {"(": ")", "[": "]", "{": "}"}
    stack = []
    for index in range(opening, len(values)):
        if values[index] in pairs:
            stack.append(pairs[values[index]])
        elif stack and values[index] == stack[-1]:
            stack.pop()
            if not stack:
                return index
    raise ValueError("MEM03 primitive has an unbalanced delimiter")


def _region_header(values, opening):
    if opening and values[opening - 1] == ")":
        depth = 1
        for start in range(opening - 2, -1, -1):
            if values[start] == ")":
                depth += 1
            elif values[start] == "(":
                depth -= 1
                if depth == 0:
                    if start and values[start - 1] in ("if", "for", "while", "switch"):
                        return start - 1
                    break
        else:
            raise ValueError("MEM03 primitive has an unbalanced region header")
    if opening and values[opening - 1] in ("else", "do"):
        return opening - 1
    return opening


def _original_regions(values, stacks, closings, enclosing):
    from tests.host.work_count_lexical import matches_at

    result, parent = [], ()
    start, end = 0, len(values)
    for opening in enclosing:
        begin = _region_header(values, opening)
        fragment = values[begin:opening + 1]
        candidates = [
            index for index in matches_at(values, fragment, start, end)
            if stacks[index] == parent
        ]
        if begin not in candidates:
            raise ValueError("MEM03 primitive lacks its complete enclosing compound")
        result.append((" ".join(fragment), candidates.index(begin)))
        parent = (*parent, opening)
        start, end = opening + 1, closings[opening]
    return tuple(result)


def _original_statement(values, stacks, occurrence):
    enclosing = stacks[occurrence]
    start = enclosing[-1] + 1 if enclosing else 0
    for index in range(start, occurrence):
        if values[index] in (";", "}") and stacks[index + 1] == enclosing:
            start = index + 1
    if values[start] == "else":
        raise ValueError("MEM03 primitive is not a separately observable statement")
    if values[start] in ("if", "while", "switch"):
        if values[start + 1] != "(":
            raise ValueError("MEM03 fixed control primitive is malformed")
        end = _matching_tokens(values, start + 1) + 1
    else:
        end = next((
            index + 1 for index in range(occurrence, len(values))
            if values[index] == ";" and stacks[index] == enclosing
        ), 0)
    if not start <= occurrence < end:
        raise ValueError("MEM03 fixed record access has no complete primitive")
    return start, end


@lru_cache(maxsize=4)
def original_observations(root):
    from tests.host.c_source_records import function_records
    from tests.host.work_count_lexical import compound_layout, function_spans, positional_tokens

    result = {}
    site_count = 0
    for path in (*OBSERVED_MEMORY_PATHS, *OBSERVED_OBJECT_PATHS):
        text = baseline_source(root, path)
        spans = function_spans(text)
        sites = []
        owner = (
            "KERNEL_MEMORY" if path in OBSERVED_MEMORY_PATHS else
            "KERNEL_SCHEDULER" if "/scheduler/" in path else "KERNEL_OBJECTS"
        )

        def add(function, category, kind, regions, primitive, placement, witness):
            nonlocal site_count
            site_count += 1
            sites.append((
                function, "site-" + str(site_count).zfill(4), owner, category, kind,
                regions, primitive, placement, 1, witness,
            ))

        names = OBSERVED_MEMORY_FUNCTIONS.get(path)
        if path == "kernel/memory/bootstrap_memory.c":
            names = SOURCE_PROJECTION["native_bootstrap_closure"]
        for name in function_records(text):
            if names is not None and name not in names:
                continue
            span = spans[name]
            body = positional_tokens(text[span.body_start:span.body_end])
            values = tuple(item.value for item in body)
            stacks, closings = compound_layout(values)
            for index, value in enumerate(values):
                if value not in ("for", "while") or index + 1 == len(values) or values[index + 1] != "(":
                    continue
                close = _matching_tokens(values, index + 1)
                if close + 1 >= len(values) or values[close + 1] != "{":
                    raise ValueError("MEM03 fixed loop primitive has no compound body")
                regions = _original_regions(values, stacks, closings, (*stacks[index], close + 1))
                add(name, "other", "INDEX_PROBE", regions, " ".join(values[index:close + 2]), "loop-entry", ())
            if name in (
                "micros_frame_ownership_validate", "micros_frame_ownership_runtime_validate",
                "micros_kernel_objects_validate", "micros_scheduler_core_validate",
            ):
                add(name, "other", "COMPLETE_VALIDATOR_CALL", (), "", "entry", ())
            if path not in OBSERVED_MEMORY_PATHS:
                continue
            for index, token in enumerate(values[:-1]):
                if token not in OBSERVED_FIELDS or values[index + 1] != "[":
                    continue
                if token in ("owners", "handoff_targets", "owner_counts", "managed_ranges", "allocated_bitmap"):
                    if index == 0 or values[index - 1] not in ("->", "."):
                        continue
                start, end = _original_statement(values, stacks, index)
                regions = _original_regions(values, stacks, closings, stacks[start])
                category = "other"
                if token in ("owners", "handoff_targets"):
                    category = "inactive-frame" if name == "inactive_state_is_zero" else "active-frame"
                elif token == "managed_ranges":
                    description = " ".join(region[0] for region in regions)
                    category = (
                        "inactive-geometry"
                        if "index = allocator -> managed_range_count" in description
                        or "index = ownership -> managed_range_count" in description
                        else "active-geometry"
                    )
                add(
                    name, category, OBSERVED_FIELDS[token], regions, " ".join(values[start:end]),
                    "before", (token, index - start),
                )
        if sites:
            result[path] = tuple(sites)
    if site_count != 173 or digest(result) != ORIGINAL_OBSERVATION_DIGEST:
        raise ValueError("MEM03 primitive catalogue differs from its independent fixed-source declaration")
    return result


@lru_cache(maxsize=4)
def current_observations(root):
    from tests.host.c_source_records import tokens

    original = original_observations(root)
    result = {
        path: [site for site in sites if site[1] not in REPLACED_OWNER_READS]
        for path, sites in original.items()
    }
    additions = 0

    def append(path, name, category, kind, regions, primitive, placement, witness=()):
        nonlocal additions
        additions += 1
        result[path].append((
            name, "current-site-" + str(additions).zfill(4), "KERNEL_MEMORY", category, kind,
            regions, " ".join(tokens(primitive)), placement, 1, witness,
        ))

    def clone(path, original_name, name, select):
        for row in original[path]:
            if row[0] == original_name and select(row):
                append(path, name, row[3], row[4], row[5], row[6], row[7], row[9])

    def field(name, category, regions, primitive, member, occurrence=0):
        values = tokens(primitive)
        positions = [index for index, value in enumerate(values[:-1]) if value == member and values[index + 1] == "["]
        append(
            "kernel/memory/frame_ownership_validation.c", name, category, "RECORD_VISIT",
            regions, primitive, "before", (member, positions[occurrence]),
        )

    active_loop = "for ( index = 0 ; index < ownership -> managed_range_count ; ++ index ) {"
    clone(
        "kernel/memory/frame_ownership.c", "micros_frame_ownership_geometry_matches",
        "micros_frame_ownership_active_geometry_matches",
        lambda row: row[3] == "active-geometry" or row[7] == "loop-entry" and row[6] == active_loop,
    )
    clone(
        "kernel/memory/bootstrap_memory.c", "validate_process_owners",
        "validate_active_process_owners", lambda row: True,
    )
    field(
        "micros_frame_ownership_selected_record", "active-frame", (),
        "observed = ownership->owners[frame_index];", "owners",
    )
    field(
        "micros_frame_ownership_selected_record", "active-frame", (),
        "target = ownership->handoff_targets[frame_index];", "handoff_targets",
    )
    field(
        "validate_metadata_header", "other", (),
        "if (ownership->owner_counts[MICROS_FRAME_OWNER_FREE] != 0)", "owner_counts",
    )
    kind_loop = "for ( kind_index = 0 ; kind_index < MICROS_FRAME_OWNER_KIND_COUNT ; ++ kind_index ) {"
    append(
        "kernel/memory/frame_ownership_validation.c", "validate_metadata_header",
        "other", "INDEX_PROBE", ((kind_loop, 0),), kind_loop, "loop-entry",
    )
    field(
        "validate_metadata_header", "other", ((kind_loop, 0),),
        "uint64_t count = ownership->owner_counts[kind_index];", "owner_counts",
    )
    phase = "if ( ownership -> phase == MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF ) {"
    for occurrence in (0, 1):
        field(
            "validate_metadata_header", "other", ((phase, 0),),
            "if (ownership->owner_counts[MICROS_FRAME_OWNER_KERNEL_TEMPORARY] != 0 "
            "|| ownership->owner_counts[MICROS_FRAME_OWNER_PROCESS_USER] != 0)",
            "owner_counts", occurrence,
        )
    clone(
        "kernel/memory/frame_ownership_validation.c", "micros_frame_ownership_validate",
        "micros_frame_ownership_validate_active", lambda row: row[5] == ((kind_loop, 0),),
    )
    result = {path: tuple(sites) for path, sites in result.items()}
    if additions != 14 or sum(map(len, result.values())) != 179 or digest(result) != CURRENT_OBSERVATION_DIGEST:
        raise ValueError("MEM03 current primitives differ from their independent pre-edit declaration")
    return result


def observation_sites(root, path, stage, channel):
    if stage not in (Stage.RED, Stage.GREEN) or channel not in STATE_WORK_CONTRACT["channels"]:
        raise ValueError("MEM03 observation has an unknown source stage or channel")
    catalog = (original_observations if stage is Stage.RED else current_observations)(str(root))
    expected = ORIGINAL_OBSERVATION_DIGEST if stage is Stage.RED else CURRENT_OBSERVATION_DIGEST
    if digest(catalog) != expected:
        raise ValueError("MEM03 primitive catalogue changed after declaration")
    sites = catalog.get(path, ())
    return tuple(site for site in sites if channel == "aggregate" or site[3] == channel)


def _observation_call(site):
    return (
        'MICROS_TEST_WORK_HIT("' + site[1] + '", MICROS_WORK_OWNER_' + site[2]
        + ", MICROS_WORK_KIND_" + site[4] + ", 1);"
    )


def _observation_bindings(root, path, text, stage, channel):
    from tests.host.c_source_records import tokens
    from tests.host.work_count_lexical import compound_layout, function_spans, matches_at, positional_tokens
    from tests.host.work_count_projection import resolve_regions
    from tests.host.work_count_types import Region
    from tests.host.selected_release_handoff_source import legacy_replaced_functions

    spans = function_spans(text)
    replaced = legacy_replaced_functions(root, "MEM03", path, text, stage)
    result = []
    for site in observation_sites(root, path, stage, channel):
        function, identifier, _, _, kind, regions, primitive, placement, units, witness = site
        if function in replaced:
            continue
        if function not in spans or units != 1:
            raise ValueError("MEM03 observation has an absent function or invalid units")
        span = spans[function]
        if placement == "entry":
            if kind != "COMPLETE_VALIDATOR_CALL" or regions or primitive or witness:
                raise ValueError("MEM03 diagnostic entry has another primitive")
            result.append((site, span.body_start, span.body_start))
            continue
        body = positional_tokens(text[span.body_start:span.body_end])
        values = tuple(item.value for item in body)
        stacks, closings = compound_layout(values)
        start, end, enclosing = resolve_regions(
            values, stacks, closings, tuple(Region(*region) for region in regions),
        )
        if placement == "loop-entry":
            if (
                kind != "INDEX_PROBE" or not enclosing or witness
                or regions[-1][0] != primitive or tokens(primitive)[0] not in ("for", "while")
            ):
                raise ValueError("MEM03 loop observation is not bound to its actual index primitive")
            primitive_position = span.body_start + body[_region_header(values, enclosing[-1])].start
            result.append((site, span.body_start + body[enclosing[-1]].end, primitive_position))
            continue
        fragment = tokens(primitive)
        matches = [
            index for index in matches_at(values, fragment, start, end)
            if stacks[index] == enclosing and (index == 0 or values[index - 1] in (";", "{", "}"))
        ]
        if (
            placement != "before" or len(matches) != 1 or len(witness) != 2
            or witness[1] >= len(fragment) - 1
            or fragment[witness[1]:witness[1] + 2] != (witness[0], "[")
        ):
            raise ValueError("MEM03 primitive occurrence is missing or ambiguous: " + identifier)
        result.append((
            site, span.body_start + body[matches[0]].start,
            span.body_start + body[matches[0] + witness[1]].start,
        ))
    return tuple(result)


def _validate_observation_coverage(root, path, text, stage):
    from tests.host.work_count_lexical import function_spans, positional_tokens
    from tests.host.selected_release_handoff_source import legacy_replaced_functions

    if path not in (*OBSERVED_MEMORY_PATHS, *OBSERVED_OBJECT_PATHS):
        return
    replaced = legacy_replaced_functions(root, "MEM03", path, text, stage)
    fields, loops = set(), set()
    names = OBSERVED_MEMORY_FUNCTIONS.get(path)
    if path == "kernel/memory/bootstrap_memory.c":
        names = SOURCE_PROJECTION["native_bootstrap_closure"]
    if names is not None and stage is Stage.GREEN:
        names = (*names, *SOURCE_PROJECTION["new_helpers"].get(path, ()))
    for name, span in function_spans(text).items():
        if name in replaced or names is not None and name not in names:
            continue
        body = positional_tokens(text[span.body_start:span.body_end])
        values = tuple(item.value for item in body)
        for index, value in enumerate(values[:-1]):
            position = span.body_start + body[index].start
            if value in ("for", "while") and values[index + 1] == "(":
                loops.add(position)
            if path not in OBSERVED_MEMORY_PATHS or value not in OBSERVED_FIELDS:
                continue
            if value in ("owners", "handoff_targets", "owner_counts", "managed_ranges", "allocated_bitmap"):
                if index == 0 or values[index - 1] not in ("->", "."):
                    continue
                if values[index + 1] != "[":
                    raise ValueError("MEM03 measured source introduced an unobserved array alias")
            if values[index + 1] == "[":
                fields.add(position)
    bindings = _observation_bindings(root, path, text, stage, "aggregate")
    observed_fields = [position for site, _, position in bindings if site[7] == "before"]
    observed_loops = [position for site, _, position in bindings if site[7] == "loop-entry"]
    if (
        len(observed_fields) != len(set(observed_fields)) or set(observed_fields) != fields
        or len(observed_loops) != len(set(observed_loops)) or set(observed_loops) != loops
    ):
        raise ValueError("MEM03 source has a missing, duplicated or undeclared primitive")


def _render_observations(root, path, text, stage, channel):
    _validate_observation_coverage(root, path, text, stage)
    edits = {}
    for site, position, _ in _observation_bindings(root, path, text, stage, channel):
        edits.setdefault(position, []).append(_observation_call(site))
    for position in sorted(edits, reverse=True):
        text = text[:position] + "".join(edits[position]) + text[position:]
    return OBSERVATION_PREFIX + text


def observe_source(root, path, text, stage, channel):
    from tests.host.selected_release_handoff_source import legacy_observe_source

    validate_retained_source(root, path, text, stage)
    current = legacy_observe_source(root, "MEM03", path, text, stage, channel)
    if current is not None:
        return current
    return _render_observations(root, path, text, stage, channel)


def project_observations(root, path, text, stage, channel):
    from tests.host.selected_release_handoff_source import project_legacy_observations

    current = project_legacy_observations(root, "MEM03", path, text, stage, channel)
    if current is not None:
        return current
    if not text.startswith(OBSERVATION_PREFIX):
        raise ValueError("MEM03 observation preamble is not its private native declaration")
    restored = text[len(OBSERVATION_PREFIX):]
    for site in observation_sites(root, path, stage, channel):
        call = _observation_call(site)
        if restored.count(call) != 1:
            raise ValueError("MEM03 observation is missing, changed or duplicated")
        restored = restored.replace(call, "", 1)
    validate_retained_source(root, path, restored, stage)
    if _render_observations(root, path, restored, stage, channel) != text:
        raise ValueError("MEM03 observation moved away from its actual primitive or compound")
    return restored


ORIGINAL_AUDIT_PATH_DIGEST = "c16ed9079e302b8666c990bcc2c353573eafae42bd12e119ac724c07a5faa364"
ORIGINAL_AUDIT_DIGEST = "12e4aedba9d72e929b12023d66360ea2e9ae676898c5a0956d0df0ad4ea72eb4"
RETAINED_AUDIT_DIGEST = "f1c50683f243a2384ff2ebeda83754fabcb17834d0202ee5b0b499afb07259ea"
MIGRATED_AUDIT_DIGEST = "183af7606cc9fe40e7bb5b8b9bb0ea33882bbb5399c7b96afb7983c6fb85dc5a"
COMPLETE_AUDITS = ("micros_frame_ownership_validate", "micros_frame_ownership_runtime_validate")
RETAINED_AUDIT_FUNCTIONS = (
    "validation_authority_is_valid", "micros_frame_ownership_runtime_initialize",
    "boot_initialize_memory", "validate_prepared_services",
    "bootstrap_preparation_rollback_preparation", "micros_frame_ownership_runtime_validate",
    "micros_bootstrap_memory_run_self_test",
)
WHOLE_ACTIVE_FUNCTIONS = (
    "micros_frame_ownership_prepare_wired_process_user_set", "micros_frame_ownership_complete_handoff",
    "micros_vm_boot_build", "micros_vm_boot_validate_authority",
)


@lru_cache(maxsize=4)
def original_audit_paths(root):
    paths = subprocess.check_output(
        ("git", "ls-tree", "-r", "--name-only", "-z", SOURCE_PROJECTION["source_reference"], "--", "kernel"),
        cwd=root, timeout=30,
    ).decode().split("\0")
    selected = tuple(sorted(path for path in paths if path.endswith(".c") and not path.startswith("kernel/tests/")))
    if len(selected) != 183 or digest(selected) != ORIGINAL_AUDIT_PATH_DIGEST:
        raise ValueError("MEM03 original audit namespace changed")
    return selected


def _audit_calls(text, path, names):
    from tests.host.c_source_records import function_records
    from tests.host.test_frame_ownership_source_layout import c_lexical_text

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


@lru_cache(maxsize=4)
def declared_audit_boundaries(root):
    original = tuple(sorted(
        call for path in original_audit_paths(root)
        for call in _audit_calls(baseline_source(root, path), path, COMPLETE_AUDITS)
    ))
    retained = tuple(row for row in original if row[1] in RETAINED_AUDIT_FUNCTIONS)
    migrated = tuple((
        *row[:2],
        "micros_frame_ownership_runtime_validate_metadata" if row[2].endswith("runtime_validate") else
        "micros_frame_ownership_validate_active" if row[1] in WHOLE_ACTIVE_FUNCTIONS else
        "micros_frame_ownership_validate_metadata",
        *row[3:],
    ) for row in original if row not in retained)
    if (
        digest(original) != ORIGINAL_AUDIT_DIGEST or digest(retained) != RETAINED_AUDIT_DIGEST
        or digest(migrated) != MIGRATED_AUDIT_DIGEST
    ):
        raise ValueError("MEM03 audit boundaries differ from the independently declared 26/8/18 census")
    return original, retained, migrated


def measure_audit_boundaries(root, stage):
    from tools.stack_artifact_files import read_regular

    root = Path(root).resolve()
    if stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM03 audit measurement has an unknown source stage")
    original, _, migrated = declared_audit_boundaries(str(root))
    wanted = {(row[0], row[1], row[2]) for row in migrated}
    paths = original_audit_paths(str(root)) if stage is Stage.RED else tuple(sorted(
        path.relative_to(root).as_posix() for path in (root / "kernel").rglob("*.c")
        if not path.relative_to(root).as_posix().startswith("kernel/tests/")
    ))
    if not set(original_audit_paths(str(root))) <= set(paths):
        raise ValueError("MEM03 current audit namespace omitted an original production source")
    files, complete, replacements = {}, [], []
    names = (*COMPLETE_AUDITS, "micros_frame_ownership_validate_metadata",
             "micros_frame_ownership_validate_active", "micros_frame_ownership_runtime_validate_metadata")
    for path in paths:
        if stage is Stage.RED:
            text = baseline_source(str(root), path)
            data = text.encode()
            identity = {"kind": "file", "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        else:
            data, actual = read_regular(regular_source(root, path))
            text = data.decode()
            identity = {key: actual[key] for key in ("kind", "size", "sha256")}
        files[path] = identity
        for row in _audit_calls(text, path, names):
            if row[2] in COMPLETE_AUDITS:
                complete.append(row)
            elif row[:3] in wanted:
                replacements.append(row)
    result = {
        "stage": stage.value, "source_inputs": files,
        "complete": tuple(sorted(complete)), "migrated": tuple(sorted(replacements)),
    }
    if stage is Stage.RED and (result["complete"] != original or result["migrated"]):
        raise ValueError("MEM03 fixed execution no longer has the original complete-audit boundaries")
    if stage is Stage.GREEN:
        for path, identity in files.items():
            _, actual = read_regular(regular_source(root, path))
            if any(actual[key] != value for key, value in identity.items()):
                raise ValueError("MEM03 caller source changed during audit measurement")
    return result


def _native_target_recipe(index, channel, target):
    observed = channel != "ordinary"
    root = "${MEM03_NATIVE_ROOT}/" + channel
    result = (
        "    add_executable(" + target + " ${MEM03_SOURCES_" + str(index) + "}\n"
        "        tests/host/selected_ownership_test.c\n"
        + ("        tests/work/work_counts.c\n" if observed else "")
        + "    )\n"
        "    target_include_directories(" + target + " PRIVATE\n"
        '        "' + root + '" "' + root + '/include"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include")\n'
        "    target_compile_features(" + target + " PRIVATE c_std_17)\n"
        "    target_compile_options(" + target
        + " PRIVATE -Wall -Wextra -Werror -Wpedantic -Wmissing-prototypes -Wstrict-prototypes -fno-builtin)\n"
        '    if(MICROS_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")\n'
        "        target_compile_options(" + target + " PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)\n"
        "        target_link_options(" + target + " PRIVATE -fsanitize=address,undefined)\n"
        "    endif()\n"
    )
    if observed:
        return result + "    target_compile_definitions(" + target + " PRIVATE " + " ".join(
            RECIPE_ADMISSION["observed_definitions"],
        ) + ")\n"
    return result + (
        "    target_compile_definitions(" + target + " PRIVATE\n"
        "        MICROS_SELECTED_OWNERSHIP_LAUNCHER=1\n"
        '        "MICROS_SELECTED_OWNERSHIP_PYTHON=\\"${MICROS_PYTHON}\\""\n'
        '        "MICROS_SELECTED_OWNERSHIP_DRIVER=\\"${CMAKE_CURRENT_SOURCE_DIR}/tools/check_selected_ownership.py\\""\n'
        '        "MICROS_SELECTED_OWNERSHIP_ROOT=\\"${CMAKE_CURRENT_SOURCE_DIR}\\""\n'
        "    )\n"
    )


def native_recipe():
    channels = (*STATE_WORK_CONTRACT["channels"], "ordinary")
    result = "    set_property(GLOBAL APPEND PROPERTY JOB_POOLS mem03_source_proof=1)\n"
    result += "".join(
        "    add_custom_command(\n"
        "        OUTPUT ${" + owner + "_NATIVE_OUTPUTS} APPEND\n"
        "        DEPENDS tests/host/selected_ownership_source.py\n"
        "            tests/host/selected_ownership_authority.py\n"
        "            tests/host/evidence_authority_scope.py\n"
        "            tools/validation_context.py\n"
        "            tools/validation_inputs.py\n"
        "    )\n"
        for owner in ("MEM01", "MEM02")
    )
    result += (
        '    set(MEM03_NATIVE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/selected-ownership")\n'
        "    set(MEM03_NATIVE_INPUTS\n"
        + "".join("        " + path + "\n" for path in RECIPE_ADMISSION["native_source_members"])
        + "    )\n"
        "    file(GLOB_RECURSE MEM03_NATIVE_HEADERS\n"
        '        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}/arch/*.h"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.h"\n'
        "    )\n"
        "    set(MEM03_NATIVE_OUTPUTS)\n"
    )
    for index, channel in enumerate(channels):
        root = "${MEM03_NATIVE_ROOT}/" + channel
        result += (
            "    set(MEM03_SOURCES_" + str(index) + ")\n"
            "    foreach(mem03_source IN LISTS MEM03_NATIVE_INPUTS)\n"
            "        list(APPEND MEM03_SOURCES_" + str(index) + ' "' + root + '/${mem03_source}")\n'
            "    endforeach()\n"
            "    list(APPEND MEM03_NATIVE_OUTPUTS ${MEM03_SOURCES_" + str(index) + "})\n"
            "    foreach(mem03_header IN LISTS MEM03_NATIVE_HEADERS)\n"
            '        file(RELATIVE_PATH mem03_relative_header "${CMAKE_CURRENT_SOURCE_DIR}" "${mem03_header}")\n'
            '        list(APPEND MEM03_NATIVE_OUTPUTS "' + root + '/${mem03_relative_header}")\n'
            "    endforeach()\n"
        )
    result += (
        "    add_custom_command(\n"
        "        OUTPUT ${MEM03_NATIVE_OUTPUTS}\n"
        "        JOB_POOL mem03_source_proof\n"
        '        COMMAND "${MICROS_PYTHON}" ${MICROS_PYTHON_ARGS}\n'
        '            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_selected_ownership_fixture.py"\n'
        "        DEPENDS ${MEM03_NATIVE_INPUTS} ${MEM03_NATIVE_HEADERS}\n"
        "            tools/generate_selected_ownership_fixture.py\n"
        "            tests/host/selected_ownership_source.py\n"
        "            tests/host/selected_ownership_authority.py\n"
        "            tests/host/evidence_authority_scope.py\n"
        "            tools/validation_context.py\n"
        "            tools/validation_inputs.py\n"
        "            tests/host/selected_ownership_test.h\n"
        "            tests/host/c_source_records.py\n"
        "            tests/host/work_count_lexical.py\n"
        "            tests/host/work_count_projection.py\n"
        "            tests/host/test_frame_ownership_source_layout.py\n"
        '        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"\n'
        "        VERBATIM\n"
        "    )\n"
    )
    for index, (channel, target) in enumerate(zip(channels, RECIPE_ADMISSION["native_targets"])):
        result += _native_target_recipe(index, channel, target)
    result += (
        "    add_test(NAME selected-ownership COMMAND selected_ownership_ordinary_test\n"
        + "".join('        "$<TARGET_FILE:' + target + '>"\n' for target in RECIPE_ADMISSION["native_targets"][:-1])
        + "    )\n"
        "    set_tests_properties(selected-ownership PROPERTIES LABELS slow)\n"
        + "".join(
            "    add_dependencies(" + tier + " " + " ".join(RECIPE_ADMISSION["native_targets"]) + ")\n"
            for tier in RECIPE_ADMISSION["native_tiers"]
        )
        + "\n"
    )
    return result


def extend_cmake(text):
    from tests.host.selected_address_space_source import native_recipe as selected_address_space_recipe
    from tests.host.selected_release_handoff_source import native_recipe as selected_release_handoff_recipe

    anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    if hashlib.sha256(text.encode()).hexdigest() != RECIPE_ADMISSION["base_cmake_sha256"] or text.count(anchor) != 1:
        raise ValueError("MEM03 native recipe lacks its complete reviewed validation base")
    for owner in ("MEM01", "MEM02"):
        output = "        OUTPUT ${" + owner + "_NATIVE_OUTPUTS}\n"
        if text.count(output) != 1:
            raise ValueError("MEM03 source-proof pool lacks its exact predecessor generator")
        text = text.replace(output, output + "        JOB_POOL mem03_source_proof\n", 1)
    return text.replace(anchor, native_recipe() + selected_address_space_recipe() + selected_release_handoff_recipe() + anchor, 1)


def project_cmake(text):
    from tests.host.selected_address_space_source import project_cmake as project_selected_address_space

    text = project_selected_address_space(text)
    from tests.host.qemu_transcript_source import project_transcript_cmake

    text = project_transcript_cmake(text)
    if not any(marker in text for marker in ("MEM03_NATIVE", "selected_ownership", "selected-ownership", "mem03_source_proof")):
        return text
    addition = native_recipe()
    anchor = '    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/stack/StackEvidence.cmake")\n'
    if text.count(addition + anchor) != 1:
        raise ValueError("MEM03 native recipe is changed, partial or relocated")
    original = text.replace(addition, "", 1)
    for owner in ("MEM01", "MEM02"):
        output = "        OUTPUT ${" + owner + "_NATIVE_OUTPUTS}\n"
        current = output + "        JOB_POOL mem03_source_proof\n"
        if original.count(current) != 1:
            raise ValueError("MEM03 source-proof pool is changed, partial or relocated")
        original = original.replace(current, output, 1)
    if hashlib.sha256(original.encode()).hexdigest() not in (
        RECIPE_ADMISSION["base_cmake_sha256"], RECIPE_ADMISSION["reference_cmake_sha256"],
    ):
        raise ValueError("MEM03 native recipe changed unrelated predecessor bytes")
    return original


def _native_scopes(family, path):
    prefix = "tests/host/memory_native_source_bindings.py:native-source-admission:"
    if family not in ("MEM01", "MEM02"):
        raise ValueError("MEM03 has no native admission in this family")
    return tuple(
        (key, key.removeprefix(prefix)) for key in SOURCE_PROJECTION["retirement_keys"]
        if key == prefix + family + "/definitions/" + path
        or key.startswith(prefix + family + "/function/" + path + "/")
        or key == prefix + family + "/native-input/" + path
    )


def native_assertion_admission(root, family, path, text, stage):
    from tests.host.evidence_authority_scope import identity_required
    from tests.host.selected_address_space_source import native_assertion_admission as next_admission

    inherited = next_admission(root, family, path, text, stage)

    if stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM03 legacy admission has an unknown source stage")
    scopes = _native_scopes(family, path)
    if stage is Stage.RED or not scopes:
        return inherited
    selected = frozenset(scope for key, scope in scopes if not identity_required(root, key))
    if selected:
        validate_retained_source(root, path, text, Stage.GREEN)
    return selected | inherited


def legacy_observation_sites(root, family, path, stage, sites):
    root = Path(__file__).resolve().parents[2] if root is None else Path(root)
    scopes = _native_scopes(family, path)
    if stage is Stage.RED or not scopes:
        return sites
    text = regular_source(root, path).read_text()
    if not native_assertion_admission(root, family, path, text, stage):
        return sites
    if family == "MEM02":
        sites = tuple(row for row in sites if row[1] != "lookup-owner-record")
    helpers = set(SOURCE_PROJECTION["new_helpers"].get(path, ()))
    helpers.add("micros_frame_ownership_allocator_bit_is_set")
    additions = []
    for row in current_observations(str(root)).get(path, ()):
        if row[0] not in helpers or row[7] != "before":
            continue
        kind = row[4]
        if kind == "RANGE_VISIT" and "micros_frame_range_snapshot" in row[6]:
            kind = "RECORD_VISIT"
        regions = row[5]
        if any(occurrence != 0 for _, occurrence in regions):
            raise ValueError("MEM03 legacy helper has an undeclared primitive occurrence")
        if family == "MEM01":
            additions.append((row[0], "mem03-" + row[1], kind, tuple(region for region, _ in regions), row[6]))
        else:
            additions.append((row[0], "mem03-" + row[1], kind, regions, row[6], "before", 1))
    return (*sites, *additions)


def _legacy_changes(path):
    if path in ("tests/host/kernel_layout_source.py", "tests/host/test_kernel_directory_layout.py"):
        return ()
    if path == "tests/host/test_frame_validation_hot_path.py":
        original = (
            "    if (\n"
            '        function_token_digest(preprocessed, "validate_active_frames")\n'
            "        != ACTIVE_TOKENS_SHA256\n"
            '        or function_token_digest(preprocessed, "inactive_state_is_zero")\n'
            "        != INACTIVE_TOKENS_SHA256\n"
            "    ):\n"
            '        raise AssertionError("validator work shape differs from reviewed identity")\n'
        )
        extended = (
            "    from tests.host.evidence_authority_scope import identity_required\n"
            "    from tests.host.selected_ownership_source import assert_complete_helper_tokens\n\n"
            "    for name, key, expected in (\n"
            '        ("validate_active_frames",\n'
            '         "tests/host/test_frame_validation_hot_path.py:data-entry:ACTIVE_TOKENS_SHA256",\n'
            "         ACTIVE_TOKENS_SHA256),\n"
            '        ("inactive_state_is_zero",\n'
            '         "tests/host/test_frame_validation_hot_path.py:data-entry:INACTIVE_TOKENS_SHA256",\n'
            "         INACTIVE_TOKENS_SHA256),\n"
            "    ):\n"
            "        if identity_required(ROOT, key):\n"
            "            if function_token_digest(preprocessed, name) != expected:\n"
            '                raise AssertionError("validator work shape differs from reviewed identity")\n'
            "        else:\n"
            "            assert_complete_helper_tokens(preprocessed, name)\n"
        )
        return ((original, extended),)
    if path == "tools/generate_kernel_mapping_fixture.py":
        original = '                raise ValueError("MEM02 corrected native input remains pending its actual pre-production Red")\n'
        return ((original, (
            "                from tests.host.selected_ownership_source import native_assertion_admission\n"
            '                if "MEM02/native-input/" + name not in native_assertion_admission(\n'
            '                    root, "MEM02", name, data.decode("utf-8"), stage,\n'
            "                ):\n" + "    " + original
        )),)
    if path == "tests/host/bootstrap_allocation_source.py":
        return (
            (
                '    original = baseline_source(Path(root), path)\n',
                '    from tests.host.selected_ownership_source import native_assertion_admission\n\n'
                '    retired = native_assertion_admission(root, "MEM01", path, text, stage)\n'
                '    original = baseline_source(Path(root), path)\n',
            ),
            (
                "    if set(after) != set(before) | names:\n",
                '    if "MEM01/definitions/" + path not in retired and set(after) != set(before) | names:\n',
            ),
            (
                "    for name, old in before.items():\n        current = after[name]\n",
                "    for name, old in before.items():\n"
                '        if "MEM01/function/" + path + "/" + name in retired:\n'
                "            continue\n        current = after[name]\n",
            ),
            (
                "def observation_sites(path, stage):\n",
                "def observation_sites(path, stage, root=None):\n",
            ),
            (
                "    result = (*COMMON_OBSERVATIONS.get(path, ()), *selected.get(path, ()))\n",
                "    result = (*COMMON_OBSERVATIONS.get(path, ()), *selected.get(path, ()))\n"
                "    from tests.host.selected_ownership_source import legacy_observation_sites\n"
                '    result = legacy_observation_sites(root, "MEM01", path, stage, result)\n',
            ),
            (
                "def _observation_spans(path, text, stage):\n",
                "def _observation_spans(path, text, stage, root=None):\n",
            ),
            (
                "    for site in observation_sites(path, stage):\n",
                "    for site in observation_sites(path, stage, root=root):\n",
            ),
            (
                "def _render_observations(path, text, stage):\n",
                "def _render_observations(path, text, stage, root=None):\n",
            ),
            (
                "    for site, begin, _ in _observation_spans(path, text, stage):\n",
                "    for site, begin, _ in _observation_spans(path, text, stage, root=root):\n",
            ),
            (
                "    return _render_observations(path, text, stage)\n",
                "    return _render_observations(path, text, stage, root=root)\n",
            ),
            (
                "    if _render_observations(path, original, stage) != text:\n",
                "    if _render_observations(path, original, stage, root=root) != text:\n",
            ),
        )
    if path == "tests/host/kernel_mapping_source.py":
        original = '            raise ValueError("MEM02 fixed or noncandidate native source changed: " + path)\n'
        return (
            (
                original,
                "            from tests.host.selected_ownership_source import native_assertion_admission\n"
                '            if stage is Stage.GREEN and "MEM02/native-input/" + path in native_assertion_admission(\n'
                '                root, "MEM02", path, text, stage,\n'
                "            ):\n                return\n" + original,
            ),
            (
                "    keys = [(row[0], row[1]) for row in result]\n",
                "    from tests.host.selected_ownership_source import legacy_observation_sites\n"
                '    result = legacy_observation_sites(root, "MEM02", path, stage, result)\n'
                "    keys = [(row[0], row[1]) for row in result]\n",
            ),
            (
                "def project_gate_adapter(path, text):\n"
                "    from tests.host.work_count_recipe import project_feedback_reader\n\n"
                "    text = project_feedback_reader(path, text)\n",
                "def project_gate_adapter(path, text):\n"
                "    from tests.host.selected_ownership_source import project_legacy_adapter\n"
                "    from tests.host.work_count_recipe import project_feedback_reader\n\n"
                "    text = project_legacy_adapter(path, text)\n"
                "    text = project_feedback_reader(path, text)\n",
            ),
            (
                "def project_cmake(text):\n",
                "def project_cmake(text):\n"
                "    from tests.host.selected_ownership_source import project_cmake as project_selected_cmake\n\n"
                "    text = project_selected_cmake(text)\n",
            ),
        )
    raise ValueError("MEM03 has no legacy adapter in this owner")


def extend_legacy_adapter(path, text):
    if hashlib.sha256(text.encode()).hexdigest() != SOURCE_PROJECTION["validation_base_source_sha256"].get(path):
        raise ValueError("MEM03 adapter lacks its complete reviewed validation base")
    for before, after in _legacy_changes(path):
        expected = 2 if path.endswith("bootstrap_allocation_source.py") and before == "    for site in observation_sites(path, stage):\n" else 1
        if text.count(before) != expected or after in text:
            raise ValueError("MEM03 adapter lacks its exact original clause")
        text = text.replace(before, after)
    return text


def project_legacy_adapter(path, text):
    from tests.host.selected_address_space_source import project_legacy_adapter as project_next

    if "tests.host.selected_address_space_source" in text:
        text = project_next(Path(__file__).resolve().parents[2], path, text)
    if path == "tests/host/test_kernel_bootstrap_source_layout.py":
        if "micros_frame_ownership_runtime_validate_metadata" not in text:
            return text
        root = str(Path(__file__).resolve().parents[2])
        original = baseline_source(root, path)
        if text != extend_bootstrap_readonly_fixture(original):
            raise ValueError("MEM03 read-only fixture changed unrelated bootstrap authority")
        return original
    if path not in SOURCE_ROLES["fixed_adapters"]:
        return text
    marker = (
        "from tests.host.evidence_authority_scope import"
        if path in ("tests/host/kernel_layout_source.py", "tests/host/test_kernel_directory_layout.py")
        else "selected_ownership"
    )
    if marker not in text:
        return text
    root = str(Path(__file__).resolve().parents[2])
    original = baseline_adapter_source(root, path)
    if text != extend_legacy_adapter(path, original):
        raise ValueError("MEM03 legacy adapter is changed, partial or alters unrelated policy")
    return original


def extend_bootstrap_readonly_fixture(text):
    anchor = '            "micros_bootstrap_control_validate", "micros_frame_ownership_runtime_validate",\n'
    if text.count(anchor) != 1 or "micros_frame_ownership_runtime_validate_metadata" in text:
        raise ValueError("MEM03 read-only fixture lacks its exact original boundary")
    return text.replace(anchor, anchor + '            "micros_frame_ownership_runtime_validate_metadata",\n', 1)


def validate_scaffold(root):
    from tests.host.work_count_recipe import project_host_test_commands
    from tests.host.selected_address_space_source import project_legacy_adapter as project_next

    root = Path(root)
    cmake = regular_source(root, "CMakeLists.txt").read_text()
    project_host_test_commands(cmake, require_current=True)
    if project_cmake(cmake) == cmake:
        raise ValueError("MEM03 six-product/one-CTest integration is absent")
    for path in SOURCE_ROLES["fixed_adapters"]:
        actual = project_next(root, path, regular_source(root, path).read_text())
        original = baseline_adapter_source(str(root), path)
        if actual != extend_legacy_adapter(path, original):
            raise ValueError("MEM03 frozen native/identity adapter is absent or altered: " + path)
    path = SOURCE_ROLES["unchanged_candidate"]
    if regular_source(root, path).read_text() != baseline_source(str(root), path):
        raise ValueError("MEM03 changed the independently declared C4 identities")
    path = "tests/host/test_kernel_bootstrap_source_layout.py"
    if regular_source(root, path).read_text() != extend_bootstrap_readonly_fixture(baseline_source(str(root), path)):
        raise ValueError("MEM03 read-only dependency fixture is absent or altered")
    for path in SOURCE_ROLES["immutable_additions"]:
        regular_source(root, path)
