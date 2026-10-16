#!/usr/bin/env python3

import argparse
import enum
import os
import re
import selectors
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


sys.path.insert(0, str(Path(__file__).resolve().parent.parent))


from tools.qemu_smoke_records import PANIC_MARKER
from tools.qemu_smoke_records import FAILURE_MARKER
from tools.qemu_smoke_records import TRAP_READY_MARKER
from tools.qemu_smoke_records import TRAP_TEST_MARKER
from tools.qemu_smoke_records import TRAP_TEST_PASS
from tools.qemu_smoke_records import TIMER_TEST_MARKER
from tools.qemu_smoke_records import TIMER_TEST_PASS
from tools.qemu_smoke_records import FRAME_ALLOCATOR_READY_MARKER
from tools.qemu_smoke_records import FRAME_ALLOCATOR_TEST_MARKER
from tools.qemu_smoke_records import FRAME_ALLOCATOR_READY_PATTERN
from tools.qemu_smoke_records import FRAME_ALLOCATOR_TEST_PASS
from tools.qemu_smoke_records import MMU_READY_MARKER
from tools.qemu_smoke_records import MMU_TEST_MARKER
from tools.qemu_smoke_records import MMU_READY_PATTERN
from tools.qemu_smoke_records import MMU_TEST_PASS
from tools.qemu_smoke_records import FRAME_OWNERSHIP_READY_MARKER
from tools.qemu_smoke_records import FRAME_OWNERSHIP_TEST_MARKER
from tools.qemu_smoke_records import FRAME_OWNERSHIP_READY_PATTERN
from tools.qemu_smoke_records import FRAME_OWNERSHIP_TEST_PASS
from tools.qemu_smoke_records import USER_ADDRESS_SPACE_TEST_MARKER
from tools.qemu_smoke_records import USER_ADDRESS_SPACE_TEST_PASS
from tools.qemu_smoke_records import USER_EXECUTION_TEST_MARKER
from tools.qemu_smoke_records import USER_EXECUTION_TEST_PASS
from tools.qemu_smoke_records import SCHEDULER_TEST_MARKER
from tools.qemu_smoke_records import SCHEDULER_TEST_PASS
from tools.qemu_smoke_records import ENDPOINT_TEST_MARKER
from tools.qemu_smoke_records import ENDPOINT_TEST_PASS
from tools.qemu_smoke_records import IPC_TEST_MARKER
from tools.qemu_smoke_records import IPC_ADDRESS_SPACES_MARKER
from tools.qemu_smoke_records import IPC_ADDRESS_SPACES_PASS
from tools.qemu_smoke_records import IPC_TEST_PASS
from tools.qemu_smoke_records import OBJECTS_READY_MARKER
from tools.qemu_smoke_records import OBJECT_MODEL_TEST_MARKER
from tools.qemu_smoke_records import NESTED_TRAP_TEST_MARKER
from tools.qemu_smoke_records import OBJECTS_READY_PATTERN
from tools.qemu_smoke_records import OBJECT_MODEL_TEST_PASS
from tools.qemu_smoke_records import NESTED_TRAP_TEST_PASS
from tools.qemu_smoke_records import FDT_COUNTS_PATTERN
from tools.qemu_smoke_records import FDT_EVENT_NAMES
from tools.qemu_smoke_records import FDT_RANGE_PATTERN
from tools.qemu_smoke_records import PANIC_CORE_PATTERNS
from tools.qemu_smoke_records import TRAP_CONTEXT_PATTERN
from tools.qemu_smoke_records import VM_SELF_FAULT_PATTERN


from tools.qemu_smoke_records import SmokeOutcome


from tools.qemu_smoke_records import QemuResult


from tools.qemu_smoke_platform import _has_complete_fdt_events


from tools.qemu_smoke_records import _split_output_records


from tools.qemu_smoke_faults import _has_complete_panic_report


from tools.qemu_smoke_platform import _has_complete_trap_ready


from tools.qemu_smoke_objects import parse_objects_ready


from tools.qemu_smoke_objects import _has_complete_objects_ready


from tools.qemu_smoke_platform import _has_complete_trap_test_report


from tools.qemu_smoke_platform import _has_complete_timer_test_report


from tools.qemu_smoke_platform import parse_frame_allocator_ready_counts


from tools.qemu_smoke_platform import _has_complete_frame_allocator_ready


from tools.qemu_smoke_platform import _frame_allocator_ready_precedes_target_outcome


from tools.qemu_smoke_platform import has_expected_frame_allocator_growth


from tools.qemu_smoke_platform import _has_complete_frame_allocator_test_report


from tools.qemu_smoke_platform import parse_mmu_ready


from tools.qemu_smoke_platform import _has_complete_mmu_ready


from tools.qemu_smoke_platform import _mmu_ready_precedes_target_outcome


from tools.qemu_smoke_platform import _has_complete_mmu_test_report


from tools.qemu_smoke_platform import parse_frame_ownership_ready


from tools.qemu_smoke_platform import _has_complete_frame_ownership_ready


from tools.qemu_smoke_platform import _frame_ownership_ready_precedes_target_outcome


from tools.qemu_smoke_platform import _has_complete_frame_ownership_test_report


from tools.qemu_smoke_objects import _has_complete_user_address_space_test_report


from tools.qemu_smoke_objects import _has_complete_user_execution_test_report


from tools.qemu_smoke_objects import _has_complete_scheduler_test_report


from tools.qemu_smoke_objects import _has_complete_endpoint_test_report


from tools.qemu_smoke_objects import _has_complete_ipc_test_report


from tools.qemu_smoke_objects import _objects_ready_precedes_target_outcome


from tools.qemu_smoke_objects import _has_complete_object_model_test_report


from tools.qemu_smoke_objects import _has_complete_nested_trap_test_report


from tools.qemu_smoke_faults import _has_complete_trap_context


from tools.qemu_smoke_faults import _has_vm_self_fault


from tools.qemu_smoke_faults import _has_complete_scheduler_invalid_context


from tools.qemu_smoke_command import parse_nm_symbol_address


from tools.qemu_smoke_command import resolve_symbol_address


from tools.qemu_smoke_outcome import _has_required_output


from tools.qemu_smoke_outcome import _has_ordered_patterns


from tools.qemu_smoke_outcome import matches_expected_result


from tools.qemu_smoke_outcome import classify_smoke


from tools.qemu_smoke_command import build_qemu_command


from tools.qemu_smoke_execution import _decode_output


from tools.qemu_smoke_execution import _serial_trigger_observed


from tools.qemu_smoke_execution import _run_qemu_with_serial_input


from tools.qemu_smoke_execution import run_qemu


from tools.qemu_smoke_outcome import print_tap_result


from tools.qemu_smoke_arguments import parse_arguments


from tools.qemu_smoke_cli import main


if __name__ == "__main__":
    sys.exit(main())
