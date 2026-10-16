"""Source contracts for the independently reviewed QEMU runner correction."""

import ast
import contextlib
import copy
import hashlib
import io
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
REVIEWED_BASE = "934faa9fd0e9e406758c0f2e336fb28af57739db"
RUNNER_PATH = "tools/run_qemu_smoke.py"
TEST_PATH = "tests/host/test_run_qemu_smoke.py"
STRUCTURAL_TEST_PATH = "tests/host/test_qemu_runner_source_layout.py"
ORIGINAL_RUNNER_DIGEST = "9e54f744074d077d05b52880495b73f7c74ae1db0a6ad751f8b7024688bf79e3"
ORIGINAL_RUNNER_AST = "030d1e7a6d45a4ee368313b7d2abfe2ed976ec83b42af73f9e96a12133c1d8ef"
ORIGINAL_TEST_AST = "1035ef3996abb56e2d51f4a028ec3bb060c443b6988a4e602cf358d00a83d20c"
PRODUCTION_PATHS = (
    RUNNER_PATH, "tools/qemu_smoke_records.py", "tools/qemu_smoke_platform.py",
    "tools/qemu_smoke_objects.py", "tools/qemu_smoke_faults.py",
    "tools/qemu_smoke_command.py", "tools/qemu_smoke_execution.py",
    "tools/qemu_smoke_outcome.py", "tools/qemu_smoke_arguments.py", "tools/qemu_smoke_cli.py",
)
TEST_PATHS = (
    TEST_PATH, "tests/host/qemu_smoke_test_fixture.py",
    "tests/host/qemu_smoke_classification_cases.py", "tests/host/qemu_smoke_panic_cases.py",
    "tests/host/qemu_smoke_platform_cases.py", "tests/host/qemu_smoke_memory_cases.py",
    "tests/host/qemu_smoke_object_cases.py", "tests/host/qemu_smoke_execution_cases.py",
)
TEST_CASE_LAYOUT = {
    2: (("SmokeClassificationCases", "SmokeClassificationTest", tuple(range(12))),),
    3: (("PanicReportCases", "ExpectedOutcomeTest", (*range(9), *range(59, 64))),),
    4: (("PlatformReportCases", "ExpectedOutcomeTest", tuple(range(9, 31))),),
    5: (("MemoryReportCases", "ExpectedOutcomeTest", tuple(range(31, 46))),),
    6: (("ObjectReportCases", "ExpectedOutcomeTest", tuple(range(46, 59))),),
    7: (
        ("NmSymbolCases", "NmSymbolTest", tuple(range(2))),
        ("QemuCommandCases", "QemuCommandTest", tuple(range(2))),
        ("QemuSerialInputCases", "QemuSerialInputTest", tuple(range(4))),
        ("TapOutputCases", "TapOutputTest", (0,)),
    ),
}
AGGREGATE_MIXINS = {
    "SmokeClassificationTest": ("SmokeClassificationCases",),
    "ExpectedOutcomeTest": ("PanicReportCases", "PlatformReportCases", "MemoryReportCases", "ObjectReportCases"),
    "NmSymbolTest": ("NmSymbolCases",),
    "QemuCommandTest": ("QemuCommandCases",),
    "QemuSerialInputTest": ("QemuSerialInputCases",),
    "TapOutputTest": ("TapOutputCases",),
}
ORIGINAL_TEST_METHOD_RAW = {'ExpectedOutcomeTest.test_accepts_complete_clean_panic': '9d8fd0ce5d831be5566d803d6f98efc24ec93201430bcb026b2677246c08ced5',
 'ExpectedOutcomeTest.test_accepts_complete_endpoint_test_report': '464c6fa2c97fa62e35897661e3db9b00ef03c185bcbadbdf9c697104ef992ea7',
 'ExpectedOutcomeTest.test_accepts_complete_frame_allocator_ready_report': '0384456bbf6652497a77e1e8c8b69635e1ad7db15b199d475c40fa4e02b9d8a3',
 'ExpectedOutcomeTest.test_accepts_complete_frame_allocator_test_report': '10780b0b5fb99c947b0a6c5b8d316cef32683e4378c15f438d8179abc0ef0dab',
 'ExpectedOutcomeTest.test_accepts_complete_frame_ownership_ready_report': '725fa8c7be9dca6482c211a2015a1b8701d32fc2b521bafc828bdbf186323aa1',
 'ExpectedOutcomeTest.test_accepts_complete_frame_ownership_test_report': '9e003029789131e5f9a45cead112697f4f7f0bb7a21fe9250fdded1954e11a3e',
 'ExpectedOutcomeTest.test_accepts_complete_ipc_test_report': 'b20a9602199e07074f1b0e801385fd81908373a1522c749b216f10bb7e37fd81',
 'ExpectedOutcomeTest.test_accepts_complete_mmu_ready_report': '26f559517d0a83c032d106164a3cfcb5f3c52ba63fb2564c497c2eeeccf85176',
 'ExpectedOutcomeTest.test_accepts_complete_mmu_test_report': '6dac36a77cfbac0ad9d2dc124ec41fddabe4ca85a3f4d4d70f1b572a9ac7e69d',
 'ExpectedOutcomeTest.test_accepts_complete_nested_trap_test_report': '590ed26b3e2752b3bce6664722c4f6699b1bd899d8fa3deedb6c91b09b6a1a3f',
 'ExpectedOutcomeTest.test_accepts_complete_object_model_test_report': 'ad704773a685de696bd9430cc5ef29836fe83b58cb484e7286393aba48918ed5',
 'ExpectedOutcomeTest.test_accepts_complete_objects_ready_report': 'ccc94de1944dbe2ab39f63ff9993363b3dd0f1677754593d8927eaf26649ea2f',
 'ExpectedOutcomeTest.test_accepts_complete_scheduler_test_report': 'e69a468c89cc25a5c48d89d82822b532aa6d11a0b974d711134e61912269c10f',
 'ExpectedOutcomeTest.test_accepts_complete_timer_test_report': '8baf773e31f508f859ea08d5d7ea1951049e89aec2d585ec460f2d85b145bfcb',
 'ExpectedOutcomeTest.test_accepts_complete_trap_panic_context': '5780d31ffa43b45bb9d7e49238d00ca08b737d0a3376c7d68e5455d84d1e2eb0',
 'ExpectedOutcomeTest.test_accepts_complete_trap_recovery': '738b8e91bdee00c47761e49edcd8e7407210f30030a658ca0f7ed6fee4ed304f',
 'ExpectedOutcomeTest.test_accepts_complete_user_address_space_test_report': 'a799204860023c9c18be423908c416bd1c6f21e600f2aabbb6cd3fa02f698646',
 'ExpectedOutcomeTest.test_accepts_complete_user_execution_test_report': '7246f113b4935ad1311acd04ed0b10214f53daad291aa2fbb6b44245b109f754',
 'ExpectedOutcomeTest.test_ignores_unrelated_target_prefix_before_allocator_ready': '815fd5f0101d4ee2c0e71d6db6ad9657b48af60e6726e412a2e205d7483afc08',
 'ExpectedOutcomeTest.test_rejects_duplicate_or_early_frame_allocator_ready': 'dba2e7251ed00fc5828304fa6c791324870c7476da43a4c5e0d2e802dc4afe3a',
 'ExpectedOutcomeTest.test_rejects_duplicate_or_early_mmu_ready_report': 'd816eb615b807fc845d09bae85a860c12f94dd0152d7bb39c3d3b62fa0595a64',
 'ExpectedOutcomeTest.test_rejects_duplicate_or_out_of_order_panic_records': '63765aef49edd1d19f63e7ce009095f1fdb917e3ff29634ef8d350706bd0e112',
 'ExpectedOutcomeTest.test_rejects_duplicate_timer_test_report': 'c6e93d925b151d9f78c66ece28a8793b8898bee60bc04c743276f2aafb8a7a5a',
 'ExpectedOutcomeTest.test_rejects_duplicate_trap_panic_context': 'd8855332e7c73a919582f12989c0f78a97980f0c904ec485e86baae41959e67c',
 'ExpectedOutcomeTest.test_rejects_duplicate_trap_recovery_pass': '247c8c984c7f6e64b563a2fd324c1f3748a2aabfb496cd49c621ad54a7db638a',
 'ExpectedOutcomeTest.test_rejects_early_or_malformed_frame_allocator_test_report': '7aa5c1d09c964ac1e37fe176e640c8a68e4dee287e5c91b76cea7403cd6739c3',
 'ExpectedOutcomeTest.test_rejects_forbidden_marker': 'ce4e610435723029860beb49ca484a8e2bc7fac85eadd17e4621c07a41b2410d',
 'ExpectedOutcomeTest.test_rejects_forbidden_pattern': '453360a7978e875e44f683c736ee7d09f1ca865161077b73046b4be49ae0ab6a',
 'ExpectedOutcomeTest.test_rejects_frame_allocator_ready_after_target_outcome': 'bd78608cd9e72dbf38c12f7de3f30f11ab34f8f9630d3c8c1f87f9a31570f71a',
 'ExpectedOutcomeTest.test_rejects_frame_ownership_ready_after_target_outcome': '3eaaa92d5758c2ce60aeb2ff9589d25d1da9877751b7071208e31d3b67c161a1',
 'ExpectedOutcomeTest.test_rejects_invalid_endpoint_test_report': '518075b4ac66924de117057050535553db5acd3ae8520fa525347314ee7fc089',
 'ExpectedOutcomeTest.test_rejects_invalid_frame_ownership_ready_report': '8690c7b56a4f2d8b28f4e1fb1b44d7d8f85f9511c59853a6a9a22af1ba2021ec',
 'ExpectedOutcomeTest.test_rejects_invalid_frame_ownership_test_report': '2502674bb4050ef162c0c17ea57429a01b9d43c52d1ed612c5ad66a1b6d86ae2',
 'ExpectedOutcomeTest.test_rejects_invalid_ipc_test_report': '7984de3c4ca11e747140a22a757072cba66d5a4cecd0867e1232d1e5222963f4',
 'ExpectedOutcomeTest.test_rejects_invalid_mmu_ready_report': 'e815a87c3041287fd5214962392fb6d754be4d39f9f63b629c15027a678c8c5a',
 'ExpectedOutcomeTest.test_rejects_invalid_mmu_test_report': 'f0edeffccd132b73f7048ace4e59229892fd4b2470c936acb1b7d176839c2488',
 'ExpectedOutcomeTest.test_rejects_invalid_nested_trap_test_report': 'e471c679b43e8f34230bc4de87e1ecab1cf5ec7831144d40be8f6788c100622b',
 'ExpectedOutcomeTest.test_rejects_invalid_object_model_test_report': '374298b2dd40b240009db55ef172d53e02e76a9997b3b955a129bda0843b06fb',
 'ExpectedOutcomeTest.test_rejects_invalid_objects_ready_report': '4677d55ec451e31f483cc60896f71e408fa54421ca1686129188239bb0dbfc9c',
 'ExpectedOutcomeTest.test_rejects_invalid_scheduler_test_report': 'f4d08c75e24f9e0036738052f40a852a03de9f9ca79b0a4d9b53b258000adb40',
 'ExpectedOutcomeTest.test_rejects_invalid_user_address_space_test_report': '8225fe58490c18b5980f881d3a0c6d4ad6fd5faf05a34c9fc0cc8e14f86b1c99',
 'ExpectedOutcomeTest.test_rejects_invalid_user_execution_test_report': 'dc44e257bc0fb0c8e4f3a4fd794fc2038682bda5a7a028c4d655829c18a65abe',
 'ExpectedOutcomeTest.test_rejects_malformed_or_unterminated_frame_allocator_ready': 'b8397494ceabc402f0b2687c7b27cfc1fa425a2c053bbc12fc555a29566fb069',
 'ExpectedOutcomeTest.test_rejects_malformed_timer_interval': '783144910d13d3a85c0441f25aa99bd0914b8ee69ade00ebab03c562e15248b6',
 'ExpectedOutcomeTest.test_rejects_malformed_timer_tick_count': 'e0a2616e105810ff5b3577cd38ecb9db957fb839c85c0cde56c11d4e7c18a3b5',
 'ExpectedOutcomeTest.test_rejects_missing_or_duplicate_frame_allocator_test_report': '18b4dab344826483ab3c1d572b1dc782fbf2bc0028b711b4447d90dc265fdb65',
 'ExpectedOutcomeTest.test_rejects_missing_timer_test_report': '74d7a01fd230ac4c967227bd5ff3540f34c4990be373f3c701640421c6c5bbc8',
 'ExpectedOutcomeTest.test_rejects_mmu_ready_after_target_outcome': '982610a28e93a9659e42343a3b5106b5ae5f62b3c3c52d2dd0613dc2754a5faa',
 'ExpectedOutcomeTest.test_rejects_out_of_order_trap_recovery_pass': '2d31c31754580dc91bc847e6369ba2547496fe5cf4225a23d08d90e8365b935b',
 'ExpectedOutcomeTest.test_rejects_panic_that_times_out': '2870f671a8add21d06df7e7fb537e49369f27f77d9a278b804dbbc6efa9b8925',
 'ExpectedOutcomeTest.test_rejects_panic_with_explicit_failure': 'ae2b685e88b68a9d4958187ebe374234b53f33a44de8145abb3a2de863ea1a2a',
 'ExpectedOutcomeTest.test_rejects_timer_test_report_before_fdt_ready': '0fb675e1c7841716de4dd559d07050732264b4b909033ecf3eb3a114ac03e81e',
 'ExpectedOutcomeTest.test_rejects_trap_context_for_direct_panic': 'db5e0e052203ebdc33533253024c89147f49753d0cd14e22298b5a56afef9213',
 'ExpectedOutcomeTest.test_rejects_trap_pass_before_fdt_ready': '094699eaf895f49cff20593d907a59141371b91ae8033003a2de1eeff2099954',
 'ExpectedOutcomeTest.test_rejects_unterminated_final_panic_record': '56346da112f99195a628977d0dcf5bdcc201647b7df8fd4c7e4b092d2918facb',
 'ExpectedOutcomeTest.test_rejects_unterminated_frame_allocator_test_report': '4df18e25ddf7a421c12e46ad86ba7006f5ddf6736b1fa4be6b994e5154caa0e7',
 'ExpectedOutcomeTest.test_rejects_unterminated_timer_test_report': 'c7188e6de24307c061b157ee08720750cf8187463b273ba862b620d860dabed4',
 'ExpectedOutcomeTest.test_rejects_unterminated_trap_panic_context': '1d01ddbe1e009c1b4dbd2440bd451db1bdb93ac38631ed96e1e144aaef9ae72f',
 'ExpectedOutcomeTest.test_rejects_wrong_trap_context_sepc': 'f5fff8b0de65549843f85cc306b544bd2813543c66f01da8544a38376591e158',
 'ExpectedOutcomeTest.test_rejects_zero_or_mismatched_frame_allocator_counts': 'b994825f36c14a227fd5429a9818e090166f671660db1c0ed7133b7595fa982f',
 'ExpectedOutcomeTest.test_requires_exact_invalid_scheduler_context_report': 'af621cc55d03ef5351269ba3ae0db6f2e753027bdf975460cae40627b56c399b',
 'ExpectedOutcomeTest.test_requires_exact_vm_self_fault_and_context': 'ee48fa2b3d4505f760a8a621a9e7b6249c5460978b33a9b2946548148c284074',
 'ExpectedOutcomeTest.test_requires_expected_frame_allocator_growth': '919b70a41f45d61ed93ba8285b7f4bf819aa697126b81cbcbab3e088d600d4ea',
 'ExpectedOutcomeTest.test_requires_patterns_in_order': '6f0231013dea411aff52b2b027f92ded5682140dbf42b151ca57be17f85bc435',
 'NmSymbolTest.test_parses_exact_defined_symbol': '31ed5e6373f20ba9736da9d97cd3eaf9eb5205b255f4de9abe04187755841dee',
 'NmSymbolTest.test_rejects_duplicate_defined_symbol': 'e027b6618f2fa27c541fa434674e02f4780594d7e2866cd8ebfc85c61b374d0b',
 'QemuCommandTest.test_accepts_explicit_guest_memory_size': '8e019d4217dbb5bbf38bb76ee3f0c38ecfeb9bfcd1a2295d6991e731afea41cf',
 'QemuCommandTest.test_uses_explicit_single_hart_plic_machine': '1ac3c23f9324e96302b30eb5e85cb05f44dd2553eb7cc292a2ad54f72b228eee',
 'QemuSerialInputTest.test_closed_serial_input_pipe_returns_a_normal_result': '2c606d7317e4a9f15abc4b75890e3bc483a5918c1d2873e4ae1a151d9dcf6679',
 'QemuSerialInputTest.test_default_execution_keeps_stdin_closed': 'cee66d32839541226158242ea131fa7197a173f426f4796448ba5e7442b900c9',
 'QemuSerialInputTest.test_trigger_requires_an_exact_terminated_line': 'd61b6250cf4f2f38ede6d8caa7c77ae88a229896f7f42b76df0f34ae307947df',
 'QemuSerialInputTest.test_writes_serial_input_once_after_complete_trigger_line': '84430c555be2e7c306298f195037a5ac0cb72ab6cf6954c8bafeec5d1b680dde',
 'SmokeClassificationTest.test_accepts_complete_fdt_event_stream': 'f6e5cd4a58d0f99ea7d1f577b227160ead7db91a6fe5c710562b61ab826e3afa',
 'SmokeClassificationTest.test_accepts_marker_followed_by_clean_exit': 'ce5a383e04c110014dc77c5a67af9e6f5fd932b6f8f392541a18183f61771972',
 'SmokeClassificationTest.test_rejects_empty_fdt_reservation_result': '5932233cf098cb95bffdde82af88c9d44098bd253fe04fb1b82a16f8de886103',
 'SmokeClassificationTest.test_rejects_incomplete_fdt_event_stream': '0d031936498ac3192f073279cc8553d01ae2fc8436321e6892fcd43fc6ba5be3',
 'SmokeClassificationTest.test_rejects_malformed_duplicate_fdt_count_summary': '5b91a69557e74e8d7cbc8ec97de7d503f9b2174aaa937c56aa649dbe1d2ea778',
 'SmokeClassificationTest.test_rejects_malformed_fdt_range_event': '62918c994064d0b0d8f4ac62cd100d8b5561c00715a66d96ebffa7c8f2f871f6',
 'SmokeClassificationTest.test_rejects_marker_embedded_in_another_line': 'eab27ab7b3568c53646ccb229cb0851f089be2350504e98221bf8bd215d12af0',
 'SmokeClassificationTest.test_reports_explicit_target_failure': '986f2a9d7bfe6804ed8a05ec185cd36ebbc5db72f6dd25870b088f046f78bed1',
 'SmokeClassificationTest.test_reports_kernel_panic': '5190344d55e12325cd142a88585f52ddb0adbc8a90df8a313adaa44d5e7137d3',
 'SmokeClassificationTest.test_reports_timeout_before_other_output_states': 'e93f8d546a71fac87903bb6c9e48f5bbdf054ec7bbcdd02eefb59899518b8648',
 'SmokeClassificationTest.test_reports_unexpected_exit_without_marker': 'c05a835f5b20f6c455a679c1f336944e284d959ab080f6b69679f17934653d9e',
 'SmokeClassificationTest.test_requires_every_expected_marker': '3e255b1420ac5a1c625ab01f9bdfe79e0de5cf90c3a1fc5773b9620a42325506',
 'TapOutputTest.test_pass_includes_machine_readable_outcome': '644d5d6b6491c7d741267ebe86ed3d6bce6fdf3765ca83230fd0f0326b083603'}
ORIGINAL_TEST_DATA_RAW = {'ENDPOINT_TEST_OUTPUT': '939b303a0e6a8639b0371dcb3324b350f6acf37d32049225ef95fe8cf06813e6',
 'ENDPOINT_TEST_PASS': '2cedd0527ce74d4aefeda0c7136c04caf4b117f989fd45fbce59c36a0f96cd78',
 'FRAME_ALLOCATOR_OUTPUT': 'd76d359f78267052b8c3eaa96a8dd55da412e86b185358b95bf37aa5beaaacf7',
 'FRAME_ALLOCATOR_READY_RECORD': 'ee6d68a3900270216b348729b1ca5bd4b31506f9dc10bdd7b3aa9f4ae2c046a9',
 'FRAME_ALLOCATOR_TEST_OUTPUT': 'a6db5c6f0601aac940f1c9888ea513319ac1de181539ca5180aadfed48330e70',
 'FRAME_ALLOCATOR_TEST_PASS': 'b80c54dac7570fe7ee901f5c708087abc0f1ca45313dc3720cf472be963b4b39',
 'FRAME_OWNERSHIP_OUTPUT': 'aa41a96217f00170c68b3180416fcfaaa8201f18fce9b1f4d1f96256ec3a46f7',
 'FRAME_OWNERSHIP_READY_RECORD': 'dfa91213c49cce5d5092cc8f6477c2d38d8c770f8be8e31f545f118cba650fd5',
 'FRAME_OWNERSHIP_TEST_OUTPUT': '2599e6547f361893550cad4342d638fc7ec05b249cec687a982ecc83e7bd91d6',
 'FRAME_OWNERSHIP_TEST_PASS': '8976588f0216408037d861e3025891a9a686fc3b63e491aac47c0b7edc30d293',
 'IPC_ADDRESS_SPACES_PASS': 'c12d02765c4edfdf11026bec4991144c976839203d0879f344a0ad8d65d8a229',
 'IPC_TEST_OUTPUT': '41ea8c34e49b1efb1b3096e9571725cfae3c981cddf593b4713a3085678d98e2',
 'IPC_TEST_PASS': 'aa44735aeccd02de6e74384013bdb7f48a8305f4d096dcf02514a6129f55ab89',
 'MMU_OUTPUT': '5c6f9c9521b6750e6fa0b72d9679189483ffd537ee025ae0e8964dbcfc9ca50e',
 'MMU_READY_RECORD': 'b9f6c6a6115fb87b68e8829f0cce43334752392a38151b8a5524f017f6004705',
 'MMU_TEST_OUTPUT': 'bf6a85f763bbdb1e0259601e0f18f84444c67236ef3d254a259e5ca80e914bcb',
 'MMU_TEST_PASS': '0a361de8d2142b693cdd7eb1eec463ee2085fc91f21ef10d78ea3eeedf9fd0c9',
 'NESTED_TRAP_TEST_OUTPUT': '745a0cd6f31e215c8b0adb85b893d1def0697467ace97d628bf39b90e14304e6',
 'NESTED_TRAP_TEST_PASS': '4593c1f6fff7a66caa977dc796e85b5bcd9380101f202b9e210d31ad68c63ba6',
 'OBJECTS_READY_RECORD': '3dc8a8a0431e75f6c02eaaaf6cef59cd39510a7f1f82f149b16d7ada29a85f17',
 'OBJECT_MODEL_TEST_OUTPUT': '322d844ec08f8d1e50d0ffc2e77942a35cec71ad0d94e153f7af36ae83b593ec',
 'OBJECT_MODEL_TEST_PASS': 'a89906486e5e8af87c193ff0d0704c321aaedfb3bf6a0cb3b1f6941d519802ee',
 'PANIC_OUTPUT': 'cd533d9fd7467dab1b4e9ce092a06cec38e6c271861a6140a9129f341b343cbc',
 'SCHEDULER_INVALID_CONTEXT_RECORD': 'd003b0f589bd4a7e82c4450047a82dbda5ed344def4bcd2560ab1e33d359aa54',
 'SCHEDULER_INVALID_DIAGNOSTIC': 'c9665692e07724cb2e65052d232ba198a01b5349a04c8aa7d7971452b4d2521e',
 'SCHEDULER_INVALID_OUTPUT': '428c76245fca97971b627831ef6dd1283417caae494750dd5f6fc667070eec94',
 'SCHEDULER_TEST_OUTPUT': '633bbbe9d0e31457609ed0da3a78bfbb24fb66201b5df978c333e7bbb37e3260',
 'SCHEDULER_TEST_PASS': '33c88824a5cfa7fe377d4120cdc4b03a7eeef844dc3fa379b15948d9e0c98a45',
 'TIMER_TEST_OUTPUT': '98289eae378b9ea98134e475f65c3b3402fc6cbed181d9df7d182b88443ae18e',
 'TRAP_CONTEXT_RECORD': 'b3d0841a9382b4df9ee01fc43a29fa8421ffafa68d4b374d031a64296b69cb8c',
 'TRAP_PANIC_OUTPUT': '095feb62a20fede62fed379a499a446cbf245447c25c3d151fac82e73ed2938b',
 'TRAP_RECOVERY_OUTPUT': '6665a4f247b996c609343396d66b93c6951f3cc6a32f36e86e4cdf1e5a1068ec',
 'USER_ADDRESS_SPACE_TEST_OUTPUT': 'a6916bc3361e42062ee37e4f42fc11476382eed7b62d8d326551360299cbc89e',
 'USER_ADDRESS_SPACE_TEST_PASS': '7099df6228459b6f42ea4b47e45d1d01dac567e5eb53b79c345acfb4ea750944',
 'USER_EXECUTION_TEST_OUTPUT': '5f449b1afb0e8e5f7434a7c84d35a8334baa63766d5307f0e826a44712d1180b',
 'USER_EXECUTION_TEST_PASS': '0607c704b6ad02dd96b6480656d379426aef2cf9c0c5b47329a34f061888df75'}
ORIGINAL_LONG_FUNCTIONS = (
    ("matches_expected_result", 217), ("parse_arguments", 330), ("main", 187),
)
FUNCTION_OWNERS = {
    1: ("_split_output_records",),
    2: (
        "_has_complete_fdt_events", "_has_complete_trap_ready", "_has_complete_trap_test_report",
        "_has_complete_timer_test_report", "parse_frame_allocator_ready_counts",
        "_has_complete_frame_allocator_ready", "_frame_allocator_ready_precedes_target_outcome",
        "has_expected_frame_allocator_growth", "_has_complete_frame_allocator_test_report",
        "parse_mmu_ready", "_has_complete_mmu_ready", "_mmu_ready_precedes_target_outcome",
        "_has_complete_mmu_test_report", "parse_frame_ownership_ready",
        "_has_complete_frame_ownership_ready", "_frame_ownership_ready_precedes_target_outcome",
        "_has_complete_frame_ownership_test_report",
    ),
    3: (
        "parse_objects_ready", "_has_complete_objects_ready", "_has_complete_user_address_space_test_report",
        "_has_complete_user_execution_test_report", "_has_complete_scheduler_test_report",
        "_has_complete_endpoint_test_report", "_has_complete_ipc_test_report",
        "_objects_ready_precedes_target_outcome", "_has_complete_object_model_test_report",
        "_has_complete_nested_trap_test_report",
    ),
    4: ("_has_complete_panic_report", "_has_complete_trap_context", "_has_vm_self_fault", "_has_complete_scheduler_invalid_context"),
    5: ("parse_nm_symbol_address", "resolve_symbol_address", "build_qemu_command"),
    6: ("_decode_output", "_serial_trigger_observed", "_run_qemu_with_serial_input", "run_qemu"),
    7: ("_has_required_output", "_has_ordered_patterns", "matches_expected_result", "classify_smoke", "print_tap_result"),
    8: ("parse_arguments",),
    9: ("main",),
}
OWNER_STDLIB_IMPORTS = {
    1: ("import enum", "import re", "from dataclasses import dataclass", "from typing import Optional"),
    2: (),
    3: (),
    4: ("from pathlib import Path",),
    5: ("import subprocess",),
    6: ("import os", "import selectors", "import subprocess", "import time"),
    7: ("import re", "import shlex"),
    8: ("import argparse", "import os", "import re", "from pathlib import Path"),
    9: ("import sys",),
}
OWNER_PRIVATE_DIRECTIONS = {
    1: set(), 2: {1}, 3: {1, 2}, 4: {1}, 5: set(),
    6: {1}, 7: {1, 2, 3, 4}, 8: set(), 9: {1, 2, 5, 6, 7, 8},
}
OUTLINE_PHASES = {
    "matches_expected_result": (
        ("_matches_foundation_reports", 2, 9, (
            "result", "output_lines", "require_fdt_events", "require_fdt_reservations",
            "require_panic_report", "require_trap_ready", "require_trap_test_report",
            "require_timer_test_report", "require_frame_allocator_ready", "require_frame_allocator_test_report",
        ), True),
        ("_matches_memory_reports", 9, 15, (
            "result", "require_mmu_ready", "require_mmu_test_report", "require_frame_ownership_ready",
            "require_frame_ownership_test_report", "require_user_address_space_test_report",
            "require_user_execution_test_report",
        ), True),
        ("_matches_object_reports", 15, 22, (
            "result", "require_scheduler_test_report", "require_endpoint_test_report", "require_ipc_test_report",
            "require_scheduler_invalid_context", "require_objects_ready", "require_object_model_test_report",
            "require_nested_trap_test_report",
        ), True),
        ("_matches_fault_reports", 22, 26, (
            "result", "output_lines", "require_trap_context", "expected_trap_context_sepc",
            "expected_trap_context_origin", "expected_trap_context_scause", "expected_trap_context_stval",
            "require_vm_self_fault", "expected_vm_self_fault_sepc", "expected_vm_self_fault_scause",
            "expected_vm_self_fault_stval", "expected_vm_self_fault_ownership",
        ), True),
    ),
    "parse_arguments": (
        ("_add_execution_arguments", 1, 14, ("parser",), False),
        ("_add_platform_report_arguments", 14, 28, ("parser",), False),
        ("_add_object_report_arguments", 28, 35, ("parser",), False),
        ("_add_fault_arguments", 35, 46, ("parser",), False),
        ("_validate_execution_arguments", 48, 58, ("parser", "arguments"), False),
        ("_validate_fault_arguments", 58, 62, ("parser", "arguments"), False),
    ),
}
OUTLINE_ROOT_PLANS = {
    "matches_expected_result": (0, 1, *(row[0] for row in OUTLINE_PHASES["matches_expected_result"]), 26),
    "parse_arguments": (
        0, *(row[0] for row in OUTLINE_PHASES["parse_arguments"][:4]), 46, 47,
        *(row[0] for row in OUTLINE_PHASES["parse_arguments"][4:]), 62,
    ),
}
MATCHING_CALL_ARGUMENTS = (
    "arguments", "result", "outcome", "expected_outcome",
    "expected_trap_context_sepc", "expected_vm_self_fault_sepc",
)
# Frozen from the original 27/63-statement bodies before outlining.
ORIGINAL_STATEMENT_COUNTS = {"matches_expected_result": 27, "parse_arguments": 63}
ORIGINAL_STATEMENT_GAPS = {"matches_expected_result": {1: "\n"}, "parse_arguments": {48: "\n"}}
OUTLINED_FUNCTIONS = {"matches_expected_result", "parse_arguments", "main"}

# Immutable metadata is inserted from the reviewed offline archive before source rewriting.
ORIGINAL_NODE_LAYOUT = (('Import:0',
  '#!/usr/bin/env python3\n\n',
  'b53e5a5286172fb0a4772843ba3ed62331687bcb9939ec9c49e9066d4ee4272d',
  '356e6775028bc09e699d8b986887aaebc5e3e8e414a507fe911e3ba7da1264d9'),
 ('Import:1',
  '',
  '6e343ac03bc73c3a16eea4b4743c0ad055768f79034d994ddcc6c3caed60217c',
  'f8d674320947c8c954eba144e9d5a4f51124bcc9d34d4211e6a5f2274078222c'),
 ('Import:2',
  '',
  '3727adff524e0616022eadd8f4af21a0778b29fc4c77bdfefd1afce2cbf5e4b7',
  'ac3a74d6dd975a8e8fe4b7f85fce794793a9355595b850e3926964913948d386'),
 ('Import:3',
  '',
  'a830333312c4c9c2233bb02762bd498203b1c3bf0527614735693b4b79f6d167',
  '6b81b4ca19c88ec507aad63f1f49291209368a9490113655a68e994c91d092c1'),
 ('Import:4',
  '',
  '9a60319879ee88ae2b4a273d8f45962bc499df0b50ae75bfd829a71b0e51352f',
  '567bfd8741b5c1754b0d1838b547143b4f04a26275ce98937689a425962e33dc'),
 ('Import:5',
  '',
  'b0dd8c25d757357471226c9aee63a48ef79828ad783b138fff229dcc5edc5622',
  '7159ff9f451ba939512f02dfc8541d2967cba52178ae7f8a024892d7d9e0911a'),
 ('Import:6',
  '',
  '17ea74bb79ba96edc39a8382a97f22993b80cc4088b3e2717a98917c60c1dd5a',
  '9d97d469cf8d66465563e1e22c6dab3b6107819aba2d9956c7080016c1ab9c1b'),
 ('Import:7',
  '',
  'c517577851c489e45abae2591256c40404a05c6d19cd4d5ae7fd22b0084cec6c',
  'f7252e3c1dbe1d5edf1f34c020a58a05b55f706b2c1d9479eae91f9a9d4be275'),
 ('Import:8',
  '',
  '7b2ddb8a2e64c6166feedaf30ee50ac62165fe149fdd8b0d7b777ebee21ca34d',
  'ff05bad0fd9a7212d3cd85fd2726055168be21bc8f6e0d93b29965794b0675d4'),
 ('ImportFrom:9',
  '',
  '07ac27eea41ef7e0d14eeb4cc65ebf0beaa2041ad913826ebdf97d56d6febd5f',
  '321ed2ed3afceb74be547c9905ed4c676e453b1a68fe7cb2d3e214caeac2624d'),
 ('ImportFrom:10',
  '',
  '3cb9ffccba5146b2dd177a30c3057a4395ebd5c745d0789d2ed95d0cf4afe271',
  '59051b17634cdcb5a576ceb58fdc0a4e5652ed3b60d66667e92ddefd931abeca'),
 ('ImportFrom:11',
  '',
  '16ad5ac83052a2ee1295dbea41963af43541233aa44d793d2363b29a9ac7e689',
  '14fb4ac0c90c87c6b4c672e2bf3de36cf41f934438645a7cc37129c0e1bc80f8'),
 ('Assign:PANIC_MARKER',
  '\n\n',
  'daa6f95744bddf0b98b38418dcfe2cfb4f6fe8a63c3282436adf62e01e302dad',
  'df8619a259279f60e04bded2c8c28b115552de401d7e76648ad81496c6b2902d'),
 ('Assign:FAILURE_MARKER',
  '',
  '8cbb51bec63f93eb3814e682911caf50d2ff546a4b442d06fe14d85b70641171',
  '60ae8baa853327bf0833e725d029f0a326d5b1adbe7567fe76e67059af99229a'),
 ('Assign:TRAP_READY_MARKER',
  '',
  '4c09e89772f996fbbc341cad628d0f506e0485c3935c18607f5fcd2bfcd6f58f',
  '1c19b5d0ab20442dec355c6bd18508d32117a1d82858d5a7bcdcfe4eb1090976'),
 ('Assign:TRAP_TEST_MARKER',
  '',
  'e5cc0540b0a0289f7acbd0e19e21a3de7fac4c7ce97bac356c6e5d5bcb4443bb',
  '491d690f48c6ac646ae9f7c888784e0788c5d72e54c78c936ff98df1bd38e5ee'),
 ('Assign:TRAP_TEST_PASS',
  '',
  'f43feae256a8110a59ad2b4ce867524a95aedc5c4ed4b01d9a90f090abcc93a5',
  'f3b42c952bbb65972b5cdf4622688a1f141c9c999210ecde3e195eed479e28a8'),
 ('Assign:TIMER_TEST_MARKER',
  '',
  'acb32b49040cc4a4f16aa12a4b49a90e0d0e0eae19cd9d2c9f0c432103a52ddc',
  'd9e092582df6315c4bcfa00abf465801c80d3ef7fc0936da321ad1359453be01'),
 ('Assign:TIMER_TEST_PASS',
  '',
  '257fe9b6444583c8d7e186af6adedeca070944ec9ab97b18efeda16be516669f',
  '44cd04405083bbd47928f011e1d93499c4d914693e73e2b3b2f5ba23c5391dc1'),
 ('Assign:FRAME_ALLOCATOR_READY_MARKER',
  '',
  '35e23e98edc4ce851107348e55ed3859d23e5e73d0c4b4c187b881d8852eec4c',
  'a8b9c76bb3b923f8b23150c31751ac5e792e27ea18fa11d4d5d5fe62fccc5e2c'),
 ('Assign:FRAME_ALLOCATOR_TEST_MARKER',
  '',
  'c86537a83dd551bf7124de474fe15644d67451cf33162c1ef02825b6709512f8',
  '18447d05278e2703e0de42bf7e5aeb024ac2f414d6db08a9628fd681730c8314'),
 ('Assign:FRAME_ALLOCATOR_READY_PATTERN',
  '',
  'f030da2b4fccd06f4e657b8e1d2b6c6f38a44d66621a24f0d4e9d57083ae2ddf',
  '25fa1f97bb8435b0edea2afe47469a4552baf37c75a0ce8df616e2c027b8b938'),
 ('Assign:FRAME_ALLOCATOR_TEST_PASS',
  '',
  '1dd11dc0547b2e102a19ece3cacc7f990e3d0c8c4af69273e036dc6cc29028e8',
  '6bf5688353f58edf6543555b04d8e51dea307bd21749d4e1067f1ef674884834'),
 ('Assign:MMU_READY_MARKER',
  '',
  'f371455c1843a74a1025af161b0bbd737dec850f64256dd860593968975a0bb0',
  '89b0cc3f20aa8fb720de5d7298789ef80082502a1e877990b8b17d95a3b5eb49'),
 ('Assign:MMU_TEST_MARKER',
  '',
  '1f64dc7f32694822e2fb4d2469b630e3edffbcff3e1703f08cc57ba0c2e78520',
  '21ee6f0c9ec176c698f43a3cb9f60b07fbc396219d776f8a5e43149b8f405eee'),
 ('Assign:MMU_READY_PATTERN',
  '',
  '943bd3de01895bab34fc544e511e36c354e04f4fc377b11807c74fb3571d9d5a',
  '549f7451dc2cfc146f096a212ffa710068674fcaffb709ee946854682768ad63'),
 ('Assign:MMU_TEST_PASS',
  '',
  '14786286122739efcc16ec6ae70a407482920ec27dea8aba5a44c8507cbf27eb',
  '9e6ad7884a4a5b669b808ae01f03afa7e181f1d3d357633dedb96a17f2a02a8a'),
 ('Assign:FRAME_OWNERSHIP_READY_MARKER',
  '',
  '8f1111994e4374bc9682c2b191f56bc64c20bbcb8e5aa124cb2cc43aa60e6d4e',
  'd06533fd4d20edbf1349d7537997cd8a0004db64ae57e9ea4fe958ce873eee17'),
 ('Assign:FRAME_OWNERSHIP_TEST_MARKER',
  '',
  'bbcc1543f6891d53f7c6c49b223cbe2f11546cd1145d0de1e560f33afb81c9cc',
  '563d3c0f5c4b60bc083f925cb3d0e3ce283e306a8bc220971b4692dd8c1628d4'),
 ('Assign:FRAME_OWNERSHIP_READY_PATTERN',
  '',
  'c5ae22344af4ac6aad4a415c023f6c3a66820898a08d52a6fcac629950480b85',
  '6c07045b25027ce130c55a5a7229941f7b75ed23ff5e6ca2462e4303b51741d6'),
 ('Assign:FRAME_OWNERSHIP_TEST_PASS',
  '',
  'c18da9bb0bd023904ca278a6427a3418f89611e0f1121d5251db651f0ca61ad5',
  'd3dfacfca07dc7e37c0ef4843a1c4ea9510d5af8a14b1e026677cd480ed778ae'),
 ('Assign:USER_ADDRESS_SPACE_TEST_MARKER',
  '',
  'bc30188ddcb54b2cfc1a17eaa33d0227d0923c01119dd11b0975cbe0b2ee0fd5',
  '3736d8278b432e275e71be6f1807cc8e1019de20e343973d3a821e6748078fe0'),
 ('Assign:USER_ADDRESS_SPACE_TEST_PASS',
  '',
  '1cf945f24ac24e80bb677557112961430e6f2c592a6686303f58955e59efbe0f',
  '78fb2c1a3370a3b7ede5cad4d8394cc608aedd06c28c14141da9156f4339bbdd'),
 ('Assign:USER_EXECUTION_TEST_MARKER',
  '',
  '0081f7b49aaf63cc7fe688f585526cc3f20d851265c18144e321b3aa391dc3de',
  '736f02259ba48a50c7631e193a5657c4709145324e1711e9028a23cc27b2a554'),
 ('Assign:USER_EXECUTION_TEST_PASS',
  '',
  '1944e33f7ff4777177125655f3f8448253b3cbdf010131df0c3461a7d7da8023',
  '439bcf328e09e32111e17ecbd9d76ac7c31ad74c7b7755da00f8d6f946467654'),
 ('Assign:SCHEDULER_TEST_MARKER',
  '',
  '6d5f803c1919be7fb5cd4044688847b7e5c44ba15eb61647a00d22bb1507f0fa',
  'bb845bb9e1b3e5f0a74d9de1d5592e0f2df84213a4737c3057e623ca6c50fb13'),
 ('Assign:SCHEDULER_TEST_PASS',
  '',
  '229aac41b5e37142eca618a149ccf481bc45c0ad97c2346abd7e0beb7d49a40e',
  'e2bb6c94f42a27cf97e3c0576bb69f1dda165b29bfac5b723e3c661b728d5598'),
 ('Assign:ENDPOINT_TEST_MARKER',
  '',
  'c7e7996374156700578031e410d24b0b20af21e73b7f7b0cd34b35ff32956058',
  '65169fd890b53b1ac7858a7aa88a8a742d7c32492c952b1d77f3bf45f4d05da0'),
 ('Assign:ENDPOINT_TEST_PASS',
  '',
  'ca74d2f53baf86bb9708898d30553040683d64d014763195a2c1809261429050',
  '83f7ed8743647ff442be78a5de4d704c27ae9e4b1e84c2c701ac9be7948be430'),
 ('Assign:IPC_TEST_MARKER',
  '',
  'b38ff341302a787358b6082d5ec5b09a106925678f06d065160bca6823657e8c',
  'adaef077f2d87b4005bea8f7aba4c229220cd3b12ca9b6d101eebaa4d723724b'),
 ('Assign:IPC_ADDRESS_SPACES_MARKER',
  '',
  '5cd31a9097511f3dd35eeb7c3725bb4d297bd6fda196c793611d42231afa35fb',
  '7c7c133b8e3dac5caef0add99e495511095a156d8e881c4970b022aaeca80d23'),
 ('Assign:IPC_ADDRESS_SPACES_PASS',
  '',
  '7847315db49626c1c895c12bf73f825c85cb88d63d65c0070f442f55167e7576',
  '2940eb23b60b5eec38ce3d5c5ec0fc1b150be84cd60924cd5a7cb45af0ee5ba5'),
 ('Assign:IPC_TEST_PASS',
  '',
  'b38f18f171ff4063630c8ae98b3a9e051dbcbd0366203524912c1f1202ed5cbb',
  '0d4e175c72bd59ffe7e9aed44ea92b7486f719b5502a495d86bc0c8d63ec2a91'),
 ('Assign:OBJECTS_READY_MARKER',
  '',
  '4745fc7ec71bfbd5914113a3306743281942e388fb3a74f62e8be9d8035a1059',
  '9c134aa8d08220e5c55357c03c998f6a508983811ea47566d52f3cafaca3c429'),
 ('Assign:OBJECT_MODEL_TEST_MARKER',
  '',
  '59a1b7e32b89f8aac77c37d4d1d244809f4bbbe405c228e9c2688457fe715aad',
  '2bd028f566294476cb9c06f4c6c3061ba107905da69c16ae96464f4753322a87'),
 ('Assign:NESTED_TRAP_TEST_MARKER',
  '',
  'afafd0848f1fed49a99360635d1f2352149fb4f1c2463e07f7e1912a80a5ddaf',
  'db4f9c16f25dc174c0ac804e472c15fe71b3d929ceb17f7e208e764f65823706'),
 ('Assign:OBJECTS_READY_PATTERN',
  '',
  '4af9fbec5222a14510a3b22a8b72e9274bea01d1e927a6f1c646f7e841f111ec',
  '0858af0200de788c9925ca454e22e04f6ba8768b2d2c9eee0dd0e6cfb9af3c04'),
 ('Assign:OBJECT_MODEL_TEST_PASS',
  '',
  'f53189b4cd08b9ed30ab3885082b8d07e63b0b089c378ab26e9b401bab66f6aa',
  '63d458427a83879677274474f555e4eb69d61f1906e12a53344c65852d94b98c'),
 ('Assign:NESTED_TRAP_TEST_PASS',
  '',
  '0f9d7520327a8a7d7555d073a1dc8f67a7b9a08de2120462b30da401dc88c867',
  'dfd7d614873837fc6e446d30dfc52f1fe92aa04de19e0dc1b0ba8b7298bc8804'),
 ('Assign:FDT_COUNTS_PATTERN',
  '',
  'd6a3390fb14b2de76d2c08071ee6263c2b4b96f7f6601ca762ea932722fdbfc9',
  'ffe2be610733cdee439ace3df696a33d0aea2d6881af63a1b921c64b15eb36c6'),
 ('Assign:FDT_EVENT_NAMES',
  '',
  '0024b26d52c9c4562907cf4fe06da95df03d9a1c95009579b4370edc06504a45',
  '93d2d6b41fec8657d2b93bb5986e99f82555238d547c6fbf6f9ba5d4c1d84484'),
 ('Assign:FDT_RANGE_PATTERN',
  '',
  'b792505e465174a230b46b858dbb7de3dcc76dccb9cc0ef6b9e4a57b29fc80a9',
  '8b10443b1b07d2a8d50f2b7d35b14caeb83d2c07d3c3342ac2d77aaeae463a6a'),
 ('Assign:PANIC_CORE_PATTERNS',
  '',
  '21e91fcd41f168b5eab02c086ef85e436bbc1a42c42d8f0270a866c46356115a',
  'ac870013126ec02732ec636486a51256caa75f945bcb7cce63b854112410d29e'),
 ('Assign:TRAP_CONTEXT_PATTERN',
  '',
  'e218f4f950840818bd68840a62da766a6f2a6b55b5819a11086028d020ea764e',
  'da83a39963118a7188d781e1b0b5a6e99be899bb6e7909b5027c7aa583727dd3'),
 ('Assign:VM_SELF_FAULT_PATTERN',
  '',
  'b89c25cd3ec6d1bf18b87f5ead6f410e53521bfa6f6b847950bf07e55079888c',
  '97faffbc7976d7c7c5554d3878093c9a4b1e283acb8d1fa235a378ccdd06eed2'),
 ('ClassDef:SmokeOutcome',
  '\n\n',
  'f101260b90d2950f95d2ede49725f087a8cf4482bf626ead9889132eb56cd9d4',
  '144f3c5aba66d4f7dd892c415407ff0d6b0b8e8e016833ec1948e849a79d24a0'),
 ('ClassDef:QemuResult',
  '\n\n',
  '833b33076024c04dac1048f486b6097fe4c4e63d1a1158d9a1a516dd7846efd3',
  '38c5a6859e9ac7eaa1a205c8cc28157cff6453456854acae24fe86c24019a49b'),
 ('FunctionDef:_has_complete_fdt_events',
  '\n\n',
  '7ebcf3abeb8937f4a96828644fe13583b3657eff8ecdc7226631b3bd956f7332',
  'c39de83ad176472418a8681321f68419f41aadd5dba97d6cb0beb4e77c69b557'),
 ('FunctionDef:_split_output_records',
  '\n\n',
  '01c6f8f897a78bae26c6eeff0998a67d4c23de37d3b9ea1497b509631ae86ecd',
  'dc42ab71698fa71875b4e01e06aceb52da8a8c65c2c67fc33fdc523a5e914967'),
 ('FunctionDef:_has_complete_panic_report',
  '\n\n',
  '4dda144d8f523e8e419cf5fa87f4e246a404116ef3b0a323de0a548de3013fbc',
  'da54dfb0035fc86de421d84dc9b64c2b013b422a21d49badfe01bd535dfb5928'),
 ('FunctionDef:_has_complete_trap_ready',
  '\n\n',
  'aaac4bc7108eca0e3505f35af06d6707f457e88d53585c0160fc45773c20054c',
  'da62a729ec48f4086e46a3d295eb7e77ef9bea9b7e643515c333c2461dafe5e5'),
 ('FunctionDef:parse_objects_ready',
  '\n\n',
  '7e35bc2b6ea9c67475783c4b9b479fae55b4ba5ca065b889de337d9a50710a2a',
  '9f7fc02bf827567126f4dae039d23ae9b2a5c5f63a6886dd5fdf65135fbe216e'),
 ('FunctionDef:_has_complete_objects_ready',
  '\n\n',
  '75828f0ebd8db10fd5de525d20dc18049f7d0e88d6ccee754b4e95e740d75df7',
  'bd7be1c85878548997974e2c02774a4b40ec2394dc3e237be98a260ea44fbc19'),
 ('FunctionDef:_has_complete_trap_test_report',
  '\n\n',
  '2abfce4ff4ccf68399652b5cf6ca786bf755c46a7e516a480fa0d7a585a73707',
  'f008e67e7352f7095f544612768fad112b0739bcdacb5a0173b95f4b58520a0a'),
 ('FunctionDef:_has_complete_timer_test_report',
  '\n\n',
  '5e4e6d051a625980818e39eee78a8668d671d37dd411ff5f2dabc9cabc7f94bc',
  'ead5e0fba2421e149d9cb3ae7a797ba92736ab64665f29c2d25df9eed5a5b572'),
 ('FunctionDef:parse_frame_allocator_ready_counts',
  '\n\n',
  '68dd864227956761c8bab27f561b8fb8a7e2a3102ecf802ad247dee2a36f68c9',
  '44ae71994b30b145506f7a82d68f67270529865f1eda951ef64be96702079f3a'),
 ('FunctionDef:_has_complete_frame_allocator_ready',
  '\n\n',
  'a39832517aaca20d7ddb13de293679436e5f6e5454692945924bfca9dd06a1ca',
  '7e9e636cc65ebb456fedbf770932861db4d2f7ca676a9ebf1928249f24957830'),
 ('FunctionDef:_frame_allocator_ready_precedes_target_outcome',
  '\n\n',
  '3590c7624e58c943cbcfe1966e9bd72db9703f14957a35de2ce34343b1e6e46c',
  'f104350373a9607df7e3cbc0873f7546896020e7af25d9499e5d72aeb761dd65'),
 ('FunctionDef:has_expected_frame_allocator_growth',
  '\n\n',
  '82990e5021e0d0c223b534a2bb233fdea2f216f1d4df3106fa9e188d7a405445',
  '1068eb309eff93b030fe2f2e5a325d5a87d4150aa86ab2162299ad3f2aa7d9af'),
 ('FunctionDef:_has_complete_frame_allocator_test_report',
  '\n\n',
  '7d96e2b926010c03f6fc9496f988e571f94c1f33478ee023eddf120fd89d082f',
  'bb742750787607ff24a79047266bb7dc9d5e3627e13b1cf06d12c5a12887c712'),
 ('FunctionDef:parse_mmu_ready',
  '\n\n',
  '4062d94e01147ecb675b0498c21ef0f5c653802882958c41d70ead01f658c58d',
  '9f126ca1a89c01ffb47ccf3b6f9837e64da215ef7e12b0d6ef7b0a42ee45e487'),
 ('FunctionDef:_has_complete_mmu_ready',
  '\n\n',
  '157feb1c3de2c4888bec67d258e38cbfc06f89ab8bfc3b45b7ee9d7a23838760',
  'c72fe59d3c5eb14f6a9c235c43963f6c14cf02e95d7db8fb954e3431e3ab139c'),
 ('FunctionDef:_mmu_ready_precedes_target_outcome',
  '\n\n',
  '5a52451fe672998ae4b6148845b31bac6340b3468a40a04d45bd0f95b308c806',
  'f07fa6e2564dceacd03f4b7bea67f84af3f488671022c181f1a462267ceb5da9'),
 ('FunctionDef:_has_complete_mmu_test_report',
  '\n\n',
  'd5a65ab2f39391e07eaad192950c9e54555d68586bd9dc91b483bc2c5381cdff',
  '7232ffebf73bec6c3facb7ad9a76549be83278da2634023d52d47d6b7124505d'),
 ('FunctionDef:parse_frame_ownership_ready',
  '\n\n',
  'c306c5ad4c81a5b9e2b12c62a1b78e35211959d86144e490433db7ecd0eec50a',
  '2be3564d3fe1b45ebe5c7e62b0552d7b5669969e2a33d1e6faf6426636c58d4c'),
 ('FunctionDef:_has_complete_frame_ownership_ready',
  '\n\n',
  '5836451f0a16b5b03c46c8e57bcadf59fcc14aa9e0c66a9d4f71fd311075f305',
  '8b2cda28c3215a8aa26ff31b48afaa1f6d8c40fa0bda45f38030cd8cceea5e31'),
 ('FunctionDef:_frame_ownership_ready_precedes_target_outcome',
  '\n\n',
  '21f33fe6fb026232462bc085c7b5e2c3bc714de6b4e7055f998a71b3f4593215',
  'd9909c21e89bc1cd249fda8d36a1d74ef14477a9419f42f4f5357ab2a21806ae'),
 ('FunctionDef:_has_complete_frame_ownership_test_report',
  '\n\n',
  '07a1fe189da36dc024c1a4a4b80adb8e4da60c7030426c33d7610129327a719a',
  '787f34d7dedcf2fc9c40ef2c039ff5e7fbc2cb87cde3a3efc275bb48493b31e8'),
 ('FunctionDef:_has_complete_user_address_space_test_report',
  '\n\n',
  'a1792fabe819d647b8ee762ec266319c0e2bbc615e43b97d2c8d3017948aa5bd',
  '484a483c6a5a54a6a6170d2c04aeb701f7d741e0bfd334c9dd751be21062a647'),
 ('FunctionDef:_has_complete_user_execution_test_report',
  '\n\n',
  '484f19ac1333a2ddb8b14e4f8e8008c088a6801577a7c22cc72a50c8201efb1d',
  '1b75a8f932c6b1910d09544caba82e25b2dd9899e1ef654fd367f6500203e477'),
 ('FunctionDef:_has_complete_scheduler_test_report',
  '\n\n',
  '0a3fd50677fdaf77b3bd9aa13f2afc83a900c4e318b7d4824235bf8e815e2ab7',
  '4c3e95effe13c477af55897092991c26c87ddcf2903376254f1fbc3f9805f950'),
 ('FunctionDef:_has_complete_endpoint_test_report',
  '\n\n',
  '6bd897cfb9fd97f6fe93b2c204337967e5cf87ec80d083fc7df8789d798c393d',
  'e52f1d63c6a8d5a92e19e3fffed7d2d505f16579212481c5e394366106e7577a'),
 ('FunctionDef:_has_complete_ipc_test_report',
  '\n\n',
  'ea3519b3a7e91e5b93e3d47df4bc3d78d851fc84d77aa615ab6f58a436e79444',
  '55d54ef52cafbb9a02147e3018d5525788e0a1d2a80765ebac9fbac85df5f45b'),
 ('FunctionDef:_objects_ready_precedes_target_outcome',
  '\n\n',
  'c5ce9079d67cda3ae56824498e9b840be3153db1b5184f817cfc9692f4561205',
  '4c4af5b53ed973908c3df6a065018d822402ded3d57133f4b87cfe3a64d09981'),
 ('FunctionDef:_has_complete_object_model_test_report',
  '\n\n',
  '62f55b41b11c680c9771e6b12937d6da77a37535c0ffc99cf10c6bda3a8a7b69',
  '76b2761ba38e13a5004d15190321766209c9a23ea08feb6d388528fd905701e3'),
 ('FunctionDef:_has_complete_nested_trap_test_report',
  '\n\n',
  'cf4d6b905e19cdfeaee623f4272ef37e3db051077b8f71766c6a05cfccc44072',
  'd9cc4629217be77df654de06030cf8d3a48f79a619bb5a44319055804ad56646'),
 ('FunctionDef:_has_complete_trap_context',
  '\n\n',
  'f82f8f59c34c927aa30f7bf05f2d68329ee9ec73864ef1abac956fb6f51b1efb',
  'e6d2aa7d5265e406b0077a666a847e6d7eebd8b896e58fd15d51dc1c14572544'),
 ('FunctionDef:_has_vm_self_fault',
  '\n\n',
  'e1a310cd3a67aa1cd48ad6767f63553f072762be0ee78ff75f7ca402139ab6df',
  '1bfe1ff1e55a389a25abe089606477305a51db67e888878474ced836cb506eba'),
 ('FunctionDef:_has_complete_scheduler_invalid_context',
  '\n\n',
  '4dffbeb8a74cacc9410bd80439e81851d4c4156d41fa39dfa1d4d7f68301c6b6',
  'c0e7a14db79d0e53359c607a1490aa03793e40c434e3d93eb43191feab2869e8'),
 ('FunctionDef:parse_nm_symbol_address',
  '\n\n',
  'c62fa1cbdf9b410ce0997124df72d9dcf027b35a17b879d4694d2b3a50d26fda',
  '86fc08a45fa8e0ad5ca5a7eb1582bd896c43282a67b05d9993ba830353704073'),
 ('FunctionDef:resolve_symbol_address',
  '\n\n',
  '5f6ff50b9c6e9cd62fc690cba0baf4e11cd3d0c6c26710d7d86685d926035d34',
  '1e0834b22d304f944fd2c25975dcb54479a3219cdbfad60b471f0f83f4ebd3f2'),
 ('FunctionDef:_has_required_output',
  '\n\n',
  'f826695f248a1c81677bb97e0c0de515a37957bddc75fc5c513a74d109538ef1',
  '4e5db1a53b37bb4930ec4f55c391c04b5d2ae91b4c868adfafec56550da3b4fb'),
 ('FunctionDef:_has_ordered_patterns',
  '\n\n',
  '3cf32ac158c5be8df064cdc39e9c129abaf532fe44ce7a7f45ada31ce383762a',
  '23a838c7cd878e0af42f5401478364ca916220f0bc78bb7c6df5d3d3737b5a8d'),
 ('FunctionDef:matches_expected_result',
  '\n\n',
  '491a15e0d4c3c01f10bfa6591343c4a548015868260da8f7eb658f41d08ad6ff',
  '88d0f9d686e1ed32bc1b41a637dec49244719e77d8f1efcc69f3e9d1135bff44'),
 ('FunctionDef:classify_smoke',
  '\n\n',
  '7ca3e6e7bf169945b9ebc622164906a31e110263bb0127ccbd01484d62f4468e',
  '7b5f5ef2ccbce71663842a88afc6877d36bb500cd606a557c41cb8041a51eaa1'),
 ('FunctionDef:build_qemu_command',
  '\n\n',
  'cbaeeadfed54bd5da75d28e41e2d8509d812670e589b2d8ac69697e76aea6297',
  '7bb9c35d32207b8d25dea76e776f63ac4f16b088a112be06f5cb2792abe77809'),
 ('FunctionDef:_decode_output',
  '\n\n',
  '9808a66d71627a17ba2eb53f0ae55cf87edfd6045de58070ae92b1558f0ab9ab',
  '205178203b71010cbf9386331a2981d3138094de50a3b9eaa970afc58456239c'),
 ('FunctionDef:_serial_trigger_observed',
  '\n\n',
  '1cfd1daf0bb33741f7ef54e61c86747afcf29b0c89dc1912c7fc9059edec7bf8',
  '6c5731c45a2e3bab6b358c25590ea0235582034bb7ce5f1f584575b926584035'),
 ('FunctionDef:_run_qemu_with_serial_input',
  '\n\n',
  '571c1f57c97d4a621cd9df996193e81376ebd24ad0c2411c1479bbe3d364d42a',
  'f6fd451a87b55b5e85b6dc8bdde423b074b52b0340304aff6846c856fe7064b7'),
 ('FunctionDef:run_qemu',
  '\n\n',
  '60c3a604214af6b839b85385f42f0bc149c498015e04ed618065a819188a93f8',
  '2b9f83e0917987873c5b3b48a3c168fdfc45403e50965f7b1617eac6503cff48'),
 ('FunctionDef:print_tap_result',
  '\n\n',
  'fa3c977f87299fc07b328c665aaeaf193a5080076341df8b48126d66961d908d',
  'ee3a5fa3e6999779764bfc4ad6978311f6aea1485d20691d4204e2b8c237ed0c'),
 ('FunctionDef:parse_arguments',
  '\n\n',
  '507d1ea42dec995240c988e4fbea3e0abeeaefdf62a7723f6a2a865e8de0643b',
  '013718788c6cd7ec1a1c26ac7898c25a423111ba39e3710060be93a537948202'),
 ('FunctionDef:main',
  '\n\n',
  'cd757f1fab23802bf37b34cce06b7c69d0e2be0d0fc2d6bc443959f2499477b1',
  'dcca459eb156d107a3899407302cbb3ece09975d8747df79cc22a5296850561b'),
 ('If:103',
  '\n\n',
  'c86a687dc0735836998a4f2d1ad0536d34c088d503731b70989149cd3c97105a',
  'e4995c06e8cea82a5a651d735d1c52e4c991c130e8ba75aa297883ab85113ba7'))

ORIGINAL_GLOBAL_NAMES = ('PANIC_MARKER',
 'FAILURE_MARKER',
 'TRAP_READY_MARKER',
 'TRAP_TEST_MARKER',
 'TRAP_TEST_PASS',
 'TIMER_TEST_MARKER',
 'TIMER_TEST_PASS',
 'FRAME_ALLOCATOR_READY_MARKER',
 'FRAME_ALLOCATOR_TEST_MARKER',
 'FRAME_ALLOCATOR_READY_PATTERN',
 'FRAME_ALLOCATOR_TEST_PASS',
 'MMU_READY_MARKER',
 'MMU_TEST_MARKER',
 'MMU_READY_PATTERN',
 'MMU_TEST_PASS',
 'FRAME_OWNERSHIP_READY_MARKER',
 'FRAME_OWNERSHIP_TEST_MARKER',
 'FRAME_OWNERSHIP_READY_PATTERN',
 'FRAME_OWNERSHIP_TEST_PASS',
 'USER_ADDRESS_SPACE_TEST_MARKER',
 'USER_ADDRESS_SPACE_TEST_PASS',
 'USER_EXECUTION_TEST_MARKER',
 'USER_EXECUTION_TEST_PASS',
 'SCHEDULER_TEST_MARKER',
 'SCHEDULER_TEST_PASS',
 'ENDPOINT_TEST_MARKER',
 'ENDPOINT_TEST_PASS',
 'IPC_TEST_MARKER',
 'IPC_ADDRESS_SPACES_MARKER',
 'IPC_ADDRESS_SPACES_PASS',
 'IPC_TEST_PASS',
 'OBJECTS_READY_MARKER',
 'OBJECT_MODEL_TEST_MARKER',
 'NESTED_TRAP_TEST_MARKER',
 'OBJECTS_READY_PATTERN',
 'OBJECT_MODEL_TEST_PASS',
 'NESTED_TRAP_TEST_PASS',
 'FDT_COUNTS_PATTERN',
 'FDT_EVENT_NAMES',
 'FDT_RANGE_PATTERN',
 'PANIC_CORE_PATTERNS',
 'TRAP_CONTEXT_PATTERN',
 'VM_SELF_FAULT_PATTERN')

ORIGINAL_CLASSES = {'QemuResult': '38c5a6859e9ac7eaa1a205c8cc28157cff6453456854acae24fe86c24019a49b',
 'SmokeOutcome': '144f3c5aba66d4f7dd892c415407ff0d6b0b8e8e016833ec1948e849a79d24a0'}

ORIGINAL_FUNCTIONS = {'_decode_output': ('9808a66d71627a17ba2eb53f0ae55cf87edfd6045de58070ae92b1558f0ab9ab',
                    '205178203b71010cbf9386331a2981d3138094de50a3b9eaa970afc58456239c'),
 '_frame_allocator_ready_precedes_target_outcome': ('3590c7624e58c943cbcfe1966e9bd72db9703f14957a35de2ce34343b1e6e46c',
                                                    'f104350373a9607df7e3cbc0873f7546896020e7af25d9499e5d72aeb761dd65'),
 '_frame_ownership_ready_precedes_target_outcome': ('21f33fe6fb026232462bc085c7b5e2c3bc714de6b4e7055f998a71b3f4593215',
                                                    'd9909c21e89bc1cd249fda8d36a1d74ef14477a9419f42f4f5357ab2a21806ae'),
 '_has_complete_endpoint_test_report': ('6bd897cfb9fd97f6fe93b2c204337967e5cf87ec80d083fc7df8789d798c393d',
                                        'e52f1d63c6a8d5a92e19e3fffed7d2d505f16579212481c5e394366106e7577a'),
 '_has_complete_fdt_events': ('7ebcf3abeb8937f4a96828644fe13583b3657eff8ecdc7226631b3bd956f7332',
                              'c39de83ad176472418a8681321f68419f41aadd5dba97d6cb0beb4e77c69b557'),
 '_has_complete_frame_allocator_ready': ('a39832517aaca20d7ddb13de293679436e5f6e5454692945924bfca9dd06a1ca',
                                         '7e9e636cc65ebb456fedbf770932861db4d2f7ca676a9ebf1928249f24957830'),
 '_has_complete_frame_allocator_test_report': ('7d96e2b926010c03f6fc9496f988e571f94c1f33478ee023eddf120fd89d082f',
                                               'bb742750787607ff24a79047266bb7dc9d5e3627e13b1cf06d12c5a12887c712'),
 '_has_complete_frame_ownership_ready': ('5836451f0a16b5b03c46c8e57bcadf59fcc14aa9e0c66a9d4f71fd311075f305',
                                         '8b2cda28c3215a8aa26ff31b48afaa1f6d8c40fa0bda45f38030cd8cceea5e31'),
 '_has_complete_frame_ownership_test_report': ('07a1fe189da36dc024c1a4a4b80adb8e4da60c7030426c33d7610129327a719a',
                                               '787f34d7dedcf2fc9c40ef2c039ff5e7fbc2cb87cde3a3efc275bb48493b31e8'),
 '_has_complete_ipc_test_report': ('ea3519b3a7e91e5b93e3d47df4bc3d78d851fc84d77aa615ab6f58a436e79444',
                                   '55d54ef52cafbb9a02147e3018d5525788e0a1d2a80765ebac9fbac85df5f45b'),
 '_has_complete_mmu_ready': ('157feb1c3de2c4888bec67d258e38cbfc06f89ab8bfc3b45b7ee9d7a23838760',
                             'c72fe59d3c5eb14f6a9c235c43963f6c14cf02e95d7db8fb954e3431e3ab139c'),
 '_has_complete_mmu_test_report': ('d5a65ab2f39391e07eaad192950c9e54555d68586bd9dc91b483bc2c5381cdff',
                                   '7232ffebf73bec6c3facb7ad9a76549be83278da2634023d52d47d6b7124505d'),
 '_has_complete_nested_trap_test_report': ('cf4d6b905e19cdfeaee623f4272ef37e3db051077b8f71766c6a05cfccc44072',
                                           'd9cc4629217be77df654de06030cf8d3a48f79a619bb5a44319055804ad56646'),
 '_has_complete_object_model_test_report': ('62f55b41b11c680c9771e6b12937d6da77a37535c0ffc99cf10c6bda3a8a7b69',
                                            '76b2761ba38e13a5004d15190321766209c9a23ea08feb6d388528fd905701e3'),
 '_has_complete_objects_ready': ('75828f0ebd8db10fd5de525d20dc18049f7d0e88d6ccee754b4e95e740d75df7',
                                 'bd7be1c85878548997974e2c02774a4b40ec2394dc3e237be98a260ea44fbc19'),
 '_has_complete_panic_report': ('4dda144d8f523e8e419cf5fa87f4e246a404116ef3b0a323de0a548de3013fbc',
                                'da54dfb0035fc86de421d84dc9b64c2b013b422a21d49badfe01bd535dfb5928'),
 '_has_complete_scheduler_invalid_context': ('4dffbeb8a74cacc9410bd80439e81851d4c4156d41fa39dfa1d4d7f68301c6b6',
                                             'c0e7a14db79d0e53359c607a1490aa03793e40c434e3d93eb43191feab2869e8'),
 '_has_complete_scheduler_test_report': ('0a3fd50677fdaf77b3bd9aa13f2afc83a900c4e318b7d4824235bf8e815e2ab7',
                                         '4c3e95effe13c477af55897092991c26c87ddcf2903376254f1fbc3f9805f950'),
 '_has_complete_timer_test_report': ('5e4e6d051a625980818e39eee78a8668d671d37dd411ff5f2dabc9cabc7f94bc',
                                     'ead5e0fba2421e149d9cb3ae7a797ba92736ab64665f29c2d25df9eed5a5b572'),
 '_has_complete_trap_context': ('f82f8f59c34c927aa30f7bf05f2d68329ee9ec73864ef1abac956fb6f51b1efb',
                                'e6d2aa7d5265e406b0077a666a847e6d7eebd8b896e58fd15d51dc1c14572544'),
 '_has_complete_trap_ready': ('aaac4bc7108eca0e3505f35af06d6707f457e88d53585c0160fc45773c20054c',
                              'da62a729ec48f4086e46a3d295eb7e77ef9bea9b7e643515c333c2461dafe5e5'),
 '_has_complete_trap_test_report': ('2abfce4ff4ccf68399652b5cf6ca786bf755c46a7e516a480fa0d7a585a73707',
                                    'f008e67e7352f7095f544612768fad112b0739bcdacb5a0173b95f4b58520a0a'),
 '_has_complete_user_address_space_test_report': ('a1792fabe819d647b8ee762ec266319c0e2bbc615e43b97d2c8d3017948aa5bd',
                                                  '484a483c6a5a54a6a6170d2c04aeb701f7d741e0bfd334c9dd751be21062a647'),
 '_has_complete_user_execution_test_report': ('484f19ac1333a2ddb8b14e4f8e8008c088a6801577a7c22cc72a50c8201efb1d',
                                              '1b75a8f932c6b1910d09544caba82e25b2dd9899e1ef654fd367f6500203e477'),
 '_has_ordered_patterns': ('3cf32ac158c5be8df064cdc39e9c129abaf532fe44ce7a7f45ada31ce383762a',
                           '23a838c7cd878e0af42f5401478364ca916220f0bc78bb7c6df5d3d3737b5a8d'),
 '_has_required_output': ('f826695f248a1c81677bb97e0c0de515a37957bddc75fc5c513a74d109538ef1',
                          '4e5db1a53b37bb4930ec4f55c391c04b5d2ae91b4c868adfafec56550da3b4fb'),
 '_has_vm_self_fault': ('e1a310cd3a67aa1cd48ad6767f63553f072762be0ee78ff75f7ca402139ab6df',
                        '1bfe1ff1e55a389a25abe089606477305a51db67e888878474ced836cb506eba'),
 '_mmu_ready_precedes_target_outcome': ('5a52451fe672998ae4b6148845b31bac6340b3468a40a04d45bd0f95b308c806',
                                        'f07fa6e2564dceacd03f4b7bea67f84af3f488671022c181f1a462267ceb5da9'),
 '_objects_ready_precedes_target_outcome': ('c5ce9079d67cda3ae56824498e9b840be3153db1b5184f817cfc9692f4561205',
                                            '4c4af5b53ed973908c3df6a065018d822402ded3d57133f4b87cfe3a64d09981'),
 '_run_qemu_with_serial_input': ('571c1f57c97d4a621cd9df996193e81376ebd24ad0c2411c1479bbe3d364d42a',
                                 'f6fd451a87b55b5e85b6dc8bdde423b074b52b0340304aff6846c856fe7064b7'),
 '_serial_trigger_observed': ('1cfd1daf0bb33741f7ef54e61c86747afcf29b0c89dc1912c7fc9059edec7bf8',
                              '6c5731c45a2e3bab6b358c25590ea0235582034bb7ce5f1f584575b926584035'),
 '_split_output_records': ('01c6f8f897a78bae26c6eeff0998a67d4c23de37d3b9ea1497b509631ae86ecd',
                           'dc42ab71698fa71875b4e01e06aceb52da8a8c65c2c67fc33fdc523a5e914967'),
 'build_qemu_command': ('cbaeeadfed54bd5da75d28e41e2d8509d812670e589b2d8ac69697e76aea6297',
                        '7bb9c35d32207b8d25dea76e776f63ac4f16b088a112be06f5cb2792abe77809'),
 'classify_smoke': ('7ca3e6e7bf169945b9ebc622164906a31e110263bb0127ccbd01484d62f4468e',
                    '7b5f5ef2ccbce71663842a88afc6877d36bb500cd606a557c41cb8041a51eaa1'),
 'has_expected_frame_allocator_growth': ('82990e5021e0d0c223b534a2bb233fdea2f216f1d4df3106fa9e188d7a405445',
                                         '1068eb309eff93b030fe2f2e5a325d5a87d4150aa86ab2162299ad3f2aa7d9af'),
 'main': ('cd757f1fab23802bf37b34cce06b7c69d0e2be0d0fc2d6bc443959f2499477b1',
          'dcca459eb156d107a3899407302cbb3ece09975d8747df79cc22a5296850561b'),
 'matches_expected_result': ('491a15e0d4c3c01f10bfa6591343c4a548015868260da8f7eb658f41d08ad6ff',
                             '88d0f9d686e1ed32bc1b41a637dec49244719e77d8f1efcc69f3e9d1135bff44'),
 'parse_arguments': ('507d1ea42dec995240c988e4fbea3e0abeeaefdf62a7723f6a2a865e8de0643b',
                     '013718788c6cd7ec1a1c26ac7898c25a423111ba39e3710060be93a537948202'),
 'parse_frame_allocator_ready_counts': ('68dd864227956761c8bab27f561b8fb8a7e2a3102ecf802ad247dee2a36f68c9',
                                        '44ae71994b30b145506f7a82d68f67270529865f1eda951ef64be96702079f3a'),
 'parse_frame_ownership_ready': ('c306c5ad4c81a5b9e2b12c62a1b78e35211959d86144e490433db7ecd0eec50a',
                                 '2be3564d3fe1b45ebe5c7e62b0552d7b5669969e2a33d1e6faf6426636c58d4c'),
 'parse_mmu_ready': ('4062d94e01147ecb675b0498c21ef0f5c653802882958c41d70ead01f658c58d',
                     '9f126ca1a89c01ffb47ccf3b6f9837e64da215ef7e12b0d6ef7b0a42ee45e487'),
 'parse_nm_symbol_address': ('c62fa1cbdf9b410ce0997124df72d9dcf027b35a17b879d4694d2b3a50d26fda',
                             '86fc08a45fa8e0ad5ca5a7eb1582bd896c43282a67b05d9993ba830353704073'),
 'parse_objects_ready': ('7e35bc2b6ea9c67475783c4b9b479fae55b4ba5ca065b889de337d9a50710a2a',
                         '9f7fc02bf827567126f4dae039d23ae9b2a5c5f63a6886dd5fdf65135fbe216e'),
 'print_tap_result': ('fa3c977f87299fc07b328c665aaeaf193a5080076341df8b48126d66961d908d',
                      'ee3a5fa3e6999779764bfc4ad6978311f6aea1485d20691d4204e2b8c237ed0c'),
 'resolve_symbol_address': ('5f6ff50b9c6e9cd62fc690cba0baf4e11cd3d0c6c26710d7d86685d926035d34',
                            '1e0834b22d304f944fd2c25975dcb54479a3219cdbfad60b471f0f83f4ebd3f2'),
 'run_qemu': ('60c3a604214af6b839b85385f42f0bc149c498015e04ed618065a819188a93f8',
              '2b9f83e0917987873c5b3b48a3c168fdfc45403e50965f7b1617eac6503cff48')}

ORIGINAL_TEST_METHOD_ORDER = {'ExpectedOutcomeTest': ('test_accepts_complete_clean_panic',
                         'test_rejects_forbidden_marker',
                         'test_rejects_forbidden_pattern',
                         'test_requires_patterns_in_order',
                         'test_rejects_panic_that_times_out',
                         'test_rejects_duplicate_or_out_of_order_panic_records',
                         'test_rejects_panic_with_explicit_failure',
                         'test_rejects_unterminated_final_panic_record',
                         'test_rejects_trap_context_for_direct_panic',
                         'test_accepts_complete_trap_recovery',
                         'test_rejects_duplicate_trap_recovery_pass',
                         'test_rejects_out_of_order_trap_recovery_pass',
                         'test_rejects_trap_pass_before_fdt_ready',
                         'test_accepts_complete_timer_test_report',
                         'test_rejects_missing_timer_test_report',
                         'test_rejects_duplicate_timer_test_report',
                         'test_rejects_timer_test_report_before_fdt_ready',
                         'test_rejects_malformed_timer_tick_count',
                         'test_rejects_malformed_timer_interval',
                         'test_rejects_unterminated_timer_test_report',
                         'test_accepts_complete_frame_allocator_ready_report',
                         'test_requires_expected_frame_allocator_growth',
                         'test_rejects_zero_or_mismatched_frame_allocator_counts',
                         'test_rejects_duplicate_or_early_frame_allocator_ready',
                         'test_rejects_malformed_or_unterminated_frame_allocator_ready',
                         'test_rejects_frame_allocator_ready_after_target_outcome',
                         'test_ignores_unrelated_target_prefix_before_allocator_ready',
                         'test_accepts_complete_frame_allocator_test_report',
                         'test_rejects_missing_or_duplicate_frame_allocator_test_report',
                         'test_rejects_early_or_malformed_frame_allocator_test_report',
                         'test_rejects_unterminated_frame_allocator_test_report',
                         'test_accepts_complete_mmu_ready_report',
                         'test_rejects_invalid_mmu_ready_report',
                         'test_rejects_duplicate_or_early_mmu_ready_report',
                         'test_rejects_mmu_ready_after_target_outcome',
                         'test_accepts_complete_mmu_test_report',
                         'test_rejects_invalid_mmu_test_report',
                         'test_accepts_complete_frame_ownership_ready_report',
                         'test_rejects_invalid_frame_ownership_ready_report',
                         'test_rejects_frame_ownership_ready_after_target_outcome',
                         'test_accepts_complete_frame_ownership_test_report',
                         'test_rejects_invalid_frame_ownership_test_report',
                         'test_accepts_complete_user_address_space_test_report',
                         'test_rejects_invalid_user_address_space_test_report',
                         'test_accepts_complete_user_execution_test_report',
                         'test_rejects_invalid_user_execution_test_report',
                         'test_accepts_complete_scheduler_test_report',
                         'test_rejects_invalid_scheduler_test_report',
                         'test_accepts_complete_endpoint_test_report',
                         'test_rejects_invalid_endpoint_test_report',
                         'test_accepts_complete_ipc_test_report',
                         'test_rejects_invalid_ipc_test_report',
                         'test_requires_exact_invalid_scheduler_context_report',
                         'test_accepts_complete_objects_ready_report',
                         'test_rejects_invalid_objects_ready_report',
                         'test_accepts_complete_object_model_test_report',
                         'test_rejects_invalid_object_model_test_report',
                         'test_accepts_complete_nested_trap_test_report',
                         'test_rejects_invalid_nested_trap_test_report',
                         'test_accepts_complete_trap_panic_context',
                         'test_rejects_duplicate_trap_panic_context',
                         'test_rejects_unterminated_trap_panic_context',
                         'test_rejects_wrong_trap_context_sepc',
                         'test_requires_exact_vm_self_fault_and_context'),
 'NmSymbolTest': ('test_parses_exact_defined_symbol', 'test_rejects_duplicate_defined_symbol'),
 'QemuCommandTest': ('test_uses_explicit_single_hart_plic_machine',
                     'test_accepts_explicit_guest_memory_size'),
 'QemuSerialInputTest': ('test_writes_serial_input_once_after_complete_trigger_line',
                         'test_default_execution_keeps_stdin_closed',
                         'test_closed_serial_input_pipe_returns_a_normal_result',
                         'test_trigger_requires_an_exact_terminated_line'),
 'SmokeClassificationTest': ('test_accepts_marker_followed_by_clean_exit',
                             'test_reports_explicit_target_failure',
                             'test_reports_kernel_panic',
                             'test_reports_unexpected_exit_without_marker',
                             'test_rejects_marker_embedded_in_another_line',
                             'test_reports_timeout_before_other_output_states',
                             'test_requires_every_expected_marker',
                             'test_accepts_complete_fdt_event_stream',
                             'test_rejects_incomplete_fdt_event_stream',
                             'test_rejects_empty_fdt_reservation_result',
                             'test_rejects_malformed_fdt_range_event',
                             'test_rejects_malformed_duplicate_fdt_count_summary'),
 'TapOutputTest': ('test_pass_includes_machine_readable_outcome',)}

ORIGINAL_TEST_METHODS = {'ExpectedOutcomeTest.test_accepts_complete_clean_panic': '20ebb9c97289184ebce58a42b033c5717762d7a68ba6066b662f186be603cc3b',
 'ExpectedOutcomeTest.test_accepts_complete_endpoint_test_report': '3bd7b966e627b3cd4e24ee234a28e8b9cb63f1db1348d626814e9962f3f8a381',
 'ExpectedOutcomeTest.test_accepts_complete_frame_allocator_ready_report': '5f35057294def88b8f4474f77638233909fa4ea095faf9bca4dbb3f5a62165d2',
 'ExpectedOutcomeTest.test_accepts_complete_frame_allocator_test_report': '3af579a43924214eb963127661a71e9647bff51eeb5a950bcd76b1f957a929d1',
 'ExpectedOutcomeTest.test_accepts_complete_frame_ownership_ready_report': '9aba1aa9748f319ce4bfe44c577bb4cf504bd89419c07f094ec0943a76f03f18',
 'ExpectedOutcomeTest.test_accepts_complete_frame_ownership_test_report': '50f01365a5331ee29eda137317df39cf07e6bb589be70816e6c313d2567283d5',
 'ExpectedOutcomeTest.test_accepts_complete_ipc_test_report': '743a6c1b23c75ce7ff9b15046bfa39fc36705a595121ce7e3f09141037bef447',
 'ExpectedOutcomeTest.test_accepts_complete_mmu_ready_report': '11957a8f7f4c8b07344f8ec4d3b9080bfcfb77df3ef98b26cd429d13045d8a29',
 'ExpectedOutcomeTest.test_accepts_complete_mmu_test_report': '799c6fe75b4bf2712b8da84097c9702754209bfff85f75b097d0232633f8c756',
 'ExpectedOutcomeTest.test_accepts_complete_nested_trap_test_report': '9f0abd175bc11826bc62120bc490712541ef3b2551590bab353731ca3d55fba3',
 'ExpectedOutcomeTest.test_accepts_complete_object_model_test_report': 'b51977bdbff27826a74daba8893fb03acb460fc2300431ae7fabf1aa2fd94531',
 'ExpectedOutcomeTest.test_accepts_complete_objects_ready_report': 'cc6460c80f2ae2c5ce8de56abac9c2b76bf2f4695ccf425a0ea4dff54a5e0d87',
 'ExpectedOutcomeTest.test_accepts_complete_scheduler_test_report': '0105fad1a251235d19fc7e01b3849b6f55374a7af80b6fbca92ce62ae99e7414',
 'ExpectedOutcomeTest.test_accepts_complete_timer_test_report': 'eb82a8b1dc6a70aef69e75a874d9a89ac71b38c2780010614aea82b1320713aa',
 'ExpectedOutcomeTest.test_accepts_complete_trap_panic_context': 'c2277d1dd3cee25ae1f0b926b61e07545342c6d8b43d60db618cd47e72740dc4',
 'ExpectedOutcomeTest.test_accepts_complete_trap_recovery': '83cde48aaa75dac7fdcce23aeaf5bdec8b039a2fb0b52c8114e02b709f870859',
 'ExpectedOutcomeTest.test_accepts_complete_user_address_space_test_report': '4240012ccc8de83cc801b4f018e9bbc27e44676a091d2938b8ebf66a9dfd8f00',
 'ExpectedOutcomeTest.test_accepts_complete_user_execution_test_report': '996522d845cec1e06b79fecd65b55f25d15d95944821fc21e31b8df10c4b9754',
 'ExpectedOutcomeTest.test_ignores_unrelated_target_prefix_before_allocator_ready': '84bfc4b5ebe23ad32fa092ae96f2764e88f64f873d391f7e7d67f1beedbe445e',
 'ExpectedOutcomeTest.test_rejects_duplicate_or_early_frame_allocator_ready': 'ecae89bde9eee3828771de57b9da506a64b897a693d7ab19b6f92f14a8da1069',
 'ExpectedOutcomeTest.test_rejects_duplicate_or_early_mmu_ready_report': '3b20439af7d0a1f3d0a99466aa8302ce31e6a04b326c0b249c04e9d4e0eaf2f8',
 'ExpectedOutcomeTest.test_rejects_duplicate_or_out_of_order_panic_records': '85457c783411155272b29d5d3dd737e73fb2bf9042ae2e688c8356c7f36f7420',
 'ExpectedOutcomeTest.test_rejects_duplicate_timer_test_report': '228c73d88be832943c517422375f461d2682c51ddb60ce9a15124a3d867ed163',
 'ExpectedOutcomeTest.test_rejects_duplicate_trap_panic_context': '518511e7668d2e3c2f2247e4ebc8e6c18608de4b75b51f87e40bb8311fe6a0a7',
 'ExpectedOutcomeTest.test_rejects_duplicate_trap_recovery_pass': 'fcc063e65ceb7356a97b4135844ab7d899fe9a3855d30a6888cdf2c059c26910',
 'ExpectedOutcomeTest.test_rejects_early_or_malformed_frame_allocator_test_report': '70c495f297219c1196451b6233d335dfc9031a0cbcea31a940f534646249e72c',
 'ExpectedOutcomeTest.test_rejects_forbidden_marker': '0303b7c429c8c9226457f081533685fac77027769bf0a1ff0fb143ce336174db',
 'ExpectedOutcomeTest.test_rejects_forbidden_pattern': 'e1b50689620f47c92b9b6af7fc9ab1a4596701b3e67be45d2b6b4017d41cba08',
 'ExpectedOutcomeTest.test_rejects_frame_allocator_ready_after_target_outcome': '480fee798da4fb949d1aed14d7d346621c861a272603232abe0ebb0d9eb79c5f',
 'ExpectedOutcomeTest.test_rejects_frame_ownership_ready_after_target_outcome': 'cf7a5f9ec2f8df1fd0c53fdc08ff154c09e54b752f0b5bd671082727d64a19d7',
 'ExpectedOutcomeTest.test_rejects_invalid_endpoint_test_report': 'd16877ada3acd1a7c3cf9579f9684189726d16e034f7c3de95848d396dbfa6a6',
 'ExpectedOutcomeTest.test_rejects_invalid_frame_ownership_ready_report': 'feb3b153ded4b6f1c2a5a1ec82f4a693b604452b574b2e7d149825680da4dfd8',
 'ExpectedOutcomeTest.test_rejects_invalid_frame_ownership_test_report': 'a6ca9130108f58a68029b1d48faceebc4db6d8c07e548248c5b3aa3f9bd1fc29',
 'ExpectedOutcomeTest.test_rejects_invalid_ipc_test_report': 'a78e5b42e2ac1fbae0cb12045fbd5e1e95b5e9273998fa0f165b81f7dbce6328',
 'ExpectedOutcomeTest.test_rejects_invalid_mmu_ready_report': '34c8a3733b66388115d5ac95e20efca07ad56a5b29af1d3951f9416a3a04b6e2',
 'ExpectedOutcomeTest.test_rejects_invalid_mmu_test_report': '68ec92e5b23e0034bea03ac5aac479bc5699562b073d4a46f3c7e1f587dbd20e',
 'ExpectedOutcomeTest.test_rejects_invalid_nested_trap_test_report': '3c4039218782747d476066ad6f7e2cef9d649f19ab3e16fe8ca9effb7c8c2211',
 'ExpectedOutcomeTest.test_rejects_invalid_object_model_test_report': '133239aec4f09c1d85b77b05560748d96bdc2e7808a21bd10791ca69a96f5d62',
 'ExpectedOutcomeTest.test_rejects_invalid_objects_ready_report': 'e77c7b050329419f23b86096a30472542ca3f93c0a70cf6d767e89870013fce1',
 'ExpectedOutcomeTest.test_rejects_invalid_scheduler_test_report': '03bfc65921e8247326d04f0bb272252b9fdc9e3b013fccac9fc9a1da3a9cfe9d',
 'ExpectedOutcomeTest.test_rejects_invalid_user_address_space_test_report': '65377e905fc791bc179f7eeed20708f93414fecd10b2921576e37014438f209e',
 'ExpectedOutcomeTest.test_rejects_invalid_user_execution_test_report': '746380d4f3e675aea0c2fc9d91773b8f4484d6e584009a961738afe1966aeedd',
 'ExpectedOutcomeTest.test_rejects_malformed_or_unterminated_frame_allocator_ready': 'd4d8b0993ad79d4c9bdc8131fd6781c7e953a9a57ffd83febb67026babe7a018',
 'ExpectedOutcomeTest.test_rejects_malformed_timer_interval': '15227ed02647becf8c08c20cff27b95572ea191cea9d670df351447b085288c1',
 'ExpectedOutcomeTest.test_rejects_malformed_timer_tick_count': '4ed29ab3c4891188bbc30541043c8407b9987121069945222857c61ddadfc78e',
 'ExpectedOutcomeTest.test_rejects_missing_or_duplicate_frame_allocator_test_report': '8dd7c95b2d035391681dd23cc4167a1cf99c8e885f47915b90b484779d162e69',
 'ExpectedOutcomeTest.test_rejects_missing_timer_test_report': '1cdb8a868bc62be7c1646f56cb17423b3d2888253ee381ca6d0f6a1b04f841b5',
 'ExpectedOutcomeTest.test_rejects_mmu_ready_after_target_outcome': '5cec99677ecab76ad76a2b027a56688c7f4562b237d7e3a3d513f612f3386c67',
 'ExpectedOutcomeTest.test_rejects_out_of_order_trap_recovery_pass': 'c2e8c0deab56146366e23fe19783e0e640574c31c120c19f82e8f82b7927c26e',
 'ExpectedOutcomeTest.test_rejects_panic_that_times_out': '7e913578a9e3a0b3281d7ff9b2b1988c78ec7da6f4d1b548c45cc9e5d0a9de27',
 'ExpectedOutcomeTest.test_rejects_panic_with_explicit_failure': '859af79f9324c55dc23e5e69c19a55c9752d81b031d3899189efa9585cf92870',
 'ExpectedOutcomeTest.test_rejects_timer_test_report_before_fdt_ready': 'b75256721356d7d0e2ceeaf0497c7d196513d734ce4a516f1d6982c5498a4c49',
 'ExpectedOutcomeTest.test_rejects_trap_context_for_direct_panic': '23ad46f8edf61dd40238787f8c77a94b342a8d071459683d2845ca5301cbf96d',
 'ExpectedOutcomeTest.test_rejects_trap_pass_before_fdt_ready': 'e043ce047dc09e735d881a6f88d57b1e7a493f839b0dcc04380499fc0b64fbe7',
 'ExpectedOutcomeTest.test_rejects_unterminated_final_panic_record': '90431e2cda79732172e3ea774c5a2176ec18f7d7c3198a9da40131d2861c70ce',
 'ExpectedOutcomeTest.test_rejects_unterminated_frame_allocator_test_report': 'aad6e98bc812e98059669e1e2cbf7fd431b897096e7b0e84f0a65adfe9699633',
 'ExpectedOutcomeTest.test_rejects_unterminated_timer_test_report': '4f5a82221029018fe6a61ad438b82d37ee5ed975cbef5d51cd7389b9cd63eafb',
 'ExpectedOutcomeTest.test_rejects_unterminated_trap_panic_context': '266477012e39eec00f6956a334538f486d8d0d4be6e9bfd53db68b801cfc0e32',
 'ExpectedOutcomeTest.test_rejects_wrong_trap_context_sepc': 'a59c636fbea367a6570e1831ce452a01d45b16f7f994fe43e2bad94fef91d06d',
 'ExpectedOutcomeTest.test_rejects_zero_or_mismatched_frame_allocator_counts': '089b6631ace9ca35f381c3b88d14750cef9ab4c5d17191c5de1cfe1a2c56e463',
 'ExpectedOutcomeTest.test_requires_exact_invalid_scheduler_context_report': '3f919761b057766f077451c43453e4e2421645a0db40b86d7587819f502d2a66',
 'ExpectedOutcomeTest.test_requires_exact_vm_self_fault_and_context': 'c2a72779399cf6bce3f8eff99415eb89bd381f40bb529891144b28d391fa993e',
 'ExpectedOutcomeTest.test_requires_expected_frame_allocator_growth': '8411f18f62071c13a344b2d9455b08258295ec1dc9a6f25b26ac4fbf067f7972',
 'ExpectedOutcomeTest.test_requires_patterns_in_order': '06c8e71f00ab5d4c0f7e774d70d342ea47c6e23785682835271bf02948f5b147',
 'NmSymbolTest.test_parses_exact_defined_symbol': 'b8ac930a68e0ad5de7739bff604842c73743e8935ba66ece23a2094e3197a918',
 'NmSymbolTest.test_rejects_duplicate_defined_symbol': '75adf2ad5bc917ef8e2e9f8ba2eed4c99e58b03a8f109f281858653db706f500',
 'QemuCommandTest.test_accepts_explicit_guest_memory_size': '7dd2156e2fe0d2560bffe163a4534ea741e89c6180d507af01310682426edd51',
 'QemuCommandTest.test_uses_explicit_single_hart_plic_machine': '4433a7368fc9ce882d6dd7f70f8be84dbf9bc0f7e30ae343f2f9cd6d0daa95de',
 'QemuSerialInputTest.test_closed_serial_input_pipe_returns_a_normal_result': 'e7724b33efddae43dafd638e9aa7c0558470e69b1777a02ce157e0ca8f4da7fd',
 'QemuSerialInputTest.test_default_execution_keeps_stdin_closed': '90b22985f5655a489a5c71c034c992765bc456ce099035b8ad5e3baae0fd5733',
 'QemuSerialInputTest.test_trigger_requires_an_exact_terminated_line': 'e67a24b22e46808b526074ab00e465ca4276dd59c318d587a2a24396359ee2c7',
 'QemuSerialInputTest.test_writes_serial_input_once_after_complete_trigger_line': '5baeb8acb1412ca7d1e27296a32f61040a2d7c4fb2166fbdc88a0d7102b70455',
 'SmokeClassificationTest.test_accepts_complete_fdt_event_stream': '646bbd5e9b92f8409fd7c7016e896eff9c542c28d483cc5f97cee62953df8e43',
 'SmokeClassificationTest.test_accepts_marker_followed_by_clean_exit': 'a46811fd85de0237b23e9f0673858ef72f67c7e4f2ea50df95e744ae6a4d98eb',
 'SmokeClassificationTest.test_rejects_empty_fdt_reservation_result': '14155c89e1d97671f1e29a2b0dbb8cf3e51f6cda3bb3ebb2b126d91c35c543ec',
 'SmokeClassificationTest.test_rejects_incomplete_fdt_event_stream': 'ffdd46929e15a43c337e645859af32e151b8a0f3898217288c2e1b247fbca2b4',
 'SmokeClassificationTest.test_rejects_malformed_duplicate_fdt_count_summary': 'da401c5d50874b4d15193eaad9f5d7e5399593cc990fe2ecb37945ea3b9147d3',
 'SmokeClassificationTest.test_rejects_malformed_fdt_range_event': '02d6d9fa4e56b2701f0e9e345639f5862c78fd517f326e98bb682ed3ce0405c5',
 'SmokeClassificationTest.test_rejects_marker_embedded_in_another_line': '40484e6ad6578527dc9c904be31c1f8109815d9584a2be0719848981ee57b7e7',
 'SmokeClassificationTest.test_reports_explicit_target_failure': '253d6e27aa28baffe9e30d73f25e357a65cd0eb81ee79f882071261a9014fc45',
 'SmokeClassificationTest.test_reports_kernel_panic': 'fe40c35216e6e9c9ce12c186766b687d4c0091dea8c9eba99a5626f1b550d6f2',
 'SmokeClassificationTest.test_reports_timeout_before_other_output_states': 'ab62c4d5751a5a168746ce61383903738a369a41515f80c19e08273bc8a59069',
 'SmokeClassificationTest.test_reports_unexpected_exit_without_marker': '120dd8808a799766b294fc87b7df6c5a92718c73a8797b0100302b8051d1d555',
 'SmokeClassificationTest.test_requires_every_expected_marker': '67db58312b530c3c5fbbb1458d4e7253ba7bc7592b49800dd05161454c649aa3',
 'TapOutputTest.test_pass_includes_machine_readable_outcome': '61b1a283f7b5fec65f180bd035fd29370cc0b1aa14832895bacb2deb55d87b5c'}

ORIGINAL_TEST_DATA = (('OBJECTS_READY_RECORD', 'eacc99eddb33ad26d2065d2f846d73cfc1871ff74478480c6687cfcc7b52d8ba'),
 ('PANIC_OUTPUT', 'ed8136345fbf40be65c1951ebbd77a544b6e0688c4f3cc1bed3bdec57c95f3b6'),
 ('TRAP_RECOVERY_OUTPUT', '1ab11ebb9e6d9e36b2af4671d42d82bc793e203392ad2e690a73032fa41518f6'),
 ('TIMER_TEST_OUTPUT', 'c822473f1c55b06f4845b88fd6b3eca9a5bc013eb3cb5cd1965cc8e6c29805dd'),
 ('FRAME_ALLOCATOR_READY_RECORD', '284c64ccb8ab0dbba4a4c42ab7effff276ba6f8de09b5fe24fdb207eeb6370ca'),
 ('FRAME_ALLOCATOR_TEST_PASS', '959808d9a4f841eb25c3ebf7d9923e8e86bf7890fc3d7fe6b0d29c7f23f86e2a'),
 ('MMU_READY_RECORD', 'b56030a3c36a84c6d05b369aa3317b9b28da85c8e668368a34c6c53c2d5bf58c'),
 ('MMU_TEST_PASS', 'd43acd58c63f01eeacb29d3498879014d0da81bcdf27d72284137fd04d725fe6'),
 ('FRAME_OWNERSHIP_READY_RECORD', 'd234960b3474adb749ab9e43154c04b2d6210b1ecfe5a5b6b365ee39ab7fe841'),
 ('FRAME_OWNERSHIP_TEST_PASS', '29d070d1b1e4e6bdfd2664ea74115b39811746b9effba92b77c395870e061343'),
 ('USER_ADDRESS_SPACE_TEST_PASS', 'd0677ff9c64cf5b72cb7573d7429f294db4063cf5ca586dc27b2a5a0f26f0f1e'),
 ('USER_EXECUTION_TEST_PASS', '633c8660712a0768223ff0c48a552eb94539eb153fc000010eb30e76b3404897'),
 ('SCHEDULER_TEST_PASS', 'e59387c4f97755ac6df5e65af1a6932d4435eadb04875784b1071c7c90d69d28'),
 ('FRAME_ALLOCATOR_OUTPUT', 'e2add5dc48981410fcfe4e4f30f2acb9f003e1e12ac084c358b3224ab754adef'),
 ('FRAME_ALLOCATOR_TEST_OUTPUT', 'db3bac4399be1e18deb89564bf90d005ed6c4eb829f9dd26b6a7df924cd6b9a2'),
 ('MMU_OUTPUT', '6ec0718b7325510fd6e9e7bfca14435a5e62c3cdce1fa2ec151875e7db7dc189'),
 ('MMU_TEST_OUTPUT', '5ef10c174eb29d88c5c2d13d71ed4c8560828eebb9eaeb409e3da3733825acb5'),
 ('FRAME_OWNERSHIP_OUTPUT', '8fc23d9e5e09fa96bfb4ed7a9c6fc926cc3442e11746d64ad4e21c7b7c17a8ff'),
 ('FRAME_OWNERSHIP_TEST_OUTPUT', '95676f0c8e89031cadd4f8d7460efe28430160ab9267de2ffeb441eae2393ec1'),
 ('USER_ADDRESS_SPACE_TEST_OUTPUT', '287649d825b827c4b9a024e8b2dcdf67d94f8377e6e76b7e8ca90024255a430c'),
 ('USER_EXECUTION_TEST_OUTPUT', '7cf3ed21355e1e8188d18cdf38f197cc591c0b2b69f6953cb90bd09bdbf9387e'),
 ('SCHEDULER_TEST_OUTPUT', 'ff4663290c9f704a30eab92a95fc9c1786bdd32c839234408f6a6a42ee9ef832'),
 ('ENDPOINT_TEST_PASS', '4cde5b191dcde0b11ee709c563b35328c82806d96e771f32b8abac170ad34104'),
 ('ENDPOINT_TEST_OUTPUT', 'cd7b4f39dad0c3a0e0e0397aeeb1c6f5369f2c18313b598eddef11534c3c1d2c'),
 ('IPC_TEST_PASS', '52eaf432595de2a48ecb0104aefae6a506b72c750916f8afbb0b85d98df524d7'),
 ('IPC_ADDRESS_SPACES_PASS', 'd7bb5787f76653005767fcf4676036f101e55147c79b0dd91ee9409c7c98f0eb'),
 ('IPC_TEST_OUTPUT', '89cf4fcb855b13a47c03cac53142314ba767e27e3a5f314d517e7d198d76c6b8'),
 ('OBJECT_MODEL_TEST_PASS', 'ff2d71b5e3b663f794f881cebf63d072f927c6a5348a95f80955b6bdd5868018'),
 ('NESTED_TRAP_TEST_PASS', '038015bd772837898ed11b535468953f85dbbfefa684864df64a00935cd9b2cd'),
 ('OBJECT_MODEL_TEST_OUTPUT', 'b554a40c0cf1a3dcd101db48589886376f936e7ed66375677cf4f22ddfdf04cc'),
 ('NESTED_TRAP_TEST_OUTPUT', '037093d4aa6681379e19f6872c550a1705292e4c5148a41334293f43c4746ef2'),
 ('TRAP_CONTEXT_RECORD', 'd1b534782d24e6100cdf6d59ad22aa62b8da196587db9e63d774a0f4882e6ac4'),
 ('TRAP_PANIC_OUTPUT', '57dc75719f5b930c0544a0f3bcd0d6e254b8aba2b65bb875e6efcb13eb31d7ec'),
 ('SCHEDULER_INVALID_DIAGNOSTIC', 'f4a3ddbb7ebc48983e974bb64482b19fc8c69cbd6366e85562ee5c79767ed9f5'),
 ('SCHEDULER_INVALID_CONTEXT_RECORD', 'e62666ea1fc73a13c2fba604ff89f6c0fe4dde7e61d7144fe6a81f827db9cbf8'),
 ('SCHEDULER_INVALID_OUTPUT', 'c17b530971d86726361c3d1a4a9a61c25b210c229302d1691090df01165ec2c9'))

BASE_CMAKE_DIGEST = '730a6dd6e29ff12cf191d3040ae607026da790be7945c94a6966c24f03c42fab'

ORIGINAL_CONTEXT_AST = 'a9982ebca20b8c311a99db5027ab5f13eafaa603379983e5bc255a7278c26b6c'

ORIGINAL_PLANNER_AST = 'eb053b1f9026e6783117ab1272450017cfd8aaeb77edbae787fc6eceaac1533a'

PROTECTED_SOURCE_HASHES = {'tests/host/c_source_records.py': 'efab84d99e3d5c49fc416a161816431b9e54615ac23af9aef3e9f0e995bc2e0d',
 'tools/validation_build_impact.py': '24e9fabf4a4750c56508a94c26a37e398edc49a75c8d6b8b2efdeeee01c0d270',
 'tools/validation_build_manifest.py': 'dc723095d1c30c3501e8273affd6575c2d592772150a065a5dda8644daf282e3',
 'tools/validation_evidence.py': 'd14cdb1ecf86ec6096e4e4bb37377a821597cce383ef48478c1e341710a3d34d',
 'tools/validation_execution.py': '17e2e475894978291c087e66fb800637a7939e99b52b412eec55cd98c0f3f80d',
 'tools/validation_inputs.py': 'ff5f513282f94dfbe106096ab5c21d888b3af33cd995c36355426aea1068b4e0'}

ORIGINAL_RETAINED_GATE_ASTS = {'tests/host/test_endpoint_source_layout.py': '746b2ffbe7c9aa6e5661db879f5c04b5e3f7a7a53380a3fd83b8e354f77c58d5',
 'tests/host/test_frame_ownership_source_layout.py': '06ba5f76b582a593068d287e01f25166bdc60de89d4d944d32118b6805de1da6',
 'tests/host/test_grant_data_plane_source_layout.py': '8ce19fa5276d0b3965d21dfaa100fe180f97fba5c756a5be471897a29cc1404d',
 'tests/host/test_kernel_address_space_source_layout.py': 'b61f3a6467db208fa1f581028eb833b14497ad4f313daafff1fde447d5744997',
 'tests/host/test_kernel_object_source_layout.py': 'ef0cadb4848b48df9c02aeb38cc0961528e623e03895cbce229d7a9fc385170c',
 'tests/host/test_run_qemu_smoke.py': '1035ef3996abb56e2d51f4a028ec3bb060c443b6988a4e602cf358d00a83d20c',
 'tests/host/test_trap_syscall_source_layout.py': 'd24ae2c02e042f357906374bf865c854fe5476ae3ad0b6ca53e5cf863378d1ff',
 'tests/host/test_tty_source_layout.py': '22678dea552a5889a017d652a0de1a5bfa0eb5ac017cab991700700ebcd6ac03'}

INITIAL_SOURCE_OWNER_MIGRATION = (
    *("FunctionDef:" + name for name in ORIGINAL_FUNCTIONS),
    *("ClassDef:" + name for name in ORIGINAL_CLASSES),
    *("Assign:" + name for name in ORIGINAL_GLOBAL_NAMES),
    *("TestMethod:" + name for name in ORIGINAL_TEST_METHODS),
    *("TestAssign:" + name for name, _ in ORIGINAL_TEST_DATA),
)
COMPLETED_SOURCE_NODES = {
    "FunctionDef:build_qemu_command", "FunctionDef:_split_output_records",
    *("FunctionDef:" + name for name in FUNCTION_OWNERS[2]),
    *("FunctionDef:" + name for name in FUNCTION_OWNERS[3]),
    *("FunctionDef:" + name for name in FUNCTION_OWNERS[4]),
    *("FunctionDef:" + name for name in FUNCTION_OWNERS[5]),
    *("FunctionDef:" + name for name in FUNCTION_OWNERS[6]),
    *("FunctionDef:" + name for name in FUNCTION_OWNERS[7]),
    *("FunctionDef:" + name for name in FUNCTION_OWNERS[8]),
    *("FunctionDef:" + name for name in FUNCTION_OWNERS[9]),
    *("ClassDef:" + name for name in ORIGINAL_CLASSES),
    *("Assign:" + name for name in ORIGINAL_GLOBAL_NAMES),
    *("TestMethod:" + name for name in ORIGINAL_TEST_METHODS),
    *("TestAssign:" + name for name, _ in ORIGINAL_TEST_DATA),
}
SOURCE_OWNER_MIGRATION = ()
LONG_TASK_MIGRATION = ()


def source(path):
    return (ROOT / path).read_text()


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def ast_digest(node):
    return digest(ast.dump(node, include_attributes=False).encode())


def definitions(text):
    return {
        node.name: node for node in ast.parse(text).body
        if isinstance(node, (ast.FunctionDef, ast.ClassDef))
    }


def node_source(text, node):
    start = min([node.lineno, *[value.lineno for value in getattr(node, "decorator_list", ())]])
    return "".join(text.splitlines(keepends=True)[start - 1:node.end_lineno])


def raw_module_fragments(text, layout):
    expected = {key: ast_hash for key, _, _, ast_hash in layout}
    by_ast = {value: key for key, value in expected.items() if key.startswith(("Import:", "ImportFrom:", "If:"))}
    observed = {}
    for node in ast.parse(text).body:
        if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
            key = type(node).__name__ + ":" + node.name
        elif isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            key = "Assign:" + node.targets[0].id
        else:
            key = by_ast.get(ast_digest(node))
        if key is None or key not in expected or key in observed:
            raise AssertionError("every actual original node has one verified source owner")
        observed[key] = (node_source(text, node), ast_digest(node))
    return observed


def assemble_raw_module(header, layout, fragments, tail):
    result = []
    if set(fragments) != {key for key, _, _, _ in layout}:
        raise AssertionError("the complete original module has exact node membership")
    for index, (key, gap, raw_hash, ast_hash) in enumerate(layout):
        raw, actual_ast = fragments[key]
        if digest(raw.encode()) != raw_hash or actual_ast != ast_hash:
            raise AssertionError("actual node bytes/AST differ from immutable source: " + key)
        if index and gap.strip():
            raise AssertionError("module transport inserts only original whitespace, never an unchecked old body")
        result.extend((header if index == 0 else gap, raw))
    result.append(tail)
    return "".join(result)


def original_destination(key):
    kind, name = key.split(":", 1)
    if kind == "FunctionDef":
        matches = [PRODUCTION_PATHS[owner] for owner, names in FUNCTION_OWNERS.items() if name in names]
        if len(matches) != 1:
            raise AssertionError("an original function has exactly one design owner")
        return matches[0]
    if kind in ("ClassDef", "Assign"):
        return PRODUCTION_PATHS[1]
    return RUNNER_PATH


def facade_alias_bindings(text):
    original_names = {
        key.split(":", 1)[1]: key for key, _, _, _ in ORIGINAL_NODE_LAYOUT
        if key.startswith(("FunctionDef:", "ClassDef:", "Assign:"))
    }
    modules = {path[:-3].replace("/", "."): path for path in PRODUCTION_PATHS[1:]}
    bindings = {}
    for node in ast.parse(text).body:
        if not isinstance(node, ast.ImportFrom) or node.module not in modules:
            continue
        if node.level:
            raise AssertionError("facade aliases use their exact absolute reviewed owner")
        for alias in node.names:
            key = original_names.get(alias.name)
            if alias.asname is not None or key is None or key in bindings:
                raise AssertionError("facade aliases do not rename, duplicate or invent original bindings")
            if original_destination(key) != modules[node.module]:
                raise AssertionError("facade alias points to the wrong original responsibility owner")
            bindings[key] = modules[node.module]
    return bindings


def validate_owner_import(owner, node, migrating=None):
    migrating = SOURCE_OWNER_MIGRATION if migrating is None else migrating
    expected = {
        ast_digest(ast.parse(statement).body[0]) for statement in OWNER_STDLIB_IMPORTS[owner]
    }
    if ast_digest(node) in expected:
        return
    modules = {path[:-3].replace("/", "."): index for index, path in enumerate(PRODUCTION_PATHS) if index}
    if (
        not isinstance(node, ast.ImportFrom) or node.level
        or node.module not in modules or modules[node.module] not in OWNER_PRIVATE_DIRECTIONS[owner]
    ):
        raise AssertionError("new owner imports only its exact necessary standard/private responsibilities")
    symbols = {
        key.split(":", 1)[1]: key for key, _, _, _ in ORIGINAL_NODE_LAYOUT
        if key.startswith(("FunctionDef:", "ClassDef:", "Assign:"))
    }
    for alias in node.names:
        key = symbols.get(alias.name)
        if (
            alias.asname is not None or key is None or key in migrating
            or original_destination(key) != PRODUCTION_PATHS[modules[node.module]]
        ):
            raise AssertionError("cross-owner source bindings keep exact original names and actual single owners")


def phase_signature(name, parameters):
    return ast.parse("def " + name + "(*, " + ", ".join(parameters) + "):\n    pass\n").body[0].args


def phase_call(name, parameters, boolean):
    call = name + "(" + ", ".join(value + "=" + value for value in parameters) + ")"
    return ast.parse("if not " + call + ":\n    return False\n" if boolean else call).body[0]


def restore_phase_function(text, function, phases, plan, count, gaps):
    nodes = definitions(text)
    if len(function.body) != len(plan):
        raise AssertionError("outlined root keeps its exact original statements and phase order")
    restored = {}
    by_name = {row[0]: row for row in phases}
    for statement, entry in zip(function.body, plan):
        if isinstance(entry, int):
            restored[entry] = node_source(text, statement)
            continue
        name, start, end, parameters, boolean = by_name[entry]
        helper = nodes.get(name)
        if (
            not isinstance(helper, ast.FunctionDef) or helper.decorator_list or helper.returns is not None
            or getattr(helper, "type_params", ())
            or ast_digest(helper.args) != ast_digest(phase_signature(name, parameters))
            or ast_digest(statement) != ast_digest(phase_call(name, parameters, boolean))
            or len(helper.body) != end - start + int(boolean)
        ):
            raise AssertionError("phase keeps exact same-name bindings, scope and continuation: " + name)
        if boolean and ast_digest(helper.body[-1]) != ast_digest(ast.parse("return True").body[0]):
            raise AssertionError("boolean phase has only its exact successful continuation")
        for index, original in enumerate(helper.body[:end - start], start):
            if index in restored:
                raise AssertionError("no original statement is duplicated by phase transport")
            restored[index] = node_source(text, original)
    if set(restored) != set(range(count)) or any(value.strip() for value in gaps.values()):
        raise AssertionError("phase inverse covers only complete actual statements and frozen whitespace")
    lines = text.splitlines(keepends=True)
    header = "".join(lines[function.lineno - 1:function.body[0].lineno - 1])
    return header + "".join(gaps.get(index, "") + restored[index] for index in range(count))


def restore_main_function(text, function):
    helper = definitions(text).get("_matches_observed_run")
    signature = ast.parse(
        "def _matches_observed_run(" + ", ".join(MATCHING_CALL_ARGUMENTS) + "):\n    pass\n"
    ).body[0].args
    if (
        not isinstance(helper, ast.FunctionDef) or helper.decorator_list or helper.returns is not None
        or getattr(helper, "type_params", ())
        or ast_digest(helper.args) != ast_digest(signature)
        or len(helper.body) != 1 or not isinstance(helper.body[0], ast.Return)
        or not isinstance(helper.body[0].value, ast.Call)
        or not isinstance(helper.body[0].value.func, ast.Name)
        or helper.body[0].value.func.id != "matches_expected_result"
    ):
        raise AssertionError("main forwards its one actual matching call through six unchanged local values")
    assignments = [
        node for node in ast.walk(function) if isinstance(node, ast.Assign)
        and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name) and node.targets[0].id == "accepted"
        and isinstance(node.value, ast.Call)
    ]
    expected = ast.parse(
        "accepted = _matches_observed_run(" + ", ".join(MATCHING_CALL_ARGUMENTS) + ")"
    ).body[0]
    if len(assignments) != 1 or ast_digest(assignments[0]) != ast_digest(expected):
        raise AssertionError("main preserves the acceptance assignment before the original serial gate")
    actual = node_source(text, helper.body[0]).splitlines(keepends=True)
    if not actual[0].startswith("    return "):
        raise AssertionError("matching-call transport keeps ordinary return formatting")
    expanded = "        accepted = " + actual[0][len("    return "):]
    expanded += "".join("    " + line for line in actual[1:])
    original = node_source(text, assignments[0])
    root = node_source(text, function)
    if root.count(original) != 1:
        raise AssertionError("main contains exactly one transported acceptance expression")
    return root.replace(original, expanded, 1)


def restored_function_source(text, function):
    if function.name not in OUTLINED_FUNCTIONS:
        return node_source(text, function)
    if function.name == "main":
        return restore_main_function(text, function)
    return restore_phase_function(
        text, function, OUTLINE_PHASES[function.name], OUTLINE_ROOT_PLANS[function.name],
        ORIGINAL_STATEMENT_COUNTS[function.name], ORIGINAL_STATEMENT_GAPS[function.name],
    )


def test_case_methods(owner):
    return {
        mixin: (aggregate, tuple(ORIGINAL_TEST_METHOD_ORDER[aggregate][index] for index in indices))
        for mixin, aggregate, indices in TEST_CASE_LAYOUT[owner]
    }


def case_owner_methods(text, owner):
    expected = test_case_methods(owner)
    methods = {}
    imports = set()
    references = set()
    classes = set()
    fixtures = {name for name, _ in ORIGINAL_TEST_DATA}
    for node in ast.parse(text).body:
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            if isinstance(node, ast.Import) and len(node.names) == 1 and node.names[0].name in ("contextlib", "io", "sys"):
                names = [node.names[0].name]
            elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module == "tools" and [value.name for value in node.names] == ["run_qemu_smoke"]:
                names = ["run_qemu_smoke"]
            elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module == "tests.host.qemu_smoke_test_fixture":
                names = [value.name for value in node.names]
                if not set(names) <= fixtures:
                    raise AssertionError("test case imports only original independent fixture names")
            else:
                raise AssertionError("test cases keep only necessary standard, runner and independent fixture imports")
            if any(value.asname is not None for value in node.names) or len(set(names)) != len(names) or imports & set(names):
                raise AssertionError("test imports do not rename or duplicate bindings")
            imports.update(names)
            continue
        if (
            not isinstance(node, ast.ClassDef) or node.name not in expected or node.name in classes
            or node.bases or node.keywords or node.decorator_list or getattr(node, "type_params", ())
            or not all(isinstance(value, ast.FunctionDef) for value in node.body)
        ):
            raise AssertionError("case owners contain only their plain original-method mixins, never another loader")
        classes.add(node.name)
        aggregate, names = expected[node.name]
        if tuple(value.name for value in node.body) != names:
            raise AssertionError("each plain mixin retains its exact original method order")
        for method in node.body:
            key = aggregate + "." + method.name
            if (
                key in methods or ast_digest(method) != ORIGINAL_TEST_METHODS[key]
                or digest(node_source(text, method).encode()) != ORIGINAL_TEST_METHOD_RAW[key]
            ):
                raise AssertionError("test method retains its complete independent raw body/signature/AST: " + key)
            methods[key] = method
            references.update(value.id for value in ast.walk(method) if isinstance(value, ast.Name) and isinstance(value.ctx, ast.Load))
    if classes != set(expected) or imports != references & (fixtures | {"contextlib", "io", "sys", "run_qemu_smoke"}):
        raise AssertionError("all and only required case classes/import bindings have real owners")
    return methods


def restored_test_ast(texts=None):
    texts = {path: source(path) for path in TEST_PATHS} if texts is None else texts
    data = ast.parse(texts[TEST_PATHS[1]]).body
    if len(data) != len(ORIGINAL_TEST_DATA):
        raise AssertionError("independent fixture owner keeps exactly all original assignments")
    for node, (name, expected) in zip(data, ORIGINAL_TEST_DATA):
        if (
            not isinstance(node, ast.Assign) or len(node.targets) != 1 or not isinstance(node.targets[0], ast.Name)
            or node.targets[0].id != name or ast_digest(node) != expected
            or digest(node_source(texts[TEST_PATHS[1]], node).encode()) != ORIGINAL_TEST_DATA_RAW[name]
        ):
            raise AssertionError("fixture bytes/AST/order stay independent of production report tables: " + name)
    methods = {}
    for owner in TEST_CASE_LAYOUT:
        selected = case_owner_methods(texts[TEST_PATHS[owner]], owner)
        if methods.keys() & selected.keys():
            raise AssertionError("test methods have one actual owner")
        methods.update(selected)
    if set(methods) != set(ORIGINAL_TEST_METHODS):
        raise AssertionError("all 85 original methods remain present exactly once")
    root = ast.parse(texts[TEST_PATH])
    original_imports = {ast_digest(node) for node in ast.parse(
        "import contextlib\nimport io\nimport sys\nimport unittest\nfrom tools import run_qemu_smoke\n"
    ).body}
    modules = {
        TEST_PATHS[owner][:-3].replace("/", "."): tuple(test_case_methods(owner))
        for owner in TEST_CASE_LAYOUT
    }
    observed = set()
    projected = []
    for node in root.body:
        if isinstance(node, ast.ImportFrom) and node.module in (*modules, "tests.host.qemu_smoke_test_fixture"):
            names = tuple(alias.name for alias in node.names)
            expected = modules.get(node.module, tuple(name for name, _ in ORIGINAL_TEST_DATA))
            if node.level or names != expected or any(alias.asname is not None for alias in node.names) or node.module in observed:
                raise AssertionError("test facade uses only exact explicit fixture/mixin aliases")
            observed.add(node.module)
            if node.module == "tests.host.qemu_smoke_test_fixture":
                projected.extend(data)
            continue
        if isinstance(node, ast.ClassDef) and node.name in AGGREGATE_MIXINS:
            expected = ast.parse(
                "class " + node.name + "(" + ", ".join((*AGGREGATE_MIXINS[node.name], "unittest.TestCase")) + "):\n    pass\n"
            ).body[0]
            if ast_digest(node) != ast_digest(expected) or node.name in observed:
                raise AssertionError("six original aggregate identities keep exact plain-mixin bases and no local overrides")
            observed.add(node.name)
            restored = copy.deepcopy(node)
            restored.bases = ast.parse("class Original(unittest.TestCase):\n    pass\n").body[0].bases
            restored.body = [methods[node.name + "." + name] for name in ORIGINAL_TEST_METHOD_ORDER[node.name]]
            projected.append(restored)
            continue
        if isinstance(node, (ast.Import, ast.ImportFrom)) and ast_digest(node) not in original_imports:
            raise AssertionError("facade retains only original standard/runner bindings and exact case aliases")
        projected.append(node)
    if observed != set(modules) | {"tests.host.qemu_smoke_test_fixture"} | set(AGGREGATE_MIXINS):
        raise AssertionError("every test owner is wired through the one original discovery facade")
    result = ast.Module(body=projected, type_ignores=root.type_ignores)
    if ast_digest(result) != ORIGINAL_TEST_AST:
        raise AssertionError("the complete actual original test module AST must restore before acceptance")
    return result


def suite_ids(suite):
    return tuple(
        identity for value in suite
        for identity in (suite_ids(value) if isinstance(value, unittest.TestSuite) else (value.id(),))
    )


def alias_rows():
    return ((RUNNER_PATH, PRODUCTION_PATHS[1:]), (TEST_PATH, TEST_PATHS[1:]))


def qemu_planner_projection(tree):
    assignments = {
        node.targets[0].id: node for node in tree.body
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name)
    }
    if "PATH_CLASSIFICATION_EQUIVALENTS" not in assignments or "HOST_UNIT_ONLY_INPUTS" not in assignments:
        raise AssertionError("complete original planner metadata declarations remain")
    aliases = assignments["PATH_CLASSIFICATION_EQUIVALENTS"].value
    host = assignments["HOST_UNIT_ONLY_INPUTS"].value
    rows, paths = ast.literal_eval(aliases), ast.literal_eval(host)
    expected = dict(alias_rows())
    selected = {old: values for old, values in rows if old in expected}
    if not selected and STRUCTURAL_TEST_PATH not in paths:
        return tree
    if selected != expected or sum(old in expected for old, _ in rows) != 2 or paths.count(STRUCTURAL_TEST_PATH) != 1:
        raise AssertionError("QEMU transport adds only its two exact families and one host-only structural input")
    aliases.elts = [node for node in aliases.elts if ast.literal_eval(node)[0] not in expected]
    host.elts = [node for node in host.elts if ast.literal_eval(node) != STRUCTURAL_TEST_PATH]
    return tree


def restored_runner_source(text=None, owners=None):
    root_text = source(RUNNER_PATH) if text is None else text
    texts = {RUNNER_PATH: root_text}
    if owners is not None:
        texts.update(owners)
    else:
        texts.update({path: source(path) for path in PRODUCTION_PATHS[1:] if (ROOT / path).is_file()})
    layout = {key: (gap, raw_hash, ast_hash) for key, gap, raw_hash, ast_hash in ORIGINAL_NODE_LAYOUT}
    original_by_ast = {
        ast_hash: key for key, _, _, ast_hash in ORIGINAL_NODE_LAYOUT
        if key.startswith(("Import:", "ImportFrom:", "If:"))
    }
    bootstrap = ast_digest(ast.parse(
        "sys.path.insert(0, str(Path(__file__).resolve().parent.parent))"
    ).body[0])
    aliases = facade_alias_bindings(root_text)
    fragments = {}
    bootstraps = 0
    phase_owners = {
        row[0]: original_destination("FunctionDef:" + name)
        for name in OUTLINED_FUNCTIONS if name in OUTLINE_PHASES for row in OUTLINE_PHASES[name]
    }
    if "main" in OUTLINED_FUNCTIONS:
        phase_owners["_matches_observed_run"] = PRODUCTION_PATHS[9]
    observed_phases = set()
    for path, current in texts.items():
        module = ast.parse(current)
        for node in module.body:
            key = None
            if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
                key = type(node).__name__ + ":" + node.name
            elif isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
                key = "Assign:" + node.targets[0].id
            elif path == RUNNER_PATH:
                key = original_by_ast.get(ast_digest(node))
            if key in layout:
                expected_path = RUNNER_PATH if key in SOURCE_OWNER_MIGRATION else original_destination(key)
                if path != expected_path or key in fragments:
                    raise AssertionError("actual original node has its exact current single owner: " + key)
                raw = restored_function_source(current, node) if isinstance(node, ast.FunctionDef) else node_source(current, node)
                fragments[key] = (raw, ast_digest(ast.parse(raw).body[0]))
                continue
            if isinstance(node, ast.FunctionDef) and node.name in phase_owners:
                if phase_owners[node.name] != path or node.name in observed_phases:
                    raise AssertionError("each ordinary phase has exactly its original responsibility owner")
                observed_phases.add(node.name)
                continue
            if path == RUNNER_PATH and ast_digest(node) == bootstrap:
                bootstraps += 1
                continue
            if path == RUNNER_PATH and isinstance(node, ast.ImportFrom) and node.module in {
                item[:-3].replace("/", ".") for item in PRODUCTION_PATHS[1:]
            }:
                if bootstraps != 1:
                    raise AssertionError("the sole repository import bootstrap precedes every package alias")
                continue
            if path != RUNNER_PATH and isinstance(node, (ast.Import, ast.ImportFrom)):
                validate_owner_import(PRODUCTION_PATHS.index(path), node)
                continue
            if path != RUNNER_PATH and len(module.body) == 1 and isinstance(node, ast.Expr) and isinstance(node.value, ast.Constant) and isinstance(node.value.value, str):
                continue
            raise AssertionError("unreviewed module-level execution or unknown source node: " + path)
    moved = {key for key in layout if key.startswith(("FunctionDef:", "ClassDef:", "Assign:")) and key not in SOURCE_OWNER_MIGRATION}
    if set(aliases) != moved or bootstraps != bool(moved):
        raise AssertionError("each moved original has its one exact facade alias/bootstrap and no extra binding")
    if observed_phases != set(phase_owners):
        raise AssertionError("all required ordinary phases have real actual definitions")
    root_nodes = ast.parse(root_text).body
    first = root_nodes[0]
    header = "".join(root_text.splitlines(keepends=True)[:first.lineno - 1])
    return assemble_raw_module(header, ORIGINAL_NODE_LAYOUT, fragments, "")


def context_guard_statements(statements):
    original = ast.parse(
        'if workflow == "test-qemu-smoke" and _file_digest(root / "tools/run_qemu_smoke.py") != QEMU_RUNNER_DIGEST:\n'
        '    raise UnsupportedContext("QEMU argument builder is outside the reviewed closure")\n'
    ).body[0]
    early = ast.parse(
        'if workflow == "test-qemu-smoke":\n    _validate_qemu_runner_family(root)\n'
    ).body[0]
    late = ast.parse(
        'if workflow == "test-qemu-smoke":\n    _validate_qemu_runner_namespace(root, tools)\n'
    ).body[0]
    early_positions = [index for index, node in enumerate(statements) if ast_digest(node) == ast_digest(early)]
    late_positions = [index for index, node in enumerate(statements) if ast_digest(node) == ast_digest(late)]
    if len(early_positions) != 1 or len(late_positions) != 1:
        raise AssertionError("one exact early source guard and pre-QEMU namespace guard are required")
    index = late_positions[0]
    if (
        index == 0 or index + 1 >= len(statements)
        or not isinstance(statements[index - 1], ast.Assign)
        or not any(isinstance(target, ast.Name) and target.id == "tools" for target in statements[index - 1].targets)
        or not isinstance(statements[index + 1], ast.Assign)
        or not any(isinstance(target, ast.Name) and target.id == "qemu" for target in statements[index + 1].targets)
        or early_positions[0] >= index
    ):
        raise AssertionError("namespace closure follows admitted workflow-tool resolution and precedes QEMU metadata")
    return [
        original if position == early_positions[0] else node
        for position, node in enumerate(statements) if position != late_positions[0]
    ]


def context_declaration_projection(text):
    restored = restored_runner_source()
    if digest(restored.encode()) != ORIGINAL_RUNNER_DIGEST:
        raise AssertionError("the complete original runner must reconstruct before any current declaration changes")
    tree = ast.parse(text)
    assignments = {
        node.targets[0].id: node for node in tree.body
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name)
    }
    root_digest = ast.literal_eval(assignments["QEMU_RUNNER_DIGEST"].value)
    if root_digest != digest(source(RUNNER_PATH).encode()):
        raise AssertionError("the admitted facade digest identifies actual source bytes")
    mapping = assignments["QEMU_RUNNER_SOURCE_DIGESTS"].value
    if not isinstance(mapping, ast.Dict) or len(mapping.keys) != len(PRODUCTION_PATHS):
        raise AssertionError("the declaration contains exactly the ten reviewed code members")
    paths = [ast.literal_eval(key) for key in mapping.keys]
    if tuple(paths) != PRODUCTION_PATHS:
        raise AssertionError("code-family declaration membership and order are independently fixed")
    for path, value in zip(paths, mapping.values):
        if isinstance(value, ast.Name):
            if path != RUNNER_PATH or value.id != "QEMU_RUNNER_DIGEST":
                raise AssertionError("only the original facade digest has its existing named declaration")
            actual = root_digest
        else:
            actual = ast.literal_eval(value)
        if actual != digest(source(path).encode()):
            raise AssertionError("a reviewed member digest must identify its actual full bytes")
    helpers = {"_validate_qemu_runner_family", "_validate_qemu_runner_namespace"}
    if {node.name for node in tree.body if isinstance(node, ast.FunctionDef)} & helpers != helpers:
        raise AssertionError("both narrowly reviewed source/loader predicates remain present")
    collect = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "collect")
    tries = [node for node in collect.body if isinstance(node, ast.Try)]
    if len(tries) != 1:
        raise AssertionError("the original complete collector failure boundary remains intact")
    tries[0].body = context_guard_statements(tries[0].body)
    assignments["QEMU_RUNNER_DIGEST"].value = ast.Constant(value=ORIGINAL_RUNNER_DIGEST)
    removed = {"QEMU_RUNNER_SOURCE_DIGESTS", "QEMU_LOADER_PROBE"}
    tree.body = [
        node for node in tree.body
        if not isinstance(node, ast.FunctionDef) or node.name not in helpers
    ]
    tree.body = [
        node for node in tree.body
        if not isinstance(node, ast.Assign) or not any(
            isinstance(target, ast.Name) and target.id in removed for target in node.targets
        )
    ]
    return tree


class QemuRunnerSourceLayoutTest(unittest.TestCase):
    def test_facade_bootstrap_must_precede_every_package_alias(self):
        current = source(RUNNER_PATH)
        bootstrap = "sys.path.insert(0, str(Path(__file__).resolve().parent.parent))"
        self.assertEqual(1, current.count(bootstrap))
        changed = current.replace(bootstrap, "", 1) + "\n" + bootstrap + "\n"
        with self.assertRaises(AssertionError):
            restored_runner_source(changed)

    def test_direct_cli_help_preserves_import_bootstrap_outside_repository(self):
        import subprocess
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                [sys.executable, "-B", str(ROOT / RUNNER_PATH), "--help"], cwd=directory,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False,
            )
        self.assertEqual((0, ""), (result.returncode, result.stderr))
        self.assertIn("Run the deterministic micros QEMU boot smoke test.", result.stdout)

    def test_planner_families_keep_all_four_original_metadata_dimensions_and_plans(self):
        from tools import validation_plan as planner

        aliases = dict(planner.PATH_CLASSIFICATION_EQUIVALENTS)
        for old, paths in alias_rows():
            self.assertEqual(paths, aliases.get(old), old)
            self.assertTrue(planner.requires_full_tier([old]))
            self.assertEqual(38, len(planner.affected_workflows([old])))
            self.assertEqual([], planner.affected_slow_models([old]))
            self.assertEqual([], planner.affected_image_workflows([old]))
            for path in paths:
                self.assertEqual(planner.requires_full_tier([old]), planner.requires_full_tier([path]))
                for classify in (planner.affected_workflows, planner.affected_slow_models, planner.affected_image_workflows):
                    self.assertEqual(classify([old]), classify([path]), path + ":" + classify.__name__)
                for tier in ("fast", "pr", "full"):
                    self.assertEqual(planner.plan([old], tier, "main"), planner.plan([path], tier, "main"))
        self.assertEqual(1, planner.HOST_UNIT_ONLY_INPUTS.count(STRUCTURAL_TEST_PATH))
        self.assertFalse(planner.requires_full_tier([STRUCTURAL_TEST_PATH]))
        for classify in (planner.affected_workflows, planner.affected_slow_models, planner.affected_image_workflows):
            self.assertEqual([], classify([STRUCTURAL_TEST_PATH]))
        self.assertEqual(
            [planner.UNIT_FAST, *planner.diff_commands("main")], planner.plan([STRUCTURAL_TEST_PATH], "fast", "main"),
        )

    def test_metadata_inverse_retains_old_rows_algorithms_and_exact_original_planner_ast(self):
        baseline = (
            "PATH_CLASSIFICATION_EQUIVALENTS = (('old', ('old_alias',)),)\n"
            "HOST_UNIT_ONLY_INPUTS = ('old_host',)\n"
            "def algorithm(value):\n    return value + 1\n"
        )
        rows = (("old", ("old_alias",)), *alias_rows())
        paths = ("old_host", STRUCTURAL_TEST_PATH)
        current = (
            "PATH_CLASSIFICATION_EQUIVALENTS = " + repr(rows) + "\n"
            "HOST_UNIT_ONLY_INPUTS = " + repr(paths) + "\n"
            "def algorithm(value):\n    return value + 1\n"
        )
        project = lambda text: ast_digest(qemu_planner_projection(ast.parse(text)))
        self.assertEqual(ast_digest(ast.parse(baseline)), project(current))
        self.assertEqual(ast_digest(ast.parse(baseline)), project(baseline))
        for changed in (
            current.replace("qemu_smoke_records.py", "qemu_smoke_other.py"),
            current.replace("tests/host/qemu_smoke_test_fixture.py", "tests/host/other_fixture.py"),
            current.replace(STRUCTURAL_TEST_PATH, "tests/host/other_gate.py"),
            current.replace(repr(rows), repr((*rows, rows[1]))),
            current.replace(repr(paths), repr((*paths, STRUCTURAL_TEST_PATH))),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                project(changed)
        self.assertNotEqual(project(baseline), project(current.replace("value + 1", "value - 1")))
        self.assertNotEqual(project(baseline), project(current.replace("old_alias", "changed_old_alias")))
        self.assertEqual(ORIGINAL_PLANNER_AST, project(source("tools/validation_plan.py")))

    def test_build_recipe_and_existing_shared_inspection_execution_libraries_are_unchanged(self):
        self.assertEqual(BASE_CMAKE_DIGEST, digest(source("CMakeLists.txt").encode()))
        for path, expected in PROTECTED_SOURCE_HASHES.items():
            self.assertEqual(expected, digest((ROOT / path).read_bytes()), path)

    def test_all_test_owners_preserve_complete_original_ast_and_single_discovery(self):
        for path in TEST_PATHS:
            self.assertTrue((ROOT / path).is_file(), path)
        self.assertEqual(ORIGINAL_TEST_AST, ast_digest(restored_test_ast()))
        from tests.host import test_run_qemu_smoke as cases

        expected = tuple(
            "tests.host.test_run_qemu_smoke." + name + "." + method
            for name in sorted(ORIGINAL_TEST_METHOD_ORDER) for method in sorted(ORIGINAL_TEST_METHOD_ORDER[name])
        )
        loader = unittest.TestLoader()
        self.assertEqual(85, len(expected))
        self.assertEqual(expected, suite_ids(loader.loadTestsFromModule(cases)))
        with mock.patch.object(sys, "path", list(sys.path)):
            discovered = loader.discover(str(ROOT / "tests/host"), pattern="test_run_qemu_smoke.py")
        self.assertEqual(tuple(value[len("tests.host."):] for value in expected), suite_ids(discovered))

    def test_independent_case_ownership_and_discovery_mutations_fail_closed(self):
        texts = {path: source(path) for path in TEST_PATHS}
        self.assertEqual((12, 14, 22, 15, 13, 9), tuple(
            len(case_owner_methods(texts[TEST_PATHS[owner]], owner)) for owner in range(2, 8)
        ))
        for owner, before, after in (
            (0, "unittest.TestCase", "object"),
            (0, "    pass", "    def test_override(self):\n        pass"),
            (0, "PANIC_OUTPUT,", "PANIC_OUTPUT as other,"),
            (1, "MICROS_BOOT", "CHANGED_BOOT"),
            (2, "class SmokeClassificationCases:", "class SmokeClassificationCases(unittest.TestCase):"),
            (2, "self.assertEqual(", "self.assertNotEqual("),
            (7, "def test_parses_exact_defined_symbol", "def test_changed_symbol"),
        ):
            path = TEST_PATHS[owner]
            self.assertIn(before, texts[path])
            changed = dict(texts)
            changed[path] = changed[path].replace(before, after, 1)
            with self.subTest(path=path, before=before), self.assertRaises(AssertionError):
                restored_test_ast(changed)
        for owner, extra in ((0, "\neffect()\n"), (1, "\nfrom tools import run_qemu_smoke\n"), (2, "\ndef load_tests(*args):\n    pass\n")):
            changed = dict(texts)
            changed[TEST_PATHS[owner]] += extra
            with self.subTest(owner=owner), self.assertRaises(AssertionError):
                restored_test_ast(changed)

    def test_argument_registration_keeps_one_parser_option_order_and_environment_defaults(self):
        import argparse
        from tools import qemu_smoke_arguments as arguments

        expected = (
            "-h", "--qemu", "--nm", "--kernel", "--memory", "--expected-frame-allocator-managed-delta",
            "--marker", "--pattern", "--forbid-marker", "--forbid-pattern", "--ordered-pattern",
            "--serial-input-trigger", "--serial-input-hex", "--expect", "--require-fdt-events",
            "--require-fdt-reservations", "--require-panic-report", "--require-trap-ready",
            "--require-trap-test-report", "--require-timer-test-report", "--require-frame-allocator-ready",
            "--require-frame-allocator-test-report", "--require-mmu-ready", "--require-mmu-test-report",
            "--require-frame-ownership-ready", "--require-frame-ownership-test-report",
            "--require-user-address-space-test-report", "--require-user-execution-test-report",
            "--require-scheduler-test-report", "--require-endpoint-test-report", "--require-ipc-test-report",
            "--require-scheduler-invalid-context", "--require-objects-ready", "--require-object-model-test-report",
            "--require-nested-trap-test-report", "--require-trap-context", "--trap-context-sepc-symbol",
            "--trap-context-origin", "--trap-context-scause", "--trap-context-stval", "--require-vm-self-fault",
            "--vm-self-fault-elf", "--vm-self-fault-symbol", "--vm-self-fault-scause", "--vm-self-fault-stval",
            "--vm-self-fault-ownership", "--timeout",
        )
        observed = []
        original = argparse.ArgumentParser.add_argument

        def record(parser, *names, **keywords):
            observed.append((id(parser), names[0]))
            return original(parser, *names, **keywords)

        with mock.patch.dict(os.environ, {"MICROS_QEMU": "/fixture/qemu", "MICROS_NM": "/fixture/nm"}):
            with mock.patch.object(argparse.ArgumentParser, "add_argument", record):
                result = arguments.parse_arguments(["--kernel", str(ROOT / "README.md"), "--marker", "BOOT"])
        self.assertEqual(expected, tuple(name for _, name in observed))
        self.assertEqual(1, len({identity for identity, _ in observed}))
        self.assertEqual(("/fixture/qemu", "/fixture/nm", ["128M"], 10.0, None), (
            result.qemu, result.nm, result.memory, result.timeout, result.serial_input,
        ))
        output = io.StringIO()
        with mock.patch.object(sys, "argv", ["run_qemu_smoke.py"]), contextlib.redirect_stdout(output):
            with self.assertRaises(SystemExit) as stopped:
                arguments.parse_arguments(["--help"])
        self.assertEqual(0, stopped.exception.code)
        self.assertIn("Run the deterministic micros QEMU boot smoke test.", output.getvalue())
        self.assertIn("LLVM nm executable used for exact target-symbol checks", " ".join(output.getvalue().split()))

    def test_argument_validation_preserves_original_first_error_order(self):
        from tools import qemu_smoke_arguments as arguments

        base = ["--kernel", str(ROOT / "README.md"), "--marker", "BOOT"]
        cases = (
            (["--timeout", "0", "--marker", ""], "--timeout must be greater than zero"),
            (["--marker", "", "--memory", ""], "--marker values must not be empty"),
            (["--memory", "", "--serial-input-trigger", "READY"], "--memory must not be empty"),
            (["--serial-input-trigger", "READY", "--pattern", "["],
             "--serial-input-trigger and --serial-input-hex must be provided together"),
            (["--serial-input-trigger", "READY", "--serial-input-hex", ""], "--serial-input-hex must not be empty"),
            (["--expected-frame-allocator-managed-delta", "1", "--pattern", "["],
             "--expected-frame-allocator-managed-delta requires two --memory values and a frame allocator report"),
            (["--trap-context-origin", "U", "--require-vm-self-fault"],
             "trap-context field requirements need --require-trap-context"),
            (["--require-vm-self-fault"], "--require-vm-self-fault needs ELF, symbol, scause, stval, and ownership"),
        )
        for extra, error in cases:
            output = io.StringIO()
            with self.subTest(extra=extra), mock.patch.object(sys, "argv", ["run_qemu_smoke.py"]):
                with contextlib.redirect_stderr(output), self.assertRaises(SystemExit) as stopped:
                    arguments.parse_arguments(base + extra)
                self.assertEqual(2, stopped.exception.code)
                self.assertEqual("run_qemu_smoke.py: error: " + error, output.getvalue().splitlines()[-1])

    def test_symbol_boundary_preserves_exact_command_and_failure_diagnostics(self):
        from tools import qemu_smoke_command as command

        with mock.patch.object(command.subprocess, "run") as run:
            run.return_value = SimpleNamespace(returncode=0, stdout="0000000080202000 T fault\n", stderr="")
            self.assertEqual(0x80202000, command.resolve_symbol_address("/fixture/nm", Path("/fixture/elf"), "fault"))
            run.assert_called_once_with(
                ["/fixture/nm", "-n", "--defined-only", "/fixture/elf"],
                stdin=command.subprocess.DEVNULL, stdout=command.subprocess.PIPE, stderr=command.subprocess.PIPE,
                text=True, check=False,
            )
            for diagnostic, expected in (("bad nm\n", "bad nm"), ("", "no diagnostic")):
                run.return_value = SimpleNamespace(returncode=1, stdout="", stderr=diagnostic)
                with self.assertRaisesRegex(RuntimeError, "^symbol inspection failed: " + expected + "$"):
                    command.resolve_symbol_address("/fixture/nm", Path("/fixture/elf"), "fault")
        with self.assertRaisesRegex(ValueError, "found 0"):
            command.parse_nm_symbol_address("invalid-address T fault\n00000001 T other\n", "fault")

    def test_serial_short_write_retries_the_original_payload_and_closes_owned_resources(self):
        from tools import qemu_smoke_execution as execution

        process = mock.Mock()
        process.poll.side_effect = [None, None, 0, 0]
        process.returncode = 0
        process.stdin.write.side_effect = [1, 2]
        selector = mock.Mock()
        selector.select.return_value = [(SimpleNamespace(fd=77), 0)]
        with mock.patch.object(execution.subprocess, "Popen", return_value=process):
            with mock.patch.object(execution.selectors, "DefaultSelector", return_value=selector):
                with mock.patch.object(execution.os, "read", side_effect=[b"READY\n", b"X\n", b""]):
                    with mock.patch.object(execution.time, "monotonic", side_effect=[100, 101, 102, 103]):
                        result = execution.run_qemu(["/fixture/qemu"], 10, "READY", b"xy")
        self.assertEqual(("READY\nX\n", 0, False, True), (
            result.output, result.return_code, result.timed_out, result.serial_input_sent,
        ))
        self.assertEqual([mock.call(b"xy"), mock.call(b"xy")], process.stdin.write.call_args_list)
        self.assertEqual([mock.call(9), mock.call(8), mock.call(7)], selector.select.call_args_list)
        selector.unregister.assert_called_once_with(process.stdout)
        selector.close.assert_called_once_with()
        process.stdin.close.assert_called_once_with()
        process.stdout.close.assert_called_once_with()
        process.kill.assert_not_called()

    def test_serial_and_ordinary_timeouts_preserve_kill_drain_decode_and_cleanup(self):
        from tools import qemu_smoke_execution as execution

        process = mock.Mock()
        process.poll.return_value = None
        process.communicate.return_value = (b"tail\xff", None)
        process.stdin.close.side_effect = BrokenPipeError
        selector = mock.Mock()
        with mock.patch.object(execution.subprocess, "Popen", return_value=process):
            with mock.patch.object(execution.selectors, "DefaultSelector", return_value=selector):
                with mock.patch.object(execution.time, "monotonic", side_effect=[100, 111]):
                    result = execution.run_qemu(["/fixture/qemu"], 10, "READY", b"xy")
        self.assertEqual(("tail\ufffd", None, True, False), (
            result.output, result.return_code, result.timed_out, result.serial_input_sent,
        ))
        process.kill.assert_called_once_with()
        process.communicate.assert_called_once_with()
        process.stdin.close.assert_called_once_with()
        process.stdout.close.assert_called_once_with()
        selector.close.assert_called_once_with()
        selector.select.assert_not_called()
        with mock.patch.object(
            execution.subprocess, "run",
            side_effect=execution.subprocess.TimeoutExpired(["/fixture/qemu"], 10, output=b"partial\xff"),
        ):
            result = execution.run_qemu(["/fixture/qemu"], 10)
        self.assertEqual(("partial\ufffd", None, True, False), (
            result.output, result.return_code, result.timed_out, result.serial_input_sent,
        ))

    def test_all_required_long_outlines_have_real_bounded_owners(self):
        for name, _ in ORIGINAL_LONG_FUNCTIONS:
            owner = original_destination("FunctionDef:" + name)
            self.assertIn(name, definitions(source(owner)))
            self.assertIn(name, OUTLINED_FUNCTIONS)
        for path in PRODUCTION_PATHS[1:]:
            for node in definitions(source(path)).values():
                if isinstance(node, ast.FunctionDef):
                    self.assertLessEqual(node.end_lineno - node.lineno + 1, 120, path + ":" + node.name)

    def test_actual_outline_mutations_cannot_replace_original_source_oracles(self):
        owners = {path: source(path) for path in PRODUCTION_PATHS[1:]}
        for owner, before, after in (
            (7, "require_mmu_ready=require_mmu_ready", "require_mmu_ready=False"),
            (7, "or result.return_code != 0", "or result.return_code == 0"),
            (7, "    return True\n", "    return False\n"),
            (8, "parser=parser", "parser=arguments"),
            (8, "parser.error(", "parser.exit("),
            (9, "arguments, result, outcome, expected_outcome,", "arguments, outcome, result, expected_outcome,"),
            (9, "markers=arguments.marker", "markers=arguments.forbid_marker"),
            (9, "def main(argv=None)", "def main(argv=())"),
        ):
            path = PRODUCTION_PATHS[owner]
            self.assertIn(before, owners[path])
            changed = dict(owners)
            changed[path] = changed[path].replace(before, after, 1)
            with self.subTest(owner=path, before=before), self.assertRaises(AssertionError):
                restored_runner_source(owners=changed)

    def test_phase_inverse_requires_actual_statements_exact_bindings_and_order(self):
        original = "def root(*, value):\n    if value:\n        return False\n    return True\n"
        outlined = (
            "def root(*, value):\n    if not phase(value=value):\n        return False\n    return True\n\n\n"
            "def phase(*, value):\n    if value:\n        return False\n    return True\n"
        )
        phases = (("phase", 0, 1, ("value",), True),)
        restore = lambda text: restore_phase_function(text, definitions(text)["root"], phases, ("phase", 1), 2, {})
        self.assertEqual(original, restore(outlined))
        for changed in (
            outlined.replace("phase(value=value)", "phase(value=other)"),
            outlined.replace("phase(*, value)", "phase(*, value=False)"),
            outlined.replace("if not phase", "if phase"),
            outlined.replace("        return False\n    return True\n", "        return False\n    return False\n"),
            outlined.replace("def phase(*, value):\n", "def phase(*, value):\n    effect()\n"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore(changed)
        self.assertNotEqual(original, restore(outlined.replace("if value:", "if not value:")))

    def test_main_inverse_keeps_actual_call_keywords_and_serial_gate_position(self):
        parameters = (
            "arguments, result, outcome, expected_outcome, "
            "expected_trap_context_sepc, expected_vm_self_fault_sepc"
        )
        original = (
            "def main(argv=None):\n    for memory in arguments.memory:\n"
            "        accepted = matches_expected_result(result=result, markers=arguments.marker)\n"
            "        accepted = accepted and result.serial_input_sent\n"
        )
        outlined = (
            "def main(argv=None):\n    for memory in arguments.memory:\n"
            "        accepted = _matches_observed_run(" + parameters + ")\n"
            "        accepted = accepted and result.serial_input_sent\n\n\n"
            "def _matches_observed_run(" + parameters + "):\n"
            "    return matches_expected_result(result=result, markers=arguments.marker)\n"
        )
        restore = lambda text: restore_main_function(text, definitions(text)["main"])
        self.assertEqual(original, restore(outlined))
        for changed in (
            outlined.replace("_matches_observed_run(arguments, result,", "_matches_observed_run(arguments, other,"),
            outlined.replace("def _matches_observed_run(arguments,", "def _matches_observed_run(other,"),
            outlined.replace("    return matches_expected_result", "    effect()\n    return matches_expected_result"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                restore(changed)
        self.assertNotEqual(original, restore(outlined.replace("markers=arguments.marker", "markers=arguments.pattern")))
        self.assertNotEqual(original, restore(outlined.replace("accepted and result.serial_input_sent", "result.serial_input_sent")))

    def test_bounded_diagnostic_command_and_execution_owners_are_complete(self):
        for owner in (4, 5, 6):
            with self.subTest(owner=PRODUCTION_PATHS[owner]):
                self.assertEqual(set(FUNCTION_OWNERS[owner]), {
                    node.name for node in ast.parse(source(PRODUCTION_PATHS[owner])).body
                    if isinstance(node, ast.FunctionDef)
                })

    def test_object_and_ipc_reports_have_their_complete_original_owner(self):
        self.assertEqual(set(FUNCTION_OWNERS[3]), {
            node.name for node in ast.parse(source(PRODUCTION_PATHS[3])).body if isinstance(node, ast.FunctionDef)
        })

    def test_platform_reports_have_their_complete_dependency_ordered_owner(self):
        self.assertEqual(set(FUNCTION_OWNERS[2]), {
            node.name for node in ast.parse(source(PRODUCTION_PATHS[2])).body if isinstance(node, ast.FunctionDef)
        })

    def test_private_import_fixture_keeps_single_type_pattern_binding_and_acyclic_owners(self):
        validate_owner_import(1, ast.parse("import enum").body[0])
        validate_owner_import(1, ast.parse("from typing import Optional").body[0])
        correct = ast.parse("from tools.qemu_smoke_records import PANIC_CORE_PATTERNS").body[0]
        validate_owner_import(4, correct, ())
        for owner, statement in (
            (1, "from tools.qemu_smoke_faults import _has_complete_panic_report"),
            (4, "from tools.run_qemu_smoke import PANIC_CORE_PATTERNS"),
            (4, "from tools.qemu_smoke_records import PANIC_CORE_PATTERNS as copied"),
            (4, "from tools.qemu_smoke_records import *"),
            (4, "import subprocess"),
        ):
            with self.subTest(owner=owner, statement=statement), self.assertRaises(AssertionError):
                validate_owner_import(owner, ast.parse(statement).body[0], ())
        with self.assertRaises(AssertionError):
            validate_owner_import(4, correct, ("Assign:PANIC_CORE_PATTERNS",))

    def test_records_have_one_actual_type_pattern_and_complete_line_owner(self):
        records = ast.parse(source(PRODUCTION_PATHS[1]))
        classes = {node.name for node in records.body if isinstance(node, ast.ClassDef)}
        values = {
            node.targets[0].id for node in records.body if isinstance(node, ast.Assign)
            and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name)
        }
        self.assertEqual(set(ORIGINAL_CLASSES), classes)
        self.assertEqual(set(ORIGINAL_GLOBAL_NAMES), values)
        self.assertIn("_split_output_records", definitions(source(PRODUCTION_PATHS[1])))

    def test_facade_reexports_actual_record_singletons_and_original_frozen_result(self):
        from tools import qemu_smoke_records, run_qemu_smoke

        for name in (*ORIGINAL_GLOBAL_NAMES, *ORIGINAL_CLASSES, "_split_output_records"):
            self.assertIs(getattr(qemu_smoke_records, name), getattr(run_qemu_smoke, name), name)
        value = run_qemu_smoke.QemuResult("exact", 0, False)
        self.assertFalse(value.serial_input_sent)
        self.assertEqual(("output", "return_code", "timed_out", "serial_input_sent"), tuple(value.__dataclass_fields__))
        self.assertEqual(
            ("pass", "failure", "panic", "unexpected-exit", "timeout"),
            tuple(value.value for value in run_qemu_smoke.SmokeOutcome),
        )

    def test_immutable_original_inventory_exists_before_any_source_rewriting(self):
        self.assertEqual(46, sum(map(len, FUNCTION_OWNERS.values())))
        self.assertEqual(46, len(ORIGINAL_FUNCTIONS))
        self.assertEqual(2, len(ORIGINAL_CLASSES))
        self.assertEqual(43, len(ORIGINAL_GLOBAL_NAMES))
        self.assertEqual(85, len(ORIGINAL_TEST_METHODS))
        self.assertEqual(36, len(ORIGINAL_TEST_DATA))
        self.assertEqual(212, len(INITIAL_SOURCE_OWNER_MIGRATION))
        self.assertEqual(set(INITIAL_SOURCE_OWNER_MIGRATION) - COMPLETED_SOURCE_NODES, set(SOURCE_OWNER_MIGRATION))
        self.assertEqual(3, len(ORIGINAL_LONG_FUNCTIONS))
        self.assertEqual(set(name for name, _ in ORIGINAL_LONG_FUNCTIONS) - OUTLINED_FUNCTIONS, set(LONG_TASK_MIGRATION))
        assignments = {
            node.targets[0].id: node.value for node in ast.parse(source(STRUCTURAL_TEST_PATH)).body
            if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name)
        }
        for name in ("SOURCE_OWNER_MIGRATION", "LONG_TASK_MIGRATION"):
            self.assertIsInstance(assignments[name], ast.Tuple)
            self.assertEqual([], assignments[name].elts)

    def test_raw_node_fixture_requires_complete_actual_bytes_ast_and_whitespace_only_transport(self):
        original = "#!/usr/bin/env python3\n\nimport os\n\nVALUE = 7\n\n\ndef original(value):\n    return value + VALUE\n"
        nodes = ast.parse(original).body
        layout = (
            ("Import:0", "#!/usr/bin/env python3\n\n", digest(node_source(original, nodes[0]).encode()), ast_digest(nodes[0])),
            ("Assign:VALUE", "\n", digest(node_source(original, nodes[1]).encode()), ast_digest(nodes[1])),
            ("FunctionDef:original", "\n\n", digest(node_source(original, nodes[2]).encode()), ast_digest(nodes[2])),
        )
        fragments = raw_module_fragments(original, layout)
        self.assertEqual(original, assemble_raw_module("#!/usr/bin/env python3\n\n", layout, fragments, ""))
        with self.assertRaises(AssertionError):
            assemble_raw_module("#!/usr/bin/env python3\n\n", layout, {key: value for key, value in fragments.items() if key != "Assign:VALUE"}, "")
        with self.assertRaises(AssertionError):
            raw_module_fragments(original + "\ndef unknown():\n    return 0\n", layout)
        changed = dict(fragments)
        raw, node_hash = changed["Assign:VALUE"]
        changed["Assign:VALUE"] = (raw.replace("7", "8"), node_hash)
        with self.assertRaises(AssertionError):
            assemble_raw_module("#!/usr/bin/env python3\n\n", layout, changed, "")
        injected = (layout[0], (layout[1][0], "\neffect()\n", *layout[1][2:]), layout[2])
        with self.assertRaises(AssertionError):
            assemble_raw_module("#!/usr/bin/env python3\n\n", injected, fragments, "")

    def test_whole_original_runner_reconstructs_from_actual_node_bytes_without_legacy_blob(self):
        restored = restored_runner_source()
        self.assertEqual(ORIGINAL_RUNNER_DIGEST, digest(restored.encode()))
        self.assertEqual(ORIGINAL_RUNNER_AST, ast_digest(ast.parse(restored)))

    def test_actual_family_source_projection_starts_at_the_complete_reviewed_file(self):
        restored = restored_runner_source()
        self.assertEqual(ORIGINAL_RUNNER_DIGEST, digest(restored.encode()))
        self.assertEqual(ORIGINAL_RUNNER_AST, ast_digest(ast.parse(restored)))

    def test_facade_binding_fixture_rejects_renames_duplicates_and_wrong_owner(self):
        correct = "from tools.qemu_smoke_command import build_qemu_command\n"
        self.assertEqual(
            {"FunctionDef:build_qemu_command": PRODUCTION_PATHS[5]}, facade_alias_bindings(correct),
        )
        for changed in (
            correct.replace("build_qemu_command", "build_qemu_command as other"),
            correct.replace("qemu_smoke_command", "qemu_smoke_execution"),
            correct + correct,
            correct.replace("build_qemu_command", "*"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                facade_alias_bindings(changed)

    def test_context_guard_fixture_preserves_early_source_and_pre_metadata_namespace_order(self):
        original = '''
if workflow == "test-qemu-smoke" and _file_digest(root / "tools/run_qemu_smoke.py") != QEMU_RUNNER_DIGEST:
    raise UnsupportedContext("QEMU argument builder is outside the reviewed closure")
cache = original_cache()
tools = DarwinTools(root, profile, cache, environment)
qemu = tools.qemu() if workflow == "test-qemu-smoke" else None
'''
        current = '''
if workflow == "test-qemu-smoke":
    _validate_qemu_runner_family(root)
cache = original_cache()
tools = DarwinTools(root, profile, cache, environment)
if workflow == "test-qemu-smoke":
    _validate_qemu_runner_namespace(root, tools)
qemu = tools.qemu() if workflow == "test-qemu-smoke" else None
'''
        expected = ast_digest(ast.parse(original))
        projected = lambda text: ast_digest(ast.Module(body=context_guard_statements(ast.parse(text).body), type_ignores=[]))
        self.assertEqual(expected, projected(current))
        for changed in (
            current.replace("_validate_qemu_runner_family(root)", "_validate_qemu_runner_family(other)"),
            current.replace("_validate_qemu_runner_namespace(root, tools)", "_validate_qemu_runner_namespace(root, other)"),
            current.replace("tools = DarwinTools", "other = DarwinTools"),
            current.replace("qemu = tools.qemu()", "different = tools.qemu()"),
        ):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                projected(changed)
        self.assertNotEqual(expected, projected(current.replace("original_cache()", "changed_cache()")))

    def test_only_exact_argument_builder_declaration_changes_the_original_context_algorithms(self):
        self.assertEqual(ORIGINAL_CONTEXT_AST, ast_digest(context_declaration_projection(source("tools/validation_context.py"))))

    def test_original_argument_builder_has_its_explicit_command_owner(self):
        self.assertNotIn("build_qemu_command", set(definitions(source(RUNNER_PATH))))
        self.assertIn("build_qemu_command", set(definitions(source(PRODUCTION_PATHS[5]))))


if __name__ == "__main__":
    unittest.main()
