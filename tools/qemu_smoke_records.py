import enum
import re
from dataclasses import dataclass
from typing import Optional


PANIC_MARKER = "MICROS_PANIC"


FAILURE_MARKER = "MICROS_TEST_FAILURE"


TRAP_READY_MARKER = "MICROS_TRAP_READY"


TRAP_TEST_MARKER = "MICROS_TRAP_TEST"


TRAP_TEST_PASS = (
    "MICROS_TRAP_TEST_PASS "
    "origin=S cause=illegal-instruction registers=preserved "
    "hart-context=routed primary-stack=selected sscratch=anchor"
)


TIMER_TEST_MARKER = "MICROS_TIMER_TEST"


TIMER_TEST_PASS = (
    "MICROS_TIMER_TEST_PASS "
    "ticks=0x0000000000000003 "
    "interval=0x00000000000186a0 "
    "active=0x0000000000000000 "
    "deadline=0xffffffffffffffff owner=hart"
)


FRAME_ALLOCATOR_READY_MARKER = "MICROS_FRAME_ALLOCATOR_READY"


FRAME_ALLOCATOR_TEST_MARKER = "MICROS_FRAME_ALLOCATOR_TEST"


FRAME_ALLOCATOR_READY_PATTERN = re.compile(
    r"^MICROS_FRAME_ALLOCATOR_READY "
    r"managed=0x([0-9a-f]{16}) "
    r"free=0x([0-9a-f]{16})$"
)


FRAME_ALLOCATOR_TEST_PASS = (
    "MICROS_FRAME_ALLOCATOR_TEST_PASS "
    "allocations=0x0000000000000004 "
    "reuse=lowest invariants=preserved"
)


MMU_READY_MARKER = "MICROS_MMU_READY"


MMU_TEST_MARKER = "MICROS_MMU_TEST"


MMU_READY_PATTERN = re.compile(
    r"^MICROS_MMU_READY "
    r"mode=sv39 "
    r"root=0x([0-9a-f]{16}) "
    r"tables=0x([0-9a-f]{16})$"
)


MMU_TEST_PASS = (
    "MICROS_MMU_TEST_PASS "
    "store-fault=text "
    "execute-fault=writable "
    "traps=0x0000000000000002"
)


FRAME_OWNERSHIP_READY_MARKER = "MICROS_FRAME_OWNERSHIP_READY"


FRAME_OWNERSHIP_TEST_MARKER = "MICROS_FRAME_OWNERSHIP_TEST"


FRAME_OWNERSHIP_READY_PATTERN = re.compile(
    r"^MICROS_FRAME_OWNERSHIP_READY "
    r"owned=0x([0-9a-f]{16}) "
    r"kernel-tables=0x([0-9a-f]{16}) "
    r"phase=bootstrap$"
)


FRAME_OWNERSHIP_TEST_PASS = (
    "MICROS_FRAME_OWNERSHIP_TEST_PASS "
    "stale=rejected "
    "release=blocked "
    "handoff=atomic "
    "invariants=preserved"
)


USER_ADDRESS_SPACE_TEST_MARKER = "MICROS_USER_ADDRESS_SPACE_TEST"


USER_ADDRESS_SPACE_TEST_PASS = (
    "MICROS_USER_ADDRESS_SPACE_TEST_PASS "
    "roots=isolated "
    "reuse=zeroed "
    "active=guarded "
    "ownership=validated "
    "sum=cleared"
)


USER_EXECUTION_TEST_MARKER = "MICROS_USER_EXECUTION_TEST"


USER_EXECUTION_TEST_PASS = (
    "MICROS_USER_EXECUTION_TEST_PASS "
    "mode=entered "
    "faults=isolated "
    "context=preserved "
    "stack=owned "
    "return=resumed"
)


SCHEDULER_TEST_MARKER = "MICROS_SCHEDULER_TEST"


SCHEDULER_TEST_PASS = (
    "MICROS_SCHEDULER_TEST_PASS "
    "queues=minix-priority "
    "current=reachable "
    "accounting=separate "
    "switches=alternating "
    "idle=resumed "
    "registers=preserved"
)


ENDPOINT_TEST_MARKER = "MICROS_ENDPOINT_TEST"


ENDPOINT_TEST_PASS = (
    "MICROS_ENDPOINT_TEST_PASS "
    "generation=validated "
    "profiles=immutable "
    "visibility=staged "
    "authorization=separate "
    "grants=generation-safe"
)


IPC_TEST_MARKER = "MICROS_IPC_TEST"


IPC_ADDRESS_SPACES_MARKER = "MICROS_IPC_ADDRESS_SPACES"


IPC_ADDRESS_SPACES_PASS = (
    "MICROS_IPC_ADDRESS_SPACES "
    "count=three "
    "roots=preserved "
    "contexts=preserved "
    "stacks=preserved "
    "scheduler=preserved "
    "messages=preserved"
)


IPC_TEST_PASS = (
    "MICROS_IPC_TEST_PASS "
    "endpoints=generation-safe "
    "queues=blocking "
    "calls=tokenized "
    "notifications=coalesced "
    "kernel-events=injected "
    "deadlock=rejected"
)


OBJECTS_READY_MARKER = "MICROS_OBJECTS_READY"


OBJECT_MODEL_TEST_MARKER = "MICROS_OBJECT_MODEL_TEST"


NESTED_TRAP_TEST_MARKER = "MICROS_NESTED_TRAP_TEST"


OBJECTS_READY_PATTERN = re.compile(
    r"^MICROS_OBJECTS_READY "
    r"processes=0x([0-9a-f]{16}) "
    r"threads=0x([0-9a-f]{16}) "
    r"harts=0x([0-9a-f]{16}) "
    r"max-threads=0x([0-9a-f]{16}) "
    r"max-harts=0x([0-9a-f]{16}) "
    r"boot-hart=0x([0-9a-f]{16})$"
)


OBJECT_MODEL_TEST_PASS = (
    "MICROS_OBJECT_MODEL_TEST_PASS "
    "process-generation=advanced "
    "stale=rejected "
    "thread-limit=enforced "
    "hart-local=preserved"
)


NESTED_TRAP_TEST_PASS = (
    "MICROS_NESTED_TRAP_TEST_PASS "
    "hart=routed emergency-stack=selected"
)


FDT_COUNTS_PATTERN = re.compile(
    r"^MICROS_FDT_COUNTS "
    r"memory=0x([0-9a-f]{16}) "
    r"reservation=0x([0-9a-f]{16}) "
    r"reserved-memory=0x([0-9a-f]{16})$"
)


FDT_EVENT_NAMES = (
    "MICROS_FDT_MEMORY",
    "MICROS_FDT_RESERVATION",
    "MICROS_FDT_RESERVED_MEMORY",
)


FDT_RANGE_PATTERN = re.compile(
    r"^(MICROS_FDT_MEMORY|"
    r"MICROS_FDT_RESERVATION|"
    r"MICROS_FDT_RESERVED_MEMORY) "
    r"base=0x[0-9a-f]{16} size=0x[0-9a-f]{16}$"
)


PANIC_CORE_PATTERNS = (
    re.compile(r"^MICROS_PANIC reason=[a-z0-9]+(?:-[a-z0-9]+)*$"),
    re.compile(
        r"^MICROS_PANIC_BUILD "
        r"version=[0-9]+[.][0-9]+[.][0-9]+$"
    ),
    re.compile(
        r"^MICROS_PANIC_SOURCE "
        r"file=([A-Za-z0-9_./-]+) line=0x[0-9a-f]{16}$"
    ),
    re.compile(
        r"^MICROS_PANIC_HART mode=S id=0x[0-9a-f]{16}$"
    ),
    re.compile(
        r"^MICROS_PANIC_MACHINE "
        r"sstatus=0x[0-9a-f]{16} "
        r"scause=0x[0-9a-f]{16} "
        r"stval=0x[0-9a-f]{16} "
        r"sepc=0x[0-9a-f]{16} "
        r"ra=0x[0-9a-f]{16} "
        r"sp=0x[0-9a-f]{16}$"
    ),
)


TRAP_CONTEXT_PATTERN = re.compile(
    r"^MICROS_TRAP_CONTEXT "
    r"origin=([SU]) "
    r"sstatus=0x[0-9a-f]{16} "
    r"scause=0x([0-9a-f]{16}) "
    r"stval=0x([0-9a-f]{16}) "
    r"sepc=0x([0-9a-f]{16}) "
    r"ra=0x[0-9a-f]{16} "
    r"sp=0x[0-9a-f]{16}$"
)


VM_SELF_FAULT_PATTERN = re.compile(
    r"^MICROS_VM_SELF_FAULT "
    r"service=0x([0-9a-f]{16}) "
    r"process-slot=0x([0-9a-f]{16}) "
    r"process-generation=0x([0-9a-f]{16}) "
    r"endpoint=0x([0-9a-f]{16}) "
    r"scause=0x([0-9a-f]{16}) "
    r"stval=0x([0-9a-f]{16}) "
    r"sepc=0x([0-9a-f]{16}) "
    r"ownership=(bootstrap|handed-off)$"
)


class SmokeOutcome(enum.Enum):
    PASS = "pass"
    FAILURE = "failure"
    PANIC = "panic"
    UNEXPECTED_EXIT = "unexpected-exit"
    TIMEOUT = "timeout"


@dataclass(frozen=True)
class QemuResult:
    output: str
    return_code: Optional[int]
    timed_out: bool
    serial_input_sent: bool = False


def _split_output_records(output):
    records = output.splitlines(keepends=True)
    output_lines = []
    terminated = []

    for record in records:
        if record.endswith("\r\n"):
            output_lines.append(record[:-2])
            terminated.append(True)
        elif record.endswith("\n"):
            output_lines.append(record[:-1])
            terminated.append(True)
        else:
            output_lines.append(record)
            terminated.append(False)
    return output_lines, terminated
