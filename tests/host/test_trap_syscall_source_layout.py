from __future__ import annotations

import ast
from collections import Counter
import copy
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import re
import unittest
from unittest.mock import patch

from tests.host import test_vm_service_source_layout as vm_layout
from tests.host.test_endpoint_source_layout import (
    alias_rows as endpoint_alias_rows,
    endpoint_cmake_projection,
)
from tests.host.test_frame_ownership_source_layout import (
    c_function_definitions,
    c_lexical_text,
    cmake_command,
    cmake_if_blocks,
)
from tests.host.test_kernel_bootstrap_source_layout import (
    body_fingerprint,
    c_function_body as lexical_function_body,
    compact,
    compact_hash,
    has_function_reference,
    python_body_hash,
    string_records,
)
from tests.host.test_kernel_pm_control_source_layout import macro_definition_text
from tests.host.test_pm_service_source_layout import cmake_commands, static_local_storage
from tests.host.test_vm_service_source_layout import (
    c_declaration_text,
    file_scope_objects,
    python_function_definitions,
)
from tools import validation_plan


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
REVIEWED_BASE = "09271b80de386870b7ecadcf0368576e46da56ae"
FUNCTION_REVIEW_LINE_LIMIT = 120
STRUCTURAL_TEST_PATH = "tests/host/test_trap_syscall_source_layout.py"
GENERATOR_PATH = "tools/generate_user_runtime_fixture.py"

SOURCE_SETS = {
    "MICROS_TRAP_SOURCES": (
        "kernel/trap.c",
        "kernel/trap_context.c",
        "kernel/trap_user.c",
        "kernel/trap_interrupt.c",
        "kernel/trap_syscall.c",
        "kernel/trap_fault.c",
        "kernel/trap_test.c",
        "kernel/trap_test_hooks.c",
    ),
    "MICROS_SYSCALL_SOURCES": (
        "kernel/syscall.c",
        "kernel/syscall_decode.c",
        "kernel/syscall_context.c",
    ),
    "MICROS_USER_RUNTIME_KERNEL_TEST_SOURCES": (
        "kernel/user_runtime_test.c",
        "kernel/user_runtime_test_state.c",
        "kernel/user_runtime_test_image.c",
        "kernel/user_runtime_test_setup.c",
        "kernel/user_runtime_test_trap.c",
        "kernel/user_runtime_test_cleanup.c",
        "kernel/user_runtime_test_report.c",
    ),
    "MICROS_USER_RUNTIME_SERVICE_TEST_SOURCES": (
        "tests/qemu/user_runtime_service.c",
        "tests/qemu/user_runtime_service_common.c",
        "tests/qemu/user_runtime_client.c",
        "tests/qemu/user_runtime_responders.c",
    ),
    "MICROS_RUNTIME_HOST_TEST_SOURCES": (
        "tests/host/runtime_test.c",
        "tests/host/runtime_test_fixture.c",
        "tests/host/runtime_wrapper_test.c",
    ),
}
FAMILY_HEADERS = {
    "MICROS_TRAP_SOURCES": "kernel/trap_internal.h",
    "MICROS_SYSCALL_SOURCES": "kernel/syscall_internal.h",
    "MICROS_USER_RUNTIME_KERNEL_TEST_SOURCES": "kernel/user_runtime_test_internal.h",
    "MICROS_USER_RUNTIME_SERVICE_TEST_SOURCES": "tests/qemu/user_runtime_service_internal.h",
    "MICROS_RUNTIME_HOST_TEST_SOURCES": "tests/host/runtime_test_internal.h",
}
PRIVATE_HEADERS = tuple(FAMILY_HEADERS.values())

# Fixed digests were read from REVIEWED_BASE, never from the edited worktree.
STABLE_HASHES = {
    "include/micros/trap.h":
        "ba2331a1bd0e0f75c3a02e28b1a14e0be635131e8a5b8327594a219309900afd",
    "include/micros/runtime.h":
        "ed5ab35aebfc487a3ad79971c4daebef3dfbb45df78c64148b8e8f379aeddaf8",
    "include/micros/syscall_abi.h":
        "8bd61b7bacc4b8c29fb62270aa8678930bb15a54e27e9aaaee03fafc5a9e8cad",
    "kernel/syscall.h":
        "98a1923551df1b5a174adedd6def10c449f3d791f31c351b75b5869e4d37a79b",
    "kernel/trap_route_core.h":
        "eb289b481f158e5b7bd85667d4cce46fdb15b9fa5d0bdfbe75f49bf8a75b5e6a",
    "kernel/user_runtime_test.h":
        "82996507726f93024cdb82e52d484592a300b856999f2da5c218bda17c41f92b",
    "kernel/user_runtime_image.h":
        "18a83acf7f5a161e4491f89704ed90c7bc9f624075425ac07035fa3aad75b332",
    "tests/qemu/user_runtime_protocol.h":
        "dbcb7d096f5736f912fab4334192279a74da4ea10d1557f9a1155410c36b58c0",
    "lib/runtime/raw_syscall.h":
        "85cb25522506c276b98f8d019e98ff318a79fd9a8b4b8238ef49ff815195ba53",
    "lib/runtime/memory.h":
        "c2cc24decf2cba2f1d9e5b8ca388b9bb6234f54f0d01ebd088a9b4c7ec2289c3",
    "arch/riscv64/trap_context.h":
        "584a45235e8eea9a973ccaebb0ace4b84f22778f360625a68feecf1dc276e5d3",
    "kernel/trap_route_core.c":
        "359dd81c331782fb3f8c5c618e06284e28e094e2de1fad1854c6cff66be96632",
    "lib/runtime/runtime.c":
        "437015598b37dd1191eab033434d9a0196e50729fbd8afcd514dc4af297b48e5",
    "lib/runtime/memory.c":
        "780f152e6a26cfcc8d01ee3760662d5bfcd941154051396f8d696c9039171323",
    "lib/runtime/memory_symbols.c":
        "64a9578265adae81e0f7f5ffc9defee01f35423f69f41e0afae827a1ce74f4f6",
    "arch/riscv64/trap.S":
        "e541ebbc7c1ab6e68eb366ec9ea353bc381db55364b999c7516e30a56c96666d",
    "arch/riscv64/trap_test.S":
        "8553924e45df6b7a6217518b907516be51a4d53eadb6e672b47b2d04dffe4437",
    "arch/riscv64/nested_trap_test.S":
        "2a73fb5c2013b60eb1631865bbf8dd15a9e6aaea907b825d8d28d44b753774a8",
    "arch/riscv64/user_runtime_test.S":
        "4a7f4478dccd6ecdc3349ccd0a41c759b2388e7d6ab691f221c396dffedbbf0c",
    "tests/qemu/user_runtime_probe.S":
        "b8a19039c3da1a00e9fd44705f0d66c624365e90a4975a0ac86382a4c599b9c6",
    "lib/runtime/start.S":
        "527bd96126331a5b5824458c7aa0c74d54957a9602617e6479616f78a2a02e8d",
    "lib/runtime/raw_syscall.S":
        "81a1063b7ddede76178fa2d8c53b32e48dc3bbe03647071d2738e0964f99e1d0",
    "lib/runtime/user.ld":
        "497fd076c2f3277938bbfe7a33eafb52d440c60ef2b038cf04e0d913d310324b",
}
EXISTING_INLINE_HEADERS = {
    "arch/riscv64/interrupt.h":
        "596e1f799fc629a2184dadb09f8fa7d41207ba04afbc6378040c57e1c8c1f1f5",
    "kernel/bootstrap_control_internal.h":
        "d2601f07bbc41301a1e98086aa910ac1625bdaed670202f911d6aa9c30bf0633",
}
ORIGINAL_SOURCE_HASHES = {
    "kernel/trap.c":
        "eb6653f00797db2828d240f1254a8390042760772fdc8e0dbd74d21210632da3",
    "kernel/syscall.c":
        "06b6f45a926e8f04f9938e774bd62fe4823a5275ad38235b952b699b66ad3f6c",
    "kernel/user_runtime_test.c":
        "d39dcf1339a92babdf0a903c917521d12cbf22f72919e529ec6cb3d81147e704",
    "tests/qemu/user_runtime_service.c":
        "9da9895f10b0bd2253bc0307720682e94d06056303330f7e0373bbfd99ee80f3",
    "tests/host/runtime_test.c":
        "0b7caaa06d4716b6d76b368185e4eebc84620e77acda671c1a4e52cd9ad0a628",
}

# Every one of the 79 original C definitions has an explicit final owner.
OWNER_GROUPS = (
    ("kernel/trap.c", "kernel/trap.c", "", (
        "micros_trap_install", "micros_trap_dispatch", "micros_trap_nested_panic",
    )),
    ("kernel/trap.c", "kernel/trap_context.c", "trap_", (
        "read_sscratch", "read_stvec", "read_tp", "read_sstatus",
        "frame_is_on_primary_stack", "resolve_trap_hart",
    )),
    ("kernel/trap.c", "kernel/trap_interrupt.c", "trap_", (
        "handle_scheduler_timer_error",
    )),
    ("kernel/trap.c", "kernel/trap_fault.c", "trap_", (
        "fail_active_bootstrap_service_trap", "record_tty_owner_failure",
        "handle_vm_self_fault",
    )),
    ("kernel/trap.c", "kernel/trap_test.c", "", (
        "trap_test_has_entry_registers", "trap_test_has_expected_exception",
        "trap_test_has_sum_restore_exception", "trap_test_prepare_return",
        "trap_test_has_return_registers", "trap_test_prepare_sum_restore_return",
        "trap_panic_test_has_expected_exception",
        "micros_trap_run_self_test", "micros_trap_run_panic_test",
    )),
    ("kernel/syscall.c", "kernel/syscall.c", "", (
        "capture_arguments", "micros_syscall_handle_user_ecall",
    )),
    ("kernel/syscall.c", "kernel/syscall_decode.c", "syscall_", (
        "decode_vm_handoff_request",
    )),
    ("kernel/syscall.c", "kernel/syscall_context.c", "syscall_", ("panic_syscall",)),
    ("kernel/user_runtime_test.c", "kernel/user_runtime_test.c", "", (
        "micros_user_runtime_runtime_run_self_test",
    )),
    ("kernel/user_runtime_test.c", "kernel/user_runtime_test_state.c", "user_runtime_test_", (
        "clear_bytes", "fill_bytes", "copy_bytes", "bytes_equal", "bytes_are_zero",
        "read_satp", "current_actor",
    )),
    ("kernel/user_runtime_test.c", "kernel/user_runtime_test_image.c", "user_runtime_test_", (
        "segment_permissions", "find_page", "allocate_image_pages",
        "copy_image_segments", "verify_loaded_pages", "translate_range",
        "patch_configuration", "snapshot_writable_pages",
        "writable_pages_match_snapshot", "read_configuration",
    )),
    ("kernel/user_runtime_test.c", "kernel/user_runtime_test_setup.c", "user_runtime_test_", (
        "prepare_process",
    )),
    ("kernel/user_runtime_test.c", "kernel/user_runtime_test_trap.c", "user_runtime_test_", (
        "hold_other_threads", "micros_user_runtime_test_before_ecall",
        "micros_user_runtime_test_after_dispatch", "micros_user_runtime_test_handle_trap",
    )),
    ("kernel/user_runtime_test.c", "kernel/user_runtime_test_cleanup.c", "user_runtime_test_", (
        "configurations_complete", "close_endpoint", "release_process",
        "endpoint_registry_is_empty", "scheduler_is_idle", "micros_user_runtime_test_finish",
    )),
    ("tests/qemu/user_runtime_service.c", "tests/qemu/user_runtime_service.c", "", (
        "startup_checks", "micros_service_main",
    )),
    ("tests/qemu/user_runtime_service.c", "tests/qemu/user_runtime_service_common.c",
     "user_runtime_service_", (
         "bytes_equal", "bytes_are_zero", "put_u32", "get_u32", "get_u64",
         "fill_message", "message_matches", "notification_matches",
         "initial_bss_is_zero", "memory_support_works",
     )),
    ("tests/qemu/user_runtime_service.c", "tests/qemu/user_runtime_client.c",
     "user_runtime_service_", ("run_client",)),
    ("tests/qemu/user_runtime_service.c", "tests/qemu/user_runtime_responders.c",
     "user_runtime_service_", ("run_server", "run_peer")),
    ("tests/host/runtime_test.c", "tests/host/runtime_test.c", "", ("main",)),
    ("tests/host/runtime_test.c", "tests/host/runtime_test_fixture.c", "runtime_test_", (
        "reset_capture", "micros_runtime_raw_syscall", "arguments_match", "message_pattern",
    )),
    ("tests/host/runtime_test.c", "tests/host/runtime_wrapper_test.c", "", (
        "test_ipc_wrappers", "test_grant_wrappers",
        "test_bootstrap_launcher_console_begin", "test_memory_support",
    )),
)
INITIAL_SYMBOL_OWNER_MIGRATION = (
    ("kernel/trap.c", "kernel/trap_test.c", "micros_trap_run_self_test"),
    ("kernel/trap.c", "kernel/trap_test.c", "micros_trap_run_panic_test"),
    ("kernel/user_runtime_test.c", "kernel/user_runtime_test_trap.c",
     "micros_user_runtime_test_before_ecall"),
    ("kernel/user_runtime_test.c", "kernel/user_runtime_test_trap.c",
     "micros_user_runtime_test_after_dispatch"),
    ("kernel/user_runtime_test.c", "kernel/user_runtime_test_trap.c",
     "micros_user_runtime_test_handle_trap"),
    ("kernel/user_runtime_test.c", "kernel/user_runtime_test_cleanup.c",
     "micros_user_runtime_test_finish"),
    ("tests/qemu/user_runtime_service.c", "tests/qemu/user_runtime_client.c", "run_client"),
    ("tests/qemu/user_runtime_service.c", "tests/qemu/user_runtime_responders.c", "run_server"),
    ("tests/qemu/user_runtime_service.c", "tests/qemu/user_runtime_responders.c", "run_peer"),
    ("tests/host/runtime_test.c", "tests/host/runtime_test_fixture.c",
     "micros_runtime_raw_syscall"),
    ("tests/host/runtime_test.c", "tests/host/runtime_wrapper_test.c", "test_ipc_wrappers"),
    ("tests/host/runtime_test.c", "tests/host/runtime_wrapper_test.c", "test_grant_wrappers"),
    ("tests/host/runtime_test.c", "tests/host/runtime_wrapper_test.c",
     "test_bootstrap_launcher_console_begin"),
    ("tests/host/runtime_test.c", "tests/host/runtime_wrapper_test.c", "test_memory_support"),
)
SYMBOL_OWNER_MIGRATION = ()

LONG_FUNCTION_REQUIREMENTS = {
    ("kernel/trap.c", "handle_vm_self_fault"): (
        143, "f52933c22ed5fc086e56c37ee1ffd402b51235ec823fbfee032c17daedb05ab4",
        "fault/current identity; runtime/service/ownership classification; record; running/sealed fatal",
    ),
    ("kernel/trap.c", "micros_trap_dispatch"): (
        924, "2f5e1329a36fcd9a21adda3ddc0aa8bdbdef9caa7c5c4c4e379c9921533c80f5",
        "routed context; accounting/capture; fault/interrupt/fixture/ecall; selected/direct return; fatal",
    ),
    ("kernel/syscall.c", "micros_syscall_handle_user_ecall"): (
        180, "d977f90498a5821870e47d47ff9e85e06e9571adb70f82d8d25f34a25360fd53",
        "arguments/PC/capture; ordered early decode; authoritative context; operation dispatch",
    ),
    ("kernel/user_runtime_test.c", "micros_user_runtime_runtime_run_self_test"): (
        157, "5ab8b4102ff80843dcca72ac477673d4d8124879444eadb0e05ac1f70e62f281",
        "image/runtime/baseline; mechanisms; identities; images/contexts/fence; admission; original-frame capture/start; failure",
    ),
    ("tests/qemu/user_runtime_service.c", "run_client"): (
        203, "269d2567f08e44e2ecffc9661ea6494a713753112431360d015e15804bdc63e0",
        "raw send; two calls/canaries; grants/copy; revoke/stale; complete/terminal receive",
    ),
    ("tests/qemu/user_runtime_service.c", "run_server"): (
        242, "f013015e770c15d86dd15a7c5d5b15e73c310ab0162d747ac7b0fae9a248b601",
        "receive/reply/call; peer/combined exchange; grants/copies; stale/final completion",
    ),
    ("tests/host/runtime_test.c", "test_ipc_wrappers"): (
        162, "b96c71d4aea1b42b8e0b62dcaf07a1fa812d15ea7def35bc40d1369087657338",
        "send/receive/call; reply/combined; notification/failure preservation",
    ),
    ("tests/host/runtime_test.c", "test_grant_wrappers"): (
        169, "77b728576ace157242dbe9bd1194257dbc4cc51e1367773da1dd0da5af98adad",
        "create success/error/null; revoke/copy/validate; negative-result/errno loop",
    ),
    (GENERATOR_PATH, "render_fixture"): (
        136, "442d716d4458f3859f62fc7cad91d75ee7f2adca91953cea8990fd7995cd284a",
        "validated ELF/symbols/permissions; segment bytes; image/symbol fields; concatenation",
    ),
}
INITIAL_LONG_FUNCTION_MIGRATION = tuple(LONG_FUNCTION_REQUIREMENTS)
LONG_FUNCTION_MIGRATION = ()

HELPER_MIGRATION_ANCHORS = {
    "kernel/trap_context.c": ("kernel/trap.c", "micros_trap_dispatch"),
    "kernel/trap_interrupt.c": ("kernel/trap.c", "micros_trap_dispatch"),
    "kernel/trap_fault.c": ("kernel/trap.c", "micros_trap_dispatch"),
    "kernel/trap_test.c": ("kernel/trap.c", "micros_trap_run_self_test"),
    "kernel/syscall_decode.c": ("kernel/syscall.c", "micros_syscall_handle_user_ecall"),
    "kernel/syscall_context.c": ("kernel/syscall.c", "micros_syscall_handle_user_ecall"),
    "kernel/user_runtime_test_state.c":
        ("kernel/user_runtime_test.c", "micros_user_runtime_runtime_run_self_test"),
    "kernel/user_runtime_test_image.c":
        ("kernel/user_runtime_test.c", "micros_user_runtime_runtime_run_self_test"),
    "kernel/user_runtime_test_setup.c":
        ("kernel/user_runtime_test.c", "micros_user_runtime_runtime_run_self_test"),
    "kernel/user_runtime_test_trap.c":
        ("kernel/user_runtime_test.c", "micros_user_runtime_test_handle_trap"),
    "kernel/user_runtime_test_cleanup.c":
        ("kernel/user_runtime_test.c", "micros_user_runtime_test_finish"),
    "tests/qemu/user_runtime_service_common.c":
        ("tests/qemu/user_runtime_service.c", "run_client"),
    "tests/qemu/user_runtime_client.c": ("tests/qemu/user_runtime_service.c", "run_client"),
    "tests/qemu/user_runtime_responders.c": ("tests/qemu/user_runtime_service.c", "run_server"),
    "tests/host/runtime_test_fixture.c": ("tests/host/runtime_test.c", "micros_runtime_raw_syscall"),
    "tests/host/runtime_wrapper_test.c": ("tests/host/runtime_test.c", "test_ipc_wrappers"),
}

ORIGINAL_BODY_HASHES = {
    "kernel/trap.c": {
        "fail_active_bootstrap_service_trap": "583704b452033015146bc6f5d334d70877a214cd4e2cba5b24c17280f63c970c",
        "record_tty_owner_failure": "f646906804a9a7fd06242b3c13c3d9c4cec509ba481be8f428106e8405d5ead0",
        "handle_vm_self_fault": "7901485ae777d696de249c2913496866eb19390ebef1aa9d69e63f8794a90d63",
        "read_sscratch": "d30edb837e11e203c411f04848b191713fff530c57da6199ad1a8adce7709399",
        "read_stvec": "42cc5fdf527817a4afa108aa9540e06dc9608f268e549ef843fc27ec102d9a65",
        "read_tp": "8ab9d20eaf2c55d4a4403e8775869cf44c5e5811f93324a6ac07a09676131d9f",
        "read_sstatus": "c470b8ab9c7a4d1eb4002a42373de033702282c37cffa3b7c0b4ba65fcd81608",
        "frame_is_on_primary_stack": "823f1a86739e7134ea9890570fc031c70901c272bb719088b39dbb4bbf55fb21",
        "resolve_trap_hart": "e78ce8bd8018a17895c984534c18a8722fe782d42b0ec266a455434608f867c7",
        "handle_scheduler_timer_error": "2fe5cd9b268d7b7ee8bdb1bfafbd5b541be55bc2fd101db09b95e81b2051dc33",
        "micros_trap_install": "747f090dd9461306abc2f65ed568c7e2e097a7ba73535baba5ae422e39db3385",
        "trap_test_has_entry_registers": "54daf3b904e01a8959fc0f376fd2f7708099623c479dc9668d6dac05db1072de",
        "trap_test_has_expected_exception": "0ea5970b4faac71919a6ef00a3f74f3fa8a7e944bfa6c3ee2d5417b9c81dd580",
        "trap_test_has_sum_restore_exception": "04f4f2f1aa4744755ae3c4dd13817dab06db83f0119162f73b23bb12cf3f3c12",
        "trap_test_prepare_return": "ae151aa3140cd3872d5e2b5d755ce77ad1efbe223af80e849603d4537d2e001f",
        "trap_test_has_return_registers": "054d0d58909557738ba0fc99e25e7f8c69ae04cdb505a5d1373569b35dabe74c",
        "trap_test_prepare_sum_restore_return": "b64604a19f314b712529d1d7aa5ac3d3de60a4586ce4f5dd047466398eaac138",
        "trap_panic_test_has_expected_exception": "a6b0abc1f6b09bbb7db142f2353a75b4aa12b2bbf7777badd019e767af5cd1ab",
        "micros_trap_dispatch": "f7e3b33fd206d9c0b3f0bcbffd7b521006f1c90bc7d488fa04b2dffacae15d7a",
        "micros_trap_nested_panic": "ae45120a9ce92b0f317b5efaf4e2467ab9e8ff9cd36924a404b875f68794f91f",
        "micros_trap_run_self_test": "6c84ddd2feca986c4cfec2ea737a20653e7bb7f4821940ad386d858949b11b4a",
        "micros_trap_run_panic_test": "50907dac5fbd0781c23c4d3f5dace8afcf564cfa460d8bb35d90997aed7ddaa1",
    },
    "kernel/syscall.c": {
        "panic_syscall": "e2fd3658d48c77eae73632ed3ca3c7cf2b6488cd14f3d7245f6e6ff1442b250c",
        "capture_arguments": "aa8569430d016aaf161284c5e9821e6bafdfe9020956a1d08907ea7acad3b34d",
        "decode_vm_handoff_request": "1efbbeddca613a23604a0af4985759744cac1bdbb00275674a62b90710d3de66",
        "micros_syscall_handle_user_ecall": "865f5231b035199fd683686758f6e3b7e6f39cad24f63398d64002ab2c978581",
    },
    "kernel/user_runtime_test.c": {
        "clear_bytes": "73cfbc2d0ffdeb300c21a7a1b2c4d55111ce337f8c8351356d4a7403dad268c1",
        "fill_bytes": "3edbd686e2cc02751a0a0072222edbb9f2973259742392d567185e243df8396c",
        "copy_bytes": "42c4439565ff848015ea2d5509c4dcaf86d2343ec4a63c09b66210800324f9a0",
        "bytes_equal": "c8ed4280fba1ea77634f995624498f79de200ebc36867a099ce9c68a69d38744",
        "bytes_are_zero": "876a4df19bdfc22233f0a056e309a0e5b243e4d7e837873de88b9e3a48977630",
        "read_satp": "3fc043c94edb3912cfd0256e2f5f21daa5aada93179b6c48be451159bf7b3d84",
        "segment_permissions": "d9a3786cdd994aecfee81be069f2c08e2819416836a2ac1913e3b6d33fa5d7c8",
        "find_page": "a20fb703529a6330558e00178740324f314d1c914e1afa74fa41f3912e485054",
        "allocate_image_pages": "e68dc5d4ab7fa382ccc9aa571c9d4e2c4e002b62f8fdbbe203658e215e8ea550",
        "copy_image_segments": "3ec0c1c31187144288ab2d6eacb37612d810f4aaac7706049df7cd425d5dbe25",
        "verify_loaded_pages": "82829ab4088b630b296f981a2c25af7aa8796a3045c420decd1406ebc94fbbc8",
        "translate_range": "c473b66aa0ba730e5acbc7532f4257a76b620712351ac07fe4100e2121d23b1b",
        "patch_configuration": "91b96114d992ea92320ec2cdc9fea1198075665a94216c6797f572141e5eea42",
        "snapshot_writable_pages": "4f2689b6c6b30a02d4df1fa7eb1552ed1858338debffb45d1ba8da532c2bb948",
        "writable_pages_match_snapshot": "a1a615905c3b553347695b0fa3af239f2cda651ceaada5520cb211341a13ae68",
        "prepare_process": "fa1040afca7394dbde1f264c318236546d28560a60b26678aa69f71786e0a1ae",
        "current_actor": "ebe8f7ca7f713a68615d8b429968acb2d70123c4d23713390206abe02d056655",
        "read_configuration": "f00098cbd2de08de9afacf4acbc790041faab98cb2fa5dfaa09c03dc24af1682",
        "configurations_complete": "8cf192b70a83bd23ac5f3aa6ba55fd2b56dfc3c42c40b270b50776078e649af7",
        "hold_other_threads": "2c2189cd415bb327157bf178c6b001cc38a867236bebb33e6ed70c42c6239f15",
        "micros_user_runtime_test_before_ecall": "7a05036387ebc0b5d06372b45753f33325600ef5e268e12af34d9751c4de0993",
        "micros_user_runtime_test_after_dispatch": "89bc630e69c6145495d6fe56d31b54db709f84e240a08c63e4c772cbe103b964",
        "micros_user_runtime_test_handle_trap": "1de83bd46b735f471eaddef6641d58e5d05c260a5573db68c164b7e0d862e814",
        "close_endpoint": "b9a8ea6eaa92d8b3cbda892e1f6c08a0af39808033c71a6a6aa456930fe2c542",
        "release_process": "758abb112e2b847c927e3c5a5707e381c9c2b5f7b268ecaf774ed1c8f75525af",
        "endpoint_registry_is_empty": "12888a868efb316e256452af1dd66c2cc37eed0c361a89e23348b951b4131d55",
        "scheduler_is_idle": "806e1c5c5d0bba61c32be0e575c723bf213f970a41fb8a6320761500a7deace2",
        "micros_user_runtime_test_finish": "2954fc10ef0b3e90ff6c231c5c0b600be73ab06b6e6eb612f672f6e5174d2deb",
        "micros_user_runtime_runtime_run_self_test": "5d31d169c24324c69a81663d2f29e20150c0d66079c561f94559f83c7a502cd4",
    },
    "tests/qemu/user_runtime_service.c": {
        "bytes_equal": "00c4b7e2db20f2f7f67892315f01633c017fa0f5e209793fba4fd43582c41651",
        "bytes_are_zero": "c937703dba8fa4dba2bd6ff34a458b531d20be5d58cb50a6092d1fae61cf9e37",
        "put_u32": "324df5770e9cfaf675836f73083380b6f41c8eca3d958590335296c719e7ea0b",
        "get_u32": "606681c48498c20b512dc8a20e61312cc0006a8496b5e598b47aa7453c87230e",
        "get_u64": "58a7c1543c983237ab9ff1380ecfd5efef94d121ae3d078c29978179547d0057",
        "fill_message": "f8ee2df6b1ec5e654abf446d33667f77f82629562d442d9a28dfaa41b427504d",
        "message_matches": "718b4075fe9221d490cb8b0bf9fc94ab87d3b14ecc35278958ce680a3e1c0dff",
        "notification_matches": "81448c2c88166695a2f69c41fb89e7a13e7582836e6314c6b21282b6d3ad7adc",
        "initial_bss_is_zero": "7ff2e92535f5e0f020321b73a76d2bd9d26d40b9c48897f2732410b014ee3e25",
        "memory_support_works": "4b1400ddcd40180218f212a30069d86473e67d554738d98767119c071bdcc009",
        "startup_checks": "8479c3b11a81fff5d5bdb599e032bd9190e82da276924f7de4a5b1bcaa72f013",
        "run_client": "c72d3c5ac5001c379fd3b263af272c202aadc3b25a13814b45a30511a565fe20",
        "run_peer": "09d076a9e469ef493682b613ef6005446bff59bea3aa6081c3507a9cad4c87fe",
        "run_server": "821d7612f1387b665208cf3056ca30baa18a086473c4ea1cefed27c868fdb474",
        "micros_service_main": "8558347eecaf37c0149253387201f1b86309e88fc95009f7e46cb90c105425d4",
    },
    "tests/host/runtime_test.c": {
        "reset_capture": "c409be947d89fadbefb25a684bf3a054d878914f4ee87c8d2a9d845eed150304",
        "micros_runtime_raw_syscall": "2d3c6206a5982cf1931cfbeac5e3508665ac9012b8d2e6643ac31092ebe11609",
        "arguments_match": "2a9c14e2eeabc3dc11c19bc7178c60481e0bce986d8fd389674c1a6d9392ff10",
        "message_pattern": "ced344095b83fe32bdf80f588939ad4c44bc48f6fd968ea5af111c3e81c4c507",
        "test_ipc_wrappers": "9e6d7cdb8d8341d6ae786075ff96a75186aebcd44e647f7eb78eb5536b10bc14",
        "test_grant_wrappers": "c587cf23207a97c6c1a079e7636e2aab64bf25516ed01567af167b9798928e4c",
        "test_bootstrap_launcher_console_begin": "59efd0a62d485b2a5c2d6da44ac53f65ab75804a53843f3da8b217498bae8fde",
        "test_memory_support": "5e3b470beb62cf7570c017d07025a87590ec5eebc7761fb1b1d4186f9bec5c4c",
        "main": "db36f044eb438542d85d767cfd9707bb73c29e6baa55e23bc68c781d133a41cd",
    },
}

TRAP_FIELDS = (
    "volatile enum trap_test_state trap_test_state;",
    "volatile uint32_t trap_test_count;",
    "uint64_t trap_test_entry_sstatus;",
)
KERNEL_FIELDS = (
    "struct micros_process_handle processes[USER_RUNTIME_PROCESS_COUNT];",
    "struct micros_thread_handle threads[USER_RUNTIME_PROCESS_COUNT];",
    "micros_endpoint_t endpoints[USER_RUNTIME_PROCESS_COUNT];",
    "struct process_image images[USER_RUNTIME_PROCESS_COUNT];",
    "unsigned char writable_snapshots[USER_RUNTIME_PROCESS_COUNT]"
    "[USER_RUNTIME_MAX_IMAGE_PAGES][MICROS_SV39_PAGE_SIZE];",
    "bool writable_snapshot_valid[USER_RUNTIME_PROCESS_COUNT][USER_RUNTIME_MAX_IMAGE_PAGES];",
    "struct micros_endpoint_registry *endpoint_registry;",
    "struct micros_grant_registry *grant_registry;",
    "uint64_t baseline_owned;",
    "uint64_t baseline_free;",
    "size_t baseline_processes;",
    "size_t baseline_threads;",
    "size_t baseline_harts;",
    "bool rodata_fault_seen;",
    "bool loader_validated;",
    "bool final_client_send;",
    "size_t returned_actor;",
)
RAW_FIELDS = (
    "uint64_t arguments[8];",
    "int64_t result;",
    "size_t calls;",
    "int write_argument;",
    "const struct micros_ipc_message *expected_input;",
    "struct micros_ipc_message input_snapshot;",
    "struct micros_ipc_message output;",
    "bool input_observed;",
)
SAVED_REGISTER_OBJECTS = tuple(
    f"uintptr_t micros_user_runtime_test_saved_{register};"
    for register in ("sp", "gp", "tp")
)
IMAGE_OBJECTS = (
    "volatile struct micros_user_runtime_test_config micros_user_runtime_test_config = "
    "{ .version = MICROS_USER_RUNTIME_TEST_VERSION, };",
    "const volatile uint64_t micros_user_runtime_test_rodata = "
    "MICROS_USER_RUNTIME_TEST_RODATA_SENTINEL;",
    "volatile uint64_t micros_user_runtime_test_data_sentinel = "
    "MICROS_USER_RUNTIME_TEST_DATA_SENTINEL;",
    "volatile struct test_region micros_user_runtime_test_read_region = {"
    ".before = MICROS_USER_RUNTIME_TEST_CANARY_BEFORE, .bytes = {"
    "0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,"
    "0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,"
    "0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,"
    "0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,"
    "}, .after = MICROS_USER_RUNTIME_TEST_CANARY_AFTER, };",
    "volatile struct test_region micros_user_runtime_test_write_region;",
    "volatile struct test_region micros_user_runtime_test_server_region;",
    "volatile uint8_t micros_user_runtime_test_bss_probe[TEST_BSS_PROBE_SIZE];",
)
RECORD_FIELDS = {
    "mapped_page": (
        "uint64_t virtual_address;", "uint64_t physical_address;",
        "uint32_t permissions;", "size_t segment_index;",
    ),
    "process_image": (
        "size_t page_count;", "struct mapped_page pages[USER_RUNTIME_MAX_IMAGE_PAGES];",
        "uint64_t stack_physical;",
    ),
    "test_region": (
        "uint64_t before;", "uint8_t bytes[TEST_TRANSFER_SIZE];", "uint64_t after;",
    ),
    "raw_capture": RAW_FIELDS,
    "micros_vm_handoff_decoded_request": (
        "struct micros_vm_handoff_request ready;",
        "struct micros_vm_tty_mapping_request tty_mapping;",
        "struct micros_vm_load_begin_request load_begin;",
        "struct micros_vm_scratch_map_request scratch_map;",
        "struct micros_vm_scratch_commit_request scratch_commit;",
        "struct micros_vm_load_freeze_request load_freeze;",
        "struct micros_vm_load_abort_request load_abort;",
        "struct micros_vm_load_commit_request load_commit;",
        "struct micros_vm_exit_release_request exit_release;",
    ),
}
NATIVE_CASE_ORDER = (
    "test_ipc_wrappers", "test_grant_wrappers",
    "test_bootstrap_launcher_console_begin", "test_memory_support",
)
QEMU_WORKFLOWS = (
    "test-qemu-smoke", "test-qemu-panic", "test-qemu-trap", "test-qemu-timer",
    "test-qemu-uart-console", "test-qemu-frame-allocator", "test-qemu-trap-panic",
    "test-qemu-mmu", "test-qemu-object-model", "test-qemu-endpoint", "test-qemu-grant",
    "test-qemu-grant-syscall", "test-qemu-user-runtime", "test-qemu-bootstrap-launcher",
    "test-qemu-vm-handoff", "test-qemu-pm-service", "test-qemu-vm-scratch-preemption",
    "test-qemu-tty", "test-qemu-ramfs", "test-qemu-vfs", "test-qemu-elf-spawn",
    "test-qemu-vm-ready-early",
    "test-qemu-vm-self-fault", "test-qemu-vm-self-fault-sealed",
    "test-qemu-bootstrap-ready-timeout", "test-qemu-bootstrap-manifest-panic",
    "test-qemu-ipc", "test-qemu-ipc-ecall-core", "test-qemu-ipc-syscall",
    "test-qemu-ipc-syscall-panic", "test-qemu-nested-trap", "test-qemu-frame-ownership",
    "test-qemu-user-address-space", "test-qemu-address-space-handoff",
    "test-qemu-user-execution", "test-qemu-scheduler",
    "test-qemu-scheduler-invalid-outgoing", "test-qemu-scheduler-invalid-next",
)
SYSCALL_WORKFLOWS = tuple(name for name in QEMU_WORKFLOWS if name in {
    "test-qemu-uart-console", "test-qemu-grant-syscall", "test-qemu-user-runtime",
    "test-qemu-bootstrap-launcher", "test-qemu-vm-handoff", "test-qemu-pm-service",
    "test-qemu-vm-scratch-preemption", "test-qemu-tty", "test-qemu-ramfs", "test-qemu-vfs",
    "test-qemu-elf-spawn",
    "test-qemu-vm-ready-early", "test-qemu-vm-self-fault", "test-qemu-vm-self-fault-sealed",
    "test-qemu-bootstrap-ready-timeout", "test-qemu-bootstrap-manifest-panic",
    "test-qemu-ipc-ecall-core", "test-qemu-ipc-syscall", "test-qemu-ipc-syscall-panic",
    "test-qemu-user-address-space", "test-qemu-address-space-handoff",
})
GENERATOR_BODY_HASHES = {
    "_format_bytes": "63b813ed21c6102b23105adf9a68b5d65f74b9665eecf621cad2a0cf1cb864fe",
    "render_fixture": "dbf04467bf19ec0f18a1fc8ff7fda423956702b15f735cdcceb064ad80b5e55e",
    "parse_arguments": "0784ba2482481dc842070a8928af06bc1ac975bd1a0ee3d4df823b627b0d8034",
    "main": "4a18380984ff8c2639fe5a4459f858076ad738c9a7fc0028b79f7de84429413d",
    "containing_load": "5cc08d3bbf24428cd92be98fa23573137a64c1071bbd54e7a4c8f6a331357f22",
}
FIXTURE_SYMBOLS = (
    "micros_user_runtime_test_config", "micros_user_runtime_test_rodata",
    "micros_user_runtime_test_rodata_fault", "micros_user_runtime_test_rodata_resume",
    "micros_runtime_service_returned", "__micros_user_data_start", "__micros_user_data_end",
    "__micros_user_bss_start", "__micros_user_bss_end", "__micros_user_image_end",
)
FIXTURE_FIELDS = (
    "config_address", "rodata_address", "rodata_fault_address", "rodata_resume_address",
    "service_returned_address", "data_start", "data_end", "bss_start", "bss_end", "image_end",
)
RUNTIME_MARKER = (
    "MICROS_USER_RUNTIME_TEST_PASS elf=freestanding startup=validated syscalls=1-10 "
    "registers=preserved stack=external data=initialized bss=zero rodata=protected "
    "return=trapped cleanup=complete\n"
)


def read_source(relative: str) -> str:
    return (REPOSITORY_ROOT / relative).read_text(encoding="utf-8")


@lru_cache(maxsize=256)
def c_function_body(text: str, name: str) -> str:
    match = re.search(
        rf"(?m)^[ \t]*(?:[A-Za-z_][A-Za-z0-9_]*[\s*]+)+"
        rf"\b{re.escape(name)}\s*\([^;{{}}]*\)\s*\{{", c_lexical_text(text),
    )
    if match is None:
        raise AssertionError(f"missing typed function definition: {name}")
    return lexical_function_body(text[match.start():], name)


def all_sources() -> tuple[str, ...]:
    return tuple(path for paths in SOURCE_SETS.values() for path in paths)


def owner_specs() -> tuple[tuple[str, str, str, str], ...]:
    return tuple(
        (old, final, name, prefix + name if prefix and not name.startswith("micros_") else name)
        for old, final, prefix, names in OWNER_GROUPS for name in names
    )


def family_for(path: str) -> str:
    for family, paths in SOURCE_SETS.items():
        if path in paths or path == FAMILY_HEADERS[family]:
            return family
    raise AssertionError(f"not an accepted source/private owner: {path}")


def family_paths(path: str) -> tuple[str, ...]:
    return SOURCE_SETS[family_for(path)]


def canonical_names(text: str, family: str) -> str:
    for old, _, name, qualified in owner_specs():
        if family_for(old) == family and qualified != name:
            text = re.sub(rf"\b{re.escape(qualified)}\b", name, text)
    return text


def active_anchors() -> set[tuple[str, str]]:
    return set(LONG_FUNCTION_MIGRATION) | {
        (old, name) for old, _, name in SYMBOL_OWNER_MIGRATION
    }


def helper_anchor(old: str, final: str, name: str) -> tuple[str, str] | None:
    if old == final:
        return (old, name) if (old, name) in LONG_FUNCTION_REQUIREMENTS else None
    if name == "trap_panic_test_has_expected_exception":
        return old, "micros_trap_run_panic_test"
    if (old, name) in LONG_FUNCTION_REQUIREMENTS:
        return old, name
    return HELPER_MIGRATION_ANCHORS[final]


@lru_cache(maxsize=32)
def cached_definitions(items: tuple[tuple[str, str], ...]) -> dict[str, dict[str, int]]:
    texts = dict(items)
    definitions = {path: c_function_definitions(text) for path, text in texts.items()}
    for path, names in definitions.items():
        lexical = c_lexical_text(texts[path])
        for name in names:
            count = len(re.findall(
                rf"(?m)^[ \t]*(?:[A-Za-z_][A-Za-z0-9_]*[\s*]+)+"
                rf"\b{re.escape(name)}\s*\([^;{{}}]*\)\s*\{{", lexical,
            ))
            if count != 1:
                raise AssertionError(f"duplicate function definition: {path}:{name}")
    return definitions


def source_definitions(texts: dict[str, str]) -> dict[str, dict[str, int]]:
    return cached_definitions(tuple(texts.items()))


def find_owner(
    texts: dict[str, str], old: str, name: str,
) -> tuple[str, str]:
    spec = [item for item in owner_specs() if (item[0], item[2]) == (old, name)]
    if len(spec) != 1:
        raise AssertionError(f"missing exact helper specification: {(old, name)}")
    _, final, _, qualified = spec[0]
    definitions = source_definitions(texts)
    spellings = {name, qualified}
    candidates = [
        (path, spelling) for path, names in definitions.items()
        for spelling in spellings if spelling in names and family_for(path) == family_for(old)
    ]
    if len(candidates) != 1:
        raise AssertionError(f"expected one helper owner: {(old, name, candidates)}")
    path, spelling = candidates[0]
    if path not in {old, final} or (path == old and spelling != name):
        raise AssertionError(f"wrong helper owner: {(old, final, name, candidates)}")
    moving = (old, final, name)
    if moving in INITIAL_SYMBOL_OWNER_MIGRATION:
        expected = old if moving in SYMBOL_OWNER_MIGRATION else final
        if path != expected:
            raise AssertionError(f"pending/final symbol owner mismatch: {(name, path, expected)}")
    elif path == old and old != final and helper_anchor(old, final, name) not in active_anchors():
        raise AssertionError(f"helper outlived its exact migration anchor: {(old, name)}")
    return path, spelling


def helper_body(texts: dict[str, str], old: str, name: str) -> str:
    path, spelling = find_owner(texts, old, name)
    return projected_body(texts, path, spelling, expand=False)


def expanded_helper_body(texts: dict[str, str], old: str, name: str) -> str:
    path, spelling = find_owner(texts, old, name)
    return projected_body(texts, path, spelling, expand=True)


def local_include_closure(relative: str) -> tuple[str, ...]:
    pending = [relative]
    observed = set()
    while pending:
        path = pending.pop()
        if path in observed:
            continue
        observed.add(path)
        text = read_source(path)
        c_lexical_text(text)
        if re.search(r"(?m)^\s*#\s*include\s+(?![<\"\s])", text):
            raise AssertionError(f"computed local include: {path}")
        includes = re.findall(r'(?m)^\s*#\s*include\s*([<"])([^">]+)[">]', text)
        for delimiter, include in includes:
            if Path(include).suffix in {".c", ".s", ".S", ".py"}:
                raise AssertionError(f"textual source include: {path}:{include}")
            resolved = vm_layout.resolve_local_include(REPOSITORY_ROOT / path, include)
            if resolved is None:
                if delimiter == '"':
                    raise AssertionError(f"unresolved local include: {path}:{include}")
                continue
            if REPOSITORY_ROOT not in resolved.parents:
                raise AssertionError(f"external local include: {path}:{include}")
            pending.append(str(resolved.relative_to(REPOSITORY_ROOT)))
        if re.search(r"\b(?:static|extern)\b", c_lexical_text(macro_definition_text(text))):
            raise AssertionError(f"macro-hidden storage: {path}")
    return tuple(sorted(observed))


def header_executable_text(relative: str) -> str:
    selected = []
    for dependency in local_include_closure(relative):
        if dependency == relative:
            continue
        text = read_source(dependency)
        selected.append(macro_definition_text(text))
        selected.extend(c_function_body(text, name) for name in c_function_definitions(text))
    return "\n".join(selected)


def check_header(relative: str, text: str, private: bool) -> None:
    definitions = c_function_definitions(text)
    if file_scope_objects(text):
        raise AssertionError(f"header object definitions: {relative}")
    if definitions:
        if private or relative not in EXISTING_INLINE_HEADERS:
            raise AssertionError(f"new header algorithm: {relative}")
        if hashlib.sha256(text.encode()).hexdigest() != EXISTING_INLINE_HEADERS[relative]:
            raise AssertionError(f"changed existing inline dependency: {relative}")
    if not private:
        return
    if re.search(r"\b(?:static|inline|_Thread_local)\b", c_declaration_text(text)):
        raise AssertionError(f"private storage/inline declaration: {relative}")
    macros = re.sub(r"\\\n\s*", " ", macro_definition_text(text))
    for macro in re.finditer(
        r"(?m)^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)([^\n]*)", macros,
    ):
        name, body = macro.groups()
        if name == "EXPECT_TRUE" and relative == FAMILY_HEADERS["MICROS_RUNTIME_HOST_TEST_SOURCES"]:
            if compact_hash(macro.group()) != EXPECT_TRUE_HASH:
                raise AssertionError("the existing native assertion macro changed")
            continue
        if body.startswith("(") or re.search(r"[;{}=]|\b(?:do|if|return|while)\b", body):
            raise AssertionError(f"private executable macro: {relative}:{name}")
        identifiers = re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", c_lexical_text(body))
        if any(
            not token.isupper() and token not in {
                "sizeof", "_Alignof", "uintptr_t", "size_t", "uint8_t", "uint16_t",
                "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
                "unsigned", "signed", "int", "long", "short", "char", "bool", "true", "false",
            }
            for token in identifiers
        ):
            raise AssertionError(f"private macro hides a callable dependency: {relative}:{name}")


def record_fields(text: str, name: str) -> tuple[str, ...]:
    matches = re.findall(
        rf"\b(?:struct|union)\s+{re.escape(name)}\s*\{{([^{{}}]*)\}}",
        c_declaration_text(text),
    )
    if len(matches) != 1:
        raise AssertionError(f"expected one exact record type: {name}")
    return tuple(compact(field + ";") for field in matches[0].split(";") if field.strip())


def matching_close(text: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    raise AssertionError("unterminated control predicate")


ROLE_FIELDS = {
    "message": "struct micros_ipc_message message;",
    "canary_before": "volatile uint64_t canary_before;",
    "canary_after": "volatile uint64_t canary_after;",
    "read_grant": "micros_grant_t read_grant;",
    "write_grant": "micros_grant_t write_grant;",
    "probe_result": "uint64_t probe_result;",
    "index": "size_t index;",
    "reply": "struct micros_ipc_message reply;",
    "token": "uint64_t token;",
}
ROLE_INITIALIZERS = {
    "canary_before": "MICROS_USER_RUNTIME_TEST_CANARY_BEFORE",
    "canary_after": "MICROS_USER_RUNTIME_TEST_CANARY_AFTER",
    "read_grant": "MICROS_GRANT_NONE",
    "write_grant": "MICROS_GRANT_NONE",
}
ROLE_MEMBERS = {
    "run_client": ("message", "canary_before", "canary_after", "read_grant", "write_grant", "probe_result", "index"),
    "run_server": ("message", "reply", "read_grant", "write_grant", "token", "index"),
    "run_peer": ("message",),
}
HART_FRAME_PARAMETERS = ("struct micros_hart *hart", "struct micros_trap_frame *frame")
ORIGINAL_PARAMETERS = {
    "kernel/trap.c": {
        "fail_active_bootstrap_service_trap": ("struct micros_hart *hart",),
        "record_tty_owner_failure": (),
        "handle_vm_self_fault": (*HART_FRAME_PARAMETERS, "uint64_t cause_code"),
        "read_sscratch": (), "read_stvec": (), "read_tp": (), "read_sstatus": (),
        "frame_is_on_primary_stack": ("const struct micros_hart *hart", "const struct micros_trap_frame *frame"),
        "resolve_trap_hart": ("const struct micros_trap_frame *frame",),
        "handle_scheduler_timer_error": (*HART_FRAME_PARAMETERS, "enum micros_scheduler_error error"),
        "micros_trap_install": (),
        "trap_test_has_entry_registers": ("const struct micros_trap_frame *frame",),
        "trap_test_has_expected_exception": ("const struct micros_trap_frame *frame",),
        "trap_test_has_sum_restore_exception": ("const struct micros_trap_frame *frame",),
        "trap_test_prepare_return": ("struct micros_trap_frame *frame",),
        "trap_test_has_return_registers": (),
        "trap_test_prepare_sum_restore_return": ("struct micros_trap_frame *frame",),
        "trap_panic_test_has_expected_exception": ("const struct micros_trap_frame *frame",),
        "micros_trap_dispatch": ("struct micros_trap_frame *frame",),
        "micros_trap_nested_panic": ("struct micros_hart *hart", "const struct micros_trap_frame *outer_frame"),
        "micros_trap_run_self_test": (), "micros_trap_run_panic_test": (),
    },
    "kernel/syscall.c": {
        "panic_syscall": (*HART_FRAME_PARAMETERS, "const char *reason"),
        "capture_arguments": ("const struct micros_trap_frame *frame",),
        "decode_vm_handoff_request": (
            "const struct micros_syscall_arguments *arguments",
            "union micros_vm_handoff_decoded_request *request",
        ),
        "micros_syscall_handle_user_ecall": HART_FRAME_PARAMETERS,
    },
    "kernel/user_runtime_test.c": {
        "clear_bytes": ("void *storage", "size_t size"),
        "fill_bytes": ("void *storage", "unsigned char value", "size_t size"),
        "copy_bytes": ("void *destination", "const void *source", "size_t size"),
        "bytes_equal": ("const void *left", "const void *right", "size_t size"),
        "bytes_are_zero": ("const void *storage", "size_t size"),
        "read_satp": (), "segment_permissions": ("uint32_t flags",),
        "find_page": ("size_t actor", "uint64_t virtual_address"),
        "allocate_image_pages": ("size_t actor",), "copy_image_segments": ("size_t actor",),
        "verify_loaded_pages": ("size_t actor",),
        "translate_range": ("size_t actor", "uint64_t address", "size_t size", "uint32_t required_permissions", "uint64_t *physical"),
        "patch_configuration": ("size_t actor",), "snapshot_writable_pages": ("size_t actor",),
        "writable_pages_match_snapshot": ("size_t actor",),
        "prepare_process": ("struct micros_kernel_objects *objects", "size_t actor"),
        "current_actor": ("const struct micros_hart *hart",),
        "read_configuration": ("size_t actor", "struct micros_user_runtime_test_config *config"),
        "configurations_complete": (),
        "hold_other_threads": ("struct micros_kernel_objects *objects", "size_t current"),
        "micros_user_runtime_test_before_ecall": HART_FRAME_PARAMETERS,
        "micros_user_runtime_test_after_dispatch": (*HART_FRAME_PARAMETERS, "enum micros_syscall_return syscall_return"),
        "micros_user_runtime_test_handle_trap": HART_FRAME_PARAMETERS,
        "close_endpoint": ("struct micros_kernel_objects *objects", "size_t actor"),
        "release_process": ("struct micros_kernel_objects *objects", "size_t actor"),
        "endpoint_registry_is_empty": (), "scheduler_is_idle": ("const struct micros_kernel_objects *objects",),
        "micros_user_runtime_test_finish": (), "micros_user_runtime_runtime_run_self_test": (),
    },
    "tests/qemu/user_runtime_service.c": {
        "bytes_equal": ("const volatile uint8_t *left", "const uint8_t *right", "size_t length"),
        "bytes_are_zero": ("const volatile uint8_t *bytes", "size_t length"),
        "put_u32": ("uint8_t *bytes", "uint32_t value"), "get_u32": ("const uint8_t *bytes",),
        "get_u64": ("const uint8_t *bytes",),
        "fill_message": ("struct micros_ipc_message *message", "uint32_t type", "uint8_t seed"),
        "message_matches": (
            "const struct micros_ipc_message *message", "micros_endpoint_t source",
            "uint32_t type", "uint8_t seed", "bool require_token",
        ),
        "notification_matches": ("const struct micros_ipc_message *message", "micros_endpoint_t source"),
        "initial_bss_is_zero": (), "memory_support_works": (), "startup_checks": (),
        "run_client": (), "run_peer": (), "run_server": (), "micros_service_main": (),
    },
    "tests/host/runtime_test.c": {
        "reset_capture": ("int64_t result",),
        "micros_runtime_raw_syscall": tuple(f"uint64_t a{index}" for index in range(8)),
        "arguments_match": tuple(f"uint64_t a{index}" for index in range(8)),
        "message_pattern": ("uint32_t type", "uint8_t seed"),
        "test_ipc_wrappers": (), "test_grant_wrappers": (),
        "test_bootstrap_launcher_console_begin": (), "test_memory_support": (), "main": (),
    },
}
PHASE_PREFIXES = {
    "MICROS_TRAP_SOURCES": "trap_",
    "MICROS_SYSCALL_SOURCES": "syscall_",
    "MICROS_USER_RUNTIME_KERNEL_TEST_SOURCES": "user_runtime_test_",
    "MICROS_USER_RUNTIME_SERVICE_TEST_SOURCES": "user_runtime_service_",
    "MICROS_RUNTIME_HOST_TEST_SOURCES": "runtime_test_",
}


def replace_code(text: str, pattern: str, replacement) -> str:
    for match in reversed(tuple(re.finditer(pattern, c_lexical_text(text)))):
        selected = replacement(match) if callable(replacement) else match.expand(replacement)
        text = text[:match.start()] + selected + text[match.end():]
    return text


def split_arguments(text: str) -> tuple[str, ...]:
    lexical = c_lexical_text(text)
    depth = 0
    start = 0
    selected = []
    for index, char in enumerate(lexical):
        depth += char in "([{"
        depth -= char in ")]}"
        if char == "," and depth == 0:
            selected.append(text[start:index].strip())
            start = index + 1
    if depth:
        raise AssertionError("unbalanced typed argument transport")
    if text[start:].strip() not in {"", "void"}:
        selected.append(text[start:].strip())
    return tuple(selected)


def parameter(declaration: str) -> tuple[str, str]:
    match = re.fullmatch(r"(.+?[\s*])([A-Za-z_]\w*)", declaration.strip(), re.DOTALL)
    if match is None:
        raise AssertionError(f"expected a named typed parameter: {declaration}")
    return compact(match.group(1)), match.group(2)


@lru_cache(maxsize=512)
def function_signature(text: str, name: str) -> tuple[str, tuple[tuple[str, str], ...]]:
    match = re.search(
        rf"(?m)^[ \t]*((?:[A-Za-z_][A-Za-z0-9_]*[\s*]+)+)"
        rf"\b{re.escape(name)}\s*\(([^;{{}}]*)\)\s*\{{", c_lexical_text(text),
    )
    if match is None:
        raise AssertionError(f"missing typed function signature: {name}")
    result = compact(re.sub(r"\b(?:static|_Noreturn)\b", "", match.group(1)))
    return result, tuple(parameter(item) for item in split_arguments(match.group(2)))


def original_parameters(path: str, name: str) -> tuple[tuple[str, str], ...] | None:
    for old, _, local, qualified in owner_specs():
        if family_for(path) == family_for(old) and name in {local, qualified}:
            return tuple(parameter(item) for item in ORIGINAL_PARAMETERS[old][local])
    return None


def projection_sources(texts: dict[str, str], path: str) -> dict[str, str]:
    family = family_for(path)
    paths = (*SOURCE_SETS[family], FAMILY_HEADERS[family])
    return {source: texts[source] if source in texts else read_source(source) for source in paths}


@lru_cache(maxsize=32)
def context_records(items: tuple[tuple[str, str], ...], family: str) -> dict[str, tuple[str, tuple[str, ...]]]:
    text = "\n".join(value for _, value in items)
    records = {}
    global_fields = {
        "MICROS_TRAP_SOURCES": ("trap", TRAP_FIELDS),
        "MICROS_USER_RUNTIME_KERNEL_TEST_SOURCES": ("runtime", KERNEL_FIELDS),
        "MICROS_RUNTIME_HOST_TEST_SOURCES": ("capture", RAW_FIELDS),
    }
    for match in re.finditer(r"\bstruct\s+([A-Za-z_]\w*)\s*\{([^{}]*)\}", c_declaration_text(text)):
        name = match.group(1)
        fields = tuple(part.strip() + ";" for part in match.group(2).split(";") if part.strip())
        if family in global_fields and Counter(map(compact, fields)) == Counter(map(compact, global_fields[family][1])):
            if name in records:
                raise AssertionError(f"duplicate context record: {name}")
            records[name] = global_fields[family][0], fields
        elif family == "MICROS_USER_RUNTIME_SERVICE_TEST_SOURCES" and fields:
            members = tuple(re.search(r"([A-Za-z_]\w*)(?:\[[^\]]*\])*\s*;$", field).group(1) for field in fields)
            if any(set(members) <= set(values) for values in ROLE_MEMBERS.values()) and (
                tuple(map(compact, fields)) == tuple(compact(ROLE_FIELDS[member]) for member in members)
            ):
                if name in records:
                    raise AssertionError(f"duplicate automatic context record: {name}")
                records[name] = "role", fields
    return records


def context_storage(
    sources: dict[str, str], type_name: str, mode: str, fields: tuple[str, ...],
) -> str:
    candidates = []
    for path, text in sources.items():
        for declaration in file_scope_objects(text):
            if re.search(rf"\bstruct\s+{re.escape(type_name)}\b", declaration):
                candidates.append((path, declaration))
    if mode == "role":
        if candidates:
            raise AssertionError("automatic role context acquired static/file-scope lifetime")
        return ""
    owner = {
        "trap": "kernel/trap_test.c",
        "runtime": "kernel/user_runtime_test_state.c",
        "capture": "tests/host/runtime_test_fixture.c",
    }[mode]
    if mode == "capture" and any(path == "tests/host/runtime_test.c" for path, _ in candidates):
        owner = "tests/host/runtime_test.c"
    initializer = (
        r"\s*=\s*\{\s*\.returned_actor\s*=\s*USER_RUNTIME_PROCESS_COUNT\s*,?\s*\}"
        if mode == "runtime" else ""
    )
    if len(candidates) != 1 or candidates[0][0] != owner:
        raise AssertionError(f"context must borrow its one exact storage owner: {type_name}")
    match = re.fullmatch(
        rf"static\s+struct\s+{re.escape(type_name)}\s+([A-Za-z_]\w*){initializer}\s*;",
        candidates[0][1], re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"context storage/initializer drift: {type_name}")
    if mode != "capture":
        originals = {compact("static " + field) for field in fields}
        if any(compact(declaration) in originals for text in sources.values() for declaration in file_scope_objects(text)):
            raise AssertionError(f"duplicated original/context state: {type_name}")
    return match.group(1)


def borrowed_context(sources: dict[str, str], name: str, type_name: str, storage: str, prefix: str) -> None:
    owners = [path for path, text in sources.items() if name in c_function_definitions(text)]
    if len(owners) != 1 or not name.startswith(prefix):
        raise AssertionError(f"not one family-owned context borrow: {name}")
    text = sources[owners[0]]
    result, parameters = function_signature(text, name)
    if result not in {f"struct{type_name}*", f"conststruct{type_name}*"} or parameters:
        raise AssertionError(f"changed typed context borrow: {name}")
    if compact(c_function_body(text, name)) != f"{{return&{storage};}}":
        raise AssertionError(f"context borrow hides executable work: {name}")


def typed_variables(body: str, parameters: tuple[tuple[str, str], ...]) -> dict[str, str]:
    result = {name: type_name for type_name, name in parameters}
    type_pattern = (
        r"(?:(?:const|volatile|static)\s+)*"
        r"(?:(?:struct|enum|union)\s+[A-Za-z_]\w*|"
        r"(?:unsigned\s+)?char|bool|int|size_t|uintptr_t|u?int(?:8|16|32|64)_t|micros_[A-Za-z_]\w*_t)"
        r"\s*[*\s]*"
    )
    for match in re.finditer(
        rf"(?m)^[ \t]*({type_pattern})([A-Za-z_]\w*)\s*(\[[^\];]*\])?\s*(?=[;=])",
        c_lexical_text(body),
    ):
        type_name = compact(re.sub(r"\bstatic\b", "", match.group(1)))
        name = match.group(2)
        result[name] = type_name + ("*" if match.group(3) else "")
        if match.group(3):
            result[f"sizeof({name})/sizeof({name}[0])"] = "size_t"
    return result


def bind_arguments(
    body: str, parameters: tuple[tuple[str, str], ...], arguments: tuple[str, ...],
    variables: dict[str, str], sources: dict[str, str], family: str,
) -> str:
    if len(parameters) != len(arguments):
        raise AssertionError("phase argument count changed")
    bindings = {}
    for (expected, name), argument in zip(parameters, arguments):
        actual = compact(argument)
        addressed = actual.startswith("&")
        variable = actual[1:] if addressed else actual
        borrow = re.fullmatch(r"([A-Za-z_]\w*)\(\)", actual)
        context = re.fullmatch(r"(?:const)?struct([A-Za-z_]\w*)\*", expected)
        schemas = context_records(tuple(sources.items()), family)
        if borrow and context and context.group(1) in schemas:
            mode, fields = schemas[context.group(1)]
            if mode == "role":
                raise AssertionError("an automatic role is not a singleton borrow")
            storage = context_storage(sources, context.group(1), mode, fields)
            borrowed_context(sources, borrow.group(1), context.group(1), storage, PHASE_PREFIXES[family])
            supplied = expected
        elif variable in variables and (
            not addressed or re.fullmatch(r"[A-Za-z_]\w*", variable)
        ):
            supplied = variables[variable] + ("*" if addressed else "")
        else:
            raise AssertionError(f"unverified phase argument transport: {argument}")
        if expected != supplied and not (
            expected.startswith("const") and expected[5:] == supplied
        ):
            raise AssertionError(f"phase parameter type changed: {name}:{expected} <- {supplied}")
        bindings[name] = (variable, addressed)

    def member(match: re.Match) -> str:
        variable, addressed = bindings[match.group(1)]
        return variable + ("." if addressed else "->") + match.group(2)

    def reference(match: re.Match) -> str:
        dereference = match.group("dereference")
        variable, addressed = bindings[dereference or match.group("reference")]
        if dereference:
            return variable if addressed else "*" + variable
        return ("&" if addressed else "") + variable

    if bindings:
        names = "|".join(map(re.escape, bindings))
        body = replace_code(body, rf"\b({names})\s*->\s*([A-Za-z_]\w*)", member)
        body = replace_code(
            body,
            rf"(?<![\w*])\*\s*(?P<dereference>{names})\b"
            rf"|(?<![\w.>])\b(?P<reference>{names})\b(?!\s*(?:->|\.))",
            reference,
        )
    return body


def project_contexts(texts: dict[str, str], path: str, name: str, body: str) -> str:
    sources = projection_sources(texts, path)
    family = family_for(path)
    schemas = context_records(tuple(sources.items()), family)
    _, parameters = function_signature(texts[path], name)
    bindings = {}
    context_parameters = set()
    automatic_members = set()
    for type_name, variable in parameters:
        match = re.fullmatch(r"(?:const)?struct([A-Za-z_]\w*)\*", type_name)
        if match is not None and match.group(1) in schemas:
            context = match.group(1)
            mode, fields = schemas[context]
            context_storage(sources, context, mode, fields)
            bindings[variable] = (mode, fields, "->", context)
            context_parameters.add(variable)
    baseline = original_parameters(path, name)
    if baseline is not None:
        ordinary = tuple(item for item in parameters if item[1] not in context_parameters)
        if tuple(item[0] for item in ordinary) != tuple(item[0] for item in baseline):
            raise AssertionError(f"original helper parameter types/order changed: {name}")
        names = {actual[1]: expected[1] for actual, expected in zip(ordinary, baseline)}
        if names:
            body = replace_code(
                body, r"(?<![\w.>])\b(" + "|".join(map(re.escape, names)) + r")\b",
                lambda match: names[match.group()],
            )
    if not schemas:
        return body
    for context, (mode, fields) in schemas.items():
        if mode != "role":
            storage = context_storage(sources, context, mode, fields)
            bindings.setdefault(storage, (mode, fields, ".", context))
            pattern = (
                rf"(?:const\s+)?struct\s+{re.escape(context)}\s*\*\s*([A-Za-z_]\w*)\s*"
                r"=\s*([A-Za-z_]\w*)\s*\(\s*\)\s*;"
            )
            for match in reversed(tuple(re.finditer(pattern, c_lexical_text(body)))):
                variable, borrow = match.groups()
                borrowed_context(sources, borrow, context, storage, PHASE_PREFIXES[family])
                bindings[variable] = (mode, fields, "->", context)
                body = body[:match.start()] + body[match.end():]
        else:
            context_storage(sources, context, mode, fields)
            pattern = (
                rf"(?m)^[ \t]*(static\s+)?struct\s+{re.escape(context)}\s+([A-Za-z_]\w*)"
                r"\s*(?:=\s*\{([^{}]*)\})?\s*;"
            )
            for match in reversed(tuple(re.finditer(pattern, c_lexical_text(body)))):
                if match.group(1):
                    raise AssertionError("automatic role context acquired static lifetime")
                variable = match.group(2)
                role = canonical_names(name, family)
                members = [re.search(r"([A-Za-z_]\w*);$", field).group(1) for field in fields]
                if role not in ROLE_MEMBERS or not set(members) <= set(ROLE_MEMBERS[role]):
                    raise AssertionError("automatic role state left its original role invocation")
                if automatic_members & set(members):
                    raise AssertionError("duplicated automatic role storage")
                automatic_members.update(members)
                members = [member for member in ROLE_MEMBERS[role] if member in members]
                expected = {
                    member: ROLE_INITIALIZERS[member]
                    for member in members if role == "run_client" and member in ROLE_INITIALIZERS
                }
                initializers = {}
                initializer = (match.group(3) or "").strip()
                for item in split_arguments("" if initializer == "0" else initializer):
                    value = re.fullmatch(r"\.([A-Za-z_]\w*)\s*=\s*([A-Za-z_]\w*)", item)
                    if value is None or value.group(1) in initializers:
                        raise AssertionError("unverified automatic context initializer")
                    initializers[value.group(1)] = value.group(2)
                end = match.end()
                if not initializer or initializer == "0":
                    for _ in expected:
                        assignment = re.match(
                            rf"\s*{re.escape(variable)}\.([A-Za-z_]\w*)\s*=\s*([A-Za-z_]\w*)\s*;",
                            c_lexical_text(body[end:]),
                        )
                        if assignment is None or assignment.group(1) in initializers:
                            raise AssertionError("automatic context lost its original initialization")
                        initializers[assignment.group(1)] = assignment.group(2)
                        end += assignment.end()
                if initializers != expected:
                    raise AssertionError("automatic canary/grant initialization changed")
                declarations = []
                for member in members:
                    declaration = ROLE_FIELDS[member]
                    if member in initializers:
                        declaration = declaration[:-1] + " = " + initializers[member] + ";"
                    declarations.append(declaration)
                bindings[variable] = (mode, fields, ".", context)
                body = body[:match.start()] + "\n".join(declarations) + body[end:]
    definitions = source_definitions({source: text for source, text in sources.items() if source.endswith(".c")})
    legacy = {spelling for old, _, local, qualified in owner_specs()
              if family_for(old) == family for spelling in (local, qualified)}
    for call in reversed(tuple(re.finditer(r"\b([A-Za-z_]\w*)\s*\(", c_lexical_text(body)))):
        called = call.group(1)
        providers = [source for source, names in definitions.items() if called in names]
        if called not in legacy or not providers:
            continue
        if len(providers) != 1:
            raise AssertionError(f"duplicate helper call provider: {called}")
        _, supplied = function_signature(sources[providers[0]], called)
        extra = [
            index for index, (ctype, _) in enumerate(supplied)
            if re.fullmatch(r"(?:const)?struct([A-Za-z_]\w*)\*", ctype)
            and re.fullmatch(r"(?:const)?struct([A-Za-z_]\w*)\*", ctype).group(1) in schemas
        ]
        if not extra:
            continue
        closing = matching_close(c_lexical_text(body), body.index("(", call.start()))
        arguments = split_arguments(body[body.index("(", call.start()) + 1:closing])
        if len(arguments) != len(supplied):
            raise AssertionError(f"helper argument count changed: {called}")
        for index in extra:
            ctype = re.fullmatch(r"(?:const)?struct([A-Za-z_]\w*)\*", supplied[index][0]).group(1)
            argument = compact(arguments[index])
            addressed = argument.startswith("&")
            variable = argument[1:] if addressed else argument
            binding = bindings.get(variable)
            borrow = re.fullmatch(r"([A-Za-z_]\w*)\(\)", argument)
            if binding is None and borrow:
                mode, fields = schemas[ctype]
                if mode != "role":
                    storage = context_storage(sources, ctype, mode, fields)
                    borrowed_context(sources, borrow.group(1), ctype, storage, PHASE_PREFIXES[family])
                    binding = (mode, fields, "->", ctype)
            if binding is None or binding[0] == "role" or binding[3] != ctype or (
                binding[2] != ("." if addressed else "->")
            ):
                raise AssertionError(f"unverified extra context argument: {called}:{argument}")
        ordinary = tuple(item for index, item in enumerate(supplied) if index not in extra)
        expected = original_parameters(path, called)
        if expected is None or tuple(item[0] for item in ordinary) != tuple(item[0] for item in expected):
            raise AssertionError(f"helper parameter shape changed: {called}")
        opening = body.index("(", call.start())
        body = body[:opening + 1] + ", ".join(arg for index, arg in enumerate(arguments) if index not in extra) + body[closing:]
    for variable, (mode, fields, operator, _) in bindings.items():
        members = {re.search(r"([A-Za-z_]\w*)(?:\[[^\]]*\])*\s*;$", field).group(1) for field in fields}

        def member(match: re.Match) -> str:
            if match.group(1) != operator or match.group(2) not in members:
                raise AssertionError("context member/operator changed")
            return ("capture." if mode == "capture" else "") + match.group(2)

        body = replace_code(body, rf"\b{re.escape(variable)}\s*(->|\.)\s*([A-Za-z_]\w*)", member)
    return body


def projected_body(
    texts: dict[str, str], path: str, name: str, expand: bool,
    active: tuple[tuple[str, str], ...] = (),
) -> str:
    """Project only checked context transport and statement/tail/boolean phases."""
    if (path, name) in active:
        raise AssertionError(f"recursive phase helper: {(path, name)}")
    body = c_function_body(texts[path], name)
    if not expand:
        return canonical_names(project_contexts(texts, path, name, body), family_for(path))
    caller_result, parameters = function_signature(texts[path], name)
    variables = typed_variables(body, parameters)
    for declaration in file_scope_objects(texts[path]):
        constant = re.fullmatch(
            r"static\s+const\s+(uint64_t)\s+([A-Z_][A-Z0-9_]*)\s*"
            r"=\s*UINT64_C\(\s*(?:0x[0-9a-fA-F]+|[0-9]+)\s*\)\s*;",
            declaration,
        )
        if constant is not None:
            variables.setdefault(constant.group(2), constant.group(1))
    family = family_for(path)
    sources = projection_sources(texts, path)
    known = {spelling for old, _, local, qualified in owner_specs()
             if family_for(old) == family for spelling in (local, qualified)}
    definitions = source_definitions({source: text for source, text in texts.items() if source.endswith(".c")})
    calls = tuple(re.finditer(r"\b([A-Za-z_]\w*)\s*\(", c_lexical_text(body)))
    for call in reversed(calls):
        called = call.group(1)
        providers = [source for source in family_paths(path) if called in definitions.get(source, {})]
        if called in known or not providers:
            continue
        if len(providers) != 1 or not called.startswith(PHASE_PREFIXES[family]):
            raise AssertionError(f"not one named family phase: {called}")
        source = providers[0]
        result, phase_parameters = function_signature(texts[source], called)
        context_type = re.fullmatch(r"(?:const)?struct([A-Za-z_]\w*)\*", result)
        schemas = context_records(tuple(projection_sources(texts, path).items()), family)
        if context_type and context_type.group(1) in schemas:
            continue
        if logical_owner(source, called) not in PERMITTED_OWNER_DEPENDENCIES[logical_owner(path, name)] | {logical_owner(path, name)}:
            raise AssertionError(f"upward/sibling phase dependency: {called}")
        opening = body.index("(", call.start())
        closing = matching_close(c_lexical_text(body), opening)
        phase = projected_body(texts, source, called, True, (*active, (path, name)))
        phase = bind_arguments(
            phase, phase_parameters, split_arguments(body[opening + 1:closing]),
            variables, sources, family,
        )[1:-1]
        start, end = call.start(), closing + 1
        before, after = body[:start], body[end:]
        if result == "void":
            tail = re.search(r"\breturn\s*;\s*$", c_statement_text(phase))
            if tail is not None:
                phase = phase[:tail.start()]
            local_returns = tuple(re.finditer(r"\breturn\b([^;]*);", c_statement_text(phase)))
            if local_returns:
                continuation = re.match(
                    r"\s*;\s*return\s*(true|false)?\s*;",
                    c_lexical_text(after),
                )
                value = continuation.group(1) if continuation is not None else None
                if continuation is not None and (
                    (caller_result == "void" and value is None)
                    or (caller_result == "bool" and value in {"true", "false"})
                ):
                    exit_statement = "return " + (value or "") + ";"
                elif caller_result == "void" and re.fullmatch(
                    r"\s*;\s*\}\s*", c_lexical_text(after),
                ):
                    exit_statement = "return;"
                else:
                    raise AssertionError(f"phase local return lacks a verified caller exit: {called}")
                if any(c_exit_value(phase[item.start(1):item.end(1)]) for item in local_returns):
                    raise AssertionError(f"value-producing local return in void phase: {called}")
                phase = replace_code(phase, r"\breturn\s*;", exit_statement)
        guard = re.search(r"\bif\s*\(\s*(!?)\s*$", before)
        if guard:
            suffix = re.match(r"\s*\)\s*\{\s*return\s*([^;{}]*);\s*\}", after)
            continuation = "return " + suffix.group(1) + ";" if suffix is not None else ""
            if suffix is None and family == "MICROS_USER_RUNTIME_KERNEL_TEST_SOURCES":
                suffix = re.match(r"\s*\)\s*\{\s*goto\s+failure\s*;\s*\}", after)
                labels = tuple(re.finditer(r"\bfailure\s*:", c_lexical_text(body)))
                if (
                    suffix is None
                    or guard.group(1) != "!"
                    or len(labels) != 1
                    or labels[0].start() <= closing
                    or re.match(r"\s*else\b", after[suffix.end():])
                    or re.search(r"\bgoto\b|(?m:^\s*[A-Za-z_]\w*\s*:)", c_lexical_text(phase))
                ):
                    raise AssertionError(f"unverified local failure phase guard: {called}")
                continuation = "goto failure;"
            if suffix is None or result != "bool":
                raise AssertionError(f"unverified phase result guard: {called}")
            success, failure = ("true", "false") if guard.group(1) else ("false", "true")
            tail = re.search(rf"\breturn\s+{success}\s*;\s*$", phase)
            if tail is None:
                raise AssertionError(f"phase lacks its exact fallthrough result: {called}")
            phase = phase[:tail.start()]
            returns = re.findall(r"\breturn\s+([^;]+);", c_lexical_text(phase))
            if any(compact(value) != failure for value in returns):
                raise AssertionError(f"unverified phase result transport: {called}")
            phase = replace_code(phase, rf"\breturn\s+{failure}\s*;", continuation)
            start, end = guard.start(), end + suffix.end()
        else:
            returning = re.search(r"\breturn\s*$", before)
            if returning:
                start = returning.start()
            elif result != "void":
                raise AssertionError(f"unverified value-producing phase: {called}")
            suffix = re.match(r"\s*;", after)
            if suffix is None:
                raise AssertionError(f"phase is not a statement/tail return: {called}")
            end += suffix.end()
        body = body[:start] + phase + body[end:]
    return canonical_names(project_contexts(texts, path, name, body), family)


def named_phases(
    texts: dict[str, str], path: str, name: str, active: tuple[tuple[str, str], ...] = (),
) -> set[tuple[str, str]]:
    if (path, name) in active:
        raise AssertionError("recursive named phase hierarchy")
    family = family_for(path)
    known = {spelling for old, _, local, qualified in owner_specs()
             if family_for(old) == family for spelling in (local, qualified)}
    definitions = source_definitions({source: text for source, text in texts.items() if source.endswith(".c")})
    sources = projection_sources(texts, path)
    schemas = context_records(tuple(sources.items()), family)
    phases = set()
    for called in re.findall(r"\b([A-Za-z_]\w*)\s*\(", c_lexical_text(c_function_body(texts[path], name))):
        providers = [source for source in family_paths(path) if called in definitions.get(source, {})]
        if called in known or not providers:
            continue
        if len(providers) != 1 or not called.startswith(PHASE_PREFIXES[family]):
            raise AssertionError("ambiguous or unqualified named phase")
        source = providers[0]
        result, _ = function_signature(texts[source], called)
        context = re.fullmatch(r"(?:const)?struct([A-Za-z_]\w*)\*", result)
        if context and context.group(1) in schemas:
            continue
        phases.add((source, called))
        phases.update(named_phases(texts, source, called, (*active, (path, name))))
    return phases


def c_statement_text(body: str) -> str:
    """Keep exact CPP branches in flow_events, not C call/assignment text."""
    return re.sub(
        r"(?m)^[ \t]*#(?:[^\n]*\\\n)*[^\n]*",
        lambda match: re.sub(r"[^\n]", " ", match.group()),
        c_lexical_text(body),
    )


def c_exit_value(expression: str) -> str:
    tokens = re.findall(
        r'''"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|/\*.*?\*/|//[^\n]*|[^\s]''',
        expression,
        re.DOTALL,
    )
    return "".join(token for token in tokens if not token.startswith(("//", "/*")))


def is_outlined_panic_continuation(body: str, position: int) -> bool:
    """Ignore only an unreachable void continuation after a standalone panic."""
    prefix = c_lexical_text(body[:position])
    calls = tuple(re.finditer(
        r"(?:^|[;{}])\s*MICROS_TRAP_PANIC\s*\(",
        prefix,
    ))
    if not calls:
        return False
    opening = prefix.index("(", calls[-1].start())
    closing = matching_close(prefix, opening)
    return prefix[closing + 1:].strip() == ";"


def flow_events(body: str) -> tuple[tuple[str, str], ...]:
    body = re.sub(r"\\\n\s*", " ", body)
    lexical = c_statement_text(body)
    events = []
    for match in re.finditer(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", lexical):
        name = match.group(1)
        if name in {"if", "for", "while", "switch"}:
            opening = lexical.index("(", match.start())
            closing = matching_close(lexical, opening)
            events.append((match.start(), "predicate:" + name, compact(lexical[opening + 1:closing])))
        elif name not in {"sizeof", "_Alignof", "_Alignas"}:
            events.append((match.start(), "call", name))
    for match in re.finditer(
        r"(?m)^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b([^\n]*)",
        body,
    ):
        events.append((match.start(), "cpp:" + match.group(1), compact(match.group(2).replace("\\\n", ""))))
    for match in re.finditer(r"\bcase\s+([^:]+):|\b(default)\s*:", lexical):
        events.append((match.start(), "case", compact(match.group(1) or match.group(2))))
    for match in re.finditer(r"\?", lexical):
        prefix = lexical[:match.start()]
        start = max(prefix.rfind(";"), prefix.rfind("{"), prefix.rfind("}")) + 1
        events.append((match.start(), "ternary", compact(prefix[start:])))
    for match in re.finditer(
        r"\b(return|goto|break|continue)\b([^;]*);",
        lexical,
    ):
        kind = match.group(1)
        value = c_exit_value(body[match.start(2):match.end(2)])
        if (
            kind == "return"
            and not value
            and is_outlined_panic_continuation(body, match.start())
        ):
            continue
        events.append((match.start(), "exit:" + kind, value))
    for match in re.finditer(
        r"(?:^|[;{}])\s*([A-Za-z_]\w*)\s*:",
        lexical,
    ):
        if match.group(1) != "default":
            events.append((match.start(1), "label", match.group(1)))
    for match in re.finditer(r'"(?:\\.|[^"\\])*"(?:\s*"(?:\\.|[^"\\])*")*', body):
        events.append((match.start(), "literal", "".join(string_records(match.group()))))
    return tuple((kind, value) for _, kind, value in sorted(events))


def flow_hash(events: tuple[tuple[str, str], ...]) -> str:
    return hashlib.sha256(json.dumps(events, separators=(",", ":")).encode()).hexdigest()


def call_arguments(body: str) -> tuple[tuple[str, str], ...]:
    lexical = c_statement_text(body)
    records = []
    for match in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", lexical):
        name = match.group(1)
        if name in {"if", "for", "while", "switch"}:
            continue
        opening = lexical.index("(", match.start())
        closing = matching_close(lexical, opening)
        records.append((name, compact(lexical[opening + 1:closing])))
    return tuple(records)


def state_effects(body: str) -> tuple[tuple[str, str], ...]:
    lexical = c_statement_text(body)
    records = []
    for match in re.finditer(r"<<=|>>=|[+\-*/%&|^]=|(?<![=!<>])=(?!=)|\+\+|--", lexical):
        prefix = lexical[:match.start()]
        start = max(prefix.rfind(";"), prefix.rfind("{"), prefix.rfind("}")) + 1
        ending = lexical.find(";", match.end())
        if ending < 0:
            raise AssertionError("unterminated state effect")
        records.append((match.group(), compact(lexical[start:ending])))
    return tuple(records)


def python_body_fingerprint(node: ast.FunctionDef) -> str:
    identifiers = {}

    def identifier(name: str) -> str:
        return identifiers.setdefault(name, f"id{len(identifiers)}")

    class RenameIdentifiers(ast.NodeTransformer):
        def visit_Name(self, selected: ast.Name) -> ast.Name:
            selected.id = identifier(selected.id)
            return self.generic_visit(selected)

        def visit_arg(self, selected: ast.arg) -> ast.arg:
            selected.arg = identifier(selected.arg)
            return self.generic_visit(selected)

        def visit_FunctionDef(self, selected: ast.FunctionDef) -> ast.FunctionDef:
            selected.name = identifier(selected.name)
            return self.generic_visit(selected)

    body = ast.Module(body=copy.deepcopy(node.body), type_ignores=[])
    normalized = RenameIdentifiers().visit(body)
    return hashlib.sha256(ast.dump(normalized, include_attributes=False).encode()).hexdigest()


def expanded_flow(
    texts: dict[str, str], path: str, name: str,
    active: tuple[tuple[str, str], ...] = (),
) -> tuple[tuple[str, str], ...]:
    return flow_events(projected_body(texts, path, name, True, active))


FLOW_HASHES = {
    ("kernel/trap.c", "handle_vm_self_fault"):
        "10cf4d87fdfe994d381602ebd62571fc55bf6f342242644195d9e28d73454436",
    ("kernel/trap.c", "micros_trap_dispatch"):
        "7a0f3d64d7e06b4377ebc265ddc97384f2ff941a7b3d1931eca878d4e5feabe4",
    ("kernel/syscall.c", "micros_syscall_handle_user_ecall"):
        "77b73b4ead6b26f742cf9f6c63525d25a7353232e070fa3d0964ae969308c988",
    ("kernel/user_runtime_test.c", "micros_user_runtime_runtime_run_self_test"):
        "86dd24387fcccee09e52601f702555f265176ea342f808446058720ce2fab469",
    ("tests/qemu/user_runtime_service.c", "run_client"):
        "0b156c9c4ff1b0ae2ce91d73372385c8e9889ccb49e01fdb9f6b2c9035202a40",
    ("tests/qemu/user_runtime_service.c", "run_server"):
        "520d29ba795f88fc20b1a6f91c28280f0d33283dcdbe309ce788f3280a7f6431",
    ("tests/host/runtime_test.c", "test_ipc_wrappers"):
        "34fc29d906519622aa79674e170b977b21c1cce5560fe3177ee1f14b5d29731e",
    ("tests/host/runtime_test.c", "test_grant_wrappers"):
        "cae796af2f3927f5e689a8762c6cf43204791cdd62bfacac955f96ef6082e6dd",
    ("kernel/user_runtime_test.c", "micros_user_runtime_test_finish"):
        "e63cb9d25364af7a93576eedc639a255afc65bbc9d568ada091019a02ef5ba2f",
    ("tests/qemu/user_runtime_service.c", "run_peer"):
        "f33445976217e63228f4dbdde2f62cfa58b983bb1571e0a17613fc73bdea9446",
    ("tests/qemu/user_runtime_service.c", "micros_service_main"):
        "da1d6a6e10de727dd65939b72d0adccfc34af1e3c966e07880bbdc4a4aec6f71",
    ("tests/host/runtime_test.c", "main"):
        "50aedaf4b8b34308a6ebd00fc6e6e3dc203f8f75040427777a7afcf3252f6d73",
    ("tests/host/runtime_test.c", "test_bootstrap_launcher_console_begin"):
        "e676ff632d681b9db032c6d972e8ff9b67a755f98c44303777b0000a5c0d0141",
    ("tests/host/runtime_test.c", "test_memory_support"):
        "2ab190366b843c3eea29a9b1703ebf6a048c0d3667b5194f81a253654ab431cb",
    ("kernel/trap.c", "micros_trap_nested_panic"):
        "3cbef9098bef9f966e80741f6cf94b22ed06e97cc902eece363c084dc629cf36",
    ("kernel/syscall.c", "decode_vm_handoff_request"):
        "ffeecf3cdb1d833b42102eb44a3d92bd8ede4f893669e82a606320832affd420",
}
CALL_ARGUMENT_HASHES = {
    ("kernel/trap.c", "handle_vm_self_fault"):
        "275b1a8752330c14b663bf2358b6af38b71b47225cbd76bb5280757252cb8ebd",
    ("kernel/trap.c", "micros_trap_dispatch"):
        "065409dacc924df45b82d4e0094c54e05e1548289244025da77741ab680aef0d",
    ("kernel/syscall.c", "micros_syscall_handle_user_ecall"):
        "427f59ffd7cc8bf23df261b7d10add022f6a45220b63ee7b8c4dca2ad038c7cc",
    ("kernel/user_runtime_test.c", "micros_user_runtime_runtime_run_self_test"):
        "695107c1802a789c1525efb3013979dc69f5be67c5687feb5d2458e1b1d3fad0",
    ("tests/qemu/user_runtime_service.c", "run_client"):
        "8998f561871551a66e4524a0d3625531c400cb8fb4393527b13306f87ed058f2",
    ("tests/qemu/user_runtime_service.c", "run_server"):
        "2b3697c85c0940f982d416c065c5ed8538da93a71d8be99ef3e6eddea31f2534",
    ("tests/host/runtime_test.c", "test_ipc_wrappers"):
        "914a8ad89cb318cf0399b6f39cc8ad95ecde942e9cff1cde4b1e7382e999ad5b",
    ("tests/host/runtime_test.c", "test_grant_wrappers"):
        "40a0ca2094ce25e15770b6f4e985b3493d6ec858dee286f6ba94190c9637e01a",
    ("kernel/user_runtime_test.c", "micros_user_runtime_test_finish"):
        "bc37ac02dc5c87a79890cdc3562c1a634018e51c00647e9e99dc0efebf0dd841",
    ("tests/qemu/user_runtime_service.c", "run_peer"):
        "a975c17445bd900117d7ca3e5f3b483b30bddcf06d43f70dbdabbf126cce2783",
    ("tests/qemu/user_runtime_service.c", "micros_service_main"):
        "336e66df4063ede54c2560e1977438395ac5672e25d421e54c2c39c0b0c22938",
    ("tests/host/runtime_test.c", "main"):
        "a4a30a9f400758efba3faaa9eb562c0b900319406aea48899077c976e6bd1057",
    ("tests/host/runtime_test.c", "test_bootstrap_launcher_console_begin"):
        "048481f666e7b5dd0469582be30155e349a053e898f4d91d48164d2c8162819d",
    ("tests/host/runtime_test.c", "test_memory_support"):
        "7e3b5e2d73aead747f5f7a8018cf59ea04cc45b726f452b45755f397a43bebd2",
    ("kernel/trap.c", "micros_trap_nested_panic"):
        "577ce9d2080d1f3f201049b46242c380e1236c61311e6c15aa909c9428791136",
    ("kernel/syscall.c", "decode_vm_handoff_request"):
        "0befddabdb7ba35c17947bea9b1fb3d42e7820d75665973a1cafcc694f79a889",
}
STATE_EFFECT_HASHES = dict(zip(FLOW_HASHES, (
    "451022fb7f571b8c42db9b47cac810eb7d0e62c8dadb7288e2cc5cb8dba0e9f4",
    "9c03e35dbc5920986255da52447f0317065b9a6f9fae0ba2c6b431ca11b2e3ef",
    "2e8120ce3815308518be643b132cf77397d6d50dff9a45873d80475db9f5bc2c",
    "36d5e62b02d902f28f4ca4c81d78b2f108e61c576e52ee5f42aeb85d59097763",
    "8e138f1cf07693cb21a3e92eb31aed3e8ef327f9afde380800c26e7f8d0218a2",
    "dc8fd2ea5ad499acc41b0980c348e1da56d6dcc1f3e3be85ff97170ad16b2301",
    "30887308b72f81995ea442e278b63ed97537aa2b1a627a3c84c6be818c3b1aed",
    "d591ea118b4c25973c1dafe17fc96412aa570b41f0c7470aab0704f1829292ab",
    "4087fb8609b501f7ac6e2d3d81981c8a1c814c4114178bbf64f6487dcf254a23",
    "e9b30cf5b4f668cff66fa206b45abd8959ac7b422f96088d73f79f2ddd8206a7",
    "228371b313eb5a4e8feb0dea16a85b2112a5969f629c8d47bd5b1ced686a4ab6",
    "4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945",
    "4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945",
    "f98e43752640c35a25dc09683774b59846eee92ee6ff4c80602a0954a0fea6cb",
    "abc827e71ad0ae12cdc420ef4dfd4852484f509d3964a86601cdd39538349109",
    "4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945",
)))

PERMITTED_OWNER_DEPENDENCIES = {
    "kernel/trap.c": set(SOURCE_SETS["MICROS_TRAP_SOURCES"]),
    "kernel/trap_context.c": set(),
    "kernel/trap_user.c": {
        "kernel/trap_context.c", "kernel/trap_interrupt.c", "kernel/trap_syscall.c",
        "kernel/trap_fault.c", "kernel/trap_test_hooks.c",
    },
    "kernel/trap_interrupt.c": {
        "kernel/trap_context.c", "kernel/trap_fault.c", "kernel/trap_test_hooks.c",
    },
    "kernel/trap_syscall.c": {
        "kernel/trap_context.c", "kernel/trap_fault.c", "kernel/trap_test_hooks.c",
    },
    "kernel/trap_fault.c": {"kernel/trap_context.c"},
    "kernel/trap_test.c": {"kernel/trap_context.c"},
    "kernel/trap_test_hooks.c": {"kernel/trap_context.c", "kernel/trap_fault.c"},
    "kernel/syscall.c": {"kernel/syscall_decode.c", "kernel/syscall_context.c"},
    "kernel/syscall_decode.c": set(),
    "kernel/syscall_context.c": set(),
    "kernel/user_runtime_test.c": {
        "kernel/user_runtime_test_state.c", "kernel/user_runtime_test_image.c",
        "kernel/user_runtime_test_setup.c", "kernel/user_runtime_test_report.c",
    },
    "kernel/user_runtime_test_state.c": set(),
    "kernel/user_runtime_test_image.c": {"kernel/user_runtime_test_state.c"},
    "kernel/user_runtime_test_setup.c": {
        "kernel/user_runtime_test_state.c", "kernel/user_runtime_test_image.c",
    },
    "kernel/user_runtime_test_trap.c": {
        "kernel/user_runtime_test_state.c", "kernel/user_runtime_test_image.c",
    },
    "kernel/user_runtime_test_cleanup.c": {
        "kernel/user_runtime_test_state.c", "kernel/user_runtime_test_image.c",
        "kernel/user_runtime_test_report.c",
    },
    "kernel/user_runtime_test_report.c": {
        "kernel/user_runtime_test_state.c", "kernel/user_runtime_test_image.c",
    },
    "tests/qemu/user_runtime_service.c": {
        "tests/qemu/user_runtime_service_common.c", "tests/qemu/user_runtime_client.c",
        "tests/qemu/user_runtime_responders.c",
    },
    "tests/qemu/user_runtime_service_common.c": set(),
    "tests/qemu/user_runtime_client.c": {"tests/qemu/user_runtime_service_common.c"},
    "tests/qemu/user_runtime_responders.c": {"tests/qemu/user_runtime_service_common.c"},
    "tests/host/runtime_test.c": {"tests/host/runtime_wrapper_test.c"},
    "tests/host/runtime_test_fixture.c": set(),
    "tests/host/runtime_wrapper_test.c": {"tests/host/runtime_test_fixture.c"},
}


def logical_owner(path: str, name: str) -> str:
    for old, final, local, qualified in owner_specs():
        if family_for(old) == family_for(path) and name in {local, qualified}:
            return final
    return path


def check_dependency_text(
    texts: dict[str, str], path: str, name: str, executable: str,
) -> None:
    owner = logical_owner(path, name)
    allowed = PERMITTED_OWNER_DEPENDENCIES[owner] | {owner}
    family = family_for(path)
    references = c_lexical_text(canonical_names(executable, family))
    definitions = source_definitions(texts)
    for source in family_paths(path):
        for called in definitions.get(source, {}):
            canonical = canonical_names(called, family)
            if has_function_reference(references, canonical):
                dependency = logical_owner(source, called)
                if dependency not in allowed:
                    raise AssertionError(f"upward/sibling helper dependency: {owner}:{name} -> {dependency}:{called}")


def check_storage(texts: dict[str, str]) -> None:
    inventory = {path: list(file_scope_objects(text)) for path, text in texts.items()}

    def consume(path: str, declaration: str) -> None:
        matches = [item for item in inventory.get(path, []) if compact(item) == compact(declaration)]
        if len(matches) != 1:
            raise AssertionError(f"expected one exact storage declaration: {path}:{declaration}")
        inventory[path].remove(matches[0])

    def context_or_originals(
        old: str, final: str, fields: tuple[str, ...], anchor: tuple[str, str],
        initializer: str = "",
    ) -> None:
        original = [
            "static " + field
            if not field.endswith(" returned_actor;")
            else "static size_t returned_actor = USER_RUNTIME_PROCESS_COUNT;"
            for field in fields
        ]
        old_declarations = {compact(item) for item in inventory.get(old, [])}
        if any(compact(item) in old_declarations for item in original):
            if anchor not in active_anchors():
                raise AssertionError(f"old storage outlived its exact anchor: {old}")
            for declaration in original:
                consume(old, declaration)
            return
        candidates = [
            declaration for declaration in inventory.get(final, [])
            if re.fullmatch(
                r"static\s+struct\s+[A-Za-z_][A-Za-z0-9_]*\s+"
                r"[A-Za-z_][A-Za-z0-9_]*\s*" + initializer + r"\s*;",
                declaration, re.DOTALL,
            )
        ]
        if len(candidates) != 1:
            raise AssertionError(f"expected one scalar file-local context: {final}")
        declaration = candidates[0]
        type_name = re.search(r"struct\s+([A-Za-z_][A-Za-z0-9_]*)", declaration).group(1)
        family_text = "\n".join(
            texts.get(path, "") for path in family_paths(final)
        ) + "\n" + read_source(FAMILY_HEADERS[family_for(final)])
        if Counter(record_fields(family_text, type_name)) != Counter(map(compact, fields)):
            raise AssertionError(f"changed context field types/count/bounds: {final}")
        consume(final, declaration)

    context_or_originals(
        "kernel/trap.c", "kernel/trap_test.c", TRAP_FIELDS,
        ("kernel/trap.c", "micros_trap_run_self_test"),
    )
    context_or_originals(
        "kernel/user_runtime_test.c", "kernel/user_runtime_test_state.c", KERNEL_FIELDS,
        ("kernel/user_runtime_test.c", "micros_user_runtime_runtime_run_self_test"),
        r"=\s*\{\s*\.returned_actor\s*=\s*USER_RUNTIME_PROCESS_COUNT\s*,?\s*\}",
    )
    for declaration in SAVED_REGISTER_OBJECTS:
        consume("kernel/user_runtime_test.c", declaration)
    consume(
        "kernel/user_runtime_test.c",
        "static const uint64_t USER_RUNTIME_TIMER_INTERVAL = UINT64_C(0x000000003b9aca00);",
    )
    image_old = "tests/qemu/user_runtime_service.c"
    image_final = "tests/qemu/user_runtime_service_common.c"
    image_owners = [
        path for path in (image_old, image_final)
        if any("micros_user_runtime_test_config" in declaration for declaration in inventory.get(path, []))
    ]
    if len(image_owners) != 1:
        raise AssertionError(f"expected one image-object owner: {image_owners}")
    if image_owners == [image_old] and (image_old, "run_client") not in active_anchors():
        raise AssertionError("old image-object storage outlived common extraction")
    for declaration in IMAGE_OBJECTS:
        consume(image_owners[0], declaration)
    capture_old = "tests/host/runtime_test.c"
    capture_final = "tests/host/runtime_test_fixture.c"
    capture_owner = capture_old if (
        capture_old, capture_final, "micros_runtime_raw_syscall"
    ) in SYMBOL_OWNER_MIGRATION else capture_final
    consume(capture_owner, "static struct raw_capture capture;")
    remaining = {path: declarations for path, declarations in inventory.items() if declarations}
    if remaining:
        raise AssertionError(f"additional/duplicated file-scope state: {remaining}")


def restored_cmake(text: str) -> str:
    restored = endpoint_cmake_projection(text)
    for name, sources in SOURCE_SETS.items():
        blocks = cmake_commands(restored, "set", name)
        if len(blocks) != 1:
            raise AssertionError(f"expected one exact source set: {name}")
        tokens = re.findall(r"(?:kernel|tests/host|tests/qemu)/[A-Za-z0-9_]+\.c", blocks[0])
        if tuple(tokens) != sources:
            raise AssertionError(f"source-set order/membership changed: {name}")
        if compact(blocks[0]) != compact(f"set({name} {' '.join(sources)})"):
            raise AssertionError(f"extra source-set content: {name}")
        restored = restored.replace(blocks[0], "", 1)
        variable = "${" + name + "}"
        if restored.count(variable) != 1:
            raise AssertionError(f"source set needs its one existing consumer: {name}")
        restored = restored.replace(variable, sources[0], 1)
    return restored


def planner_ast_without_new_paths(text: str) -> ast.Module:
    tree = ast.parse(text)
    found = set()
    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if not isinstance(target, ast.Name):
            continue
        if target.id == "PATH_CLASSIFICATION_EQUIVALENTS":
            from tests.host.test_ramfs_source_layout import alias_rows as ramfs_alias_rows

            value = ast.literal_eval(node.value)
            expected = {
                paths[0]: (*paths[1:], FAMILY_HEADERS[family])
                for family, paths in SOURCE_SETS.items()
            }
            expected.update(ramfs_alias_rows())
            expected.update(endpoint_alias_rows())
            selected = {old: aliases for old, aliases in value if old in expected}
            if selected != expected or sum(old in expected for old, _ in value) != 15:
                raise AssertionError("missing/duplicate/wrong same-owner classification aliases")
            node.value.elts = [
                item for item in node.value.elts if ast.literal_eval(item)[0] not in expected
            ]
            found.add(target.id)
        elif target.id == "HOST_UNIT_ONLY_INPUTS":
            value = ast.literal_eval(node.value)
            additions = (
                STRUCTURAL_TEST_PATH, "tests/host/test_ramfs_source_layout.py",
                "tests/host/test_endpoint_source_layout.py",
            )
            if any(value.count(path) != 1 for path in additions):
                raise AssertionError("the structural gate must be explicitly host-unit-only once")
            node.value.elts = [
                item for item in node.value.elts if ast.literal_eval(item) not in additions
            ]
            found.add(target.id)
        elif target.id == "PLANNER_INPUTS":
            additions = {
                "tools/validation_build_impact.py",
                "tests/host/test_validation_build_impact.py",
                "tools/validation_evidence.py",
                "tests/host/test_validation_evidence.py",
                "tools/validation_context.py",
                "tests/host/test_validation_context.py",
                "tools/validation_execution.py",
                "tests/host/test_validation_execution.py",
                "tools/validation_inputs.py",
                "tools/validation_build_manifest.py",
                "tests/host/test_validation_context_layout.py",
                "docs/adr/0064-fail-closed-validation-evidence-reuse.md",
            }
            value = ast.literal_eval(node.value)
            if any(value.count(path) != 1 for path in additions):
                raise AssertionError("reviewed validation extension inputs must occur once")
            node.value.elts = [
                item for item in node.value.elts if ast.literal_eval(item) not in additions
            ]
    if found != {"PATH_CLASSIFICATION_EQUIVALENTS", "HOST_UNIT_ONLY_INPUTS"}:
        raise AssertionError("planner classification assignments are missing")
    bootstrap = ast.parse(
        'if __package__ in (None, ""):\n'
        '    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))\n'
    ).body[0]
    tree.body = [
        node for node in tree.body
        if not isinstance(node, (ast.Import, ast.ImportFrom))
        and not (
            isinstance(node, ast.FunctionDef)
            and node.name in ("main", "parse_arguments", "repository_plan", "canonical_workflows")
        )
        and ast.dump(node, include_attributes=False) != ast.dump(bootstrap, include_attributes=False)
    ]
    return tree


class TrapSyscallSourceLayoutTest(unittest.TestCase):
    maxDiff = None

    @classmethod
    def setUpClass(cls) -> None:
        cls.texts = {
            path: read_source(path) for path in all_sources()
            if (REPOSITORY_ROOT / path).is_file()
        }
        cls.definitions = source_definitions(cls.texts)

    def required_text(self, path: str) -> str:
        self.assertTrue((REPOSITORY_ROOT / path).is_file(), f"missing required owner: {path}")
        return read_source(path)

    def body(self, old: str, name: str) -> str:
        return helper_body(self.texts, old, name)

    def expanded(self, old: str, name: str) -> str:
        return expanded_helper_body(self.texts, old, name)

    def test_exact_twenty_five_sources_and_five_private_headers_exist(self) -> None:
        self.assertEqual(5, len(SOURCE_SETS))
        self.assertEqual((25, 25, 5), (len(all_sources()), len(set(all_sources())), len(PRIVATE_HEADERS)))
        for path in (*all_sources(), *PRIVATE_HEADERS):
            with self.subTest(path=path):
                self.required_text(path)
        patterns = (
            "kernel/trap*.[ch]", "kernel/syscall*.[ch]", "kernel/user_runtime*.[ch]",
            "tests/host/runtime*.[ch]", "tests/qemu/user_runtime*.[ch]",
        )
        discovered = {
            str(path.relative_to(REPOSITORY_ROOT))
            for pattern in patterns for path in REPOSITORY_ROOT.glob(pattern)
        }
        neighbors = {
            "kernel/trap_route_core.c", "kernel/trap_route_core.h", "kernel/syscall.h",
            "kernel/user_runtime_test.h", "kernel/user_runtime_image.h",
            "tests/qemu/user_runtime_protocol.h",
        }
        self.assertEqual({*all_sources(), *PRIVATE_HEADERS, *neighbors}, discovered)

    def test_reviewed_eleven_headers_and_twelve_cohesive_artifacts_are_byte_exact(self) -> None:
        self.assertEqual((23, 11), (len(STABLE_HASHES), sum(path.endswith(".h") for path in STABLE_HASHES)))
        for path, expected in STABLE_HASHES.items():
            with self.subTest(path=path):
                self.assertEqual(expected, hashlib.sha256(self.required_text(path).encode()).hexdigest(), REVIEWED_BASE)

    def test_each_source_set_has_one_exact_existing_consumer(self) -> None:
        cmake = read_source("CMakeLists.txt")
        restored = restored_cmake(cmake)
        self.assertEqual(
            "eb9c397f6fef1ab1967c398002dbe570c262aacd17400a5b5d2dec87e2b6e727",
            compact_hash(restored),
            "trap wiring stays exact alongside independently reconstructed ADR-0066 RAMFS source operands",
        )
        for source in all_sources():
            self.assertEqual(1, cmake.count(source), source)

    def test_all_current_consumers_expand_the_complete_sets_once(self) -> None:
        cmake = read_source("CMakeLists.txt")
        for target, names in (
            ("micros", ("MICROS_TRAP_SOURCES", "MICROS_SYSCALL_SOURCES")),
            ("micros_user_runtime_service", ("MICROS_USER_RUNTIME_SERVICE_TEST_SOURCES",)),
            ("runtime_test", ("MICROS_RUNTIME_HOST_TEST_SOURCES",)),
        ):
            with self.subTest(target=target):
                blocks = cmake_commands(cmake, "add_executable", target)
                self.assertEqual(1, len(blocks))
                block = blocks[0]
                self.assertEqual(
                    {name: int(name in names) for name in SOURCE_SETS},
                    {name: block.count("${" + name + "}") for name in SOURCE_SETS},
                )
                self.assertFalse(set(all_sources()) & set(re.findall(r"[\w/]+\.c", block)))
        branches = cmake_if_blocks(cmake, "MICROS_BUILD_USER_RUNTIME_TEST")
        evidence = [branch for branch in branches if re.search(r"\btarget_sources\s*\(\s*micros\b", branch)]
        self.assertEqual(1, len(evidence))
        block = cmake_command(evidence[0], "target_sources", "micros")
        self.assertEqual(
            compact('target_sources(micros PRIVATE "${MICROS_USER_RUNTIME_FIXTURE_SOURCE}" '
                    'arch/riscv64/user_runtime_test.S ${MICROS_USER_RUNTIME_KERNEL_TEST_SOURCES})'),
            compact(block),
        )

    def test_exact_fourteen_migrations_and_all_seventy_nine_helper_owners(self) -> None:
        self.assertEqual((14, 14), (len(INITIAL_SYMBOL_OWNER_MIGRATION), len(set(INITIAL_SYMBOL_OWNER_MIGRATION))))
        self.assertEqual(len(SYMBOL_OWNER_MIGRATION), len(set(SYMBOL_OWNER_MIGRATION)))
        self.assertLessEqual(set(SYMBOL_OWNER_MIGRATION), set(INITIAL_SYMBOL_OWNER_MIGRATION))
        specs = owner_specs()
        self.assertEqual((79, 79), (len(specs), len({(old, name) for old, _, name, _ in specs})))
        self.assertEqual(
            {(path, name) for path, names in ORIGINAL_BODY_HASHES.items() for name in names},
            {(old, name) for old, _, name, _ in specs},
        )
        self.assertEqual(
            {(path, name) for path, names in ORIGINAL_PARAMETERS.items() for name in names},
            {(old, name) for old, _, name, _ in specs},
        )
        for old, final, name, _ in specs:
            with self.subTest(original=old, helper=name, final=final):
                find_owner(self.texts, old, name)

    def test_pending_symbols_and_helpers_allow_only_the_exact_unchanged_old_body(self) -> None:
        for old, final, name, _ in owner_specs():
            with self.subTest(original=old, helper=name):
                path, _ = find_owner(self.texts, old, name)
                pending = (old, final, name) in SYMBOL_OWNER_MIGRATION or (
                    helper_anchor(old, final, name) in active_anchors()
                )
                if path == old and (pending or (old, name) not in LONG_FUNCTION_REQUIREMENTS):
                    if pending:
                        actual = canonical_names(c_function_body(self.texts[path], name), family_for(old))
                    else:
                        actual = self.expanded(old, name)
                    self.assertEqual(ORIGINAL_BODY_HASHES[old][name], compact_hash(actual))
                elif (old, name) not in LONG_FUNCTION_REQUIREMENTS:
                    self.assertEqual(ORIGINAL_BODY_HASHES[old][name], compact_hash(self.expanded(old, name)))

    def test_retained_entries_are_unique_and_keep_their_supported_declarations(self) -> None:
        retained = {
            "kernel/trap.c": (
                "micros_trap_install", "micros_trap_dispatch", "micros_trap_nested_panic",
            ),
            "kernel/syscall.c": ("micros_syscall_handle_user_ecall",),
            "kernel/user_runtime_test.c": ("micros_user_runtime_runtime_run_self_test",),
            "tests/qemu/user_runtime_service.c": ("micros_service_main", "startup_checks"),
            "tests/host/runtime_test.c": ("main",),
        }
        for path, names in retained.items():
            for name in names:
                with self.subTest(path=path, entry=name):
                    self.assertEqual([path], [owner for owner, definitions in self.definitions.items() if name in definitions])
        for path, name in (
            ("kernel/trap.c", "micros_trap_nested_panic"),
            ("kernel/user_runtime_test.c", "micros_user_runtime_runtime_run_self_test"),
        ):
            self.assertRegex(self.texts[path], rf"_Noreturn\s+void\s+{name}\s*\(")
        self.assertNotRegex(
            c_declaration_text(self.texts["tests/qemu/user_runtime_service.c"]),
            r"_Noreturn\s+void\s+micros_service_main",
        )

    def test_new_private_headers_are_declarations_only(self) -> None:
        for path in PRIVATE_HEADERS:
            with self.subTest(header=path):
                check_header(path, self.required_text(path), private=True)

    def test_recursive_includes_cannot_smuggle_algorithms_storage_or_source(self) -> None:
        for root in (*all_sources(), *PRIVATE_HEADERS):
            with self.subTest(root=root):
                self.required_text(root)
                for path in local_include_closure(root):
                    if path.endswith(".h"):
                        check_header(path, read_source(path), path in PRIVATE_HEADERS)

    def test_cross_file_helpers_have_one_private_declaration_and_local_helpers_stay_static(self) -> None:
        headers = {
            path: self.required_text(path) for path in PRIVATE_HEADERS
        }
        stable = "\n".join(read_source(path) for path in STABLE_HASHES if path.endswith(".h"))
        for path, definitions in self.definitions.items():
            for name in definitions:
                lexical = c_declaration_text(self.texts[path])
                if re.search(rf"(?m)^static\b[^;{{}}]*\b{re.escape(name)}\s*\(", lexical):
                    continue
                if name in {"main", "micros_service_main"} or re.search(rf"\b{re.escape(name)}\s*\(", stable):
                    continue
                expected_header = FAMILY_HEADERS[family_for(path)]
                declared = [
                    header for header, text in headers.items()
                    if re.search(rf"\b{re.escape(name)}\s*\(", c_declaration_text(text))
                ]
                self.assertEqual([expected_header], declared, (path, name))
                self.assertTrue(
                    name in {item[2] for item in INITIAL_SYMBOL_OWNER_MIGRATION}
                    or name.startswith((
                        "trap_", "syscall_", "user_runtime_test_", "user_runtime_service_", "runtime_test_",
                    )), (path, name),
                )

    def test_production_and_evidence_private_include_boundaries_are_recursive(self) -> None:
        production = (*SOURCE_SETS["MICROS_TRAP_SOURCES"], *SOURCE_SETS["MICROS_SYSCALL_SOURCES"])
        forbidden = set(PRIVATE_HEADERS[2:])
        for path in all_sources():
            with self.subTest(path=path):
                self.required_text(path)
                closure = set(local_include_closure(path))
                private = closure & set(PRIVATE_HEADERS)
                self.assertLessEqual(private, {FAMILY_HEADERS[family_for(path)]})
                if path in production:
                    self.assertFalse(closure & forbidden)

    def test_helper_dependency_direction_includes_transitive_header_code_and_macros(self) -> None:
        self.assertEqual(set(all_sources()), set(PERMITTED_OWNER_DEPENDENCIES))
        for path, definitions in self.definitions.items():
            dependency_text = header_executable_text(path)
            for name in definitions:
                with self.subTest(path=path, helper=name):
                    check_dependency_text(
                        self.texts, path, name,
                        c_function_body(self.texts[path], name) + "\n" + dependency_text,
                    )

    def test_exact_existing_context_storage_and_all_ten_abi_globals(self) -> None:
        self.assertEqual((3, 17, 8, 7, 3), (
            len(TRAP_FIELDS), len(KERNEL_FIELDS), len(RAW_FIELDS),
            len(IMAGE_OBJECTS), len(SAVED_REGISTER_OBJECTS),
        ))
        check_storage(self.texts)

    def test_existing_record_fields_union_and_native_capture_shape_are_exact(self) -> None:
        combined = "\n".join(self.texts.values()) + "\n" + "\n".join(
            self.required_text(path) for path in PRIVATE_HEADERS
        )
        for name, fields in RECORD_FIELDS.items():
            with self.subTest(record=name):
                self.assertEqual(tuple(map(compact, fields)), record_fields(combined, name))

    def test_existing_static_locals_are_only_the_four_immutable_tables(self) -> None:
        expected = Counter({
            ("kernel/user_runtime_test_cleanup.c", "670631badf834981efb58688c89ae95305a66646c04557cdb42ba0b6ee429a12"): 1,
            ("kernel/user_runtime_test.c", "34462684944e22f67d56c9641e517a19bf4aa7a507dba46457d569e330b8baa2"): 1,
            ("tests/qemu/user_runtime_client.c", "4e8b7dd0e50e0a6d182301017130bda52d9aca266dfc08296037ba5ffab7cfda"): 1,
            ("tests/qemu/user_runtime_responders.c", "4e8b7dd0e50e0a6d182301017130bda52d9aca266dfc08296037ba5ffab7cfda"): 1,
        })
        observed = Counter()
        for path, definitions in self.definitions.items():
            for name in definitions:
                for declaration in static_local_storage(c_function_body(self.texts[path], name)):
                    self.assertRegex(declaration, r"^static\s+const\b")
                    observed[logical_owner(path, name), compact_hash(declaration)] += 1
        self.assertEqual(expected, observed)

    def test_image_actor_stack_transfer_and_bss_bounds_remain_named_and_exact(self) -> None:
        kernel = c_declaration_text("\n".join(
            self.texts.get(path, "") for path in SOURCE_SETS["MICROS_USER_RUNTIME_KERNEL_TEST_SOURCES"]
        ) + self.required_text("kernel/user_runtime_test_internal.h"))
        service = c_declaration_text("\n".join(
            self.texts.get(path, "") for path in SOURCE_SETS["MICROS_USER_RUNTIME_SERVICE_TEST_SOURCES"]
        ) + self.required_text("tests/qemu/user_runtime_service_internal.h"))
        self.assertIn(
            compact("USER_RUNTIME_CLIENT = 0, USER_RUNTIME_SERVER, USER_RUNTIME_PEER, "
                    "USER_RUNTIME_PROCESS_COUNT, USER_RUNTIME_MAX_IMAGE_PAGES = 8,"),
            compact(kernel),
        )
        self.assertEqual(1, len(re.findall(r"\bUSER_RUNTIME_MAX_IMAGE_PAGES\s*=\s*8\b", kernel)))
        self.assertEqual(1, len(re.findall(r"\bTEST_TRANSFER_SIZE\s*=\s*32\b", service)))
        self.assertEqual(1, len(re.findall(r"\bTEST_BSS_PROBE_SIZE\s*=\s*64\b", service)))
        setup = self.expanded("kernel/user_runtime_test.c", "prepare_process")
        self.assertIn("MICROS_USER_VIRTUAL_END-MICROS_SV39_PAGE_SIZE", compact(setup))
        self.assertIn("context.sp=MICROS_USER_VIRTUAL_END;", compact(setup))
        self.assertIn("sizeof(micros_user_runtime_test_bss_probe)", compact(self.body(
            "tests/qemu/user_runtime_service.c", "initial_bss_is_zero",
        )))

    def test_original_nonreturning_entry_owns_the_inline_supervisor_register_capture(self) -> None:
        path = "kernel/user_runtime_test.c"
        entry = self.body(path, "micros_user_runtime_runtime_run_self_test")
        capture = (
            '__asm__ volatile("mv %0, sp\\n" "mv %1, gp\\n" "mv %2, tp" '
            ': "=r"(micros_user_runtime_test_saved_sp), '
            '"=r"(micros_user_runtime_test_saved_gp), "=r"(micros_user_runtime_test_saved_tp));'
        )
        self.assertEqual(1, compact(entry).count(compact(capture)))
        self.assertIn(compact(capture + "(void)saved_status; (void)micros_scheduler_start();"), compact(entry))
        for owner, text in self.texts.items():
            self.assertEqual(
                int(owner == path),
                text.count('"mv %0, sp\\n"'),
                "a returning setup helper cannot own the supervisor continuation",
            )
        restore = read_source("arch/riscv64/user_runtime_test.S")
        for register in ("sp", "gp", "tp"):
            self.assertIn("micros_user_runtime_test_saved_" + register, restore)
        startup = self.body("tests/qemu/user_runtime_service.c", "startup_checks")
        for register, variable in (("sp", "stack_pointer"), ("gp", "global_pointer"), ("tp", "thread_pointer")):
            self.assertIn(compact(f'__asm__ volatile("mv %0, {register}" : "=r"({variable}));'), compact(startup))

    def test_expanded_route_predicates_cpp_branches_calls_and_records_are_exact(self) -> None:
        self.assertGreaterEqual(len(FLOW_HASHES), 15)
        for (old, name), expected in FLOW_HASHES.items():
            with self.subTest(original=old, root=name):
                path, spelling = find_owner(self.texts, old, name)
                self.assertEqual(expected, flow_hash(expanded_flow(self.texts, path, spelling)))
                self.assertEqual(
                    CALL_ARGUMENT_HASHES[old, name],
                    flow_hash(call_arguments(projected_body(self.texts, path, spelling, True))),
                    "typed transport may not hide different call arguments",
                )
                self.assertEqual(
                    STATE_EFFECT_HASHES[old, name],
                    flow_hash(state_effects(projected_body(self.texts, path, spelling, True))),
                    "typed transport may not hide mutations or cached register authority",
                )
        self.assertEqual(set(FLOW_HASHES), set(CALL_ARGUMENT_HASHES))
        self.assertEqual(set(FLOW_HASHES), set(STATE_EFFECT_HASHES))

    def test_supervisor_irqs_return_the_same_frame_without_user_selection(self) -> None:
        dispatch = self.expanded("kernel/trap.c", "micros_trap_dispatch")
        supervisor = dispatch[dispatch.index("#ifdef MICROS_BUILD_USER_ADDRESS_SPACE_TEST"):]
        self.assertNotRegex(
            supervisor, r"\bmicros_(?:scheduler_select_(?:captured_)?user_return|user_execution_capture_trap)\s*\(",
        )
        self.assertIn(
            compact("(void)micros_tty_interrupt_dispatch(hart, frame); return;"),
            compact(supervisor),
        )
        calls = [value for kind, value in flow_events(dispatch) if kind == "call"]
        self.assertLess(calls.index("micros_scheduler_user_trap_enter"), calls.index("micros_user_execution_test_pre_capture"))
        self.assertLess(calls.index("micros_user_execution_test_pre_capture"), calls.index("micros_user_execution_capture_trap"))
        self.assertIn(
            compact('MICROS_TRAP_PANIC(hart->hardware_id, "unexpected-exception", frame);'),
            compact(c_function_body(self.texts["kernel/trap.c"], "micros_trap_dispatch")),
            "the source-bound fatal call genuinely remains in the retained facade",
        )

    def test_ecall_dispatch_is_once_per_branch_and_selector_failure_reads_the_returned_frame(self) -> None:
        dispatch = self.expanded("kernel/trap.c", "micros_trap_dispatch")
        ordinary = dispatch[dispatch.index("#if !defined(MICROS_BUILD_USER_EXECUTION_TEST)"):]
        ordinary = ordinary[:ordinary.index("#if defined(MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST)")]
        self.assertEqual(1, ordinary.count("micros_syscall_handle_user_ecall("))
        self.assertIn(compact(
            "syscall_return == MICROS_SYSCALL_RETURN_CAPTURED "
            "? micros_scheduler_select_captured_user_return(hart, frame) "
            ": micros_scheduler_select_user_return(hart, frame)"
        ), compact(ordinary))
        failure = ordinary[ordinary.index("if (scheduler_error != MICROS_SCHEDULER_OK)"):]
        ordered = (
            "frame->a7", "MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL",
            "MICROS_BOOTSTRAP_DIAGNOSTIC_COMPLETION", "MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY",
            "MICROS_SYSCALL_ABI_TTY_CONTROL", "record_tty_owner_failure", '"ipc-syscall-return"',
        )
        positions = [failure.index(item) for item in ordered]
        self.assertEqual(sorted(positions), positions)
        self.assertNotRegex(ordinary, r"\b(?:cached|saved)_?(?:a7|operation|selector)\b")
        handoff = dispatch[dispatch.index("#ifdef MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST"):]
        handoff = handoff[:handoff.index("#if !defined(MICROS_BUILD_USER_EXECUTION_TEST)")]
        self.assertIn("if (frame->a7 == UINT64_MAX)", handoff)
        self.assertEqual(1, handoff.count("micros_syscall_handle_user_ecall("))
        self.assertNotIn('"ipc-syscall-return"', handoff)

    def test_syscall_early_decode_and_unknown_operation_precedence_are_unchanged(self) -> None:
        entry = self.expanded("kernel/syscall.c", "micros_syscall_handle_user_ecall")
        ordered = (
            "hart == NULL || frame == NULL", "frame->sepc > UINT64_MAX - 4",
            "arguments = capture_arguments(frame)", "frame->sepc += 4",
            "micros_bootstrap_control_decode(", "decode_vm_handoff_request(",
            "micros_pm_control_decode(", "micros_tty_control_decode(",
            "micros_kernel_object_runtime_authoritative_registry(",
            "micros_hart_current_thread(", "micros_thread_resolve(", "micros_process_resolve(",
            '"syscall-current-invariant"', "switch (arguments.a7)",
        )
        positions = [entry.index(item) for item in ordered]
        self.assertEqual(sorted(positions), positions)
        self.assertEqual(1, compact(entry).count("frame->sepc+=4;"))
        early = entry[:entry.index("micros_kernel_object_runtime_authoritative_registry(")]
        self.assertEqual(
            ("MICROS_SYSCALL_RETURN_NORMAL",) * 4,
            tuple(value for kind, value in flow_events(early) if kind == "exit:return"),
        )
        default = entry[entry.index("default:"):]
        self.assertIn("MICROS_SYSCALL_ABI_ARGUMENT", default)
        self.assertIn("MICROS_SYSCALL_RETURN_NORMAL", default)
        capture = self.body("kernel/syscall.c", "capture_arguments")
        self.assertEqual(tuple(f"a{index}" for index in range(8)), tuple(re.findall(r"\.(a[0-7])\s*=", capture)))
        self.assertNotRegex(c_declaration_text(entry), r"\bstatic\b")

    def test_native_four_case_order_short_circuit_errno_and_full_negative_loop(self) -> None:
        main = self.body("tests/host/runtime_test.c", "main")
        self.assertEqual(NATIVE_CASE_ORDER, tuple(re.findall(r"\b(test_[A-Za-z0-9_]+)\s*\(", main)))
        self.assertEqual(compact(
            "{ if (!test_ipc_wrappers() || !test_grant_wrappers() "
            "|| !test_bootstrap_launcher_console_begin() || !test_memory_support()) "
            "{ return 1; } return 0; }"
        ), compact(main))
        ipc = self.expanded("tests/host/runtime_test.c", "test_ipc_wrappers")
        grant = self.expanded("tests/host/runtime_test.c", "test_grant_wrappers")
        self.assertEqual(("EDOM", "ERANGE", "E2BIG", "ENOSPC", "EBUSY", "ENOENT"), tuple(re.findall(r"errno\s*=\s*(E[A-Z0-9]+)", ipc)))
        loop = compact("for (error = MICROS_SYSCALL_ABI_ARGUMENT; error >= MICROS_SYSCALL_ABI_RANGE; --error)")
        self.assertEqual(2, compact(grant).count(loop))
        self.assertIn(compact("EXPECT_TRUE(result == error && errno == EILSEQ);"), compact(grant))
        for fragment in ("0x0000000100000021", "0x0000000200000032", "0x80000003", "capture.calls == 0"):
            self.assertIn(compact(fragment), compact(grant))
        raw = self.expanded("tests/host/runtime_test.c", "micros_runtime_raw_syscall")
        self.assertEqual(tuple(str(index) for index in range(8)), tuple(re.findall(r"capture\.arguments\[(\d)\]\s*=", raw)))
        self.assertIn("capture.input_snapshot = *capture.expected_input;", raw)

    def test_role_scripts_keep_automatic_state_terminal_receives_canaries_and_status_codes(self) -> None:
        client = self.expanded("tests/qemu/user_runtime_service.c", "run_client")
        server = self.expanded("tests/qemu/user_runtime_service.c", "run_server")
        peer = self.expanded("tests/qemu/user_runtime_service.c", "run_peer")
        for role in (client, server, peer):
            self.assertIn("struct micros_ipc_message message;", role)
            self.assertNotRegex(c_declaration_text(role), r"\bstatic\s+(?!const\b)")
        for fragment in (
            "volatile uint64_t canary_before = MICROS_USER_RUNTIME_TEST_CANARY_BEFORE;",
            "volatile uint64_t canary_after = MICROS_USER_RUNTIME_TEST_CANARY_AFTER;",
            "micros_grant_t read_grant = MICROS_GRANT_NONE;",
            "micros_grant_t write_grant = MICROS_GRANT_NONE;",
        ):
            self.assertIn(compact(fragment), compact(client))
        terminal = "micros_runtime_receive(MICROS_ENDPOINT_ANY, &message) == MICROS_SYSCALL_ABI_OK"
        self.assertIn(compact(terminal), compact(client))
        self.assertIn(compact(terminal), compact(peer))
        self.assertNotIn(compact(terminal), compact(server))
        self.assertEqual(
            ("0x2100", *(f"0x20{value:x}" for value in range(2, 12))),
            tuple(re.findall(r"return\s+UINT64_C\((0x[0-9a-f]+)\)", client)),
        )
        self.assertEqual(
            ("0x401", "0x402", "0x403", "0x404", "0x40a", "0x405", "0x406", "0x407", "0x408", "0x409"),
            tuple(re.findall(r"return\s+UINT64_C\((0x[0-9a-f]+)\)", server)),
        )
        self.assertEqual(("0x301", "0x302", "0x303"), tuple(re.findall(r"return\s+UINT64_C\((0x[0-9a-f]+)\)", peer)))
        self.assertIn(compact(
            "MICROS_USER_RUNTIME_TEST_STATUS_GRANTS | MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE"
        ), compact(client))
        entry = self.expanded("tests/qemu/user_runtime_service.c", "micros_service_main")
        self.assertLess(entry.index("startup_checks()"), entry.index("micros_user_runtime_test_probe_rodata("))
        self.assertEqual(("run_client", "run_server", "run_peer"), tuple(re.findall(r"\b(run_[a-z]+)\s*\(", entry)))

    def test_runtime_finish_keeps_marker_cleanup_then_success_reset_and_failure_fallthrough(self) -> None:
        finish = self.expanded("kernel/user_runtime_test.c", "micros_user_runtime_test_finish")
        self.assertIn(RUNTIME_MARKER, string_records(finish))
        ordered = (
            "!configurations_complete()", "!close_endpoint(objects, actor)",
            "!release_process(objects, actor)", "grant_registry->active_count != 0",
            "MICROS_USER_RUNTIME_TEST_PASS", "SBI_RESET_REASON_NONE", "failure:",
            "MICROS_TEST_FAILURE user-runtime stage=", "MICROS_USER_RUNTIME_STATE actor=",
            "SBI_RESET_REASON_SYSTEM_FAILURE",
        )
        positions = [finish.index(item) for item in ordered]
        self.assertEqual(sorted(positions), positions)
        setup = self.expanded("kernel/user_runtime_test.c", "micros_user_runtime_runtime_run_self_test")
        admits = re.findall(r"micros_scheduler_admit\s*\(\s*threads\[(USER_RUNTIME_[A-Z]+)\]", setup)
        self.assertEqual(["USER_RUNTIME_SERVER", "USER_RUNTIME_PEER", "USER_RUNTIME_CLIENT"], admits)
        self.assertLess(setup.index("micros_endpoint_activate("), setup.index("prepare_process(objects, actor)"))
        self.assertLess(setup.index("prepare_process(objects, actor)"), setup.index('"fence.i"'))

    def test_generator_cli_symbols_fields_and_existing_helper_bodies_are_preserved(self) -> None:
        text = read_source(GENERATOR_PATH)
        tree = ast.parse(text)
        functions = {node.name: node for node in ast.walk(tree) if isinstance(node, ast.FunctionDef)}
        public_functions = {"_format_bytes", "render_fixture", "parse_arguments", "main"}
        self.assertLessEqual(public_functions, set(functions))
        for name in public_functions:
            self.assertEqual(1, sum(isinstance(node, ast.FunctionDef) and node.name == name for node in tree.body))
            self.assertEqual(1, sum(isinstance(node, ast.FunctionDef) and node.name == name for node in ast.walk(tree)))
        for name, expected in GENERATOR_BODY_HASHES.items():
            with self.subTest(function=name):
                if name in {"render_fixture", "containing_load"} and (
                    GENERATOR_PATH, "render_fixture"
                ) not in LONG_FUNCTION_MIGRATION:
                    continue
                self.assertEqual(expected, python_body_hash(functions[name]))
        for name, arguments, defaults in (
            ("_format_bytes", ("data",), ()), ("render_fixture", ("image",), ()),
            ("parse_arguments", ("argv",), (None,)), ("main", ("argv",), (None,)),
        ):
            node = functions[name]
            self.assertEqual(arguments, tuple(argument.arg for argument in node.args.args))
            self.assertEqual(defaults, tuple(ast.literal_eval(value) for value in node.args.defaults))
            self.assertFalse(node.args.posonlyargs or node.args.kwonlyargs or node.args.vararg or node.args.kwarg)
        symbols = [
            ast.literal_eval(node.value) for node in tree.body if isinstance(node, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == "FIXTURE_SYMBOLS" for target in node.targets)
        ]
        self.assertEqual([FIXTURE_SYMBOLS], symbols)
        field_tables = [
            ast.literal_eval(node.value) for node in ast.walk(tree) if isinstance(node, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == "symbol_fields" for target in node.targets)
        ]
        self.assertEqual([tuple(zip(FIXTURE_FIELDS, FIXTURE_SYMBOLS))], field_tables)
        self.assertIn('#include "kernel/user_runtime_image.h"', text)
        self.assertIn("micros_user_runtime_test_image = {", text)
        self.assertIn('raise SystemExit(f"user runtime fixture generation failed: {error}")', text)
        self.assertIn('return "\\n".join(output)', text)

    def test_existing_native_elf_ctest_qemu_timeout_and_source_bound_panic_identities(self) -> None:
        cmake = read_source("CMakeLists.txt")
        self.assertEqual(1, len(re.findall(r"\badd_test\s*\(\s*NAME\s+user-runtime\s+COMMAND\s+runtime_test\s*\)", cmake)))
        for target in ("test-unit-fast", "test-unit"):
            block = cmake_command(cmake, "add_custom_target", target)
            self.assertEqual(1, len(re.findall(r"\bruntime_test\b", block)))
        self.assertNotRegex(cmake, r"\bset_tests_properties\s*\([^)]*\buser-runtime\b[^)]*\bLABELS\s+slow\b")
        for target in ("test-qemu-user-runtime", "test-qemu-trap-panic"):
            block = cmake_command(cmake, "add_custom_target", target)
            expected_timeout = "12" if target == "test-qemu-user-runtime" else "8"
            self.assertRegex(block, rf"--timeout\s+{expected_timeout}\b")
            if target.endswith("trap-panic"):
                self.assertIn("MICROS_PANIC_SOURCE file=kernel/trap[.]c line=0x[0-9a-f]{16}", block)
                self.assertIn(
                    "MICROS_TRAP_CONTEXT origin=S sstatus=0x[0-9a-f]{16} "
                    "scause=0x0000000000000002 stval=0x[0-9a-f]{16} sepc=0x[0-9a-f]{16} "
                    "ra=0x[0-9a-f]{16} sp=0x[0-9a-f]{16}", block,
                )
                self.assertIn("--require-trap-context", block)
                self.assertIn("micros_trap_panic_test_fault", block)
                self.assertIn("unexpected-exception", block)
            else:
                self.assertIn(RUNTIME_MARKER.rstrip(), block)
        self.assertEqual(
            "eae6c86ce5ac8f0e1b39605ebbd6816135558f0b4f8ea293b3680d7ecb79df86",
            hashlib.sha256(read_source("CMakePresets.json").encode()).hexdigest(),
        )

    def test_original_six_path_metadata_remains_exact(self) -> None:
        self.assertEqual((38, 21), (len(QEMU_WORKFLOWS), len(SYSCALL_WORKFLOWS)))
        self.assertEqual(QEMU_WORKFLOWS, tuple(validation_plan.QEMU_WORKFLOWS))
        baselines = {
            "kernel/trap.c": (True, QEMU_WORKFLOWS, ()),
            "kernel/syscall.c": (False, SYSCALL_WORKFLOWS, ("test-ipc-model",)),
            "kernel/user_runtime_test.c": (False, ("test-qemu-user-runtime",), ()),
            "tests/qemu/user_runtime_service.c": (False, ("test-qemu-user-runtime",), ()),
            "tests/host/runtime_test.c": (False, ("test-qemu-user-runtime",), ()),
            GENERATOR_PATH: (
                False, ("test-qemu-user-runtime", "test-qemu-pm-service"), (),
            ),
        }
        for path, (full, workflows, models) in baselines.items():
            with self.subTest(path=path):
                self.assertEqual(full, validation_plan.requires_full_tier([path]))
                self.assertEqual(workflows, tuple(validation_plan.affected_workflows([path])))
                self.assertEqual(models, tuple(validation_plan.affected_slow_models([path])))
                self.assertEqual([], validation_plan.affected_image_workflows([path]))

    def test_new_sources_and_headers_inherit_all_four_metadata_fields_and_plans(self) -> None:
        equivalents = dict(validation_plan.PATH_CLASSIFICATION_EQUIVALENTS)
        for family, paths in SOURCE_SETS.items():
            expected = (*paths[1:], FAMILY_HEADERS[family])
            old = paths[0]
            with self.subTest(family=family):
                self.assertEqual(expected, equivalents.get(old))
            for path in expected:
                with self.subTest(path=path):
                    self.assertEqual(validation_plan.requires_full_tier([old]), validation_plan.requires_full_tier([path]))
                    for classifier in (
                        validation_plan.affected_workflows, validation_plan.affected_slow_models,
                        validation_plan.affected_image_workflows,
                    ):
                        self.assertEqual(classifier([old]), classifier([path]), classifier.__name__)
                    for tier in ("fast", "pr", "full"):
                        self.assertEqual(validation_plan.plan([old], tier, "main"), validation_plan.plan([path], tier, "main"))

    def test_structural_gate_is_explicitly_host_unit_only(self) -> None:
        self.assertEqual(1, validation_plan.HOST_UNIT_ONLY_INPUTS.count(STRUCTURAL_TEST_PATH))
        self.assertEqual(
            [validation_plan.UNIT_FAST, *validation_plan.diff_commands("main")],
            validation_plan.plan([STRUCTURAL_TEST_PATH], "fast", "main"),
        )
        self.assertFalse(validation_plan.requires_full_tier([STRUCTURAL_TEST_PATH]))
        for classifier in (
            validation_plan.affected_workflows, validation_plan.affected_slow_models,
            validation_plan.affected_image_workflows,
        ):
            self.assertEqual([], classifier([STRUCTURAL_TEST_PATH]))

    def test_original_planner_ownership_remains_exact_under_adr0064(self) -> None:
        tree = planner_ast_without_new_paths(read_source("tools/validation_plan.py"))
        self.assertEqual(
            BASE_PLANNER_AST_HASH,
            hashlib.sha256(ast.dump(tree, include_attributes=False).encode()).hexdigest(),
            "original ownership/pure planning stays exact; ADR-0064 owns the reviewed CLI extension",
        )

    def test_exact_nine_active_long_migrations_match_actual_signals_and_bodies(self) -> None:
        self.assertEqual((9, 9), (len(INITIAL_LONG_FUNCTION_MIGRATION), len(set(INITIAL_LONG_FUNCTION_MIGRATION))))
        self.assertEqual(len(LONG_FUNCTION_MIGRATION), len(set(LONG_FUNCTION_MIGRATION)))
        self.assertLessEqual(set(LONG_FUNCTION_MIGRATION), set(INITIAL_LONG_FUNCTION_MIGRATION))
        self.assertTrue(all(size > 120 and len(phases.split(";")) >= 3
                            for size, _, phases in LONG_FUNCTION_REQUIREMENTS.values()))
        oversized = {
            (path, name) for path, names in self.definitions.items()
            for name, size in names.items() if size > FUNCTION_REVIEW_LINE_LIMIT
        } | {
            (GENERATOR_PATH, name) for name, size in python_function_definitions(read_source(GENERATOR_PATH)).items()
            if size > FUNCTION_REVIEW_LINE_LIMIT
        }
        self.assertEqual(set(LONG_FUNCTION_MIGRATION), oversized)
        for path, name in LONG_FUNCTION_MIGRATION:
            with self.subTest(path=path, function=name):
                size = (
                    python_function_definitions(read_source(path))[name]
                    if path == GENERATOR_PATH else self.definitions[path][name]
                )
                self.assertEqual(LONG_FUNCTION_REQUIREMENTS[path, name][0], size)
                if path != GENERATOR_PATH:
                    self.assertEqual(ORIGINAL_BODY_HASHES[path][name], compact_hash(c_function_body(self.texts[path], name)))

    def test_rename_move_or_format_compression_never_discharges_a_long_function(self) -> None:
        fingerprints = {
            fingerprint: (old, name) for (old, name), (_, fingerprint, _) in LONG_FUNCTION_REQUIREMENTS.items()
            if old != GENERATOR_PATH
        }
        for path, names in self.definitions.items():
            for name, size in names.items():
                fingerprint = body_fingerprint(c_function_body(self.texts[path], name))
                if fingerprint in fingerprints:
                    original = fingerprints[fingerprint]
                    self.assertEqual(original, (path, name), "a move/rename is not a semantic split")
                    self.assertIn(original, LONG_FUNCTION_MIGRATION)
                    self.assertGreater(size, FUNCTION_REVIEW_LINE_LIMIT, "formatting is not a semantic split")
        generator_fingerprint = LONG_FUNCTION_REQUIREMENTS[GENERATOR_PATH, "render_fixture"][1]
        for node in ast.walk(ast.parse(read_source(GENERATOR_PATH))):
            if isinstance(node, ast.FunctionDef) and python_body_fingerprint(node) == generator_fingerprint:
                self.assertEqual("render_fixture", node.name)
                self.assertIn((GENERATOR_PATH, node.name), LONG_FUNCTION_MIGRATION)
                self.assertGreater(node.end_lineno - node.lineno + 1, FUNCTION_REVIEW_LINE_LIMIT)

    def test_completed_long_migrations_require_multiple_real_named_phases(self) -> None:
        for old, name in INITIAL_LONG_FUNCTION_MIGRATION:
            if (old, name) in LONG_FUNCTION_MIGRATION:
                continue
            with self.subTest(original=old, root=name):
                if old == GENERATOR_PATH:
                    tree = ast.parse(read_source(old))
                    functions = {node.name: node for node in ast.walk(tree) if isinstance(node, ast.FunctionDef)}
                    self.assertIn(name, functions)
                    phases = set()
                    pending = [name]
                    public = {"_format_bytes", "render_fixture", "parse_arguments", "main"}
                    while pending:
                        current = pending.pop()
                        for node in ast.walk(functions[current]):
                            if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Name):
                                continue
                            called = node.func.id
                            if called in functions and called not in public and called not in phases:
                                phases.add(called)
                                pending.append(called)
                else:
                    path, spelling = find_owner(self.texts, old, name)
                    projected_body(self.texts, path, spelling, True)
                    phases = named_phases(self.texts, path, spelling)
                self.assertGreaterEqual(len(phases), 2, "a short facade alone does not prove semantic outlining")

    def test_initial_pending_slice_keeps_all_five_original_sources_byte_exact(self) -> None:
        if (
            set(SYMBOL_OWNER_MIGRATION) != set(INITIAL_SYMBOL_OWNER_MIGRATION)
            or set(LONG_FUNCTION_MIGRATION) != set(INITIAL_LONG_FUNCTION_MIGRATION)
        ):
            return
        for path, expected in ORIGINAL_SOURCE_HASHES.items():
            with self.subTest(path=path):
                self.assertEqual(expected, hashlib.sha256(self.texts[path].encode()).hexdigest())
        for paths in SOURCE_SETS.values():
            for path in paths[1:]:
                with self.subTest(skeleton=path):
                    text = self.required_text(path)
                    self.assertEqual(
                        f'#include "{FAMILY_HEADERS[family_for(path)]}"',
                        text.strip(),
                        "the initial future owner is an include-only skeleton, not an extraction",
                    )


BASE_PLANNER_AST_HASH = "6db092d80f6a5ee9a84c3801440a9bab6b1a59e39fe99b251b4d92f6ccad730a"
EXPECT_TRUE_HASH = "3ef0a1204f1dfeb937f4dc99626fcbda4e9a8e1c387b703679c34a798e91c31c"


# Small projection inputs, not snapshots of the audited implementations.
META_BODIES = {
    "find_page": """{
    size_t index;

    for (index = 0; index < images[actor].page_count; ++index) {
        if (
            images[actor].pages[index].virtual_address
                == virtual_address
        ) {
            return &images[actor].pages[index];
        }
    }
    return NULL;
}""",
    "micros_syscall_handle_user_ecall": """{
    struct micros_syscall_context context;
    struct micros_syscall_arguments arguments;
    struct micros_bootstrap_control_request bootstrap_request;
    union micros_vm_handoff_decoded_request vm_handoff_request;
    struct micros_pm_control_request pm_control_request;
    struct micros_tty_control_request tty_control_request;
    const struct micros_thread *thread;
    const struct micros_process *process;
    if (hart == NULL || frame == NULL) {
        panic_syscall(hart, frame, "syscall-argument");
    }
    if (frame->sepc > UINT64_MAX - 4) {
        panic_syscall(hart, frame, "syscall-sepc-overflow");
    }
    arguments = capture_arguments(frame);
    frame->sepc += 4;
    if (
        arguments.a7 == MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL
        && micros_bootstrap_control_decode(&arguments, &bootstrap_request) != MICROS_SYSCALL_ABI_OK
    ) {
        frame->a0 = (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
    if (arguments.a7 == MICROS_SYSCALL_ABI_VM_HANDOFF
        && decode_vm_handoff_request(&arguments, &vm_handoff_request) != MICROS_SYSCALL_ABI_OK) {
        frame->a0 = (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
    if (arguments.a7 == MICROS_SYSCALL_ABI_PM_CONTROL
        && micros_pm_control_decode(&arguments, &pm_control_request) != MICROS_SYSCALL_ABI_OK) {
        frame->a0 = (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
    if (arguments.a7 == MICROS_SYSCALL_ABI_TTY_CONTROL
        && micros_tty_control_decode(&arguments, &tty_control_request) != MICROS_SYSCALL_ABI_OK) {
        frame->a0 = (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
    context.objects = micros_kernel_object_runtime_authoritative_registry();
    context.hart = micros_kernel_object_runtime_boot_hart_handle();
    if (micros_hart_current_thread(context.objects, context.hart, &context.current) != MICROS_KERNEL_OBJECT_OK
        || micros_thread_resolve(context.objects, context.current, &thread) != MICROS_KERNEL_OBJECT_OK
        || micros_process_resolve(context.objects, thread->owner, &process) != MICROS_KERNEL_OBJECT_OK) {
        panic_syscall(hart, frame, "syscall-current-invariant");
    }
    context.process = thread->owner;
    switch (arguments.a7) {
    default:
        frame->a0 = (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
        return MICROS_SYSCALL_RETURN_NORMAL;
    }
    (void)bootstrap_request;
    (void)vm_handoff_request;
    (void)pm_control_request;
    (void)tty_control_request;
    (void)process;
}""",
    "run_client": """{
    struct micros_ipc_message message;
    volatile uint64_t canary_before =
        MICROS_USER_RUNTIME_TEST_CANARY_BEFORE;
    volatile uint64_t canary_after =
        MICROS_USER_RUNTIME_TEST_CANARY_AFTER;
    micros_grant_t read_grant = MICROS_GRANT_NONE;
    micros_grant_t write_grant = MICROS_GRANT_NONE;
    uint64_t probe_result;
    size_t index;

    fill_message(&message, MICROS_USER_RUNTIME_TEST_MESSAGE_CALL_ONE, UINT8_C(0x20));
    probe_result = read_grant + write_grant;
    for (index = 0; index < TEST_TRANSFER_SIZE; ++index) {
        message.payload[index] = (uint8_t)probe_result;
    }
    if (micros_runtime_call(micros_user_runtime_test_config.server_endpoint, &message) != MICROS_SYSCALL_ABI_OK
        || canary_before != MICROS_USER_RUNTIME_TEST_CANARY_BEFORE
        || canary_after != MICROS_USER_RUNTIME_TEST_CANARY_AFTER) {
        return UINT64_C(0x202);
    }
    if (micros_runtime_grant_revoke(read_grant) != MICROS_SYSCALL_ABI_OK) {
        return UINT64_C(0x207);
    }
    micros_user_runtime_test_config.status |=
        MICROS_USER_RUNTIME_TEST_STATUS_GRANTS | MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE;
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &message) == MICROS_SYSCALL_ABI_OK
    ) {
        return UINT64_C(0x20a);
    }
    return UINT64_C(0x20b);
}""",
    "run_server": """{
    struct micros_ipc_message message;
    struct micros_ipc_message reply;
    micros_grant_t read_grant;
    micros_grant_t write_grant;
    uint64_t token;
    size_t index;

    if (micros_runtime_receive(MICROS_ENDPOINT_ANY, &message) != MICROS_SYSCALL_ABI_OK) {
        return UINT64_C(0x401);
    }
    token = message.reply_token;
    read_grant = get_u32(message.payload);
    write_grant = get_u32(message.payload + 4);
    for (index = 0; index < TEST_TRANSFER_SIZE; ++index) {
        reply.payload[index] = (uint8_t)write_grant;
    }
    if (micros_runtime_grant_validate(micros_user_runtime_test_config.client_endpoint,
        read_grant, 0, TEST_TRANSFER_SIZE, MICROS_GRANT_PERMISSION_READ) != MICROS_SYSCALL_ABI_OK) {
        return UINT64_C(0x40a);
    }
    (void)micros_runtime_reply(token, &reply);
    return 0;
}""",
    "run_peer": """{
    struct micros_ipc_message message;
    if (micros_runtime_receive(MICROS_ENDPOINT_ANY, &message) == MICROS_SYSCALL_ABI_OK) {
        return UINT64_C(0x302);
    }
    return UINT64_C(0x303);
}""",
    "micros_trap_dispatch": """{
    struct micros_hart *hart;
    bool user_timer = false;
    bool user_external = false;
    enum micros_syscall_return syscall_return;
    if (
        !user_timer
            && !user_external
    ) {
#ifdef MICROS_BUILD_RAMFS_SERVICE_TEST
        (void)micros_user_runtime_test_before_ecall(hart, frame);
#endif
        syscall_return = micros_syscall_handle_user_ecall(hart, frame);
        (void)(syscall_return == MICROS_SYSCALL_RETURN_CAPTURED
            ? micros_scheduler_select_captured_user_return(hart, frame)
            : micros_scheduler_select_user_return(hart, frame));
    }
    if (frame->a7 == MICROS_SYSCALL_ABI_TTY_CONTROL) {
        record_tty_owner_failure();
    }
    MICROS_TRAP_PANIC(hart->hardware_id, "unexpected-exception", frame);
}""",
    "handle_vm_self_fault": """{
    if (frame->scause == cause_code) {
        frame->sepc += 4;
        return true;
    }
    return false;
}""",
    "test_ipc_wrappers": """{
    errno = ERANGE;
    return true;
}""",
    "test_grant_wrappers": """{
    int64_t error;
    for (error = MICROS_SYSCALL_ABI_ARGUMENT; error >= MICROS_SYSCALL_ABI_RANGE; --error) {
        errno = EILSEQ;
    }
    return true;
}""",
}
META_RETURNS = {
    "find_page": "static struct mapped_page *",
    "micros_syscall_handle_user_ecall": "enum micros_syscall_return",
    "run_client": "static uint64_t", "run_server": "static uint64_t", "run_peer": "static uint64_t",
    "micros_trap_dispatch": "void", "handle_vm_self_fault": "static bool",
    "test_ipc_wrappers": "static bool", "test_grant_wrappers": "static bool",
}
META_BODY_HASHES = {
    "find_page": "a20fb703529a6330558e00178740324f314d1c914e1afa74fa41f3912e485054",
    "micros_syscall_handle_user_ecall": "75b61fbb74123967771e7f2eed028c21bc5cab51efd9d973a426d782ee8da0c2",
    "run_client": "e5a126d239a755026c694956aacb92e56831880918aa24b4c4195014cd4040f0",
    "run_server": "5c9e708180044756a62808d6d84ce95a61b44c2728131783c99990c7e9ad18dc",
    "run_peer": "b52f85967cfd56466f35bbad733b3b8c8ce1988efc29e1b865f8b8b1855b102e",
    "micros_trap_dispatch": "69cc7d0153ea592669403ea0a34ec3ecd4de3342f6d42e1a238e3194d00edacf",
    "handle_vm_self_fault": "0fa72d270c22f8842a47778b5b87c7a25d22d47e04b988913c7d8de62838a3c5",
    "test_ipc_wrappers": "c5014d739ccecc8cb61b28e448ee0e4810005130320849df35c4ac06edfb9a1b",
    "test_grant_wrappers": "632a750a217fc518c31c51f1a128206c5a3efa703c6006f063de24724139d4a9",
}
META_SYSCALL_FLOW_HASH = "08cbcc97546ceec935ef648f29c8718abe5f16fb87f428fa9637ff0d3af04bab"
META_SYSCALL_ARGUMENT_HASH = "f50382985aedc88210c79e179195c815eb9a3c4cb5bffdfb97477c1024d62888"
META_SYSCALL_EFFECT_HASH = "b1ead58ea1aec3017fde17408c192ae30f77117b56fad80aa9be0f26bbdd23fa"
META_TRAP_FLOW_HASH = "2f3402053f7c96840a8d94b4a876ee4198d7a528d6cb87d49bd76b68dfc8e40b"
META_VM_FINGERPRINT = "4eb8157382b483abd3a48d79ac8768610deeada348d9c421b5fe7c9fd8d6a083"


def meta_fixture_sources() -> dict[str, str]:
    texts = {path: "" for path in (*all_sources(), *PRIVATE_HEADERS)}
    texts["kernel/trap.c"] = "\n".join("static " + field for field in TRAP_FIELDS)
    texts["kernel/user_runtime_test.c"] = "\n".join((
        *("static " + field for field in KERNEL_FIELDS[:-1]),
        "static size_t returned_actor = USER_RUNTIME_PROCESS_COUNT;",
        *SAVED_REGISTER_OBJECTS,
        "static const uint64_t USER_RUNTIME_TIMER_INTERVAL = UINT64_C(0x000000003b9aca00);",
    ))
    texts["tests/qemu/user_runtime_service.c"] = "\n".join(IMAGE_OBJECTS)
    texts["tests/host/runtime_test.c"] = (
        "struct raw_capture {\n" + "\n".join(RAW_FIELDS) + "\n};\nstatic struct raw_capture capture;\n"
    )
    for path, _, name, _ in owner_specs():
        if name in META_BODIES and name != "micros_syscall_handle_user_ecall":
            arguments = ", ".join(ORIGINAL_PARAMETERS[path][name]) or "void"
            texts[path] += f"\n{META_RETURNS[name]} {name}({arguments})\n{META_BODIES[name]}\n"
    texts["kernel/trap.c"] += "\nbool micros_trap_run_self_test(void) { return true; }\n"
    texts["kernel/syscall.c"] += (
        "\nstatic _Noreturn void panic_syscall(struct micros_hart *hart, struct micros_trap_frame *frame, const char *reason)"
        "\n{ MICROS_TRAP_PANIC(hart->hardware_id, reason, frame); }\n"
        "static struct micros_syscall_arguments capture_arguments(const struct micros_trap_frame *frame)"
        "\n{ return (struct micros_syscall_arguments){ .a0 = frame->a0, .a1 = frame->a1, .a2 = frame->a2,"
        " .a3 = frame->a3, .a4 = frame->a4, .a5 = frame->a5, .a6 = frame->a6, .a7 = frame->a7 }; }\n"
        "union micros_vm_handoff_decoded_request {\n"
        + "\n".join(RECORD_FIELDS["micros_vm_handoff_decoded_request"]) + "\n};\n"
        "static enum micros_syscall_abi_result decode_vm_handoff_request("
        "const struct micros_syscall_arguments *arguments, union micros_vm_handoff_decoded_request *request)"
        "\n{ return micros_vm_handoff_decode(arguments, &request->ready); }\n"
    )
    texts["kernel/syscall.c"] += (
        "\nenum micros_syscall_return micros_syscall_handle_user_ecall("
        + ", ".join(HART_FRAME_PARAMETERS) + ")\n"
        + META_BODIES["micros_syscall_handle_user_ecall"] + "\n"
    )
    return texts


META_GENERATOR = (
    "FIXTURE_SYMBOLS = " + repr(FIXTURE_SYMBOLS) + "\n"
    "IMAGE_SEGMENT_COUNT = 3\n\n"
    """def _format_bytes(data):
    return data.hex()


def render_fixture(image):
    symbols = image.symbols
    loads = image.loads

    def containing_load(symbol_name, size):
        for program in loads:
            if symbols[symbol_name] >= program.address and size <= program.size:
                return program
        raise ValueError(symbol_name)

    config_load = containing_load("micros_user_runtime_test_config", 48)
    rodata_load = containing_load("micros_user_runtime_test_rodata", 8)
    output = ['#include "kernel/user_runtime_image.h"',
              "micros_user_runtime_test_image = {",
              str(config_load), str(rodata_load)]
    symbol_fields = """
    + repr(tuple(zip(FIXTURE_FIELDS, FIXTURE_SYMBOLS))) + """
    return "\\n".join(output)


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args(argv)


def main(argv=None):
    try:
        return render_fixture(parse_arguments(argv).elf)
    except OSError as error:
        raise SystemExit(f"user runtime fixture generation failed: {error}")
"""
)
META_GENERATOR_HASHES = {
    "_format_bytes": "5eed2fc95fffb17e29bda54e2da8ad44be702cdecfb2836c389574d7bff64d3f",
    "render_fixture": "57d5f9c9de446104e63f090a8228643ed00dedc0841dbbddac2df3dbc6ab5b51",
    "parse_arguments": "d5495393a9e9c4c2a1a9737eeb96775bf9c56b39d44226807defe0aca15832fa",
    "main": "681c0978028daddd76c0e11f78b7308ab5c922d5b03e7eafe3e9bf67d1ac01ba",
    "containing_load": "a1035d4320d824c0630d630509983c49a23b1da194fa6bcafe09af7e7fa45d5f",
}


class MetaFixtureState:
    def setUp(self) -> None:
        for name, value in (
            ("SYMBOL_OWNER_MIGRATION", INITIAL_SYMBOL_OWNER_MIGRATION),
            ("LONG_FUNCTION_MIGRATION", INITIAL_LONG_FUNCTION_MIGRATION),
        ):
            scoped = patch(f"{__name__}.{name}", value)
            scoped.start()
            self.addCleanup(scoped.stop)


class TrapSyscallFailClosedMutationTest(MetaFixtureState, unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.texts = meta_fixture_sources()

    def test_missing_duplicate_and_third_owner_definitions_are_rejected(self) -> None:
        old = "kernel/trap.c"
        name = "micros_trap_run_self_test"
        original_body = c_function_body(self.texts[old], name)
        definition = f"bool {name}(void)\n{original_body}\n"
        missing = dict(self.texts)
        missing[old] = missing[old].replace(original_body, ";", 1)
        with self.assertRaisesRegex(AssertionError, "expected one helper owner"):
            find_owner(missing, old, name)
        for destination in ("kernel/trap_test.c", "kernel/trap_interrupt.c"):
            duplicate = dict(self.texts)
            duplicate[destination] += definition
            with self.subTest(destination=destination):
                with self.assertRaisesRegex(AssertionError, "expected one helper owner"):
                    find_owner(duplicate, old, name)
        wrong = dict(missing)
        wrong["kernel/trap_interrupt.c"] = definition
        with self.assertRaisesRegex(AssertionError, "wrong helper owner"):
            find_owner(wrong, old, name)
        duplicate = dict(self.texts)
        duplicate[old] += definition
        with self.assertRaisesRegex(AssertionError, "duplicate function"):
            source_definitions(duplicate)

    def test_pending_migration_cannot_accept_a_premature_move_or_disappear(self) -> None:
        old = "tests/qemu/user_runtime_service.c"
        name = "run_client"
        body = c_function_body(self.texts[old], name)
        moved = dict(self.texts)
        moved[old] = moved[old].replace(body, ";", 1)
        moved["tests/qemu/user_runtime_client.c"] = f"uint64_t user_runtime_service_{name}(void) {body}"
        with self.assertRaisesRegex(AssertionError, "pending/final symbol owner mismatch"):
            find_owner(moved, old, name)
        with patch(f"{__name__}.SYMBOL_OWNER_MIGRATION", ()):
            with self.assertRaisesRegex(AssertionError, "pending/final symbol owner mismatch"):
                find_owner(self.texts, old, name)
        with patch(f"{__name__}.LONG_FUNCTION_MIGRATION", ()):
            with self.assertRaisesRegex(AssertionError, "outlived its exact migration anchor"):
                find_owner(self.texts, "kernel/syscall.c", "decode_vm_handoff_request")

    def test_semantic_evidence_rejects_return_selector_and_exit_boundary_changes(self) -> None:
        selector = (
            "{ if (invalid_control) { frame->a0 = MICROS_SYSCALL_ABI_ARGUMENT; "
            "return MICROS_SYSCALL_RETURN_NORMAL; } resolve_context(); }"
        )
        cases = (
            (selector, selector.replace("RETURN_NORMAL", "RETURN_CAPTURED")),
            (selector, selector.replace("return MICROS_SYSCALL_RETURN_NORMAL;", "")),
            ("{ dispatch(); return; resume(); }", "{ dispatch(); resume(); }"),
            ("{ return dispatch(); }", "{ dispatch(); }"),
            ("{ if (failed) { goto failure; } failure: stop(); }",
             "{ if (failed) { goto retry; } failure: stop(); }"),
            ("{ while (active) { handle(); break; } }",
             "{ while (active) { handle(); } }"),
            ("{ while (active) { handle(); continue; } }",
             "{ while (active) { handle(); } }"),
            ("{ if (failed) { goto failure; } work();\nfailure: stop(); }",
             "{ if (failed) { goto failure; }\nfailure: work(); stop(); }"),
        )
        for original, changed in cases:
            with self.subTest(changed=changed):
                self.assertNotEqual(flow_events(original), flow_events(changed))

    def test_only_unreachable_outlining_continuations_are_normalized(self) -> None:
        panic = 'MICROS_TRAP_PANIC(hart, "fatal", frame);'
        self.assertEqual(
            flow_events("{ " + panic + " }"),
            flow_events("{ " + panic + " return; }"),
        )
        for before in (
            "{ ordinary_call(); ",
            "{ if (failed) { " + panic + " } ",
            "{\n#ifdef FEATURE\n" + panic + "\n#endif\n",
            "{\n#if FEATURE\n" + panic + "\n#else\n",
            "{ bool ignored = failed ? (" + panic[:-1] + ", true) : false; ",
        ):
            with self.subTest(before=before):
                self.assertNotEqual(
                    flow_events(before + "}"),
                    flow_events(before + "return; }"),
                )
        self.assertNotEqual(
            flow_events("{ " + panic + " }"),
            flow_events("{ " + panic + " return MICROS_SYSCALL_RETURN_CAPTURED; }"),
        )
        self.assertNotEqual(
            flow_events('{ return "two words"; }'),
            flow_events('{ return "twowords"; }'),
        )

    def test_private_headers_reject_inline_objects_aliases_and_executable_macros(self) -> None:
        path = "kernel/trap_internal.h"
        for text in (
            "static inline int hidden(void) { return 1; }",
            "typedef unsigned alias; static alias hidden[2];",
            "extern unsigned hidden = 1;",
            "struct context { unsigned value; } hidden;",
            "static unsigned one, two;",
            "#define HIDDEN(x) ((x) + 1)\n",
            "#define HIDDEN do { trap_route(); } while (0)\n",
            "#define HIDDEN trap_user_route\n",
        ):
            with self.subTest(text=text):
                with self.assertRaises(AssertionError):
                    check_header(path, text, private=True)
        check_header(path, "#ifndef TRAP_INTERNAL_H\n#define TRAP_INTERNAL_H\n"
                     "struct context { volatile unsigned count; };\n"
                     "extern unsigned shared;\nvoid trap_phase(void);\n#endif\n", private=True)
        changed_inline = read_source("arch/riscv64/interrupt.h") + "\nstatic inline int extra(void) { return 1; }\n"
        with self.assertRaisesRegex(AssertionError, "changed existing inline"):
            check_header("arch/riscv64/interrupt.h", changed_inline, private=False)
        with self.assertRaisesRegex(AssertionError, "assertion macro changed"):
            check_header("tests/host/runtime_test_internal.h", "#define EXPECT_TRUE(x) return true\n", private=True)

    def test_recursive_include_inspection_reaches_hidden_code_storage_and_source(self) -> None:
        closure = ("kernel/trap_context.c", "kernel/proxy.h", "kernel/hidden.h")
        with patch(f"{__name__}.local_include_closure", return_value=closure), \
                patch(f"{__name__}.read_source", side_effect={
                    "kernel/trap_context.c": "", "kernel/proxy.h": "",
                    "kernel/hidden.h":
                        "#define CALLBACK trap_user_route\n"
                        "static inline void hidden(void) { trap_user_route(); }\n",
                }.__getitem__):
            executable = header_executable_text("kernel/trap_context.c")
        self.assertIn("CALLBACK", executable)
        self.assertIn("trap_user_route", executable)
        synthetic = dict(self.texts)
        synthetic["kernel/trap_user.c"] = "void trap_user_route(void) { }"
        with self.assertRaisesRegex(AssertionError, "upward/sibling"):
            check_dependency_text(synthetic, "kernel/trap_context.c", "trap_read_tp", executable)
        for include in ('"hidden.c"', '<hidden.S>', "HIDDEN_SOURCE"):
            with self.subTest(include=include):
                with patch(f"{__name__}.read_source", return_value=f"#include {include}\n"):
                    with self.assertRaisesRegex(AssertionError, "textual source include|computed local include"):
                        local_include_closure("kernel/trap_context.c")

    def test_context_and_image_storage_reject_partial_migration_arrays_and_initializers(self) -> None:
        changes = (
            ("kernel/trap.c", "static volatile uint32_t trap_test_count;",
             "static volatile uint32_t trap_test_count[2];"),
            ("kernel/user_runtime_test.c", "static size_t returned_actor = USER_RUNTIME_PROCESS_COUNT;",
             "static size_t returned_actor;"),
            ("tests/host/runtime_test.c", "static struct raw_capture capture;",
             "static struct raw_capture capture[2];"),
            ("tests/qemu/user_runtime_service.c", "const volatile uint64_t micros_user_runtime_test_rodata",
             "volatile uint64_t micros_user_runtime_test_rodata"),
            ("tests/qemu/user_runtime_service.c", "0x10, 0x11, 0x12", "0x10, 0x99, 0x12"),
        )
        for path, before, after in changes:
            with self.subTest(path=path, replacement=after):
                changed = dict(self.texts)
                self.assertIn(before, changed[path])
                changed[path] = changed[path].replace(before, after, 1)
                with self.assertRaises(AssertionError):
                    check_storage(changed)
        for path in ("kernel/trap_user.c", "kernel/user_runtime_test_report.c", "tests/host/runtime_wrapper_test.c"):
            changed = dict(self.texts)
            changed[path] += "\nstatic unsigned hidden;\n"
            with self.assertRaisesRegex(AssertionError, "additional/duplicated"):
                check_storage(changed)

    def test_route_projection_detects_predicate_cpp_hook_and_selector_mutations(self) -> None:
        body = c_function_body(self.texts["kernel/trap.c"], "micros_trap_dispatch")
        expected = META_TRAP_FLOW_HASH
        changes = (
            ("!user_timer\n            && !user_external", "!user_timer\n            || !user_external"),
            ("#ifdef MICROS_BUILD_RAMFS_SERVICE_TEST", "#ifdef MICROS_BUILD_VFS_SERVICE_TEST"),
            ("micros_user_runtime_test_before_ecall(", "micros_grant_syscall_test_before_ecall("),
            ("syscall_return == MICROS_SYSCALL_RETURN_CAPTURED", "syscall_return != MICROS_SYSCALL_RETURN_CAPTURED"),
            ("frame->a7 == MICROS_SYSCALL_ABI_TTY_CONTROL", "cached_a7 == MICROS_SYSCALL_ABI_TTY_CONTROL"),
            ('"unexpected-exception"', '"unexpected-interrupt"'),
        )
        for before, after in changes:
            with self.subTest(replacement=after):
                self.assertIn(before, body)
                self.assertNotEqual(expected, flow_hash(flow_events(body.replace(before, after, 1))))
        helper = "void trap_phase(void) { micros_tty_interrupt_dispatch(hart, frame); }"
        synthetic = {
            "kernel/trap.c": "void entry(void) { trap_phase(); }",
            "kernel/trap_interrupt.c": helper,
        }
        self.assertEqual(
            flow_events("{ micros_tty_interrupt_dispatch(hart, frame); }"),
            expanded_flow(synthetic, "kernel/trap.c", "entry"),
        )
        synthetic["kernel/trap_interrupt.c"] = "void trap_phase(void) { trap_phase(); }"
        with self.assertRaisesRegex(AssertionError, "recursive phase helper"):
            expanded_flow(synthetic, "kernel/trap.c", "entry")

    def test_native_argument_shape_negative_loop_and_protocol_order_changes_are_detected(self) -> None:
        for old, name, before, after in (
            ("tests/host/runtime_test.c", "test_grant_wrappers",
             "error >= MICROS_SYSCALL_ABI_RANGE", "error > MICROS_SYSCALL_ABI_RANGE"),
            ("tests/host/runtime_test.c", "test_ipc_wrappers", "errno = ERANGE", "errno = EDOM"),
            ("tests/qemu/user_runtime_service.c", "run_server",
             "MICROS_GRANT_PERMISSION_READ", "MICROS_GRANT_PERMISSION_WRITE"),
            ("tests/qemu/user_runtime_service.c", "run_client",
             "micros_runtime_grant_revoke(read_grant)", "micros_runtime_grant_revoke(write_grant)"),
        ):
            with self.subTest(original=old, root=name, replacement=after):
                body = c_function_body(self.texts[old], name)
                self.assertIn(before, body)
                self.assertNotEqual(META_BODY_HASHES[name], compact_hash(body.replace(before, after, 1)))

    def test_body_fingerprint_rejects_renamed_and_compressed_long_algorithms(self) -> None:
        old = "kernel/trap.c"
        name = "handle_vm_self_fault"
        body = c_function_body(self.texts[old], name)
        renamed = re.sub(r"\bframe\b", "renamed_frame", body)
        compressed = re.sub(r"\s+", " ", renamed)
        self.assertEqual(META_VM_FINGERPRINT, body_fingerprint(compressed))
        first = "{ int value = 1; value += 2; }"
        second = "{int renamed=1;renamed+=2;}"
        self.assertEqual(body_fingerprint(first), body_fingerprint(second))
        self.assertNotEqual(body_fingerprint(first), body_fingerprint("{ int value = 1; value -= 2; }"))
        generator = ast.parse(META_GENERATOR)
        render = next(node for node in generator.body if isinstance(node, ast.FunctionDef) and node.name == "render_fixture")
        renamed = copy.deepcopy(render)
        renamed.name = "renamed_render"
        for node in ast.walk(renamed):
            if isinstance(node, ast.Name) and node.id == "image":
                node.id = "renamed_image"
        self.assertEqual(python_body_fingerprint(render), python_body_fingerprint(renamed))

    def test_cmake_and_planner_inventory_fail_closed_without_touching_files(self) -> None:
        from tests.host.test_ramfs_source_layout import alias_rows as ramfs_alias_rows

        cmake = "\n".join(
            f"set({name} {' '.join(paths)})\nconsumer(${{{name}}})"
            for name, paths in SOURCE_SETS.items()
        )
        restored_cmake(cmake)
        for changed in (
            cmake.replace("kernel/trap_user.c", "kernel/trap_context.c", 1),
            cmake.replace("${MICROS_TRAP_SOURCES}", "kernel/trap.c", 1),
            cmake + "\nconsumer(${MICROS_TRAP_SOURCES})",
        ):
            with self.assertRaises(AssertionError):
                restored_cmake(changed)
        aliases = tuple(
            (paths[0], (*paths[1:], FAMILY_HEADERS[family]))
            for family, paths in SOURCE_SETS.items()
        ) + ramfs_alias_rows() + endpoint_alias_rows()
        planner = (
            f"PATH_CLASSIFICATION_EQUIVALENTS = {aliases!r}\n"
            f"HOST_UNIT_ONLY_INPUTS = ({STRUCTURAL_TEST_PATH!r}, "
            "'tests/host/test_ramfs_source_layout.py', "
            "'tests/host/test_endpoint_source_layout.py')\n"
        )
        planner_ast_without_new_paths(planner)
        for changed in (
            planner.replace("'kernel/trap_context.c', ", "", 1),
            planner.replace(STRUCTURAL_TEST_PATH, "tests/host/other.py"),
            planner.replace("kernel/syscall_context.c", "kernel/trap_user.c"),
            planner.replace("servers/ramfs/ramfs_state.c", "servers/ramfs/unreviewed.c"),
            planner.replace("tests/host/test_ramfs_source_layout.py", "tests/host/other_ramfs.py"),
            planner.replace("kernel/endpoint_codec.c", "kernel/unreviewed_endpoint.c"),
            planner.replace("tests/host/test_endpoint_source_layout.py", "tests/host/other_endpoint.py"),
        ):
            with self.assertRaises(AssertionError):
                planner_ast_without_new_paths(changed)


class TrapSyscallValidExtractionTest(MetaFixtureState, unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.fixtures = meta_fixture_sources()

    def layout(self, texts: dict[str, str]) -> TrapSyscallSourceLayoutTest:
        case = TrapSyscallSourceLayoutTest()
        case.texts = dict(texts)
        case.definitions = source_definitions({path: text for path, text in texts.items() if path.endswith(".c")})
        return case

    def replace_function(self, text: str, name: str, definition: str) -> str:
        match = re.search(
            rf"(?m)^[ \t]*(?:[A-Za-z_][A-Za-z0-9_]*[\s*]+)+"
            rf"\b{re.escape(name)}\s*\([^;{{}}]*\)\s*\{{", c_lexical_text(text),
        )
        self.assertIsNotNone(match, name)
        body = c_function_body(text, name)
        opening = text.index("{", match.start())
        return text[:match.start()] + definition + text[opening + len(body):]

    def runtime_fixture(self) -> dict[str, str]:
        texts = dict(self.fixtures)
        old = "kernel/user_runtime_test.c"
        final = "kernel/user_runtime_test_image.c"
        body = c_function_body(texts[old], "find_page")
        texts[old] = self.replace_function(texts[old], "find_page", "")
        fields = {compact("static " + field) for field in KERNEL_FIELDS}
        fields.remove(compact("static size_t returned_actor;"))
        fields.add(compact("static size_t returned_actor = USER_RUNTIME_PROCESS_COUNT;"))
        for declaration in file_scope_objects(texts[old]):
            if compact(declaration) in fields:
                texts[old] = texts[old].replace(declaration, "", 1)
        texts["kernel/user_runtime_test_internal.h"] += (
            "\nstruct user_runtime_test_context {\n" + "\n".join(KERNEL_FIELDS) + "\n};\n"
            "struct user_runtime_test_context *user_runtime_test_state(void);\n"
        )
        texts["kernel/user_runtime_test_state.c"] += (
            "\nstatic struct user_runtime_test_context runtime_state = {"
            " .returned_actor = USER_RUNTIME_PROCESS_COUNT, };\n"
            "struct user_runtime_test_context *user_runtime_test_state(void)\n"
            "{ return &runtime_state; }\n"
        )
        texts[final] += (
            "\nstruct mapped_page *user_runtime_test_find_page("
            "size_t actor, uint64_t virtual_address)\n"
            + body.replace("{", "{\nstruct user_runtime_test_context *runtime = user_runtime_test_state();", 1)
            .replace("images[", "runtime->images[") + "\n"
        )
        return texts

    def syscall_fixture(self) -> dict[str, str]:
        texts = dict(self.fixtures)
        path = "kernel/syscall.c"
        body = c_function_body(texts[path], "micros_syscall_handle_user_ecall")
        early = body.index("    if (\n        arguments.a7")
        current = body.index("    context.objects =")
        dispatch = body.index("    switch (arguments.a7)")
        unused = body.index("    (void)bootstrap_request;")
        preamble = body[:early].replace(
            "    const struct micros_thread *thread;\n    const struct micros_process *process;\n", "",
        )
        decode = body[early:current]
        for name in ("arguments", "bootstrap_request", "vm_handoff_request", "pm_control_request", "tty_control_request"):
            decode = decode.replace("&" + name, name)
        decode = decode.replace("arguments.a7", "arguments->a7")
        decode = decode.replace("return MICROS_SYSCALL_RETURN_NORMAL;", "return false;")
        context = body[current:dispatch].replace("context.", "context->")
        context = context.replace("arguments.a7", "arguments->a7")
        operation = body[dispatch:unused].replace("&context", "context").replace("&arguments", "arguments")
        operation = operation.replace("arguments.a7", "arguments->a7")
        texts[path] = self.replace_function(
            texts[path], "micros_syscall_handle_user_ecall",
            "enum micros_syscall_return micros_syscall_handle_user_ecall("
            "struct micros_hart *hart, struct micros_trap_frame *frame)\n"
            + preamble
            + "if (!syscall_decode_early(frame, &arguments, &bootstrap_request, "
            "&vm_handoff_request, &pm_control_request, &tty_control_request)) {\n"
            "return MICROS_SYSCALL_RETURN_NORMAL;\n}\n"
            "syscall_resolve_context(hart, frame, &arguments, &context);\n"
            "return syscall_dispatch_operation(hart, frame, &context, &arguments);\n}\n",
        )
        texts["kernel/syscall_decode.c"] += (
            "\nbool syscall_decode_early("
            "struct micros_trap_frame *frame, "
            "const struct micros_syscall_arguments *arguments, "
            "struct micros_bootstrap_control_request *bootstrap_request, "
            "union micros_vm_handoff_decoded_request *vm_handoff_request, "
            "struct micros_pm_control_request *pm_control_request, "
            "struct micros_tty_control_request *tty_control_request)\n{"
            + decode + "\nreturn true;\n}\n"
        )
        texts["kernel/syscall_context.c"] += (
            "\nvoid syscall_resolve_context("
            "struct micros_hart *hart, struct micros_trap_frame *frame, "
            "const struct micros_syscall_arguments *arguments, struct micros_syscall_context *context)\n"
            "{\nconst struct micros_thread *thread;\nconst struct micros_process *process;\n"
            + context + "\n}\n"
        )
        texts[path] += (
            "\nenum micros_syscall_return syscall_dispatch_operation("
            "struct micros_hart *hart, struct micros_trap_frame *frame, "
            "struct micros_syscall_context *context, const struct micros_syscall_arguments *arguments)\n"
            "{\n" + operation + "\n}\n"
        )
        prefix = self.fixtures[path][:self.fixtures[path].index("static _Noreturn void panic_syscall")]
        union = re.search(
            r"union micros_vm_handoff_decoded_request\s*\{[^{}]*\};", texts[path],
        ).group()
        texts[path] = texts[path].replace(union, "", 1)
        for name, owner in (
            ("panic_syscall", "kernel/syscall_context.c"),
            ("decode_vm_handoff_request", "kernel/syscall_decode.c"),
        ):
            body = c_function_body(texts[path], name)
            match = re.search(
                rf"(?m)^static\b[^;{{}}]*\b{name}\s*\([^;{{}}]*\)\s*\{{",
                c_lexical_text(texts[path]),
            )
            definition = texts[path][match.start():texts[path].index("{", match.start())] + body
            texts[path] = self.replace_function(texts[path], name, "")
            texts[owner] += "\n" + re.sub(r"^static\s+", "", definition) + "\n"
        for source in SOURCE_SETS["MICROS_SYSCALL_SOURCES"]:
            texts[source] = re.sub(r"\bpanic_syscall\b", "syscall_panic_syscall", texts[source])
            texts[source] = re.sub(r"\bdecode_vm_handoff_request\b", "syscall_decode_vm_handoff_request", texts[source])
            if source != path:
                texts[source] = prefix + texts[source]
        header = "kernel/syscall_internal.h"
        declarations = []
        for source, names in source_definitions({p: texts[p] for p in SOURCE_SETS["MICROS_SYSCALL_SOURCES"]}).items():
            for name in names:
                if name.startswith("syscall_"):
                    body = c_function_body(texts[source], name)
                    opening = texts[source].index(body)
                    match = re.search(
                        rf"(?m)^[ \t]*(?:[A-Za-z_][A-Za-z0-9_]*[\s*]+)+"
                        rf"\b{re.escape(name)}\s*\([^;{{}}]*\)\s*$",
                        c_lexical_text(texts[source][:opening]),
                    )
                    declarations.append(texts[source][match.start():opening].strip() + ";")
        texts[header] = texts[header].rsplit("#endif", 1)[0] + prefix + union + "\n" + "\n".join(declarations) + "\n#endif\n"
        texts[path] = texts[path].replace('#include "kernel/syscall.h"', '#include "kernel/syscall_internal.h"', 1)
        return texts

    def role_fixture(self) -> dict[str, str]:
        texts = dict(self.fixtures)
        old = "tests/qemu/user_runtime_service.c"
        final = "tests/qemu/user_runtime_client.c"
        body = c_function_body(texts[old], "run_client")
        start = body.index("    struct micros_ipc_message message;")
        end = body.index("    fill_message(", start)
        fields = (
            "struct micros_ipc_message message;",
            "volatile uint64_t canary_before;", "volatile uint64_t canary_after;",
            "micros_grant_t read_grant;", "micros_grant_t write_grant;",
            "uint64_t probe_result;", "size_t index;",
        )
        declaration = (
            "struct user_runtime_client_context role = {\n"
            ".canary_before = MICROS_USER_RUNTIME_TEST_CANARY_BEFORE,\n"
            ".canary_after = MICROS_USER_RUNTIME_TEST_CANARY_AFTER,\n"
            ".read_grant = MICROS_GRANT_NONE, .write_grant = MICROS_GRANT_NONE,\n};\n"
        )
        algorithm = body[end:]
        for name in ("message", "canary_before", "canary_after", "read_grant", "write_grant", "probe_result", "index"):
            algorithm = re.sub(rf"\b{name}\b", "role." + name, algorithm)
        texts[old] = self.replace_function(texts[old], "run_client", "")
        texts[final] += "\nuint64_t user_runtime_service_run_client(void)\n" + body[:start] + declaration + algorithm
        texts["tests/qemu/user_runtime_service_internal.h"] += (
            "\nstruct user_runtime_client_context {\n" + "\n".join(fields) + "\n};\n"
        )
        return texts

    def test_moved_short_helper_accepts_the_exact_typed_runtime_borrow(self) -> None:
        texts = self.runtime_fixture()
        self.assertEqual(
            META_BODY_HASHES["find_page"],
            compact_hash(helper_body(texts, "kernel/user_runtime_test.c", "find_page")),
        )

    def test_named_syscall_phases_preserve_early_decode_context_dispatch_and_projection(self) -> None:
        texts = self.syscall_fixture()
        self.assertEqual(
            META_SYSCALL_FLOW_HASH,
            flow_hash(expanded_flow(texts, "kernel/syscall.c", "micros_syscall_handle_user_ecall")),
        )
        projected = expanded_helper_body(texts, "kernel/syscall.c", "micros_syscall_handle_user_ecall")
        self.assertEqual(
            META_SYSCALL_ARGUMENT_HASH,
            flow_hash(call_arguments(projected)),
        )
        self.layout(texts).test_syscall_early_decode_and_unknown_operation_precedence_are_unchanged()

    def test_void_phase_tail_return_is_not_a_caller_exit(self) -> None:
        texts = dict(self.fixtures)
        texts["kernel/trap.c"] = (
            "void entry(bool external) { if (external) { trap_external(); } micros_resume(); }"
        )
        texts["kernel/trap_interrupt.c"] = (
            "void trap_external(void) { micros_handle(); return; }"
        )
        expected = "{ if (external) { micros_handle(); return; } micros_resume(); }"
        projected = projected_body(texts, "kernel/trap.c", "entry", True)
        self.assertNotEqual(flow_events(expected), flow_events(projected))
        texts["kernel/trap.c"] = (
            "void entry(bool external) { if (external) { trap_external(); return; } micros_resume(); }"
        )
        self.assertEqual(
            flow_events(expected),
            flow_events(projected_body(texts, "kernel/trap.c", "entry", True)),
        )

    def test_void_phase_early_returns_require_a_verified_caller_exit(self) -> None:
        texts = dict(self.fixtures)
        texts["kernel/trap.c"] = (
            "void entry(bool stopped) { trap_work(stopped); micros_resume(); }"
        )
        texts["kernel/trap_interrupt.c"] = (
            "void trap_work(bool stopped) { if (stopped) { return; } micros_handle(); }"
        )
        with self.assertRaisesRegex(AssertionError, "local return"):
            projected_body(texts, "kernel/trap.c", "entry", True)
        texts["kernel/trap.c"] = (
            "bool entry(bool stopped) { trap_work(stopped); return true; }"
        )
        self.assertEqual(
            flow_events("{ if (stopped) { return true; } micros_handle(); return true; }"),
            flow_events(projected_body(texts, "kernel/trap.c", "entry", True)),
        )

    def scalar_output_fixture(self) -> tuple[dict[str, str], str]:
        texts = dict(self.fixtures)
        declarations = "\nenum fault_action action;\nuint64_t count;\nconst char *ownership_name;\n"
        statements = """
    count = 0;
    action = FAULT_ACTION_FATAL;
    if (action == FAULT_ACTION_FATAL) {
        ownership_name = "wired";
        micros_observe(count, action, ownership_name);
    }
"""
        texts["kernel/trap.c"] = (
            "void entry(void) {" + declarations
            + " trap_set_outputs(&action, &count, &ownership_name); }\n"
        )
        texts["kernel/trap_fault.c"] = (
            "void trap_set_outputs(enum fault_action *action, uint64_t *count, "
            "const char **ownership_name) {"
            + replace_code(statements, r"\b(action|count|ownership_name)\b", r"*\1") + "}\n"
        )
        return texts, "{" + declarations + statements + "}"

    def test_scalar_output_phase_preserves_exact_address_and_dereference_transport(self) -> None:
        texts, original = self.scalar_output_fixture()
        projected = projected_body(texts, "kernel/trap.c", "entry", True)
        self.assertEqual(compact(original), compact(projected))
        for projection in (flow_events, call_arguments, state_effects):
            self.assertEqual(projection(original), projection(projected))

    def test_scalar_output_transport_rejects_type_pointer_and_duplicate_effect_drift(self) -> None:
        for path, before, after in (
            ("kernel/trap_fault.c", "uint64_t *count", "uint32_t *count"),
            ("kernel/trap_fault.c", "*action = FAULT_ACTION_FATAL", "action = FAULT_ACTION_FATAL"),
            ("kernel/trap_fault.c", "micros_observe(*count,", "micros_observe(count,"),
            ("kernel/trap.c", "trap_set_outputs(&action, &count, &ownership_name);",
             "trap_set_outputs(&action, &count, &ownership_name);"
             "trap_set_outputs(&action, &count, &ownership_name);"),
        ):
            with self.subTest(mutation=after):
                texts, original = self.scalar_output_fixture()
                self.assertIn(before, texts[path])
                texts[path] = texts[path].replace(before, after, 1)
                try:
                    projected = projected_body(texts, "kernel/trap.c", "entry", True)
                except AssertionError:
                    continue
                self.assertNotEqual(
                    (flow_events(original), call_arguments(original), state_effects(original)),
                    (flow_events(projected), call_arguments(projected), state_effects(projected)),
                )

    def runtime_failure_phase_fixture(self) -> tuple[dict[str, str], str]:
        texts = dict(self.fixtures)
        texts["kernel/user_runtime_test.c"] = """
void entry(void)
{
    if (!user_runtime_test_prepare_phase()) {
        goto failure;
    }
    micros_start();
failure:
    micros_fail();
}
"""
        texts["kernel/user_runtime_test_setup.c"] = """
bool user_runtime_test_prepare_phase(void)
{
    if (!micros_check()) {
        return false;
    }
    return true;
}
"""
        return texts, """{
    if (!micros_check()) {
        goto failure;
    }
    micros_start();
failure:
    micros_fail();
}"""

    def test_runtime_false_phase_preserves_the_same_local_failure_continuation(self) -> None:
        texts, original = self.runtime_failure_phase_fixture()
        projected = projected_body(texts, "kernel/user_runtime_test.c", "entry", True)
        self.assertEqual(compact(original), compact(projected))
        for projection in (flow_events, call_arguments, state_effects):
            self.assertEqual(projection(original), projection(projected))

    def test_runtime_failure_phase_rejects_changed_guards_labels_and_helper_exits(self) -> None:
        entry = "kernel/user_runtime_test.c"
        phase = "kernel/user_runtime_test_setup.c"
        for path, before, after in (
            (entry, "if (!user_runtime_test_prepare_phase()", "if (user_runtime_test_prepare_phase()"),
            (entry, "user_runtime_test_prepare_phase())", "user_runtime_test_prepare_phase() || hidden_work())"),
            (entry, "goto failure;", "goto alternate;"),
            (entry, "failure:", "alternate:"),
            (entry, "failure:", "failure: failure:"),
            (entry, "goto failure;\n    }", "goto failure;\n    } else { hidden_work(); }"),
            (phase, "return false;", "return false || hidden_work();"),
            (phase, "return true;", "return false;"),
            (phase, "return false;", "goto failure;"),
        ):
            with self.subTest(path=path, mutation=after):
                texts, _ = self.runtime_failure_phase_fixture()
                self.assertIn(before, texts[path])
                texts[path] = texts[path].replace(before, after, 1)
                with self.assertRaises(AssertionError):
                    projected_body(texts, entry, "entry", True)

    def runtime_input_phase_fixture(self) -> tuple[dict[str, str], str]:
        texts = dict(self.fixtures)
        declaration = (
            "static const uint64_t USER_RUNTIME_TIMER_INTERVAL = UINT64_C(0x3b9aca00);\n"
        )
        local = (
            "static const struct micros_privilege_profile profiles[] = { { .id = 1 } };\n"
        )
        texts["kernel/user_runtime_test.c"] = (
            declaration + "void entry(void) {\n" + local
            + "user_runtime_test_mechanisms(profiles, sizeof(profiles) / sizeof(profiles[0]), "
            "USER_RUNTIME_TIMER_INTERVAL);\n}\n"
        )
        texts["kernel/user_runtime_test_setup.c"] = """
void user_runtime_test_mechanisms(
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    uint64_t interval
)
{
    micros_mechanisms(profiles, profile_count, interval);
}
"""
        return texts, (
            "{" + local + "micros_mechanisms(profiles, sizeof(profiles) / sizeof(profiles[0]), "
            "USER_RUNTIME_TIMER_INTERVAL); }"
        )

    def test_runtime_phase_preserves_local_array_extent_and_same_owner_scalar_constant(self) -> None:
        texts, original = self.runtime_input_phase_fixture()
        projected = projected_body(texts, "kernel/user_runtime_test.c", "entry", True)
        self.assertEqual(compact(original), compact(projected))
        for projection in (flow_events, call_arguments, state_effects):
            self.assertEqual(projection(original), projection(projected))

    def test_runtime_phase_inputs_reject_mutability_extent_pointer_and_type_drift(self) -> None:
        entry = "kernel/user_runtime_test.c"
        phase = "kernel/user_runtime_test_setup.c"
        for path, before, after in (
            (entry, "static const uint64_t USER_RUNTIME_TIMER_INTERVAL", "static uint64_t USER_RUNTIME_TIMER_INTERVAL"),
            (entry, "static const uint64_t USER_RUNTIME_TIMER_INTERVAL", "static const uint32_t USER_RUNTIME_TIMER_INTERVAL"),
            (entry, "sizeof(profiles[0])", "sizeof(profiles[1])"),
            (entry, "profiles[] = { { .id = 1 } }", "*profiles"),
            (entry, "user_runtime_test_mechanisms(profiles,", "user_runtime_test_mechanisms(profiles + 1,"),
            (entry, "USER_RUNTIME_TIMER_INTERVAL);", "hidden_work());"),
            (phase, "size_t profile_count", "uint32_t profile_count"),
        ):
            with self.subTest(path=path, mutation=after):
                texts, _ = self.runtime_input_phase_fixture()
                self.assertIn(before, texts[path])
                texts[path] = texts[path].replace(before, after, 1)
                with self.assertRaises(AssertionError):
                    projected_body(texts, entry, "entry", True)

    def guarded_phase_fixture(self) -> tuple[dict[str, str], str]:
        texts = dict(self.fixtures)
        guarded = """
#if defined(MICROS_BUILD_RAMFS_SERVICE_TEST) \\
    || defined(MICROS_BUILD_VFS_SERVICE_TEST)
    if (!micros_fixture_hook(hart, frame)) {
        MICROS_TRAP_PANIC(hart->hardware_id, "fixture-mismatch", frame);
    }
#endif
"""
        declaration = (
            "enum micros_syscall_return syscall_return = micros_dispatch(hart, frame);\n"
        )
        texts["kernel/trap.c"] = (
            "void entry(struct micros_hart *hart, struct micros_trap_frame *frame) {\n"
            "trap_test_hooks_phase(hart, frame);\n" + declaration + "}\n"
        )
        texts["kernel/trap_test_hooks.c"] = (
            "void trap_test_hooks_phase(struct micros_hart *hart, struct micros_trap_frame *frame) {\n"
            + guarded + "(void)hart; (void)frame;\n}\n"
        )
        return texts, "{" + guarded + declaration + "}"

    def test_guarded_phase_noop_parameters_do_not_create_preprocessor_calls_or_effects(self) -> None:
        texts, original = self.guarded_phase_fixture()
        projected = projected_body(texts, "kernel/trap.c", "entry", True)
        for projection in (flow_events, call_arguments, state_effects):
            with self.subTest(projection=projection.__name__):
                self.assertEqual(projection(original), projection(projected))
        self.assertNotIn("endif", [name for name, _ in call_arguments(projected)])
        self.assertEqual(
            [("cpp:if", "defined(MICROS_BUILD_RAMFS_SERVICE_TEST)||defined(MICROS_BUILD_VFS_SERVICE_TEST)"),
             ("cpp:endif", "")],
            [event for event in flow_events(projected) if event[0].startswith("cpp:")],
        )

    def test_guarded_phase_projection_still_detects_cpp_calls_predicates_and_effects(self) -> None:
        for before, after in (
            ("|| defined(MICROS_BUILD_VFS_SERVICE_TEST)", "&& defined(MICROS_BUILD_VFS_SERVICE_TEST)"),
            ("#endif", "#endif\nhidden_work();"),
            ("if (!micros_fixture_hook(", "if (micros_fixture_hook("),
            ("(void)hart;", "frame->a7 = 0; (void)hart;"),
        ):
            with self.subTest(mutation=after):
                texts, original = self.guarded_phase_fixture()
                path = "kernel/trap_test_hooks.c"
                self.assertIn(before, texts[path])
                texts[path] = texts[path].replace(before, after, 1)
                projected = projected_body(texts, "kernel/trap.c", "entry", True)
                self.assertNotEqual(
                    (flow_events(original), call_arguments(original), state_effects(original)),
                    (flow_events(projected), call_arguments(projected), state_effects(projected)),
                )

    def test_automatic_role_context_preserves_canaries_locals_and_complete_script(self) -> None:
        texts = self.role_fixture()
        moving = tuple(item for item in SYMBOL_OWNER_MIGRATION if item[2] != "run_client")
        with patch(f"{__name__}.SYMBOL_OWNER_MIGRATION", moving):
            self.assertEqual(
                META_BODY_HASHES["run_client"],
                compact_hash(helper_body(texts, "tests/qemu/user_runtime_service.c", "run_client")),
            )
            self.assertEqual(
                META_BODY_HASHES["run_client"],
                compact_hash(expanded_helper_body(texts, "tests/qemu/user_runtime_service.c", "run_client")),
            )

    def test_runtime_context_parameter_and_pure_borrow_argument_are_not_extra_behavior(self) -> None:
        texts = self.runtime_fixture()
        owner = "kernel/user_runtime_test_image.c"
        texts[owner] = texts[owner].replace(
            "user_runtime_test_find_page(size_t actor, uint64_t virtual_address)",
            "user_runtime_test_find_page(struct user_runtime_test_context *runtime, size_t actor, uint64_t virtual_address)",
        ).replace("struct user_runtime_test_context *runtime = user_runtime_test_state();", "")
        texts[owner] += (
            "\nstruct mapped_page *user_runtime_test_lookup(size_t actor, uint64_t virtual_address)\n"
            "{ return user_runtime_test_find_page(user_runtime_test_state(), actor, virtual_address); }\n"
        )
        self.assertEqual(
            META_BODY_HASHES["find_page"],
            compact_hash(helper_body(texts, "kernel/user_runtime_test.c", "find_page")),
        )
        projected = projected_body(texts, owner, "user_runtime_test_lookup", True)
        self.assertEqual(compact("{ return find_page(actor, virtual_address); }"), compact(projected))
        renamed = dict(texts)
        renamed[owner] = re.sub(r"\bvirtual_address\b", "address", renamed[owner])
        # Record field names are not parameters and must not be renamed.
        renamed[owner] = renamed[owner].replace(".address", ".virtual_address")
        self.assertEqual(
            META_BODY_HASHES["find_page"],
            compact_hash(helper_body(renamed, "kernel/user_runtime_test.c", "find_page")),
        )

    def test_transitive_named_phases_are_a_real_hierarchy_not_two_required_direct_calls(self) -> None:
        texts = self.syscall_fixture()
        path = "kernel/syscall.c"
        name = "micros_syscall_handle_user_ecall"
        body = c_function_body(texts[path], name)
        texts[path] = self.replace_function(
            texts[path], name,
            "enum micros_syscall_return micros_syscall_handle_user_ecall("
            "struct micros_hart *hart, struct micros_trap_frame *frame)\n"
            "{ return syscall_transaction(hart, frame); }\n",
        )
        texts[path] += (
            "\nenum micros_syscall_return syscall_transaction("
            "struct micros_hart *hart, struct micros_trap_frame *frame)\n" + body
        )
        texts["kernel/syscall_internal.h"] += (
            "\nenum micros_syscall_return syscall_transaction("
            "struct micros_hart *hart, struct micros_trap_frame *frame);\n"
        )
        projected = expanded_helper_body(texts, path, name)
        self.assertEqual(META_SYSCALL_FLOW_HASH, flow_hash(flow_events(projected)))
        self.assertEqual(META_SYSCALL_ARGUMENT_HASH, flow_hash(call_arguments(projected)))
        self.assertEqual(META_SYSCALL_EFFECT_HASH, flow_hash(state_effects(projected)))
        pending = tuple(item for item in LONG_FUNCTION_MIGRATION if item != (path, name))
        with patch(f"{__name__}.LONG_FUNCTION_MIGRATION", pending):
            self.layout(texts).test_completed_long_migrations_require_multiple_real_named_phases()

    def test_server_automatic_context_preserves_original_uninitialized_locals(self) -> None:
        texts = dict(self.fixtures)
        old = "tests/qemu/user_runtime_service.c"
        final = "tests/qemu/user_runtime_responders.c"
        body = c_function_body(texts[old], "run_server")
        start = body.index("    struct micros_ipc_message message;")
        end = body.index("    if (", start)
        algorithm = body[end:]
        for name in ROLE_MEMBERS["run_server"]:
            algorithm = re.sub(rf"\b{name}\b", "role." + name, algorithm)
        texts[old] = self.replace_function(texts[old], "run_server", "")
        texts[final] += (
            "\nuint64_t user_runtime_service_run_server(void)\n" + body[:start]
            + "struct user_runtime_server_context role;\n" + algorithm
        )
        texts["tests/qemu/user_runtime_service_internal.h"] += (
            "\nstruct user_runtime_server_context {\n"
            + "\n".join(ROLE_FIELDS[name] for name in ROLE_MEMBERS["run_server"]) + "\n};\n"
        )
        moving = tuple(item for item in SYMBOL_OWNER_MIGRATION if item[2] != "run_server")
        with patch(f"{__name__}.SYMBOL_OWNER_MIGRATION", moving):
            self.assertEqual(
                META_BODY_HASHES["run_server"],
                compact_hash(helper_body(texts, old, "run_server")),
            )
            self.assertEqual(META_BODY_HASHES["run_server"], compact_hash(expanded_helper_body(texts, old, "run_server")))

    def test_new_runtime_context_layout_is_private_but_every_member_type_is_exact(self) -> None:
        texts = self.runtime_fixture()
        header = "kernel/user_runtime_test_internal.h"
        texts[header] = texts[header].replace("\n".join(KERNEL_FIELDS), "\n".join(reversed(KERNEL_FIELDS)))
        self.assertEqual(
            META_BODY_HASHES["find_page"],
            compact_hash(helper_body(texts, "kernel/user_runtime_test.c", "find_page")),
        )

    def test_automatic_role_member_layout_is_not_an_unreviewed_image_abi(self) -> None:
        texts = self.role_fixture()
        header = "tests/qemu/user_runtime_service_internal.h"
        fields = tuple(ROLE_FIELDS[name] for name in ROLE_MEMBERS["run_client"])
        texts[header] = texts[header].replace("\n".join(fields), "\n".join(reversed(fields)))
        moving = tuple(item for item in SYMBOL_OWNER_MIGRATION if item[2] != "run_client")
        with patch(f"{__name__}.SYMBOL_OWNER_MIGRATION", moving):
            self.assertEqual(
                META_BODY_HASHES["run_client"],
                compact_hash(helper_body(texts, "tests/qemu/user_runtime_service.c", "run_client")),
            )

    def test_role_terminal_phase_keeps_the_same_automatic_context_and_return_codes(self) -> None:
        texts = self.role_fixture()
        path = "tests/qemu/user_runtime_client.c"
        name = "user_runtime_service_run_client"
        body = c_function_body(texts[path], name)
        start = body.rindex("    if (\n        micros_runtime_receive(MICROS_ENDPOINT_ANY")
        phase = body[start:-1].replace("role.", "role->")
        texts[path] = self.replace_function(
            texts[path], name,
            "uint64_t user_runtime_service_run_client(void)\n"
            + body[:start] + "return user_runtime_service_client_terminal(&role);\n}\n",
        )
        texts[path] += (
            "\nstatic uint64_t user_runtime_service_client_terminal(struct user_runtime_client_context *role)\n{"
            + phase + "\n}\n"
        )
        texts[path] = texts[path].replace(
            "\nuint64_t user_runtime_service_run_client(void)",
            "\nstatic uint64_t user_runtime_service_client_terminal(struct user_runtime_client_context *role);\n"
            "uint64_t user_runtime_service_run_client(void)", 1,
        )
        moving = tuple(item for item in SYMBOL_OWNER_MIGRATION if item[2] != "run_client")
        with patch(f"{__name__}.SYMBOL_OWNER_MIGRATION", moving):
            projected = expanded_helper_body(texts, "tests/qemu/user_runtime_service.c", "run_client")
            self.assertEqual(META_BODY_HASHES["run_client"], compact_hash(projected))

    def test_role_context_may_initialize_the_same_canaries_with_immediate_assignments(self) -> None:
        texts = self.role_fixture()
        path = "tests/qemu/user_runtime_client.c"
        declaration = re.search(
            r"struct user_runtime_client_context role\s*=\s*\{[^{}]*\};", texts[path],
        ).group()
        assignments = "\n".join(f"role.{member} = {value};" for member, value in ROLE_INITIALIZERS.items())
        texts[path] = texts[path].replace(declaration, "struct user_runtime_client_context role;\n" + assignments, 1)
        moving = tuple(item for item in SYMBOL_OWNER_MIGRATION if item[2] != "run_client")
        with patch(f"{__name__}.SYMBOL_OWNER_MIGRATION", moving):
            self.assertEqual(
                META_BODY_HASHES["run_client"],
                compact_hash(helper_body(texts, "tests/qemu/user_runtime_service.c", "run_client")),
            )

    def test_automatic_role_transport_cannot_erase_a_second_copy_of_the_same_values(self) -> None:
        texts = self.role_fixture()
        path = "tests/qemu/user_runtime_client.c"
        declaration = re.search(
            r"struct user_runtime_client_context role\s*=\s*\{[^{}]*\};", texts[path],
        ).group()
        texts[path] = texts[path].replace(declaration, declaration + "\n" + declaration.replace("role =", "second ="), 1)
        moving = tuple(item for item in SYMBOL_OWNER_MIGRATION if item[2] != "run_client")
        with patch(f"{__name__}.SYMBOL_OWNER_MIGRATION", moving):
            with self.assertRaisesRegex(AssertionError, "duplicated automatic"):
                helper_body(texts, "tests/qemu/user_runtime_service.c", "run_client")

    def assert_runtime_rejected(self, texts: dict[str, str]) -> None:
        try:
            body = helper_body(texts, "kernel/user_runtime_test.c", "find_page")
        except AssertionError:
            return
        self.assertNotEqual(META_BODY_HASHES["find_page"], compact_hash(body))

    def assert_syscall_rejected(self, texts: dict[str, str]) -> None:
        try:
            body = expanded_helper_body(texts, "kernel/syscall.c", "micros_syscall_handle_user_ecall")
        except AssertionError:
            return
        self.assertNotEqual(
            (
                META_SYSCALL_FLOW_HASH,
                META_SYSCALL_ARGUMENT_HASH,
                META_SYSCALL_EFFECT_HASH,
            ),
            (flow_hash(flow_events(body)), flow_hash(call_arguments(body)), flow_hash(state_effects(body))),
        )

    def test_runtime_projection_rejects_wrong_members_operators_schema_and_owners(self) -> None:
        owner = "kernel/user_runtime_test_image.c"
        header = "kernel/user_runtime_test_internal.h"
        state = "kernel/user_runtime_test_state.c"
        for path, before, after in (
            (owner, "runtime->images[", "runtime->processes["),
            (owner, "runtime->images[", "runtime.images["),
            (owner, "runtime->images[", "runtime->hidden["),
            (header, "size_t returned_actor;", "uint32_t returned_actor;"),
            (header, "USER_RUNTIME_MAX_IMAGE_PAGES][MICROS_SV39_PAGE_SIZE]", "2][MICROS_SV39_PAGE_SIZE]"),
            (state, "return &runtime_state;", "++runtime_state.returned_actor; return &runtime_state;"),
            (state, "return &runtime_state;", "return &foreign_state;"),
        ):
            with self.subTest(path=path, mutation=after):
                texts = self.runtime_fixture()
                self.assertIn(before, texts[path])
                texts[path] = texts[path].replace(before, after, 1)
                self.assert_runtime_rejected(texts)
        texts = self.runtime_fixture()
        texts[state] += "\nstatic struct user_runtime_test_context second = { .returned_actor = USER_RUNTIME_PROCESS_COUNT, };\n"
        self.assert_runtime_rejected(texts)

    def test_syscall_projection_rejects_predicate_guard_type_order_and_duplicate_call_drift(self) -> None:
        for path, before, after in (
            ("kernel/syscall_decode.c", "arguments->a7 == MICROS_SYSCALL_ABI_PM_CONTROL",
             "arguments->a7 != MICROS_SYSCALL_ABI_PM_CONTROL"),
            ("kernel/syscall.c", "if (!syscall_decode_early(", "if (syscall_decode_early("),
            ("kernel/syscall.c", "frame, &arguments, &bootstrap_request", "frame, &bootstrap_request, &bootstrap_request"),
            ("kernel/syscall.c", "syscall_resolve_context(hart, frame, &arguments, &context);",
             "syscall_resolve_context(hart, frame, &arguments, &context);\n"
             "syscall_resolve_context(hart, frame, &arguments, &context);"),
            ("kernel/syscall_context.c", "context->process = thread->owner;", "context->process = process->parent;"),
            ("kernel/syscall_decode.c", "return false;", "return false || hidden_work();"),
        ):
            with self.subTest(path=path, mutation=after):
                texts = self.syscall_fixture()
                self.assertIn(before, texts[path])
                texts[path] = texts[path].replace(before, after, 1)
                self.assert_syscall_rejected(texts)
        texts = self.syscall_fixture()
        statement = "syscall_resolve_context(hart, frame, &arguments, &context);"
        texts["kernel/syscall.c"] = texts["kernel/syscall.c"].replace(statement, "", 1)
        texts["kernel/syscall.c"] = texts["kernel/syscall.c"].replace(
            "if (!syscall_decode_early(", statement + "\nif (!syscall_decode_early(", 1,
        )
        self.assert_syscall_rejected(texts)

    def test_transport_cannot_hide_calls_static_request_storage_or_nonresult_register_writes(self) -> None:
        texts = self.syscall_fixture()
        texts["kernel/syscall_context.c"] += "\nstatic struct micros_syscall_arguments hidden;\n"
        with self.assertRaisesRegex(AssertionError, "additional/duplicated"):
            check_storage(texts)
        for mutation in ("frame->a1 = 0;", "hidden_work();"):
            texts = self.syscall_fixture()
            texts["kernel/syscall_context.c"] = texts["kernel/syscall_context.c"].replace(
                "context->process = thread->owner;", mutation + "\ncontext->process = thread->owner;", 1,
            )
            self.assert_syscall_rejected(texts)

    def test_role_projection_rejects_volatile_lifetime_initializer_and_predicate_drift(self) -> None:
        header = "tests/qemu/user_runtime_service_internal.h"
        owner = "tests/qemu/user_runtime_client.c"
        moving = tuple(item for item in SYMBOL_OWNER_MIGRATION if item[2] != "run_client")
        for path, before, after in (
            (header, "volatile uint64_t canary_before;", "uint64_t canary_before;"),
            (owner, "struct user_runtime_client_context role =", "static struct user_runtime_client_context role ="),
            (owner, ".canary_before = MICROS_USER_RUNTIME_TEST_CANARY_BEFORE", ".canary_before = MICROS_USER_RUNTIME_TEST_CANARY_AFTER"),
            (owner, "role.canary_before !=", "role.canary_before =="),
            (owner, "role.canary_before !=", "role.canary_after !="),
        ):
            with self.subTest(path=path, mutation=after):
                texts = self.role_fixture()
                self.assertIn(before, texts[path])
                texts[path] = texts[path].replace(before, after, 1)
                with patch(f"{__name__}.SYMBOL_OWNER_MIGRATION", moving):
                    try:
                        body = helper_body(texts, "tests/qemu/user_runtime_service.c", "run_client")
                    except AssertionError:
                        continue
                    self.assertNotEqual(
                        META_BODY_HASHES["run_client"],
                        compact_hash(body),
                    )
        texts = self.role_fixture()
        texts[owner] += "\nstatic struct user_runtime_client_context retained;\n"
        with patch(f"{__name__}.SYMBOL_OWNER_MIGRATION", moving):
            with self.assertRaisesRegex(AssertionError, "lifetime"):
                helper_body(texts, "tests/qemu/user_runtime_service.c", "run_client")

    def test_pending_helpers_are_not_allowed_to_use_completed_context_normalization(self) -> None:
        texts = self.runtime_fixture()
        body = c_function_body(texts["kernel/user_runtime_test_image.c"], "user_runtime_test_find_page")
        texts["kernel/user_runtime_test.c"] += (
            "\nstatic struct mapped_page *find_page(size_t actor, uint64_t virtual_address)\n" + body
        )
        texts["kernel/user_runtime_test_image.c"] = ""
        case = self.layout(texts)
        with patch(f"{__name__}.owner_specs", return_value=((
            "kernel/user_runtime_test.c", "kernel/user_runtime_test_image.c",
            "find_page", "user_runtime_test_find_page",
        ),)):
            with self.assertRaises(AssertionError):
                case.test_pending_symbols_and_helpers_allow_only_the_exact_unchanged_old_body()

    def test_generator_public_signatures_and_pending_private_body_are_still_exact(self) -> None:
        original = META_GENERATOR
        with patch(f"{__name__}.GENERATOR_BODY_HASHES", META_GENERATOR_HASHES), \
                patch(f"{__name__}.read_source", side_effect=lambda path: original if path == GENERATOR_PATH else read_source_original(path)):
            TrapSyscallSourceLayoutTest().test_generator_cli_symbols_fields_and_existing_helper_bodies_are_preserved()
        for before, after in (
            ("def render_fixture(image):", "def render_fixture(image, extra=None):"),
            ('parser.add_argument("--elf"', 'parser.add_argument("--input"'),
            ("def containing_load(symbol_name, size):", "def containing_load(symbol_name, size=0):"),
        ):
            with self.subTest(mutation=after):
                changed = original.replace(before, after, 1)
                with patch(f"{__name__}.GENERATOR_BODY_HASHES", META_GENERATOR_HASHES), \
                        patch(f"{__name__}.read_source", side_effect=lambda path: changed if path == GENERATOR_PATH else read_source_original(path)):
                    with self.assertRaises(AssertionError):
                        TrapSyscallSourceLayoutTest().test_generator_cli_symbols_fields_and_existing_helper_bodies_are_preserved()

    def test_generator_private_lookup_may_transport_symbols_and_loads_explicitly(self) -> None:
        text = META_GENERATOR
        start = text.index("    def containing_load(")
        end = text.index("    config_load =", start)
        helper = text[start:end]
        helper = "\n".join(line[4:] if line.startswith("    ") else line for line in helper.splitlines())
        helper = helper.replace("def containing_load(symbol_name, size):", "def containing_load(symbols, loads, symbol_name, size):")
        rendered = text[:start] + text[end:]
        rendered = rendered.replace("containing_load(", "containing_load(symbols, loads, ")
        rendered = rendered.replace("\ndef render_fixture(", "\n" + helper + "\n\ndef render_fixture(", 1)
        pending = tuple(item for item in LONG_FUNCTION_MIGRATION if item != (GENERATOR_PATH, "render_fixture"))
        with patch(f"{__name__}.GENERATOR_BODY_HASHES", META_GENERATOR_HASHES), \
                patch(f"{__name__}.LONG_FUNCTION_MIGRATION", pending), \
                patch(f"{__name__}.read_source", side_effect=lambda path: rendered if path == GENERATOR_PATH else read_source_original(path)):
            TrapSyscallSourceLayoutTest().test_generator_cli_symbols_fields_and_existing_helper_bodies_are_preserved()


class TrapSyscallMetaFixtureLifecycleTest(unittest.TestCase):
    def assert_isolated_state(self, symbols: tuple, longs: tuple) -> None:
        reads = []
        fixture_paths = {*all_sources(), *PRIVATE_HEADERS, GENERATOR_PATH}

        def extracted_source(path: str) -> str:
            if path in fixture_paths:
                reads.append(path)
                return "/* completed responsibility owner: no original monolith */\n"
            return read_source_original(path)

        cases = (
            TrapSyscallFailClosedMutationTest("test_pending_migration_cannot_accept_a_premature_move_or_disappear"),
            TrapSyscallFailClosedMutationTest("test_context_and_image_storage_reject_partial_migration_arrays_and_initializers"),
            TrapSyscallFailClosedMutationTest("test_native_argument_shape_negative_loop_and_protocol_order_changes_are_detected"),
            TrapSyscallValidExtractionTest("test_named_syscall_phases_preserve_early_decode_context_dispatch_and_projection"),
            TrapSyscallValidExtractionTest("test_moved_short_helper_accepts_the_exact_typed_runtime_borrow"),
            TrapSyscallValidExtractionTest("test_role_terminal_phase_keeps_the_same_automatic_context_and_return_codes"),
            TrapSyscallValidExtractionTest("test_server_automatic_context_preserves_original_uninitialized_locals"),
            TrapSyscallValidExtractionTest("test_generator_public_signatures_and_pending_private_body_are_still_exact"),
            TrapSyscallValidExtractionTest("test_generator_private_lookup_may_transport_symbols_and_loads_explicitly"),
        )
        with patch(f"{__name__}.SYMBOL_OWNER_MIGRATION", symbols), \
                patch(f"{__name__}.LONG_FUNCTION_MIGRATION", longs), \
                patch(f"{__name__}.read_source", side_effect=extracted_source):
            result = unittest.TestResult()
            unittest.TestSuite(cases).run(result)
            self.assertEqual([], result.errors + result.failures)
            self.assertEqual(9, result.testsRun)
            self.assertEqual([], result.skipped)
            self.assertEqual((symbols, longs), (SYMBOL_OWNER_MIGRATION, LONG_FUNCTION_MIGRATION))
        self.assertEqual([], reads, "meta fixtures must not reconstruct pending sources from live owners")

    def test_meta_fixtures_are_isolated_from_the_extracted_syscall_state(self) -> None:
        self.assert_isolated_state(
            INITIAL_SYMBOL_OWNER_MIGRATION,
            tuple(entry for entry in INITIAL_LONG_FUNCTION_MIGRATION
                  if entry != ("kernel/syscall.c", "micros_syscall_handle_user_ecall")),
        )

    def test_meta_fixtures_are_isolated_from_eventually_final_migrations(self) -> None:
        self.assert_isolated_state((), ())


read_source_original = read_source


if __name__ == "__main__":
    unittest.main()
