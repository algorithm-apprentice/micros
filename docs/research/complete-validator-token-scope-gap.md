# Complete-Validator Token Scope Gap

## Fixed references and observed boundary

This is evidence for the Accepted corrective
[ADR-0104](../adr/0104-precise-complete-validator-token-authority.md), not a
new runtime implementation or a completed MEM03 outcome.

```text
fixed runtime: 8927f9956eb270962517b19715a12bc93a5fa466 (#214)
design base:   97459f6566ce8312760cdd6c5a37507e2b15d7bd (#215)
design tree:   6c4f243371ecb50b688f8d66adc43cb472f22585
blocked tree:  59599cc5bc6e8a8299ed0e2e3ecddd29ee524b74
```

The first uncommitted MEM03 implementation preserved all original complete
diagnostic bodies. It recorded actual Red before production edits, then
current Green for all three work oracles, 99 valid cases, 495 snapshots and
the ordinary control. All twelve B01-B12 QEMU commands passed in
recertification. B14 ran 267 tests in 3,783.592 seconds; 266 passed and this
one failed:

```text
tests.host.test_frame_validation_hot_path.FrameValidationHotPathTests.
test_complete_validation_uses_bitmap_word_bounded_scans
AssertionError: validator work shape differs from reviewed identity
```

The recertification command exited two and published no terminal event.
The implementation was preserved as a reference stash with the exact blocked
tree and the branch `archive/mem03-prefix-evidence-blocked`; it has no
permanent implementation commit or accepted pull request.

## Actual selector behavior

In the fixed test owner,
`tests/host/test_frame_validation_hot_path.py:function_source` locates a
function name and passes `segment_start = 0` to
`c_signature_start`. The latter uses blank lines to delimit a signature.
`clang_preprocessed` invokes the existing RISC-V C17 Clang preprocessing
command with `-E -P`.

For these inputs, the resulting selected span starts with:

```text
void micros_frame_ownership_clear_bytes(void *storage, size_t size);
void micros_frame_ownership_copy_bytes(
```

It contains private declarations plus the preceding lookup/count functions,
not merely the named helper. The inactive span additionally includes the
active helper. Changes to MEM03's private declarations and ordinary paths
therefore invalidate both nominal helper identities.

## Minimal reproduction results

The author used the existing preprocessing/token helpers and correct
`c_source_records.function_records` inspector. Fixed headers were generated
from the SHA-bound #214 native input archive in a temporary directory;
current headers were the blocked candidate. No production source or expected
hash was edited by the probe.

| Source / headers | Legacy active span bytes | Legacy inactive span bytes |
| --- | ---: | ---: |
| Fixed / fixed | 7,272 | 8,680 |
| Fixed / current | 7,572 | 8,980 |
| Current / current | 7,474 | 8,882 |

Fixed source and fixed headers reproduce the original literal identities:

```text
ACTIVE_TOKENS_SHA256:
d2abefb28aa1d0de7d2a5eb15badea0319d9da9b5689c3bf29faebe3c6cb43ca

INACTIVE_TOKENS_SHA256:
a6ce4e4e0f0b143d874094b5ace3e1fbcd7ff4238bbff8bfaae46a02e3235887
```

The correct complete signature/body selections are 2,467 and 1,407 bytes.
Their token digests are identical in all three combinations:

```text
validate_active_frames:
d023646f0a6a0fe5bd3f5bfca640ae3ef72e5a3837a4ac00936bbdc1701de320

inactive_state_is_zero:
b5696c8cd0c3f62b187bcb771b68c7901a598b347a71eb202a10a8fc2072ee5a
```

The raw unpreprocessed bodies also remain identical. Their SHA-256 values are:

```text
validate_active_frames:
29ad541c236ad63b119b1a047bab4e0842a3bcaaa0e6a701a03b1d409e23fbc3

inactive_state_is_zero:
f27dc2ad6b12ab0932bdd1ec08eaec97da9b9eb84f11c3ec6660d8ec6154b494
```

Thus the failure is a source-authority scope defect, not a changed complete
scan. The original hashes are not replaced with the candidate's differing
prefix hashes.

## Actual affected identity closure

Both additional keys are `TRANSPORT_IDENTITY` records:

```text
tests/host/test_frame_validation_hot_path.py:data-entry:ACTIVE_TOKENS_SHA256
tests/host/test_frame_validation_hot_path.py:data-entry:INACTIVE_TOKENS_SHA256
```

Each has primary unit B02, consumers B02/B14 and exactly these payloads:

```text
kernel/memory/frame_ownership_validation.c
tests/host/test_frame_validation_hot_path.py
```

The existing `python-ast:assert_word_bounded_scans` record is not retired.
Its owner, original token-call path and complete current AST checks remain.
No shared C inspector, compiler policy, build parser or other source
authority belongs in the added candidate closure.

The Accepted correction adds two keys and one frozen adapter, not another
runtime subject. The resulting exact envelope and 25/5/10 coverage are
specified in the [MEM03 contract](../architecture/selected-frame-ownership.md).

## Baseline classification

The fixed MINIX reference remains
`4db99f4012570a577414fe2a43697b2f239b699e`. Selected metadata authority and
separate diagnostics remain the unchanged
[MEM03 baseline](minix-selected-frame-ownership.md). This correction is a
compatible evidence extension: it restores the intended helper boundary
without changing behavior, algorithms, complexity, stack or storage.

Host-side function-record selection remains bounded by the input translation
unit and hashes only the selected helper tokens. No measurement is inferred
from timing. The retained current Clang AST proof and real primitive-work
tests remain separate evidence.
