import contextlib

import io

import sys

import unittest

from tools import run_qemu_smoke

from tests.host.qemu_smoke_test_fixture import (
    OBJECTS_READY_RECORD,
    PANIC_OUTPUT,
    TRAP_RECOVERY_OUTPUT,
    TIMER_TEST_OUTPUT,
    FRAME_ALLOCATOR_READY_RECORD,
    FRAME_ALLOCATOR_TEST_PASS,
    MMU_READY_RECORD,
    MMU_TEST_PASS,
    FRAME_OWNERSHIP_READY_RECORD,
    FRAME_OWNERSHIP_TEST_PASS,
    USER_ADDRESS_SPACE_TEST_PASS,
    USER_EXECUTION_TEST_PASS,
    SCHEDULER_TEST_PASS,
    FRAME_ALLOCATOR_OUTPUT,
    FRAME_ALLOCATOR_TEST_OUTPUT,
    MMU_OUTPUT,
    MMU_TEST_OUTPUT,
    FRAME_OWNERSHIP_OUTPUT,
    FRAME_OWNERSHIP_TEST_OUTPUT,
    USER_ADDRESS_SPACE_TEST_OUTPUT,
    USER_EXECUTION_TEST_OUTPUT,
    SCHEDULER_TEST_OUTPUT,
    ENDPOINT_TEST_PASS,
    ENDPOINT_TEST_OUTPUT,
    IPC_TEST_PASS,
    IPC_ADDRESS_SPACES_PASS,
    IPC_TEST_OUTPUT,
    OBJECT_MODEL_TEST_PASS,
    NESTED_TRAP_TEST_PASS,
    OBJECT_MODEL_TEST_OUTPUT,
    NESTED_TRAP_TEST_OUTPUT,
    TRAP_CONTEXT_RECORD,
    TRAP_PANIC_OUTPUT,
    SCHEDULER_INVALID_DIAGNOSTIC,
    SCHEDULER_INVALID_CONTEXT_RECORD,
    SCHEDULER_INVALID_OUTPUT,
)

from tests.host.qemu_smoke_classification_cases import (
    SmokeClassificationCases,
)

from tests.host.qemu_smoke_panic_cases import (
    PanicReportCases,
)

from tests.host.qemu_smoke_platform_cases import (
    PlatformReportCases,
)

from tests.host.qemu_smoke_memory_cases import (
    MemoryReportCases,
)

from tests.host.qemu_smoke_object_cases import (
    ObjectReportCases,
)

from tests.host.qemu_smoke_execution_cases import (
    NmSymbolCases,
    QemuCommandCases,
    QemuSerialInputCases,
    TapOutputCases,
)


class SmokeClassificationTest(
    SmokeClassificationCases,
    unittest.TestCase,
):
    pass



class ExpectedOutcomeTest(
    PanicReportCases,
    PlatformReportCases,
    MemoryReportCases,
    ObjectReportCases,
    unittest.TestCase,
):
    pass



class NmSymbolTest(
    NmSymbolCases,
    unittest.TestCase,
):
    pass



class QemuCommandTest(
    QemuCommandCases,
    unittest.TestCase,
):
    pass



class QemuSerialInputTest(
    QemuSerialInputCases,
    unittest.TestCase,
):
    pass



class TapOutputTest(
    TapOutputCases,
    unittest.TestCase,
):
    pass



if __name__ == "__main__":
    unittest.main()
