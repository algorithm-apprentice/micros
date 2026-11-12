"""Current IPC ownership, model independence and storage contracts."""

from pathlib import Path
import re
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from tests.host.build_source_records import cmake_command, cmake_if_blocks
from tests.host.c_source_records import c_declaration_text, c_function_definitions, c_lexical_text, file_scope_objects, tokens
from tests.host.source_snapshot import isolated_source_view, source_bytes
from tests.host import test_vm_service_source_layout as vm_layout
from tests.host.test_kernel_pm_control_source_layout import macro_definition_text
from tests.host.test_pm_service_source_layout import static_local_storage
from tools import validation_plan


def current_text(path, encoding="utf-8"):
    return source_bytes(REPOSITORY_ROOT, path).decode(encoding)


PUBLIC_PATHS = ('include/micros/ipc.h',
 'include/micros/ipc_core.h',
 'include/micros/ipc_abi.h',
 'include/micros/ipc_runtime.h',
 'include/micros/ipc_buffer.h',
 'kernel/ipc/ipc_syscall.h',
 'kernel/tests/ipc/ipc_syscall_test.h',
 'kernel/tests/ipc/ipc_ecall_test.h',
 'kernel/tests/ipc/ipc_syscall_panic_test.h',
 'kernel/ipc/ipc_runtime_internal.h',
 'kernel/ipc/ipc_buffer_internal.h',
 'kernel/ipc/ipc_abi.c',
 'kernel/ipc/ipc_buffer.c',
 'kernel/ipc/ipc_runtime.c',
 'kernel/tests/ipc/ipc_syscall_panic_test.c',
 'arch/riscv64/ipc_syscall_test.S',
 'arch/riscv64/ipc_ecall_test.S',
 'arch/riscv64/ipc_syscall_panic_test.S',
 'tests/host/ipc_abi_test.c',
 'tests/host/ipc_state_test.c',
 'tests/host/ipc_queue_test.c',
 'tests/host/ipc_queue_mutation_test.c',
 'tests/host/ipc_delivery_test.c',
 'tests/host/ipc_deadlock_test.c',
 'tests/host/ipc_current_guard_test.c',
 'tests/host/ipc_model_main.c')

# Narrow declaration data captured from independently reviewed OBJ02 #246.
CURRENT_FILE_SCOPE_OBJECTS = {'tests/host/ipc_current_guard_test.c': ('static struct micros_kernel_objects objects ;',
                                         'static struct micros_process_handle process ;',
                                         'static struct micros_thread_handle threads [ GUARD_THREAD_COUNT ] '
                                         ';',
                                         'static struct micros_hart_handle hart ;',
                                         'static struct micros_hart_handle other_hart ;'),
 'tests/host/ipc_deadlock_test.c': ('static struct micros_endpoint_registry registry ;',
                                    'static struct micros_kernel_objects objects ;',
                                    'static struct micros_process_handle processes [ MICROS_PROCESS_CAPACITY '
                                    '] ;',
                                    'static struct micros_thread_handle primary_threads [ '
                                    'MICROS_PROCESS_CAPACITY ] ;',
                                    'static micros_endpoint_t endpoints [ MICROS_PROCESS_CAPACITY ] ;',
                                    'static struct micros_hart_handle hart ;'),
 'tests/host/ipc_delivery_test.c': ('static struct micros_endpoint_registry registry ;',
                                    'static struct micros_kernel_objects objects ;',
                                    'static struct micros_process_handle processes [ DELIVERY_PROCESS_COUNT '
                                    '] ;',
                                    'static micros_endpoint_t endpoints [ DELIVERY_PROCESS_COUNT ] ;',
                                    'static struct micros_thread_handle source_threads [ '
                                    'DELIVERY_SOURCE_COUNT ] ;',
                                    'static struct micros_thread_handle destination_threads [ '
                                    'DELIVERY_DESTINATION_THREAD_COUNT ] ;',
                                    'static struct micros_thread_handle anchor_thread ;',
                                    'static struct micros_hart_handle hart ;',
                                    'static struct micros_ipc_message source_messages [ '
                                    'DELIVERY_SOURCE_COUNT ] ;'),
 'tests/host/ipc_queue_mutation_test.c': ('static struct micros_endpoint_registry registry ;',
                                          'static struct micros_kernel_objects objects ;',
                                          'static struct micros_process_handle processes [ '
                                          'MUTATION_PROCESS_COUNT ] ;',
                                          'static struct micros_thread_handle threads [ '
                                          'MUTATION_PROCESS_COUNT ] ;',
                                          'static micros_endpoint_t endpoints [ MUTATION_PROCESS_COUNT ] ;',
                                          'static struct micros_hart_handle hart ;'),
 'tests/host/ipc_queue_test.c': ('static struct micros_endpoint_registry registry ;',
                                 'static struct micros_kernel_objects objects ;',
                                 'static struct micros_process_handle processes [ QUEUE_PROCESS_COUNT ] ;',
                                 'static struct micros_thread_handle threads [ QUEUE_PROCESS_COUNT ] ;',
                                 'static micros_endpoint_t endpoints [ QUEUE_PROCESS_COUNT ] ;',
                                 'static struct micros_hart_handle hart ;'),
 'tests/host/ipc_state_test.c': ('static struct micros_endpoint_registry registry ;',
                                 'static struct micros_kernel_objects objects ;',
                                 'static struct micros_process_handle process ;',
                                 'static struct micros_thread_handle thread ;'),
 'kernel/tests/ipc/ipc_syscall_panic_test.c': ('static const uint64_t TEST_CODE_VIRTUAL_ADDRESS = '
                                               'MICROS_USER_VIRTUAL_BASE ;',
                                               'static const uint64_t TEST_DATA_VIRTUAL_ADDRESS = '
                                               'MICROS_USER_VIRTUAL_BASE + UINT64_C ( 0x00002000 ) ;',
                                               'static const uint64_t TEST_STACK_VIRTUAL_ADDRESS = '
                                               'MICROS_USER_VIRTUAL_BASE + UINT64_C ( 0x00004000 ) ;',
                                               'static const uint64_t TEST_RECEIVE_OFFSET = UINT64_C ( 0x100 '
                                               ') ;',
                                               'static const uint64_t TEST_TIMER_INTERVAL = UINT64_C ( '
                                               '0x0000000010000000 ) ;',
                                               'static struct micros_process_handle processes [ '
                                               'IPC_PANIC_PROCESS_COUNT ] ;',
                                               'static struct micros_thread_handle threads [ '
                                               'IPC_PANIC_PROCESS_COUNT ] ;',
                                               'static micros_endpoint_t endpoints [ IPC_PANIC_PROCESS_COUNT '
                                               '] ;',
                                               'static struct micros_endpoint_registry * registry ;',
                                               'static uint64_t code_physical [ IPC_PANIC_PROCESS_COUNT ] ;',
                                               'static uint64_t data_physical [ IPC_PANIC_PROCESS_COUNT ] ;',
                                               'static uint64_t stack_physical [ IPC_PANIC_PROCESS_COUNT ] ;',
                                               'static bool buffer_invalidated ;'),
 'kernel/ipc/ipc_runtime.c': ('static struct micros_endpoint_registry authoritative_registry ;',
                              'static struct micros_endpoint_registry initialization_scratch ;',
                              'static bool runtime_ready ;'),
 'kernel/tests/ipc/ipc_syscall_test_fixture.c': ('uintptr_t micros_ipc_syscall_test_saved_sp ;',
                                                 'uintptr_t micros_ipc_syscall_test_saved_gp ;',
                                                 'uintptr_t micros_ipc_syscall_test_saved_tp ;',
                                                 'static struct ipc_syscall_test_context test_context ;',
                                                 'const size_t ipc_syscall_test_scenario_threads [ 21 ] = { '
                                                 'IPC_SYSCALL_CLIENT , IPC_SYSCALL_SERVER , '
                                                 'IPC_SYSCALL_SERVER , IPC_SYSCALL_CLIENT , '
                                                 'IPC_SYSCALL_SERVER , IPC_SYSCALL_SERVER , '
                                                 'IPC_SYSCALL_CLIENT , IPC_SYSCALL_SERVER , '
                                                 'IPC_SYSCALL_CLIENT , IPC_SYSCALL_PEER , IPC_SYSCALL_SERVER '
                                                 ', IPC_SYSCALL_PEER , IPC_SYSCALL_PEER , IPC_SYSCALL_PEER , '
                                                 'IPC_SYSCALL_CLIENT , IPC_SYSCALL_SERVER , '
                                                 'IPC_SYSCALL_SERVER , IPC_SYSCALL_SERVER , '
                                                 'IPC_SYSCALL_CLIENT , IPC_SYSCALL_CLIENT , '
                                                 'IPC_SYSCALL_CLIENT , } ;',
                                                 'const enum ipc_syscall_command '
                                                 'ipc_syscall_test_scenario_commands [ 21 ] = { '
                                                 'IPC_COMMAND_CLIENT_SEND_ONE , '
                                                 'IPC_COMMAND_SERVER_RECEIVE_SEND_ONE , '
                                                 'IPC_COMMAND_SERVER_WAIT_CALL_ONE , '
                                                 'IPC_COMMAND_CLIENT_CALL_ONE , IPC_COMMAND_SERVER_REPLY_ONE '
                                                 ', IPC_COMMAND_SERVER_WAIT_CALL_TWO , '
                                                 'IPC_COMMAND_CLIENT_CALL_TWO , '
                                                 'IPC_COMMAND_SERVER_REPLY_RECEIVE_TWO , '
                                                 'IPC_COMMAND_CLIENT_WAIT_SERVER_CALL , '
                                                 'IPC_COMMAND_PEER_SEND , IPC_COMMAND_SERVER_CALL_CLIENT , '
                                                 'IPC_COMMAND_PEER_NOTIFY_ONE , IPC_COMMAND_PEER_NOTIFY_TWO '
                                                 ', IPC_COMMAND_PEER_BLOCK , IPC_COMMAND_CLIENT_REPLY_THREE '
                                                 ', IPC_COMMAND_SERVER_RECEIVE_NOTIFY , '
                                                 'IPC_COMMAND_SERVER_WAKE_PEER , '
                                                 'IPC_COMMAND_SERVER_BLOCK_FINAL , '
                                                 'IPC_COMMAND_CLIENT_ERROR_UNAUTHORIZED , '
                                                 'IPC_COMMAND_CLIENT_ERROR_ARGUMENT , '
                                                 'IPC_COMMAND_CLIENT_ERROR_FAULT , } ;'),
 'kernel/tests/ipc/ipc_ecall_test_fixture.c': ('const struct micros_privilege_profile '
                                               'ipc_ecall_test_profiles [ 2 ] = { { . id = 1 , . name = , . '
                                               'operations = MICROS_PRIVILEGE_OPERATION_SEND | '
                                               'MICROS_PRIVILEGE_OPERATION_NOTIFY , . notify_targets = '
                                               'UINT32_C ( 1 ) << 2 , } , { . id = 2 , . name = , . '
                                               'operations = MICROS_PRIVILEGE_OPERATION_RECEIVE , } , } ;',
                                               'const uint8_t ipc_ecall_test_profile_ids [ '
                                               'IPC_ECALL_PROCESS_COUNT ] = { 1 , 2 , } ;',
                                               'uintptr_t micros_ipc_ecall_test_saved_sp ;',
                                               'uintptr_t micros_ipc_ecall_test_saved_gp ;',
                                               'uintptr_t micros_ipc_ecall_test_saved_tp ;',
                                               'static struct ipc_ecall_test_context test_context ;'),
 'kernel/tests/ipc/ipc_test_fixture.c': ('static struct ipc_test_context test_context ;',
                                         'static const struct micros_ipc_message zero_message ;'),
 'tests/host/ipc_model_test_fixture.c': ('static const size_t model_initial_thread_counts [ '
                                         'MODEL_PROCESS_COUNT ] = { 2 , 2 , 2 , 1 , 1 , 1 , 1 , } ;',
                                         'static struct ipc_model_test_context model_context ;'),
 'tests/host/ipc_close_model_test.c': ('static const char * const close_scenario_operation_names [ '
                                       'CLOSE_SCENARIO_OPERATION_COUNT ] = { , , , , , , , , } ;',),
 'tests/host/ipc_close_test_fixture.c': ('static struct ipc_close_test_context close_context ;',),
 'tests/host/ipc_notify_model_test.c': ('static const char * const notify_model_operation_names [ '
                                        'NOTIFY_MODEL_OPERATION_COUNT ] = { , , , , , , , } ;',),
 'tests/host/ipc_notify_test_fixture.c': ('static struct ipc_notify_test_context notify_context ;',),
 'tests/host/ipc_reply_test_fixture.c': ('static struct ipc_reply_test_context reply_context ;',),
 'tests/host/ipc_call_test_fixture.c': ('static struct ipc_call_test_context call_context ;',),
 'tests/host/ipc_operation_test_fixture.c': ('static struct ipc_operation_test_context operation_context ;',)}

CURRENT_STATIC_LOCAL_OBJECTS = {'tests/host/ipc_model_main.c': ('static const struct { const char * name ; bool ( * run ) ( void ) ; } '
                                 'tests [ ] = { { , micros_endpoint_model_test_run , } , { , '
                                 'micros_ipc_notify_model_test_run , } , { , micros_ipc_close_model_test_run '
                                 ', } , { , micros_grant_model_test_run , } , { , micros_ipc_model_test_run '
                                 ', } , } ;',),
 'tests/host/ipc_deadlock_test.c': ('static const struct micros_privilege_profile profiles [ ] = { { . id = '
                                    '1 , . name = , . operations = MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                    'MICROS_PRIVILEGE_OPERATION_CALL | MICROS_PRIVILEGE_OPERATION_SEND | '
                                    'MICROS_PRIVILEGE_OPERATION_REPLY | '
                                    'MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE , . call_targets = UINT32_C ( '
                                    '1 ) << 1 , . send_targets = UINT32_C ( 1 ) << 1 , } , } ;',
                                    'static const struct { const char * name ; bool ( * run ) ( void ) ; } '
                                    'tests [ ] = { { , test_bounded_reference_model , } , { , '
                                    'test_all_endpoint_chain_lengths , } , { , test_receive_and_call_cycles '
                                    ', } , { , test_dependency_precedence_and_any_termination , } , { , '
                                    'test_reply_receive_uses_post_reply_graph , } , { , '
                                    'test_source_only_dependency_is_traversed , } , { , '
                                    'test_projection_and_corruption_are_atomic , } , } ;'),
 'tests/host/ipc_delivery_test.c': ('static const struct micros_privilege_profile profile = { . id = 1 , . '
                                    'name = , . operations = MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                    'MICROS_PRIVILEGE_OPERATION_CALL | MICROS_PRIVILEGE_OPERATION_SEND , . '
                                    'call_targets = UINT32_C ( 1 ) << 1 , . send_targets = UINT32_C ( 1 ) << '
                                    '1 , } ;',),
 'tests/host/ipc_queue_mutation_test.c': ('static const struct micros_privilege_profile profile = { . id = 1 '
                                          ', . name = , . operations = MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                          'MICROS_PRIVILEGE_OPERATION_CALL | MICROS_PRIVILEGE_OPERATION_SEND '
                                          ', . call_targets = UINT32_C ( 1 ) << 1 , . send_targets = '
                                          'UINT32_C ( 1 ) << 1 , } ;',),
 'tests/host/ipc_queue_test.c': ('static const struct micros_privilege_profile profile = { . id = 1 , . name '
                                 '= , . operations = MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                 'MICROS_PRIVILEGE_OPERATION_SEND , . send_targets = UINT32_C ( 1 ) << 1 , } '
                                 ';',),
 'tests/host/ipc_state_test.c': ('static const struct micros_privilege_profile profile = { . id = 1 , . name '
                                 '= , . operations = MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                 'MICROS_PRIVILEGE_OPERATION_SEND , . send_targets = UINT32_C ( 1 ) << 1 , } '
                                 ';',),
 'tests/host/ipc_abi_test.c': ('static const struct { enum micros_ipc_error error ; int64_t result ; } '
                               'mappings [ ] = { { MICROS_IPC_OK , MICROS_IPC_ABI_OK } , { '
                               'MICROS_IPC_ERROR_ARGUMENT , MICROS_IPC_ABI_ARGUMENT } , { '
                               'MICROS_IPC_ERROR_DEAD_ENDPOINT , MICROS_IPC_ABI_DEAD_ENDPOINT } , { '
                               'MICROS_IPC_ERROR_UNAUTHORIZED , MICROS_IPC_ABI_UNAUTHORIZED } , { '
                               'MICROS_IPC_ERROR_STATE , MICROS_IPC_ABI_STATE } , { '
                               'MICROS_IPC_ERROR_DEADLOCK , MICROS_IPC_ABI_DEADLOCK } , { '
                               'MICROS_IPC_ERROR_MESSAGE_FAULT , MICROS_IPC_ABI_MESSAGE_FAULT } , { '
                               'MICROS_IPC_ERROR_REPLY_TOKEN , MICROS_IPC_ABI_REPLY_TOKEN } , { '
                               'MICROS_IPC_ERROR_REPLY_TOKEN_EXHAUSTED , '
                               'MICROS_IPC_ABI_REPLY_TOKEN_EXHAUSTED , } , { '
                               'MICROS_IPC_ERROR_ENDPOINT_CLOSING , MICROS_IPC_ABI_ENDPOINT_CLOSING , } , } '
                               ';',),
 'kernel/tests/ipc/ipc_syscall_panic_test.c': ('static const struct micros_privilege_profile profiles [ ] = '
                                               '{ { . id = 1 , . name = , . operations = '
                                               'MICROS_PRIVILEGE_OPERATION_RECEIVE , } , { . id = 2 , . name '
                                               '= , . operations = MICROS_PRIVILEGE_OPERATION_SEND , . '
                                               'send_targets = UINT32_C ( 1 ) << 1 , } , } ;',),
 'kernel/tests/ipc/ipc_syscall_test_setup.c': ('static const struct micros_privilege_profile profiles [ ] = '
                                               '{ { . id = 1 , . name = , . operations = '
                                               'MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                               'MICROS_PRIVILEGE_OPERATION_SEND | '
                                               'MICROS_PRIVILEGE_OPERATION_CALL | '
                                               'MICROS_PRIVILEGE_OPERATION_REPLY , . call_targets = UINT32_C '
                                               '( 1 ) << 2 , . send_targets = UINT32_C ( 1 ) << 2 , } , { . '
                                               'id = 2 , . name = , . operations = '
                                               'MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                               'MICROS_PRIVILEGE_OPERATION_SEND | '
                                               'MICROS_PRIVILEGE_OPERATION_CALL | '
                                               'MICROS_PRIVILEGE_OPERATION_REPLY | '
                                               'MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE , . call_targets = '
                                               'UINT32_C ( 1 ) << 1 , . send_targets = UINT32_C ( 1 ) << 3 , '
                                               '} , { . id = 3 , . name = , . operations = '
                                               'MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                               'MICROS_PRIVILEGE_OPERATION_SEND | '
                                               'MICROS_PRIVILEGE_OPERATION_NOTIFY , . send_targets = '
                                               'UINT32_C ( 1 ) << 2 , . notify_targets = UINT32_C ( 1 ) << 2 '
                                               ', } , } ;',),
 'kernel/tests/ipc/ipc_test_setup.c': ('static const struct micros_privilege_profile profiles [ ] = { { . id '
                                       '= IPC_TEST_CLIENT_PROFILE , . name = , . operations = '
                                       'MICROS_PRIVILEGE_OPERATION_RECEIVE | MICROS_PRIVILEGE_OPERATION_SEND '
                                       '| MICROS_PRIVILEGE_OPERATION_CALL | '
                                       'MICROS_PRIVILEGE_OPERATION_NOTIFY , . call_targets = UINT32_C ( 1 ) '
                                       '<< IPC_TEST_SERVER_PROFILE , . send_targets = UINT32_C ( 1 ) << '
                                       'IPC_TEST_SERVER_PROFILE , . notify_targets = UINT32_C ( 1 ) << '
                                       'IPC_TEST_SERVER_PROFILE , } , { . id = IPC_TEST_SERVER_PROFILE , . '
                                       'name = , . operations = MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                       'MICROS_PRIVILEGE_OPERATION_SEND | MICROS_PRIVILEGE_OPERATION_REPLY | '
                                       'MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE | '
                                       'MICROS_PRIVILEGE_OPERATION_NOTIFY , . send_targets = UINT32_C ( 1 ) '
                                       '<< IPC_TEST_CLIENT_PROFILE , . notify_targets = UINT32_C ( 1 ) << '
                                       'IPC_TEST_CLIENT_PROFILE , } , { . id = IPC_TEST_PEER_PROFILE , . '
                                       'name = , . operations = MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                       'MICROS_PRIVILEGE_OPERATION_SEND | MICROS_PRIVILEGE_OPERATION_CALL | '
                                       'MICROS_PRIVILEGE_OPERATION_NOTIFY , . call_targets = UINT32_C ( 1 ) '
                                       '<< IPC_TEST_SERVER_PROFILE , . send_targets = UINT32_C ( 1 ) << '
                                       'IPC_TEST_SERVER_PROFILE , . notify_targets = UINT32_C ( 1 ) << '
                                       'IPC_TEST_CLIENT_PROFILE , } , } ;',
                                       'static const uint8_t profile_ids [ IPC_TEST_PROCESS_COUNT ] = { '
                                       'IPC_TEST_CLIENT_PROFILE , IPC_TEST_SERVER_PROFILE , '
                                       'IPC_TEST_PEER_PROFILE , } ;'),
 'tests/host/ipc_model_test_trace.c': ('static const char * const names [ MODEL_OPERATION_COUNT ] = { , , , '
                                       ', , , , , , , } ;',),
 'tests/host/ipc_model_test_fixture.c': ('static const struct micros_privilege_profile profiles [ ] = { { . '
                                         'id = MODEL_FULL_PROFILE , . name = , . operations = '
                                         'MICROS_PRIVILEGE_OPERATION_DEFINED_MASK , . call_targets = '
                                         'UINT32_C ( 1 ) << MODEL_FULL_PROFILE , . send_targets = UINT32_C ( '
                                         '1 ) << MODEL_FULL_PROFILE , . notify_targets = UINT32_C ( 1 ) << '
                                         'MODEL_FULL_PROFILE , } , { . id = MODEL_DENIED_PROFILE , . name = '
                                         ', . operations = MICROS_PRIVILEGE_OPERATION_RECEIVE , } , } ;',),
 'tests/host/ipc_model_test_common.c': ('static const struct micros_ipc_message zero_message ;',),
 'tests/host/ipc_close_test_fixture.c': ('static const struct micros_privilege_profile profiles [ ] = { { . '
                                         'id = 1 , . name = , . operations = '
                                         'MICROS_PRIVILEGE_OPERATION_DEFINED_MASK , . call_targets = '
                                         'UINT32_C ( 1 ) << 1 , . send_targets = UINT32_C ( 1 ) << 1 , . '
                                         'notify_targets = UINT32_C ( 1 ) << 1 , } , } ;',),
 'tests/host/ipc_close_test.c': ('static const struct { const char * name ; bool ( * run ) ( struct '
                                 'ipc_close_test_context * ) ; } tests [ ] = { { , '
                                 'test_close_accepts_full_capacity_queue , } , { , '
                                 'test_close_cancels_complete_transaction , } , { , '
                                 'test_cascading_close_discards_owned_dead_endpoint_result , } , { , '
                                 'test_source_only_close_cancels_queued_sender , } , { , '
                                 'test_source_only_notification_dependency_blocks_strict_close , } , { , '
                                 'test_successful_staged_deliveries_block_close_atomically , } , { , '
                                 'test_close_failures_are_atomic , } , } ;',),
 'tests/host/ipc_notify_model_test.c': ('static const enum notify_model_operation scripted_operations [ ] = '
                                        '{ NOTIFY_MODEL_NOTIFY , NOTIFY_MODEL_NOTIFY , '
                                        'NOTIFY_MODEL_KERNEL_NOTIFY , NOTIFY_MODEL_KERNEL_NOTIFY , '
                                        'NOTIFY_MODEL_RECEIVE_ANY , NOTIFY_MODEL_NOTIFY , '
                                        'NOTIFY_MODEL_RECEIVE_ANY , NOTIFY_MODEL_RECEIVE_SPECIFIC , '
                                        'NOTIFY_MODEL_ZERO_REJECTED , NOTIFY_MODEL_OPERATION_REJECTED , '
                                        'NOTIFY_MODEL_TARGET_REJECTED , } ;',
                                        'static const size_t scripted_sources [ ] = { 0 , 0 , 0 , 0 , 0 , 1 '
                                        ', 0 , 1 , 0 , 0 , 0 , } ;'),
 'tests/host/ipc_notify_test_fixture.c': ('static const struct micros_privilege_profile profiles [ ] = { { . '
                                          'id = 1 , . name = , . operations = '
                                          'MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                          'MICROS_PRIVILEGE_OPERATION_NOTIFY , . notify_targets = ( UINT32_C '
                                          '( 1 ) << 2 ) | ( UINT32_C ( 1 ) << 4 ) , } , { . id = 2 , . name '
                                          '= , . operations = MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                          'MICROS_PRIVILEGE_OPERATION_CALL , . call_targets = UINT32_C ( 1 ) '
                                          '<< 4 , } , { . id = 3 , . name = , . operations = '
                                          'MICROS_PRIVILEGE_OPERATION_SEND , . send_targets = ( UINT32_C ( 1 '
                                          ') << 2 ) | ( UINT32_C ( 1 ) << 4 ) , } , { . id = 4 , . name = , '
                                          '. operations = MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                          'MICROS_PRIVILEGE_OPERATION_REPLY | '
                                          'MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE , } , { . id = 5 , . '
                                          'name = , . operations = MICROS_PRIVILEGE_OPERATION_RECEIVE , } , '
                                          '{ . id = 6 , . name = , . operations = '
                                          'MICROS_PRIVILEGE_OPERATION_NOTIFY , . notify_targets = UINT32_C ( '
                                          '1 ) << 6 , } , } ;',
                                          'static const uint8_t profile_ids [ NOTIFY_PROCESS_COUNT ] = { 1 , '
                                          '1 , 2 , 3 , 4 , 5 , 6 , 1 , } ;'),
 'tests/host/ipc_notify_test.c': ('static const struct { const char * name ; bool ( * run ) ( struct '
                                  'ipc_notify_test_context * ) ; } tests [ ] = { { , '
                                  'test_notify_coalesces_and_never_blocks , } , { , '
                                  'test_kernel_notify_coalesces_and_any_receives , } , { , '
                                  'test_kernel_notify_wakes_only_any_receiver , } , { , '
                                  'test_kernel_notify_prepare_commit_is_retained , } , { , '
                                  'test_staged_kernel_notification_is_detected , } , { , '
                                  'test_kernel_notify_rejections_are_atomic , } , { , '
                                  'test_kernel_notify_validation_and_close_rules , } , { , '
                                  'test_notify_wakes_first_matching_receiver , } , { , '
                                  'test_call_reply_wait_does_not_consume_notification , } , { , '
                                  'test_receive_prefers_lowest_notification_before_sender , } , { , '
                                  'test_reply_receive_prefers_pending_notification , } , { , '
                                  'test_reply_receive_pending_failure_is_atomic , } , { , '
                                  'test_notification_validators_and_close_preflight , } , { , '
                                  'test_notify_rejections_preserve_state , } , { , '
                                  'test_notify_scheduler_failure_is_atomic , } , } ;',),
 'tests/host/ipc_reply_test_fixture.c': ('static const struct micros_privilege_profile profiles [ ] = { { . '
                                         'id = 1 , . name = , . operations = MICROS_PRIVILEGE_OPERATION_CALL '
                                         ', . call_targets = ( UINT32_C ( 1 ) << 2 ) | ( UINT32_C ( 1 ) << 3 '
                                         ') | ( UINT32_C ( 1 ) << 4 ) | ( UINT32_C ( 1 ) << 5 ) , } , { . id '
                                         '= 2 , . name = , . operations = MICROS_PRIVILEGE_OPERATION_RECEIVE '
                                         '| MICROS_PRIVILEGE_OPERATION_SEND | '
                                         'MICROS_PRIVILEGE_OPERATION_REPLY | '
                                         'MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE , . send_targets = '
                                         'UINT32_C ( 1 ) << 2 , } , { . id = 3 , . name = , . operations = '
                                         'MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                         'MICROS_PRIVILEGE_OPERATION_SEND , . send_targets = UINT32_C ( 1 ) '
                                         '<< 2 , } , { . id = 4 , . name = , . operations = '
                                         'MICROS_PRIVILEGE_OPERATION_REPLY , } , { . id = 5 , . name = , . '
                                         'operations = MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE , } , } ;',
                                         'static const uint8_t profile_ids [ REPLY_PROCESS_COUNT ] = { 1 , 2 '
                                         ', 2 , 3 , 4 , 5 , 2 , } ;'),
 'tests/host/ipc_reply_test.c': ('static const struct { const char * name ; bool ( * run ) ( struct '
                                 'ipc_reply_test_context * ) ; } tests [ ] = { { , '
                                 'test_staged_call_tokens_are_unique , } , { , '
                                 'test_reply_requires_request_return , } , { , '
                                 'test_reply_routes_exact_tokens_without_send_target , } , { , '
                                 'test_reply_preserves_independent_caller_flags , } , { , '
                                 'test_reply_rejections_are_atomic , } , { , '
                                 'test_reply_scheduler_failure_preserves_token , } , { , '
                                 'test_reply_rejects_source_only_caller , } , { , '
                                 'test_reply_receive_blocks_and_later_receives , } , { , '
                                 'test_reply_receive_selects_specific_sender_atomically , } , { , '
                                 'test_reply_receive_requires_combined_operation , } , { , '
                                 'test_reply_receive_rejections_are_atomic , } , { , '
                                 'test_reply_receive_scheduler_failures_are_atomic , } , } ;',),
 'tests/host/ipc_call_test_fixture.c': ('static const struct micros_privilege_profile profiles [ ] = { { . '
                                        'id = 1 , . name = , . operations = MICROS_PRIVILEGE_OPERATION_CALL '
                                        ', . call_targets = UINT32_C ( 1 ) << 2 , } , { . id = 2 , . name = '
                                        ', . operations = MICROS_PRIVILEGE_OPERATION_RECEIVE , } , { . id = '
                                        '3 , . name = , . operations = MICROS_PRIVILEGE_OPERATION_RECEIVE , '
                                        '} , { . id = 4 , . name = , . operations = '
                                        'MICROS_PRIVILEGE_OPERATION_CALL , . call_targets = UINT32_C ( 1 ) '
                                        '<< 4 , } , } ;',
                                        'static const uint8_t profile_ids [ CALL_PROCESS_COUNT ] = { 1 , 2 , '
                                        '3 , 4 , 2 , } ;'),
 'tests/host/ipc_call_test.c': ('static const struct { const char * name ; bool ( * run ) ( struct '
                                'ipc_call_test_context * ) ; } tests [ ] = { { , '
                                'test_call_delivers_to_waiting_receiver , } , { , '
                                'test_call_preserves_independent_receiver_flags , } , { , '
                                'test_queued_calls_bind_exact_threads_and_deliver_fifo , } , { , '
                                'test_low_level_enqueue_rejects_reply_tokens , } , { , '
                                'test_token_exhaustion_is_preflighted , } , { , '
                                'test_call_rejections_are_atomic , } , { , '
                                'test_staged_call_token_binding_is_validated , } , { , '
                                'test_allocator_validator_rejects_unissued_token , } , } ;',),
 'tests/host/ipc_operation_test_fixture.c': ('static const struct micros_privilege_profile profiles [ ] = { '
                                             '{ . id = 1 , . name = , . operations = '
                                             'MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                             'MICROS_PRIVILEGE_OPERATION_SEND , . send_targets = UINT32_C ( '
                                             '1 ) << 2 , } , { . id = 2 , . name = , . operations = '
                                             'MICROS_PRIVILEGE_OPERATION_RECEIVE | '
                                             'MICROS_PRIVILEGE_OPERATION_SEND , . send_targets = UINT32_C ( '
                                             '1 ) << 1 , } , { . id = 3 , . name = , . operations = '
                                             'MICROS_PRIVILEGE_OPERATION_RECEIVE , } , { . id = 4 , . name = '
                                             ', . operations = MICROS_PRIVILEGE_OPERATION_SEND , . '
                                             'send_targets = UINT32_C ( 1 ) << 4 , } , } ;',
                                             'static const uint8_t profile_ids [ OPERATION_PROCESS_COUNT ] = '
                                             '{ 1 , 1 , 2 , 3 , 4 , 1 , } ;')}

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

SOURCE_SETS = {
    "MICROS_IPC_CORE_SOURCES": (
        'kernel/ipc/ipc_common.c',
        'kernel/ipc/ipc_queue.c',
        'kernel/ipc/ipc_completion.c',
        'kernel/ipc/ipc_tokens.c',
        'kernel/ipc/ipc_notifications.c',
        'kernel/ipc/ipc_deadlock.c',
        'kernel/ipc/ipc_rendezvous.c',
        'kernel/ipc/ipc_close.c',
        'kernel/ipc/ipc_reply.c',
        'kernel/ipc/ipc.c',
    ),
    "MICROS_IPC_SYSCALL_SOURCES": (
        'kernel/ipc/ipc_syscall_result.c',
        'kernel/ipc/ipc_syscall_request.c',
        'kernel/ipc/ipc_syscall.c',
    ),
    "MICROS_IPC_OPERATION_HOST_TEST_SOURCES": (
        "tests/host/ipc_operation_test.c",
        "tests/host/ipc_operation_test_fixture.c",
        "tests/host/ipc_send_operation_test.c",
        "tests/host/ipc_receive_operation_test.c",
    ),
    "MICROS_IPC_CALL_HOST_TEST_SOURCES": (
        "tests/host/ipc_call_test.c",
        "tests/host/ipc_call_test_fixture.c",
        "tests/host/ipc_call_delivery_test.c",
        "tests/host/ipc_call_validation_test.c",
    ),
    "MICROS_IPC_REPLY_HOST_TEST_SOURCES": (
        "tests/host/ipc_reply_test.c",
        "tests/host/ipc_reply_test_fixture.c",
        "tests/host/ipc_reply_authority_test.c",
        "tests/host/ipc_reply_validation_test.c",
        "tests/host/ipc_reply_receive_test.c",
    ),
    "MICROS_IPC_NOTIFY_HOST_TEST_SOURCES": (
        "tests/host/ipc_notify_test.c",
        "tests/host/ipc_notify_test_fixture.c",
        "tests/host/ipc_notify_delivery_test.c",
        "tests/host/ipc_kernel_notify_test.c",
        "tests/host/ipc_notify_validation_test.c",
        "tests/host/ipc_notify_model_test.c",
    ),
    "MICROS_IPC_CLOSE_HOST_TEST_SOURCES": (
        "tests/host/ipc_close_test.c",
        "tests/host/ipc_close_test_fixture.c",
        "tests/host/ipc_close_cancellation_test.c",
        "tests/host/ipc_close_validation_test.c",
        "tests/host/ipc_close_model_test.c",
    ),
    "MICROS_IPC_MODEL_HOST_TEST_SOURCES": (
        "tests/host/ipc_model_test.c",
        "tests/host/ipc_model_test_common.c",
        "tests/host/ipc_model_test_fixture.c",
        "tests/host/ipc_model_test_trace.c",
        "tests/host/ipc_model_reference_authority.c",
        "tests/host/ipc_model_reference_scheduler.c",
        "tests/host/ipc_model_reference_ipc_state.c",
        "tests/host/ipc_model_reference_rendezvous.c",
        "tests/host/ipc_model_reference_notifications.c",
        "tests/host/ipc_model_reference_lifecycle.c",
        "tests/host/ipc_model_reference.c",
        "tests/host/ipc_model_test_production.c",
        "tests/host/ipc_model_test_step.c",
        "tests/host/ipc_model_test_actions.c",
        "tests/host/ipc_model_test_prelude.c",
    ),
    "MICROS_IPC_QEMU_TEST_SOURCES": (
        'kernel/tests/ipc/ipc_test.c',
        'kernel/tests/ipc/ipc_test_fixture.c',
        'kernel/tests/ipc/ipc_test_setup.c',
        'kernel/tests/ipc/ipc_test_scenarios.c',
        'kernel/tests/ipc/ipc_test_report.c',
    ),
    "MICROS_IPC_ECALL_QEMU_TEST_SOURCES": (
        'kernel/tests/ipc/ipc_ecall_test.c',
        'kernel/tests/ipc/ipc_ecall_test_fixture.c',
        'kernel/tests/ipc/ipc_ecall_test_setup.c',
        'kernel/tests/ipc/ipc_ecall_test_report.c',
    ),
    "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES": (
        'kernel/tests/ipc/ipc_syscall_test.c',
        'kernel/tests/ipc/ipc_syscall_test_fixture.c',
        'kernel/tests/ipc/ipc_syscall_test_setup.c',
        'kernel/tests/ipc/ipc_syscall_test_commands.c',
        'kernel/tests/ipc/ipc_syscall_test_scenarios.c',
        'kernel/tests/ipc/ipc_syscall_test_hooks.c',
        'kernel/tests/ipc/ipc_syscall_test_report.c',
    ),
}

PRIVATE_HEADERS = {
    "MICROS_IPC_CORE_SOURCES": 'kernel/ipc/ipc_internal.h',
    "MICROS_IPC_SYSCALL_SOURCES": 'kernel/ipc/ipc_syscall_internal.h',
    "MICROS_IPC_OPERATION_HOST_TEST_SOURCES": "tests/host/ipc_operation_test_internal.h",
    "MICROS_IPC_CALL_HOST_TEST_SOURCES": "tests/host/ipc_call_test_internal.h",
    "MICROS_IPC_REPLY_HOST_TEST_SOURCES": "tests/host/ipc_reply_test_internal.h",
    "MICROS_IPC_NOTIFY_HOST_TEST_SOURCES": "tests/host/ipc_notify_test_internal.h",
    "MICROS_IPC_CLOSE_HOST_TEST_SOURCES": "tests/host/ipc_close_test_internal.h",
    "MICROS_IPC_MODEL_HOST_TEST_SOURCES": "tests/host/ipc_model_test_internal.h",
    "MICROS_IPC_QEMU_TEST_SOURCES": 'kernel/tests/ipc/ipc_test_internal.h',
    "MICROS_IPC_ECALL_QEMU_TEST_SOURCES": 'kernel/tests/ipc/ipc_ecall_test_internal.h',
    "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES": 'kernel/tests/ipc/ipc_syscall_test_internal.h',
}

FINAL_OWNER_GROUPS = {
    'kernel/ipc/ipc_common.c': ("micros_ipc_thread_has_staged_kernel_notification",),
    'kernel/ipc/ipc_completion.c': ("micros_ipc_stage_no_message_completion",),
    'kernel/ipc/ipc_notifications.c': (
        "micros_ipc_notify", "micros_ipc_inject_kernel_notification",
        "micros_ipc_prepare_kernel_notification",
        "micros_ipc_commit_kernel_notification_prevalidated",
    ),
    'kernel/ipc/ipc_rendezvous.c': (
        "micros_ipc_sender_enqueue", "micros_ipc_receiver_enqueue",
        "micros_ipc_sender_commit_delivery", "micros_ipc_receiver_commit_delivery",
    ),
    'kernel/ipc/ipc_close.c': ("micros_ipc_endpoint_close",),
    'kernel/ipc/ipc_reply.c': (
        "micros_ipc_reply", "micros_ipc_reply_expected_caller", "micros_ipc_reply_receive",
    ),
    "tests/host/ipc_send_operation_test.c": (
        "test_authorized_send_blocks_with_snapshot",
        "test_authorized_send_matches_first_receiver", "test_send_rejections_are_atomic",
    ),
    "tests/host/ipc_receive_operation_test.c": (
        "test_authorized_receive_blocks", "test_receive_specific_and_any_fifo",
        "test_receive_rejections_are_atomic",
    ),
    "tests/host/ipc_call_delivery_test.c": (
        "test_call_delivers_to_waiting_receiver", "test_call_preserves_independent_receiver_flags",
        "test_queued_calls_bind_exact_threads_and_deliver_fifo",
    ),
    "tests/host/ipc_call_validation_test.c": (
        "test_low_level_enqueue_rejects_reply_tokens", "test_token_exhaustion_is_preflighted",
        "test_call_rejections_are_atomic", "test_staged_call_token_binding_is_validated",
        "test_allocator_validator_rejects_unissued_token",
    ),
    "tests/host/ipc_reply_authority_test.c": (
        "test_staged_call_tokens_are_unique", "test_reply_requires_request_return",
        "test_reply_routes_exact_tokens_without_send_target",
        "test_reply_preserves_independent_caller_flags",
    ),
    "tests/host/ipc_reply_validation_test.c": (
        "test_reply_rejections_are_atomic", "test_reply_scheduler_failure_preserves_token",
        "test_reply_rejects_source_only_caller",
    ),
    "tests/host/ipc_reply_receive_test.c": (
        "test_reply_receive_blocks_and_later_receives",
        "test_reply_receive_selects_specific_sender_atomically",
        "test_reply_receive_requires_combined_operation",
        "test_reply_receive_rejections_are_atomic",
        "test_reply_receive_scheduler_failures_are_atomic",
    ),
    "tests/host/ipc_notify_delivery_test.c": (
        "test_notify_coalesces_and_never_blocks", "test_notify_wakes_first_matching_receiver",
        "test_call_reply_wait_does_not_consume_notification",
        "test_receive_prefers_lowest_notification_before_sender",
        "test_reply_receive_prefers_pending_notification",
    ),
    "tests/host/ipc_kernel_notify_test.c": (
        "test_kernel_notify_coalesces_and_any_receives",
        "test_kernel_notify_wakes_only_any_receiver",
        "test_kernel_notify_prepare_commit_is_retained",
        "test_staged_kernel_notification_is_detected",
        "test_kernel_notify_rejections_are_atomic",
        "test_kernel_notify_validation_and_close_rules",
    ),
    "tests/host/ipc_notify_validation_test.c": (
        "test_reply_receive_pending_failure_is_atomic",
        "test_notification_validators_and_close_preflight",
        "test_notify_rejections_preserve_state", "test_notify_scheduler_failure_is_atomic",
    ),
    "tests/host/ipc_notify_model_test.c": ("test_seeded_notify_model",),
    "tests/host/ipc_close_cancellation_test.c": (
        "test_close_cancels_complete_transaction",
        "test_cascading_close_discards_owned_dead_endpoint_result",
        "test_source_only_close_cancels_queued_sender",
    ),
    "tests/host/ipc_close_validation_test.c": (
        "test_source_only_notification_dependency_blocks_strict_close",
        "test_successful_staged_deliveries_block_close_atomically",
        "test_close_failures_are_atomic",
    ),
    "tests/host/ipc_close_model_test.c": ("test_seeded_close_scenarios",),
    'kernel/tests/ipc/ipc_syscall_test_hooks.c': (
        "micros_ipc_syscall_test_before_ecall", "micros_ipc_syscall_test_after_return",
        "micros_ipc_syscall_test_before_timer_return", "micros_ipc_syscall_test_after_timer_return",
    ),
    'kernel/tests/ipc/ipc_syscall_test_report.c': ("micros_ipc_syscall_test_finish",),
    'kernel/tests/ipc/ipc_ecall_test_report.c': (
        "micros_ipc_ecall_core_test_finish", "micros_ipc_ecall_core_test_finish_trap",
    ),
}

DECLARATION_ONLY_HEADERS = {
    *PRIVATE_HEADERS.values(), *(path for path in PUBLIC_PATHS if path.endswith(".h")),
    "include/micros/endpoint.h", "include/micros/scheduler_core.h",
    "include/micros/kernel_objects.h", "include/micros/kernel_object_runtime.h",
    "include/micros/scheduler.h", 'kernel/trap/syscall.h',
    'kernel/endpoint/endpoint_internal.h', 'kernel/scheduler/scheduler_core_internal.h',
    'kernel/boot/bootstrap_image.h', 'kernel/boot/bootstrap_runtime.h',
    'kernel/objects/kernel_object_runtime_internal.h',
}

CASE_LABELS = {
    "call": (
        ("call delivers to waiting receiver", "test_call_delivers_to_waiting_receiver"),
        ("call preserves independent receiver flags", "test_call_preserves_independent_receiver_flags"),
        ("queued calls bind exact threads and deliver FIFO", "test_queued_calls_bind_exact_threads_and_deliver_fifo"),
        ("low-level enqueue rejects reply tokens", "test_low_level_enqueue_rejects_reply_tokens"),
        ("token exhaustion is preflighted", "test_token_exhaustion_is_preflighted"),
        ("call rejections are atomic", "test_call_rejections_are_atomic"),
        ("staged call token binding is validated", "test_staged_call_token_binding_is_validated"),
        ("allocator validator rejects unissued token", "test_allocator_validator_rejects_unissued_token"),
    ),
    "reply": (
        ("staged call tokens are unique", "test_staged_call_tokens_are_unique"),
        ("reply requires request return", "test_reply_requires_request_return"),
        ("reply routes exact tokens without send target", "test_reply_routes_exact_tokens_without_send_target"),
        ("reply preserves independent caller flags", "test_reply_preserves_independent_caller_flags"),
        ("reply rejections are atomic", "test_reply_rejections_are_atomic"),
        ("reply scheduler failure preserves token", "test_reply_scheduler_failure_preserves_token"),
        ("reply rejects source-only caller", "test_reply_rejects_source_only_caller"),
        ("reply receive blocks and later receives", "test_reply_receive_blocks_and_later_receives"),
        ("reply receive selects specific sender atomically", "test_reply_receive_selects_specific_sender_atomically"),
        ("reply receive requires combined operation", "test_reply_receive_requires_combined_operation"),
        ("reply receive rejections are atomic", "test_reply_receive_rejections_are_atomic"),
        ("reply receive scheduler failures are atomic", "test_reply_receive_scheduler_failures_are_atomic"),
    ),
    "notify": (
        ("notify coalesces and never blocks", "test_notify_coalesces_and_never_blocks"),
        ("kernel notify coalesces and any receives", "test_kernel_notify_coalesces_and_any_receives"),
        ("kernel notify wakes only any receiver", "test_kernel_notify_wakes_only_any_receiver"),
        ("kernel notify prepare commit is retained", "test_kernel_notify_prepare_commit_is_retained"),
        ("staged kernel notification is detected", "test_staged_kernel_notification_is_detected"),
        ("kernel notify rejections are atomic", "test_kernel_notify_rejections_are_atomic"),
        ("kernel notify validation and close rules", "test_kernel_notify_validation_and_close_rules"),
        ("notify wakes first matching receiver", "test_notify_wakes_first_matching_receiver"),
        ("call reply wait excludes notification", "test_call_reply_wait_does_not_consume_notification"),
        ("receive prefers lowest notification before sender", "test_receive_prefers_lowest_notification_before_sender"),
        ("reply receive prefers pending notification", "test_reply_receive_prefers_pending_notification"),
        ("reply receive pending failure is atomic", "test_reply_receive_pending_failure_is_atomic"),
        ("notification validators and close preflight", "test_notification_validators_and_close_preflight"),
        ("notify rejections preserve state", "test_notify_rejections_preserve_state"),
        ("notify scheduler failure is atomic", "test_notify_scheduler_failure_is_atomic"),
    ),
    "close": (
        ("full-capacity queue terminates at its tail", "test_close_accepts_full_capacity_queue"),
        ("complete cancellation transaction", "test_close_cancels_complete_transaction"),
        ("cascading close discards owned dead-endpoint result", "test_cascading_close_discards_owned_dead_endpoint_result"),
        ("source-only close cancels queued sender", "test_source_only_close_cancels_queued_sender"),
        ("source-only notification dependency blocks strict close", "test_source_only_notification_dependency_blocks_strict_close"),
        ("successful staged deliveries block close atomically", "test_successful_staged_deliveries_block_close_atomically"),
        ("stale corrupt and late failures are atomic", "test_close_failures_are_atomic"),
    ),
}

SYSCALL_SCRIPT = (
    ("CLIENT", "CLIENT_SEND_ONE"),
    ("SERVER", "SERVER_RECEIVE_SEND_ONE"),
    ("SERVER", "SERVER_WAIT_CALL_ONE"),
    ("CLIENT", "CLIENT_CALL_ONE"),
    ("SERVER", "SERVER_REPLY_ONE"),
    ("SERVER", "SERVER_WAIT_CALL_TWO"),
    ("CLIENT", "CLIENT_CALL_TWO"),
    ("SERVER", "SERVER_REPLY_RECEIVE_TWO"),
    ("CLIENT", "CLIENT_WAIT_SERVER_CALL"),
    ("PEER", "PEER_SEND"),
    ("SERVER", "SERVER_CALL_CLIENT"),
    ("PEER", "PEER_NOTIFY_ONE"),
    ("PEER", "PEER_NOTIFY_TWO"),
    ("PEER", "PEER_BLOCK"),
    ("CLIENT", "CLIENT_REPLY_THREE"),
    ("SERVER", "SERVER_RECEIVE_NOTIFY"),
    ("SERVER", "SERVER_WAKE_PEER"),
    ("SERVER", "SERVER_BLOCK_FINAL"),
    ("CLIENT", "CLIENT_ERROR_UNAUTHORIZED"),
    ("CLIENT", "CLIENT_ERROR_ARGUMENT"),
    ("CLIENT", "CLIENT_ERROR_FAULT"),
)

COMMON = 'kernel/ipc/ipc_common.c'

QUEUE = 'kernel/ipc/ipc_queue.c'

COMPLETION = 'kernel/ipc/ipc_completion.c'

TOKENS = 'kernel/ipc/ipc_tokens.c'

NOTIFICATIONS = 'kernel/ipc/ipc_notifications.c'

DEADLOCK = 'kernel/ipc/ipc_deadlock.c'

MODEL_COMMON = "tests/host/ipc_model_test_common.c"

MODEL_FIXTURE = "tests/host/ipc_model_test_fixture.c"

MODEL_REFERENCE = "tests/host/ipc_model_reference.c"

MODEL_PRIMITIVES = (
    "tests/host/ipc_model_reference_authority.c",
    "tests/host/ipc_model_reference_scheduler.c",
    "tests/host/ipc_model_reference_ipc_state.c",
)

MODEL_OPERATIONS = (
    "tests/host/ipc_model_reference_rendezvous.c",
    "tests/host/ipc_model_reference_notifications.c",
    "tests/host/ipc_model_reference_lifecycle.c",
)

PERMITTED_OWNER_DEPENDENCIES = {
    COMMON: (),
    QUEUE: (COMMON,),
    COMPLETION: (COMMON,),
    TOKENS: (COMMON, COMPLETION),
    NOTIFICATIONS: (COMMON, QUEUE, COMPLETION),
    DEADLOCK: (COMMON,),
    'kernel/ipc/ipc_rendezvous.c': (COMMON, QUEUE, COMPLETION, TOKENS, NOTIFICATIONS),
    'kernel/ipc/ipc_close.c': (COMMON,),
    'kernel/ipc/ipc_reply.c': (COMMON, QUEUE, COMPLETION, TOKENS, NOTIFICATIONS, DEADLOCK,
                             'kernel/ipc/ipc_rendezvous.c'),
    'kernel/ipc/ipc.c': (COMMON, QUEUE, TOKENS, DEADLOCK, 'kernel/ipc/ipc_rendezvous.c'),
    'kernel/ipc/ipc_syscall_result.c': (),
    'kernel/ipc/ipc_syscall_request.c': ('kernel/ipc/ipc_syscall_result.c', *SOURCE_SETS["MICROS_IPC_CORE_SOURCES"]),
    'kernel/ipc/ipc_syscall.c': (
        'kernel/ipc/ipc_syscall_result.c', 'kernel/ipc/ipc_syscall_request.c',
        *SOURCE_SETS["MICROS_IPC_CORE_SOURCES"],
    ),
    MODEL_COMMON: (),
    **{path: (MODEL_COMMON,) for path in MODEL_PRIMITIVES},
    **{path: (MODEL_COMMON, *MODEL_PRIMITIVES) for path in MODEL_OPERATIONS},
    MODEL_REFERENCE: (MODEL_COMMON, *MODEL_PRIMITIVES, *MODEL_OPERATIONS),
    MODEL_FIXTURE: (MODEL_COMMON, MODEL_REFERENCE),
    "tests/host/ipc_model_test_trace.c": (MODEL_COMMON,),
    "tests/host/ipc_model_test_production.c": (MODEL_COMMON, *SOURCE_SETS["MICROS_IPC_CORE_SOURCES"]),
    "tests/host/ipc_model_test_step.c": (
        MODEL_REFERENCE, "tests/host/ipc_model_test_production.c", "tests/host/ipc_model_test_trace.c",
    ),
    "tests/host/ipc_model_test_actions.c": (
        MODEL_COMMON, MODEL_FIXTURE, "tests/host/ipc_model_reference_authority.c",
    ),
    "tests/host/ipc_model_test_prelude.c": (MODEL_COMMON, "tests/host/ipc_model_test_actions.c", "tests/host/ipc_model_test_step.c"),
    "tests/host/ipc_model_test.c": (
        MODEL_COMMON, MODEL_FIXTURE, "tests/host/ipc_model_test_prelude.c",
        "tests/host/ipc_model_test_actions.c", "tests/host/ipc_model_test_step.c",
        "tests/host/ipc_model_test_trace.c",
    ),
}

for family in ("OPERATION", "CALL", "REPLY", "NOTIFY", "CLOSE"):
    paths = SOURCE_SETS[f"MICROS_IPC_{family}_HOST_TEST_SOURCES"]
    for path in paths:
        PERMITTED_OWNER_DEPENDENCIES[path] = (
            (*paths, *SOURCE_SETS["MICROS_IPC_CORE_SOURCES"]) if path == paths[0]
            else (*SOURCE_SETS["MICROS_IPC_CORE_SOURCES"],)
            if path == paths[1] else (paths[1], *SOURCE_SETS["MICROS_IPC_CORE_SOURCES"])
        )
for group in ("MICROS_IPC_QEMU_TEST_SOURCES", "MICROS_IPC_ECALL_QEMU_TEST_SOURCES", "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES"):
    paths = SOURCE_SETS[group]
    facade, fixture, setup = paths[:3]
    for path in paths:
        suffix = Path(path).stem
        dependencies = (
            paths if path == facade and group == "MICROS_IPC_QEMU_TEST_SOURCES"
            else (fixture, setup) if path == facade
            else () if path == fixture
            else (fixture, "kernel/tests/ipc/ipc_syscall_test_commands.c")
            if group == "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES" and path == setup
            else (fixture, "kernel/tests/ipc/ipc_syscall_test_scenarios.c") if suffix.endswith("_hooks")
            else (fixture, setup) if suffix.endswith("_report")
            else (fixture, "kernel/tests/ipc/ipc_syscall_test_commands.c")
            if group == "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES" and suffix.endswith("_scenarios")
            else (fixture,)
        )
        PERMITTED_OWNER_DEPENDENCIES[path] = (*dependencies, *SOURCE_SETS["MICROS_IPC_CORE_SOURCES"])


def all_sources() -> tuple[str, ...]:
    return tuple(path for paths in SOURCE_SETS.values() for path in paths)

def accepted_family_paths() -> tuple[str, ...]:
    return tuple(sorted({*all_sources(), *PRIVATE_HEADERS.values(), *PUBLIC_PATHS}))

def discovered_family_paths() -> tuple[str, ...]:
    return tuple(sorted({
        str(path.relative_to(REPOSITORY_ROOT))
        for pattern in ("kernel/ipc/ipc*.[ch]", "kernel/tests/ipc/ipc*.[ch]", "tests/host/ipc*.[ch]",
                        "include/micros/ipc*.h", "arch/riscv64/ipc*.S")
        for path in (REPOSITORY_ROOT).glob(pattern)
    }))

def compiled_source_header_closure() -> tuple[str, ...]:
    return tuple(sorted({
        dependency for path in accepted_family_paths() if path.endswith((".c", ".h"))
        for dependency in vm_layout.local_include_closure(path)
    }))

def storage_inventory(roots: tuple[str, ...]) -> dict[str, tuple[str, ...]]:
    inventory = {}
    for path in sorted({dependency for root in roots for dependency in vm_layout.local_include_closure(root)}):
        objects = file_scope_objects(current_text(REPOSITORY_ROOT / path))
        if objects:
            inventory[path] = objects
    return inventory

def owner_dependency_text(relative: str) -> str:
    selected = []
    for dependency in vm_layout.local_include_closure(relative):
        text = current_text(REPOSITORY_ROOT / dependency)
        if dependency in DECLARATION_ONLY_HEADERS and dependency != relative:
            text = macro_definition_text(text)
        selected.append(re.sub(
            r"\b(?:struct|union|enum)\s+[A-Za-z_][A-Za-z0-9_]*", "", c_lexical_text(text),
        ))
    return "\n".join(selected)

def private_function_names(text):
    declarations = re.sub(r"\b_Static_assert\s*\([^;]*\)\s*;", "", c_declaration_text(text))
    return tuple(name for name in re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", declarations)
                 if name not in ("_Alignas", "_Alignof", "sizeof"))


class KernelIpcSourceLayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.definitions = {
            path: c_function_definitions(current_text(REPOSITORY_ROOT / path))
            for path in all_sources()
        }

    def test_recursive_header_scan_keeps_hidden_calls_and_storage(self) -> None:
        with isolated_source_view(), TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            (root / "owner.c").write_text('#include "forward.h"\nvoid owner(void) {}\n')
            (root / "forward.h").write_text('#include "nested.h"\n')
            (root / "nested.h").write_text(
                "struct micros_ipc_reply;\n"
                "#define HIDDEN_CALL micros_ipc_endpoint_close\n"
                "static inline void hidden(void) { micros_ipc_reply(0); }\n"
                "static struct ipc_model_test_context duplicate = {0};\n",
            )
            with patch(f"{__name__}.REPOSITORY_ROOT", root), \
                    patch.object(vm_layout, "REPOSITORY_ROOT", root):
                dependency = owner_dependency_text("owner.c")
                storage = storage_inventory(("owner.c",))
            self.assertIn("micros_ipc_reply(0)", dependency)
            self.assertIn("micros_ipc_endpoint_close", dependency)
            self.assertNotIn("struct micros_ipc_reply", dependency)
            self.assertEqual({"nested.h"}, set(storage))

    def test_context_shape_rejects_arrays_aliases_and_codeclarations(self) -> None:
        scalar = r"static\s+struct\s+ipc_model_test_context\s+model_context\s*;"
        for declaration in (
            "static struct ipc_model_test_context model_context[2];",
            "static model_alias model_context;",
            "extern struct ipc_model_test_context model_context = {0};",
        ):
            self.assertEqual(1, len(file_scope_objects(declaration)))
            self.assertIsNone(re.fullmatch(scalar, declaration))
        for declaration in (
            "static struct ipc_model_test_context model_context, hidden;",
            "static struct ipc_model_test_context model_context = {0}, hidden = {0};",
            "static struct ipc_model_test_context model_context[2], hidden[2];",
        ):
            with self.assertRaisesRegex(AssertionError, "multiple objects"):
                file_scope_objects(declaration)

    def test_complete_accepted_source_and_private_header_inventory_exists(self) -> None:
        self.assertEqual(68, len(all_sources()))
        self.assertEqual(11, len(PRIVATE_HEADERS))
        self.assertEqual(
            [],
            [path for path in (*all_sources(), *PRIVATE_HEADERS.values())
             if not (REPOSITORY_ROOT / path).is_file()],
        )
        self.assertEqual(accepted_family_paths(), discovered_family_paths())



    def test_private_headers_have_no_function_or_storage_definitions(self) -> None:
        for path in PRIVATE_HEADERS.values():
            text = current_text(REPOSITORY_ROOT / path)
            self.assertEqual({}, c_function_definitions(text), path)
            self.assertEqual((), file_scope_objects(text), path)
            self.assertNotRegex(c_declaration_text(text), r"\bstatic\b", path)




    def test_current_file_scope_storage_has_exact_approved_owners(self):
        observed = {
            path: tuple(" ".join(tokens(declaration)) for declaration in declarations)
            for path, declarations in storage_inventory(
                tuple(path for path in accepted_family_paths() if path.endswith((".c", ".h"))),
            ).items()
        }
        self.assertEqual(CURRENT_FILE_SCOPE_OBJECTS, observed)

    def test_static_local_storage_and_macros_cannot_add_hidden_state(self):
        observed = {}
        for path in compiled_source_header_closure():
            text = current_text(REPOSITORY_ROOT / path)
            declarations = static_local_storage(text)
            if declarations:
                observed[path] = tuple(" ".join(tokens(declaration)) for declaration in declarations)
            self.assertNotRegex(c_lexical_text(macro_definition_text(text)), r"\bstatic\b", path)
        self.assertEqual(CURRENT_STATIC_LOCAL_OBJECTS, observed)

    def test_recursive_owner_direction_and_private_imports_are_bounded(self) -> None:
        definitions = {
            path: set(self.definitions[path])
            for path in all_sources()
        }
        symbols = {}
        for path, names in definitions.items():
            for name in names:
                symbols.setdefault(name, set()).add(path)
        private_core = set(private_function_names(
            current_text(REPOSITORY_ROOT / 'kernel/ipc/ipc_internal.h')
        ))
        for path in all_sources():
            text = owner_dependency_text(path)
            closure = vm_layout.local_include_closure(path)
            allowed = {path, *PERMITTED_OWNER_DEPENDENCIES[path]}
            violations = {
                name: sorted(owners) for name, owners in symbols.items()
                if name not in definitions[path] and not owners & allowed
                and re.search(rf"\b{re.escape(name)}\b", text)
            }
            self.assertEqual({}, violations, path)
            if path not in SOURCE_SETS["MICROS_IPC_CORE_SOURCES"]:
                self.assertNotIn('kernel/ipc/ipc_internal.h', closure, path)
                for name in private_core:
                    self.assertNotRegex(text, rf"\b{re.escape(name)}\b", (path, name))
            if path.startswith("kernel/") and "_test" not in path:
                self.assertFalse(
                    set(closure) & (set(PRIVATE_HEADERS.values()) - {
                        'kernel/ipc/ipc_internal.h', 'kernel/ipc/ipc_syscall_internal.h',
                    }),
                    path,
                )
        for path in compiled_source_header_closure():
            self.assertNotRegex(
                current_text(REPOSITORY_ROOT / path),
                r'(?m)^\s*#\s*include\s*[<"][^">]+\.(?:c|py)[">]', path,
            )

    def test_action_selection_rejects_reference_mutators(self) -> None:
        actions_path = "tests/host/ipc_model_test_actions.c"
        actions_dependency = owner_dependency_text(actions_path)
        for call in (
            "ipc_model_hold_thread(0, 0);",
            "ipc_model_drain_thread(0, 0);",
            "ipc_model_stage_delivery(0, 0, 0);",
            "ipc_model_ipc_append(0, 0, 0, 0);",
        ):
            with self.subTest(call=call):
                def dependency_with_mutator(path: str) -> str:
                    return f"{actions_dependency}\n{call}\n" if path == actions_path else ""

                with patch(f"{__name__}.owner_dependency_text",
                           side_effect=dependency_with_mutator):
                    with self.assertRaisesRegex(AssertionError, call.split("(")[0]):
                        self.test_recursive_owner_direction_and_private_imports_are_bounded()

    def test_reference_decisions_and_action_selection_do_not_execute_production(self) -> None:
        for path in (*MODEL_PRIMITIVES, *MODEL_OPERATIONS):
            text = owner_dependency_text(path)
            self.assertNotRegex(text, r"\bmicros_[A-Za-z0-9_]+\s*\(", path)
        actions = owner_dependency_text("tests/host/ipc_model_test_actions.c")
        for name in (
            "ipc_model_send", "ipc_model_receive", "ipc_model_call", "ipc_model_reply",
            "ipc_model_reply_receive", "ipc_model_notify", "ipc_model_kernel_notify",
            "ipc_model_close_reuse", "ipc_model_apply_transitions", "ipc_model_execute_step",
            "ipc_model_production_execute_action",
        ):
            self.assertNotRegex(actions, rf"\b{name}\b")
        compare = owner_dependency_text(MODEL_REFERENCE)
        self.assertEqual(
            {"micros_kernel_objects_validate", "micros_scheduler_core_validate",
             "micros_endpoint_registry_validate_objects"},
            set(re.findall(r"\b(micros_[A-Za-z0-9_]+)\s*\(", compare)),
        )

    def test_private_declarations_have_one_current_owner(self):
        for path in PRIVATE_HEADERS.values():
            names = private_function_names(current_text(REPOSITORY_ROOT / path))
            for name in names:
                if name != "_Alignas":
                    owners = [source for source in all_sources() if name in self.definitions[source]]
                    self.assertEqual(1, len(owners), (path, name, owners))

    def test_native_case_labels_order_and_existing_executions_are_retained(self) -> None:
        for family, expected in CASE_LABELS.items():
            path = REPOSITORY_ROOT / f"tests/host/ipc_{family}_test.c"
            observed = tuple(re.findall(r'\{\s*"([^"\n]+)"\s*,\s*(test_[A-Za-z_]+)', current_text(path)))
            self.assertEqual(expected, observed, family)
        operation = current_text(REPOSITORY_ROOT / "tests/host/ipc_operation_test.c")
        self.assertEqual(
            (
                "test_authorized_send_blocks_with_snapshot", "test_authorized_send_matches_first_receiver",
                "test_send_rejections_are_atomic", "test_authorized_receive_blocks",
                "test_receive_specific_and_any_fifo", "test_receive_rejections_are_atomic",
            ),
            tuple(re.findall(r"\b(test_[A-Za-z_]+)\s*\(", operation)),
        )
        cmake = current_text(REPOSITORY_ROOT / "CMakeLists.txt")
        self.assertEqual(1, cmake.count("NAME endpoint-profile"))
        self.assertEqual(1, cmake.count("NAME ipc-model"))

    def test_acceptance_model_seed_draw_trace_and_comparison_order_are_retained(self) -> None:
        runner = current_text(REPOSITORY_ROOT / "tests/host/ipc_model_test.c")
        actions = current_text(REPOSITORY_ROOT / "tests/host/ipc_model_test_actions.c")
        prelude = c_declaration_text(current_text(REPOSITORY_ROOT / "tests/host/ipc_model_test_prelude.c"))
        step = current_text(REPOSITORY_ROOT / "tests/host/ipc_model_test_step.c")
        trace = current_text(REPOSITORY_ROOT / "tests/host/ipc_model_test_trace.c")
        fixture = current_text(REPOSITORY_ROOT / MODEL_FIXTURE)
        self.assertIn("UINT32_C(0x30accee5)", runner)
        self.assertIn("struct model_state model;", runner)
        self.assertIn("struct model_coverage coverage;", runner)
        self.assertIn("while (step < MODEL_STEPS)", runner)
        self.assertEqual(2, len(re.findall(r"\bmodel_random_next\s*\(", runner)))
        self.assertEqual(93, len(re.findall(r"\bRUN\s*\(", prelude)))
        self.assertEqual(tuple(range(13)), tuple(int(value) for value in re.findall(r"\bcase\s+(\d+):", actions)))
        self.assertIn("return selected ? selected_action : recovery_action(fixture, model, random, step)", actions)
        ordered = (
            "ipc_model_reference_execute_action", "ipc_model_production_execute_action",
            "ipc_model_record_trace", "update_coverage(coverage", "*result = expected",
            "if (expected != actual)", "ipc_model_matches_production",
        )
        self.assertEqual(sorted(step.index(name) for name in ordered),
                         [step.index(name) for name in ordered])
        self.assertIn("UINT64_C(0x30accee57ace0001)", fixture)
        self.assertIn("(fixture->model_trace_hash << 7) | (fixture->model_trace_hash >> 57)", trace)
        for fragment in (
            "((uint64_t)step << 32)", "((uint64_t)action->operation << 56)",
            "((uint64_t)action->thread_slot << 16)", "((uint64_t)action->process_slot << 8)",
            "^ action->endpoint", "^ action->source", "^ action->value", "^ (uint64_t)result",
        ):
            self.assertIn(fragment, trace)
        compare = current_text(REPOSITORY_ROOT / MODEL_REFERENCE)
        body = compare[compare.index("bool ipc_model_matches_production("):]
        comparisons = (
            "compare_acceptance_aggregates", "compare_acceptance_processes",
            "compare_acceptance_endpoints_and_events", "compare_acceptance_threads",
            "compare_acceptance_hart_and_ready_queues", "compare_acceptance_public_invariants",
        )
        self.assertEqual(sorted(body.index(name) for name in comparisons),
                         [body.index(name) for name in comparisons])

    def test_small_models_keep_original_counts_draws_and_forced_prefix(self) -> None:
        notify = current_text(REPOSITORY_ROOT / "tests/host/ipc_notify_model_test.c")
        close = current_text(REPOSITORY_ROOT / "tests/host/ipc_close_model_test.c")
        self.assertIn("UINT32_C(0x30c0a1e5)", notify)
        self.assertIn("state.step < NOTIFY_MODEL_STEPS", notify)
        self.assertEqual(3, len(re.findall(r"\bnotify_model_next_random\s*\(", notify)))
        self.assertLess(notify.index("notify_model_next_random(&state->random_state)"),
                        notify.index("state->operation = scripted_operations"))
        self.assertLess(notify.index("notify_model_next_random(&state->random_state)",
                                     notify.index("notify_model_next_random(&state->random_state)") + 1),
                        notify.index("state->operation = scripted_operations"))
        self.assertIn("UINT32_C(0x30c105e1)", close)
        self.assertIn("case_index < CLOSE_SCENARIO_CASE_COUNT", close)
        self.assertEqual(4, len(re.findall(r"\bclose_scenario_next_random\s*\(", close)))
        select = close.index("select_close_model_scenario(&random_state, &scenario)")
        record = close.index("close_scenario_record(fixture, case_index, scenario.operation)", select)
        setup = close.index("ipc_close_test_setup_fixture(fixture, 3, 1)", record)
        self.assertLess(select, record)
        self.assertLess(record, setup)

    def test_target_script_capture_and_evidence_identities_remain_exact(self) -> None:
        fixture = current_text(REPOSITORY_ROOT / 'kernel/tests/ipc/ipc_syscall_test_fixture.c')
        threads = re.search(r"ipc_syscall_test_scenario_threads\[21\]\s*=\s*\{(.*?)\};", fixture, re.DOTALL)
        commands = re.search(r"ipc_syscall_test_scenario_commands\[21\]\s*=\s*\{(.*?)\};", fixture, re.DOTALL)
        self.assertIsNotNone(threads)
        self.assertIsNotNone(commands)
        self.assertEqual(
            SYSCALL_SCRIPT,
            tuple(zip(re.findall(r"\bIPC_SYSCALL_(CLIENT|SERVER|PEER)\b", threads.group(1)),
                      re.findall(r"\bIPC_COMMAND_([A-Z_]+)\b", commands.group(1)))),
        )
        self.assertIn("_Static_assert(", fixture)
        for family, entry in (("ecall", "micros_scheduler_test_enter_without_timer"),
                              ("syscall", "micros_scheduler_start")):
            facade = current_text(REPOSITORY_ROOT / f"kernel/tests/ipc/ipc_{family}_test.c")
            self.assertEqual(1, facade.count('"mv %0, sp'))
            self.assertLess(facade.index('"mv %0, sp'), facade.index(entry))
            for path in SOURCE_SETS[
                "MICROS_IPC_ECALL_QEMU_TEST_SOURCES" if family == "ecall"
                else "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES"
            ][1:]:
                self.assertNotIn('"mv %0, sp', current_text(REPOSITORY_ROOT / path), path)
            owner = current_text(REPOSITORY_ROOT / f"kernel/tests/ipc/ipc_{family}_test_fixture.c")
            for register in ("sp", "gp", "tp"):
                self.assertEqual(1, owner.count(f"uintptr_t micros_ipc_{family}_test_saved_{register};"))
        cmake = current_text(REPOSITORY_ROOT / "CMakeLists.txt")
        markers = {
            "test-qemu-ipc": "MICROS_IPC_TEST_PASS endpoints=generation-safe queues=blocking calls=tokenized notifications=coalesced kernel-events=injected deadlock=rejected",
            "test-qemu-ipc-ecall-core": "MICROS_IPC_ECALL_CORE_TEST_PASS dispatch=production guard=transactional completion=returned errors=stable registers=preserved",
            "test-qemu-ipc-syscall": "MICROS_IPC_SYSCALL_TEST_PASS spaces=three operations=six blocking=validated calls=tokenized notifications=coalesced errors=stable completion=deferred registers=preserved cleanup=complete",
            "test-qemu-ipc-syscall-panic": "MICROS_PANIC reason=invalid-bootstrap-ipc-buffer",
        }
        for target, marker in markers.items():
            block = cmake_command(cmake, "add_custom_target", target)
            self.assertIn(marker, block)
            self.assertRegex(block, r"--timeout\s+8\b")
            self.assertEqual(1, cmake.count(marker))
        negative = current_text(REPOSITORY_ROOT / 'kernel/tests/ipc/ipc_syscall_test_fixture.c')
        self.assertEqual(
            {"accounting_owner", "accounting_started_at", "kernel_counter_ticks"},
            set(re.findall(r"negative_objects_candidate\.harts\s*\[[^]]+\]\s*\.\s*(\w+)\s*=", negative)),
        )
        self.assertIn("].user_context", negative)
        self.assertIn("].accounted_thread", negative)
        hooks = current_text(REPOSITORY_ROOT / 'kernel/tests/ipc/ipc_syscall_test_hooks.c')
        self.assertIn("fixture->timer_completion_started", hooks)
        self.assertIn("fixture->timer_expected_context", hooks)

    def test_source_sets_and_current_consumers_are_exact(self) -> None:
        cmake = current_text(REPOSITORY_ROOT / "CMakeLists.txt")
        for name, expected in SOURCE_SETS.items():
            block = cmake_command(cmake, "set", name)
            self.assertEqual(
                expected,
                tuple(re.findall(r"(?:kernel|tests/host)/[A-Za-z0-9_/]+\.c", block)),
                name,
            )
        for target, required in {
            "endpoint_test": (
                "MICROS_IPC_CORE_SOURCES", "MICROS_IPC_OPERATION_HOST_TEST_SOURCES",
                "MICROS_IPC_CALL_HOST_TEST_SOURCES", "MICROS_IPC_REPLY_HOST_TEST_SOURCES",
                "MICROS_IPC_NOTIFY_HOST_TEST_SOURCES", "MICROS_IPC_CLOSE_HOST_TEST_SOURCES",
            ),
            "ipc_model_test": (
                "MICROS_IPC_CORE_SOURCES", "MICROS_IPC_MODEL_HOST_TEST_SOURCES",
                "MICROS_IPC_NOTIFY_HOST_TEST_SOURCES", "MICROS_IPC_CLOSE_HOST_TEST_SOURCES",
            ),
            "bootstrap_control_test": ("MICROS_IPC_CORE_SOURCES",),
            "pm_test": ("MICROS_IPC_CORE_SOURCES",),
            "micros": ("MICROS_IPC_CORE_SOURCES", "MICROS_IPC_SYSCALL_SOURCES"),
        }.items():
            block = cmake_command(cmake, "add_executable", target)
            for name in SOURCE_SETS:
                self.assertEqual(int(name in required), block.count(f"${{{name}}}"),
                                 (target, name))
            for source in all_sources():
                self.assertNotIn(source, block, target)
        for name, count in {
            "MICROS_IPC_CORE_SOURCES": 6, "MICROS_IPC_SYSCALL_SOURCES": 1,
            "MICROS_IPC_OPERATION_HOST_TEST_SOURCES": 1, "MICROS_IPC_CALL_HOST_TEST_SOURCES": 1,
            "MICROS_IPC_REPLY_HOST_TEST_SOURCES": 1, "MICROS_IPC_NOTIFY_HOST_TEST_SOURCES": 2,
            "MICROS_IPC_CLOSE_HOST_TEST_SOURCES": 2, "MICROS_IPC_MODEL_HOST_TEST_SOURCES": 1,
            "MICROS_IPC_QEMU_TEST_SOURCES": 1, "MICROS_IPC_ECALL_QEMU_TEST_SOURCES": 1,
            "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES": 1,
        }.items():
            self.assertEqual(count, cmake.count(f"${{{name}}}"), name)
        for flag, source_set, assembly in (
            ("MICROS_BUILD_IPC_TEST", "MICROS_IPC_QEMU_TEST_SOURCES", None),
            ("MICROS_BUILD_IPC_ECALL_CORE_TEST", "MICROS_IPC_ECALL_QEMU_TEST_SOURCES", "arch/riscv64/ipc_ecall_test.S"),
            ("MICROS_BUILD_IPC_SYSCALL_TEST", "MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES", "arch/riscv64/ipc_syscall_test.S"),
        ):
            sources = [block for block in cmake_if_blocks(cmake, flag) if "target_sources(" in block]
            self.assertEqual(1, len(sources), flag)
            self.assertEqual(1, sources[0].count("${" + source_set + "}"))
            if assembly:
                self.assertEqual(1, sources[0].count(assembly))

    def test_supported_entries_have_one_responsible_owner(self):
        definitions = {}
        for path, names in self.definitions.items():
            for name in names:
                definitions.setdefault(name, []).append(path)
        for owner, names in FINAL_OWNER_GROUPS.items():
            for name in names:
                self.assertEqual([owner], definitions.get(name), name)

    def test_current_paths_keep_broad_validation_classification(self):
        for name, paths in SOURCE_SETS.items():
            baseline = paths[-1] if name in {"MICROS_IPC_CORE_SOURCES", "MICROS_IPC_SYSCALL_SOURCES"} else paths[0]
            for path in (*paths, PRIVATE_HEADERS[name]):
                for tier in ("fast", "pr"):
                    self.assertEqual(
                        validation_plan.plan([baseline], tier, "main"),
                        validation_plan.plan([path], tier, "main"), path,
                    )
        self.assertIn("tests/host/test_kernel_ipc_source_layout.py", validation_plan.HOST_UNIT_ONLY_INPUTS)


if __name__ == "__main__":
    unittest.main()
