# GitHub Actions CI Baseline

## Scope and fixed reference

This is development-execution research for the user's requested GitHub
Actions CI, not a MINIX runtime subsystem or a new OS dependency.
The initial project base is merged EV03 #184,
`4668f47bafd9fe6f9e4791fe104a511c499547e6`.

The local MINIX reference remains
`4db99f4012570a577414fe2a43697b2f239b699e`.
The existing [validation-driver study](minix-validation-driver-baseline.md)
is supplemented by read-only inspection of:

- `minix/tests/Makefile:23-27,78-103`: explicit test identities, architecture
  membership, source dependencies and named programs;
- `minix/tests/run:49-91`: explicit selection, executable inventory and
  nonempty-run admission;
- `minix/tests/run:111-146`: privilege-dependent execution and per-test status;
  and
- `minix/tests/run:148-179`: ordered execution, retained failures and final
  reporting.

No MINIX source, shell expansion, cleanup, privilege switch or runner is
copied or executed.

## Behavior, structures and complexity

MINIX retains a named test list, scalar counters and failed-test text. It
scans the selected list to count runnable tests and again to execute/report
them. Selection/control work is linear in the selected test count, excluding
test execution. Repeated failed-name concatenation can copy quadratically
many characters over a growing failure list. Its shell stack is not an OS
stack-bound oracle.

`micros` already owns its ordered canonical command list, structured per-command
results and separate complete logs. CI delegates to that same implementation;
it does not replace its selection or result algorithm. Added orchestration is
a fixed number of phases plus linear log/artifact transport. The helper uses
bounded host/tool/identity metadata and no target stack storage.

| Reference property | Classification | Hosted consequence |
| --- | --- | --- |
| Named tests and explicit dependency order | Baseline parity | Preserve canonical workflow identity and ordering |
| Hosted POSIX test processes versus cross-built RV64/QEMU | Required adaptation | Run existing native, target, image and stack gates unchanged |
| Per-test failure reporting | Baseline parity | Preserve planner statuses and complete logs |
| Missing/root-only tests may be skipped | Divergence requiring correction | No selected `micros` gate may silently skip |
| Exact Git source, tool and artifact identity | Compatible extension | Preserve existing current-candidate evidence |
| Ephemeral remote runner and read-only token | Required adaptation | Add execution hosting, not kernel/service authority |

The runtime correction DAG, B01-B14 finding ownership, BASE01, STACK13 and
RECERT01 remain unchanged.

## Current project evidence

EV03's author-validated tree was
`fae00197363894971b0304bd8a0d8012b1ed3809`; publication and squash preserve it.
The complete authoritative plan satisfied 48 commands, with 46 freshly run
and two exact-current receipts validly reused under ADR-0064.
The underlying native result was 37 CTests and 1,212 Python cases.

The fresh stack aggregate checked 42 configurations, 111 ELF outputs and
ten observed scenarios. Its 35,548,530-byte bundle has SHA-256
`23294281a0b736581998adad92eba0397c766b37af53207504e50a79b8b4121b`.
The full spawn VM's actual/static use is 3,744 bytes of the unchanged 4 KiB
stack. B13 is explicitly blocked rather than certified.

These are local author results, not a GitHub-hosted run or a promise about
hosted elapsed time. Read-only GitHub API inspection on 2026-11-01 found the
repository public, Actions enabled, and zero workflows.

## Why the initial runner is macOS arm64

The existing reusable profile and fatal-stack default-firmware resolver bind
Darwin arm64, Homebrew Cellar/configuration layout, LLVM major 23 and QEMU
major 11. ADR-0075 admits final GIL-enabled CPython 3.15 patch releases.
ADR-0086 pins exact OpenSBI bytes. Unsupported hosts/firmware are not
equivalent execution contexts.

The official
[GitHub-hosted runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
lists standard `macos-26` arm64 runners with three M1 vCPUs, 7 GB RAM and
14 GB SSD. Standard hosted compute is free for public repositories; artifact
storage still needs bounded retention. Each job receives a fresh VM.
QEMU uses the existing software-emulated RV64 target, not nested hardware
virtualization.

The inspected
[macOS 26 arm64 image declaration](https://github.com/actions/runner-images/blob/main/images/macos/macos-26-arm64-Readme.md)
reports image `20261026.0455.2`, macOS 26.6.2, Apple Clang 21, Homebrew,
Python 3.15.1, and Homebrew LLVM 20. The preinstalled LLVM is therefore not
the target compiler; the job must select the major-23 formula explicitly.
The runner label selects the OS/architecture family, not immutable VM bytes.

Official Homebrew formula records inspected on 2026-11-01 provide Apple
Silicon Tahoe bottles for:

- [Python 3.15](https://formulae.brew.sh/formula/python@3.15): 3.15.2;
- [LLVM](https://formulae.brew.sh/formula/llvm): 23.1.4, also named `llvm@23`;
- [LLD](https://formulae.brew.sh/formula/lld): 23.1.4, also named `lld@23`; and
- [QEMU](https://formulae.brew.sh/formula/qemu): 11.1.3.

The declared families remain fixed while supported patch releases are checked
and recorded. The implementation does not invent a package snapshot,
downgrade mechanism or Linux firmware exception.

The [actionlint formula](https://formulae.brew.sh/formula/actionlint) provides
version 1.7.12 and ShellCheck-backed validation on the selected runner. This
small CI-specific linter avoids adding a YAML parser or general workflow
interpreter to the project's native evidence.

## Action and event evidence

GitHub API inspection verified these tags resolve directly to commits in the
official repositories:

- [checkout v7.0.1](https://github.com/actions/checkout/tree/3d3c42e5aac5ba805825da76410c181273ba90b1):
  `3d3c42e5aac5ba805825da76410c181273ba90b1`;
- [upload-artifact v7.0.2](https://github.com/actions/upload-artifact/tree/cf430e030ddbb5b0abf93d22962f4752f3646cd9):
  `cf430e030ddbb5b0abf93d22962f4752f3646cd9`.

The checked action definitions use Node 24. Checkout documents full history
with `fetch-depth: 0` and disabling retained credentials. Artifact upload
supports explicit paths, no-overwrite, failure on missing input, compression
and bounded retention.

GitHub's
[event contract](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows)
defines pull-request merge-ref execution and the need for a published
workflow. Its
[secure-use guidance](https://docs.github.com/en/actions/reference/security/secure-use)
supports least-privilege tokens, full action-source pinning and avoiding
privileged untrusted-checkout triggers. The initial job requires neither
`pull_request_target` nor `workflow_run`.

## Publication dependency

An unpublished workflow cannot produce its own hosted acceptance result.
The design therefore separates a complete local/publication review from
final implementation acceptance after the real PR run. Neither checkpoint
waives review before commit or a successful remote gate before merge.
The exact bounded exception is Accepted
[ADR-0088](../adr/0088-github-actions-validation.md). Only its separately
authorized implementation phase may create the hosted workflow.
