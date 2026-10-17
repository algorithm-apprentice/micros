# ELF Spawn VM Boot-Database Validation Timeout Root Cause

## Scope

This note records the read-only investigation that again blocked the CPython
3.15 host-runtime implementation after the frame-ownership correction merged.
It does not authorize a retry, timeout increase, compiler-optimization change,
QEMU-runner relaxation, or production edit.

The investigation used:

- the exact same-candidate producer and reuse reports;
- the unchanged `riscv64-elf-spawn-test` kernel and VM service images;
- the guest bootstrap failure and panic register records;
- `llvm-addr2line`, `llvm-nm`, target disassembly, and the generated map;
- current `micros` source and Accepted ADRs; and
- the existing canonical MINIX VM bootstrap study.

No additional QEMU retry was used to obtain the root cause.

## Original problem and scope check

The original acceptance problem is not "make one ELF-spawn run pass." It is:

> make the required current-candidate validation deterministic enough that one
> no-retry producer and one exact reuse consumer are valid evidence.

ADR-0076 corrected one measured kernel hot path that violated that objective.
The later failure below occurs earlier, while VM service 2 is still
`STARTING`, and maps to a separate user-space validator. Treating it as a
reason to retry the Python gate, raise the readiness deadline, or continue
hardening unrelated host tooling would be another XY detour.

## Symptom

Candidate:

```text
HEAD: 82ac6235aec93094f6826959696bf8c977d351f3
tree: 6e5e8bafeefe57d21c0212c9a949abf2287020cc
```

The complete producer passed all 47 commands:

```text
producer run: a02acfb615f04be3b6cab52173ecab11
test-qemu-elf-spawn: FRESH/PASS
producer.complete: true
final_candidate: true
```

The exact bounded reuse consumer reused the three selected receipts, then ran
TTY, RAMFS, VFS, and ELF-spawn fresh. VFS passed, but ELF-spawn emitted:

```text
MICROS_BOOTSTRAP_FAILURE reason=ready-timeout
service=0x0000000000000002
endpoint=0x0000000000001001
phase=failed
state=starting
detail=0x0000000021fd2cb4
```

The build step was `[0/1]`: QEMU ran the already linked image without compiling
or relinking it. The successful producer and failed consumer therefore used
the same binaries:

```text
kernel/micros.elf:
c27c51a885842c61ec4884d091b5f3afabcc6261597a743905f3e2b9c8a70a40

bootstrap/micros-vm-server.elf:
c7e460ded0d98ccabf2dcb574f7a3f06e4733f1ae3d4cd52198072b52bedc6a2
```

The failure detail is the kernel-owned absolute deadline. The manifest timeout
is 100,000,000 counter ticks:

```text
deadline:     570,240,180 ticks = 57.024018 seconds
release time: 470,240,180 ticks = 47.024018 seconds
ready budget: 100,000,000 ticks = 10 seconds
```

The guest did not hit the 180-second host harness timeout. Its own accepted
bootstrap deadline fired first.

## Five Whys

### 1. Why did the reuse consumer fail?

VM service 2 did not complete its handoff/control initialization and ordinary
readiness call within the fixed 10-second guest deadline.

### 2. Why had VM not reported ready?

The panic record captured:

```text
sepc = 0x00000000400034f6
```

In the exact embedded VM ELF this maps to:

```text
vm_core_boot_database_is_immutable
servers/vm/vm_validation.c
```

The instruction is the branch immediately after an out-of-line call to:

```text
vm_core_initial_frame_is_free
```

VM was actively validating its portable state, not blocked in IPC, deadlocked,
or waiting on QEMU.

### 3. Why is complete VM validation on the readiness path?

The exact startup sequence is:

```text
micros_service_main:
  micros_vm_state_initialize()
  vm_server_complete_handoff()
  vm_server_configure_control()
  -> micros_vm_service_initialize
  -> micros_vm_state_validate
  -> vm_core_validate_state
  -> vm_core_boot_database_is_immutable
```

`micros_vm_service_initialize()` deliberately requires a complete valid state
before the production VM control protocol becomes reachable. Ordinary launcher
readiness follows that initialization.

### 4. Why does the immutability check execute so much work?

The fixed boot-information object is 364,704 bytes:

```text
sizeof(struct micros_vm_boot_info) = 0x590a0
frame_states offset                = 102,560
frame_states capacity              = 262,144
```

The current function hashes every byte from index zero through 364,703. Inside
the complete 262,144-byte frame-state region, every byte calls
`vm_core_initial_frame_is_free()`.

The observed QEMU image manages only:

```text
managed frames: 0x70e8 = 28,904
fixed capacity:  0x40000 = 262,144
inactive tail:              233,240
inactive share:              88.97%
```

The initial-free bitmap is zero beyond `managed_frame_count`, and the inactive
frame-state tail must also be zero. Nevertheless, Debug target code performs
233,240 unnecessary out-of-line bitmap helper calls and one FNV multiply for
each inactive byte.

Disassembly proves:

```text
jalr ... <vm_core_initial_frame_is_free>
beqz a0, ...
...
addi index, index, 1
j loop
```

The build has no optimization flag. This work shape therefore does not depend
on compiler inlining.

### 5. Why can the same exact binary pass or fail?

The RISC-V `time` counter advances independently of guest instruction
throughput. The complete initialization work is near the fixed 10-second
deadline under QEMU. Host scheduling and load change how many guest
instructions execute before the same counter deadline, so the unchanged image
can finish first or be interrupted in the same deterministic loop.

The apparent flake is a wall-clock threshold over excessive deterministic
work.

## Relationship to ADR-0076

ADR-0076 remains correct and necessary. It removed repeated complete
frame-ledger work from high-frequency kernel translation after services were
running.

This failure is a separate earlier path:

```text
owner: servers/vm/vm_validation.c
phase: VM STARTING before ordinary readiness
state: VM-local boot database and initial-free bitmap
trigger: one complete VM service initialization validation
```

Both defects share a fixed-capacity inactive tail, but they have different
authority, state, source ownership, and acceptance boundaries. The VM
correction must not reopen or broaden the merged frame-ownership design.

## Existing validation semantics

The initial boot digest covers the complete fixed boot-information object with
the header digest field treated as zero.

After initialization, originally free frame-state bytes are allowed to change
among the accepted VM-local lifecycle values. Runtime immutability validation
therefore recomputes the same digest while:

- treating the header digest field as zero;
- treating every frame whose initial-free bitmap bit is set as
  `MICROS_VM_FRAME_FREE`;
- hashing every originally nonfree active frame state exactly;
- hashing every inactive frame-state byte exactly; and
- comparing the result with the imported boot summary digest.

Later validation independently checks active local frame states, mapping
records, aggregate counts, lifecycle phase, and the inactive frame-state tail.

The correction must retain those semantics. In particular, skipping the
inactive frame-state bytes without checking both the bytes and the
initial-free bitmap tail would weaken corruption detection.

## MINIX behavioral baseline

The canonical
[MINIX VM bootstrap study](minix-vm-bootstrap-and-handoff.md) records:

- VM imports bounded kernel boot information;
- validates counts and alignment;
- copies authoritative input into VM-owned private state;
- constructs a fixed free-page bitmap;
- reconstructs the boot process memory state; and
- enters its ordinary server loop after initialization.

MINIX does not preserve one fixed 364,704-byte transport object and recompute a
complete canonical digest over that object before each later VM transition.
Its durable authority is the VM-owned allocator/process/page-table state built
from the imported data.

`micros` retains stronger fixed-capacity, zero-tail, typed-state, exact
generation, and independent-digest checks as the Accepted VH-04 compatible
extension. That extension does not require a linear inactive-tail FNV loop or
one helper call per capacity slot.

## Difference classification

| Topic | MINIX baseline | `micros` behavior | Classification |
| --- | --- | --- | --- |
| Boot import | VM imports bounded authoritative kernel memory/process data | VM imports one pointer-free generated boot object | Required adaptation |
| Private VM state | VM constructs its allocator and process/page-table state before ordinary service | VM constructs `micros_vm_state` and the initial-free bitmap before readiness | Baseline parity |
| Fixed object digest | No equivalent persistent complete transport digest | Every runtime validation proves boot-database immutability | Compatible extension |
| Inactive capacity | No equivalent 233,240-byte digest tail on this path | Current implementation hashes and queries every fixed-capacity frame slot | Compatible extension with measured implementation defect |
| Ready deadline | Boot inhibition/order prevents dependents from outrunning VM | One explicit guest-counter deadline fails incomplete VM startup | Required adaptation |

No MINIX behavior, authority transition, or failure result must change.

## Rejected hypotheses

### The frame-ownership correction regressed

The captured program counter is in the user VM ELF, not the merged kernel
frame validator. The frame correction remains independently validated.

### CPython, OpenSSL, or validation reuse changed target behavior

The producer and consumer used the same target images. The Python runtime only
constructed and observed the QEMU process; it did not alter guest VM state.

### A stale or rebuilt ELF changed the outcome

The failing target executed with `[0/1]`, so no object or ELF was rebuilt.
Image hashes remained fixed.

### VM was deadlocked

The captured `sepc` is inside an active loop and the guest reached its own
deadline and panic path.

### The 10-second timeout is simply too small

The same image sometimes finishes within it. Increasing the timeout would move
the threshold while retaining 233,240 inactive helper calls and linear zero
hash work. It would not correct the cause.

### Build with optimization

The project requires Debug target evidence. Depending on optimizer inlining or
vectorization would leave the rejected source work shape intact and make
correctness depend on a build-mode accident.

## Required correction

The correction is defined by Accepted
[ADR-0077](../adr/0077-bounded-complete-vm-boot-database-validation.md):

- preserve the exact digest, state, API, ABI, capacity, and invariant results;
- reject zero or over-capacity coherent header/summary managed counts before
  any count-derived pointer, index, loop, mask, or subtraction;
- hash the immutable prefix in bounded byte chunks;
- canonicalize active frame states from one loaded initial-free bitmap word
  per 64-frame chunk;
- check the inactive frame-state tail directly for zero;
- check the partial inactive bitmap boundary and every full inactive word
  contiguously from the exact first fully inactive word;
- advance the unchanged FNV digest across the proven zero tail with
  logarithmic exponentiation rather than one multiply per byte;
- retain complete runtime validation on every existing call site;
- add semantic boundary/corruption guards and one deterministic structural
  work-shape Red; and
- require the exact no-retry VFS-to-ELF-spawn sequence plus complete producer
  and reuse evidence.
