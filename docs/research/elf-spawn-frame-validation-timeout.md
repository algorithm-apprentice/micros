# ELF Spawn Frame-Validation Timeout Root Cause

## Scope

This note records the read-only investigation that blocked the CPython 3.15
host-runtime implementation. It does not authorize a retry, timeout increase,
QEMU-runner relaxation, or production change.

The investigation used:

- the exact `test-qemu-elf-spawn` command from the complete PR plan;
- the generated `riscv64-elf-spawn-test` kernel and map;
- QEMU's monitor to freeze the hart and inspect registers/stack memory;
- `llvm-addr2line`, `llvm-nm`, and target disassembly;
- current `micros` source and Accepted ADRs; and
- local MINIX commit
  `4db99f4012570a577414fe2a43697b2f239b699e`.

No GitHub or web copy of MINIX was used.

## Symptom

The same clean candidate and exact kernel image sometimes completed and
sometimes crossed the 180-second timeout. Failed runs stopped after different
UART records:

- ownership readiness only;
- one or two parent I/O records;
- child I/O;
- the final pass marker; or
- the pass marker followed by no clean QEMU exit.

The image SHA-256 during diagnosis was:

```text
d74a393591f10d460860ee09f514dab34eb732bf8028a478b2fc89f721f32aa6
```

A successful run and failed runs used that unchanged image. This ruled out
source drift, stale generated fixture bytes, and a build-directory mismatch.

The pass marker alone was not sufficient evidence: it is written before the
test-only VFS drain and final kernel report. A timed-out run therefore cannot
be accepted even when that marker is visible.

## Five Whys

### 1. Why did the host report a QEMU timeout?

The guest did not reach final SBI shutdown within 180 seconds.

### 2. Why did the guest not reach final shutdown?

It was still making slow progress through application/service syscalls. The
last visible UART line varied because the next synchronous operation had not
returned before the wall-clock limit.

### 3. Why were ordinary syscalls taking so long?

Two independent monitor captures stopped the hart during a user `ecall` at:

```text
micros_frame_ownership_allocator_bit_is_set
  <- micros_frame_ownership_validate
  <- micros_frame_ownership_runtime_validate
  <- load_validation_context
  <- micros_user_address_space_validate_locked
  <- micros_user_address_space_translate
```

The hart was executing kernel validation, not idle, deadlocked, or waiting in
QEMU.

### 4. Why does one address translation execute so much validation work?

ADR-0040 correctly requires complete root, owner, reachability, ledger, and
shared-root validation before translation. The current ledger implementation
scans the fixed maximum frame capacity on every complete validation.

The observed image reported:

```text
managed frames: 0x70e8 = 28,904
fixed capacity:  0x40000 = 262,144
inactive tail:              233,240
inactive share:              88.97%
```

Captured `frame_index` values in the validator included 108,764 and 219,920,
both far beyond `managed_frame_count`.

### 5. Why does the complete scan cross the integration timeout?

The target is a Debug build. Disassembly shows that each inactive frame calls:

```text
micros_frame_owner_is_equal
micros_frame_ownership_allocator_bit_is_set
```

The second call reloads the same bitmap word for up to 64 adjacent inactive
frames. ELF spawn performs many IPC-buffer, grant-copy, PM, VFS, and application
translations, multiplying this fixed-capacity scan until host-speed variation
decides whether the wall-clock threshold is crossed.

The root cause is therefore the instruction shape of complete frame-ledger
validation on a high-frequency translation path. The timeout is a threshold
effect, not permission to treat the test as probabilistic.

## MINIX behavioral baseline

The local MINIX reference was read with:

```text
git -C /Users/alephling/Repo/minix show \
  4db99f4012570a577414fe2a43697b2f239b699e:\
minix/kernel/arch/i386/memory.c

git -C /Users/alephling/Repo/minix show \
  4db99f4012570a577414fe2a43697b2f239b699e:\
minix/kernel/system/do_safecopy.c
```

The traced safe-copy path is:

1. `verify_grant()` validates the selected grant and range;
2. `virtual_copy_f()` resolves the selected source and destination processes;
3. `lin_lin_copy()` iterates only the requested byte range;
4. `createpde()` maps each selected source/destination chunk; and
5. `PHYS_COPY_CATCH` copies the chunk or returns the selected source/destination
   fault.

The separate `vm_lookup()` and `vm_lookup_range()` entry points also inspect
only selected mappings and ranges, but they are not called by
`virtual_copy_f()`. MINIX does not scan a global physical-ownership table
before each copy.

## Difference classification

| Topic | MINIX baseline | `micros` behavior | Classification |
| --- | --- | --- | --- |
| Selected-range copy | Validate the selected grant, processes, mapped chunks, and bytes | Validate and copy the selected bounded user range | Baseline parity |
| Mapping mechanism | i386 `createpde()` plus `PHYS_COPY_CATCH` | RISC-V Sv39 walk, exact PTE permissions, and bounded physical chunks | Required adaptation |
| Global typed ownership | No equivalent complete typed ledger scan per copy | Complete generation-safe allocator/owner validation precedes translation | Compatible extension |
| Inactive storage | No fixed 262,144-entry typed tail on this path | Every call checks every inactive owner, target, and bit | Compatible extension with measured implementation defect |

The correction must preserve the compatible safety extension while changing
only its internal execution shape.

Separately, retrying a failed host gate until lucky is a development-evidence
defect. It is prohibited by the repository workflow, not classified as a
MINIX runtime difference.

## Rejected hypotheses

### CPython 3.15 subprocess behavior

Both CPython 3.9 and CPython 3.15 produced successful runs. CPython 3.15 also
produced failed and successful runs with the same image. Interpreter startup
and pipe handling were not the guest-side cause.

### OpenSSL or compiler environment

Runs with only OpenSSL isolation, only compiler pins, only
`MICROS_PYTHON`, the complete profile environment, and a stripped environment
all reached successful executions. No one host variable explained the stall.

### Stale build output

A clean deletion and rebuild of only
`build/riscv64-elf-spawn-test` reproduced the timeout. Generated fixture and
seed bytes were deterministic.

### A single VFS/TTY drain race

Some failures stopped before any parent application output; others stopped
after child or pass output. The common point was not one protocol state but
the repeated translation/validation path used by all those calls.

### Guest deadlock

Monitor captures showed the hart actively retiring instructions inside the
validator. The system was slow, not quiescent.

## Required correction

The correction is defined by Accepted
[ADR-0076](../adr/0076-bounded-complete-frame-ownership-validation.md):

- keep complete validation and current public errors;
- remove per-frame allocator-bit queries from active and inactive scans;
- validate the inactive bitmap by words;
- eliminate out-of-line calls from the inactive owner/handoff loop;
- keep every inactive entry and bit checked;
- add semantic equivalence and structural work-shape regressions; and
- require the exact VFS-then-ELF-spawn reproducer and complete producer to pass
  without retry.
