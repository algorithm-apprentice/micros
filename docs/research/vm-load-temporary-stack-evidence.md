# VM Load Temporary Stack Evidence

## Provenance and observed failure

This is the documentation-only corrective prerequisite for EV03 under
[Accepted ADR-0087](../adr/0087-phase-bounded-vm-load-temporaries.md).
The inspected `micros` HEAD is
`b80c543a9ecb89f199f5853db5eafddd1e15417d`.
The blocked uncommitted implementation has tree
`fb4b9d3f54d6f1ab47941f9342eb26d1d12e86df` and source SHA-256
`b1c5fe62faef19d60feaa28f97c1df5cbe85b54ad822f3021b72612856ca8bf6`.
Its preserved patch SHA-256 is
`d3ac300438b5832d98be4c6a0e0e98c9b436e73342a0f0973f60b1aa48251311`.
These identify historical failure evidence, not current acceptance.

Actual selected RV64 Clang 23.1.4 objects in
`riscv64-tty-service-test` produced:

| Function | Metadata bytes | Observed bytes |
| --- | ---: | ---: |
| `micros_vm_load_prepare` | 1616 | 1696 |
| `prepare_binary_plans` | 96 | 176 |
| `vm_core_validate_state` | 128 | 160 |
| `vm_core_transaction_common_is_canonical` | 48 | 80 |
| `vm_core_transaction_plans_are_canonical` | 576 | 608 |

Both `.su` and relocated `.stack_sizes` agree. The complete linked VM
startup graph has a metadata bound of 3,664 bytes and observed bound of
4,240 bytes. The unchanged user capacity is 4,096 bytes. The observed
graph failed before the TTY QEMU scenario was launched.

The metadata artifacts predate the complete blocked source seal. Their
unchanged VM source/object comparison is diagnostic historical evidence, not
a same-candidate EV03 pass. The observed excess remains independently
established on its actual source/object/linked chain.

The relevant unchanged source SHA-256 values are:

```text
servers/vm/vm_load.c
234f26064004ce4bdf2995b3c97e955349a675ac9a5eb06ba2ac2208f065d68a

servers/vm/vm_validation.c
dd744d6ca649a1501b5ecb1cb53b4ac67db4cf25e027c6f145bdb5ea13677f95
```

The observed witness is:

| Source/function | Frame bytes |
| --- | ---: |
| `lib/runtime/start.S:_start` | 0 |
| `tests/qemu/vm_server.c:micros_service_main` | 96 |
| `tests/qemu/vm_server_control.c:vm_server_serve_control` | 144 |
| `servers/vm/vm_service_core.c:micros_vm_service_handle_message` | 224 |
| `servers/vm/vm_service_load.c:vm_service_load` | 192 |
| `servers/vm/vm_load.c:micros_vm_load_prepare` | 1696 |
| `servers/vm/vm_validation.c:vm_core_validate_state` | 160 |
| `servers/vm/vm_validation.c:vm_core_transaction_common_is_canonical` | 80 |
| `servers/vm/vm_validation.c:vm_core_transaction_plans_are_canonical` | 608 |
| `servers/vm/vm_stack.c:micros_application_stack_plan` | 288 |
| `servers/vm/vm_stack.c:vm_image_decode_and_plan_stack` | 112 |
| `lib/spawn/spawn_request.c:micros_spawn_request_decode` | 240 |
| `lib/spawn/spawn_request.c:spawn_request_decode_strings` | 112 |
| `lib/spawn/spawn_request.c:spawn_request_decode_string_reference` | 176 |
| `lib/spawn/spawn_request.c:spawn_request_string_bytes_are_canonical` | 112 |
| `tests/stack/observer.S:__cyg_profile_func_exit` | 0 |
| **Total** | **4240** |

This is a simultaneous-frame upper-bound failure, not a reproduced write
outside a live stack. A success transcript for another input cannot waive it.

## Fixed MINIX comparison

The local reference remains MINIX
`4db99f4012570a577414fe2a43697b2f239b699e`; it was read locally, not copied
from a web source. The earlier behavioral study in
[the VM source-organization note](minix-vm-service-source-organization.md)
is supplemented by the later
[B07 structural audit](minix-complete-baseline-b07-vm.md), which remains
authoritative for its unresolved divergences.

| Fixed source | Behavioral, structural, and storage fact |
| --- | --- |
| `minix/lib/libexec/exec_elf.c:58-75` | Header unpacking uses bounded pointer/scalar locals and returns checked header/program-header views |
| `minix/lib/libexec/exec_elf.c:127-173` | `libexec_load_elf` retains one caller-owned `exec_info`, validates program file ranges before clearing the process, then performs segment work |
| `minix/lib/libexec/exec_elf.c:171-245` | Per-segment address/range/copy decisions use scalar locals and the retained program-header view, not a duplicate full VM validation transaction |
| `minix/lib/libexec/exec_general.c:21-50` | Allocation adapters delegate mapping through the existing process endpoint and return explicit failure |
| `minix/lib/libexec/exec_general.c:54-94` | Clearing/copying and PM coordination remain separate operations over the passed execution descriptor |

MINIX's `exec_info` and callbacks do not transfer pointer authority into
`micros`. The fixed RV64 ELF subset, snapshots, direct grants, automatic
failure-atomic candidate, and kernel-mediated scratch/freeze remain the
Accepted adaptations. No reference source, test, identifier inventory, or
assembly is imported.

With `p` program headers and `b` image bytes, the referenced loader performs
`O(p)` header/segment traversal plus the delegated allocation/copy work,
including `O(b)` byte movement; its local auxiliary workspace is `O(1)`
in those dimensions. No recursive load traversal is introduced. Callback
stack depth belongs to the invoked owner, not an inferred zero-cost call.

`micros` retains bounded tables, a transaction candidate and temporary
ELF/stack plans. Its current repeated complete validation has the additional
cost recorded by B07-F001/F002. This correction changes neither that cost
nor the algorithms that B07 requires later work to replace.

## Lifetime and complexity mapping

Let `U` be the already live service caller chain, `P` the preparation
workspace frame, `V` the maximum complete-validation chain, and `B` the
maximum preparation subcall chain. The old structure conservatively needs
`U + P + max(V, B)` even after preparation values have been published.

The required structure has a small facade frame `F` and an independently
emitted preparation helper frame `P'`. Its bound is
`U + F + max(V, P' + B)`. The helper returns before the final `V`;
it is not a new stack owner and contributes its full frame while active.
These expressions specify simultaneous lifetime, not predicted measured
byte values.

The original initial validation, source checks, binary planning, lowest-frame
selection, input zero/copy, transaction publication, phase write, and final
validation remain in their exact order. The extra private C call adds
constant work. Average, worst-case, and amortized asymptotic work are
unchanged, including all pre-existing validation costs.

Preparation still has `O(Pg + A)` automatic workspace in the fixed page/
argument capacities represented by its original arrays and plans.
Persistent storage is unchanged. No allocation, shared workspace, recursion,
TLS, or second transaction owner is added.

| Difference | ADR-0080 classification |
| --- | --- |
| RV64 fixed ELF/stack and isolated grant/scratch authority | Required adaptation retained from ADR-0050 |
| Failure-atomic local plans and exact publication | Compatible extension retained; no new state or failure order |
| Separate automatic preparation lifetime | Compatible extension: same algorithm/order, constant additional call, reduced simultaneous storage overlap |
| Complete validation cadence, frame selection and movement divergences | Divergence requiring correction; still owned by B07's VM01-VM06 tasks |

## Required next evidence

The design authors no executable spike. After separate Acceptance and merge,
the restarted implementation first reproduces the overlap failure, then
checks exact source-phase preservation and actual frame release.
Native load/failure/rollback cases and the existing integration gates remain
required. Full ordinary/observed graph and target evidence across all current
EV03 scopes, not just this witness, must pass before EV04 can open.
