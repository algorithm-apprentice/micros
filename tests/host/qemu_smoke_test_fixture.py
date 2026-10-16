OBJECTS_READY_RECORD = (
    "MICROS_OBJECTS_READY "
    "processes=0x0000000000000000 "
    "threads=0x0000000000000000 "
    "harts=0x0000000000000001 "
    "max-threads=0x0000000000000001 "
    "max-harts=0x0000000000000001 "
    "boot-hart=0x0000000000000000\n"
)

PANIC_OUTPUT = (
    "MICROS_BOOT 0.1.0\n"
    + OBJECTS_READY_RECORD
    + "MICROS_TRAP_READY\n"
    "MICROS_FDT_MEMORY "
    "base=0x0000000080000000 "
    "size=0x0000000008000000\n"
    "MICROS_FDT_RESERVED_MEMORY "
    "base=0x0000000080000000 "
    "size=0x0000000000040000\n"
    "MICROS_FDT_COUNTS "
    "memory=0x0000000000000001 "
    "reservation=0x0000000000000000 "
    "reserved-memory=0x0000000000000001\n"
    "MICROS_FDT_READY\n"
    "MICROS_PANIC reason=intentional-test\n"
    "MICROS_PANIC_BUILD version=0.1.0\n"
    "MICROS_PANIC_SOURCE "
    "file=kernel/main.c line=0x0000000000000042\n"
    "MICROS_PANIC_HART mode=S id=0x0000000000000000\n"
    "MICROS_PANIC_MACHINE "
    "sstatus=0x0000000200000000 "
    "scause=0x0000000000000000 "
    "stval=0x0000000000000000 "
    "sepc=0x0000000000000000 "
    "ra=0x0000000080201234 "
    "sp=0x0000000080204000\n"
)

TRAP_RECOVERY_OUTPUT = (
    "MICROS_BOOT 0.1.0\n"
    + OBJECTS_READY_RECORD
    + "MICROS_TRAP_READY\n"
    "MICROS_FDT_MEMORY "
    "base=0x0000000080000000 "
    "size=0x0000000008000000\n"
    "MICROS_FDT_RESERVED_MEMORY "
    "base=0x0000000080000000 "
    "size=0x0000000000040000\n"
    "MICROS_FDT_COUNTS "
    "memory=0x0000000000000001 "
    "reservation=0x0000000000000000 "
    "reserved-memory=0x0000000000000001\n"
    "MICROS_FDT_READY\n"
    "MICROS_TRAP_TEST_PASS "
    "origin=S cause=illegal-instruction registers=preserved "
    "hart-context=routed primary-stack=selected sscratch=anchor\n"
)

TIMER_TEST_OUTPUT = TRAP_RECOVERY_OUTPUT.replace(
    "MICROS_TRAP_TEST_PASS "
    "origin=S cause=illegal-instruction registers=preserved "
    "hart-context=routed primary-stack=selected sscratch=anchor\n",
    "MICROS_TIMER_TEST_PASS "
    "ticks=0x0000000000000003 "
    "interval=0x00000000000186a0 "
    "active=0x0000000000000000 "
    "deadline=0xffffffffffffffff owner=hart\n",
)

FRAME_ALLOCATOR_READY_RECORD = (
    "MICROS_FRAME_ALLOCATOR_READY "
    "managed=0x0000000000000100 "
    "free=0x0000000000000100\n"
)

FRAME_ALLOCATOR_TEST_PASS = (
    "MICROS_FRAME_ALLOCATOR_TEST_PASS "
    "allocations=0x0000000000000004 "
    "reuse=lowest invariants=preserved\n"
)

MMU_READY_RECORD = (
    "MICROS_MMU_READY "
    "mode=sv39 "
    "root=0x0000000080216000 "
    "tables=0x0000000000000042\n"
)

MMU_TEST_PASS = (
    "MICROS_MMU_TEST_PASS "
    "store-fault=text "
    "execute-fault=writable "
    "traps=0x0000000000000002\n"
)

FRAME_OWNERSHIP_READY_RECORD = (
    "MICROS_FRAME_OWNERSHIP_READY "
    "owned=0x0000000000000042 "
    "kernel-tables=0x0000000000000042 "
    "phase=bootstrap\n"
)

FRAME_OWNERSHIP_TEST_PASS = (
    "MICROS_FRAME_OWNERSHIP_TEST_PASS "
    "stale=rejected "
    "release=blocked "
    "handoff=atomic "
    "invariants=preserved\n"
)

USER_ADDRESS_SPACE_TEST_PASS = (
    "MICROS_USER_ADDRESS_SPACE_TEST_PASS "
    "roots=isolated "
    "reuse=zeroed "
    "active=guarded "
    "ownership=validated "
    "sum=cleared\n"
)

USER_EXECUTION_TEST_PASS = (
    "MICROS_USER_EXECUTION_TEST_PASS "
    "mode=entered "
    "faults=isolated "
    "context=preserved "
    "stack=owned "
    "return=resumed\n"
)

SCHEDULER_TEST_PASS = (
    "MICROS_SCHEDULER_TEST_PASS "
    "queues=minix-priority "
    "current=reachable "
    "accounting=separate "
    "switches=alternating "
    "idle=resumed "
    "registers=preserved\n"
)

FRAME_ALLOCATOR_OUTPUT = TRAP_RECOVERY_OUTPUT.replace(
    "MICROS_TRAP_TEST_PASS "
    "origin=S cause=illegal-instruction registers=preserved "
    "hart-context=routed primary-stack=selected sscratch=anchor\n",
    "",
).replace(
    "MICROS_FDT_READY\n",
    "MICROS_FDT_READY\n" + FRAME_ALLOCATOR_READY_RECORD,
)

FRAME_ALLOCATOR_TEST_OUTPUT = (
    FRAME_ALLOCATOR_OUTPUT + FRAME_ALLOCATOR_TEST_PASS
)

MMU_OUTPUT = FRAME_ALLOCATOR_OUTPUT + MMU_READY_RECORD

MMU_TEST_OUTPUT = MMU_OUTPUT + MMU_TEST_PASS

FRAME_OWNERSHIP_OUTPUT = MMU_OUTPUT + FRAME_OWNERSHIP_READY_RECORD

FRAME_OWNERSHIP_TEST_OUTPUT = (
    FRAME_OWNERSHIP_OUTPUT + FRAME_OWNERSHIP_TEST_PASS
)

USER_ADDRESS_SPACE_TEST_OUTPUT = (
    FRAME_OWNERSHIP_OUTPUT + USER_ADDRESS_SPACE_TEST_PASS
)

USER_EXECUTION_TEST_OUTPUT = (
    FRAME_OWNERSHIP_OUTPUT + USER_EXECUTION_TEST_PASS
)

SCHEDULER_TEST_OUTPUT = (
    FRAME_OWNERSHIP_OUTPUT + SCHEDULER_TEST_PASS
)

ENDPOINT_TEST_PASS = (
    "MICROS_ENDPOINT_TEST_PASS "
    "generation=validated "
    "profiles=immutable "
    "visibility=staged "
    "authorization=separate "
    "grants=generation-safe\n"
)

ENDPOINT_TEST_OUTPUT = (
    FRAME_OWNERSHIP_OUTPUT + ENDPOINT_TEST_PASS
)

IPC_TEST_PASS = (
    "MICROS_IPC_TEST_PASS "
    "endpoints=generation-safe "
    "queues=blocking "
    "calls=tokenized "
    "notifications=coalesced "
    "kernel-events=injected "
    "deadlock=rejected\n"
)

IPC_ADDRESS_SPACES_PASS = (
    "MICROS_IPC_ADDRESS_SPACES "
    "count=three "
    "roots=preserved "
    "contexts=preserved "
    "stacks=preserved "
    "scheduler=preserved "
    "messages=preserved\n"
)

IPC_TEST_OUTPUT = (
    FRAME_OWNERSHIP_OUTPUT
    + IPC_ADDRESS_SPACES_PASS
    + IPC_TEST_PASS
)

OBJECT_MODEL_TEST_PASS = (
    "MICROS_OBJECT_MODEL_TEST_PASS "
    "process-generation=advanced "
    "stale=rejected "
    "thread-limit=enforced "
    "hart-local=preserved\n"
)

NESTED_TRAP_TEST_PASS = (
    "MICROS_NESTED_TRAP_TEST_PASS "
    "hart=routed emergency-stack=selected\n"
)

OBJECT_MODEL_TEST_OUTPUT = MMU_OUTPUT + OBJECT_MODEL_TEST_PASS

NESTED_TRAP_TEST_OUTPUT = MMU_OUTPUT + NESTED_TRAP_TEST_PASS

TRAP_CONTEXT_RECORD = (
    "MICROS_TRAP_CONTEXT "
    "origin=S "
    "sstatus=0x0000000200000100 "
    "scause=0x0000000000000002 "
    "stval=0x00000000c0001073 "
    "sepc=0x0000000080202000 "
    "ra=0x0000000000000101 "
    "sp=0x0000000080205000\n"
)

TRAP_PANIC_OUTPUT = (
    PANIC_OUTPUT.replace(
        "MICROS_PANIC reason=intentional-test\n",
        "MICROS_PANIC reason=unexpected-exception\n",
    )
    .replace(
        "file=kernel/main.c",
        "file=kernel/trap.c",
    )
    + TRAP_CONTEXT_RECORD
)

SCHEDULER_INVALID_DIAGNOSTIC = (
    "MICROS_SCHEDULER_INVALID_CONTEXT "
    "state=outgoing ownership=preserved accounting=kernel\n"
)

SCHEDULER_INVALID_CONTEXT_RECORD = (
    "MICROS_TRAP_CONTEXT "
    "origin=U "
    "sstatus=0x0000000200000020 "
    "scause=0x8000000000000005 "
    "stval=0x0000000000000000 "
    "sepc=0x0000000040000010 "
    "ra=0x0000000000001001 "
    "sp=0x0000000040005000\n"
)

SCHEDULER_INVALID_OUTPUT = (
    PANIC_OUTPUT.replace(
        "MICROS_PANIC reason=intentional-test\n",
        SCHEDULER_INVALID_DIAGNOSTIC
        + "MICROS_PANIC reason=invalid-bootstrap-user-context\n",
    )
    + SCHEDULER_INVALID_CONTEXT_RECORD
)
